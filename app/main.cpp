#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Non-blocking keyboard polling for confirm mode's ~2s reply-confirmation window. Deliberately
// app/-local #ifdef rather than a formal hal/ seam: confirm mode's keyboard concept is PC-CLI
// only (app/ never compiles for a future radio target, which would use a touchscreen/buttons
// instead anyway) -- see .claude/state/context.md's Phase 3 entry for why this was asked and
// decided rather than just matching hal_audio/hal_cat/hal_time's existing per-platform-file
// pattern. The POSIX path is written blind, like the rest of this project's POSIX code -- no
// Linux box exists on this dev machine, first real proof is CI.
#ifdef _WIN32
#include <conio.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "config.h"
#include "wav_decode.h"
#include "band_table.h"
#include "tx_playback.h"

extern "C" {
#include "argus.h"
#include "atropos.h"
#include "cerberus.h"
#include "hal_audio.h"
#include "hal_cat.h"
#include "hal_time.h"
#include "horae.h"
#include "qso.h"
#include "ring.h"
#include "sym_types.h"
#include "tx.h"
#include <common/wave.h>
}

// Hamlib's own headers already self-guard with `#ifdef __cplusplus extern "C" {` --
// deliberately not nested inside the block above.
#include <hamlib/rig.h>

namespace {

constexpr const char* kVersion = "0.0.0-phase1";
std::atomic<bool> g_stop{ false };

void on_sigint(int)
{
    g_stop = true;
}

#ifdef _WIN32
void enable_raw_keyboard_mode() { }
void restore_keyboard_mode() { }

// Non-blocking: true iff a key was pressed since the last check (and consumes it).
bool key_was_pressed()
{
    if (_kbhit()) {
        (void)_getch();
        return true;
    }
    return false;
}
#else
termios g_saved_termios{};

void enable_raw_keyboard_mode()
{
    tcgetattr(STDIN_FILENO, &g_saved_termios);
    termios raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void restore_keyboard_mode()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

bool key_was_pressed()
{
    int ch = getchar();
    return ch != EOF;
}
#endif

// Runs on miniaudio's realtime capture thread (hal/audio_miniaudio.c) -- must not block,
// allocate, or do I/O, per hal_audio.h's own contract. Handing samples to the ring is the
// only thing it does; the decode-thread loop in main() below is the consumer.
void capture_callback(const float* samples, uint32_t frame_count, void* user)
{
    sym_ring_t* ring = static_cast<sym_ring_t*>(user);
    sym_ring_write(ring, samples, frame_count);
}

// std::to_string(double) always pads to 6 decimal places ("0.300000") -- this trims trailing
// zeros (and a trailing '.') for the banner text in autonomous_mode() below.
std::string format_minutes(double minutes)
{
    std::string s = std::to_string(minutes);
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        s.erase((last == dot) ? last : last + 1);
    }
    return s;
}

std::string find_default_capture_device_name()
{
    hal_audio_device_t devices[32];
    size_t device_count = 0;
    hal_audio_enumerate(false, devices, 32, &device_count);
    for (size_t i = 0; i < device_count; ++i) {
        if (devices[i].is_default) {
            return devices[i].name;
        }
    }
    return (device_count > 0) ? devices[0].name : "(none found)";
}

std::string find_default_playback_device_name()
{
    hal_audio_device_t devices[32];
    size_t device_count = 0;
    hal_audio_enumerate(true, devices, 32, &device_count);
    for (size_t i = 0; i < device_count; ++i) {
        if (devices[i].is_default) {
            return devices[i].name;
        }
    }
    return (device_count > 0) ? devices[0].name : "(none found)";
}

void print_decode_line(int hour, int min, int sec, const std::string& text, float freq_hz, float time_s, int score)
{
    // score*0.5 is ft8_lib's own rough SNR stand-in (see demo/decode_ft8.c) -- no real SNR
    // estimate exists yet, see .claude/state/context.md.
    std::printf("%02d%02d%02d %+05.1f %+4.2f %4.0f ~  %s\n",
        hour, min, sec, (double)(score * 0.5f), (double)time_s, (double)freq_hz, text.c_str());
}

int list_devices()
{
    hal_audio_device_t devices[32];
    size_t device_count = 0;

    hal_audio_enumerate(false, devices, 32, &device_count);
    std::cout << "Capture devices (--device):\n";
    for (size_t i = 0; i < device_count; ++i) {
        std::cout << (devices[i].is_default ? "* " : "  ") << devices[i].name << "\n";
    }
    if (device_count == 0) {
        std::cerr << "No capture devices found.\n";
    }

    hal_audio_enumerate(true, devices, 32, &device_count);
    std::cout << "\nPlayback devices (--playback-device, armed/beacon only):\n";
    for (size_t i = 0; i < device_count; ++i) {
        std::cout << (devices[i].is_default ? "* " : "  ") << devices[i].name << "\n";
    }
    if (device_count == 0) {
        std::cerr << "No playback devices found.\n";
    }

    return 0;
}

// Best-effort: WSJT-X's own saved-audio files and the ft8_lib WAV corpus both name files
// ..._HHMMSS.wav (or start with it) -- pull that out for a readable slot-time column when
// decoding a file rather than listening live. Falls back to all-question-marks rather than
// guessing if the filename doesn't match.
std::string guess_hhmmss_from_filename(const std::string& wav_path)
{
    std::smatch m;
    static const std::regex kPattern(R"((\d{6})\.wav$)", std::regex::icase);
    if (std::regex_search(wav_path, m, kPattern)) {
        return m[1].str();
    }
    return "??????";
}

int decode_wav_mode(const std::string& wav_path)
{
    std::string hhmmss = guess_hhmmss_from_filename(wav_path);
    int hour = 0, min = 0, sec = 0;
    if (hhmmss != "??????") {
        hour = std::stoi(hhmmss.substr(0, 2));
        min = std::stoi(hhmmss.substr(2, 2));
        sec = std::stoi(hhmmss.substr(4, 2));
    }

    std::vector<WavDecode> decodes = decode_wav_file(wav_path);
    for (const auto& d : decodes) {
        print_decode_line(hour, min, sec, d.text, d.freq_hz, d.time_s, d.score);
    }
    std::cout << decodes.size() << " decode(s) from " << wav_path << "\n";
    return 0;
}

// Phase 2, no keying: synthesizes `text` with core/tx.c and writes it straight to a WAV
// file -- no audio device, no CAT, no PTT touched anywhere. Round-trips through
// --decode-wav for a quick sanity check: `symbolon --tx-test "CQ KC5CD EM12" out.wav &&
// symbolon --decode-wav out.wav` should print the same text back.
int tx_test_mode(const std::string& text, const std::string& wav_path, float freq_hz)
{
    const int sample_rate_hz = 12000;
    const int num_samples = sym_tx_signal_samples(sample_rate_hz);
    std::vector<float> signal((size_t)num_samples);

    if (sym_tx_synthesize(text.c_str(), freq_hz, sample_rate_hz, signal.data()) != SYM_TX_OK) {
        std::cerr << "Failed to encode \"" << text << "\" as an FT8 message (not a valid standard, nonstandard, or free-text message?)\n";
        return 1;
    }
    if (save_wav(signal.data(), num_samples, sample_rate_hz, wav_path.c_str()) != 0) {
        std::cerr << "Failed to write " << wav_path << "\n";
        return 1;
    }

    std::cout << "Wrote " << num_samples << " samples (" << ((float)num_samples / (float)sample_rate_hz)
               << "s) at " << freq_hz << " Hz to " << wav_path << "\n";
    return 0;
}

bool parse_cat_mode(const std::string& s, hal_cat_mode_t* out_mode)
{
    if (s == "USB") { *out_mode = HAL_CAT_MODE_USB; return true; }
    if (s == "LSB") { *out_mode = HAL_CAT_MODE_LSB; return true; }
    if (s == "DATA-U") { *out_mode = HAL_CAT_MODE_DATA_U; return true; }
    if (s == "DATA-L") { *out_mode = HAL_CAT_MODE_DATA_L; return true; }
    if (s == "CW") { *out_mode = HAL_CAT_MODE_CW; return true; }
    return false;
}

bool parse_cat_agc(const std::string& s, hal_cat_agc_t* out_agc)
{
    if (s == "off") { *out_agc = HAL_CAT_AGC_OFF; return true; }
    if (s == "slow") { *out_agc = HAL_CAT_AGC_SLOW; return true; }
    if (s == "fast") { *out_agc = HAL_CAT_AGC_FAST; return true; }
    if (s == "auto") { *out_agc = HAL_CAT_AGC_AUTO; return true; }
    return false;
}

const char* cat_mode_name(hal_cat_mode_t mode)
{
    static const char* kModeNames[] = { "USB", "LSB", "DATA-U", "DATA-L", "CW" };
    return kModeNames[mode];
}

const char* cat_agc_name(hal_cat_agc_t agc)
{
    static const char* kAgcNames[] = { "off", "slow", "fast", "auto" };
    return kAgcNames[agc];
}

struct CatOptions {
    std::string port;
    double set_freq_hz = 0.0;
    std::string set_mode; // empty = don't set
    int set_preamp = -1;  // -1 = don't set, 0 = off, 1 = on
    std::string set_agc;  // empty = don't set
    double set_power_w = -1.0; // <0 = don't set
};

// Phase 2, no keying: opens real CAT via Hamlib, applies any requested settings (frequency,
// mode, preamp, AGC, TX power -- none of these are PTT, no RF is emitted by any of them),
// then prints the rig's current status. hal_cat_set_ptt() exists and works (see
// tests/hal/test_cat_hamlib.c against RIG_MODEL_DUMMY), but nothing in this CLI calls it;
// per the kickoff's "never key the antenna first" ordering, that's Phase 4's job, after
// core/atropos.c's watchdog exists.
int cat_info_mode(const CatOptions& opts)
{
    hal_cat_config_t cfg{};
    cfg.port = opts.port.c_str();
    cfg.baud = 19200; // X6200 SERIAL-B via the CH342 USB bridge, per the kickoff's hardware facts
    cfg.rig_model = RIG_MODEL_X6200;
    // Hamlib's X6200 backend has no real tx power table (confirmed against real hardware,
    // see hal_cat.h's field comment) -- 8W is the X6200's actual spec-sheet max (12V supply;
    // 5W on its own battery), used here to make the watts<->fraction math accurate instead
    // of trusting Hamlib's broken generic fallback.
    cfg.max_tx_power_watts = 8.0f;

    hal_cat_t* cat = nullptr;
    if (hal_cat_open(&cat, &cfg) != HAL_RC_OK) {
        std::cerr << "Failed to open CAT on " << opts.port << "\n";
        return 1;
    }

    if (opts.set_freq_hz > 0.0) {
        if (hal_cat_set_freq_hz(cat, (uint64_t)opts.set_freq_hz) != HAL_RC_OK) {
            std::cerr << "Failed to set frequency: " << hal_cat_last_error(cat) << "\n";
            hal_cat_close(cat);
            return 1;
        }
    }

    if (!opts.set_mode.empty()) {
        hal_cat_mode_t mode;
        if (!parse_cat_mode(opts.set_mode, &mode)) {
            std::cerr << "Unknown mode \"" << opts.set_mode << "\" (expected USB, LSB, DATA-U, DATA-L, or CW)\n";
            hal_cat_close(cat);
            return 1;
        }
        if (hal_cat_set_mode(cat, mode) != HAL_RC_OK) {
            std::cerr << "Failed to set mode: " << hal_cat_last_error(cat) << "\n";
            hal_cat_close(cat);
            return 1;
        }
    }

    if (opts.set_preamp != -1) {
        if (hal_cat_set_preamp(cat, opts.set_preamp != 0) != HAL_RC_OK) {
            std::cerr << "Failed to set preamp: " << hal_cat_last_error(cat) << "\n";
            hal_cat_close(cat);
            return 1;
        }
    }

    if (!opts.set_agc.empty()) {
        hal_cat_agc_t agc;
        if (!parse_cat_agc(opts.set_agc, &agc)) {
            std::cerr << "Unknown AGC setting \"" << opts.set_agc << "\" (expected off, slow, fast, or auto)\n";
            hal_cat_close(cat);
            return 1;
        }
        if (hal_cat_set_agc(cat, agc) != HAL_RC_OK) {
            std::cerr << "Failed to set AGC: " << hal_cat_last_error(cat) << "\n";
            hal_cat_close(cat);
            return 1;
        }
    }

    if (opts.set_power_w >= 0.0) {
        // Round to the nearest 0.5W step -- the granularity this control is wired for.
        float watts = std::round((float)opts.set_power_w * 2.0f) / 2.0f;
        if (hal_cat_set_power_watts(cat, watts) != HAL_RC_OK) {
            std::cerr << "Failed to set TX power: " << hal_cat_last_error(cat) << "\n";
            hal_cat_close(cat);
            return 1;
        }
    }

    uint64_t freq_hz = 0;
    if (hal_cat_get_freq_hz(cat, &freq_hz) == HAL_RC_OK) {
        std::cout << "Frequency: " << freq_hz << " Hz\n";
    } else {
        std::cerr << "Failed to read frequency: " << hal_cat_last_error(cat) << "\n";
    }

    hal_cat_mode_t mode = HAL_CAT_MODE_USB;
    if (hal_cat_get_mode(cat, &mode) == HAL_RC_OK) {
        std::cout << "Mode: " << cat_mode_name(mode) << "\n";
    } else {
        std::cerr << "Failed to read mode: " << hal_cat_last_error(cat) << "\n";
    }

    bool preamp_enabled = false;
    if (hal_cat_get_preamp(cat, &preamp_enabled) == HAL_RC_OK) {
        std::cout << "Preamp: " << (preamp_enabled ? "on" : "off") << "\n";
    } else {
        std::cerr << "Failed to read preamp: " << hal_cat_last_error(cat) << "\n";
    }

    hal_cat_agc_t agc = HAL_CAT_AGC_OFF;
    if (hal_cat_get_agc(cat, &agc) == HAL_RC_OK) {
        std::cout << "AGC: " << cat_agc_name(agc) << "\n";
    } else {
        std::cerr << "Failed to read AGC: " << hal_cat_last_error(cat) << "\n";
    }

    float power_w = 0.0f;
    if (hal_cat_get_power_watts(cat, &power_w) == HAL_RC_OK) {
        std::cout << "TX power: " << power_w << " W\n";
    } else {
        std::cerr << "Failed to read TX power: " << hal_cat_last_error(cat) << "\n";
    }

    hal_cat_close(cat);
    return 0;
}

// Interactive, run at the bench (not from an automated test runner -- needs a human reading
// the rig's own display): steps hal_cat_set_power_watts() through 0.5W increments from 0.5W
// to 8.0W (the X6200's spec-sheet max, see hal_cat.h's max_tx_power_watts comment) and asks
// what the radio's display shows at each step, to build a real commanded-vs-actual table.
// No PTT is asserted -- setting a power level alone doesn't transmit.
int cat_power_cal_mode(const std::string& port, const std::string& csv_path)
{
    hal_cat_config_t cfg{};
    cfg.port = port.c_str();
    cfg.baud = 19200;
    cfg.rig_model = RIG_MODEL_X6200;
    cfg.max_tx_power_watts = 8.0f;

    hal_cat_t* cat = nullptr;
    if (hal_cat_open(&cat, &cfg) != HAL_RC_OK) {
        std::cerr << "Failed to open CAT on " << port << "\n";
        return 1;
    }

    std::cout << "X6200 TX power calibration -- commands each level below, then asks what the\n"
                 "radio's own display shows. Press Enter alone to skip a step.\n\n";

    std::vector<std::pair<float, float>> readings;
    for (float commanded = 0.5f; commanded <= 8.0f + 0.001f; commanded += 0.5f) {
        if (hal_cat_set_power_watts(cat, commanded) != HAL_RC_OK) {
            std::cerr << "Failed to set " << commanded << " W: " << hal_cat_last_error(cat) << "\n";
            continue;
        }
        std::cout << "Commanded " << commanded << " W -- radio display shows: " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) {
            continue;
        }
        try {
            float displayed = std::stof(line);
            readings.push_back({ commanded, displayed });
        } catch (const std::exception&) {
            std::cerr << "  (couldn't parse \"" << line << "\", skipping)\n";
        }
    }

    hal_cat_close(cat);

    std::ofstream out(csv_path);
    if (!out) {
        std::cerr << "Failed to write " << csv_path << "\n";
        return 1;
    }
    out << "commanded_watts,displayed_watts\n";
    std::cout << "\ncommanded_watts,displayed_watts\n";
    for (const auto& r : readings) {
        out << r.first << "," << r.second << "\n";
        std::cout << r.first << "," << r.second << "\n";
    }

    std::cout << "\nWrote " << readings.size() << " reading(s) to " << csv_path << "\n";
    return 0;
}

// CLI overrides for the whitelist/gates config -- collected separately from SymbolonConfig
// itself so "was this flag actually passed" is unambiguous (an empty std::string/unset float
// can't be distinguished from "the user passed an empty value" otherwise). Applied on top of
// whatever --config's INI file already populated, per config.h's own documented order: CLI
// always wins. --legacy-mode is deliberately not documented here or in the README -- see
// core/cerberus.h's own note on cerberus_config_t.beacon_allow_token_alone and
// .claude/state/context.md's Phase 3 entry on why (Part 97 station-identification default).
struct ConfigOptions {
    std::string config_path;
    std::string beacon_token_path;
    std::string my_call;
    std::string my_grid;
    std::string whitelist_csv;
    std::string band;
    double freq_min_hz = -1.0; // <0 = not set
    double freq_max_hz = -1.0;
    bool has_min_snr = false;
    double min_snr_db = 0.0;
    bool legacy_mode = false; // undocumented: beacon_allow_token_alone
};

// Builds the effective SymbolonConfig from --config's INI file (if given) with CLI overrides
// applied on top, per config.h's documented order. Shared by --dump-config and (once built)
// the confirm-mode loop, so both see identical effective settings.
SymbolonConfig build_effective_config(const ConfigOptions& opts)
{
    SymbolonConfig cfg{};
    if (!opts.config_path.empty()) {
        if (!load_config_file(opts.config_path, cfg)) {
            std::cerr << "Warning: couldn't open config file " << opts.config_path << "\n";
        }
    }
    if (!opts.beacon_token_path.empty()) {
        if (!load_beacon_token_file(opts.beacon_token_path, cfg.cerberus)) {
            std::cerr << "Warning: couldn't open beacon token file " << opts.beacon_token_path << "\n";
        }
    }

    if (!opts.my_call.empty()) {
        std::strncpy(cfg.cerberus.my_call, opts.my_call.c_str(), sizeof(cfg.cerberus.my_call) - 1);
        std::strncpy(cfg.qso.my_call, opts.my_call.c_str(), sizeof(cfg.qso.my_call) - 1);
    }
    if (!opts.my_grid.empty()) {
        std::strncpy(cfg.qso.my_grid, opts.my_grid.c_str(), sizeof(cfg.qso.my_grid) - 1);
    }
    if (!opts.whitelist_csv.empty()) {
        cfg.cerberus.whitelist_count = 0;
        std::stringstream ss(opts.whitelist_csv);
        std::string item;
        while (std::getline(ss, item, ',') && cfg.cerberus.whitelist_count < CERBERUS_MAX_WHITELIST) {
            // trim
            size_t start = item.find_first_not_of(" \t");
            size_t end = item.find_last_not_of(" \t");
            if (start == std::string::npos) {
                continue;
            }
            std::string call = item.substr(start, end - start + 1);
            std::strncpy(cfg.cerberus.whitelist[cfg.cerberus.whitelist_count], call.c_str(),
                sizeof(cfg.cerberus.whitelist[0]) - 1);
            ++cfg.cerberus.whitelist_count;
        }
    }
    if (!opts.band.empty()) {
        cfg.cerberus.has_band_gate = true;
        std::strncpy(cfg.cerberus.band, opts.band.c_str(), sizeof(cfg.cerberus.band) - 1);
    }
    if (opts.freq_min_hz >= 0.0 && opts.freq_max_hz >= 0.0) {
        cfg.cerberus.has_freq_gate = true;
        cfg.cerberus.freq_min_hz = (float)opts.freq_min_hz;
        cfg.cerberus.freq_max_hz = (float)opts.freq_max_hz;
    }
    if (opts.has_min_snr) {
        cfg.cerberus.has_snr_gate = true;
        cfg.cerberus.min_snr_db = (float)opts.min_snr_db;
    }
    cfg.cerberus.beacon_allow_token_alone = opts.legacy_mode;

    return cfg;
}

// Verifies the config plumbing end-to-end through the real binary, not just the doctest unit
// tests -- matches the project's own "compiles and the unit test passes is not the same claim
// as the shipped binary actually uses it" lesson from Phase 2 (see context.md). Deliberately
// never prints the beacon token's literal value (just whether one is configured) or anything
// about --legacy-mode -- this output is the kind of thing that ends up pasted into a bug
// report.
int dump_config_mode(const ConfigOptions& opts)
{
    SymbolonConfig cfg = build_effective_config(opts);

    std::cout << "my_call: " << (cfg.cerberus.my_call[0] ? cfg.cerberus.my_call : "(not set)") << "\n";
    std::cout << "my_grid: " << (cfg.qso.my_grid[0] ? cfg.qso.my_grid : "(not set)") << "\n";
    std::cout << "whitelist (" << cfg.cerberus.whitelist_count << "):";
    for (int i = 0; i < cfg.cerberus.whitelist_count; ++i) {
        std::cout << " " << cfg.cerberus.whitelist[i];
    }
    std::cout << "\n";
    std::cout << "beacon token: " << (cfg.cerberus.beacon_token[0] ? "configured" : "not configured") << "\n";
    std::cout << "band gate: " << (cfg.cerberus.has_band_gate ? cfg.cerberus.band : "(disabled)") << "\n";
    if (cfg.cerberus.has_freq_gate) {
        std::cout << "freq gate: " << cfg.cerberus.freq_min_hz << "-" << cfg.cerberus.freq_max_hz << " Hz\n";
    } else {
        std::cout << "freq gate: (disabled)\n";
    }
    if (cfg.cerberus.has_snr_gate) {
        std::cout << "min SNR gate: " << cfg.cerberus.min_snr_db << " dB\n";
    } else {
        std::cout << "min SNR gate: (disabled)\n";
    }
    return 0;
}

// sym_host_t adapters -- the seam core/atropos.c is built around (see core/sym_types.h):
// core/ never calls hal_*/OS functions directly, so app/ wires real HAL calls into the
// plain function pointers atropos.c calls through instead. hal_rc_t *is* sym_rc_t (a plain
// typedef alias, see hal/hal_types.h), so hal_ptt_set_adapter needs no translation at all.
uint64_t hal_mono_us_adapter(void* user)
{
    (void)user;
    return hal_time_mono_us();
}

uint64_t hal_utc_us_adapter(void* user)
{
    (void)user;
    return hal_time_utc_us();
}

sym_rc_t hal_ptt_set_adapter(void* user, int assert_tx)
{
    hal_cat_t* cat = static_cast<hal_cat_t*>(user);
    return hal_cat_set_ptt(cat, assert_tx != 0);
}

// Phase 4's own kickoff-specified bench verification, done for real against real hardware --
// but only after core/atropos.c's own offline unit tests (tests/core/test_atropos.c) already
// prove the mechanism with a fake clock, per the kickoff's explicit ordering ("fires under a
// simulated stall -- as a unit test -- before it's ever verified on the air").
//
// Deliberately asserts PTT and then never releases it from this harness -- the entire point
// is proving atropos_watchdog_tick() is what turns it back off, not this code. A second,
// independent hard backstop (kHardBackstopUs, well past atropos.c's own 13.5s) exists only so
// a bug in atropos.c can never leave the rig transmitting unattended during this test --
// belt-and-suspenders, not a substitute for the real watchdog gate: if the backstop is ever
// the thing that actually fires, that's reported as a FAIL, not treated as a safe fallback.
int atropos_watchdog_test_mode(const std::string& port, float power_watts)
{
    hal_cat_config_t cfg{};
    cfg.port = port.c_str();
    cfg.baud = 19200;
    cfg.rig_model = RIG_MODEL_X6200;
    cfg.max_tx_power_watts = 8.0f;

    hal_cat_t* cat = nullptr;
    if (hal_cat_open(&cat, &cfg) != HAL_RC_OK) {
        std::cerr << "Failed to open CAT on " << port << "\n";
        return 1;
    }

    if (hal_cat_set_power_watts(cat, power_watts) != HAL_RC_OK) {
        std::cerr << "Failed to set TX power to " << power_watts << " W: " << hal_cat_last_error(cat) << "\n";
        hal_cat_close(cat);
        return 1;
    }

    std::cout << "*** PTT WATCHDOG BENCH TEST ***\n"
              << "About to assert PTT at " << power_watts << " W on " << port << ".\n"
              << "This is a DELIBERATE hang: this test harness will NOT release PTT itself.\n"
              << "core/atropos.c's watchdog is expected to force it off automatically at ~13.5s.\n"
              << "An independent hard backstop in this harness will force it off at 16s and\n"
              << "report FAILURE if the watchdog somehow doesn't fire first.\n\n"
              << "Confirm the rig is connected to a dummy load, not an antenna, before continuing.\n"
              << "Press Enter to assert PTT, or Ctrl+C to abort...\n";
    std::string line;
    std::getline(std::cin, line);

    sym_host_t host{};
    host.mono_us = hal_mono_us_adapter;
    host.utc_us = hal_utc_us_adapter;
    host.ptt_set = hal_ptt_set_adapter;
    host.user = cat;

    atropos_config_t atropos_cfg{};
    atropos_cfg.ptt_watchdog_us = 13500000ULL; // 13.5s -- the kickoff's own spec constant

    atropos_t atropos;
    atropos_init(&atropos, &atropos_cfg, &host);

    std::cout << "\nAsserting PTT now...\n";
    if (hal_cat_set_ptt(cat, true) != HAL_RC_OK) {
        std::cerr << "Failed to assert PTT: " << hal_cat_last_error(cat) << "\n";
        hal_cat_close(cat);
        return 1;
    }
    atropos_ptt_asserted(&atropos);

    const uint64_t kHardBackstopUs = 16ULL * 1000000ULL;
    uint64_t start_us = hal_time_mono_us();
    bool watchdog_fired = false;

    for (;;) {
        uint64_t elapsed_us = hal_time_mono_us() - start_us;
        std::cout << "\r  PTT held for " << (double)elapsed_us / 1000000.0 << "s..." << std::flush;

        if (atropos_watchdog_tick(&atropos)) {
            watchdog_fired = true;
            std::cout << "\n\nWatchdog fired at " << (double)elapsed_us / 1000000.0 << "s -- PTT forced off.\n";
            break;
        }
        if (elapsed_us >= kHardBackstopUs) {
            std::cerr << "\n\n*** HARD BACKSTOP: watchdog did not fire by 16s -- forcing PTT off from this harness. ***\n";
            hal_cat_set_ptt(cat, false);
            hal_cat_close(cat);
            return 1;
        }
        hal_time_sleep_us(200000);
    }

    bool still_asserted = true;
    if (hal_cat_get_ptt(cat, &still_asserted) == HAL_RC_OK) {
        std::cout << "Rig-reported PTT state: " << (still_asserted ? "STILL ASSERTED (FAIL)" : "off (confirmed)") << "\n";
    } else {
        std::cout << "Warning: couldn't read back PTT state: " << hal_cat_last_error(cat) << "\n";
    }

    hal_cat_close(cat);

    if (watchdog_fired && !still_asserted) {
        std::cout << "\n*** PASS: PTT watchdog test succeeded. ***\n";
        return 0;
    }
    std::cerr << "\n*** FAIL: watchdog test did not complete cleanly. ***\n";
    return 1;
}

// Phase 3: confirm mode. Same capture/slot/decode loop as the default listening mode below,
// with every decode also run through cerberus_evaluate() and fed into the qso.c state
// machine; when a reply is composed, the operator confirms with any keypress within a short
// window before the next slot begins (kickoff: "the window between decodes landing and the
// next slot opening is short (~2 s)... a missed confirmation must skip the slot and re-offer,
// never transmit late"). Duplicates rather than shares the default mode's loop body -- kept
// separate (matching this file's existing one-function-per-mode pattern) rather than risking
// a shared-loop refactor of the already-working default path for what's currently a single
// new caller.
//
// Deliberately never opens CAT and never calls hal_cat_set_ptt() anywhere -- per
// .claude/CLAUDE.md's "never key the antenna first" ordering, this composes and displays what
// WOULD be sent and nothing more; actual keying is Phase 4's job, after core/atropos.c's PTT
// watchdog exists. current_band is a plain operator-supplied string (no CAT connection exists
// in this mode to read it automatically) -- pass "" to leave any configured band gate always
// failing closed rather than silently trusting an unverified value.
int confirm_mode(const SymbolonConfig& cfg, const std::string& device_name, const std::string& current_band)
{
    if (cfg.cerberus.my_call[0] == '\0' || cfg.cerberus.whitelist_count == 0) {
        std::cerr << "Confirm mode needs --my-call and a non-empty whitelist (via --config and/or --whitelist).\n";
        return 1;
    }

    std::signal(SIGINT, on_sigint);
    enable_raw_keyboard_mode();

    std::string capture_name = device_name.empty() ? find_default_capture_device_name() : device_name;
    std::cout << "symbolon " << kVersion << " -- confirm mode -- capture device: " << capture_name << "\n";
    std::cout << "My call: " << cfg.cerberus.my_call << "  Whitelist:";
    for (int i = 0; i < cfg.cerberus.whitelist_count; ++i) {
        std::cout << " " << cfg.cerberus.whitelist[i];
    }
    std::cout << "\n\n*** DRY RUN ONLY -- no CAT is opened, no PTT is ever asserted here. Nothing is\n"
                 "    actually transmitted; this only composes and displays replies. ***\n\n";

    const uint32_t kSampleRate = 12000;
    const size_t kRingCapacity = (size_t)kSampleRate * 5; // 5s headroom -- comfortably covers
        // the ~2s confirm-window pause below, during which the outer loop isn't draining the
        // ring (the capture thread keeps writing into it regardless).
    std::vector<float> ring_backing(kRingCapacity);
    sym_ring_t ring;
    sym_ring_init(&ring, ring_backing.data(), kRingCapacity);

    hal_audio_config_t audio_cfg{};
    audio_cfg.sample_rate_hz = kSampleRate;
    audio_cfg.capture_device = device_name.empty() ? nullptr : device_name.c_str();

    hal_audio_t* audio = nullptr;
    if (hal_audio_open(&audio, &audio_cfg, capture_callback, nullptr, &ring) != SYM_RC_OK) {
        std::cerr << "Failed to open capture device: " << hal_audio_last_error(audio) << "\n";
        restore_keyboard_mode();
        return 1;
    }
    if (hal_audio_start(audio) != SYM_RC_OK) {
        std::cerr << "Failed to start capture: " << hal_audio_last_error(audio) << "\n";
        hal_audio_close(audio);
        restore_keyboard_mode();
        return 1;
    }

    argus_config_t argus_cfg{};
    argus_cfg.f_min_hz = 200.0f;
    argus_cfg.f_max_hz = 3000.0f;
    argus_cfg.sample_rate_hz = (int)kSampleRate;
    argus_cfg.time_osr = 2;
    argus_cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &argus_cfg);

    const int block_size = argus_block_size(&argus);
    std::vector<float> block_buf((size_t)block_size);

    qso_t qso;
    qso_init(&qso);

    const uint64_t kConfirmWindowUs = 2ULL * 1000000ULL; // ~2s, per the kickoff

    uint64_t current_slot_epoch_us = 0;
    bool slot_started = false;

    std::cout << "Listening... (Ctrl+C to stop)\n";

    while (!g_stop) {
        uint64_t utc_us = hal_time_utc_us();
        horae_slot_t slot = horae_slot_at(utc_us);

        if (!slot_started || slot.slot_epoch_us != current_slot_epoch_us) {
            if (slot_started) {
                argus_decode_t decodes[64];
                int n = argus_decode_slot(&argus, decodes, 64);
                time_t slot_time = (time_t)(current_slot_epoch_us / 1000000ULL);
                struct tm tm_slot = *std::gmtime(&slot_time);

                for (int i = 0; i < n; ++i) {
                    print_decode_line(tm_slot.tm_hour, tm_slot.tm_min, tm_slot.tm_sec,
                        decodes[i].text, decodes[i].freq_hz, decodes[i].time_s, decodes[i].score);

                    const char* band_arg = current_band.empty() ? nullptr : current_band.c_str();
                    cerberus_result_t match = cerberus_evaluate(&cfg.cerberus, &decodes[i], band_arg);

                    if (match.matched) {
                        std::cout << "  -> matched (" << (match.is_beacon_token ? "beacon token" : "exchange") << ")\n";
                        qso_on_decode(&qso, &cfg.qso, &decodes[i]);
                    } else if (qso.step == QSO_STEP_IDLE) {
                        // cerberus_evaluate() deliberately rejects every CQ (see cerberus.h) --
                        // this is qso.c's own separate, QSO-state-aware check for the one
                        // that starts a new exchange.
                        qso_on_cq_heard(&qso, &cfg.qso, &cfg.cerberus, &decodes[i]);
                    }
                }

                if (qso.step == QSO_STEP_PENDING_CONFIRM) {
                    std::cout << "\n>>> Pending reply: \"" << qso.pending_text
                               << "\" -- press any key within ~2s to confirm (dry run) <<<\n";
                    uint64_t deadline = hal_time_mono_us() + kConfirmWindowUs;
                    bool confirmed = false;
                    while (hal_time_mono_us() < deadline) {
                        if (key_was_pressed()) {
                            confirmed = true;
                            break;
                        }
                        hal_time_sleep_us(20000);
                    }
                    if (confirmed) {
                        qso_confirm_sent(&qso);
                        std::cout << "Confirmed (dry run -- nothing was actually transmitted).\n";
                    } else {
                        qso_confirm_missed(&qso);
                        std::cout << "Missed the confirm window -- will re-offer at the next opportunity.\n";
                    }
                }

                if (qso.step == QSO_STEP_COMPLETE) {
                    std::cout << "\n=== QSO complete with " << qso.peer_call << " ===\n";
                    if (qso.snr_i_sent_valid && qso.snr_i_got_valid) {
                        std::cout << "  Report I gave him: " << qso.snr_i_sent << " dB\n";
                        std::cout << "  Report he gave me:  " << qso.snr_i_got << " dB\n";
                        std::cout << "  Asymmetry: " << (qso.snr_i_got - qso.snr_i_sent) << " dB\n\n";
                    }
                    qso_reset(&qso);
                }
            }
            argus_reset(&argus);
            current_slot_epoch_us = slot.slot_epoch_us;
            slot_started = true;
        }

        if (sym_ring_available(&ring) >= (size_t)block_size) {
            sym_ring_read(&ring, block_buf.data(), (size_t)block_size);
            argus_process_block(&argus, block_buf.data());
        } else {
            hal_time_sleep_us(50000);
        }
    }

    argus_free(&argus);
    hal_audio_stop(audio);
    hal_audio_close(audio);
    restore_keyboard_mode();
    std::cout << "\nStopped.\n";
    return 0;
}


// Duplex audio context for armed/beacon mode: unlike confirm_mode()/the default listen loop
// (capture only, hal_audio_open's `user` is a bare sym_ring_t*), this mode also plays a
// synthesized TX signal back out, so the shared `user` pointer needs to reach both the
// capture ring and the TxPlayback buffer -- hence its own small context struct and
// trampolines rather than reusing capture_callback/tx_playback_callback directly.
struct DuplexAudioContext {
    sym_ring_t* ring;
    TxPlayback* tx;
};

void duplex_capture_callback(const float* samples, uint32_t frame_count, void* user)
{
    auto* ctx = static_cast<DuplexAudioContext*>(user);
    sym_ring_write(ctx->ring, samples, frame_count);
}

uint32_t duplex_playback_callback(float* out, uint32_t frame_count, void* user)
{
    auto* ctx = static_cast<DuplexAudioContext*>(user);
    return ctx->tx->drain(out, frame_count);
}

// Phase 4: armed/beacon autonomy. Shared by both modes -- unlike confirm_mode() vs. the
// default listen loop (which duplicate deliberately to avoid risking already-working code),
// armed and beacon are both new here, so sharing from the start is the right call.
// armed_qso_limit <= 0 means beacon (no QSO-count bound, runs until Ctrl+C or an atropos
// interlock ends it); armed_timeout_minutes <= 0 means no wall-clock bound either way.
//
// This is the first code path in the whole project that can key PTT without a human
// confirming each transmission -- see .claude/CLAUDE.md's "never key the antenna first"
// ordering. Every interlock below is the real core/atropos.c mechanism, not a placeholder:
// the frequency allowlist is derived from --current-band and refuses to arm at all if it
// doesn't resolve; the dead-man timer, TX-slot budget, and session TX-time cap are all wired
// from CLI flags (0/unset = disabled, matching atropos.h's own "mechanism exists, operator's
// call" stance -- printed loudly in the startup banner below, not hidden).
int autonomous_mode(const SymbolonConfig& cfg, const std::string& device_name,
    const std::string& playback_device_name, const std::string& current_band,
    const std::string& cat_port, float tx_power_watts,
    float tx_freq_hz, int armed_qso_limit, double armed_timeout_minutes,
    double dead_man_minutes, int max_tx_per_hour, double max_tx_minutes,
    double tx_freq_tolerance_hz, bool tune_vfo)
{
    if (cfg.cerberus.my_call[0] == '\0' || cfg.cerberus.whitelist_count == 0) {
        std::cerr << "Armed/beacon mode needs --my-call and a non-empty whitelist (via --config and/or --whitelist).\n";
        return 1;
    }
    uint64_t dial_hz = 0;
    if (!band_to_dial_hz(current_band, &dial_hz)) {
        std::cerr << "Armed/beacon mode needs a recognized --current-band (e.g. 20m) to derive the frequency allowlist.\n";
        return 1;
    }
    if (cat_port.empty()) {
        std::cerr << "Armed/beacon mode needs --cat-port.\n";
        return 1;
    }
    if (tx_power_watts <= 0.0f) {
        std::cerr << "Armed/beacon mode needs --tx-power (watts) set explicitly.\n";
        return 1;
    }

    hal_cat_config_t cat_cfg{};
    cat_cfg.port = cat_port.c_str();
    cat_cfg.baud = 19200;
    cat_cfg.rig_model = RIG_MODEL_X6200;
    cat_cfg.max_tx_power_watts = 8.0f;

    hal_cat_t* cat = nullptr;
    if (hal_cat_open(&cat, &cat_cfg) != HAL_RC_OK) {
        std::cerr << "Failed to open CAT on " << cat_port << "\n";
        return 1;
    }

    // --tune-vfo: the app tunes the rig itself to --current-band's dial frequency. Off by
    // default -- the operator remains responsible for having the rig on the right band, same
    // as every prior session's manual --cat-set-freq workflow. A failed tune here is reported
    // but not fatal: atropos_freq_allowed() reads the rig's *live* dial frequency before every
    // auto-send regardless (see below), so a rig left on the wrong band still fails closed at
    // send time even if this step didn't run or silently failed.
    if (tune_vfo) {
        if (hal_cat_set_freq_hz(cat, dial_hz) != HAL_RC_OK) {
            std::cerr << "Warning: failed to tune VFO to " << dial_hz << " Hz: " << hal_cat_last_error(cat)
                       << " -- continuing, but the rig may not be on the configured band.\n";
        } else if (hal_cat_set_mode(cat, HAL_CAT_MODE_USB) != HAL_RC_OK) {
            std::cerr << "Warning: failed to set USB mode: " << hal_cat_last_error(cat) << "\n";
        } else {
            std::cout << "Tuned VFO to " << dial_hz << " Hz (USB).\n";
        }
    }

    if (hal_cat_set_power_watts(cat, tx_power_watts) != HAL_RC_OK) {
        std::cerr << "Failed to set TX power to " << tx_power_watts << " W: " << hal_cat_last_error(cat) << "\n";
        hal_cat_close(cat);
        return 1;
    }

    sym_host_t host{};
    host.mono_us = hal_mono_us_adapter;
    host.utc_us = hal_utc_us_adapter;
    host.ptt_set = hal_ptt_set_adapter;
    host.user = cat;

    atropos_config_t atropos_cfg{};
    atropos_cfg.ptt_watchdog_us = 13500000ULL; // 13.5s -- the kickoff's own spec constant
    atropos_cfg.dead_man_timeout_us = (uint64_t)(dead_man_minutes * 60.0 * 1000000.0);
    atropos_cfg.max_tx_slots_per_hour = max_tx_per_hour;
    atropos_cfg.max_tx_us_session = (uint64_t)(max_tx_minutes * 60.0 * 1000000.0);
    atropos_cfg.allowed_freq_hz[0] = dial_hz;
    atropos_cfg.allowed_freq_count = 1;
    atropos_cfg.freq_tolerance_hz = (uint64_t)tx_freq_tolerance_hz;

    atropos_t atropos;
    atropos_init(&atropos, &atropos_cfg, &host);

    bool is_beacon = (armed_qso_limit <= 0);
    uint64_t armed_timeout_us = (armed_timeout_minutes > 0.0) ? (uint64_t)(armed_timeout_minutes * 60.0 * 1000000.0) : 0ULL;

    std::cout << "*** " << (is_beacon ? "BEACON" : "ARMED") << " MODE ***\n"
              << "My call: " << cfg.cerberus.my_call << "  Whitelist:";
    for (int i = 0; i < cfg.cerberus.whitelist_count; ++i) {
        std::cout << " " << cfg.cerberus.whitelist[i];
    }
    std::cout << "\nBand: " << current_band << " (dial " << dial_hz << " Hz +/- "
              << atropos_cfg.freq_tolerance_hz << " Hz)   TX power: " << tx_power_watts << " W\n"
              << "VFO tuning: " << (tune_vfo ? "automatic (app tunes to the dial frequency above)"
                                              : "operator responsibility (--tune-vfo not passed)") << "\n"
              << "PTT watchdog: 13.5s (fixed)\n"
              << "Dead-man timer: " << (dead_man_minutes > 0.0 ? (format_minutes(dead_man_minutes) + " min") : std::string("DISABLED")) << "\n"
              << "TX slots/hour cap: " << (max_tx_per_hour > 0 ? std::to_string(max_tx_per_hour) : std::string("DISABLED")) << "\n"
              << "Session TX-time cap: " << (max_tx_minutes > 0.0 ? (format_minutes(max_tx_minutes) + " min") : std::string("DISABLED")) << "\n";
    if (!is_beacon) {
        std::cout << "Armed bound: " << armed_qso_limit << " QSO(s)"
                  << (armed_timeout_us > 0 ? (" or " + format_minutes(armed_timeout_minutes) + " min") : std::string(""))
                  << ", whichever comes first\n";
    }
    std::cout << "\n*** THIS MODE ACTUALLY TRANSMITS. Confirm the rig is on the intended antenna\n"
                 "    and power setting before continuing. ***\n"
                 "Press Enter to arm and start, or Ctrl+C to abort...\n";
    std::string confirm_line;
    std::getline(std::cin, confirm_line);

    std::signal(SIGINT, on_sigint);
    enable_raw_keyboard_mode();

    atropos_arm(&atropos);

    std::string capture_name = device_name.empty() ? find_default_capture_device_name() : device_name;
    // No cross-defaulting from --device: on real USB audio codecs (the X6200 included) the
    // capture and playback endpoints are typically named differently (e.g. "Microphone (...)"
    // vs "Speakers (...)"), confirmed by --list-devices on this dev machine -- guessing one
    // from the other risks silently falling back to the system default while the banner still
    // claims the guessed name (exactly issue #3's original bug, just relabeled). Same
    // system-default-when-unset pattern as --device itself.
    std::string playback_name = playback_device_name.empty() ? find_default_playback_device_name() : playback_device_name;
    std::cout << "Capture device: " << capture_name << "   Playback device: " << playback_name << "   Listening...\n";

    const uint32_t kSampleRate = 12000;
    const size_t kRingCapacity = (size_t)kSampleRate * 5;
    std::vector<float> ring_backing(kRingCapacity);
    sym_ring_t ring;
    sym_ring_init(&ring, ring_backing.data(), kRingCapacity);

    TxPlayback tx_playback;
    DuplexAudioContext audio_ctx{ &ring, &tx_playback };

    hal_audio_config_t audio_cfg{};
    audio_cfg.sample_rate_hz = kSampleRate;
    audio_cfg.capture_device = device_name.empty() ? nullptr : device_name.c_str();
    audio_cfg.playback_device = playback_device_name.empty() ? nullptr : playback_device_name.c_str();

    hal_audio_t* audio = nullptr;
    if (hal_audio_open(&audio, &audio_cfg, duplex_capture_callback, duplex_playback_callback, &audio_ctx) != SYM_RC_OK) {
        std::cerr << "Failed to open capture/playback device: " << hal_audio_last_error(audio) << "\n";
        restore_keyboard_mode();
        hal_cat_close(cat);
        return 1;
    }
    if (hal_audio_start(audio) != SYM_RC_OK) {
        std::cerr << "Failed to start audio: " << hal_audio_last_error(audio) << "\n";
        hal_audio_close(audio);
        restore_keyboard_mode();
        hal_cat_close(cat);
        return 1;
    }

    argus_config_t argus_cfg{};
    argus_cfg.f_min_hz = 200.0f;
    argus_cfg.f_max_hz = 3000.0f;
    argus_cfg.sample_rate_hz = (int)kSampleRate;
    argus_cfg.time_osr = 2;
    argus_cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &argus_cfg);

    const int block_size = argus_block_size(&argus);
    std::vector<float> block_buf((size_t)block_size);

    qso_t qso;
    qso_init(&qso);

    uint64_t current_slot_epoch_us = 0;
    bool slot_started = false;
    int qso_completed_count = 0;
    uint64_t session_start_us = hal_time_mono_us();

    while (!g_stop && atropos.armed) {
        if (key_was_pressed()) {
            atropos_operator_input(&atropos);
        }
        atropos_watchdog_tick(&atropos);
        if (atropos_dead_man_tick(&atropos)) {
            std::cout << "\n*** Dead-man timer expired -- auto-disarmed. Stopping. ***\n";
            break;
        }
        if (armed_timeout_us > 0 && (hal_time_mono_us() - session_start_us) >= armed_timeout_us) {
            std::cout << "\n*** Armed timeout reached -- disarming. Stopping. ***\n";
            atropos_disarm(&atropos);
            break;
        }

        uint64_t utc_us = hal_time_utc_us();
        horae_slot_t slot = horae_slot_at(utc_us);

        if (!slot_started || slot.slot_epoch_us != current_slot_epoch_us) {
            if (slot_started) {
                argus_decode_t decodes[64];
                int n = argus_decode_slot(&argus, decodes, 64);
                time_t slot_time = (time_t)(current_slot_epoch_us / 1000000ULL);
                struct tm tm_slot = *std::gmtime(&slot_time);

                for (int i = 0; i < n; ++i) {
                    print_decode_line(tm_slot.tm_hour, tm_slot.tm_min, tm_slot.tm_sec,
                        decodes[i].text, decodes[i].freq_hz, decodes[i].time_s, decodes[i].score);

                    const char* band_arg = current_band.empty() ? nullptr : current_band.c_str();
                    cerberus_result_t match = cerberus_evaluate(&cfg.cerberus, &decodes[i], band_arg);

                    if (match.matched) {
                        std::cout << "  -> matched (" << (match.is_beacon_token ? "beacon token" : "exchange") << ")\n";
                        qso_on_decode(&qso, &cfg.qso, &decodes[i]);
                    } else if (qso.step == QSO_STEP_IDLE) {
                        // cerberus_evaluate() deliberately rejects every CQ (see cerberus.h) --
                        // this is qso.c's own separate, QSO-state-aware check for the one that
                        // starts a new exchange.
                        qso_on_cq_heard(&qso, &cfg.qso, &cfg.cerberus, &decodes[i]);
                    }
                }

                if (qso.step == QSO_STEP_PENDING_CONFIRM) {
                    uint64_t live_dial_hz = dial_hz;
                    hal_cat_get_freq_hz(cat, &live_dial_hz); // best-effort; falls back to the
                        // configured band's dial freq on a read failure -- still gated by
                        // atropos_freq_allowed() below either way.

                    bool freq_ok = atropos_freq_allowed(&atropos, live_dial_hz);
                    bool budget_ok = atropos_tx_budget_ok(&atropos);

                    if (freq_ok && budget_ok) {
                        std::cout << "\n>>> Sending: \"" << qso.pending_text << "\" <<<\n";

                        const int sample_rate_hz = 12000;
                        const int num_samples = sym_tx_signal_samples(sample_rate_hz);
                        std::vector<float> signal((size_t)num_samples);
                        if (sym_tx_synthesize(qso.pending_text, tx_freq_hz, sample_rate_hz, signal.data()) == SYM_TX_OK) {
                            tx_playback.arm(signal.data(), signal.size());

                            if (hal_cat_set_ptt(cat, true) == HAL_RC_OK) {
                                atropos_ptt_asserted(&atropos);
                                while (!tx_playback.is_done() && atropos.ptt_asserted) {
                                    atropos_watchdog_tick(&atropos);
                                    hal_time_sleep_us(50000);
                                }
                                if (atropos.ptt_asserted) {
                                    // Normal completion -- the watchdog never had to step in.
                                    hal_cat_set_ptt(cat, false);
                                    atropos_ptt_released(&atropos);
                                } else {
                                    std::cout << "  (watchdog forced PTT off mid-transmission)\n";
                                }
                                qso_confirm_sent(&qso);
                                std::cout << "Sent.\n";
                            } else {
                                std::cerr << "Failed to assert PTT: " << hal_cat_last_error(cat) << " -- skipping this slot.\n";
                                qso_confirm_missed(&qso);
                            }
                        } else {
                            std::cerr << "Failed to synthesize \"" << qso.pending_text << "\" -- skipping this slot.\n";
                            qso_confirm_missed(&qso);
                        }
                    } else {
                        std::cout << "\n>>> Pending reply \"" << qso.pending_text << "\" blocked ("
                                   << (!freq_ok ? "frequency allowlist" : "TX budget")
                                   << ") -- skipping this slot, per the kickoff's \"never transmit late\" rule. <<<\n";
                        qso_confirm_missed(&qso);
                    }
                }

                if (qso.step == QSO_STEP_COMPLETE) {
                    std::cout << "\n=== QSO complete with " << qso.peer_call << " ===\n";
                    if (qso.snr_i_sent_valid && qso.snr_i_got_valid) {
                        std::cout << "  Report I gave him: " << qso.snr_i_sent << " dB\n";
                        std::cout << "  Report he gave me:  " << qso.snr_i_got << " dB\n";
                        std::cout << "  Asymmetry: " << (qso.snr_i_got - qso.snr_i_sent) << " dB\n\n";
                    }
                    qso_reset(&qso);
                    atropos_operator_input(&atropos); // a completed exchange also counts as a
                        // dead-man reset, per the user's choice this session -- not keypress-only.
                    ++qso_completed_count;

                    if (!is_beacon && qso_completed_count >= armed_qso_limit) {
                        std::cout << "\n*** Armed limit of " << armed_qso_limit << " QSO(s) reached -- disarming. ***\n";
                        atropos_disarm(&atropos);
                    }
                }
            }
            argus_reset(&argus);
            current_slot_epoch_us = slot.slot_epoch_us;
            slot_started = true;
        }

        if (sym_ring_available(&ring) >= (size_t)block_size) {
            sym_ring_read(&ring, block_buf.data(), (size_t)block_size);
            argus_process_block(&argus, block_buf.data());
        } else {
            hal_time_sleep_us(50000);
        }
    }

    argus_free(&argus);
    hal_audio_stop(audio);
    hal_audio_close(audio);
    hal_cat_close(cat);
    restore_keyboard_mode();

    std::cout << "\nStopped. " << qso_completed_count << " QSO(s) completed, "
              << atropos.tx_slot_count << " TX slot(s), "
              << ((double)atropos.total_tx_us_session / 1000000.0) << "s total TX time.\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::string decode_wav_path;
    std::string device_name;
    std::string playback_device_name;
    std::string tx_test_text;
    std::string tx_test_wav_path;
    float tx_test_freq_hz = 1500.0f;
    CatOptions cat_opts;
    std::string cat_power_cal_port;
    std::string cat_power_cal_csv;
    ConfigOptions config_opts;
    bool dump_config = false;
    bool confirm = false;
    std::string current_band;
    std::string atropos_test_port;
    float atropos_test_power_w = 0.5f;
    bool armed = false;
    int armed_qso_limit = 0;
    bool beacon = false;
    double armed_timeout_minutes = 0.0;
    double dead_man_minutes = 0.0;
    int max_tx_per_hour = 0;
    double max_tx_minutes = 0.0;
    double tx_freq_tolerance_hz = 100.0;
    float tx_power_watts = 0.0f;
    bool tune_vfo = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "symbolon " << kVersion << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--list-devices") == 0) {
            return list_devices();
        }
        if (std::strcmp(argv[i], "--decode-wav") == 0 && i + 1 < argc) {
            decode_wav_path = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_name = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--playback-device") == 0 && i + 1 < argc) {
            playback_device_name = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--tx-test") == 0 && i + 2 < argc) {
            tx_test_text = argv[++i];
            tx_test_wav_path = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--tx-freq") == 0 && i + 1 < argc) {
            tx_test_freq_hz = std::stof(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--cat-port") == 0 && i + 1 < argc) {
            cat_opts.port = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--cat-set-freq") == 0 && i + 1 < argc) {
            cat_opts.set_freq_hz = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--cat-set-mode") == 0 && i + 1 < argc) {
            cat_opts.set_mode = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--cat-preamp") == 0 && i + 1 < argc) {
            std::string v = argv[++i];
            cat_opts.set_preamp = (v == "on") ? 1 : 0;
            continue;
        }
        if (std::strcmp(argv[i], "--cat-agc") == 0 && i + 1 < argc) {
            cat_opts.set_agc = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--cat-set-power") == 0 && i + 1 < argc) {
            cat_opts.set_power_w = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--cat-power-cal") == 0 && i + 2 < argc) {
            cat_power_cal_port = argv[++i];
            cat_power_cal_csv = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_opts.config_path = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--beacon-token-file") == 0 && i + 1 < argc) {
            config_opts.beacon_token_path = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--my-call") == 0 && i + 1 < argc) {
            config_opts.my_call = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--my-grid") == 0 && i + 1 < argc) {
            config_opts.my_grid = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--whitelist") == 0 && i + 1 < argc) {
            config_opts.whitelist_csv = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--band") == 0 && i + 1 < argc) {
            config_opts.band = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--freq-min") == 0 && i + 1 < argc) {
            config_opts.freq_min_hz = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--freq-max") == 0 && i + 1 < argc) {
            config_opts.freq_max_hz = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--min-snr") == 0 && i + 1 < argc) {
            config_opts.has_min_snr = true;
            config_opts.min_snr_db = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--legacy-mode") == 0) {
            config_opts.legacy_mode = true;
            continue;
        }
        if (std::strcmp(argv[i], "--dump-config") == 0) {
            dump_config = true;
            continue;
        }
        if (std::strcmp(argv[i], "--confirm") == 0) {
            confirm = true;
            continue;
        }
        if (std::strcmp(argv[i], "--current-band") == 0 && i + 1 < argc) {
            current_band = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--atropos-watchdog-test") == 0 && i + 1 < argc) {
            atropos_test_port = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--atropos-test-power") == 0 && i + 1 < argc) {
            atropos_test_power_w = std::stof(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--armed") == 0 && i + 1 < argc) {
            armed = true;
            armed_qso_limit = std::stoi(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--armed-timeout-minutes") == 0 && i + 1 < argc) {
            armed_timeout_minutes = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--beacon") == 0) {
            beacon = true;
            continue;
        }
        if (std::strcmp(argv[i], "--dead-man-minutes") == 0 && i + 1 < argc) {
            dead_man_minutes = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--max-tx-per-hour") == 0 && i + 1 < argc) {
            max_tx_per_hour = std::stoi(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--max-tx-minutes") == 0 && i + 1 < argc) {
            max_tx_minutes = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--tx-freq-tolerance-hz") == 0 && i + 1 < argc) {
            tx_freq_tolerance_hz = std::stod(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--tune-vfo") == 0) {
            tune_vfo = true;
            continue;
        }
        if (std::strcmp(argv[i], "--tx-power") == 0 && i + 1 < argc) {
            tx_power_watts = std::stof(argv[++i]);
            continue;
        }
    }

    if (armed && beacon) {
        std::cerr << "--armed and --beacon are mutually exclusive.\n";
        return 1;
    }
    if ((armed || beacon) && confirm) {
        std::cerr << "--confirm cannot be combined with --armed/--beacon.\n";
        return 1;
    }

    if (dump_config) {
        return dump_config_mode(config_opts);
    }
    if (confirm) {
        SymbolonConfig cfg = build_effective_config(config_opts);
        return confirm_mode(cfg, device_name, current_band);
    }
    if (armed || beacon) {
        SymbolonConfig cfg = build_effective_config(config_opts);
        return autonomous_mode(cfg, device_name, playback_device_name, current_band, cat_opts.port, tx_power_watts,
            tx_test_freq_hz, beacon ? 0 : armed_qso_limit, armed_timeout_minutes,
            dead_man_minutes, max_tx_per_hour, max_tx_minutes, tx_freq_tolerance_hz, tune_vfo);
    }
    if (!atropos_test_port.empty()) {
        return atropos_watchdog_test_mode(atropos_test_port, atropos_test_power_w);
    }
    if (!decode_wav_path.empty()) {
        return decode_wav_mode(decode_wav_path);
    }
    if (!tx_test_text.empty()) {
        return tx_test_mode(tx_test_text, tx_test_wav_path, tx_test_freq_hz);
    }
    if (!cat_power_cal_port.empty()) {
        return cat_power_cal_mode(cat_power_cal_port, cat_power_cal_csv);
    }
    if (!cat_opts.port.empty()) {
        return cat_info_mode(cat_opts);
    }

    std::signal(SIGINT, on_sigint);

    std::string capture_name = device_name.empty() ? find_default_capture_device_name() : device_name;
    std::cout << "symbolon " << kVersion << " -- capture device: " << capture_name << "\n";

    const uint32_t kSampleRate = 12000;
    const size_t kRingCapacity = (size_t)kSampleRate * 5; // 5s headroom between decode-loop polls
    std::vector<float> ring_backing(kRingCapacity);
    sym_ring_t ring;
    sym_ring_init(&ring, ring_backing.data(), kRingCapacity);

    hal_audio_config_t audio_cfg{};
    audio_cfg.sample_rate_hz = kSampleRate;
    audio_cfg.capture_device = device_name.empty() ? nullptr : device_name.c_str();

    hal_audio_t* audio = nullptr;
    if (hal_audio_open(&audio, &audio_cfg, capture_callback, nullptr, &ring) != SYM_RC_OK) {
        std::cerr << "Failed to open capture device: " << hal_audio_last_error(audio) << "\n";
        return 1;
    }
    if (hal_audio_start(audio) != SYM_RC_OK) {
        std::cerr << "Failed to start capture: " << hal_audio_last_error(audio) << "\n";
        hal_audio_close(audio);
        return 1;
    }

    argus_config_t argus_cfg{};
    argus_cfg.f_min_hz = 200.0f;
    argus_cfg.f_max_hz = 3000.0f;
    argus_cfg.sample_rate_hz = (int)kSampleRate;
    argus_cfg.time_osr = 2;
    argus_cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &argus_cfg);

    const int block_size = argus_block_size(&argus);
    std::vector<float> block_buf((size_t)block_size);

    uint64_t current_slot_epoch_us = 0;
    bool slot_started = false;

    std::cout << "Listening... (Ctrl+C to stop)\n";

    while (!g_stop) {
        uint64_t utc_us = hal_time_utc_us();
        horae_slot_t slot = horae_slot_at(utc_us);

        if (!slot_started || slot.slot_epoch_us != current_slot_epoch_us) {
            if (slot_started) {
                argus_decode_t decodes[64];
                int n = argus_decode_slot(&argus, decodes, 64);
                time_t slot_time = (time_t)(current_slot_epoch_us / 1000000ULL);
                // Single-threaded use of gmtime() here (only main() calls it); gmtime_r
                // isn't reliably available without POSIX feature-test macros in MinGW's
                // C++ mode, and this loop has no concurrent caller to race against.
                struct tm tm_slot = *std::gmtime(&slot_time);
                for (int i = 0; i < n; ++i) {
                    print_decode_line(tm_slot.tm_hour, tm_slot.tm_min, tm_slot.tm_sec,
                        decodes[i].text, decodes[i].freq_hz, decodes[i].time_s, decodes[i].score);
                }
            }
            argus_reset(&argus);
            current_slot_epoch_us = slot.slot_epoch_us;
            slot_started = true;
        }

        if (sym_ring_available(&ring) >= (size_t)block_size) {
            sym_ring_read(&ring, block_buf.data(), (size_t)block_size);
            argus_process_block(&argus, block_buf.data());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    argus_free(&argus);
    hal_audio_stop(audio);
    hal_audio_close(audio);
    std::cout << "\nStopped.\n";
    return 0;
}
