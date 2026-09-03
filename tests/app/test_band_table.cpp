#include "doctest.h"
#include "band_table.h"

TEST_CASE("band_to_dial_hz resolves every canonical band to the kickoff's dial frequency")
{
    uint64_t hz = 0;

    CHECK(band_to_dial_hz("160m", &hz));
    CHECK(hz == 1840000ULL);

    CHECK(band_to_dial_hz("80m", &hz));
    CHECK(hz == 3573000ULL);

    CHECK(band_to_dial_hz("40m", &hz));
    CHECK(hz == 7074000ULL);

    CHECK(band_to_dial_hz("30m", &hz));
    CHECK(hz == 10136000ULL);

    CHECK(band_to_dial_hz("20m", &hz));
    CHECK(hz == 14074000ULL);

    CHECK(band_to_dial_hz("17m", &hz));
    CHECK(hz == 18100000ULL);

    CHECK(band_to_dial_hz("15m", &hz));
    CHECK(hz == 21074000ULL);

    CHECK(band_to_dial_hz("12m", &hz));
    CHECK(hz == 24915000ULL);

    CHECK(band_to_dial_hz("10m", &hz));
    CHECK(hz == 28074000ULL);
}

TEST_CASE("band_to_dial_hz rejects an unrecognized band name")
{
    uint64_t hz = 12345;
    CHECK_FALSE(band_to_dial_hz("6m", &hz));
    CHECK_FALSE(band_to_dial_hz("", &hz));
    CHECK(hz == 12345); // untouched on failure
}
