$! test_live.com - VTREE end to end on a pseudo-terminal (PTYDRIVE plays the user).
$! Builds its own tree [.LIVE] in the work directory (and only there), runs
$! VTREE in it with the keys in P1 (default live_keys.txt), then checks that the
$! privileges VTREE turned off are back. Print the transcript with tools/vtscreen.py.
$ set noon
$ here = f$environment("DEFAULT")
$ keys = p1
$ if keys .eqs. "" then keys = "[.tests.vms]live_keys.txt"
$ if f$search("ptydrive.exe") .eqs. ""
$ then
$   cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/INCLUDE=[.probes] -
       /object=ptydrive.obj [.tests.vms]ptydrive.c
$   link ptydrive
$ endif
$! ---- the test tree: removed and made again each run
$ if f$search("live.dir") .nes. ""
$ then
$   set protection=(o:rwed) [.live...]*.*;*
$   n = 0
$ wipe:
$   delete/nolog [.live...]*.*;*
$   n = n + 1
$   if f$search("[.live...]*.*;*") .nes. "" .and. n .lt. 6 then goto wipe
$   set protection=(o:rwed) live.dir
$   delete/nolog live.dir;1
$ endif
$ create/directory [.live.alpha.beta]
$ create/directory [.live.gamma]
$ create/directory [.live.locked]
$ open/write f [.live]notes.txt
$ write f "First version"
$ close f
$ open/write f [.live]notes.txt
$ i = 1
$ lines:
$   write f "line ''i' of the second version of NOTES.TXT, with a" + "	" + "tab"
$   i = i + 1
$   if i .le. 60 then goto lines
$ close f
$ create [.live]keys.fdl
FILE
        ORGANIZATION            indexed
RECORD
        FORMAT                  variable
        SIZE                    80
KEY 0
        SEG0_LENGTH             4
        SEG0_POSITION           0
$ create [.live]keys.txt
0003 third record (key 0003)
0001 first record (key 0001)
0002 second record (key 0002)
$ convert/fdl=[.live]keys.fdl [.live]keys.txt [.live]data.idx
$ delete/nolog [.live]keys.fdl;*,keys.txt;*
$ copy/nolog sys$system:set.exe [.live]bin.dat
$ create [.live.alpha]readme.txt
In ALPHA.
$ create [.live.alpha.beta]deep.txt
In BETA.
$ create [.live.locked]hidden.txt
You should not see this.
$ set protection=(s:rwed,o,g,w) [.live]locked.dir
$ write sys$output "==== test tree"
$ directory/size/date [.live...]
$ directory/full [.live]notes.txt;2,bin.dat,data.idx
$! ---- VTREE under PTYDRIVE
$ build = here - "]" + ".BUILD]"
$ vtree :== $'build'vtree.exe
$ drive := $'here'ptydrive.exe
$ write sys$output "==== privileges before: SYSPRV ", f$privilege("SYSPRV"), " READALL ", f$privilege("READALL")
$ drive 'keys'
$ write sys$output "==== privileges after: SYSPRV ", f$privilege("SYSPRV"), " READALL ", f$privilege("READALL")
$ exit 1
