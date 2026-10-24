$! VTREE$SETUP.COM - define the VTREE command for a user
$!
$! Add to LOGIN.COM (or SYS$MANAGER:SYLOGIN.COM for everyone):
$!     $ @VTREE$ROOT:[000000]VTREE$SETUP.COM
$!
$ if f$trnlnm("VTREE$ROOT") .eqs. ""
$ then
$   write sys$error "VTREE$SETUP: VTREE$ROOT is not defined; run VTREE$STARTUP.COM first"
$   exit 44
$ endif
$ vtree :== $VTREE$ROOT:[000000]VTREE.EXE
$ exit 1
