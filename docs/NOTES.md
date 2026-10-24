# Notes: platform behaviour observed

## N1. SMG$ on a pseudo-terminal behaves as on a real VT (2026-10-24, x86 and IA64)

`probes/probe_smg.c` under PTYDRIVE (VT100 pseudo-terminal, 24x80). Identical on both
nodes. `SMG$CREATE_PASTEBOARD` reports type 6 and the real size. Borders and
`SMG$C_SPEC_GRAPHICS` text come out as `ESC ) 0` once, then SO/SI around `q x t m j k`:
DEC line drawing works without any setup. Bold, reverse and underline are plain SGR.
SMG switches the keypad to application mode (`ESC =`) and back on delete (`ESC >`), and
clears the screen on `SMG$DELETE_PASTEBOARD`.

`SMG$READ_KEYSTROKE` codes (from `SMG$KEYCODE_TO_NAME`):

| Key | Code | Name | | Key | Code | Name |
|---|---|---|---|---|---|---|
| Up/Down/Left/Right | 274/275/276/277 | UP DOWN LEFT RIGHT | | Find | 311 | E1 |
| PF1, PF2 | 256, 257 | PF1 PF2 | | Insert Here | 312 | E2 |
| KP0 | 260 | KP0 | | Remove | 313 | E3 |
| KP Enter | 270 | ENTER | | Select | 314 | E4 |
| F10 | 290 | F10 | | Prev Screen | 315 | E5 |
| Help (F15) | 295 | HELP | | Next Screen | 316 | E6 |
| Do (F16) | 296 | DO | | F17 | 297 | F17 |
| Return, Tab | 13, 9 | CTRLM CTRLI | | Delete | 127 | (none) |

Letters keep their case (c = 99, C = 67). Ctrl/W arrives as 23.

## N2. Reading a directory tree: RMS search cost (2026-10-24, x86 and IA64)

`probes/probe_scan.c`, read-only:

| Node | Spec | Entries | $SEARCH only | + ACP attributes |
|---|---|---|---|---|
| x86 | `SYS$COMMON:[SYSHLP...]*.*;*` | 1,309 | 548 ms | 1,957 ms |
| IA64 | same | 2,105 | 695 ms | 869 ms |
| x86 | `SYS$SYSDEVICE:[000000...]*.DIR;1` | 11,869 | 74.7 s | 77.5 s |

Per entry: about 0.4 ms to find it, and 0.1 to 1.1 ms more to read its attributes
(record attributes, owner, protection, dates) with `IO$_ACCESS` on its file ID. The
ACP read never opens the file, so it takes no lock and a writer can't block it.

A directories-only search of a whole system disk still takes 75 s, because RMS reads
every directory in full to find the `.DIR` entries. A tree can't be read all at start-up
(D7).

The FAT block numbers come high word first (`FAT$W_HIBLKH` then `L`). `<fatdef.h>`
is in `SYS$LIB_C.TLB`, not `SYS$STARLET_C.TLB`, so it isn't on the default include
path; the probe reads the record by offset instead.

## N3. The account has protection-overriding privileges on by default (2026-10-24)

`probes/probe_sec.c`: on both nodes, IAIN's process starts with SYSPRV, READALL and
GRPPRV enabled (BYPASS off). Any file operation from it ignores protection unless the
program switches them off. `SYS$SETPRV(0, mask)` turns them off for the process; it
returns the previous state, so they can be restored (D5).

## N4. File security through $GET/$SET_SECURITY (2026-10-24, x86 and IA64)

Same probe, on a scratch file in the work directory:
- `$GET_SECURITY` with `OSS$_PROTECTION` and `OSS$_OWNER` works with 4-byte buffers.
- `$SET_SECURITY` with `OSS$_PROTECTION` needs a **2-byte** buffer; 4 gives
  `%SYSTEM-F-IVBUFLEN`.
- `$PARSE_ACL` turns `(IDENTIFIER=[*,*],ACCESS=READ)` into a 12-byte ACE (first byte
  is the length), `OSS$_ACL_ADD_ENTRY` adds it, and `OSS$_ACL_READ_ENTRY` with a
  context reads the ACL back one ACE per call; `$FORMAT_ACL` gives the text back.
- There is no `$RELEASE_SECURITY` prototype in `starlet.h`. The read context is
  released by one more `$GET_SECURITY` call with `OSS$M_RELCTX`.

## N5. A directory alias is recognised by its back-link (2026-10-24, x86 and IA64)

`probes/probe_alias.c` on `SYS$SYSDEVICE:[SYS0]*.DIR;1`: every subdirectory's header
back-link (`ATR$C_BACKLINK`) equals the directory it was found in (`NAML$W_DID`),
except `SYSCOMMON.DIR`, whose back-link is the MFD (4,4,0): it is the alias of
`[000000]VMS$COMMON.DIR`. The directory bit (`FCH$V_DIRECTORY`, bit 13 of
`ATR$C_UCHAR`) is set on all of them. This is D8's test.

## N6. Physical names of a rooted device (2026-10-24, x86 and IA64)

`$PARSE` of `SYS$LOGIN` (`USER$ROOT:[IAIN]`, `USER$ROOT` a concealed rooted logical):
by default device `USER$ROOT:`, directory `[IAIN]`. With `NAML$M_NOCONCEAL`: IA64
`DKA800:` and `[USERS.][IAIN]`; x86 `X86VMS$DKA0:` and `[USERS.][iain]` (the node's
allocation-class name, and the directory in the case it was created in). VTREE logs
the physical device and merges `.][` into `.` to get `[USERS.IAIN]`. Directory names
must be compared without regard to case.

## N7. A temporary $SETPRV is undone at image exit (2026-10-24, x86 and IA64)

`probe_alias` turns SYSPRV off with `$SETPRV` prmflg 0 and exits; DCL then reports
`F$PRIVILEGE("SYSPRV")` TRUE again. Image rundown resets the current privileges from
the process's, so D5's "restore on every exit path" holds even for an abnormal exit,
with no exit handler needed for it.

## N8. With privileges off, some directory headers can't be read (2026-10-24, IA64)

On Myi64, `[000000]VMS$COMMON.DIR` is `(RWE,RWE,RE,E)`: the world may pass through it
but not read it. With VTREE's privileges off (D5), `IO$_ACCESS` on its alias
`[SYS0]SYSCOMMON.DIR` fails, so its back-link (N5) can't be checked, and listing
`[SYS0.SYSCOMMON]` fails with `%RMS-E-PRV`; `SYS$COMMON:[SYSLIB]` is still reachable
by name. On x86 the MFD itself can't be listed without privileges, so the path to
the user's directory is made from the names in the start specification.
VTREE marks such entries "(unchecked)" and treats them as aliases (D8).

## N9. What DCL COPY keeps (2026-10-24, x86 E9.2-4)

`probes/copy_setup.com` makes a VFC file, a variable file with an ACL, protection
(S:RWED,O:RWED,G:R,W:R) and an expiration date, a Stream_LF file with `/NOBACKUP`, an
indexed file, an image and an empty file; `COPY [.SRC]*.*;* [.DCL]`, then
`probes/copy_compare.com` compares `DIRECTORY/FULL` and MD5 checksums. COPY keeps the
contents (all checksums equal), organisation, record format and attributes, longest
record and the **creation date**. It does not keep: protection (the copy gets the
default, here (S:RWED,O:RWED,G:RE,W)), the ACL, the expiration date, `/NOBACKUP`; the
revision date is the time of the copy and the revision count goes up by one.

## N10. VTREE's copy matches COPY; version limits (2026-10-24, x86 and IA64)

`tests/vms/t_copy.c` with `vtp_copy` (raw block copy into a file created with UFO,
then the source's 32-byte record attributes written at deaccess with this file's own
allocation, D12): on all six test files the MD5 checksums, organisation, record
attributes and creation date equal the source's, and protection, ACL and expiration
come out as with DCL COPY (N9). The one difference: COPY's copy shows the source's
revision count plus one, VTREE's shows 1.

Version limits: `SET DIRECTORY/VERSION_LIMIT` sets a directory's default (the
directory file's `FAT$W_VERSIONS`); `SET FILE/VERSION_LIMIT` on a `.DIR` file only
limits that entry. A lookup with `IO$_ACCESS` and a FIB returns an existing entry's
limit in `FIB$W_VERLIMIT`; 32767 there means no limit. With a default of 3, a new
entry got 3.

## N11. Renaming a directory needs delete access on its .DIR file (2026-10-24, both)

`p2_keys.txt`: `$RENAME` of a just-created `NEWDIR.DIR;1` fails with `%RMS-F-RMV`
("ACP remove function failed"): a new directory file has no D for anyone (x86
`(RWE,RWE,RE,E)`; on IA64 it takes its parent's, here `(RWE,RWE,RE,RE)`), and the
rename removes the old entry. With O:D added it works; VTREE adds it only for the
rename, for a directory the user owns, after asking, and puts the old protection
back (logged as PROT, RENAME, PROT). Checked: the renamed directory's protection
equals its untouched siblings' on both nodes.

## N12. Typeahead answered a question (2026-10-24, x86)

An early `p2_keys.txt` was a step out: the "y" in text meant for the editor answered
a Y/N confirmation. `vt_ask` now throws away pending keystrokes before every question
(`vtp_flush_keys`); in the stub, where the tests queue their answers in advance,
it does nothing.

## N13. Deleting, purging, pruning and grafting on the nodes (2026-10-24, both)

`p3_keys.txt` in the test tree: an exact-version `$ERASE` per file; a purge keeping
one version deleted only the older NOTES.TXT; a prune went deepest first (files, then
each emptied directory after adding owner D to its `.DIR`); deleting a non-empty
directory is refused by VTREE before the file system would refuse it. A prune of a
branch holding a directory that can't be read (LOCKED) is refused with nothing
deleted. `$RENAME` of `ALPHA.DIR` into another directory (graft) updates its
back-link: afterwards it is a plain entry of its new parent (N5's test), and the
prune that followed accepted it.

An earlier version of the script reached its destructive steps by arrow keys, drifted,
and aimed a prune at the whole work directory; VTREE refused (LOCKED inside), and the
script's "n" answers were a second guard. The scripts now position with Log and
exact filespecs before every destructive step (CLAUDE.md).
