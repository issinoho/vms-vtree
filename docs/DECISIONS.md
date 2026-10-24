# Decisions

## D1. Name: VTREE, "inspired by XTree Gold" (2026-10-24, user's choice)

XTree is a trademark, so the product, command and repo don't use the name; the README
says what it is inspired by. Considered: DIRTREE (plain), XTREE (closest, but risky
for a public release). Chosen: `vms-vtree`, command `VTREE`, PCSI product `VTREE`.
No XTree code, help text or artwork is copied; only the ideas of the layout and the
one-letter commands are borrowed.

## D2. Screen: SMG$ (2026-10-24, user's choice)

SMG$ gives the DEC look for free (line drawing, renditions, LK201 and keypad keys,
pasteboards that survive broadcast messages) and is the same on x86 and IA64 (N1).
Considered: VSI C curses (old, weak keypad support) and raw escape sequences (own key
decoding, most work).

Amended by D11: SMG$ keeps the keyboard and broadcast trapping; the screen is written
by VTREE itself so that it can be in colour.

## D3. Code layout: pure core, thin platform (2026-10-24)

As in vms-steward and vms-histsh: C99, VSI C, 32-bit pointers. `src/core/` has no I/O:
the tree and file-list models, tagging, filespec matching, sorting, formatting
(sizes, dates, protection), key bindings, the operation planner (what a copy or prune
*would* do), and the **screen model**: a grid of cells with a rendition and a
character set. `src/plat/vt_platform.h` is the whole OS surface, without VMS types.
`src/plat/vms/` implements it with RMS, ACP QIOs and system services, and draws the
screen model with SMG$. `src/plat/stub/` is an in-memory file system and a text
renderer, so the whole program, screens included, is unit-tested on Linux with
snapshot files.

## D4. Phased, read-only first (2026-10-24, user's choice)

Phase 1 can change nothing: browse, tag, view, show. Each later phase adds one class of
change, in rising order of risk: create and copy, then rename and move, then delete,
then security and attributes. Each phase is tested on both nodes before the next.

## D5. Privileges that override protection are off while VTREE runs (2026-10-24)

N3: the developer's own account starts with SYSPRV, READALL and GRPPRV on, so without
this a casual user of VTREE would delete or overwrite files that protection was meant
to save. VTREE turns off BYPASS, SYSPRV, READALL and GRPPRV at start-up (process
level, reversible) and restores the previous set at exit, including after Ctrl/Y and
errors: a temporary `$SETPRV` is undone by image rundown whatever the exit (N7). The
status panel shows "Privs off".

Open for Phase 3/4: how a system manager deliberately works with privileges on. Proposed:
a `/PRIVILEGES` qualifier only (no in-program toggle), and a reverse-video
"PRIVILEGED" in the title line for the whole session.

## D6. Changes through system services, never through built DCL lines (2026-10-24)

Create directory (`SYS$CREATE_DIR` / `LIB$CREATE_DIR`), rename (`LIB$RENAME_FILE`),
delete (`LIB$DELETE_FILE` or RMS `$ERASE` by exact version), security
(`$SET_SECURITY`): each with its exact status shown and logged. Building a `COPY` or
`DELETE` command line from file names would bring quoting problems with ODS-5 names
(`^` escapes, spaces, dots) and a path to running the wrong command. The only DCL VTREE
runs is what the user asks for: eXecute (spawn a DCL) and the editor.

**Copy** is open: RMS `$CREATE` from the source's attributes plus block copy, callable
CONVERT, or callable BACKUP. It must keep organisation, record format, indexed keys,
dates and the ACL. Probe at the start of Phase 2.

## D7. The tree is read one level at a time (2026-10-24)

N2: a whole system disk takes 75 s to read through RMS. VTREE reads a directory's
subdirectories when its branch is opened (or when it is the start directory), and the
files of the directory in the file window when it is shown, attributes included
(ACP, no file opened). "Log disk" (read the whole tree, as XTree did at start-up) is an
explicit command with a progress count, stopped by any key. A cache file between runs
is not planned.

Open: reading directory files directly in block mode (documented ODS-2/ODS-5 record
format) may be much faster than `$SEARCH`. Probe in Phase 1 if the lazy tree feels slow.

## D8. Never follow a directory alias in a recursive operation (2026-10-24)

A system disk has alias directory entries (`[SYS0]SYSCOMMON.DIR` is also
`[000000]VMS$COMMON.DIR`). Following one double-counts in a scan and, in a prune,
deletes another tree. VTREE recognises an alias (a directory file ID already seen, or a
back-link that is not the directory it was found in, N5) and shows it as such but never
descends into it for a recursive count, copy or delete.

Amended 2026-10-24 (N8): an entry whose back-link can't be read (its header is
protected, or its parent can't be listed and the path came from a typed name) is
"(unchecked)" and treated as an alias. Each node records only its own entry; a
recursive command never passes such an entry on its way down from where it started,
but may start inside one. Showall covers what is reachable that way from the highest
ancestor of the cursor.

## D9. Every version is shown, like DIRECTORY (2026-10-24, confirmed by the user)

The file window lists all versions (`*.*;*`) by default, as DIRECTORY does. A key
switches to "highest version only". A deletion takes the exact version selected unless
the user chose "all versions".

## D10. Destructive operations: preview, confirm, log (2026-10-24, proposed)

- Every delete, purge, overwrite, prune, protection or owner change first shows what it
  will do (count, blocks, the first names), then asks once: `Y` or `N`, default `N`.
- Prune (delete a branch) also needs the directory's name typed back.
- Copy never overwrites: VMS makes a new version anyway; a full version (32767)
  or a version limit that would purge something is reported first.
- Each change is appended to an operation log (default `SYS$LOGIN:VTREE.LOG`), with
  the status returned.
- A failure stops a multi-file operation and asks whether to skip or stop.
To confirm with the user before Phase 3.

## D11. The XTree Gold colour palette, written as ANSI colour (2026-10-24, user's request)

The user asked for XTree Gold's colours. SMG$ only emits colour through renditions
defined in a TERMTABLE (`SMG$M_USER1`..`8`), which would mean a site-wide or
per-user TERMTABLE.EXE just for VTREE. Instead `src/core/vt_ansi.c` turns the screen
model into VT sequences itself (cursor positioning, SGR, DEC Special Graphics),
sending only the cells that changed, and `src/plat/vms` writes them to `TT:` with
`IO$_WRITEVBLK|IO$M_NOFORMAT`. SMG$ keeps what it is good at: the keyboard (LK201
keys, N1) and broadcast trapping. Being pure, the renderer is unit-tested on Linux.

Each cell carries a role (title, menu, menu label, or "by rendition"), and the palette
maps roles and renditions to colours: blue screen, white text, bright white title,
yellow keys, labels and tag diamonds, cyan frames, bright cyan menu labels, a
black-on-cyan highlight bar (black on white in the window not in use), errors white
on red. The values are from memory of XTree Gold's defaults and live in one place
(`P_*` in vt_ansi.c).

DECterm, xterm and PuTTY show ANSI colour; a real VT220/VT320 ignores the colour
parameters. `DEFINE VTREE$DISPLAY MONO` gives the monochrome rendition (bold,
underline, reverse), which reads better on such a terminal.
