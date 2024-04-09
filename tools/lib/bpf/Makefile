# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/lib/traceevent/Makefile

RM ?= rm
srctree := $(realpath $(srctree))

VERSION_SCRIPT := libbpf.map
LIBBPF_VERSION := $(shell \
	grep -oE '^LIBBPF_([0-9.]+)' $(VERSION_SCRIPT) | \
	sort -rV | head -n1 | cut -d'_' -f2)
LIBBPF_MAJOR_VERSION := $(word 1,$(subst ., ,$(LIBBPF_VERSION)))
LIBBPF_MINOR_VERSION := $(word 2,$(subst ., ,$(LIBBPF_VERSION)))

MAKEFLAGS += --no-print-directory

# This will work when bpf is built in tools env. where srctree
# isn't set and when invoked from selftests build, where srctree
# is a ".". building_out_of_srctree is undefined for in srctree
# builds
ifndef building_out_of_srctree
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

INSTALL = install

# Use DESTDIR for installing into a different root directory.
# This is useful for building a package. The program will be
# installed in this directory as if it was the root directory.
# Then the build tool can move it later.
DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

include $(srctree)/tools/scripts/Makefile.arch

ifeq ($(LP64), 1)
  libdir_relative = lib64
else
  libdir_relative = lib
endif

prefix ?= /usr/local
libdir = $(prefix)/$(libdir_relative)
man_dir = $(prefix)/share/man
man_dir_SQ = '$(subst ','\'',$(man_dir))'

export man_dir man_dir_SQ INSTALL
export DESTDIR DESTDIR_SQ

include $(srctree)/tools/scripts/Makefile.include

# copy a bit from Linux kbuild

ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifndef VERBOSE
  VERBOSE = 0
endif

INCLUDES = -I$(or $(OUTPUT),.) \
	   -I$(srctree)/tools/include -I$(srctree)/tools/include/uapi

export prefix libdir src obj

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
libdir_relative_SQ = $(subst ','\'',$(libdir_relative))

OBJ		= $@
N		=

LIB_TARGET	= libbpf.a libbpf.so.$(LIBBPF_VERSION)
LIB_FILE	= libbpf.a libbpf.so*
PC_FILE		= libbpf.pc

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -O2
endif

# Append required CFLAGS
override CFLAGS += -std=gnu89
override CFLAGS += $(EXTRA_WARNINGS) -Wno-switch-enum
override CFLAGS += -Werror -Wall
override CFLAGS += $(INCLUDES)
override CFLAGS += -fvisibility=hidden
override CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
override CFLAGS += $(CLANG_CROSS_FLAGS)

# flags specific for shared library
SHLIB_FLAGS := -DSHARED -fPIC

ifeq ($(VERBOSE),1)
  Q =
else
  Q = @
endif

# Disable command line variables (CFLAGS) override from top
# level Makefile (perf), otherwise build Makefile will get
# the same command line setup.
MAKEOVERRIDES=

all:

export srctree OUTPUT CC LD CFLAGS V
include $(srctree)/tools/build/Makefile.include

SHARED_OBJDIR	:= $(OUTPUT)sharedobjs/
STATIC_OBJDIR	:= $(OUTPUT)staticobjs/
BPF_IN_SHARED	:= $(SHARED_OBJDIR)libbpf-in.o
BPF_IN_STATIC	:= $(STATIC_OBJDIR)libbpf-in.o
BPF_HELPER_DEFS	:= $(OUTPUT)bpf_helper_defs.h
BPF_GENERATED	:= $(BPF_HELPER_DEFS)

LIB_TARGET	:= $(addprefix $(OUTPUT),$(LIB_TARGET))
LIB_FILE	:= $(addprefix $(OUTPUT),$(LIB_FILE))
PC_FILE		:= $(addprefix $(OUTPUT),$(PC_FILE))

TAGS_PROG := $(if $(shell which etags 2>/dev/null),etags,ctags)

GLOBAL_SYM_COUNT = $(shell readelf -s --wide $(BPF_IN_SHARED) | \
			   cut -d "@" -f1 | sed 's/_v[0-9]_[0-9]_[0-9].*//' | \
			   sed 's/\[.*\]//' | \
			   awk '/GLOBAL/ && /DEFAULT/ && !/UND|ABS/ {print $$NF}' | \
			   sort -u | wc -l)
VERSIONED_SYM_COUNT = $(shell readelf --dyn-syms --wide $(OUTPUT)libbpf.so | \
			      sed 's/\[.*\]//' | \
			      awk '/GLOBAL/ && /DEFAULT/ && !/UND|ABS/ {print $$NF}' | \
			      grep -Eo '[^ ]+@LIBBPF_' | cut -d@ -f1 | sort -u | wc -l)

CMD_TARGETS = $(LIB_TARGET) $(PC_FILE)

all: fixdep
	$(Q)$(MAKE) all_cmd

all_cmd: $(CMD_TARGETS) check

$(BPF_IN_SHARED): force $(BPF_GENERATED)
	@(test -f ../../include/uapi/linux/bpf.h -a -f ../../../include/uapi/linux/bpf.h && ( \
	(diff -B ../../include/uapi/linux/bpf.h ../../../include/uapi/linux/bpf.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/bpf.h' differs from latest version at 'include/uapi/linux/bpf.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/bpf_common.h -a -f ../../../include/uapi/linux/bpf_common.h && ( \
	(diff -B ../../include/uapi/linux/bpf_common.h ../../../include/uapi/linux/bpf_common.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/bpf_common.h' differs from latest version at 'include/uapi/linux/bpf_common.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/if_xdp.h -a -f ../../../include/uapi/linux/if_xdp.h && ( \
	(diff -B ../../include/uapi/linux/if_xdp.h ../../../include/uapi/linux/if_xdp.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/if_xdp.h' differs from latest version at 'include/uapi/linux/if_xdp.h'" >&2 )) || true
	$(Q)$(MAKE) $(build)=libbpf OUTPUT=$(SHARED_OBJDIR) CFLAGS="$(CFLAGS) $(SHLIB_FLAGS)"

$(BPF_IN_STATIC): force $(BPF_GENERATED)
	$(Q)$(MAKE) $(build)=libbpf OUTPUT=$(STATIC_OBJDIR)

$(BPF_HELPER_DEFS): $(srctree)/tools/include/uapi/linux/bpf.h
	$(QUIET_GEN)$(srctree)/scripts/bpf_doc.py --header \
		--file $(srctree)/tools/include/uapi/linux/bpf.h > $(BPF_HELPER_DEFS)

$(OUTPUT)libbpf.so: $(OUTPUT)libbpf.so.$(LIBBPF_VERSION)

$(OUTPUT)libbpf.so.$(LIBBPF_VERSION): $(BPF_IN_SHARED) $(VERSION_SCRIPT)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) \
		--shared -Wl,-soname,libbpf.so.$(LIBBPF_MAJOR_VERSION) \
		-Wl,--version-script=$(VERSION_SCRIPT) $< -lelf -lz -o $@
	@ln -sf $(@F) $(OUTPUT)libbpf.so
	@ln -sf $(@F) $(OUTPUT)libbpf.so.$(LIBBPF_MAJOR_VERSION)

$(OUTPUT)libbpf.a: $(BPF_IN_STATIC)
	$(QUIET_LINK)$(RM) -f $@; $(AR) rcs $@ $^

$(OUTPUT)libbpf.pc:
	$(QUIET_GEN)sed -e "s|@PREFIX@|$(prefix)|" \
		-e "s|@LIBDIR@|$(libdir_SQ)|" \
		-e "s|@VERSION@|$(LIBBPF_VERSION)|" \
		< libbpf.pc.template > $@

check: check_abi check_version

check_abi: $(OUTPUT)libbpf.so $(VERSION_SCRIPT)
	@if [ "$(GLOBAL_SYM_COUNT)" != "$(VERSIONED_SYM_COUNT)" ]; then	 \
		echo "Warning: Num of global symbols in $(BPF_IN_SHARED)"	 \
		     "($(GLOBAL_SYM_COUNT)) does NOT match with num of"	 \
		     "versioned symbols in $^ ($(VERSIONED_SYM_COUNT))." \
		     "Please make sure all LIBBPF_API symbols are"	 \
		     "versioned in $(VERSION_SCRIPT)." >&2;		 \
		readelf -s --wide $(BPF_IN_SHARED) |			 \
		    cut -d "@" -f1 | sed 's/_v[0-9]_[0-9]_[0-9].*//' |	 \
		    sed 's/\[.*\]//' |					 \
		    awk '/GLOBAL/ && /DEFAULT/ && !/UND/ {print $$NF}'|  \
		    sort -u > $(OUTPUT)libbpf_global_syms.tmp;		 \
		readelf --dyn-syms --wide $(OUTPUT)libbpf.so |		 \
		    sed 's/\[.*\]//' |					 \
		    awk '/GLOBAL/ && /DEFAULT/ && !/UND|ABS/ {print $$NF}'|  \
		    grep -Eo '[^ ]+@LIBBPF_' | cut -d@ -f1 |		 \
		    sort -u > $(OUTPUT)libbpf_versioned_syms.tmp; 	 \
		diff -u $(OUTPUT)libbpf_global_syms.tmp			 \
		     $(OUTPUT)libbpf_versioned_syms.tmp;		 \
		rm $(OUTPUT)libbpf_global_syms.tmp			 \
		   $(OUTPUT)libbpf_versioned_syms.tmp;			 \
		exit 1;							 \
	fi

HDR_MAJ_VERSION := $(shell grep -oE '^$(pound)define LIBBPF_MAJOR_VERSION ([0-9]+)$$' libbpf_version.h | cut -d' ' -f3)
HDR_MIN_VERSION := $(shell grep -oE '^$(pound)define LIBBPF_MINOR_VERSION ([0-9]+)$$' libbpf_version.h | cut -d' ' -f3)

check_version: $(VERSION_SCRIPT) libbpf_version.h
	@if [ "$(HDR_MAJ_VERSION)" != "$(LIBBPF_MAJOR_VERSION)" ]; then        \
		echo "Error: libbpf major version mismatch detected: "	       \
		     "'$(HDR_MAJ_VERSION)' != '$(LIBBPF_MAJOR_VERSION)'" >&2;  \
		exit 1;							       \
	fi
	@if [ "$(HDR_MIN_VERSION)" != "$(LIBBPF_MINOR_VERSION)" ]; then	       \
		echo "Error: libbpf minor version mismatch detected: "	       \
		     "'$(HDR_MIN_VERSION)' != '$(LIBBPF_MINOR_VERSION)'" >&2;  \
		exit 1;							       \
	fi

define do_install_mkdir
	if [ ! -d '$(DESTDIR_SQ)$1' ]; then		\
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$1';	\
	fi
endef

define do_install
	if [ ! -d '$(DESTDIR_SQ)$2' ]; then		\
		$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$2';	\
	fi;						\
	$(INSTALL) $(if $3,-m $3,) $1 '$(DESTDIR_SQ)$2'
endef

install_lib: all_cmd
	$(call QUIET_INSTALL, $(LIB_TARGET)) \
		$(call do_install_mkdir,$(libdir_SQ)); \
		cp -fpR $(LIB_FILE) $(DESTDIR)$(libdir_SQ)

SRC_HDRS := bpf.h libbpf.h btf.h libbpf_common.h libbpf_legacy.h	     \
	    bpf_helpers.h bpf_tracing.h bpf_endian.h bpf_core_read.h	     \
	    skel_internal.h libbpf_version.h usdt.bpf.h
GEN_HDRS := $(BPF_GENERATED)

INSTALL_PFX := $(DESTDIR)$(prefix)/include/bpf
INSTALL_SRC_HDRS := $(addprefix $(INSTALL_PFX)/, $(SRC_HDRS))
INSTALL_GEN_HDRS := $(addprefix $(INSTALL_PFX)/, $(notdir $(GEN_HDRS)))

$(INSTALL_SRC_HDRS): $(INSTALL_PFX)/%.h: %.h
	$(call QUIET_INSTALL, $@) \
		$(call do_install,$<,$(prefix)/include/bpf,644)

$(INSTALL_GEN_HDRS): $(INSTALL_PFX)/%.h: $(OUTPUT)%.h
	$(call QUIET_INSTALL, $@) \
		$(call do_install,$<,$(prefix)/include/bpf,644)

install_headers: $(BPF_GENERATED) $(INSTALL_SRC_HDRS) $(INSTALL_GEN_HDRS)
	$(call QUIET_INSTALL, libbpf_headers)

install_pkgconfig: $(PC_FILE)
	$(call QUIET_INSTALL, $(PC_FILE)) \
		$(call do_install,$(PC_FILE),$(libdir_SQ)/pkgconfig,644)

install: install_lib install_pkgconfig install_headers

clean:
	$(call QUIET_CLEAN, libbpf) $(RM) -rf $(CMD_TARGETS)		     \
		*~ .*.d .*.cmd LIBBPF-CFLAGS $(BPF_GENERATED)		     \
		$(SHARED_OBJDIR) $(STATIC_OBJDIR)			     \
		$(addprefix $(OUTPUT),					     \
			    *.o *.a *.so *.so.$(LIBBPF_MAJOR_VERSION) *.pc)

PHONY += force cscope tags check check_abi check_version
force:

cscope:
	ls *.c *.h > cscope.files
	cscope -b -q -I $(srctree)/include -f cscope.out

tags:
	$(RM) -f TAGS tags
	ls *.c *.h | xargs $(TAGS_PROG) -a

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)

# Delete partially updated (corrupted) files on error
.DELETE_ON_ERROR:

help:
	@echo 'libbpf common targets:'
	@echo '  HINT: use "V=1" to enable verbose build'
	@echo '  all     - build libraries and pkgconfig'
	@echo '  clean   - remove all generated files'
	@echo '  check   - check ABI and version info'
	@echo ''
	@echo 'libbpf install targets:'
	@echo '  HINT: use "prefix"(defaults to "/usr/local") or "DESTDIR" (defaults to "/")'
	@echo '        to adjust target destination, e.g. "make prefix=/usr/local install"'
	@echo '  install          - build and install all headers, libraries and pkgconfig'
	@echo '  install_headers  - install only headers to include/bpf'
	@echo ''
	@echo 'libbpf make targets:'
	@echo '  tags    - use ctags to make tag information for source code browsing'
	@echo '  cscope  - use cscope to make interactive source code browsing database'
