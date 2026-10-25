# Fly, Bat, Worm - a screensaver of the thought experiment from Neal Stephenson's "Anathem"
VERSION  := 1.0.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/fly-bat-worm

# the pictures in assets/art are built into the program, so it must be compiled from here
build/fly-bat-worm: src/fbw.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/fbw.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/fly-bat-worm
	SDL_VIDEODRIVER=dummy ./build/fly-bat-worm --snapshot build/check.bmp --time 90 --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/fly-bat-worm $(DESTDIR)$(PREFIX)/bin/fly-bat-worm
	install -Dm755 packaging/linux/fly-bat-worm-screensaver $(DESTDIR)$(PREFIX)/bin/fly-bat-worm-screensaver
	install -Dm644 packaging/linux/fly-bat-worm.desktop $(DESTDIR)$(PREFIX)/share/applications/fly-bat-worm.desktop
	install -Dm644 packaging/linux/fly-bat-worm-saver.desktop $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/fly-bat-worm.desktop
	install -Dm644 packaging/linux/fly-bat-worm-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/fly-bat-worm-screensaver.desktop
	install -Dm644 packaging/fly-bat-worm.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/fly-bat-worm.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/fly-bat-worm/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/fly-bat-worm/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fly-bat-worm $(DESTDIR)$(PREFIX)/bin/fly-bat-worm-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/fly-bat-worm.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/fly-bat-worm-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/fly-bat-worm.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/fly-bat-worm.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/fly-bat-worm

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/fly-bat-worm
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/fly-bat-worm-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/fly-bat-worm_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=fly-bat-worm-$(VERSION)/ -o build/rpm/SOURCES/fly-bat-worm-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/fly-bat-worm.spec
	cp build/rpm/RPMS/*/fly-bat-worm-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small FlyBatWorm.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/fly-bat-worm.exe src/fbw.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/FlyBatWorm.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/fly-bat-worm.exe build/win/FlyBatWorm.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/fly-bat-worm.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean

# Fuzz targets (AddressSanitizer + UndefinedBehaviorSanitizer + libFuzzer)
FUZZ_CC ?= clang
FUZZ_CFLAGS ?= -O1 -g -fsanitize=fuzzer,address,undefined -Isrc -Wall
FUZZ_STANDALONE_CFLAGS ?= -O1 -g -fsanitize=address,undefined -Isrc -Wall

fuzz: build/fuzz/fuzz_cli build/fuzz/fuzz_stb_image

build/fuzz/fuzz_cli: test/fuzz/fuzz_cli.c
	@mkdir -p build/fuzz
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DLIBFUZZER -o $@ $< -lm

build/fuzz/fuzz_stb_image: test/fuzz/fuzz_stb_image.c
	@mkdir -p build/fuzz
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DLIBFUZZER -o $@ $< -lm

fuzz-check: fuzz
	build/fuzz/fuzz_cli test/fuzz_corpus/cli -max_total_time=3 -runs=1000
	build/fuzz/fuzz_stb_image test/fuzz_corpus/stb_image -max_total_time=3 -runs=1000

fuzz-container:
	docker run --rm -v $(CURDIR):/workspace -w /workspace screensaver-sdl:latest make fuzz-check

.PHONY: fuzz fuzz-check fuzz-container
