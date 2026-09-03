#include "doctest.h"
#include "config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Unique scratch file per test run so parallel/repeat ctest invocations don't collide.
std::string make_scratch_path(const char* suffix)
{
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path()
        / ("symbolon_test_config_" + std::to_string(++counter) + suffix);
    return path.string();
}

void write_file(const std::string& path, const std::string& contents)
{
    std::ofstream out(path);
    out << contents;
}

} // namespace

TEST_CASE("load_config_file parses station, whitelist, and gates sections")
{
    std::string path = make_scratch_path(".ini");
    write_file(path,
        "[station]\n"
        "call = KC5CD\n"
        "grid = EM10\n"
        "\n"
        "; a comment line\n"
        "[whitelist]\n"
        "calls = W5XYZ, N0CALL\n"
        "\n"
        "[gates]\n"
        "band = 20m\n"
        "freq_min_hz = 200\n"
        "freq_max_hz = 3000\n"
        "min_snr_db = -20\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));

    CHECK(std::string(cfg.cerberus.my_call) == "KC5CD");
    CHECK(std::string(cfg.qso.my_call) == "KC5CD");
    CHECK(std::string(cfg.qso.my_grid) == "EM10");

    REQUIRE(cfg.cerberus.whitelist_count == 2);
    CHECK(std::string(cfg.cerberus.whitelist[0]) == "W5XYZ");
    CHECK(std::string(cfg.cerberus.whitelist[1]) == "N0CALL");

    CHECK(cfg.cerberus.has_band_gate);
    CHECK(std::string(cfg.cerberus.band) == "20m");
    CHECK(cfg.cerberus.has_freq_gate);
    CHECK(cfg.cerberus.freq_min_hz == doctest::Approx(200.0f));
    CHECK(cfg.cerberus.freq_max_hz == doctest::Approx(3000.0f));
    CHECK(cfg.cerberus.has_snr_gate);
    CHECK(cfg.cerberus.min_snr_db == doctest::Approx(-20.0f));

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file leaves the freq gate disabled when only one bound is given")
{
    std::string path = make_scratch_path(".ini");
    write_file(path,
        "[gates]\n"
        "freq_min_hz = 200\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));

    CHECK_FALSE(cfg.cerberus.has_freq_gate);

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file returns false when the file can't be opened")
{
    SymbolonConfig cfg{};
    CHECK_FALSE(load_config_file("Z:/this/path/does/not/exist.ini", cfg));
}

TEST_CASE("load_config_file skips malformed lines without aborting the rest of the file")
{
    std::string path = make_scratch_path(".ini");
    write_file(path,
        "[station]\n"
        "this line has no equals sign\n"
        "call = KC5CD\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));
    CHECK(std::string(cfg.cerberus.my_call) == "KC5CD");

    std::filesystem::remove(path);
}

TEST_CASE("load_beacon_token_file trims trailing whitespace and newlines")
{
    std::string path = make_scratch_path(".txt");
    write_file(path, "PROPTEST1\n");

    cerberus_config_t cerberus{};
    REQUIRE(load_beacon_token_file(path, cerberus));
    CHECK(std::string(cerberus.beacon_token) == "PROPTEST1");

    std::filesystem::remove(path);
}

TEST_CASE("load_beacon_token_file returns false when the file can't be opened")
{
    cerberus_config_t cerberus{};
    CHECK_FALSE(load_beacon_token_file("Z:/this/path/does/not/exist.txt", cerberus));
}
