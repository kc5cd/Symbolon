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

#include "config.h"
#include "wav_decode.h"

extern "C" {
#include "argus.h"
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

// Runs on miniaudio's realtime capture thread (hal/audio_miniaudio.c) -- must not block,
// allocate, or do I/O, per hal_audio.h's own contract. Handing samples to the ring is the
// only thing it does; the decode-thread loop in main() below is the consumer.
void capture_callback(const float* samples, uint32_t frame_count, void* user)
{
    sym_ring_t* ring = static_cast<sym_ring_t*>(user);
    sym_ring_write(ring, samples, frame_count);
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
    for (size_t i = 0; i < device_count; ++i) {
        std::cout << (devices[i].is_default ? "* " : "  ") << devices[i].name << "\n";
    }
    if (device_count == 0) {
        std::cerr << "No capture devices found.\n";
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

} // namespace

int main(int argc, char** argv)
{
    std::string decode_wav_path;
    std::string device_name;
    std::string tx_test_text;
    std::string tx_test_wav_path;
    float tx_test_freq_hz = 1500.0f;
    CatOptions cat_opts;
    std::string cat_power_cal_port;
    std::string cat_power_cal_csv;
    ConfigOptions config_opts;
    bool dump_config = false;

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
    }

    if (dump_config) {
        return dump_config_mode(config_opts);
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
