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

// Issue #13: every operational value the CLI can set should also be settable from the INI
// file. These cases cover the new [device]/[cat]/[operation]/[autonomy]/[logging] sections
// plus [gates]'s legacy_mode key, following this file's existing style.

TEST_CASE("load_config_file parses device, cat, operation, autonomy, and logging sections")
{
    std::string path = make_scratch_path(".ini");
    write_file(path,
        "[device]\n"
        "capture = Microphone (USB Audio Device)\n"
        "playback = Speakers (USB Audio Device)\n"
        "\n"
        "[cat]\n"
        "port = COM17\n"
        "tune_vfo = true\n"
        "\n"
        "[operation]\n"
        "current_band = 20m\n"
        "tx_freq_hz = 1500\n"
        "tx_power_watts = 5\n"
        "tx_freq_tolerance_hz = 100\n"
        "\n"
        "[autonomy]\n"
        "armed_timeout_minutes = 10\n"
        "dead_man_minutes = 3\n"
        "max_tx_per_hour = 6\n"
        "max_tx_minutes = 15\n"
        "\n"
        "[logging]\n"
        "log_db = station.sqlite\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));

    CHECK(cfg.app.capture_device == "Microphone (USB Audio Device)");
    CHECK(cfg.app.playback_device == "Speakers (USB Audio Device)");
    CHECK(cfg.app.cat_port == "COM17");
    CHECK(cfg.app.tune_vfo);
    CHECK(cfg.app.current_band == "20m");
    CHECK(cfg.app.tx_freq_hz == doctest::Approx(1500.0));
    CHECK(cfg.app.tx_power_watts == doctest::Approx(5.0));
    CHECK(cfg.app.tx_freq_tolerance_hz == doctest::Approx(100.0));
    CHECK(cfg.app.armed_timeout_minutes == doctest::Approx(10.0));
    CHECK(cfg.app.dead_man_minutes == doctest::Approx(3.0));
    CHECK(cfg.app.max_tx_per_hour == 6);
    CHECK(cfg.app.max_tx_minutes == doctest::Approx(15.0));
    CHECK(cfg.app.log_db_path == "station.sqlite");

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file leaves the new app sections at their unset sentinels when absent")
{
    std::string path = make_scratch_path(".ini");
    write_file(path, "[station]\ncall = KC5CD\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));

    CHECK(cfg.app.capture_device.empty());
    CHECK(cfg.app.playback_device.empty());
    CHECK(cfg.app.cat_port.empty());
    CHECK_FALSE(cfg.app.tune_vfo);
    CHECK(cfg.app.current_band.empty());
    CHECK(cfg.app.tx_freq_hz < 0.0);
    CHECK(cfg.app.tx_power_watts < 0.0);
    CHECK(cfg.app.tx_freq_tolerance_hz < 0.0);
    CHECK(cfg.app.armed_timeout_minutes < 0.0);
    CHECK(cfg.app.dead_man_minutes < 0.0);
    CHECK(cfg.app.max_tx_per_hour < 0);
    CHECK(cfg.app.max_tx_minutes < 0.0);
    CHECK(cfg.app.log_db_path.empty());

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file parses [gates] legacy_mode without documenting it anywhere else")
{
    std::string path = make_scratch_path(".ini");
    write_file(path, "[gates]\nlegacy_mode = true\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));
    CHECK(cfg.cerberus.beacon_allow_token_alone);

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file rejects a malformed boolean and leaves the field at its default")
{
    std::string path = make_scratch_path(".ini");
    write_file(path, "[cat]\ntune_vfo = yes\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));
    CHECK_FALSE(cfg.app.tune_vfo);

    std::filesystem::remove(path);
}

TEST_CASE("load_config_file skips an unknown key in a known new section without aborting")
{
    std::string path = make_scratch_path(".ini");
    write_file(path,
        "[device]\n"
        "bogus_key = whatever\n"
        "capture = RealDevice\n");

    SymbolonConfig cfg{};
    REQUIRE(load_config_file(path, cfg));
    CHECK(cfg.app.capture_device == "RealDevice");

    std::filesystem::remove(path);
}
