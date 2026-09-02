#include "doctest.h"
#include "tx_playback.h"

#include <vector>

TEST_CASE("TxPlayback is silent and done before anything is armed")
{
    TxPlayback tx;
    CHECK(tx.is_done());

    std::vector<float> out(8, 999.0f);
    uint32_t n = tx.drain(out.data(), 8);
    CHECK(n == 8);
    for (float sample : out) {
        CHECK(sample == doctest::Approx(0.0f));
    }
}

TEST_CASE("TxPlayback drains an armed buffer exactly, across multiple partial reads")
{
    std::vector<float> signal = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    TxPlayback tx;
    tx.arm(signal.data(), signal.size());
    CHECK_FALSE(tx.is_done());

    std::vector<float> out(3);
    uint32_t n = tx.drain(out.data(), 3);
    CHECK(n == 3);
    CHECK(out[0] == doctest::Approx(1.0f));
    CHECK(out[1] == doctest::Approx(2.0f));
    CHECK(out[2] == doctest::Approx(3.0f));
    CHECK_FALSE(tx.is_done());

    // Second read spans the boundary: 2 real samples left, then zero-fill for the rest of
    // the requested block.
    out.assign(4, -1.0f);
    n = tx.drain(out.data(), 4);
    CHECK(n == 4);
    CHECK(out[0] == doctest::Approx(4.0f));
    CHECK(out[1] == doctest::Approx(5.0f));
    CHECK(out[2] == doctest::Approx(0.0f));
    CHECK(out[3] == doctest::Approx(0.0f));
    CHECK(tx.is_done());

    // Further reads after exhaustion stay all-zero.
    out.assign(2, -1.0f);
    n = tx.drain(out.data(), 2);
    CHECK(n == 2);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(0.0f));
}

TEST_CASE("TxPlayback re-arming replaces the previous buffer")
{
    std::vector<float> first = { 1.0f, 1.0f };
    std::vector<float> second = { 2.0f, 2.0f, 2.0f };
    TxPlayback tx;

    tx.arm(first.data(), first.size());
    std::vector<float> out(2);
    tx.drain(out.data(), 2);
    CHECK(tx.is_done());

    tx.arm(second.data(), second.size());
    CHECK_FALSE(tx.is_done());
    out.assign(3, -1.0f);
    tx.drain(out.data(), 3);
    CHECK(out[0] == doctest::Approx(2.0f));
    CHECK(out[1] == doctest::Approx(2.0f));
    CHECK(out[2] == doctest::Approx(2.0f));
    CHECK(tx.is_done());
}
