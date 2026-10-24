$! t_copy.com - build and run t_copy after copy_setup; compare its copies with the source.
$ set noon
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" -
         + "/INCLUDE=([.src.core],[.src.app],[.src.plat],[.src.plat.vms])"
$ cc'cflags'/object=[.build]t_copy.obj [.tests.vms]t_copy.c
$ if $severity .ne. 1 then exit 44
$ link/executable=[.build]t_copy.exe [.build]t_copy.obj,vt_plat_vms.obj,vt_util.obj,vt_match.obj,vt_edit.obj,vt_tree.obj,vt_screen.obj,vt_ansi.obj
$ if $severity .ne. 1 then exit 44
$ @copy_setup
$ set directory/version_limit=3 [.copytest.vt]
$ wait 0:0:2
$ run [.build]t_copy
$ @copy_compare VT
$ directory [.copytest.vt]
