# vms-vtree: a full-screen file manager for OpenVMS

## Goal

A file and directory manager for OpenVMS x86-64 and IA64 in the spirit of XTree Gold:
a directory tree above a file window, one-letter commands, tagging, and every file and
directory operation done cleanly and safely. The look is DEC's: VT line drawing,
reverse-video title and menu lines, LK201 editing keys and the keypad, with PF1 as the
**GOLD** key for XTree Gold's second set of commands.

Green-field C, SMG$ for the screen (D2), same conventions as vms-steward and
vms-histsh: `tools/vms.sh`, DECISIONS.md, NOTES.md, PCSI kits for X86VMS and I64VMS,
the family release process.

## The screen (80 or 132 columns, 24 or more rows)

```
 VTREE V0.1   DKA0:[USERS.IAIN.VMS_VTREE]                   24-OCT-2026 15:02
┌────────────────────────────────────────────────────────┬───────────────────┐
│ DKA0:                                                  │ FILE  *.*;*       │
│ ├─USERS                                                │                   │
│ │ ├─IAIN                                               │ DISK  DKA0:       │
│ │ │ ├─COM                                              │  Free  1,234,567  │
│ │ │ └─VMS_VTREE                                        │                   │
│ │ └─GUEST                                              │ DIRECTORY         │
│ └─SYS0 ...                                             │  Files       42   │
├────────────────────────────────────────────────────────┤  Blocks   1,200   │
│ BUILD_VMS.COM;3     MAKEFILE.;1         README.MD;7    │                   │
│ CLAUDE.MD;2        ◆PLAN.MD;4           VTREE.EXE;12   │ TAGGED            │
│                                                        │  Files  1  Blk 8  │
│                                                        │ IAIN [200,101]    │
│                                                        │  Privs off        │
└────────────────────────────────────────────────────────┴───────────────────┘
 DIR    Avail Copy Delete Filespec Log Makedir Prune Rename Showall Tag Untag
 COMMANDS  GOLD alternate   Return file window   Help   F10 exit
```

The two bottom lines show the commands of the current window, and change when GOLD
(PF1) is pressed, as XTree Gold's did for Alt and Ctrl.

## Keys

| Key | Action |
|---|---|
| Arrows, Prev/Next Screen | move; Prev/Next page |
| Return | tree → file window of that directory; file window → back |
| `+` / `-` / `*` | open one level / close / open the whole branch (D7) |
| Select, `T` / `U` | tag / untag the current entry |
| GOLD `T` / GOLD `U` | tag / untag everything shown |
| Find, `F` | filespec filter (`*.C;*`, `*.COM`, lists) |
| `S` / `B` | showall: files of the whole logged disk / of the branch |
| `V` | view (records, text, hex) |
| `I` | info: DIRECTORY/FULL details, security, ACL |
| `L` | log another disk or directory |
| `C`, `R`, `M`, `MK` | copy, rename, move, make directory (Phase 2) |
| Remove, `D`, `P` | delete, purge (Phase 3); prune branch is GOLD `P` |
| GOLD `A` | attributes: protection, owner, ACL, file characteristics (Phase 4) |
| `E` / `X` | edit (EDIT/TPU or the user's editor) / eXecute a DCL subprocess |
| Do | command line: type a VTREE command by name |
| Help, PF2 | help for the current window |
| Ctrl/W | redraw (DEC convention) |
| F10, Ctrl/Z | exit (asks if anything is tagged) |

## Phase 0: scaffolding and probes (done 2026-10-24)

Repo skeleton from vms-histsh (`tools/vms.sh`, PTYDRIVE in `tests/vms/`), these docs,
and three probes on both nodes: SMG$ screen and keys on a pseudo-terminal (N1), tree
reading speed (N2), privileges and file security (N3, N4). Outcomes: D5, D7.

## Phase 1: the read-only browser (built and tested 2026-10-24; awaiting the user's try)

Status: everything listed below works on x86 and IA64 except resize while running
(the size is read at start-up) and the Do key (later). Tested with `make test`,
`build_vms TEST` on both nodes, and `tools/live.sh` (PTYDRIVE scripts
`live_keys.txt` and `big_keys.txt`, screens read by eye). Colour: D11.

Nothing in this phase can change a file. Core first, on Linux with the stub platform:
- Models: devices, the lazy tree (D7), the file window (D9 versions), tag set, filespec
  matching (VMS wildcards `*` `%` and `...`, ODS-5 `^` escapes, case-blind), sort by
  name, type, size, date.
- Screen model and layout for 80 and 132 columns and any height; snapshot tests.
- Key bindings and the GOLD prefix.
VMS platform:
- Devices: mounted disks via `$DEVICE_SCAN` and `$GETDVI` (free blocks, ODS level).
- Tree: one directory's subdirectories by `$SEARCH` (NAML, long names); alias
  detection (D8), with a probe.
- Files: names and attributes via `$SEARCH` + ACP `IO$_ACCESS` (N2).
- View: text by RMS records (any organisation), hex dump by blocks, read-only, shared
  access so files in use can still be viewed.
- Info: everything DIRECTORY/FULL shows, plus `$GET_SECURITY` and the ACL (N4).
- Showall / branch file lists, tagging across directories.
- Screen: SMG$ pasteboard, broadcast trapping (messages shown in the menu line, not
  over the screen), resize, Ctrl/W, exit handler that restores the terminal and the
  privileges (D5). Ctrl/C and Ctrl/Y never leave the terminal in a bad state.
Exit: unit and snapshot tests on Linux; PTYDRIVE transcripts on both nodes; the user
tries it on both nodes; D9 confirmed.

## Phase 2: create, copy, rename, move (no data is lost) (built and tested 2026-10-24)

Status: done as below on x86 and IA64 (D12, N9-N12): make directory (M), rename
directory (R, with the delete-access step of N11), copy (C / GOLD C, "as" pattern and
"to" directory, Select/Tab points in the tree, a missing directory can be created),
move within a disk (M / GOLD M, versions kept), rename (R / GOLD R), edit (E,
VTREE$EDITOR), DCL subprocess (X), operation log, typeahead flushed before every
question. Tests: `make test` (135 + 66), `t_copy.com` against DCL COPY, `p2_keys.txt`
under PTYDRIVE on both nodes. Not done: cross-disk move (needs Phase 3's delete).

Copy probe first (D6). Make directory; copy one, tagged, or to a directory picked in the
tree, with an optional new name pattern (`*.BAK`); rename a file, tagged files with a
pattern, or a directory; move within a volume (rename) and across volumes (copy, check,
and only then delete the source: the delete part waits for Phase 3's safeguards). Edit
and eXecute (spawn). Same tests, both nodes.

## Phase 3: delete (built and tested 2026-10-24)

Status: done on x86 and IA64 (N13): delete versions (D/Remove, GOLD D tagged), purge
(P, keep N, by the filespec's names), delete an empty directory (D in the tree),
prune (GOLD P: preview, Y, name typed back), graft (GOLD G, same disk, never into its
own branch), move between disks (copy, compare block by block, then delete).
Tests: `make test` (147 + 104), `p3_keys.txt` on both nodes, Phase 1-2 scripts
again. Cross-disk move is tested on Linux only: the nodes' test areas are each on one
disk, and the scripts don't write outside the work directory. Open: D5's privileged
mode.

With D10's safeguards: delete a version, a file's versions, the tagged files; purge
with `/KEEP`; delete an empty directory (protection D is set only if the user owns it
and agrees); prune a branch (preview, typed name, aliases never followed, D8); graft
(move a branch within a volume); finish cross-volume move. Operation log. D5's
privileged mode decided here.

## Phase 4: attributes and security (built and tested 2026-10-24)

Status: done on x86 and IA64 (N14): the attributes screen (GOLD A on a directory, A
on a file, GOLD A for the tagged files) sets protection (DCL syntax; classes not
named stay, per file), owner (name or UIC), ACL entries (add; delete the chosen one;
GOLD D the whole ACL), expiration date, /NOBACKUP and erase-on-delete. Search (S),
compare two tagged files (GOLD V, DIFFERENCES into the viewer), print (O / GOLD O,
SYS$PRINT; not run on the nodes, to keep jobs off the real queue). Privileged mode
(D5). Not done: version limits, ACE editing in place (delete and add instead).

Protection, owner, ACL editing (add, delete, replace ACEs; `$PARSE_ACL` and
`$FORMAT_ACL`), file characteristics (expiration date, `/NOBACKUP`, version limit,
erase on delete), directory version limit. Compare two files (DIFFERENCES in a
subprocess), search the tagged files' contents, print to a queue.

## Phase 5: kit and release (kits built and installed on both nodes 2026-10-24; publishing waits for the user)

Status: `tools/kit.sh x86` and `tools/kit.sh ia64` build the T0.1-0 kits after the
native tests (`MAKE_KIT.COM` checks the image reports 0.1.0 with `-v`), in
`out/kits/` with SHA256SUMS; `tests/vms/pcsi_install.com` installs and verifies.
README banner (a real screen, `tools/banner.py`), badges and release notes
(`out/release-notes.md`) follow the family's format. Installed with the user's
permission on x86 and IA64 (ISSINOHO VTREE T0.1-0; VTREE$ROOT in SYS$COMMON:[VTREE.]),
and the installed copy passed `test_live.com live_keys.txt INSTALLED` on both. Not
done: the GitHub repo and release, the family site entry. 0.1.1 (2026-10-24) adds
VTREE$SETUP.COM (defines the VTREE command), as VLMF 0.1.1 did; both nodes upgraded,
and at the user's request VTREE$STARTUP is in SYSTARTUP_VMS and VTREE$SETUP in
SYLOGIN on both (vms-site-config); a batch login has the command.

PCSI kits for X86VMS and I64VMS (`tools/kit.sh`, producer ISSINOHO), a `VTREE` DCL
verb (`SET COMMAND` from the startup procedure, `/PRIVILEGES` and a start directory),
install test on a node with the user, then the family release process.

## Later ideas

Split window (two trees, as XTree Gold's F8), browsing BACKUP save sets and ZIP
archives, a remembered tree cache per disk, DECwindows not planned.
