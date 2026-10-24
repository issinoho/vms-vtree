# CLAUDE.md

Working notes for vms-vtree: a full-screen file and directory manager for OpenVMS
(x86-64 and IA64), in DEC style and inspired by XTree Gold. `docs/PLAN.md` is the phased
plan; `docs/DECISIONS.md` records every decision and the alternatives considered;
`docs/NOTES.md` records platform behaviour and how it was observed. Read them before
starting work. Work phase by phase, and stop and ask at each phase exit.

Green-field code, not a port. Never use the XTree name for the product (D1), and copy no
XTree code, help text or artwork.

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work. Never raw `ssh host cmd`, never `WAIT` over
  ssh. Run long jobs with `run_in_background`. Pass DCL specs to it in single quotes
  (bash eats `$SYSDEVICE` in double quotes).
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), credentials or passwords. Write `<x86-host>` instead.
- **Nothing destructive or system-wide on the nodes without asking**: no PRODUCT
  INSTALL, no SYLOGIN or startup edits, no system logical names, no UAF or rights
  changes. Tests that change files do so only in the work directory, in a tree they
  create themselves.
- Prefer a 20-line probe in `probes/` over speculation. Don't claim something works
  until a test on the node shows it. Probes and tests are bounded: no loop that depends
  on an earlier step having worked.

## Hard constraints

- **C99, VSI C, 32-bit pointers.** No C11, no VLAs, no anonymous unions. The Linux
  build uses `-std=c99 -pedantic -Wall -Wextra -Werror`.
- **`src/core/` does no I/O** (D3). `src/plat/vt_platform.h` is the whole OS surface and
  has no VMS types; `src/plat/vms/` implements it, `src/plat/stub/` fakes it for Linux.
- **Protection always applies** (D5): VTREE turns off BYPASS, SYSPRV, READALL and GRPPRV
  at start-up and restores them on every exit path.
- **No DCL command lines built from file names** (D6). Changes go through RMS and
  system services with exact status checks.
- **Never descend into a directory alias** in a recursive operation (D8).
- Every destructive operation previews, confirms (default No) and logs (D10).

## OpenVMS

Undefined symbols are *warnings* on VMS: check `$SEVERITY` after every LINK. Build from
clean after changing headers or qualifiers. x86 has VSI C V7.7, IA64 V7.4: take item
codes and sizes from the headers on the build node. `<fatdef.h>` is in
`SYS$LIB_C.TLB` (N2). `$SET_SECURITY` protection is a 2-byte item (N4).
From the siblings: `#include "../x.h"` doesn't work in VSI C; the C RTL lowercases
unquoted `argv`; `LIB$WAIT` needs the float type argument (4, `LIB$K_IEEE_S`).

## Commits

Commit in logical steps with messages that explain why. Record what was verified and how.
Don't push unless the user asks.
