$! probe_alias.com - P1 = dirspec to list *.DIR;1 in, P2 = spec to $PARSE.
$ set noon
$ cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES probe_alias.c
$ if $severity .ne. 1 then exit 44
$ link probe_alias
$ if $severity .ne. 1 then exit 44
$ a := $'f$environment("DEFAULT")'probe_alias.exe
$ a "''p1'" "''p2'"
$ write sys$output "after exit: SYSPRV enabled = ", f$privilege("SYSPRV")
