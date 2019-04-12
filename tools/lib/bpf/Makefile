# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/lib/traceevent/Makefile

BPF_VERSION = 0
BPF_PATCHLEVEL = 0
BPF_EXTRAVERSION = 3

MAKEFLAGS += --no-print-directory

ifeq ($(srctree),)
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

FEATURE_USER = .libbpf
FEATURE_TESTS = libelf libelf-mmap bpf reallocarray cxx
FEATURE_DISPLAY = libelf bpf

INCLUDES = -I. -I$(srctree)/tools/include -I$(srctree)/tools/arch/$(ARCH)/include/uapi -I$(srctree)/tools/include/uapi
FEATURE_CHECK_CFLAGS-bpf = $(INCLUDES)

check_feat := 1
NON_CHECK_FEAT_TARGETS := clean TAGS tags cscope help
ifdef MAKECMDGOALS
ifeq ($(filter-out $(NON_CHECK_FEAT_TARGETS),$(MAKECMDGOALS)),)
  check_feat := 0
endif
endif

ifeq ($(check_feat),1)
ifeq ($(FEATURES_DUMP),)
include $(srctree)/tools/build/Makefile.feature
else
include $(FEATURES_DUMP)
endif
endif

export prefix libdir src obj

# Shell quotes
libdir_SQ = $(subst ','\'',$(libdir))
libdir_relative_SQ = $(subst ','\'',$(libdir_relative))

VERSION		= $(BPF_VERSION)
PATCHLEVEL	= $(BPF_PATCHLEVEL)
EXTRAVERSION	= $(BPF_EXTRAVERSION)

OBJ		= $@
N		=

LIBBPF_VERSION	= $(BPF_VERSION).$(BPF_PATCHLEVEL).$(BPF_EXTRAVERSION)

LIB_TARGET	= libbpf.a libbpf.so.$(LIBBPF_VERSION)
LIB_FILE	= libbpf.a libbpf.so*
PC_FILE		= libbpf.pc

# Set compile option CFLAGS
ifdef EXTRA_CFLAGS
  CFLAGS := $(EXTRA_CFLAGS)
else
  CFLAGS := -g -Wall
endif

ifeq ($(feature-libelf-mmap), 1)
  override CFLAGS += -DHAVE_LIBELF_MMAP_SUPPORT
endif

ifeq ($(feature-reallocarray), 0)
  override CFLAGS += -DCOMPAT_NEED_REALLOCARRAY
endif

# Append required CFLAGS
override CFLAGS += $(EXTRA_WARNINGS)
override CFLAGS += -Werror -Wall
override CFLAGS += -fPIC
override CFLAGS += $(INCLUDES)
override CFLAGS += -fvisibility=hidden

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

BPF_IN		:= $(OUTPUT)libbpf-in.o
VERSION_SCRIPT	:= libbpf.map

LIB_TARGET	:= $(addprefix $(OUTPUT),$(LIB_TARGET))
LIB_FILE	:= $(addprefix $(OUTPUT),$(LIB_FILE))
PC_FILE		:= $(addprefix $(OUTPUT),$(PC_FILE))

GLOBAL_SYM_COUNT = $(shell readelf -s --wide $(BPF_IN) | \
			   awk '/GLOBAL/ && /DEFAULT/ && !/UND/ {s++} END{print s}')
VERSIONED_SYM_COUNT = $(shell readelf -s --wide $(OUTPUT)libbpf.so | \
			      grep -Eo '[^ ]+@LIBBPF_' | cut -d@ -f1 | sort -u | wc -l)

CMD_TARGETS = $(LIB_TARGET) $(PC_FILE)

CXX_TEST_TARGET = $(OUTPUT)test_libbpf

ifeq ($(feature-cxx), 1)
	CMD_TARGETS += $(CXX_TEST_TARGET)
endif

TARGETS = $(CMD_TARGETS)

all: fixdep
	$(Q)$(MAKE) all_cmd

all_cmd: $(CMD_TARGETS) check

$(BPF_IN): force elfdep bpfdep
	@(test -f ../../include/uapi/linux/bpf.h -a -f ../../../include/uapi/linux/bpf.h && ( \
	(diff -B ../../include/uapi/linux/bpf.h ../../../include/uapi/linux/bpf.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/bpf.h' differs from latest version at 'include/uapi/linux/bpf.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/bpf_common.h -a -f ../../../include/uapi/linux/bpf_common.h && ( \
	(diff -B ../../include/uapi/linux/bpf_common.h ../../../include/uapi/linux/bpf_common.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/bpf_common.h' differs from latest version at 'include/uapi/linux/bpf_common.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/netlink.h -a -f ../../../include/uapi/linux/netlink.h && ( \
	(diff -B ../../include/uapi/linux/netlink.h ../../../include/uapi/linux/netlink.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/netlink.h' differs from latest version at 'include/uapi/linux/netlink.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/if_link.h -a -f ../../../include/uapi/linux/if_link.h && ( \
	(diff -B ../../include/uapi/linux/if_link.h ../../../include/uapi/linux/if_link.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/if_link.h' differs from latest version at 'include/uapi/linux/if_link.h'" >&2 )) || true
	@(test -f ../../include/uapi/linux/if_xdp.h -a -f ../../../include/uapi/linux/if_xdp.h && ( \
	(diff -B ../../include/uapi/linux/if_xdp.h ../../../include/uapi/linux/if_xdp.h >/dev/null) || \
	echo "Warning: Kernel ABI header at 'tools/include/uapi/linux/if_xdp.h' differs from latest version at 'include/uapi/linux/if_xdp.h'" >&2 )) || true
	$(Q)$(MAKE) $(build)=libbpf

$(OUTPUT)libbpf.so: $(OUTPUT)libbpf.so.$(LIBBPF_VERSION)

$(OUTPUT)libbpf.so.$(LIBBPF_VERSION): $(BPF_IN)
	$(QUIET_LINK)$(CC) --shared -Wl,-soname,libbpf.so.$(VERSION) \
				    -Wl,--version-script=$(VERSION_SCRIPT) $^ -lelf -o $@
	@ln -sf $(@F) $(OUTPUT)libbpf.so
	@ln -sf $(@F) $(OUTPUT)libbpf.so.$(VERSION)

$(OUTPUT)libbpf.a: $(BPF_IN)
	$(QUIET_LINK)$(RM) $@; $(AR) rcs $@ $^

$(OUTPUT)test_libbpf: test_libbpf.cpp $(OUTPUT)libbpf.a
	$(QUIET_LINK)$(CXX) $(INCLUDES) $^ -lelf -o $@

$(OUTPUT)libbpf.pc:
	$(QUIET_GEN)sed -e "s|@PREFIX@|$(prefix)|" \
		-e "s|@LIBDIR@|$(libdir_SQ)|" \
		-e "s|@VERSION@|$(LIBBPF_VERSION)|" \
		< libbpf.pc.template > $@

check: check_abi

check_abi: $(OUTPUT)libbpf.so
	@if [ "$(GLOBAL_SYM_COUNT)" != "$(VERSIONED_SYM_COUNT)" ]; then	 \
		echo "Warning: Num of global symbols in $(BPF_IN)"	 \
		     "($(GLOBAL_SYM_COUNT)) does NOT match with num of"	 \
		     "versioned symbols in $^ ($(VERSIONED_SYM_COUNT))." \
		     "Please make sure all LIBBPF_API symbols are"	 \
		     "versioned in $(VERSION_SCRIPT)." >&2;		 \
		exit 1;							 \
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
	$(INSTALL) $1 $(if $3,-m $3,) '$(DESTDIR_SQ)$2'
endef

install_lib: all_cmd
	$(call QUIET_INSTALL, $(LIB_TARGET)) \
		$(call do_install_mkdir,$(libdir_SQ)); \
		cp -fpR $(LIB_FILE) $(DESTDIR)$(libdir_SQ)

install_headers:
	$(call QUIET_INSTALL, headers) \
		$(call do_install,bpf.h,$(prefix)/include/bpf,644); \
		$(call do_install,libbpf.h,$(prefix)/include/bpf,644); \
		$(call do_install,btf.h,$(prefix)/include/bpf,644); \
		$(call do_install,xsk.h,$(prefix)/include/bpf,644);

install_pkgconfig: $(PC_FILE)
	$(call QUIET_INSTALL, $(PC_FILE)) \
		$(call do_install,$(PC_FILE),$(libdir_SQ)/pkgconfig,644)

install: install_lib install_pkgconfig

### Cleaning rules

config-clean:
	$(call QUIET_CLEAN, config)
	$(Q)$(MAKE) -C $(srctree)/tools/build/feature/ clean >/dev/null

clean:
	$(call QUIET_CLEAN, libbpf) $(RM) $(TARGETS) $(CXX_TEST_TARGET) \
		*.o *~ *.a *.so *.so.$(VERSION) .*.d .*.cmd *.pc LIBBPF-CFLAGS
	$(call QUIET_CLEAN, core-gen) $(RM) $(OUTPUT)FEATURE-DUMP.libbpf



PHONY += force elfdep bpfdep
force:

elfdep:
	@if [ "$(feature-libelf)" != "1" ]; then echo "No libelf found"; exit 1 ; fi

bpfdep:
	@if [ "$(feature-bpf)" != "1" ]; then echo "BPF API too old"; exit 1 ; fi

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
