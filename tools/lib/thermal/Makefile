# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/lib/perf/Makefile

LIBTHERMAL_VERSION = 0
LIBTHERMAL_PATCHLEVEL = 0
LIBTHERMAL_EXTRAVERSION = 1

MAKEFLAGS += --no-print-directory

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
# $(info Determined 'srctree' to be $(srctree))
endif

INSTALL = install

# Use DESTDIR for installing into a different root directory.
# This is useful for building a package. The program will be
# installed in this directory as if it was the root directory.
# Then the build tool can move it later.
DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

include $(srctree)/tools/scripts/Makefile.include
include $(srctree)/tools/scripts/Makefile.arch

ifeq ($(LP64), 1)
  libdir_relative = lib64
else
  libdir_relative = lib
endif

prefix ?=
libdir = $(prefix)/$(libdir_relative)

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
libdir_relative_SQ = $(subst ','\'',$(libdir_relative))

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -Wall
endif

INCLUDES = \
-I/usr/include/libnl3 \
-I$(srctree)/tools/lib/thermal/include \
-I$(srctree)/tools/lib/ \
-I$(srctree)/tools/include \
-I$(srctree)/tools/arch/$(SRCARCH)/include/ \
-I$(srctree)/tools/arch/$(SRCARCH)/include/uapi \
-I$(srctree)/tools/include/uapi

# Append required CFLAGS
override CFLAGS += $(EXTRA_WARNINGS)
override CFLAGS += -Werror -Wall
override CFLAGS += -fPIC
override CFLAGS += $(INCLUDES)
override CFLAGS += -fvisibility=hidden
override CFGLAS += -Wl,-L.
override CFGLAS += -Wl,-lthermal

all:

export srctree OUTPUT CC LD CFLAGS V
export DESTDIR DESTDIR_SQ

include $(srctree)/tools/build/Makefile.include

VERSION_SCRIPT := libthermal.map

PATCHLEVEL    = $(LIBTHERMAL_PATCHLEVEL)
EXTRAVERSION  = $(LIBTHERMAL_EXTRAVERSION)
VERSION       = $(LIBTHERMAL_VERSION).$(LIBTHERMAL_PATCHLEVEL).$(LIBTHERMAL_EXTRAVERSION)

LIBTHERMAL_SO := $(OUTPUT)libthermal.so.$(VERSION)
LIBTHERMAL_A  := $(OUTPUT)libthermal.a
LIBTHERMAL_IN := $(OUTPUT)libthermal-in.o
LIBTHERMAL_PC := $(OUTPUT)libthermal.pc
LIBTHERMAL_ALL := $(LIBTHERMAL_A) $(OUTPUT)libthermal.so*

THERMAL_UAPI := include/uapi/linux/thermal.h

$(THERMAL_UAPI): FORCE
	ln -sf $(srctree)/$@ $(srctree)/tools/$@

$(LIBTHERMAL_IN): FORCE
	$(Q)$(MAKE) $(build)=libthermal

$(LIBTHERMAL_A): $(LIBTHERMAL_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIBTHERMAL_IN)

$(LIBTHERMAL_SO): $(LIBTHERMAL_IN)
	$(QUIET_LINK)$(CC) --shared -Wl,-soname,libthermal.so \
                                    -Wl,--version-script=$(VERSION_SCRIPT) $^ -o $@
	@ln -sf $(@F) $(OUTPUT)libthermal.so
	@ln -sf $(@F) $(OUTPUT)libthermal.so.$(LIBTHERMAL_VERSION)


libs: $(THERMAL_UAPI) $(LIBTHERMAL_A) $(LIBTHERMAL_SO) $(LIBTHERMAL_PC)

all: fixdep
	$(Q)$(MAKE) libs

clean:
	$(call QUIET_CLEAN, libthermal) $(RM) $(LIBTHERMAL_A) \
                *.o *~ *.a *.so *.so.$(VERSION) *.so.$(LIBTHERMAL_VERSION) .*.d .*.cmd LIBTHERMAL-CFLAGS $(LIBTHERMAL_PC)

$(LIBTHERMAL_PC):
	$(QUIET_GEN)sed -e "s|@PREFIX@|$(prefix)|" \
		-e "s|@LIBDIR@|$(libdir_SQ)|" \
		-e "s|@VERSION@|$(VERSION)|" \
		< libthermal.pc.template > $@

define do_install_mkdir
	if [ ! -d '$(DESTDIR_SQ)$1' ]; then             \
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$1'; \
	fi
endef

define do_install
	if [ ! -d '$(DESTDIR_SQ)$2' ]; then             \
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$2'; \
	fi;                                             \
	$(INSTALL) $1 $(if $3,-m $3,) '$(DESTDIR_SQ)$2'
endef

install_lib: libs
	$(call QUIET_INSTALL, $(LIBTHERMAL_ALL)) \
		$(call do_install_mkdir,$(libdir_SQ)); \
		cp -fpR $(LIBTHERMAL_ALL) $(DESTDIR)$(libdir_SQ)

install_headers:
	$(call QUIET_INSTALL, headers) \
		$(call do_install,include/thermal.h,$(prefix)/include/thermal,644); \

install_pkgconfig: $(LIBTHERMAL_PC)
	$(call QUIET_INSTALL, $(LIBTHERMAL_PC)) \
		$(call do_install,$(LIBTHERMAL_PC),$(libdir_SQ)/pkgconfig,644)

install_doc:
	$(Q)$(MAKE) -C Documentation install-man install-html install-examples

install: install_lib install_headers install_pkgconfig

FORCE:

.PHONY: all install clean FORCE
