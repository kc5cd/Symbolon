# Symbolon

An FT8 selective responder for measuring bidirectional HF propagation between two specific
stations. It sits on a band, answers only a configured target station, and records both
directions of every exchange — what the other end heard of you, and what you heard of them —
into a queryable dataset. Full design and rationale: `symbolon-kickoff-prompt.md`.

**Status:** early development. Phase 0 (build skeleton), Phase 1 (receive-only decode), and
Phase 2 (CAT via Hamlib, TX message synthesis, no keying) are complete. Phase 3 (rules
engine, QSO state machine, `--confirm` dry-run mode) is complete. Phase 4 (PTT watchdog and
safety interlocks, `--armed`/`--beacon` autonomy) is built and unit-tested, with the PTT
watchdog itself bench-verified against real hardware — `--armed`/`--beacon` have not yet been
run for real, per `symbolon-kickoff-prompt.md`'s "never key the antenna first" verification
ordering. `core/mnemosyne.c`'s heard/QSO observation log — planned as Phase 5 in the kickoff
(GitHub issue #10) — is also built ahead of schedule and wired into `--confirm`/`--armed`/
`--beacon` via `app/sqlite_sink.cpp`; see `--log-db` below.

Phase 1's decoder recovers roughly 50-70% of what WSJT-X decodes on the same audio, both
offline (a ~30-file WAV regression corpus) and live (WSJT-X-recorded samples) — a known,
documented ceiling in the vendored `ft8_lib` decode algorithm, not a bug. A revisit/rewrite
of the decode library is planned as a dedicated pass later, before the eventual on-radio
(ARMv7) native build.

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
symbolon --list-devices           # show available capture AND playback devices
symbolon --device "<name>"        # capture on a specific device instead of the default
symbolon --playback-device "<name>" # armed/beacon: play synthesized TX audio on a specific device (default: system default)
symbolon --decode-wav <path>      # decode one WAV file and print results, no device needed
symbolon --tx-test "<text>" <wav> # synthesize an FT8 message to a WAV file, no radio touched
symbolon --tx-freq <hz>           # audio frequency for --tx-test (default 1500 Hz)
symbolon --cat-port <port>        # open CAT via Hamlib, apply any settings below, print status
symbolon --cat-set-freq <hz>      # set VFO frequency (with --cat-port)
symbolon --cat-set-mode <mode>    # USB | LSB | DATA-U | DATA-L | CW (with --cat-port)
symbolon --cat-preamp <on|off>    # toggle the front-end preamp (with --cat-port)
symbolon --cat-agc <setting>      # off | slow | fast | auto (with --cat-port)
symbolon --cat-set-power <watts>  # TX power, rounded to the nearest 0.5W (with --cat-port)
symbolon --cat-power-cal <port> <csv> # interactive: step 0.5W increments, log the rig's own display
symbolon --config <path>          # load whitelist/gates from an INI file
symbolon --my-call <call> --my-grid <grid> --whitelist <csv>  # CLI overrides, win over --config
symbolon --band <name> --freq-min <hz> --freq-max <hz> --min-snr <db>  # optional match gates
symbolon --beacon-token-file <path>  # private free-text beacon token, kept out of --config
symbolon --dump-config            # print the effective whitelist/gates config and exit
symbolon --confirm                # confirm-mode QSO dry run (see below) -- needs --my-call + --whitelist
symbolon --current-band <name>    # tells confirm mode what band it's on, for the band gate
symbolon --log-db <path>          # heard/QSO observation log, confirm+armed/beacon (default: symbolon.sqlite)
symbolon --atropos-watchdog-test <port>   # bench test: real PTT, deliberately hung (see below)
symbolon --atropos-test-power <watts>     # TX power for the watchdog test (default 0.5W)
symbolon --armed <n>              # auto-sequence up to n QSOs, then disarm -- ACTUALLY TRANSMITS
symbolon --beacon                 # run continuously against whitelist matches -- ACTUALLY TRANSMITS
symbolon --tx-power <watts>       # TX power for armed/beacon (required, no default)
symbolon --dead-man-minutes <m>   # auto-disarm after m minutes with no operator input/completed QSO
symbolon --max-tx-per-hour <n>    # TX-slot budget per rolling hour (0 = unlimited)
symbolon --max-tx-minutes <m>     # hard session TX-time cap (0 = unlimited)
symbolon --armed-timeout-minutes <m>  # wall-clock bound for --armed, on top of its QSO count
symbolon --tx-freq-tolerance-hz <hz>  # dial-frequency allowlist tolerance (default 100 Hz)
symbolon --tune-vfo                # armed/beacon: let the app tune the VFO itself (default: operator does it)
```

`--confirm` is Phase 3 (rules engine + QSO state machine): matches incoming decodes against
the configured whitelist/gates and, for a recognized exchange step with a whitelisted station,
composes the next reply and waits for any keypress (~2s window) to confirm — **dry run only**,
it never opens CAT and never asserts PTT.

`--atropos-watchdog-test` is Phase 4's own kickoff-specified bench verification: asserts real
PTT via CAT and deliberately never releases it, relying entirely on `core/atropos.c`'s
watchdog to force it off automatically around 13.5s (an independent hard backstop in the test
harness itself fires at 16s and reports failure if the watchdog doesn't). Requires a dummy
load, not an antenna, and prompts for confirmation before ever asserting PTT.

`--armed`/`--beacon` are Phase 4's autonomy modes and **do actually transmit** — the only
modes in this codebase that key PTT outside a dedicated bench test. Both need `--my-call`,
`--whitelist`, `--current-band` (a recognized band name — its dial frequency becomes the
`core/atropos.c` frequency-allowlist interlock, ±`--tx-freq-tolerance-hz`), `--cat-port`, and
`--tx-power` (no default, set consciously). Both open duplex audio (capture for decoding,
playback for the synthesized TX signal) — `--playback-device` selects the playback side
explicitly, same as `--device` already does for capture; on a real USB audio codec (the
X6200's included) the two directions are commonly named differently (e.g. `Microphone (...)`
vs `Speakers (...)`), so neither is ever guessed from the other — omitting either flag falls
back to that direction's system default, and both resolved names are printed in the startup
banner so it's never silently wrong. `--armed <n>` auto-sequences up to `n` complete
QSOs and then disarms (or `--armed-timeout-minutes`, whichever comes first); `--beacon` runs
continuously against whitelist matches with no QSO-count bound. Every atropos.c interlock
applies — the fixed 13.5s PTT watchdog, `--dead-man-minutes` (auto-disarm on no operator
input/completed exchange), `--max-tx-per-hour`, and `--max-tx-minutes` — and any interlock
left at its default (0/unset) is printed as `DISABLED` in the startup banner rather than
silently assumed, per this project's "the mechanisms are there and honest" stance. A separate
pre-flight check (not an atropos.c interlock, and not a hard gate) also queries the OS's own
NTP-sync status and prints it in the banner — FT8 needs roughly ±1s timing accuracy, so an
unsynced clock is flagged loudly but arming is still the operator's call. Both modes
print a full interlock summary and require pressing Enter (after confirming the rig is on the
intended antenna and power) before ever arming.

`--log-db <path>` (both `--confirm` and `--armed`/`--beacon`) is `core/mnemosyne.c`'s
observation log: every decode from a whitelisted station is recorded regardless of whether
it was directed at me, a CQ, or ever became a full exchange — "heard but not worked" is
still real propagation evidence for this project's core research goal. Completed QSOs are
recorded too, with both SNR directions and the asymmetry the whole project exists to measure.
Written via `app/sqlite_sink.cpp` to a SQLite database (WAL mode) plus a `_observations.csv`/
`_qso_log.csv` sidecar next to it for eyeballing without a SQLite client. A failure to open
the log is a warning, not fatal — confirm/armed/beacon keep running without it.

`--tx-test` and everything under `--cat-port` are Phase 2 (CAT + TX synthesis, **no
keying**) — none of them ever assert PTT, including the power/mode/AGC/preamp controls.
Round-trip a synthesized message through the decoder as a sanity check:
`symbolon --tx-test "CQ KC5CD EM12" out.wav && symbolon --decode-wav out.wav`.

**TX power caveat, confirmed against real X6200 hardware**: Hamlib's `RIG_MODEL_X6200`
backend doesn't populate a real TX power table, so `--cat-set-power` computes the
watts↔fraction conversion itself against the X6200's actual 8W (12V supply) spec-sheet max
rather than trusting Hamlib's generic fallback (which silently assumed a wrong, much higher
ceiling). The *set* side is accurate — confirmed exact at full power (8W commanded → 8W read
back) — but the printed readback at partial power settings doesn't track linearly with what
was set, most likely real PA drive-curve behavior rather than a bug in this code. Don't treat
that printed number as exact except at full power; check the rig's own display or a wattmeter
for actual output at a given setting. `--cat-power-cal` builds a real commanded-vs-displayed
table interactively at the bench; the resulting CSV is a local artifact, not committed here.

The `ctest` suite includes a regression corpus (`corpus.aggregate`) that decodes ~30 real
FT8 recordings from `third_party/ft8_lib/test/wav/` and checks aggregate recall against
known-good transcripts. The gate is 70% recall, not 100% — `ft8_lib`'s own decoder tops out
around 73% on this corpus, short of what WSJT-X's more sophisticated decoder achieves.

## License

GPL-3.0-or-later — see `LICENSE`. Third-party dependencies (vendored and linked) carry their
own licenses; see `THIRD-PARTY-LICENSES.md` for the full breakdown, including how the one
copyleft dependency (Hamlib, LGPL-2.1, linked not vendored) interacts with that choice.
