# Vendored Hamlib (Windows dev SDK)

- Version: **4.7.2** (released 2026-06-21)
- Source: https://github.com/Hamlib/Hamlib/releases/download/4.7.2/hamlib-w64-4.7.2.zip
- SHA-256: `8553bc6c5c6032e8debf99c017e98f58fed7e07e7c25d04815dc3e8bbe3304c7`
  (matches `SHA256SUM-w64-4.7.2` published on the same release page, GPG-signed by
  maintainer N0NB)
- License: **LGPL-2.1** (`COPYING.LIB.txt`, kept verbatim) — see `THIRD-PARTY-LICENSES.md`
  for how this interacts with Symbolon's own GPL-3.0-or-later.

Hamlib has no upstream git repository suitable for a submodule pin (releases are prebuilt
binary archives, not the source tree), so — like `third_party/sqlite/` — this is a plain
checksum-verified snapshot rather than a submodule.

**Trimmed from the official release zip** to just what a MinGW-w64 build and its runtime
actually need:
- `include/hamlib/*.h` — unmodified.
- `lib/gcc/libhamlib.dll.a` → `lib/libhamlib.dll.a` — the MinGW import library. The MSVC
  variant (`lib/msvc/libhamlib-4.lib` + `.def`) is not vendored; this project only builds
  with MinGW-w64/GCC (see `.claude/CLAUDE.md`'s toolchain note), never MSVC.
- `bin/libhamlib-4.dll` plus `libusb-1.0.dll`, one of its two runtime dependencies confirmed
  via `objdump -p libhamlib-4.dll` — everything else in the release's `bin/` (Hamlib's own
  CLI tools: `rigctl.exe`, `rigctld.exe`, etc., `libgcc_s_seh-1.dll`, which `libhamlib-4.dll`
  doesn't actually depend on) is not needed to build or run Symbolon and was left out.
- **`libwinpthread-1.dll` is deliberately not vendored**, even though it's
  `libhamlib-4.dll`'s other runtime dependency. Hamlib's own build of that DLL (325 KB) is a
  different build than the one this project's own executables need from the MinGW toolchain
  (Qt's copy is 53 KB, different hash) — copying it next to `symbolon.exe` shadowed the
  correct one (Windows searches an exe's own directory before `PATH`) and broke the binary
  with `STATUS_DLL_NOT_FOUND`, confirmed empirically, not assumed. Left off `PATH`
  resolution instead, same as this project's other MinGW runtime DLLs already are.
- `COPYING.LIB.txt` — kept for LGPL compliance on the redistributed binary.
- Not vendored: `doc/`, `AUTHORS.txt`, `THANKS.txt`, `README.w64-bin.txt`.

Linux never vendors this — `hal/CMakeLists.txt` locates a system Hamlib via `pkg-config`
instead (a `libhamlib-dev`-equivalent package is expected to be present; **unverified on
this machine**, no Linux box here — see the Linux presets' own "written blind" caveat in
`.claude/CLAUDE.md`, first real proof is CI).

To upgrade: download the new `hamlib-w64-<ver>.zip`, verify its SHA-256 against the
`SHA256SUM-w64-<ver>` file published alongside it, re-extract the same trimmed file set
listed above, and update this note.
