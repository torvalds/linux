# ################################################################
# Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

# Version numbers
LIBVER_MAJOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MAJOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ./zstd.h`
LIBVER_MINOR_SCRIPT:=`sed -n '/define ZSTD_VERSION_MINOR/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ./zstd.h`
LIBVER_PATCH_SCRIPT:=`sed -n '/define ZSTD_VERSION_RELEASE/s/.*[[:blank:]]\([0-9][0-9]*\).*/\1/p' < ./zstd.h`
LIBVER_SCRIPT:= $(LIBVER_MAJOR_SCRIPT).$(LIBVER_MINOR_SCRIPT).$(LIBVER_PATCH_SCRIPT)
LIBVER_MAJOR := $(shell echo $(LIBVER_MAJOR_SCRIPT))
LIBVER_MINOR := $(shell echo $(LIBVER_MINOR_SCRIPT))
LIBVER_PATCH := $(shell echo $(LIBVER_PATCH_SCRIPT))
LIBVER := $(shell echo $(LIBVER_SCRIPT))
VERSION?= $(LIBVER)

CPPFLAGS+= -I. -I./common -DXXH_NAMESPACE=ZSTD_
ifeq ($(OS),Windows_NT)   # MinGW assumed
CPPFLAGS   += -D__USE_MINGW_ANSI_STDIO   # compatibility with %zu formatting
endif
CFLAGS  ?= -O3
DEBUGFLAGS= -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
            -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement \
            -Wstrict-prototypes -Wundef -Wpointer-arith -Wformat-security \
            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings \
            -Wredundant-decls -Wmissing-prototypes -Wc++-compat
CFLAGS  += $(DEBUGFLAGS) $(MOREFLAGS)
FLAGS    = $(CPPFLAGS) $(CFLAGS)

HAVE_COLORNEVER = $(shell echo a | grep --color=never a > /dev/null 2> /dev/null && echo 1 || echo 0)
GREP_OPTIONS ?=
ifeq ($HAVE_COLORNEVER, 1)
GREP_OPTIONS += --color=never
endif
GREP = grep $(GREP_OPTIONS)

ZSTDCOMMON_FILES := $(sort $(wildcard common/*.c))
ZSTDCOMP_FILES := $(sort $(wildcard compress/*.c))
ZSTDDECOMP_FILES := $(sort $(wildcard decompress/*.c))
ZDICT_FILES := $(sort $(wildcard dictBuilder/*.c))
ZDEPR_FILES := $(sort $(wildcard deprecated/*.c))
ZSTD_FILES := $(ZSTDCOMMON_FILES)

ZSTD_LEGACY_SUPPORT ?= 5
ZSTD_LIB_COMPRESSION ?= 1
ZSTD_LIB_DECOMPRESSION ?= 1
ZSTD_LIB_DICTBUILDER ?= 1
ZSTD_LIB_DEPRECATED ?= 1
HUF_FORCE_DECOMPRESS_X1 ?= 0
HUF_FORCE_DECOMPRESS_X2 ?= 0
ZSTD_FORCE_DECOMPRESS_SHORT ?= 0
ZSTD_FORCE_DECOMPRESS_LONG ?= 0
ZSTD_NO_INLINE ?= 0
ZSTD_STRIP_ERROR_STRINGS ?= 0

ifeq ($(ZSTD_LIB_COMPRESSION), 0)
	ZSTD_LIB_DICTBUILDER = 0
	ZSTD_LIB_DEPRECATED = 0
endif

ifeq ($(ZSTD_LIB_DECOMPRESSION), 0)
	ZSTD_LEGACY_SUPPORT = 0
	ZSTD_LIB_DEPRECATED = 0
endif

ifneq ($(ZSTD_LIB_COMPRESSION), 0)
	ZSTD_FILES += $(ZSTDCOMP_FILES)
endif

ifneq ($(ZSTD_LIB_DECOMPRESSION), 0)
	ZSTD_FILES += $(ZSTDDECOMP_FILES)
endif

ifneq ($(ZSTD_LIB_DEPRECATED), 0)
	ZSTD_FILES += $(ZDEPR_FILES)
endif

ifneq ($(ZSTD_LIB_DICTBUILDER), 0)
	ZSTD_FILES += $(ZDICT_FILES)
endif

ifneq ($(HUF_FORCE_DECOMPRESS_X1), 0)
	CFLAGS += -DHUF_FORCE_DECOMPRESS_X1
endif

ifneq ($(HUF_FORCE_DECOMPRESS_X2), 0)
	CFLAGS += -DHUF_FORCE_DECOMPRESS_X2
endif

ifneq ($(ZSTD_FORCE_DECOMPRESS_SHORT), 0)
	CFLAGS += -DZSTD_FORCE_DECOMPRESS_SHORT
endif

ifneq ($(ZSTD_FORCE_DECOMPRESS_LONG), 0)
	CFLAGS += -DZSTD_FORCE_DECOMPRESS_LONG
endif

ifneq ($(ZSTD_NO_INLINE), 0)
	CFLAGS += -DZSTD_NO_INLINE
endif

ifneq ($(ZSTD_STRIP_ERROR_STRINGS), 0)
	CFLAGS += -DZSTD_STRIP_ERROR_STRINGS
endif

ifneq ($(ZSTD_LEGACY_SUPPORT), 0)
ifeq ($(shell test $(ZSTD_LEGACY_SUPPORT) -lt 8; echo $$?), 0)
	ZSTD_FILES += $(shell ls legacy/*.c | $(GREP) 'v0[$(ZSTD_LEGACY_SUPPORT)-7]')
endif
	CPPFLAGS += -I./legacy
endif
CPPFLAGS  += -DZSTD_LEGACY_SUPPORT=$(ZSTD_LEGACY_SUPPORT)

ZSTD_OBJ   := $(patsubst %.c,%.o,$(ZSTD_FILES))

# macOS linker doesn't support -soname, and use different extension
# see : https://developer.apple.com/library/mac/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/DynamicLibraryDesignGuidelines.html
ifeq ($(shell uname), Darwin)
	SHARED_EXT = dylib
	SHARED_EXT_MAJOR = $(LIBVER_MAJOR).$(SHARED_EXT)
	SHARED_EXT_VER = $(LIBVER).$(SHARED_EXT)
	SONAME_FLAGS = -install_name $(LIBDIR)/libzstd.$(SHARED_EXT_MAJOR) -compatibility_version $(LIBVER_MAJOR) -current_version $(LIBVER)
else
	SONAME_FLAGS = -Wl,-soname=libzstd.$(SHARED_EXT).$(LIBVER_MAJOR)
	SHARED_EXT = so
	SHARED_EXT_MAJOR = $(SHARED_EXT).$(LIBVER_MAJOR)
	SHARED_EXT_VER = $(SHARED_EXT).$(LIBVER)
endif


.PHONY: default all clean install uninstall

default: lib-release

all: lib

libzstd.a: ARFLAGS = rcs
libzstd.a: $(ZSTD_OBJ)
	@echo compiling static library
	@$(AR) $(ARFLAGS) $@ $^

libzstd.a-mt: CPPFLAGS += -DZSTD_MULTITHREAD
libzstd.a-mt: libzstd.a

ifneq (,$(filter Windows%,$(OS)))

LIBZSTD = dll\libzstd.dll
$(LIBZSTD): $(ZSTD_FILES)
	@echo compiling dynamic library $(LIBVER)
	@$(CC) $(FLAGS) -DZSTD_DLL_EXPORT=1 -shared $^ -o $@
	dlltool -D $@ -d dll\libzstd.def -l dll\libzstd.lib

else

LIBZSTD = libzstd.$(SHARED_EXT_VER)
$(LIBZSTD): LDFLAGS += -shared -fPIC -fvisibility=hidden
$(LIBZSTD): $(ZSTD_FILES)
	@echo compiling dynamic library $(LIBVER)
	@$(CC) $(FLAGS) $^ $(LDFLAGS) $(SONAME_FLAGS) -o $@
	@echo creating versioned links
	@ln -sf $@ libzstd.$(SHARED_EXT_MAJOR)
	@ln -sf $@ libzstd.$(SHARED_EXT)

endif


libzstd : $(LIBZSTD)

libzstd-mt : CPPFLAGS += -DZSTD_MULTITHREAD
libzstd-mt : libzstd

lib: libzstd.a libzstd

lib-mt: CPPFLAGS += -DZSTD_MULTITHREAD
lib-mt: lib

lib-release lib-release-mt: DEBUGFLAGS :=
lib-release: lib
lib-release-mt: lib-mt

# Special case : building library in single-thread mode _and_ without zstdmt_compress.c
ZSTDMT_FILES = compress/zstdmt_compress.c
ZSTD_NOMT_FILES = $(filter-out $(ZSTDMT_FILES),$(ZSTD_FILES))
libzstd-nomt: LDFLAGS += -shared -fPIC -fvisibility=hidden
libzstd-nomt: $(ZSTD_NOMT_FILES)
	@echo compiling single-thread dynamic library $(LIBVER)
	@echo files : $(ZSTD_NOMT_FILES)
	@$(CC) $(FLAGS) $^ $(LDFLAGS) $(SONAME_FLAGS) -o $@

clean:
	@$(RM) -r *.dSYM   # macOS-specific
	@$(RM) core *.o *.a *.gcda *.$(SHARED_EXT) *.$(SHARED_EXT).* libzstd.pc
	@$(RM) dll/libzstd.dll dll/libzstd.lib libzstd-nomt*
	@$(RM) common/*.o compress/*.o decompress/*.o dictBuilder/*.o legacy/*.o deprecated/*.o
	@echo Cleaning library completed

#-----------------------------------------------------------------------------
# make install is validated only for Linux, macOS, BSD, Hurd and Solaris targets
#-----------------------------------------------------------------------------
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU OpenBSD FreeBSD NetBSD DragonFly SunOS Haiku))

DESTDIR     ?=
# directory variables : GNU conventions prefer lowercase
# see https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html
# support both lower and uppercase (BSD), use uppercase in script
prefix      ?= /usr/local
PREFIX      ?= $(prefix)
exec_prefix ?= $(PREFIX)
libdir      ?= $(exec_prefix)/lib
LIBDIR      ?= $(libdir)
includedir  ?= $(PREFIX)/include
INCLUDEDIR  ?= $(includedir)

ifneq (,$(filter $(shell uname),FreeBSD NetBSD DragonFly))
PKGCONFIGDIR ?= $(PREFIX)/libdata/pkgconfig
else
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
endif

ifneq (,$(filter $(shell uname),SunOS))
INSTALL ?= ginstall
else
INSTALL ?= install
endif

INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA    ?= $(INSTALL) -m 644


libzstd.pc:
libzstd.pc: libzstd.pc.in
	@echo creating pkgconfig
	@sed -e 's|@PREFIX@|$(PREFIX)|' \
             -e 's|@LIBDIR@|$(LIBDIR)|' \
             -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|' \
             -e 's|@VERSION@|$(VERSION)|' \
             $< >$@

install: install-pc install-static install-shared install-includes
	@echo zstd static and shared library installed

install-pc: libzstd.pc
	@$(INSTALL) -d -m 755 $(DESTDIR)$(PKGCONFIGDIR)/
	@$(INSTALL_DATA) libzstd.pc $(DESTDIR)$(PKGCONFIGDIR)/

install-static: libzstd.a
	@echo Installing static library
	@$(INSTALL) -d -m 755 $(DESTDIR)$(LIBDIR)/
	@$(INSTALL_DATA) libzstd.a $(DESTDIR)$(LIBDIR)

install-shared: libzstd
	@echo Installing shared library
	@$(INSTALL) -d -m 755 $(DESTDIR)$(LIBDIR)/
	@$(INSTALL_PROGRAM) $(LIBZSTD) $(DESTDIR)$(LIBDIR)
	@ln -sf $(LIBZSTD) $(DESTDIR)$(LIBDIR)/libzstd.$(SHARED_EXT_MAJOR)
	@ln -sf $(LIBZSTD) $(DESTDIR)$(LIBDIR)/libzstd.$(SHARED_EXT)

install-includes:
	@echo Installing includes
	@$(INSTALL) -d -m 755 $(DESTDIR)$(INCLUDEDIR)/
	@$(INSTALL_DATA) zstd.h $(DESTDIR)$(INCLUDEDIR)
	@$(INSTALL_DATA) common/zstd_errors.h $(DESTDIR)$(INCLUDEDIR)
	@$(INSTALL_DATA) deprecated/zbuff.h $(DESTDIR)$(INCLUDEDIR)     # prototypes generate deprecation warnings
	@$(INSTALL_DATA) dictBuilder/zdict.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	@$(RM) $(DESTDIR)$(LIBDIR)/libzstd.a
	@$(RM) $(DESTDIR)$(LIBDIR)/libzstd.$(SHARED_EXT)
	@$(RM) $(DESTDIR)$(LIBDIR)/libzstd.$(SHARED_EXT_MAJOR)
	@$(RM) $(DESTDIR)$(LIBDIR)/$(LIBZSTD)
	@$(RM) $(DESTDIR)$(PKGCONFIGDIR)/libzstd.pc
	@$(RM) $(DESTDIR)$(INCLUDEDIR)/zstd.h
	@$(RM) $(DESTDIR)$(INCLUDEDIR)/zstd_errors.h
	@$(RM) $(DESTDIR)$(INCLUDEDIR)/zbuff.h   # Deprecated streaming functions
	@$(RM) $(DESTDIR)$(INCLUDEDIR)/zdict.h
	@echo zstd libraries successfully uninstalled

endif
