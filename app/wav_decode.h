#ifndef SYM_WAV_DECODE_H
#define SYM_WAV_DECODE_H

#include <string>
#include <vector>

// Shared by symbolon's own --decode-wav mode and tests/corpus/test_corpus.cpp, so the two
// don't drift into two slightly-different decode paths.

struct WavDecode {
    std::string text;
    float freq_hz;
    float time_s;
    int score; // ft8_lib's raw sync-strength score; see argus.h -- no real SNR estimate yet
};

// Loads wav_path and decodes it as a single FT8 slot, using Phase 1's settled waterfall
// parameters (200-3000 Hz, time_osr = freq_osr = 2). Returns an empty vector and prints an
// error to stderr if the file can't be loaded -- callers can't distinguish "load failed"
// from "loaded but nothing decoded" from the return value alone; check stderr if that
// matters.
std::vector<WavDecode> decode_wav_file(const std::string& wav_path);

#endif // SYM_WAV_DECODE_H
