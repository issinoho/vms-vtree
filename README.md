# vms-vtree

A full-screen file and directory manager for OpenVMS x86-64 and IA64, inspired by
XTree Gold and dressed as a DEC application: a directory tree above a file window,
one-letter commands, tagging, VT line drawing, the LK201 editing keys, and PF1 as the
GOLD key for the second set of commands.

**Status: early development.** Phase 0 (probes) is done; Phase 1, a read-only
browser, is next. See [docs/PLAN.md](docs/PLAN.md).

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
