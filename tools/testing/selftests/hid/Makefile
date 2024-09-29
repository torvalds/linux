# SPDX-License-Identifier: GPL-2.0

# based on tools/testing/selftest/bpf/Makefile
include ../../../build/Build.include
include ../../../scripts/Makefile.arch
include ../../../scripts/Makefile.include

TEST_PROGS := hid-core.sh
TEST_PROGS += hid-apple.sh
TEST_PROGS += hid-gamepad.sh
TEST_PROGS += hid-ite.sh
TEST_PROGS += hid-keyboard.sh
TEST_PROGS += hid-mouse.sh
TEST_PROGS += hid-multitouch.sh
TEST_PROGS += hid-sony.sh
TEST_PROGS += hid-tablet.sh
TEST_PROGS += hid-usb_crash.sh
TEST_PROGS += hid-wacom.sh

TEST_FILES := run-hid-tools-tests.sh

CXX ?= $(CROSS_COMPILE)g++

HOSTPKG_CONFIG := pkg-config

CFLAGS += -g -O0 -rdynamic -Wall -Werror -I$(OUTPUT)
CFLAGS += -I$(OUTPUT)/tools/include

LDLIBS += -lelf -lz -lrt -lpthread

# Silence some warnings when compiled with clang
ifneq ($(LLVM),)
CFLAGS += -Wno-unused-command-line-argument
endif

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = hid_bpf

# Emit succinct information message describing current building step
# $1 - generic step name (e.g., CC, LINK, etc);
# $2 - optional "flavor" specifier; if provided, will be emitted as [flavor];
# $3 - target (assumed to be file); only file name will be emitted;
# $4 - optional extra arg, emitted as-is, if provided.
ifeq ($(V),1)
Q =
msg =
else
Q = @
msg = @printf '  %-8s%s %s%s\n' "$(1)" "$(if $(2), [$(2)])" "$(notdir $(3))" "$(if $(4), $(4))";
MAKEFLAGS += --no-print-directory
submake_extras := feature_display=0
endif

# override lib.mk's default rules
OVERRIDE_TARGETS := 1
override define CLEAN
	$(call msg,CLEAN)
	$(Q)$(RM) -r $(TEST_GEN_PROGS)
	$(Q)$(RM) -r $(EXTRA_CLEAN)
endef

include ../lib.mk

TOOLSDIR := $(top_srcdir)/tools
LIBDIR := $(TOOLSDIR)/lib
BPFDIR := $(LIBDIR)/bpf
TOOLSINCDIR := $(TOOLSDIR)/include
BPFTOOLDIR := $(TOOLSDIR)/bpf/bpftool
SCRATCH_DIR := $(OUTPUT)/tools
BUILD_DIR := $(SCRATCH_DIR)/build
INCLUDE_DIR := $(SCRATCH_DIR)/include
BPFOBJ := $(BUILD_DIR)/libbpf/libbpf.a
ifneq ($(CROSS_COMPILE),)
HOST_BUILD_DIR		:= $(BUILD_DIR)/host
HOST_SCRATCH_DIR	:= $(OUTPUT)/host-tools
HOST_INCLUDE_DIR	:= $(HOST_SCRATCH_DIR)/include
else
HOST_BUILD_DIR		:= $(BUILD_DIR)
HOST_SCRATCH_DIR	:= $(SCRATCH_DIR)
HOST_INCLUDE_DIR	:= $(INCLUDE_DIR)
endif
HOST_BPFOBJ := $(HOST_BUILD_DIR)/libbpf/libbpf.a
RESOLVE_BTFIDS := $(HOST_BUILD_DIR)/resolve_btfids/resolve_btfids

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))
ifeq ($(VMLINUX_BTF),)
$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)")
endif

# Define simple and short `make test_progs`, `make test_sysctl`, etc targets
# to build individual tests.
# NOTE: Semicolon at the end is critical to override lib.mk's default static
# rule for binaries.
$(notdir $(TEST_GEN_PROGS)): %: $(OUTPUT)/% ;

# sort removes libbpf duplicates when not cross-building
MAKE_DIRS := $(sort $(BUILD_DIR)/libbpf $(HOST_BUILD_DIR)/libbpf		\
	       $(HOST_BUILD_DIR)/bpftool $(HOST_BUILD_DIR)/resolve_btfids	\
	       $(INCLUDE_DIR))
$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

# LLVM's ld.lld doesn't support all the architectures, so use it only on x86
ifeq ($(SRCARCH),x86)
LLD := lld
else
LLD := ld
endif

DEFAULT_BPFTOOL := $(HOST_SCRATCH_DIR)/sbin/bpftool

TEST_GEN_PROGS_EXTENDED += $(DEFAULT_BPFTOOL)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(BPFOBJ)

BPFTOOL ?= $(DEFAULT_BPFTOOL)
$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)    \
		    $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)			       \
		    ARCH= CROSS_COMPILE= CC=$(HOSTCC) LD=$(HOSTLD) 	       \
		    EXTRA_CFLAGS='-g -O0'				       \
		    OUTPUT=$(HOST_BUILD_DIR)/bpftool/			       \
		    LIBBPF_OUTPUT=$(HOST_BUILD_DIR)/libbpf/		       \
		    LIBBPF_DESTDIR=$(HOST_SCRATCH_DIR)/			       \
		    prefix= DESTDIR=$(HOST_SCRATCH_DIR)/ install-bin

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   | $(BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    EXTRA_CFLAGS='-g -O0'				       \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

ifneq ($(BPFOBJ),$(HOST_BPFOBJ))
$(HOST_BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
		| $(HOST_BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR)                             \
		    EXTRA_CFLAGS='-g -O0' ARCH= CROSS_COMPILE=		       \
		    OUTPUT=$(HOST_BUILD_DIR)/libbpf/ CC=$(HOSTCC) LD=$(HOSTLD) \
		    DESTDIR=$(HOST_SCRATCH_DIR)/ prefix= all install_headers
endif

$(INCLUDE_DIR)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL) | $(INCLUDE_DIR)
ifeq ($(VMLINUX_H),)
	$(call msg,GEN,,$@)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@
else
	$(call msg,CP,,$@)
	$(Q)cp "$(VMLINUX_H)" $@
endif

$(RESOLVE_BTFIDS): $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/resolve_btfids	\
		       $(TOOLSDIR)/bpf/resolve_btfids/main.c	\
		       $(TOOLSDIR)/lib/rbtree.c			\
		       $(TOOLSDIR)/lib/zalloc.c			\
		       $(TOOLSDIR)/lib/string.c			\
		       $(TOOLSDIR)/lib/ctype.c			\
		       $(TOOLSDIR)/lib/str_error_r.c
	$(Q)$(MAKE) $(submake_extras) -C $(TOOLSDIR)/bpf/resolve_btfids	\
		CC=$(HOSTCC) LD=$(HOSTLD) AR=$(HOSTAR) \
		LIBBPF_INCLUDE=$(HOST_INCLUDE_DIR) \
		OUTPUT=$(HOST_BUILD_DIR)/resolve_btfids/ BPFOBJ=$(HOST_BPFOBJ)

# Get Clang's default includes on this system, as opposed to those seen by
# '--target=bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}')
endef

# Determine target endianness.
IS_LITTLE_ENDIAN = $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
MENDIAN=$(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))
BPF_CFLAGS = -g -Werror -D__TARGET_ARCH_$(SRCARCH) $(MENDIAN) 		\
	     -I$(INCLUDE_DIR)

CLANG_CFLAGS = $(CLANG_SYS_INCLUDES) \
	       -Wno-compare-distinct-pointer-types

# Build BPF object using Clang
# $1 - input .c file
# $2 - output .o file
# $3 - CFLAGS
define CLANG_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(CLANG) $3 -O2 --target=bpf -c $1 -mcpu=v3 -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but with disabled alu32
define CLANG_NOALU32_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(CLANG) $3 -O2 --target=bpf -c $1 -mcpu=v2 -o $2
endef
# Build BPF object using GCC
define GCC_BPF_BUILD_RULE
	$(call msg,GCC-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(BPF_GCC) $3 -O2 -c $1 -o $2
endef

BPF_PROGS_DIR := progs
BPF_BUILD_RULE := CLANG_BPF_BUILD_RULE
BPF_SRCS := $(notdir $(wildcard $(BPF_PROGS_DIR)/*.c))
BPF_OBJS := $(patsubst %.c,$(OUTPUT)/%.bpf.o, $(BPF_SRCS))
BPF_SKELS := $(patsubst %.c,$(OUTPUT)/%.skel.h, $(BPF_SRCS))
TEST_GEN_FILES += $(BPF_OBJS)

$(BPF_PROGS_DIR)-bpfobjs := y
$(BPF_OBJS): $(OUTPUT)/%.bpf.o:				\
	     $(BPF_PROGS_DIR)/%.c			\
	     $(wildcard $(BPF_PROGS_DIR)/*.h)		\
	     $(INCLUDE_DIR)/vmlinux.h				\
	     $(wildcard $(BPFDIR)/hid_bpf_*.h)			\
	     $(wildcard $(BPFDIR)/*.bpf.h)			\
	     | $(OUTPUT) $(BPFOBJ)
	$(call $(BPF_BUILD_RULE),$<,$@, $(BPF_CFLAGS))

$(BPF_SKELS): %.skel.h: %.bpf.o $(BPFTOOL) | $(OUTPUT)
	$(call msg,GEN-SKEL,$(BINARY),$@)
	$(Q)$(BPFTOOL) gen object $(<:.o=.linked1.o) $<
	$(Q)$(BPFTOOL) gen skeleton $(<:.o=.linked1.o) name $(notdir $(<:.bpf.o=)) > $@

$(OUTPUT)/%.o: %.c $(BPF_SKELS)
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $(filter %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/%: $(OUTPUT)/%.o
	$(call msg,BINARY,,$@)
	$(Q)$(LINK.c) $^ $(LDLIBS) -o $@

EXTRA_CLEAN := $(SCRATCH_DIR) $(HOST_SCRATCH_DIR) feature bpftool	\
	$(addprefix $(OUTPUT)/,*.o *.skel.h no_alu32)
