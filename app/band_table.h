#ifndef SYMBOLON_BAND_TABLE_H
#define SYMBOLON_BAND_TABLE_H

#include <cstdint>
#include <string>

// The kickoff doc's own canonical FT8 USB dial frequencies -- used both for the frequency-
// allowlist interlock (core/atropos.c) and, eventually, the Phase 5 band sweep. Treat this
// table as canonical rather than deriving it ad hoc anywhere else.

// Resolves a band name ("20m", "40m", ...) to its FT8 dial frequency in Hz. Returns false
// (out_hz left untouched) for an unrecognized name.
bool band_to_dial_hz(const std::string& band, uint64_t* out_hz);

#endif // SYMBOLON_BAND_TABLE_H
