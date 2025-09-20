# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/lib/perf/Makefile

LIBTHERMAL_TOOLS_VERSION = 0
LIBTHERMAL_TOOLS_PATCHLEVEL = 0
LIBTHERMAL_TOOLS_EXTRAVERSION = 2

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
override CFGLAS += -Wl,-L.
override CFGLAS += -Wl,-lthermal

all:

export srctree OUTPUT CC LD CFLAGS V
export DESTDIR DESTDIR_SQ

include $(srctree)/tools/build/Makefile.include

PATCHLEVEL    = $(LIBTHERMAL_TOOLS_PATCHLEVEL)
EXTRAVERSION  = $(LIBTHERMAL_TOOLS_EXTRAVERSION)
VERSION       = $(LIBTHERMAL_TOOLS_VERSION).$(LIBTHERMAL_TOOLS_PATCHLEVEL).$(LIBTHERMAL_TOOLS_EXTRAVERSION)

LIBTHERMAL_TOOLS_SO := $(OUTPUT)libthermal_tools.so.$(VERSION)
LIBTHERMAL_TOOLS_A  := $(OUTPUT)libthermal_tools.a
LIBTHERMAL_TOOLS_IN := $(OUTPUT)libthermal_tools-in.o
LIBTHERMAL_TOOLS_PC := $(OUTPUT)libthermal_tools.pc

LIBTHERMAL_TOOLS_ALL := $(LIBTHERMAL_TOOLS_A) $(OUTPUT)libthermal_tools.so*

$(LIBTHERMAL_TOOLS_IN): FORCE
	$(Q)$(MAKE) $(build)=libthermal_tools

$(LIBTHERMAL_TOOLS_A): $(LIBTHERMAL_TOOLS_IN)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIBTHERMAL_TOOLS_IN)

$(LIBTHERMAL_TOOLS_SO): $(LIBTHERMAL_TOOLS_IN)
	$(QUIET_LINK)$(CC) --shared -Wl,-soname,libthermal_tools.so $^ -o $@
	@ln -sf $(@F) $(OUTPUT)libthermal_tools.so
	@ln -sf $(@F) $(OUTPUT)libthermal_tools.so.$(LIBTHERMAL_TOOLS_VERSION)


libs: $(LIBTHERMAL_TOOLS_A) $(LIBTHERMAL_TOOLS_SO) $(LIBTHERMAL_TOOLS_PC)

all: fixdep
	$(Q)$(MAKE) libs

clean:
	$(call QUIET_CLEAN, libthermal_tools) $(RM) $(LIBTHERMAL_TOOLS_A) \
                *.o *~ *.a *.so *.so.$(VERSION) *.so.$(LIBTHERMAL_TOOLS_VERSION) .*.d .*.cmd LIBTHERMAL_TOOLS-CFLAGS $(LIBTHERMAL_TOOLS_PC)

$(LIBTHERMAL_TOOLS_PC):
	$(QUIET_GEN)sed -e "s|@PREFIX@|$(prefix)|" \
		-e "s|@LIBDIR@|$(libdir_SQ)|" \
		-e "s|@VERSION@|$(VERSION)|" \
		< libthermal_tools.pc.template > $@

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
	$(call QUIET_INSTALL, $(LIBTHERMAL_TOOLS_ALL)) \
		$(call do_install_mkdir,$(libdir_SQ)); \
		cp -fpR $(LIBTHERMAL_TOOLS_ALL) $(DESTDIR)$(libdir_SQ)

install_headers:
	$(call QUIET_INSTALL, headers) \
		$(call do_install,include/thermal.h,$(prefix)/include/thermal,644); \

install_pkgconfig: $(LIBTHERMAL_TOOLS_PC)
	$(call QUIET_INSTALL, $(LIBTHERMAL_TOOLS_PC)) \
		$(call do_install,$(LIBTHERMAL_TOOLS_PC),$(libdir_SQ)/pkgconfig,644)

install_doc:
	$(Q)$(MAKE) -C Documentation install-man install-html install-examples

#install: install_lib install_headers install_pkgconfig install_doc
install: install_lib install_headers install_pkgconfig

FORCE:

.PHONY: all install clean FORCE
