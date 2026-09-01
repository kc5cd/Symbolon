# Vendored SQLite

- Version: **3.53.4** (`SQLITE_VERSION 3.53.4`, version number 3053004)
- Source: https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
- SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
  (matches the published checksum on https://www.sqlite.org/download.html)
- Files: `sqlite3.c`, `sqlite3.h`, `sqlite3ext.h` — unmodified amalgamation, no `shell.c`
  (Symbolon has no interactive SQL shell requirement).

SQLite has no upstream git repository suitable for a submodule pin, so this is a plain
copied snapshot rather than a submodule, unlike the other `third_party/` dependencies. To
upgrade: download the new amalgamation zip from sqlite.org, verify its SHA3-256 against the
value published on the download page, replace these three files, and update this note.
