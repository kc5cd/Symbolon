#include "band_table.h"

namespace {
struct BandEntry {
    const char* name;
    uint64_t dial_hz;
};

// kHz values from the kickoff doc's "FT8 dial frequencies (USB dial, kHz)" table.
constexpr BandEntry kBands[] = {
    { "160m", 1840000ULL },
    { "80m", 3573000ULL },
    { "40m", 7074000ULL },
    { "30m", 10136000ULL },
    { "20m", 14074000ULL },
    { "17m", 18100000ULL },
    { "15m", 21074000ULL },
    { "12m", 24915000ULL },
    { "10m", 28074000ULL },
};
} // namespace

bool band_to_dial_hz(const std::string& band, uint64_t* out_hz)
{
    for (const auto& entry : kBands) {
        if (band == entry.name) {
            *out_hz = entry.dial_hz;
            return true;
        }
    }
    return false;
}
