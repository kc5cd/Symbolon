# Third-party licenses

Symbolon itself is licensed under the GNU General Public License, version 3 or later
(GPL-3.0-or-later) — see `LICENSE` at the repo root. This file covers the dependencies it
vendors or links, and how each one's license interacts with that choice. Every license
below was read directly from the dependency's own license file (or, for Hamlib, its GitHub
repository) rather than assumed — see each entry for the source checked.

## Vendored under `third_party/` (compiled into Symbolon, unmodified from upstream)

| Dependency | License | Source checked |
|---|---|---|
| [ft8_lib](https://github.com/kgoba/ft8_lib) | MIT | `third_party/ft8_lib/LICENSE` |
| ft8_lib's bundled `fft/` (kissfft, Mark Borgerding) | BSD-3-Clause | SPDX header in `third_party/ft8_lib/fft/kiss_fft.h`/`kiss_fftr.h` |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | MIT | `third_party/unity/LICENSE.txt` |
| [doctest](https://github.com/doctest/doctest) | MIT | `third_party/doctest/LICENSE.txt` |
| [miniaudio](https://github.com/mackron/miniaudio) | Public domain (Unlicense) **or** MIT-No-Attribution, dual — either may be relied on | `third_party/miniaudio/LICENSE` |
| [SQLite](https://www.sqlite.org/) | Public domain (author disclaims copyright) | `third_party/sqlite/sqlite3.h`'s own header comment; see also `third_party/sqlite/VERSION.md` |

All six are permissive (or public domain) and impose no obligations beyond, at most,
retaining their own copyright notice — none of them constrain Symbolon's own license choice.
They're reproduced unmodified per the project's own vendoring guardrail (see
`.claude/CLAUDE.md`'s "Don't modify vendored third-party code" note), so their license terms
travel with the files themselves; nothing extra to do beyond this table.

## Linked, not vendored

| Dependency | License | Source checked |
|---|---|---|
| [Hamlib](https://github.com/Hamlib/Hamlib) (`libhamlib`) | LGPL-2.1 | `COPYING.LIB` in the Hamlib repository |

Hamlib's own CLI frontend programs (`rigctl`, `rotctl`, etc.) are separately licensed under
GPL-2.0 (`COPYING` in that repository) — Symbolon never links or copies from that code, only
from the library's public C API, so only LGPL-2.1 is actually relevant here.

**Why LGPL-2.1 doesn't constrain the license choice above:** LGPL exists specifically to
permit linking from software under a different license — its only real requirement is that
users of a combined/statically-linked binary can obtain and relink a modified copy of the
LGPL'd library (LGPL-2.1 §6). Since Symbolon's own source is published in full under
GPL-3.0-or-later, that requirement is satisfied automatically regardless of which FOSS
license Symbolon uses — this would have been true under MIT or Apache-2.0 too. It's also
fully compatible in the other direction: LGPL-licensed code may be used within a GPL-licensed
combined work (the LGPL portion's own terms still govern that portion; GPL governs the rest).

**Static vs. dynamic linking, practically:** dynamic linking (the natural default — a shared
`.so` on Linux via `pkg-config`, a `.dll` + import library on Windows, per the kickoff's
Phase 2 plan) keeps LGPL-2.1 compliance simplest: retain Hamlib's own copyright/license
notice, and a user can already swap the linked library for a modified build without needing
anything extra from Symbolon. Static linking would additionally require providing the
linkable object files (or an equivalent relinking mechanism) per LGPL-2.1 §6(a) — avoid it
for the Hamlib link specifically unless there's a concrete reason to prefer it.

## If a dependency changes

Adding a new vendored or linked dependency later: check its actual license file (or
repository license page) before assuming compatibility, add a row to whichever table above
applies, and flag anything copyleft (GPL/LGPL/AGPL) or otherwise unusual explicitly rather
than folding it in silently — this file should stay an accurate, checked record, not a
place licenses get assumed compatible by default.
