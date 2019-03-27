# ################################################################
# Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ##########################################################################
# zstd : Command Line Utility, supporting gzip-like arguments
# zstd32 : Same as zstd, but forced to compile in 32-bits mode
# zstd_nolegacy : zstd without support of decompression of legacy versions
# zstd-small : minimal zstd without dictionary builder and benchmark
# zstd-compress : compressor-only version of zstd
# zstd-decompress : decompressor-only version of zstd
# ##########################################################################

ZSTDDIR = ../lib

# Version numbers
LIBVER_SRC := $(ZSTDDIR)/zstd.h
LIBVER_MAJOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MAJOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(LIBVER_SRC)`
LIBVER_MINOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MINOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(LIBVER_SRC)`
LIBVER_PATCH_SCRIPT:=`sed -n '/define ZSTD_VERSION_RELEASE/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < $(LIBVER_SRC)`
LIBVER_SCRIPT:= $(LIBVER_MAJOR_SCRIPT).$(LIBVER_MINOR_SCRIPT).$(LIBVER_PATCH_SCRIPT)
LIBVER_MAJOR := $(shell echo $(LIBVER_MAJOR_SCRIPT))
LIBVER_MINOR := $(shell echo $(LIBVER_MINOR_SCRIPT))
LIBVER_PATCH := $(shell echo $(LIBVER_PATCH_SCRIPT))
LIBVER  := $(shell echo $(LIBVER_SCRIPT))

ZSTD_VERSION = $(LIBVER)

HAVE_COLORNEVER = $(shell echo a | grep --color=never a > /dev/null 2> /dev/null && echo 1 || echo 0)
GREP_OPTIONS ?=
ifeq ($HAVE_COLORNEVER, 1)
GREP_OPTIONS += --color=never
endif
GREP = grep $(GREP_OPTIONS)

ifeq ($(shell $(CC) -v 2>&1 | $(GREP) -c "gcc version "), 1)
ALIGN_LOOP = -falign-loops=32
else
ALIGN_LOOP =
endif

CPPFLAGS+= -I$(ZSTDDIR) -I$(ZSTDDIR)/common -I$(ZSTDDIR)/compress \
           -I$(ZSTDDIR)/dictBuilder \
           -DXXH_NAMESPACE=ZSTD_
ifeq ($(OS),Windows_NT)   # MinGW assumed
CPPFLAGS   += -D__USE_MINGW_ANSI_STDIO   # compatibility with %zu formatting
endif
CFLAGS  ?= -O3
DEBUGFLAGS+=-Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
            -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement \
            -Wstrict-prototypes -Wundef -Wpointer-arith -Wformat-security \
            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings \
            -Wredundant-decls -Wmissing-prototypes -Wc++-compat
CFLAGS  += $(DEBUGFLAGS) $(MOREFLAGS)
FLAGS    = $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)


ZSTDCOMMON_FILES := $(ZSTDDIR)/common/*.c
ZSTDCOMP_FILES := $(ZSTDDIR)/compress/*.c
ZSTDDECOMP_FILES := $(ZSTDDIR)/decompress/*.c
ZSTD_FILES := $(ZSTDDECOMP_FILES) $(ZSTDCOMMON_FILES) $(ZSTDCOMP_FILES)
ZDICT_FILES := $(ZSTDDIR)/dictBuilder/*.c
ZSTDDECOMP_O = $(ZSTDDIR)/decompress/zstd_decompress.o

ZSTD_LEGACY_SUPPORT ?= 5
ZSTDLEGACY_FILES :=
ifneq ($(ZSTD_LEGACY_SUPPORT), 0)
ifeq ($(shell test $(ZSTD_LEGACY_SUPPORT) -lt 8; echo $$?), 0)
	ZSTDLEGACY_FILES += $(shell ls $(ZSTDDIR)/legacy/*.c | $(GREP) 'v0[$(ZSTD_LEGACY_SUPPORT)-7]')
endif
	CPPFLAGS += -I$(ZSTDDIR)/legacy
else
endif

# Sort files in alphabetical order for reproducible builds
ZSTDLIB_FILES := $(sort $(wildcard $(ZSTD_FILES)) $(wildcard $(ZSTDLEGACY_FILES)) $(wildcard $(ZDICT_FILES)))

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
RES64_FILE = windres/zstd64.res
RES32_FILE = windres/zstd32.res
ifneq (,$(filter x86_64%,$(shell $(CC) -dumpmachine)))
    RES_FILE = $(RES64_FILE)
else
    RES_FILE = $(RES32_FILE)
endif
else
EXT =
endif

VOID = /dev/null

# thread detection
NO_THREAD_MSG := ==> no threads, building without multithreading support
HAVE_PTHREAD := $(shell printf '\#include <pthread.h>\nint main(void) { return 0; }' > have_pthread.c && $(CC) $(FLAGS) -o have_pthread$(EXT) have_pthread.c -pthread 2> $(VOID) && rm have_pthread$(EXT) && echo 1 || echo 0; rm have_pthread.c)
HAVE_THREAD := $(shell [ "$(HAVE_PTHREAD)" -eq "1" -o -n "$(filter Windows%,$(OS))" ] && echo 1 || echo 0)
ifeq ($(HAVE_THREAD), 1)
THREAD_MSG := ==> building with threading support
THREAD_CPP := -DZSTD_MULTITHREAD
THREAD_LD := -pthread
else
THREAD_MSG := $(NO_THREAD_MSG)
endif

# zlib detection
NO_ZLIB_MSG := ==> no zlib, building zstd without .gz support
HAVE_ZLIB := $(shell printf '\#include <zlib.h>\nint main(void) { return 0; }' > have_zlib.c && $(CC) $(FLAGS) -o have_zlib$(EXT) have_zlib.c -lz 2> $(VOID) && rm have_zlib$(EXT) && echo 1 || echo 0; rm have_zlib.c)
ifeq ($(HAVE_ZLIB), 1)
ZLIB_MSG := ==> building zstd with .gz compression support
ZLIBCPP = -DZSTD_GZCOMPRESS -DZSTD_GZDECOMPRESS
ZLIBLD = -lz
else
ZLIB_MSG := $(NO_ZLIB_MSG)
endif

# lzma detection
NO_LZMA_MSG := ==> no liblzma, building zstd without .xz/.lzma support
HAVE_LZMA := $(shell printf '\#include <lzma.h>\nint main(void) { return 0; }' > have_lzma.c && $(CC) $(FLAGS) -o have_lzma$(EXT) have_lzma.c -llzma 2> $(VOID) && rm have_lzma$(EXT) && echo 1 || echo 0; rm have_lzma.c)
ifeq ($(HAVE_LZMA), 1)
LZMA_MSG := ==> building zstd with .xz/.lzma compression support
LZMACPP = -DZSTD_LZMACOMPRESS -DZSTD_LZMADECOMPRESS
LZMALD = -llzma
else
LZMA_MSG := $(NO_LZMA_MSG)
endif

# lz4 detection
NO_LZ4_MSG := ==> no liblz4, building zstd without .lz4 support
HAVE_LZ4 := $(shell printf '\#include <lz4frame.h>\n\#include <lz4.h>\nint main(void) { return 0; }' > have_lz4.c && $(CC) $(FLAGS) -o have_lz4$(EXT) have_lz4.c -llz4 2> $(VOID) && rm have_lz4$(EXT) && echo 1 || echo 0; rm have_lz4.c)
ifeq ($(HAVE_LZ4), 1)
LZ4_MSG := ==> building zstd with .lz4 compression support
LZ4CPP = -DZSTD_LZ4COMPRESS -DZSTD_LZ4DECOMPRESS
LZ4LD = -llz4
else
LZ4_MSG := $(NO_LZ4_MSG)
endif

# explicit backtrace enable/disable for Linux & Darwin
ifeq ($(BACKTRACE), 0)
DEBUGFLAGS += -DBACKTRACE_ENABLE=0
endif
ifeq (,$(filter Windows%, $(OS)))
ifeq ($(BACKTRACE), 1)
DEBUGFLAGS += -DBACKTRACE_ENABLE=1
DEBUGFLAGS_LD += -rdynamic
endif
endif


.PHONY: default
default: zstd-release

.PHONY: all
all: zstd

.PHONY: allVariants
allVariants: zstd zstd-compress zstd-decompress zstd-small zstd-nolegacy

$(ZSTDDECOMP_O): CFLAGS += $(ALIGN_LOOP)

zstd : CPPFLAGS += $(THREAD_CPP) $(ZLIBCPP) $(LZMACPP) $(LZ4CPP)
zstd : LDFLAGS += $(THREAD_LD) $(ZLIBLD) $(LZMALD) $(LZ4LD) $(DEBUGFLAGS_LD)
zstd : CPPFLAGS += -DZSTD_LEGACY_SUPPORT=$(ZSTD_LEGACY_SUPPORT)
zstd : $(ZSTDLIB_FILES) zstdcli.o util.o fileio.o benchfn.o benchzstd.o datagen.o dibio.o
	@echo "$(THREAD_MSG)"
	@echo "$(ZLIB_MSG)"
	@echo "$(LZMA_MSG)"
	@echo "$(LZ4_MSG)"
ifneq (,$(filter Windows%,$(OS)))
	windres/generate_res.bat
endif
	$(CC) $(FLAGS) $^ $(RES_FILE) -o $@$(EXT) $(LDFLAGS)

.PHONY: zstd-release
zstd-release: DEBUGFLAGS := -DBACKTRACE_ENABLE=0
zstd-release: DEBUGFLAGS_LD :=
zstd-release: zstd

zstd32 : CPPFLAGS += $(THREAD_CPP)
zstd32 : LDFLAGS  += $(THREAD_LD)
zstd32 : CPPFLAGS += -DZSTD_LEGACY_SUPPORT=$(ZSTD_LEGACY_SUPPORT)
zstd32 : $(ZSTDLIB_FILES) zstdcli.c util.c fileio.c benchfn.c benchzstd.c datagen.c dibio.c
ifneq (,$(filter Windows%,$(OS)))
	windres/generate_res.bat
endif
	$(CC) -m32 $(FLAGS) $^ $(RES32_FILE) -o $@$(EXT)

zstd-nolegacy : $(ZSTD_FILES) $(ZDICT_FILES) zstdcli.o util.o fileio.c benchfn.o benchzstd.o datagen.o dibio.o
	$(CC) $(FLAGS) $^ -o $@$(EXT) $(LDFLAGS)

zstd-nomt : THREAD_CPP :=
zstd-nomt : THREAD_LD  :=
zstd-nomt : THREAD_MSG := - multi-threading disabled
zstd-nomt : zstd

zstd-nogz : ZLIBCPP :=
zstd-nogz : ZLIBLD  :=
zstd-nogz : ZLIB_MSG := - gzip support is disabled
zstd-nogz : zstd

zstd-noxz : LZMACPP :=
zstd-noxz : LZMALD  :=
zstd-noxz : LZMA_MSG := - xz/lzma support is disabled
zstd-noxz : zstd


zstd-pgo :
	$(MAKE) clean
	$(MAKE) zstd MOREFLAGS=-fprofile-generate
	./zstd -b19i1 $(PROFILE_WITH)
	./zstd -b16i1 $(PROFILE_WITH)
	./zstd -b9i2 $(PROFILE_WITH)
	./zstd -b $(PROFILE_WITH)
	./zstd -b7i2 $(PROFILE_WITH)
	./zstd -b5 $(PROFILE_WITH)
	$(RM) zstd *.o $(ZSTDDECOMP_O) $(ZSTDDIR)/compress/*.o
	$(MAKE) zstd MOREFLAGS=-fprofile-use

# minimal target, with only zstd compression and decompression. no bench. no legacy.
zstd-small: CFLAGS = -Os -s
zstd-frugal zstd-small: $(ZSTD_FILES) zstdcli.c util.c fileio.c
	$(CC) $(FLAGS) -DZSTD_NOBENCH -DZSTD_NODICT $^ -o $@$(EXT)

zstd-decompress: $(ZSTDCOMMON_FILES) $(ZSTDDECOMP_FILES) zstdcli.c util.c fileio.c
	$(CC) $(FLAGS) -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_NOCOMPRESS $^ -o $@$(EXT)

zstd-compress: $(ZSTDCOMMON_FILES) $(ZSTDCOMP_FILES) zstdcli.c util.c fileio.c
	$(CC) $(FLAGS) -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_NODECOMPRESS $^ -o $@$(EXT)

zstdmt: zstd
	ln -sf zstd zstdmt

.PHONY: generate_res
generate_res:
	windres/generate_res.bat

.PHONY: clean
clean:
	$(MAKE) -C $(ZSTDDIR) clean
	@$(RM) $(ZSTDDIR)/decompress/*.o $(ZSTDDIR)/decompress/zstd_decompress.gcda
	@$(RM) core *.o tmp* result* *.gcda dictionary *.zst \
        zstd$(EXT) zstd32$(EXT) zstd-compress$(EXT) zstd-decompress$(EXT) \
        zstd-small$(EXT) zstd-frugal$(EXT) zstd-nolegacy$(EXT) zstd4$(EXT) \
        *.gcda default.profraw have_zlib$(EXT)
	@echo Cleaning completed

MD2ROFF = ronn
MD2ROFF_FLAGS = --roff --warnings --manual="User Commands" --organization="zstd $(ZSTD_VERSION)"

zstd.1: zstd.1.md ../lib/zstd.h
	cat $< | $(MD2ROFF) $(MD2ROFF_FLAGS) | sed -n '/^\.\\\".*/!p' > $@

zstdgrep.1: zstdgrep.1.md ../lib/zstd.h
	cat $< | $(MD2ROFF) $(MD2ROFF_FLAGS) | sed -n '/^\.\\\".*/!p' > $@

zstdless.1: zstdless.1.md ../lib/zstd.h
	cat $< | $(MD2ROFF) $(MD2ROFF_FLAGS) | sed -n '/^\.\\\".*/!p' > $@

.PHONY: man
man: zstd.1 zstdgrep.1 zstdless.1

.PHONY: clean-man
clean-man:
	rm zstd.1
	rm zstdgrep.1
	rm zstdless.1

.PHONY: preview-man
preview-man: clean-man man
	man ./zstd.1
	man ./zstdgrep.1
	man ./zstdless.1

#-----------------------------------------------------------------------------
# make install is validated only for Linux, macOS, BSD, Hurd and Solaris targets
#-----------------------------------------------------------------------------
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU OpenBSD FreeBSD NetBSD DragonFly SunOS Haiku))

HAVE_COLORNEVER = $(shell echo a | egrep --color=never a > /dev/null 2> /dev/null && echo 1 || echo 0)
EGREP_OPTIONS ?=
ifeq ($HAVE_COLORNEVER, 1)
EGREP_OPTIONS += --color=never
endif
EGREP = egrep $(EGREP_OPTIONS)

# Print a two column output of targets and their description. To add a target description, put a
# comment in the Makefile with the format "## <TARGET>: <DESCRIPTION>".  For example:
#
## list: Print all targets and their descriptions (if provided)
.PHONY: list
list:
	@TARGETS=$$($(MAKE) -pRrq -f $(lastword $(MAKEFILE_LIST)) : 2>/dev/null \
		| awk -v RS= -F: '/^# File/,/^# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' \
		| $(EGREP) -v  -e '^[^[:alnum:]]' | sort); \
	{ \
	    printf "Target Name\tDescription\n"; \
	    printf "%0.s-" {1..16}; printf "\t"; printf "%0.s-" {1..40}; printf "\n"; \
	    for target in $$TARGETS; do \
	        line=$$($(EGREP) "^##[[:space:]]+$$target:" $(lastword $(MAKEFILE_LIST))); \
	        description=$$(echo $$line | awk '{i=index($$0,":"); print substr($$0,i+1)}' | xargs); \
	        printf "$$target\t$$description\n"; \
	    done \
	} | column -t -s $$'\t'


DESTDIR     ?=
# directory variables : GNU conventions prefer lowercase
# see https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html
# support both lower and uppercase (BSD), use uppercase in script
prefix      ?= /usr/local
PREFIX      ?= $(prefix)
exec_prefix ?= $(PREFIX)
bindir      ?= $(exec_prefix)/bin
BINDIR      ?= $(bindir)
datarootdir ?= $(PREFIX)/share
mandir      ?= $(datarootdir)/man
man1dir     ?= $(mandir)/man1

ifneq (,$(filter $(shell uname),OpenBSD FreeBSD NetBSD DragonFly SunOS))
MANDIR  ?= $(PREFIX)/man
MAN1DIR ?= $(MANDIR)/man1
else
MAN1DIR ?= $(man1dir)
endif

ifneq (,$(filter $(shell uname),SunOS))
INSTALL ?= ginstall
else
INSTALL ?= install
endif

INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_SCRIPT  ?= $(INSTALL_PROGRAM)
INSTALL_DATA    ?= $(INSTALL) -m 644
INSTALL_MAN     ?= $(INSTALL_DATA)

.PHONY: install
install: zstd
	@echo Installing binaries
	@$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)/ $(DESTDIR)$(MAN1DIR)/
	@$(INSTALL_PROGRAM) zstd $(DESTDIR)$(BINDIR)/zstd
	@ln -sf zstd $(DESTDIR)$(BINDIR)/zstdcat
	@ln -sf zstd $(DESTDIR)$(BINDIR)/unzstd
	@ln -sf zstd $(DESTDIR)$(BINDIR)/zstdmt
	@$(INSTALL_SCRIPT) zstdless $(DESTDIR)$(BINDIR)/zstdless
	@$(INSTALL_SCRIPT) zstdgrep $(DESTDIR)$(BINDIR)/zstdgrep
	@echo Installing man pages
	@$(INSTALL_MAN) zstd.1 $(DESTDIR)$(MAN1DIR)/zstd.1
	@ln -sf zstd.1 $(DESTDIR)$(MAN1DIR)/zstdcat.1
	@ln -sf zstd.1 $(DESTDIR)$(MAN1DIR)/unzstd.1
	@$(INSTALL_MAN) zstdgrep.1 $(DESTDIR)$(MAN1DIR)/zstdgrep.1
	@$(INSTALL_MAN) zstdless.1 $(DESTDIR)$(MAN1DIR)/zstdless.1
	@echo zstd installation completed

.PHONY: uninstall
uninstall:
	@$(RM) $(DESTDIR)$(BINDIR)/zstdgrep
	@$(RM) $(DESTDIR)$(BINDIR)/zstdless
	@$(RM) $(DESTDIR)$(BINDIR)/zstdcat
	@$(RM) $(DESTDIR)$(BINDIR)/unzstd
	@$(RM) $(DESTDIR)$(BINDIR)/zstdmt
	@$(RM) $(DESTDIR)$(BINDIR)/zstd
	@$(RM) $(DESTDIR)$(MAN1DIR)/zstdless.1
	@$(RM) $(DESTDIR)$(MAN1DIR)/zstdgrep.1
	@$(RM) $(DESTDIR)$(MAN1DIR)/zstdcat.1
	@$(RM) $(DESTDIR)$(MAN1DIR)/unzstd.1
	@$(RM) $(DESTDIR)$(MAN1DIR)/zstd.1
	@echo zstd programs successfully uninstalled

endif
