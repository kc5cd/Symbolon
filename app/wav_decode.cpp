#include "wav_decode.h"

#include <iostream>

extern "C" {
#include "argus.h"
#include <common/wave.h>
}

std::vector<WavDecode> decode_wav_file(const std::string& wav_path)
{
    const int kCapacity = 20 * 12000; // 20s @ 12kHz -- comfortably more than one FT8 slot
    std::vector<float> signal(kCapacity);
    int num_samples = kCapacity;
    int sample_rate = 0;
    if (load_wav(signal.data(), &num_samples, &sample_rate, wav_path.c_str()) != 0) {
        std::cerr << "load_wav failed for " << wav_path << "\n";
        return {};
    }

    argus_config_t cfg{};
    cfg.f_min_hz = 200.0f;
    cfg.f_max_hz = 3000.0f;
    cfg.sample_rate_hz = sample_rate;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &cfg);

    int block_size = argus_block_size(&argus);
    for (int pos = 0; pos + block_size <= num_samples; pos += block_size) {
        argus_process_block(&argus, signal.data() + pos);
    }

    argus_decode_t decodes[256];
    int num_decoded = argus_decode_slot(&argus, decodes, 256);
    argus_free(&argus);

    std::vector<WavDecode> results;
    results.reserve((size_t)num_decoded);
    for (int i = 0; i < num_decoded; ++i) {
        results.push_back({ decodes[i].text, decodes[i].freq_hz, decodes[i].time_s, decodes[i].score });
    }
    return results;
}
