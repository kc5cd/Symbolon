# Kickoff Prompt: Symbolon — FT8 Selective Responder (C11 core / C++17 app)

Paste this whole document as your first message in a fresh `claude` CLI session, started at
`Z:\Repositories\C_C++\Symbolon`. Everything needed to begin is in this document — the
design was worked out in a prior planning session and the decisions below are settled.

---

## Name

**Symbolon.** A *symbolon* was a token — a shard of pottery or bone — broken deliberately in
two, each party keeping one half. Reunited later, sometimes generations later, the halves
were matched along the break to prove that this stranger was in fact the known
correspondent. It's the root of our word "symbol."

Two specific parties, each holding a half, recognized only by matching — that's the
whitelist. And FT8 literally transmits **symbols**, 79 of them per slot, the unit the
decoder recovers. The name is the myth of selective recognition and the DSP primitive at
once.

Binary name: `symbolon`. Dispatch API prefix: `sym_` (`sym_dispatch()`, `sym_tier_t`,
`SYM_TIER_OBSERVE`, …) — used throughout this document; the architecture tree below uses the
real filenames, lore names included. A few internal modules carry matching Greek names,
paired with a plain-language comment so nobody has to hold the mythology in their head to
read the code:

| File | Does | Myth |
|---|---|---|
| `core/horae.c` | UTC 15 s slot alignment and slicing | The Horae guard Olympus' gates — timing *and* gatekeeping |
| `core/argus.c` | STFT + waterfall build, candidate search, decode | Hundred-eyed, ever-watchful, never fully asleep |
| `core/cerberus.c` | Rules / match engine | Decides what passes and what is turned away |
| `core/atropos.c` | PTT watchdog + safety interlocks | The Fate who **cuts the thread** |
| `core/mnemosyne.c` | Observation records / store callback | Titaness of Memory — the record is the deliverable |
| `app/odyssey.cpp` | Multi-band sweep orchestration | The long structured journey across many places before coming home |

Everything else (`ring.c`, `tx.c`, `qso.c`, `api.c`) keeps plain functional names — the lore
is a light coat of paint on the interesting modules, not a puzzle layered over the whole
tree. Don't invent more Greek names beyond this table without checking with me first.

---

## Project context

I'm KC5CD, an Extra Class amateur operator. I need to measure HF propagation between my QTH
and a friend's station, in both directions, across multiple bands. My radio is a **Xiegu
X6200**.

FT8 is the right instrument for this. It decodes ~20 dB below the noise floor, and every
completed exchange carries two signal reports — what he heard of me, and what I heard of
him. The difference between those two numbers is the measurement I actually care about,
because it distinguishes "the band is closed" from "one end of this path is deaf."

WSJT-X can do this by hand, but it can't be left to run a structured multi-band test, it
answers whoever calls rather than only the station under test, and its log is built for
chasing contacts rather than for analysis. This tool is deliberately narrower: it responds
**only** to a configured set of stations and messages, records both directions of every
exchange into a queryable store, and can eventually run a scripted band sweep unattended.

**Deliverable:** a cross-platform CLI tool (Windows + Linux, both from day one) that sits on
a band, answers only the target station, completes FT8 QSOs at a configurable level of
autonomy, and writes a bidirectional SNR dataset I can plot.

---

## Settled decisions — do not relitigate these

These were argued out already. If you think one is wrong, say so once, briefly, with a
concrete reason — then follow it unless I tell you otherwise. Do not silently redesign.

| Decision | Choice |
|---|---|
| Core language | **C11** for `core/` and `hal/` |
| App layer | **C++17** for `app/` (PC only, never compiled for the radio) |
| FT8 DSP | **Vendor `ft8_lib`** (kgoba, MIT). Do not reimplement the modem |
| Audio | **miniaudio** (single-header C) — one API over WASAPI + ALSA |
| CAT | **Hamlib**, `RIG_MODEL_X6200` |
| Store | **SQLite** (vendored amalgamation) + CSV export |
| Build | **CMake ≥ 3.25** (floor is actually 3.21, for `ctest --output-junit` — see Testing framework), presets for `win-x64` and `linux-x64` at Phase 0; an ARMv7 cross preset joins at Phase 7, not before |
| MCP | **Deferred to Phase 6.** The tier-gated dispatch seam is built from day one |
| ADIF / LoTW | **Explicitly out of scope.** Not wanted |

### Why C and not .NET/C++ — the reasoning, so you don't undo it

The obvious answer for a Windows CLI is C# — better tooling, easier audio, a first-class MCP
SDK. That was the initial recommendation and it was wrong, for one reason:

**The X6200 is a Linux computer with a radio attached.** It runs an Allwinner "Sunxi"
**ARMv7 quad-core** with a Mali400 GPU, **Linux kernel 5.8.9**, glibc, and ALSA +
PulseAudio. The vendor GUI is a Qt app at `/usr/app_qt/` storing config in SQLite at
`xparam.db`. Root access is a solved problem (see references). So a responder running *on
the radio itself*, with no PC at all, is a realistic follow-on project.

A C responder cross-compiles to roughly 300 KB and drops onto the rootfs with `scp`. A
self-contained .NET one is ~75 MB with a ~45 MB resident footprint, on an appliance rootfs
already hosting a Qt GUI and PulseAudio, targeting `linux-arm` — .NET's least-invested
platform. Not impossible; indefensible.

And the reuse is real: **capture → decode → match → sequence → synthesize → key is ~80% of
the code and is identical on both targets.** Only the CLI chrome, Hamlib CAT, and MCP are
PC-only.

**C rather than C++ for the core** because every dependency is already C (`ft8_lib`, Hamlib,
miniaudio, SQLite, ALSA), and C++ adds a `libstdc++` ABI you would have to version-match
against the radio's rootfs or static-link away, forfeiting the size advantage that justified
the choice. A C core links into C++ with zero friction, which is why `app/` can be C++17.

**Rejected alternative:** driving WSJT-X over its UDP protocol (port 2237, `Decode`/`Reply`
messages). That would be ~15% of the work and uses a battle-tested decoder, but it forecloses
the on-radio path entirely and constrains responses to what the `Reply` message can express.

The on-radio port is **aspirational, not committed**. Write disciplined portable C and keep
the HAL boundary clean because that is good practice regardless — but when portability and
PC-side quality genuinely conflict, favor the PC. Do not contort the design for a target we
may never build.

---

## Architecture

The organizing constraint: `core/` and `hal/` must compile for the X6200 unchanged. So
**`core/` makes no OS calls at all** — no file I/O, no sockets, no threads, no clock reads,
no `printf`. Everything platform-touching goes through `hal/`. Everything convenient but
heavy lives in `app/`, which a radio build never sees.

```
third_party/
  ft8_lib/             vendored from upstream, unmodified, mirroring its own layout:
    ft8/                 encode/decode/message packing — core/ links this
    fft/                 kissfft — core/ links this
    common/              wave.c (WAV I/O) — file I/O, so app/ and tests link this, core/ does NOT
    demo/, test/         upstream's gen_ft8/decode_ft8 demos and its own test.c + test/wav/ corpus
  unity/                vendored ThrowTheSwitch Unity — core/ test framework
  doctest/              vendored doctest — app/ test framework
core/                  portable C11 — zero OS dependencies, builds for both targets
  ring.c/h             lock-free SPSC sample ring: audio thread → decode thread
  horae.c/h            UTC 15 s slot alignment and slicing
  argus.c/h            STFT → ftx_waterfall_t, candidate search, decode
  cerberus.c/h         match engine: whitelist ∧ directed-at-me ∧ text ∧ band/SNR gates
  qso.c/h              QSO state machine + autonomy modes
  tx.c/h               message compose + GFSK tone synthesis
  atropos.c/h          PTT watchdog, dead-man timer, TX budget, freq allowlist
  mnemosyne.c/h        observation records, emitted via callback (core never writes files)
  api.c/h              ◄── tier-gated command/query dispatch. THE MCP SEAM
hal/                   thin platform seam — one implementation per target
  hal_audio.h            audio_miniaudio.c   │  audio_x6200.c  (ALSA direct)      [stub]
  hal_cat.h              cat_hamlib.c        │  cat_x6200.c    (backend IPC)      [stub]
  hal_time.h             time_win.c / time_posix.c
app/                   C++17, PC only
  main.cpp             CLI, config, console UI
  sqlite_sink.cpp      mnemosyne callback → SQLite + CSV
  odyssey.cpp          multi-band sweep scripting
  mcp/                 ← Phase 6, wraps core/api.c
```

`hal/cat_x6200.c` is the genuine unknown — on the radio there is no CAT, you talk to the
backend directly, and nobody has documented that yet. Leave it an unimplemented stub. The
point is that the **seam exists from commit one**, so the eventual port is a new file rather
than a refactor.

### The dispatch API — `core/api.h`

Both the CLI and the future MCP server call `sym_dispatch()`. The privilege tier is chosen
once at startup; every command declares the minimum tier it requires, so the gate has
exactly one implementation. **The CLI itself always runs full-privilege** — a human at the
keyboard is the control operator, no tier applies to them. `--mcp-tier` constrains only what
the MCP surface (Phase 6) is allowed to advertise and invoke.

```c
typedef enum {
    SYM_TIER_OBSERVE = 0,   /* read decodes, SNR history, rig state, log */
    SYM_TIER_CONTROL,       /* + QSY, mode, whitelist/rules, arm/disarm  */
    SYM_TIER_ORCHESTRATE,   /* + run a scripted band sweep               */
    SYM_TIER_TRANSMIT,      /* + compose and key an arbitrary message    */
} sym_tier_t;

typedef struct {
    const char* name;
    sym_tier_t   min_tier;
    sym_rc_t   (*invoke)(sym_ctx_t*, const sym_args_t*, sym_result_t*);
} sym_command_t;
```

Ordering is deliberate: **orchestrate ranks below transmit**, because a scripted sweep has a
far narrower blast radius than arbitrary keying.

When the MCP server is eventually built, `sym_command_list(tier)` filters the advertised tool
list — tools above the selected tier are **never registered**, not registered-and-refused.
An unadvertised tool cannot be talked into firing. The dispatcher re-checks the tier on
every invoke regardless; the two checks are independent on purpose.

I want to select the tier as a startup flag (`--mcp-tier=observe|control|orchestrate|transmit`)
and have the MCP surface expose that level *and below*, nothing above.

---

## FT8 protocol reference

All verified against `ft8_lib/ft8/constants.h`. Use these rather than deriving them.

| Quantity | Value |
|---|---|
| Sample rate | 12000 Hz mono (WSJT-X convention) |
| Symbol period | 0.160 s → **1920 samples/symbol** at 12 kHz |
| Channel symbols | 79 (`FT8_NN`) = 58 data (`FT8_ND`) + 3 × 7 Costas |
| Costas sync | `FT8_NUM_SYNC` 3, `FT8_LENGTH_SYNC` 7, `FT8_SYNC_OFFSET` 36 → offsets 0, 36, 72 |
| TX duration | 79 × 0.160 = **12.64 s**, starting 0.5 s into the slot |
| Slot | `FT8_SLOT_TIME` 15.0 s, aligned to UTC |
| Tone spacing | 6.25 Hz (8-FSK); occupied bandwidth ~50 Hz |
| FEC | LDPC(174, 91) — `FTX_LDPC_N` 174, `FTX_LDPC_K` 91, `FTX_LDPC_M` 83 |
| CRC | 14-bit, polynomial `0x2757` |
| Payload | `FTX_PAYLOAD_LENGTH_BYTES` 10 (77 bits + 14 CRC = 91) |

FT4 constants also exist (`FT4_SLOT_TIME` 7.5 s, `FT4_NN` 105, symbol period 0.048 s) if we
ever want it. Not in scope now.

### `ft8_lib` API surface

Verified from the headers. Decode path:

```c
int  ftx_find_candidates(const ftx_waterfall_t* power, int num_candidates,
                         ftx_candidate_t heap[], int min_score);
bool ftx_decode_candidate(const ftx_waterfall_t* power, const ftx_candidate_t* cand,
                          int max_iterations, ftx_message_t* message,
                          ftx_decode_status_t* status);
```

```c
typedef struct {
    int max_blocks, num_blocks, num_bins, time_osr, freq_osr;
    WF_ELEM_T* mag; int block_stride; ftx_protocol_t protocol;
} ftx_waterfall_t;

typedef struct { int16_t score, time_offset, freq_offset;
                 uint8_t time_sub, freq_sub; } ftx_candidate_t;

typedef struct { float freq, time; int ldpc_errors;
                 uint16_t crc_extracted, crc_calculated; } ftx_decode_status_t;
```

Message packing (`ft8/message.h`):

```c
typedef struct { uint8_t payload[FTX_PAYLOAD_LENGTH_BYTES]; uint16_t hash; } ftx_message_t;

ftx_message_rc_t ftx_message_encode_std(ftx_message_t*, ftx_callsign_hash_interface_t*,
                                        const char* call_to, const char* call_de,
                                        const char* extra);
ftx_message_rc_t ftx_message_decode_std(const ftx_message_t*, ftx_callsign_hash_interface_t*,
                                        char* call_to, char* call_de, char* extra,
                                        ftx_field_t field_types[]);
ftx_message_rc_t ftx_message_encode_free(ftx_message_t*, const char* text);
void             ftx_message_decode_free(const ftx_message_t*, char* text);
ftx_message_type_t ftx_message_get_type(const ftx_message_t*);
```

`ftx_callsign_hash_interface_t` is a pair of callbacks (`lookup_hash` / `save_hash`) we must
implement — it backs non-standard and hashed callsigns. A small fixed-size table is fine.

`ftx_message_decode_std()` giving us `call_to` / `call_de` / `extra` as separate fields is
what makes the rules engine clean — we match on structure, not on string-scraping.

### Suggested waterfall parameters

`time_osr = freq_osr = 2`, monitoring roughly 200–3000 Hz. Bin spacing becomes 3.125 Hz;
blocks per slot ≈ 15 / 0.16 ≈ 93. Budget the `mag` buffer accordingly. Tune against real
signals in Phase 1 — if our decode set diverges from WSJT-X on the same audio, these
parameters are the first suspect.

---

## Hardware facts

**Xiegu X6200, PC side:**
- Connects via the USB-C socket labeled **DEV**. Presents both CAT and a USB sound card.
- CAT is on **SERIAL-B at 19200 bps** through a **CH342** USB bridge — the CH342 driver must
  be installed on Windows before plugging in.
- Protocol is an Icom **CI-V** subset (Xiegu radios emulate IC-7000/IC-7100).
- Hamlib: **`RIG_MODEL_X6200`** exists in `rigs/icom/xiegu.c`, inheriting `x6100_priv_caps`,
  **CI-V address `0xA4`**, 300–19200 baud.

Use Hamlib rather than hand-rolled CI-V frames. Beyond being tested, it owns the `COM5` vs
`/dev/ttyACM0` difference for us. **Link it normally, located by CMake** — `pkg-config` on
Linux, an imported target pointing at the prebuilt SDK on Windows — per the platform-in-CMake
guardrail. Runtime `dlopen`/`LoadLibrary` is only worth it if Hamlib ever needs to be optional
at runtime, and that's a plan-mode decision, not a default.

**Xiegu X6200, internal (for the aspirational port only):**
- Sunxi ARMv7 quad-core, Mali400, kernel 5.8.9, glibc, ALSA + PulseAudio.
- Vendor Qt app at `/usr/app_qt/`, settings in `xparam.db` (SQLite).
- Community research notes the stock RTTY/PSK decoders are broken and split TX transmits on
  the RX frequency — further reason to trust our own DSP over the vendor's.
- RAM size is **not confirmed**. Verify before assuming anything fits.

---

## Rules and autonomy

Match predicates compose — **all configured predicates must hold**:

- **Callsign whitelist** — `call_de` ∈ configured set
- **Directed at me** — `call_to == "KC5CD"`, so CQs and third-party traffic are ignored
- **Message text** — message type or literal content, including a private free-text token
  I'll agree with my friend to mark beacon exchanges
- **Gates** — band, audio-frequency window, minimum SNR

**Non-matching decodes must still be recorded.** "Heard but not worked" is propagation
evidence and I want it in the dataset.

Three autonomy modes, selected at startup — I want all three available:

1. **`confirm`** — composes the reply, displays it, waits for a keypress. The window between
   decodes landing and the next slot opening is short (~2 s), so a missed confirmation must
   **skip the slot and re-offer**, never transmit late.
2. **`armed`** — auto-sequences a full QSO for a bounded number of exchanges or until
   timeout, then disarms itself.
3. **`beacon`** — runs continuously against whitelist matches.

Standard sequence to implement in `qso.c` (both directions — him calling CQ, and him calling
me directly). **`W5XYZ`, `EM12`, `EM10` below are invented placeholders**, not real callsigns
or grids — substitute the real values at config time, don't hardcode these:

```
him:  CQ W5XYZ EM12
me:   W5XYZ KC5CD EM10          ← grid
him:  KC5CD W5XYZ -12           ← his report of me      ─┐ capture both
me:   W5XYZ KC5CD R-08          ← my report of him      ─┘
him:  KC5CD W5XYZ RR73
me:   W5XYZ KC5CD 73
```

### FT8 dial frequencies (USB dial, kHz), for the allowlist and the band sweep

160 m 1840 · 80 m 3573 · 40 m 7074 · 30 m 10136 · 20 m 14074 · 17 m 18100 · 15 m 21074 ·
12 m 24915 · 10 m 28074. The Phase 5 sweep and the frequency-allowlist interlock both need
this table — treat it as canonical rather than deriving it ad hoc.

### Safety interlocks — required before Phase 4 ships

- **PTT watchdog: force release if PTT is asserted beyond 13.5 s.** A stuck PTT is the
  classic failure mode of this class of program and the one that damages finals. Cheap to
  prevent. Build it early and test it deliberately.
- **Dead-man timer** — auto-disarm after N minutes with no operator input
- **TX slot budget per hour**, plus a hard `--max-tx-minutes` session cap
- **Frequency allowlist** validated before any key-down
- Clock check at startup — FT8 tolerates roughly ±2 s of slot error, so warn loudly if the
  system clock isn't NTP-synced

`beacon` mode makes this an automatically controlled station. These interlocks exist so that
control is demonstrable and bounded, and so that unattended operation is a deliberate choice
rather than a default. I'm aware of the Part 97 implications and will make those calls
myself — just make sure the mechanisms are there and honest.

---

## Data model

Bidirectional SNR is the entire point, so it is a first-class column, not something
reconstructed from a log afterward.

```sql
CREATE TABLE decode (            -- every decode, matched or not
  id INTEGER PRIMARY KEY, session_id INTEGER,
  utc TEXT NOT NULL, slot_epoch INTEGER,
  band TEXT, dial_hz INTEGER, audio_hz REAL,
  snr_db INTEGER, dt_s REAL,
  msg TEXT, call_de TEXT, call_to TEXT, grid TEXT,
  is_target INTEGER
);

CREATE TABLE exchange (          -- one row per completed bidirectional report pair
  id INTEGER PRIMARY KEY, session_id INTEGER,
  utc TEXT, band TEXT, peer TEXT,
  snr_i_sent INTEGER,            -- report I gave him
  snr_i_got  INTEGER,            -- report he gave me
  asymmetry_db INTEGER           -- snr_i_got - snr_i_sent
);

CREATE TABLE sweep_step (        -- scripted multi-band test results
  id INTEGER PRIMARY KEY, session_id INTEGER,
  band TEXT, started_utc TEXT, ended_utc TEXT,
  tx_slots INTEGER, decodes_heard INTEGER, qso_completed INTEGER,
  best_snr_i_got INTEGER, best_snr_i_sent INTEGER
);
```

`asymmetry_db` answers the actual question. Positive means his signal reaches me better than
mine reaches him. CSV export of the same data for plotting.

---

## Testing framework — required from baseline

Built in from Phase 0, not retrofitted, because CI runs on Casey's own K8s runners and must
be able to execute headless with **no radio attached**.

| Layer | Tool | Why |
|---|---|---|
| Orchestration | **CTest** | Native to CMake; `ctest --output-junit results.xml` feeds K8s CI reporting directly. **Requires CMake ≥ 3.21** — this raises the project's CMake floor; pin `cmake_minimum_required(VERSION 3.25)` for modern presets too |
| `core/` (C11) | **Unity** (ThrowTheSwitch, MIT) | Three files, zero dependencies, pure C — keeps core tests exactly as portable as `core/` itself, so they still run unchanged if Phase 7 happens |
| `app/` (C++17) | **doctest** (MIT, header-only) | Negligible build-time cost; used only for app-layer logic |
| Regression | **`ft8_lib`'s WAV corpus** | `third_party/ft8_lib/test/wav/` + its `.txt` expectations, wired as CTest cases — see Verification strategy above |

Both frameworks vendor **unmodified** under `third_party/`, per the guardrails.

## Phasing

Each phase ends at something verifiable — by an automated test first, then on the bench or
on the air. Don't run ahead, and don't treat "builds" as "done": every phase below has a
concrete test gate, not just a build gate.

| # | Deliverable | Test gate |
|---|---|---|
| 0 | CMake skeleton, vendored deps (incl. Unity/doctest/ft8_lib), HAL headers | `ctest` runs green on both platforms with ≥1 real assertion; JUnit XML emitted |
| 1 | **RX only** — capture → slot → waterfall → decode → console | **Full `test/wav/` corpus decodes and matches its `.txt` expectations** — offline, no radio. Then confirm live against WSJT-X on the same band |
| 2 | CAT (Hamlib) + TX synthesis, **no keying** | Encode→decode round-trip in-process (Unity); CAT exercised against a Hamlib dummy rig, not real hardware |
| 3 | Rules engine + QSO state machine + `confirm` mode | Table-driven `cerberus.c` match tests; `qso.c` sequence tests driven by synthetic decodes. Then first real QSO with my friend |
| 4 | `armed` + `beacon` + all safety interlocks | **`atropos.c` watchdog fires under a simulated stall — as a unit test**, before it's ever verified on the air |
| 5 | SQLite/CSV store, bidirectional SNR, band sweep | SQLite schema round-trip test; `asymmetry_db` computed correctly from a known exchange pair. Then a plotted multi-band dataset |
| 6 | *Deferred:* MCP server over `sym_dispatch()` | Tier-filtering test: assert higher-tier tools are absent from `tools/list` at each `--mcp-tier` setting |
| 7 | *Aspirational:* X6200 native build | `core/` Unity suite cross-compiled and passing on ARMv7; ARMv7 CMake preset joins here |

Phase 4 matters most: the PTT watchdog is the one failure mode that damages hardware, and
"hang the state machine on the air and see if it recovers" is a poor first line of defense
when it can be exercised as a deterministic unit test instead.

---

## Verification strategy

**Decoder correctness, before any transmitter is involved:**
- `ft8_lib` ships a real regression corpus at `third_party/ft8_lib/test/wav/`: roughly 30
  recordings (a `191111_*` capture series plus `websdr_test1..20`, two of them already at our
  12 kHz working rate), each paired with a `.txt` of expected decodes. Wire this whole corpus
  as CTest cases — see **Testing framework** below. It's the highest-value test in the
  project because it validates the full RX chain **offline, with no radio attached**
- Generate known messages with its `gen_ft8` demo, run them through our decode path
- Run against a live band with WSJT-X decoding the same audio in parallel. Decode sets
  should agree closely on strong signals; divergence means our waterfall parameters are wrong

**Encoder correctness, before key-down:**
- Encode → write WAV → **decode it in WSJT-X**. If WSJT-X reads our transmission, then
  framing, Costas placement, LDPC, and CRC are all correct
- Full loopback through a virtual audio cable: our TX → our RX, no RF

**On-air, strictly in this order:**
1. **Dummy load**, low power. Confirm CAT asserts and releases PTT, and that the waterfall
   shows a clean ~50 Hz signal at the intended audio offset
2. Deliberately hang the state machine mid-transmission; confirm the PTT watchdog fires at 13.5 s
3. `confirm` mode with my friend, one exchange. Verify both SNR directions land in `exchange`
4. `armed` mode, bounded exchange count
5. Only then, `beacon`

Never key the antenna on a first run of anything.

---

## Open items — decide with me, don't just pick

- **Config format** — small vendored TOML parser vs. plain INI. Decide at Phase 0
- **MCP transport** — native C with cJSON in the same binary, vs. a thin shim process
  translating to a core socket protocol. Decide at Phase 6, informed by actual CLI use
- **X6200 backend IPC** — how `hal/cat_x6200.c` would key the radio from inside. Unknowable
  until someone's on the radio. Leave stubbed

---

## Do not reuse sibling-directory code

`Z:\Repositories\C_C++\` has other projects next to this one (`ft8_lib`, `FTxC`, `HF`,
among others) that happen to touch overlapping ground — FT8, DSP, ham radio C++. **Do not
read from, copy from, or vendor from any of them.** This project's dependency tree is
self-contained: pull `ft8_lib`, `miniaudio`, Hamlib, and SQLite fresh from their own
upstream sources (see References) at the most recent stable release, not from a sibling
checkout of unknown age or modification state. If a sibling directory is ever relevant, I'll
say so explicitly and we'll decide together — don't go looking on your own.

## Final guardrails

These govern the whole project and apply beyond Phase 0 — check any structural decision
against them, not just the initial scaffold:

- **One codebase, Windows and Linux both, from day one.** Not a Linux-first port done later,
  not divergent forks — the same source builds both targets throughout.
- **Minimal `#ifdef`.** Platform differences are resolved by **which files get compiled**,
  chosen in CMake (`if(WIN32) target_sources(...)` / the `hal/*_win.c` vs `hal/*_posix.c`
  split already in the architecture), not by conditional compilation scattered through
  shared source. A stray `#ifdef _WIN32` inside a `core/` or `hal/` file that isn't a HAL
  implementation file is a sign the seam is in the wrong place — stop and reconsider the
  split rather than patching around it.
- **All platform variation lives in the CMake file(s).** Compiler flags, defines, link
  libraries, source-file selection — if it differs per platform, it's declared in
  `CMakeLists.txt`/presets, not hand-written per-file logic.
- **Don't modify vendored third-party code.** `ft8_lib`, miniaudio, Hamlib, SQLite, Unity,
  and doctest go in unmodified from upstream — patch around them in our own code, not inside
  theirs. This keeps upgrades a clean drop-in and keeps our bugs out of their blast radius.

**These four are defaults, not absolutes.** If a real constraint forces an exception —
some Hamlib quirk that's genuinely unworkable without a local patch, a platform capability
CMake can't express cleanly — stop, lay out the constraint and the tradeoff, and get my
explicit sign-off in **plan mode** before doing it. Case-by-case, argued, not silently
assumed.

## Git conventions

Mirrored from my other repos (`AntScopeZ`, `ModernHAMLoggerQt`), which are configured
identically. Set these up in Phase 0, before the first commit.

**Repo identity** — this is my callsign identity, not my account email:

```
git config user.name  "KC5CD"
git config user.email "KC5CD.Radio@gmail.com"
git config core.sshCommand "ssh -i Z:/Repositories/_claude/keys/kc5cd_ed25519"
git config core.filemode false
git config core.ignorecase true
```

**Commit signing is non-negotiable.** SSH-based signing:

```
git config gpg.format ssh
git config user.signingkey "Z:\Repositories\_claude\keys\kc5cd_ed25519.pub"
git config gpg.ssh.allowedSignersFile "Z:\Repositories\_claude\keys\allowed_signers"
git config tag.gpgsign true
```

(`commit.gpgsign` is already set globally — don't need to set it per-repo, but confirm it's
in effect.) Never `--no-gpg-sign`, never bypass. Before ending any session that made commits,
confirm each is signed and valid: `git log --show-signature -1`. If signing fails — missing
key, misconfigured `gpg.format`/`user.signingkey` — **stop and fix the configuration rather
than committing unsigned.**

**Two policies override your defaults — read carefully, they are not optional:**

- **Never put a Claude session link in a commit message.** No
  `https://claude.ai/code/session_...` URL or other internal/session-identifying reference —
  not even in a trailer. A `Co-Authored-By:` line is fine on its own. If your normal habit is
  to add a session-link trailer automatically, suppress it for this repo.
- **Never `git push` unless I explicitly ask for it in that conversation turn.** I work
  locally and push in batches myself. Pushing on my behalf without being asked — even
  "helpfully," even from a background or worktree session — is not wanted.

**Conventions:**
- Default branch `main`. Never commit directly to it once there's real work — branch first.
- Remote will be `git@github.com:kc5cd/Symbolon.git` — I'll create the GitHub repo and add
  the remote myself; don't try to create it.
- Feature branches: descriptive kebab-case, no prefix (`testing-framework`, not
  `feature/testing-framework`).
- Commit subjects: imperative mood, sentence case, no scope tag, no trailing period —
  e.g. *"Add ability to delete an Operation"*, *"Always store callsigns uppercase"*.
- `.gitattributes`: `* -text` — no line-ending translation, since the same source compiles
  on both platforms.
- `.gitignore` covers: build output (`build*/`, `CMakeCache.txt`, `CMakeFiles/`),
  `CMakeUserPresets.json` (but track `CMakePresets.json` itself), IDE/OS cruft, and the
  Claude working files `.claude/settings.local.json`, `.claude/worktrees/`, `.claude/plans/`,
  `.claude/state/`, `/TODO.md`.

**Session continuity:** use the `.claude/state/` convention — `plan.md`, `context.md`,
`tasks.md`, gitignored, personal working notes. Check them at the start of a session if
present; refresh them with the `update-dev-docs` skill before ending one or compacting
context, so the next session doesn't re-derive settled decisions from scratch.

## What I need from this session

Don't start writing the signal chain immediately.

1. **Confirm the toolchain and dependencies.** CMake, a C11 + C++17 compiler on both
   platforms, Hamlib availability (system package on Linux, prebuilt DLL on Windows), and
   whether `ft8_lib` and `miniaudio`, pulled fresh from their own upstream repos, vendor
   cleanly. Report anything that doesn't.
2. **Propose the Phase 0 skeleton before building it** — directory layout, the CMake
   structure that keeps platform selection out of `#ifdef` and inside `CMakeLists.txt`, the
   vendoring strategy for unmodified third-party sources, and the exact `hal_*.h` interface
   signatures. The HAL headers are the most important thing to get right early, because
   they're what makes Phase 7 possible. Show them to me before you write implementations
   behind them.
3. **Propose a `CLAUDE.md`** capturing: build commands for both platforms, the
   `core/` = no-OS-calls rule, the phase gate discipline, the never-key-the-antenna-first
   testing rule, the Git conventions section above (signing, no session links, no unsolicited
   push), and how to run the test suite (`ctest`, and how the WAV corpus regression test is
   invoked specifically).

Then work the phases in order. Small, logical, reviewable commits. Check in with me before
anything structural, and **always** before the first transmission of any phase.

---

## References

- [kgoba/ft8_lib](https://github.com/kgoba/ft8_lib) — FT8/FT4 encode + decode, MIT
- [Hamlib `rigs/icom/xiegu.c`](https://github.com/Hamlib/Hamlib/blob/master/rigs/icom/xiegu.c) — `RIG_MODEL_X6200`
- [mackron/miniaudio](https://github.com/mackron/miniaudio) — cross-platform audio, single header
- [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity) — C unit test framework for `core/`
- [doctest/doctest](https://github.com/doctest/doctest) — C++ unit test framework for `app/`
- [tom-acco/Xiegu-X6200-Research](https://github.com/tom-acco/Xiegu-X6200-Research) — X6200 internals, rooting, firmware notes
- [AetherRadio/awesome-x6100](https://github.com/AetherRadio/awesome-x6100) — sibling-radio hacking resources, useful precedent
- [WSJT-X User Guide](https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.6.1.html) — protocol behavior and the UDP interface, if we ever want it
- [MCP C# SDK](https://github.com/modelcontextprotocol/csharp-sdk) — only relevant if Phase 6 goes the shim route
