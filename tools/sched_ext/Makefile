# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
include ../build/Build.include
include ../scripts/Makefile.arch
include ../scripts/Makefile.include

all: all_targets

ifneq ($(LLVM),)
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif

CLANG_TARGET_FLAGS_arm          := arm-linux-gnueabi
CLANG_TARGET_FLAGS_arm64        := aarch64-linux-gnu
CLANG_TARGET_FLAGS_hexagon      := hexagon-linux-musl
CLANG_TARGET_FLAGS_m68k         := m68k-linux-gnu
CLANG_TARGET_FLAGS_mips         := mipsel-linux-gnu
CLANG_TARGET_FLAGS_powerpc      := powerpc64le-linux-gnu
CLANG_TARGET_FLAGS_riscv        := riscv64-linux-gnu
CLANG_TARGET_FLAGS_s390         := s390x-linux-gnu
CLANG_TARGET_FLAGS_x86          := x86_64-linux-gnu
CLANG_TARGET_FLAGS              := $(CLANG_TARGET_FLAGS_$(ARCH))

ifeq ($(CROSS_COMPILE),)
ifeq ($(CLANG_TARGET_FLAGS),)
$(error Specify CROSS_COMPILE or add '--target=' option to lib.mk)
else
CLANG_FLAGS     += --target=$(CLANG_TARGET_FLAGS)
endif # CLANG_TARGET_FLAGS
else
CLANG_FLAGS     += --target=$(notdir $(CROSS_COMPILE:%-=%))
endif # CROSS_COMPILE

CC := $(LLVM_PREFIX)clang$(LLVM_SUFFIX) $(CLANG_FLAGS) -fintegrated-as
else
CC := $(CROSS_COMPILE)gcc
endif # LLVM

CURDIR := $(abspath .)
TOOLSDIR := $(abspath ..)
LIBDIR := $(TOOLSDIR)/lib
BPFDIR := $(LIBDIR)/bpf
TOOLSINCDIR := $(TOOLSDIR)/include
BPFTOOLDIR := $(TOOLSDIR)/bpf/bpftool
APIDIR := $(TOOLSINCDIR)/uapi
GENDIR := $(abspath ../../include/generated)
GENHDR := $(GENDIR)/autoconf.h

ifeq ($(O),)
OUTPUT_DIR := $(CURDIR)/build
else
OUTPUT_DIR := $(O)/build
endif # O
OBJ_DIR := $(OUTPUT_DIR)/obj
INCLUDE_DIR := $(OUTPUT_DIR)/include
BPFOBJ_DIR := $(OBJ_DIR)/libbpf
SCXOBJ_DIR := $(OBJ_DIR)/sched_ext
BINDIR := $(OUTPUT_DIR)/bin
BPFOBJ := $(BPFOBJ_DIR)/libbpf.a
ifneq ($(CROSS_COMPILE),)
HOST_BUILD_DIR		:= $(OBJ_DIR)/host/obj
HOST_OUTPUT_DIR	:= $(OBJ_DIR)/host
HOST_INCLUDE_DIR	:= $(HOST_OUTPUT_DIR)/include
else
HOST_BUILD_DIR		:= $(OBJ_DIR)
HOST_OUTPUT_DIR	:= $(OUTPUT_DIR)
HOST_INCLUDE_DIR	:= $(INCLUDE_DIR)
endif
HOST_BPFOBJ := $(HOST_BUILD_DIR)/libbpf/libbpf.a
RESOLVE_BTFIDS := $(HOST_BUILD_DIR)/resolve_btfids/resolve_btfids
DEFAULT_BPFTOOL := $(HOST_OUTPUT_DIR)/sbin/bpftool

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)					\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)		\
		     ../../vmlinux						\
		     /sys/kernel/btf/vmlinux					\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))
ifeq ($(VMLINUX_BTF),)
$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)")
endif

BPFTOOL ?= $(DEFAULT_BPFTOOL)

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

CFLAGS += -g -O2 -rdynamic -pthread -Wall -Werror $(GENFLAGS)			\
	  -I$(INCLUDE_DIR) -I$(GENDIR) -I$(LIBDIR)				\
	  -I$(TOOLSINCDIR) -I$(APIDIR) -I$(CURDIR)/include

# Silence some warnings when compiled with clang
ifneq ($(LLVM),)
CFLAGS += -Wno-unused-command-line-argument
endif

LDFLAGS += -lelf -lz -lpthread

IS_LITTLE_ENDIAN = $(shell $(CC) -dM -E - </dev/null |				\
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')

# Get Clang's default includes on this system, as opposed to those seen by
# '-target bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}')
endef

BPF_CFLAGS = -g -D__TARGET_ARCH_$(SRCARCH)					\
	     $(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)		\
	     -I$(CURDIR)/include -I$(CURDIR)/include/bpf-compat			\
	     -I$(INCLUDE_DIR) -I$(APIDIR)					\
	     -I../../include							\
	     $(call get_sys_includes,$(CLANG))					\
	     -Wall -Wno-compare-distinct-pointer-types				\
	     -O2 -mcpu=v3

# sort removes libbpf duplicates when not cross-building
MAKE_DIRS := $(sort $(OBJ_DIR)/libbpf $(HOST_BUILD_DIR)/libbpf			\
	       $(HOST_BUILD_DIR)/bpftool $(HOST_BUILD_DIR)/resolve_btfids	\
	       $(INCLUDE_DIR) $(SCXOBJ_DIR) $(BINDIR))

$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)			\
	   $(APIDIR)/linux/bpf.h						\
	   | $(OBJ_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) CROSS_COMPILE=$(CROSS_COMPILE) 		\
		    -C $(BPFDIR) OUTPUT=$(OBJ_DIR)/libbpf/			\
		    EXTRA_CFLAGS='-g -O0 -fPIC'					\
		    LDFLAGS="$(LDFLAGS)"					\
		    DESTDIR=$(OUTPUT_DIR) prefix= all install_headers

$(HOST_BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		\
	   $(APIDIR)/linux/bpf.h						\
	   | $(HOST_BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) 				\
		    OUTPUT=$(HOST_BUILD_DIR)/libbpf/				\
		    ARCH= CROSS_COMPILE= CC="$(HOSTCC)" LD=$(HOSTLD)		\
		    EXTRA_CFLAGS='-g -O0 -fPIC'					\
		    DESTDIR=$(HOST_OUTPUT_DIR) prefix= all install_headers

$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)	\
		    $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)				\
		    ARCH= CROSS_COMPILE= CC="$(HOSTCC)" LD=$(HOSTLD)		\
		    EXTRA_CFLAGS='-g -O0'					\
		    OUTPUT=$(HOST_BUILD_DIR)/bpftool/				\
		    LIBBPF_OUTPUT=$(HOST_BUILD_DIR)/libbpf/			\
		    LIBBPF_DESTDIR=$(HOST_OUTPUT_DIR)/				\
		    prefix= DESTDIR=$(HOST_OUTPUT_DIR)/ install-bin

$(INCLUDE_DIR)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL) | $(INCLUDE_DIR)
ifeq ($(VMLINUX_H),)
	$(call msg,GEN,,$@)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@
else
	$(call msg,CP,,$@)
	$(Q)cp "$(VMLINUX_H)" $@
endif

$(SCXOBJ_DIR)/%.bpf.o: %.bpf.c $(INCLUDE_DIR)/vmlinux.h include/scx/*.h		\
		       | $(BPFOBJ) $(SCXOBJ_DIR)
	$(call msg,CLNG-BPF,,$(notdir $@))
	$(Q)$(CLANG) $(BPF_CFLAGS) -target bpf -c $< -o $@

$(INCLUDE_DIR)/%.bpf.skel.h: $(SCXOBJ_DIR)/%.bpf.o $(INCLUDE_DIR)/vmlinux.h $(BPFTOOL)
	$(eval sched=$(notdir $@))
	$(call msg,GEN-SKEL,,$(sched))
	$(Q)$(BPFTOOL) gen object $(<:.o=.linked1.o) $<
	$(Q)$(BPFTOOL) gen object $(<:.o=.linked2.o) $(<:.o=.linked1.o)
	$(Q)$(BPFTOOL) gen object $(<:.o=.linked3.o) $(<:.o=.linked2.o)
	$(Q)diff $(<:.o=.linked2.o) $(<:.o=.linked3.o)
	$(Q)$(BPFTOOL) gen skeleton $(<:.o=.linked3.o) name $(subst .bpf.skel.h,,$(sched)) > $@
	$(Q)$(BPFTOOL) gen subskeleton $(<:.o=.linked3.o) name $(subst .bpf.skel.h,,$(sched)) > $(@:.skel.h=.subskel.h)

SCX_COMMON_DEPS := include/scx/common.h include/scx/user_exit_info.h | $(BINDIR)

c-sched-targets = scx_simple scx_qmap scx_central scx_flatcg

$(addprefix $(BINDIR)/,$(c-sched-targets)): \
	$(BINDIR)/%: \
		$(filter-out %.bpf.c,%.c) \
		$(INCLUDE_DIR)/%.bpf.skel.h \
		$(SCX_COMMON_DEPS)
	$(eval sched=$(notdir $@))
	$(CC) $(CFLAGS) -c $(sched).c -o $(SCXOBJ_DIR)/$(sched).o
	$(CC) -o $@ $(SCXOBJ_DIR)/$(sched).o $(BPFOBJ) $(LDFLAGS)

$(c-sched-targets): %: $(BINDIR)/%

install: all
	$(Q)mkdir -p $(DESTDIR)/usr/local/bin/
	$(Q)cp $(BINDIR)/* $(DESTDIR)/usr/local/bin/

clean:
	rm -rf $(OUTPUT_DIR) $(HOST_OUTPUT_DIR)
	rm -f *.o *.bpf.o *.bpf.skel.h *.bpf.subskel.h
	rm -f $(c-sched-targets)

help:
	@echo   'Building targets'
	@echo   '================'
	@echo   ''
	@echo   '  all		  - Compile all schedulers'
	@echo   ''
	@echo   'Alternatively, you may compile individual schedulers:'
	@echo   ''
	@printf '  %s\n' $(c-sched-targets)
	@echo   ''
	@echo   'For any scheduler build target, you may specify an alternative'
	@echo   'build output path with the O= environment variable. For example:'
	@echo   ''
	@echo   '   O=/tmp/sched_ext make all'
	@echo   ''
	@echo   'will compile all schedulers, and emit the build artifacts to'
	@echo   '/tmp/sched_ext/build.'
	@echo   ''
	@echo   ''
	@echo   'Installing targets'
	@echo   '=================='
	@echo   ''
	@echo   '  install	  - Compile and install all schedulers to /usr/bin.'
	@echo   '		    You may specify the DESTDIR= environment variable'
	@echo   '		    to indicate a prefix for /usr/bin. For example:'
	@echo   ''
	@echo   '                     DESTDIR=/tmp/sched_ext make install'
	@echo   ''
	@echo   '		    will build the schedulers in CWD/build, and'
	@echo   '		    install the schedulers to /tmp/sched_ext/usr/bin.'
	@echo   ''
	@echo   ''
	@echo   'Cleaning targets'
	@echo   '================'
	@echo   ''
	@echo   '  clean		  - Remove all generated files'

all_targets: $(c-sched-targets)

.PHONY: all all_targets $(c-sched-targets) clean help

# delete failed targets
.DELETE_ON_ERROR:

# keep intermediate (.bpf.skel.h, .bpf.o, etc) targets
.SECONDARY:
