#ifndef SYMBOLON_CONFIG_H
#define SYMBOLON_CONFIG_H

#include <string>

extern "C" {
#include "cerberus.h"
#include "qso.h"
}

// App/HAL-layer operational settings -- deliberately its own struct, not folded into
// cerberus_config_t/qso_config_t, since those are core/ structs and core/ has no business
// knowing about device names, serial ports, or TX budgets (core/'s no-OS-calls rule).
// Unset numeric fields use a negative sentinel (none of these are legitimately negative in
// practice), matching the sentinel style ConfigOptions (app/main.cpp) already uses for
// freq_min_hz/freq_max_hz; unset strings are just empty. Booleans default false and, per
// this project's own CLI shape (no --no-tune-vfo exists), can only be turned on by either
// source -- see app/main.cpp's build_effective_config() for the exact merge rule.
struct AppOperationalConfig {
    // [device]
    std::string capture_device;
    std::string playback_device;

    // [cat]
    std::string cat_port;
    bool tune_vfo = false;

    // [operation]
    std::string current_band;
    double tx_freq_hz = -1.0;
    double tx_power_watts = -1.0;
    double tx_freq_tolerance_hz = -1.0;

    // [autonomy]
    double armed_timeout_minutes = -1.0;
    double dead_man_minutes = -1.0;
    int max_tx_per_hour = -1;
    double max_tx_minutes = -1.0;

    // [logging]
    std::string log_db_path;
};

// Plain hand-written INI, no vendored parser -- the project's standing choice (see
// .claude/state/context.md's "Config format" entry, made before Phase 1). CLI flags always
// win over the file; see app/main.cpp's own option-merge order.
struct SymbolonConfig {
    cerberus_config_t cerberus{};
    qso_config_t qso{};
    AppOperationalConfig app{};
};

// Parses `path` into `out`: [station] call/grid, [whitelist] calls=csv, [gates]
// band/freq_min_hz/freq_max_hz/min_snr_db/legacy_mode, [device] capture/playback, [cat]
// port/tune_vfo, [operation] current_band/tx_freq_hz/tx_power_watts/tx_freq_tolerance_hz,
// [autonomy] armed_timeout_minutes/dead_man_minutes/max_tx_per_hour/max_tx_minutes,
// [logging] log_db. `out` should already be zero-initialized; callers that want CLI
// overrides to win should apply this function first, then apply overrides on top -- not the
// other way around. A malformed line is skipped with a warning to stderr rather than
// aborting the whole file. Returns false only if `path` itself can't be opened.
//
// [gates]'s legacy_mode key deliberately gets no mention in README or any --help text --
// same "not readily apparent" treatment as the --legacy-mode CLI flag it backs (see
// cerberus.h's beacon_allow_token_alone comment). It's real and parsed like any other key;
// it's just never documented.
bool load_config_file(const std::string& path, SymbolonConfig& out);

// Reads the entire contents of `path`, trims trailing whitespace/newlines, and copies it into
// out_cerberus.beacon_token. Deliberately separate from load_config_file() -- the private
// beacon token is kept out of the main whitelist/gates config by design (agreed with the
// user: "separate file/flag, kept out of the main config"), so it's never looked for inside
// an INI file at all.
bool load_beacon_token_file(const std::string& path, cerberus_config_t& out_cerberus);

#endif // SYMBOLON_CONFIG_H
