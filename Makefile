# BUILD SETTINGS ###############################################################

ifneq ($(filter Msys Cygwin, $(shell uname -o)), )
    PLATFORM := WIN32
    TYRIAN_DIR = C:\\TYRIAN
else
    PLATFORM := UNIX
    TYRIAN_DIR = $(gamesdir)/tyrian
endif

# Networking is enabled automatically when SDL3_net is installed; override the
# detection with `make WITH_NETWORK=false` (or `true`).
WITH_NETWORK ?= auto

################################################################################

# see https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html

SHELL = /bin/sh

CC ?= gcc
INSTALL ?= install
PKG_CONFIG ?= pkg-config
WINDRES ?= windres

VCS_IDREV ?= (git describe --tags || git rev-parse --short HEAD)

INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA ?= $(INSTALL) -m 644

prefix ?= /usr/local
exec_prefix ?= $(prefix)

bindir ?= $(exec_prefix)/bin
datarootdir ?= $(prefix)/share
datadir ?= $(datarootdir)
docdir ?= $(datarootdir)/doc/opentyrian
mandir ?= $(datarootdir)/man
man6dir ?= $(mandir)/man6
man6ext ?= .6
desktopdir ?= $(datarootdir)/applications
icondir ?= $(datarootdir)/icons

# see https://www.pathname.com/fhs/pub/fhs-2.3.html

gamesdir ?= $(datadir)/games

###

TARGET := opentyrian
RES :=
ifeq ($(PLATFORM), WIN32)
    TARGET := opentyrian.exe
    # The icon, from the same resource script the Visual Studio build uses.
    RES := obj/resources.o
endif

SRCS := $(wildcard src/*.c)
# Vendored zip reader (MIT); the project's own files keep their warnings, this one does not.
SRCS += src/third_party/miniz/miniz.c
MINIZ_DEFINES := -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
OBJS := $(SRCS:src/%.c=obj/%.o)
DEPS := $(SRCS:src/%.c=obj/%.d)

# The launcher art (assets/launcher/*.png) is embedded in the binary: a C source
# is generated from the PNGs at build time and compiled like any other file.
LAUNCHER_ART_FILES := assets/launcher/panel-tyrian21.png \
                      assets/launcher/panel-tyrian2000.png \
                      assets/launcher/title-tyrian21.png \
                      assets/launcher/title-tyrian2000.png
LAUNCHER_ART_SYMBOLS := launcher_art_panel21=assets/launcher/panel-tyrian21.png \
                        launcher_art_panel2000=assets/launcher/panel-tyrian2000.png \
                        launcher_art_title21=assets/launcher/title-tyrian21.png \
                        launcher_art_title2000=assets/launcher/title-tyrian2000.png
OBJS += obj/launcher_art.o
DEPS += obj/launcher_art.d

###

ifeq ($(WITH_NETWORK), auto)
    ifeq ($(shell $(PKG_CONFIG) --exists sdl3-net && echo yes), yes)
        WITH_NETWORK := true
    else
        WITH_NETWORK := false
    endif
endif

ifeq ($(WITH_NETWORK), true)
    EXTRA_CPPFLAGS += -DWITH_NETWORK
endif

OPENTYRIAN_VERSION := $(shell $(VCS_IDREV) 2>/dev/null && \
                              touch src/opentyrian_version.h)
ifneq ($(OPENTYRIAN_VERSION), )
    EXTRA_CPPFLAGS += -DOPENTYRIAN_VERSION='"$(OPENTYRIAN_VERSION)"'
endif

# -Wno-format-truncation only exists in GCC; Clang rejects it under -Werror
ifeq ($(findstring clang, $(shell $(CC) --version 2>/dev/null)), )
    WNO_FORMAT_TRUNCATION := -Wno-format-truncation
endif

CPPFLAGS ?= -MMD
CPPFLAGS += -DNDEBUG
CFLAGS ?= -pedantic \
          -Wall \
          -Wextra \
          $(WNO_FORMAT_TRUNCATION) \
          -Wno-missing-field-initializers \
          -O2
LDFLAGS ?=
LDLIBS ?=

ifeq ($(WITH_NETWORK), true)
    # Some sdl3-net .pc files (Homebrew's, at least) ship an empty prefix and
    # therefore bogus bare -I/include and -L/lib search paths that make the
    # linker warn.  Take the real paths from both packages (so this still works
    # when SDL3 and SDL3_net live under different prefixes, as in a static
    # source build) and drop those two empty-prefix artifacts.
    SDL_CPPFLAGS := $(filter-out -I/include, $(shell $(PKG_CONFIG) sdl3 sdl3-net --cflags))
    SDL_LDFLAGS := $(filter-out -L/lib, $(shell $(PKG_CONFIG) sdl3 sdl3-net --libs-only-L --libs-only-other))
    SDL_LDLIBS := $(shell $(PKG_CONFIG) sdl3 sdl3-net --libs-only-l)
else
    SDL_CPPFLAGS := $(shell $(PKG_CONFIG) sdl3 --cflags)
    SDL_LDFLAGS := $(shell $(PKG_CONFIG) sdl3 --libs-only-L --libs-only-other)
    SDL_LDLIBS := $(shell $(PKG_CONFIG) sdl3 --libs-only-l)
endif

ALL_CPPFLAGS = -DTARGET_$(PLATFORM) \
               -DTYRIAN_DIR='"$(TYRIAN_DIR)"' \
               $(EXTRA_CPPFLAGS) \
               $(SDL_CPPFLAGS) \
               $(CPPFLAGS)
ALL_CFLAGS = -std=iso9899:1999 \
             $(CFLAGS)
ALL_LDFLAGS = $(SDL_LDFLAGS) \
              $(LDFLAGS)
ALL_LDLIBS = -lm \
             $(SDL_LDLIBS) \
             $(LDLIBS)

###

.PHONY : all
all : $(TARGET)

.PHONY : debug
debug : CPPFLAGS += -UNDEBUG
debug : CFLAGS += -Werror
debug : CFLAGS += -O0
debug : CFLAGS += -g3
debug : all

# AddressSanitizer + UndefinedBehaviorSanitizer build (clang/gcc).  Not installed;
# used to hunt memory errors and undefined behaviour in the regression suite.
# A clean rebuild is forced because make does not track a change of CFLAGS, and
# the recursive invocation keeps `make -j asan` from racing clean against all.
.PHONY : asan
asan :
	$(MAKE) clean
	$(MAKE) CC="$(CC)" CPPFLAGS="-UNDEBUG" \
	        CFLAGS="-pedantic -Wall -Wextra $(WNO_FORMAT_TRUNCATION) \
	                -Wno-missing-field-initializers \
	                -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
	        LDFLAGS="-fsanitize=address,undefined" all

.PHONY : installdirs
installdirs :
	mkdir -p $(DESTDIR)$(bindir)
	mkdir -p $(DESTDIR)$(docdir)
	mkdir -p $(DESTDIR)$(man6dir)
	mkdir -p $(DESTDIR)$(desktopdir)
	mkdir -p $(DESTDIR)$(icondir)/hicolor/22x22/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/24x24/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/32x32/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/48x48/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/128x128/apps

.PHONY : install
install : $(TARGET) installdirs
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(bindir)/
	$(INSTALL_DATA) README.md $(DESTDIR)$(docdir)/
	$(INSTALL_DATA) linux/man/opentyrian.6 $(DESTDIR)$(man6dir)/opentyrian$(man6ext)
	$(INSTALL_DATA) linux/opentyrian.desktop $(DESTDIR)$(desktopdir)/
	$(INSTALL_DATA) linux/icons/tyrian-22.png $(DESTDIR)$(icondir)/hicolor/22x22/apps/opentyrian.png
	$(INSTALL_DATA) linux/icons/tyrian-24.png $(DESTDIR)$(icondir)/hicolor/24x24/apps/opentyrian.png
	$(INSTALL_DATA) linux/icons/tyrian-32.png $(DESTDIR)$(icondir)/hicolor/32x32/apps/opentyrian.png
	$(INSTALL_DATA) linux/icons/tyrian-48.png $(DESTDIR)$(icondir)/hicolor/48x48/apps/opentyrian.png
	$(INSTALL_DATA) linux/icons/tyrian-128.png $(DESTDIR)$(icondir)/hicolor/128x128/apps/opentyrian.png

.PHONY : uninstall
uninstall :
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(docdir)/README.md
	rm -f $(DESTDIR)$(man6dir)/opentyrian$(man6ext)
	rm -f $(DESTDIR)$(desktopdir)/opentyrian.desktop
	rm -f $(DESTDIR)$(icondir)/hicolor/22x22/apps/opentyrian.png
	rm -f $(DESTDIR)$(icondir)/hicolor/24x24/apps/opentyrian.png
	rm -f $(DESTDIR)$(icondir)/hicolor/32x32/apps/opentyrian.png
	rm -f $(DESTDIR)$(icondir)/hicolor/48x48/apps/opentyrian.png
	rm -f $(DESTDIR)$(icondir)/hicolor/128x128/apps/opentyrian.png

.PHONY : clean
clean :
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm -f obj/launcher_art.c
	rm -f $(RES)
	rm -f $(TARGET)

# Pass regexes through the environment so shell metacharacters stay literal.
export REGRESS_ONLY

.PHONY : regress
regress :
	TYRIAN_DATA="$(TYRIAN_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh
	if [ -z "$$REGRESS_ONLY" ] || printf '%s\n' check_progress.sh | grep -Eq "$$REGRESS_ONLY"; then tools/check_progress.sh ./opentyrian 2.1 "$(TYRIAN_DATA)"; fi

.PHONY : regress-quick
regress-quick :
	TYRIAN_DATA="$(TYRIAN_DATA)" TYRIAN2000_DATA="$(TYRIAN2000_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh --quick

# Tyrian 2000 suite: separate manifest, baselines (test/regress-2000) and data.
# TYRIAN2000_DATA must name a verified Tyrian 2000 directory; there is no fallback.
.PHONY : regress-2000
regress-2000 :
	TYRIAN2000_DATA="$(TYRIAN2000_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress-2000.sh
	if [ -z "$$REGRESS_ONLY" ] || printf '%s\n' check_progress.sh | grep -Eq "$$REGRESS_ONLY"; then tools/check_progress.sh ./opentyrian 2000 "$(TYRIAN2000_DATA)"; fi

.PHONY : regress-replay
regress-replay :
	TYRIAN_DATA="$(TYRIAN_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh --replay-check

.PHONY : regress-interp
regress-interp :
	TYRIAN_DATA="$(TYRIAN_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh --interp-check

.PHONY : regress-smooth
regress-smooth :
	TYRIAN_DATA="$(TYRIAN_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh --smoothness-check

.PHONY : regress-parallax
regress-parallax :
	TYRIAN_DATA="$(TYRIAN_DATA)" REGRESS_JOBS="$(REGRESS_JOBS)" tools/regress.sh --parallax-check

$(TARGET) : $(OBJS) $(RES)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

-include $(DEPS)

obj/%.o : src/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

obj/launcher_art.c : $(LAUNCHER_ART_FILES) tools/embed_assets.sh
	@mkdir -p "$(dir $@)"
	sh tools/embed_assets.sh $@ $(LAUNCHER_ART_SYMBOLS)

obj/launcher_art.o : obj/launcher_art.c
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

obj/third_party/miniz/miniz.o : ALL_CFLAGS += -Wno-error -w $(MINIZ_DEFINES)

obj/resources.o : visualc/resources.rc visualc/tyrian.ico visualc/opentyrian.manifest
	@mkdir -p "$(dir $@)"
	$(WINDRES) --include-dir visualc -i $< -o $@
