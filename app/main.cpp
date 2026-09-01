#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "wav_decode.h"

extern "C" {
#include "argus.h"
#include "hal_audio.h"
#include "hal_cat.h"
#include "hal_time.h"
#include "horae.h"
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

const char* cat_mode_name(hal_cat_mode_t mode)
{
    static const char* kModeNames[] = { "USB", "LSB", "DATA-U", "DATA-L", "CW" };
    return kModeNames[mode];
}

struct CatOptions {
    std::string port;
    double set_freq_hz = 0.0;
    std::string set_mode; // empty = don't set
    int set_preamp = -1;  // -1 = don't set, 0 = off, 1 = on
};

// Phase 2, no keying: opens real CAT via Hamlib, applies any requested settings (frequency,
// mode, preamp -- none of these are PTT, no RF is emitted by any of them), then prints the
// rig's current status. hal_cat_set_ptt() exists and works (see tests/hal/test_cat_hamlib.c
// against RIG_MODEL_DUMMY), but nothing in this CLI calls it; per the kickoff's "never key
// the antenna first" ordering, that's Phase 4's job, after core/atropos.c's watchdog exists.
int cat_info_mode(const CatOptions& opts)
{
    hal_cat_config_t cfg{};
    cfg.port = opts.port.c_str();
    cfg.baud = 19200; // X6200 SERIAL-B via the CH342 USB bridge, per the kickoff's hardware facts
    cfg.rig_model = RIG_MODEL_X6200;

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

    hal_cat_close(cat);
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
    }

    if (!decode_wav_path.empty()) {
        return decode_wav_mode(decode_wav_path);
    }
    if (!tx_test_text.empty()) {
        return tx_test_mode(tx_test_text, tx_test_wav_path, tx_test_freq_hz);
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
