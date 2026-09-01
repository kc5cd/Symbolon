#ifndef SYMBOLON_CONFIG_H
#define SYMBOLON_CONFIG_H

#include <string>

extern "C" {
#include "cerberus.h"
#include "qso.h"
}

// Plain hand-written INI, no vendored parser -- the project's standing choice (see
// .claude/state/context.md's "Config format" entry, made before Phase 1). CLI flags always
// win over the file; see app/main.cpp's own option-merge order.
struct SymbolonConfig {
    cerberus_config_t cerberus{};
    qso_config_t qso{};
};

// Parses `path` ([station] call/grid, [whitelist] calls=csv, [gates]
// band/freq_min_hz/freq_max_hz/min_snr_db) into `out`. `out` should already be
// zero-initialized; callers that want CLI overrides to win should apply this function first,
// then apply overrides on top -- not the other way around. A malformed line is skipped with a
// warning to stderr rather than aborting the whole file. Returns false only if `path` itself
// can't be opened.
bool load_config_file(const std::string& path, SymbolonConfig& out);

// Reads the entire contents of `path`, trims trailing whitespace/newlines, and copies it into
// out_cerberus.beacon_token. Deliberately separate from load_config_file() -- the private
// beacon token is kept out of the main whitelist/gates config by design (agreed with the
// user: "separate file/flag, kept out of the main config"), so it's never looked for inside
// an INI file at all.
bool load_beacon_token_file(const std::string& path, cerberus_config_t& out_cerberus);

#endif // SYMBOLON_CONFIG_H
