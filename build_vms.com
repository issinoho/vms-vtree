$! build_vms.com - build VTREE on OpenVMS (x86-64 or IA64), always from clean.
$!   @build_vms          build [.build]vtree.exe
$!   @build_vms TEST     also build and run the unit and snapshot tests (TEST-OK)
$! Any compiler warning stops the build (as -Werror does on Linux).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps the rest.
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" -
         + "/INCLUDE=([.src.core],[.src.app],[.src.plat],[.src.plat.vms],[.src.plat.stub])"
$ core = "vt_util,vt_match,vt_edit,vt_tree,vt_screen,vt_ansi,vt_plan"
$ app = "vt_app,vt_draw,vt_view,vt_ops,vt_del,vt_attr"
$ i = 0
$ core_loop:
$   m = f$element(i, ",", core)
$   if m .eqs. "," then goto core_done
$   cc'cflags'/object=[.build]'m'.obj [.src.core]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto core_loop
$ core_done:
$ i = 0
$ app_loop:
$   m = f$element(i, ",", app)
$   if m .eqs. "," then goto app_done
$   cc'cflags'/object=[.build]'m'.obj [.src.app]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto app_loop
$ app_done:
$ cc'cflags'/object=[.build]vt_plat_vms.obj [.src.plat.vms]vt_plat_vms.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vt_main.obj [.src.plat.vms]vt_main.c
$ if $severity .ne. 1 then goto failed
$ objs = "[.build]vt_main.obj,vt_plat_vms.obj," + core + "," + app
$ link/executable=[.build]vtree.exe 'objs'
$ if $severity .ne. 1 then goto failed
$ write sys$output "build: [.build]vtree.exe"
$ if p1 .nes. "TEST" then exit 1
$!
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ link/executable=[.build]test_core.exe [.build]test_core.obj,'core'
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_app.obj [.tests.unit]test_app.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vt_plat_stub.obj [.src.plat.stub]vt_plat_stub.c
$ if $severity .ne. 1 then goto failed
$ link/executable=[.build]test_app.exe [.build]test_app.obj,vt_plat_stub.obj,'core','app'
$ if $severity .ne. 1 then goto failed
$ run [.build]test_core
$ if .not. $status then goto failed
$ run [.build]test_app
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$ failed:
$ write sys$output "BUILD-FAILED"
$ exit 44
