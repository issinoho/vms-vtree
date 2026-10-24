$! copy_setup.com - test files for the copy probe, all in [.COPYTEST] of the work directory.
$ set noon
$ if f$search("copytest.dir") .nes. ""
$ then
$   set protection=(o:rwed) [.copytest...]*.*;*
$   delete/nolog [.copytest...]*.*;*
$   delete/nolog [.copytest...]*.*;*
$   set protection=(o:rwed) copytest.dir
$   delete/nolog copytest.dir;
$ endif
$ create/directory [.copytest.src]
$ create/directory [.copytest.dcl]
$ create/directory [.copytest.vt]
$ open/write f [.copytest.src]vfc.txt
$ write f "a VFC file from DCL OPEN/WRITE"
$ write f "second line"
$ close f
$ create [.copytest.src]var.txt
A variable-length file from CREATE.
With three lines,
the last one longer than the others for a non-trivial longest-record value.
$ convert/fdl=sys$input [.copytest.src]var.txt [.copytest.src]stream.txt
RECORD
        FORMAT stream_lf
$ create [.copytest.src]keys.txt
0003 third
0001 first
0002 second
$ convert/fdl=sys$input [.copytest.src]keys.txt [.copytest.src]indexed.dat
FILE
        ORGANIZATION indexed
RECORD
        FORMAT variable
        SIZE 80
KEY 0
        SEG0_LENGTH 4
        SEG0_POSITION 0
$ delete/nolog [.copytest.src]keys.txt;*
$ copy/nolog sys$system:set.exe [.copytest.src]image.exe
$ create [.copytest.src]empty.dat
$ set security/acl=(identifier=[*,*],access=read) [.copytest.src]var.txt
$ set security/protection=(s:rwed,o:rwed,g:r,w:r) [.copytest.src]var.txt
$ set file/expiration_date=1-JAN-2030 [.copytest.src]var.txt
$ set file/nobackup [.copytest.src]stream.txt
$ write sys$output "setup done"
