# Linux build of the core, the app and the stub platform, with their tests.
# The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -g -Isrc/core -Isrc/app -Isrc/plat -Isrc/plat/stub
CORE = src/core/vt_util.c src/core/vt_match.c src/core/vt_edit.c src/core/vt_tree.c src/core/vt_screen.c src/core/vt_ansi.c
APP = src/app/vt_app.c src/app/vt_draw.c src/app/vt_view.c
HDRS = $(wildcard src/core/*.h src/app/*.h src/plat/*.h src/plat/stub/*.h)

all: test

build/test_core: tests/unit/test_core.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE)

build/test_app: tests/unit/test_app.c $(CORE) $(APP) src/plat/stub/vt_plat_stub.c $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_app.c $(CORE) $(APP) src/plat/stub/vt_plat_stub.c

test: build/test_core build/test_app
	./build/test_core
	./build/test_app

snapshots: build/test_app
	VT_UPDATE_SNAPSHOTS=1 ./build/test_app

clean:
	rm -rf build
.PHONY: all test snapshots clean
