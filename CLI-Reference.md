# symbolon CLI Reference

This page documents every command-line flag the `symbolon` executable (`app/`) currently
accepts. It's derived directly from the argument-parsing loop in `app/main.cpp` (the source
of truth) rather than from any other doc — if this page and the code ever disagree, trust the
code and file an issue.

**There is no `--help` flag.** An unrecognized flag doesn't error; it's silently ignored and
the program falls through to whatever mode the flags it *did* recognize select (the default
listen loop, if none matched). Typos in a flag name fail silently — double-check spelling
against this page rather than trusting the program to tell you.

## How to read this page

`symbolon` doesn't have subcommands — it's one binary whose behavior is selected by *which*
flags are present. Internally, main() checks a fixed priority order and runs the first mode
it finds a match for:

`--dump-config` → `--confirm` → `--armed`/`--beacon` → `--atropos-watchdog-test` →
`--decode-wav` → `--tx-test` → `--cat-power-cal` → `--cat-port` (info-only) → default listen
loop

So if you accidentally combine flags from two different modes (say, `--dump-config` and
`--confirm`), the higher-priority one silently wins and the other is ignored — there's no
warning. Two explicit exceptions are checked before any of that: `--armed` and `--beacon`
together is a hard error, and so is `--confirm` combined with either of them. Treat each
section below as its own mode and don't mix flags across sections unless a flag is explicitly
called out as shared.

---

## 1. General / decode flags

The baseline mode with no radio, no CAT, and no transmitting at all.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--version` | — | — | Prints `symbolon <version>` and exits immediately. Ignores every other flag. |
| `--list-devices` | — | — | Lists available audio capture devices and exits. |
| `--device <name>` | device name string | system default capture device | Used by every audio-capturing mode (default listen loop, `--confirm`, `--armed`/`--beacon`) — not decode-wav, which reads a file instead. |
| `--decode-wav <path>` | WAV file path | — | Decodes one WAV file offline and prints results. No audio device is opened, no radio needed. |
| *(no mode flag)* | — | — | Default mode: opens the capture device and runs the receive-only decode loop, printing decodes as they land, until Ctrl+C. |

Example:
```
symbolon                          # live capture on the default audio device
symbolon --list-devices
symbolon --device "USB Audio CODEC"
symbolon --decode-wav sample.wav
```

---

## 2. TX synthesis flags (Phase 2 — no keying)

Synthesizes an FT8 message to a WAV file. **Never asserts PTT, never opens CAT.** Useful for
round-tripping a message through the decoder as a sanity check.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--tx-test "<text>" <wav-path>` | free-text message, output path | — | Takes **two** positional arguments after the flag. Encodes `<text>` as an FT8 waveform and writes it to `<wav-path>`. |
| `--tx-freq <hz>` | audio tone frequency | `1500` Hz | Audio-frequency (not RF) tone used by `--tx-test`. **Also reused, unchanged, as the audio tone frequency for real transmissions in `--armed`/`--beacon` mode** — it isn't a TX-synthesis-only flag despite living in this section; see section 6. |

Example:
```
symbolon --tx-test "CQ KC5CD EM12" out.wav && symbolon --decode-wav out.wav
symbolon --tx-test "KC5CD W1AW -12" reply.wav --tx-freq 1200
```

---

## 3. CAT control flags (Phase 2 — no keying)

Opens real CAT via Hamlib against an X6200 (or, in principle, any Hamlib-supported rig — see
the README's hardware table), applies any requested settings, then prints the rig's current
status. **None of these ever assert PTT** — frequency, mode, preamp, AGC, and power are all
CAT-only reads/writes.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--cat-port <port>` | serial port (e.g. `COM5`, `/dev/ttyUSB0`) | — | Required to enter CAT-info mode at all. Opens CAT at 19200 baud, `RIG_MODEL_X6200`, applies any of the settings below that were also passed, then prints frequency/mode/preamp/AGC/power. |
| `--cat-set-freq <hz>` | frequency in Hz | don't set | Requires `--cat-port`. |
| `--cat-set-mode <mode>` | one of `USB`, `LSB`, `DATA-U`, `DATA-L`, `CW` | don't set | Requires `--cat-port`. Unrecognized value is a hard error before anything is applied. |
| `--cat-preamp <on\|off>` | `on` or `off` | don't set | Requires `--cat-port`. |
| `--cat-agc <setting>` | one of `off`, `slow`, `fast`, `auto` | don't set | Requires `--cat-port`. Unrecognized value is a hard error. |
| `--cat-set-power <watts>` | watts | don't set | Requires `--cat-port`. Rounded to the nearest 0.5 W step. See the TX-power caveat below. |
| `--cat-power-cal <port> <csv-path>` | serial port, output CSV path | — | **Standalone** — does not need `--cat-port`; dispatches to its own interactive mode before `--cat-port` is even checked. Steps commanded power from 0.5 W to 8.0 W in 0.5 W increments, prompts you to read the rig's own display at each step, and writes a `commanded_watts,displayed_watts` CSV. No PTT — setting a power level alone doesn't transmit. |

**TX power caveat** (confirmed against real X6200 hardware): Hamlib's `RIG_MODEL_X6200`
backend has no real TX power table, so `--cat-set-power`/`--cat-power-cal` compute the
watts↔fraction conversion against the X6200's actual 8 W (12 V supply) spec-sheet max rather
than trusting Hamlib's generic (and wrong) fallback ceiling. The *set* side is accurate and
confirmed exact at full power, but the printed readback at partial power doesn't track
linearly — likely real PA drive-curve behavior, not a bug. Trust the rig's own display or a
wattmeter over the printed number except at full power.

Example:
```
symbolon --cat-port COM5                                  # just read and print status
symbolon --cat-port COM5 --cat-set-freq 14074000 --cat-set-mode USB
symbolon --cat-port COM5 --cat-preamp off --cat-agc fast --cat-set-power 5
symbolon --cat-power-cal COM5 x6200-cal.csv                # interactive, at the bench
```

---

## 4. Config / whitelist / gates flags

These build the "effective config" — who symbolon will respond to and under what conditions
— shared by `--confirm`, `--armed`, and `--beacon`. CLI flags always override whatever a
`--config` INI file already set.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--config <path>` | path to an INI file | none | Sections/keys: `[station] call=`, `grid=`; `[whitelist] calls=<csv>`; `[gates] band=`, `freq_min_hz=`, `freq_max_hz=`, `min_snr_db=`. A file that can't be opened prints a warning and is otherwise ignored (not fatal). |
| `--my-call <call>` | your callsign | none | CLI override; wins over `--config`. Required by `--confirm` and `--armed`/`--beacon`. |
| `--my-grid <grid>` | your Maidenhead grid | none | CLI override; wins over `--config`. |
| `--whitelist <csv>` | comma-separated callsigns | none (empty = matches nobody) | CLI override; wins over `--config`. An empty whitelist is explicit opt-in required — it is never a wildcard. Required (non-empty) by `--confirm` and `--armed`/`--beacon`. |
| `--band <name>` | band gate name (e.g. `20m`) | gate disabled | Optional match gate: a decode's band must equal this to pass. Enables `has_band_gate`. This is the *gate value*, distinct from `--current-band` (sections 5/6), which tells the running mode what band the rig is actually on right now. |
| `--freq-min <hz>` / `--freq-max <hz>` | Hz | gate disabled | Both must be passed together — the pair is only applied as a gate if neither is left at its unset (`< 0`) default. |
| `--min-snr <db>` | dB | gate disabled | Optional match gate: a decode's SNR must be ≥ this. |
| `--beacon-token-file <path>` | path to a text file | none | Loads a private free-text beacon token, kept deliberately out of `--config`'s INI (so it's never accidentally versioned/shared alongside the whitelist config). The file's trimmed contents become the token. |
| `--dump-config` | — | — | Prints the effective merged config (INI + CLI overrides) and exits — doesn't run any mode. Never prints the beacon token's literal value, only whether one is configured. |

Example:
```
symbolon --config station.ini --dump-config
symbolon --my-call KC5CD --my-grid EM12 --whitelist W1AW,K1ABC --dump-config
symbolon --config station.ini --band 20m --min-snr -15 --dump-config
```

---

## 5. Confirm-mode flags (Phase 3 — dry run only)

`--confirm` matches incoming decodes against the whitelist/gates config and, for a recognized
exchange step from a whitelisted station, composes the next reply and waits for any keypress
(~2 s window) to "confirm" it. **It never opens CAT and never asserts PTT** — it only shows
what *would* be sent.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--confirm` | — | off | Enters confirm mode. **Requires** `--my-call` and a non-empty `--whitelist` (via `--config` and/or CLI) — exits with an error otherwise. Mutually exclusive with `--armed`/`--beacon`. |
| `--current-band <name>` | any string | `""` | Tells confirm mode what band the rig is on, purely for the `--band` gate comparison. Unlike in armed/beacon mode, this is **not** validated against the recognized band-name table — confirm mode never opens CAT, so there's nothing to derive a dial frequency from. Leave unset and any configured `--band` gate simply always fails closed. |

Example:
```
symbolon --my-call KC5CD --whitelist W1AW --confirm --current-band 20m
symbolon --config station.ini --confirm --current-band 40m --device "USB Audio CODEC"
```

---

## 6. Watchdog bench-test flags (Phase 4)

`--atropos-watchdog-test` is a real-hardware bench verification of `core/atropos.c`'s PTT
watchdog: it asserts real PTT via CAT and **deliberately never releases it**, relying on the
watchdog to force PTT off on its own at ~13.5 s (with an independent 16 s hard backstop in the
test harness itself that reports failure if the watchdog doesn't fire first). This is the
kickoff doc's own required step before `--armed`/`--beacon` are ever run for real.

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--atropos-watchdog-test <port>` | serial CAT port | — | Enters the watchdog bench test. Opens CAT at 19200 baud, `RIG_MODEL_X6200`. Prompts for confirmation ("rig connected to a dummy load, not an antenna") before ever asserting PTT — press Enter to proceed, Ctrl+C to abort. |
| `--atropos-test-power <watts>` | watts | `0.5` W | TX power used for the watchdog test. Low by default since this test deliberately holds PTT for ~13.5 s straight. |

Example:
```
symbolon --atropos-watchdog-test COM5
symbolon --atropos-watchdog-test COM5 --atropos-test-power 1.0
```

**Requires a dummy load, not an antenna.** Follow the project's "never key the antenna
first" ordering (see the kickoff doc) — this is a deliberate 13+ second PTT hold.

---

## 7. Armed / beacon autonomy flags (Phase 4) — **THESE ACTUALLY TRANSMIT**

`--armed` and `--beacon` are the only modes in this codebase that key PTT and put RF on the
air outside a dedicated bench test. Read this whole section, and everything atropos.c's
interlocks are documented to do, before using either.

- `--armed <n>` auto-sequences up to `n` complete QSOs and then disarms.
- `--beacon` runs continuously against whitelist matches with no QSO-count bound.
- They are mutually exclusive with each other, and both are mutually exclusive with
  `--confirm`.

### Required for both `--armed` and `--beacon`

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--my-call <call>` | your callsign | — | See section 4. Missing → hard error. |
| `--whitelist <csv>` | callsigns | — | See section 4. Empty → hard error. |
| `--current-band <name>` | one of `160m`, `80m`, `40m`, `30m`, `20m`, `17m`, `15m`, `12m`, `10m` | — | **Must** be one of these recognized names — unlike confirm mode, armed/beacon derives the actual FT8 USB dial frequency from it, which becomes `core/atropos.c`'s frequency-allowlist interlock (± `--tx-freq-tolerance-hz`). An unrecognized name is a hard error. |
| `--cat-port <port>` | serial port | — | Missing → hard error. |
| `--tx-power <watts>` | watts | **none — must be set explicitly** | Deliberately no default; `<= 0` is a hard error. This is the actual RF output power. |

### Autonomy / interlock tuning (all optional)

| Flag | Argument | Default | Notes |
|---|---|---|---|
| `--armed <n>` | QSO count | — | `n <= 0` is treated as beacon-equivalent internally, but use `--beacon` explicitly for that. |
| `--beacon` | — | — | No QSO-count bound; only the interlocks below (and Ctrl+C) stop it. |
| `--armed-timeout-minutes <m>` | minutes | `0` (disabled) | Wall-clock bound for `--armed`, on top of its QSO count — whichever limit is hit first wins. Not used by `--beacon`. |
| `--dead-man-minutes <m>` | minutes | `0` (disabled) | Auto-disarm after `m` minutes with no operator input or completed QSO. |
| `--max-tx-per-hour <n>` | count | `0` (unlimited) | TX-slot budget per rolling hour. |
| `--max-tx-minutes <m>` | minutes | `0` (unlimited) | Hard session TX-time cap. |
| `--tx-freq-tolerance-hz <hz>` | Hz | `100` | Tolerance band around `--current-band`'s dial frequency for the frequency-allowlist interlock. |
| `--tx-freq <hz>` | Hz | `1500` | Shared with `--tx-test` (section 2) — the audio tone frequency used for the actual transmitted audio. |
| `--tune-vfo` | — | off | If passed, the app tunes the rig's VFO to `--current-band`'s dial frequency itself (and sets USB mode) before arming. **Default is off** — the operator is responsible for having the rig on the right band/mode already. Note this is a convenience only: `atropos_freq_allowed()` re-reads the rig's *live* dial frequency before every auto-send regardless, so a rig left on the wrong band still fails closed at send time whether or not `--tune-vfo` was passed or succeeded. |

The fixed 13.5 s PTT watchdog (same one exercised by `--atropos-watchdog-test`) always
applies and has no flag to change it. Any interlock left at its default (unset/`0`) is
printed as `DISABLED` in the startup banner rather than silently assumed — the project's "the
mechanisms are there and honest" stance.

Both modes print a full summary (call/whitelist, band/dial frequency/tolerance, TX power, VFO
tuning mode, watchdog, and every interlock's current value or `DISABLED`) and then require
pressing Enter — after you've confirmed the rig is on the intended antenna and power — before
arming. Ctrl+C at that prompt aborts with nothing transmitted.

Example:
```
# Armed for 3 QSOs, operator has already tuned/keyed the rig manually:
symbolon --config station.ini --armed 3 --current-band 20m --cat-port COM5 \
    --tx-power 5 --dead-man-minutes 15 --max-tx-per-hour 6

# Continuous beacon, app tunes the VFO itself, generous interlocks:
symbolon --config station.ini --beacon --current-band 40m --cat-port COM5 \
    --tx-power 5 --tune-vfo --dead-man-minutes 30 --max-tx-per-hour 10 --max-tx-minutes 20
```

---

## Flag reuse and gotchas worth knowing

- **`--tx-freq`** is not confined to `--tx-test` — it's the same variable used as the audio
  tone frequency for real transmissions under `--armed`/`--beacon`. Set it consciously if
  you're relying on it there.
- **`--band` vs `--current-band`** are two different things: `--band` (section 4) is a match
  *gate* you configure once (only decodes on this band pass); `--current-band` (sections 5/6)
  is what you tell a running mode the rig is *actually* on right now, and in armed/beacon mode
  it also drives the frequency-allowlist interlock via the recognized band-name table.
- **Mode-flag precedence**: see "How to read this page" above — if you accidentally combine
  flags belonging to two different modes, the higher-priority one silently wins with no
  warning printed.
- **No validation on numeric parsing.** Flags parsed with `std::stof`/`std::stod`/`std::stoi`
  (e.g. `--tx-freq`, `--cat-set-freq`, `--max-tx-per-hour`) will throw and crash the process
  on a non-numeric argument rather than printing a friendly error. Double-check values before
  scripting these.

## See also

- `README.md`'s "Running" section for the condensed version of this page and the hardware/
  license context around it.
- `symbolon-kickoff-prompt.md` at the repo root for the full design rationale behind the
  phase structure, the interlocks in `core/atropos.c`, and the "never key the antenna first"
  verification ordering these flags are built around.
