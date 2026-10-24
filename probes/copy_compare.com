$! copy_compare.com - P1 = subdirectory holding copies (DCL or VT). For each test file:
$! the DIRECTORY/FULL lines that differ from the source's, then MD5 of both.
$ set noon
$ d = p1
$ files = "VFC.TXT,VAR.TXT,STREAM.TXT,INDEXED.DAT,IMAGE.EXE,EMPTY.DAT"
$ i = 0
$ loop:
$   f = f$element(i, ",", files)
$   if f .eqs. "," then exit 1
$   directory/full/noheading/notrailing/output=[.copytest]a.lis [.copytest.src]'f';1
$   directory/full/noheading/notrailing/output=[.copytest]b.lis [.copytest.'d']'f';1
$   write sys$output "==== ", f
$   differences/ignore=(spacing)/output=[.copytest]ab.lis/parallel/width=132 [.copytest]a.lis [.copytest]b.lis
$   search/nowarnings [.copytest]ab.lis "Created","Modified","Expires","Backup","organization","attributes","Record format","protection","Access Cntrl","Size"
$   checksum/algorithm=md5/file [.copytest.src]'f';1
$   s = checksum$checksum
$   checksum/algorithm=md5/file [.copytest.'d']'f';1
$   write sys$output "  md5 same: ", f$string(s .eqs. checksum$checksum)
$   delete/nolog [.copytest]a.lis;*,b.lis;*,ab.lis;*
$   i = i + 1
$   goto loop
