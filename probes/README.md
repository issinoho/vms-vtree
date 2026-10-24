# Phase 0 probes

Bounded, read-only (except `probe_sec`'s own scratch file in the work directory).
Results are in `docs/NOTES.md` N1–N4.

    tools/vms.sh x86 put probes/*.c probes/*.h probes/probe_smg_keys.txt tests/vms/ptydrive.c
    tools/vms.sh x86 run probes/probes.com ['<spec to scan>'] [NOSMG]

- `probe_smg.c`: SMG$ display, line drawing, renditions, and the key codes of every key
  VTREE uses, driven by PTYDRIVE with `probe_smg_keys.txt` (N1).
- `probe_scan.c`: RMS `$SEARCH` and ACP attribute reads over a tree, timed (N2).
- `probe_sec.c`: privileges off; `$GET/$SET_SECURITY`, `$PARSE_ACL`, `$FORMAT_ACL` (N3, N4).
