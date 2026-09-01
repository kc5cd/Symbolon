# Symbolon

An FT8 selective responder for measuring bidirectional HF propagation between two specific
stations. It sits on a band, answers only a configured target station, and records both
directions of every exchange — what the other end heard of you, and what you heard of them —
into a queryable dataset. Full design and rationale: `symbolon-kickoff-prompt.md`.

**Status:** early development. Phase 0 (build skeleton) and Phase 1 (receive-only decode)
are complete — `symbolon` captures audio, decodes FT8 over a 15 s slot, and prints results
to the console. Not yet capable of transmitting.

## Supported hardware

Rig control (CAT) is done through [Hamlib](https://github.com/Hamlib/Hamlib), not a
hand-rolled protocol implementation. In principle, **any radio Hamlib supports** can be
driven at the CAT layer — see Hamlib's own
[supported rig list](https://github.com/Hamlib/Hamlib/wiki/Supported-Radios) for the full
set.

In practice, this project is built and actively tested against one radio:

| Radio | Hamlib model | Notes |
|---|---|---|
| Xiegu X6200 | `RIG_MODEL_X6200` | CI-V (Icom-compatible) protocol, CI-V address `0xA4`, 300–19200 baud. Connects via the radio's USB-C **DEV** port, which presents both CAT (SERIAL-B, 19200 bps, through a CH342 USB bridge — install its driver on Windows) and a USB sound card for audio in/out. |

Getting a different Hamlib-supported rig working should mostly be a matter of pointing
`hal_cat_config_t.rig_model` (see `hal/hal_cat.h`) at the right `RIG_MODEL_*` constant and
adjusting the serial parameters — nothing in `core/` or `app/` assumes X6200-specific
behavior. That path is untested against real hardware other than the X6200, though.

Audio capture/playback goes through [miniaudio](https://github.com/mackron/miniaudio)
(WASAPI on Windows, ALSA on Linux), independent of which rig is in use.

## Platforms

Windows and Linux, from a single codebase, built with the same CMake project. See
`CMakePresets.json` for the available configure/build/test presets.

## Building

Requires:
- CMake ≥ 3.25
- A C11 + C++17 compiler (developed against MinGW-w64 GCC 13.1 on Windows; any recent
  GCC/Clang on Linux should work)
- [Ninja](https://ninja-build.org/)
- Hamlib development headers/library (only needed once CAT support is wired up — not yet a
  Phase 0/1 requirement)

```
cmake --preset win-x64-debug      # or linux-x64-debug
cmake --build --preset win-x64-debug
ctest --preset win-x64-debug
```

Third-party dependencies (`ft8_lib`, Unity, doctest, miniaudio, SQLite) are vendored under
`third_party/`, unmodified from upstream — see `third_party/CMakeLists.txt` and each
subdirectory's own version notes.

## Running

```
symbolon                          # live capture on the default audio device
symbolon --list-devices           # show available capture devices
symbolon --device "<name>"        # capture on a specific device instead of the default
symbolon --decode-wav <path>      # decode one WAV file and print results, no device needed
```

The `ctest` suite includes a regression corpus (`corpus.aggregate`) that decodes ~30 real
FT8 recordings from `third_party/ft8_lib/test/wav/` and checks aggregate recall against
known-good transcripts. The gate is 70% recall, not 100% — `ft8_lib`'s own decoder tops out
around 73% on this corpus, short of what WSJT-X's more sophisticated decoder achieves.

## License

GPL-3.0-or-later — see `LICENSE`. Third-party dependencies (vendored and linked) carry their
own licenses; see `THIRD-PARTY-LICENSES.md` for the full breakdown, including how the one
copyleft dependency (Hamlib, LGPL-2.1, linked not vendored) interacts with that choice.
