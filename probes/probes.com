$! probes.com - build and run the Phase 0 probes (all bounded, all read-only except
$! probe_sec's own scratch file). P1 = tree to scan (default SYS$COMMON:[SYSHLP...]).
$ set noon
$ spec = p1
$ if spec .eqs. "" then spec = "SYS$COMMON:[SYSHLP...]*.*;*"
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES"
$ build: subroutine
$   cc'cflags' 'p1'.c
$   if $severity .ne. 1 then exit 44
$   link 'p1'
$   if $severity .ne. 1 then exit 44
$ endsubroutine
$ call build probe_smg
$ smg_ok = $status
$ call build probe_scan
$ scan_ok = $status
$ call build probe_sec
$ sec_ok = $status
$ cc'cflags'/include=[] ptydrive.c
$ link ptydrive
$ write sys$output "==== probe_scan ", spec
$ scan := $'f$environment("DEFAULT")'probe_scan.exe
$ if scan_ok then scan "''spec'"
$ write sys$output "==== probe_sec"
$ if sec_ok then run probe_sec
$ write sys$output "==== probe_smg under ptydrive"
$ drive := $'f$environment("DEFAULT")'ptydrive.exe
$ if smg_ok .and. p2 .nes. "NOSMG" then drive probe_smg_keys.txt
$ exit 1
