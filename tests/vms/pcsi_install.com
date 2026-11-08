$! pcsi_install.com - install the VTREE PCSI kit on this node and verify it.
$! First installation (no hand-installed copies to remove). Changes nothing else:
$! no SYSTARTUP_VMS or SYLOGIN line, no symbols for other users. Run from the work
$! directory after tools/kit.sh. Approved by the user 2026-10-24 ("proceed").
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM,CMKRNL,SYSLCK,BYPASS,WORLD")
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ if arch .eqs. "ALPHA" then base = "AXPVMS"
$ kitdir = f$environment("DEFAULT") - "]" + ".KIT_''arch']"
$ write sys$output "=== already installed?"
$ product show product VTREE /producer=ISSINOHO
$ write sys$output "=== INSTALL from ", kitdir
$ product install VTREE /producer=ISSINOHO /base_system='base' /source='kitdir' /options=noconfirm /log
$ write sys$output "=== install status ", $status
$ product show product VTREE /producer=ISSINOHO
$ write sys$output "=== VERIFY"
$ show logical/system VTREE*
$ directory/protection/owner/size VTREE:,SYS$STARTUP:VTREE$STARTUP.COM
$ vtree_v := $VTREE:VTREE.EXE
$ vtree_v "-v"
$ dummy = f$setprv(old_privs)
