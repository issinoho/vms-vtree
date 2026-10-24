$! VTREE$STARTUP.COM - system startup for VTREE (a full-screen file manager)
$!
$! Installed into SYS$STARTUP. Defines the system logical names VTREE$ROOT
$! (the installed [VTREE] directory) and VTREE (its top level, where
$! VTREE.EXE is). To run it at every boot, add it to
$! SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:VTREE$STARTUP.COM
$!
$! P1 = "INSTALL": also print the post-installation tasks (PCSI runs it so).
$! P1 = "REMOVE":  deassign the logical names instead (PCSI runs it so at removal).
$!
$! Users then need the command: in SYS$MANAGER:SYLOGIN.COM or their LOGIN.COM,
$!     $ @VTREE$ROOT:[000000]VTREE$SETUP.COM
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("VTREE", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VTREE
$   if f$trnlnm("VTREE$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VTREE$ROOT
$   exit 1
$ endif
$!
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[VTREE]. Rooted logicals need the physical form (as GREP$STARTUP).
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "VTREE.]"
$ if root .eqs. dir + "VTREE.]"
$ then
$   write sys$error "VTREE$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ define/system/executive_mode/translation_attributes=concealed VTREE$ROOT 'dev''root'
$ define/system/executive_mode VTREE VTREE$ROOT:[000000]
$ if f$search("VTREE:VTREE.EXE") .eqs. ""
$ then
$   write sys$error "VTREE$STARTUP: VTREE.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for VTREE"
$ say ""
$ say "    At system startup: to define VTREE$ROOT and VTREE at every boot, add to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:VTREE$STARTUP.COM"
$ say "    For users: the VTREE command, in SYS$MANAGER:SYLOGIN.COM or their LOGIN.COM:"
$ say "    $ @VTREE$ROOT:[000000]VTREE$SETUP.COM"
$ say ""
$ say "    VTREE runs with BYPASS, SYSPRV, READALL and GRPPRV switched off, so file"
$ say "    protection applies; VTREE/PRIVILEGES keeps them on, for system managers."
$ say "    PRODUCT REMOVE VTREE removes the product and deassigns the logical names."
$ say ""
$ exit 1
