# vms-vtree

A full-screen file and directory manager for OpenVMS x86-64 and IA64, inspired by
XTree Gold and dressed as a DEC application: a directory tree above a file window,
one-letter commands, tagging, VT line drawing, the LK201 editing keys, and PF1 as the
GOLD key for the second set of commands.

**Status: early development.** Phase 1, a read-only browser, works on x86-64 and
IA64; nothing in it can change a file yet. See [docs/PLAN.md](docs/PLAN.md).

## Trying it

Build on the node with `@build_vms`, then

    $ VTREE :== $dev:[dir.BUILD]VTREE.EXE
    $ VTREE                      ! starts in your default directory
    $ VTREE SYS$SYSROOT:[SYSHLP] ! or anywhere else

It needs a VT100-compatible terminal of at least 24 x 80 (132 columns are used if
the terminal has them). The screen uses XTree Gold's colours; on a terminal without
colour, `DEFINE VTREE$DISPLAY MONO` first. PF1 is GOLD, Help (or PF2, or `?`)
shows the keys, F10 or Ctrl/Z leaves.

## Safety first

- VTREE turns off the privileges that override file protection (BYPASS, SYSPRV,
  READALL, GRPPRV) while it runs, so it can only do what protection allows.
- Every delete, purge, prune and security change shows what it will do and asks first;
  the answer defaults to No. Changes are logged.
- It never follows a directory alias in a recursive operation.
- It changes files through RMS and system services, never by building DCL commands
  out of file names.

The reasoning is in [docs/DECISIONS.md](docs/DECISIONS.md).

XTree is a trademark of its owners; vms-vtree is unrelated to it and contains none of
its code.

## Licence

MIT. See [LICENSE](LICENSE).
