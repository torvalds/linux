# SPDX-License-Identifier: GPL-2.0
include ../../../build/Build.include
include ../../../scripts/Makefile.arch
include ../../../scripts/Makefile.include

CXX ?= $(CROSS_COMPILE)g++

CURDIR := $(abspath .)
TOOLSDIR := $(abspath ../../..)
LIBDIR := $(TOOLSDIR)/lib
BPFDIR := $(LIBDIR)/bpf
TOOLSINCDIR := $(TOOLSDIR)/include
TOOLSARCHINCDIR := $(TOOLSDIR)/arch/$(SRCARCH)/include
BPFTOOLDIR := $(TOOLSDIR)/bpf/bpftool
APIDIR := $(TOOLSINCDIR)/uapi
ifneq ($(O),)
GENDIR := $(O)/include/generated
else
GENDIR := $(abspath ../../../../include/generated)
endif
GENHDR := $(GENDIR)/autoconf.h
PKG_CONFIG ?= $(CROSS_COMPILE)pkg-config

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

BPF_GCC		?= $(shell command -v bpf-gcc;)
SAN_CFLAGS	?=
SAN_LDFLAGS	?= $(SAN_CFLAGS)
RELEASE		?=
OPT_FLAGS	?= $(if $(RELEASE),-O2,-O0)

LIBELF_CFLAGS	:= $(shell $(PKG_CONFIG) libelf --cflags 2>/dev/null)
LIBELF_LIBS	:= $(shell $(PKG_CONFIG) libelf --libs 2>/dev/null || echo -lelf)

SKIP_DOCS	?=
SKIP_LLVM	?=

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

CFLAGS += -g $(OPT_FLAGS) -rdynamic -std=gnu11				\
	  -Wall -Werror -fno-omit-frame-pointer				\
	  $(GENFLAGS) $(SAN_CFLAGS) $(LIBELF_CFLAGS)			\
	  -I$(CURDIR) -I$(INCLUDE_DIR) -I$(GENDIR) -I$(LIBDIR)		\
	  -I$(TOOLSINCDIR) -I$(TOOLSARCHINCDIR) -I$(APIDIR) -I$(OUTPUT)
LDFLAGS += $(SAN_LDFLAGS)
LDLIBS += $(LIBELF_LIBS) -lz -lrt -lpthread

PCAP_CFLAGS	:= $(shell $(PKG_CONFIG) --cflags libpcap 2>/dev/null && echo "-DTRAFFIC_MONITOR=1")
PCAP_LIBS	:= $(shell $(PKG_CONFIG) --libs libpcap 2>/dev/null)
LDLIBS += $(PCAP_LIBS)
CFLAGS += $(PCAP_CFLAGS)

# Some utility functions use LLVM libraries
jit_disasm_helpers.c-CFLAGS = $(LLVM_CFLAGS)

ifneq ($(LLVM),)
# Silence some warnings when compiled with clang
CFLAGS += -Wno-unused-command-line-argument
endif

# Check whether bpf cpu=v4 is supported or not by clang
ifneq ($(shell $(CLANG) --target=bpf -mcpu=help 2>&1 | grep 'v4'),)
CLANG_CPUV4 := 1
endif

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_progs \
	test_sockmap \
	test_tcpnotify_user \
	test_progs-no_alu32
TEST_INST_SUBDIRS := no_alu32

# Also test bpf-gcc, if present
ifneq ($(BPF_GCC),)
TEST_GEN_PROGS += test_progs-bpf_gcc
TEST_INST_SUBDIRS += bpf_gcc

# The following tests contain C code that, although technically legal,
# triggers GCC warnings that cannot be disabled: declaration of
# anonymous struct types in function parameter lists.
progs/btf_dump_test_case_bitfields.c-bpf_gcc-CFLAGS := -Wno-error
progs/btf_dump_test_case_namespacing.c-bpf_gcc-CFLAGS := -Wno-error
progs/btf_dump_test_case_packing.c-bpf_gcc-CFLAGS := -Wno-error
progs/btf_dump_test_case_padding.c-bpf_gcc-CFLAGS := -Wno-error
progs/btf_dump_test_case_syntax.c-bpf_gcc-CFLAGS := -Wno-error

endif

ifneq ($(CLANG_CPUV4),)
TEST_GEN_PROGS += test_progs-cpuv4
TEST_INST_SUBDIRS += cpuv4
endif

TEST_GEN_FILES = test_tc_edt.bpf.o
TEST_FILES = xsk_prereqs.sh $(wildcard progs/btf_dump_test_case_*.c)

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_lirc_mode2.sh \
	test_tc_tunnel.sh \
	test_tc_edt.sh \
	test_xdping.sh \
	test_bpftool_build.sh \
	test_bpftool.sh \
	test_bpftool_map.sh \
	test_bpftool_metadata.sh \
	test_doc_build.sh \
	test_xsk.sh \
	test_xdp_features.sh

TEST_PROGS_EXTENDED := \
	ima_setup.sh verify_sig_setup.sh \
	test_bpftool.py

TEST_KMODS := bpf_testmod.ko bpf_test_no_cfi.ko bpf_test_modorder_x.ko \
	bpf_test_modorder_y.ko bpf_test_rqspinlock.ko
TEST_KMOD_TARGETS = $(addprefix $(OUTPUT)/,$(TEST_KMODS))

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = \
	bench \
	flow_dissector_load \
	runqslower \
	test_cpp \
	test_lirc_mode2_user \
	veristat \
	xdp_features \
	xdp_hw_metadata \
	xdp_synproxy \
	xdping \
	xskxceiver

TEST_GEN_FILES += $(TEST_KMODS) liburandom_read.so urandom_read sign-file uprobe_multi

ifneq ($(V),1)
submake_extras := feature_display=0
endif

# override lib.mk's default rules
OVERRIDE_TARGETS := 1
override define CLEAN
	$(call msg,CLEAN)
	$(Q)$(RM) -r $(TEST_GEN_PROGS)
	$(Q)$(RM) -r $(TEST_GEN_PROGS_EXTENDED)
	$(Q)$(RM) -r $(TEST_GEN_FILES)
	$(Q)$(RM) -r $(TEST_KMODS)
	$(Q)$(RM) -r $(EXTRA_CLEAN)
	$(Q)$(MAKE) -C test_kmods clean
	$(Q)$(MAKE) docs-clean
endef

include ../lib.mk

NON_CHECK_FEAT_TARGETS := clean docs-clean
CHECK_FEAT := $(filter-out $(NON_CHECK_FEAT_TARGETS),$(or $(MAKECMDGOALS), "none"))
ifneq ($(CHECK_FEAT),)
FEATURE_USER := .selftests
FEATURE_TESTS := llvm
FEATURE_DISPLAY := $(FEATURE_TESTS)

# Makefile.feature expects OUTPUT to end with a slash
ifeq ($(shell expr $(MAKE_VERSION) \>= 4.4), 1)
$(let OUTPUT,$(OUTPUT)/,\
	$(eval include ../../../build/Makefile.feature))
else
override OUTPUT := $(OUTPUT)/
$(eval include ../../../build/Makefile.feature)
override OUTPUT := $(patsubst %/,%,$(OUTPUT))
endif
endif

ifneq ($(SKIP_LLVM),1)
ifeq ($(feature-llvm),1)
  LLVM_CFLAGS  += -DHAVE_LLVM_SUPPORT
  LLVM_CONFIG_LIB_COMPONENTS := mcdisassembler all-targets
  # both llvm-config and lib.mk add -D_GNU_SOURCE, which ends up as conflict
  LLVM_CFLAGS  += $(filter-out -D_GNU_SOURCE,$(shell $(LLVM_CONFIG) --cflags))
  # Prefer linking statically if it's available, otherwise fallback to shared
  ifeq ($(shell $(LLVM_CONFIG) --link-static --libs >/dev/null 2>&1 && echo static),static)
    LLVM_LDLIBS  += $(shell $(LLVM_CONFIG) --link-static --libs $(LLVM_CONFIG_LIB_COMPONENTS))
    LLVM_LDLIBS  += $(filter-out -lxml2,$(shell $(LLVM_CONFIG) --link-static --system-libs $(LLVM_CONFIG_LIB_COMPONENTS)))
    LLVM_LDLIBS  += -lstdc++
  else
    LLVM_LDLIBS  += $(shell $(LLVM_CONFIG) --link-shared --libs $(LLVM_CONFIG_LIB_COMPONENTS))
  endif
  LLVM_LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags)
endif
endif

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
RUNQSLOWER_OUTPUT := $(BUILD_DIR)/runqslower/

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))
ifeq ($(VMLINUX_BTF),)
$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)")
endif

# Define simple and short `make test_progs`, `make test_maps`, etc targets
# to build individual tests.
# NOTE: Semicolon at the end is critical to override lib.mk's default static
# rule for binaries.
$(notdir $(TEST_GEN_PROGS) $(TEST_KMODS)				\
	 $(TEST_GEN_PROGS_EXTENDED)): %: $(OUTPUT)/% ;

# sort removes libbpf duplicates when not cross-building
MAKE_DIRS := $(sort $(BUILD_DIR)/libbpf $(HOST_BUILD_DIR)/libbpf	\
	       $(BUILD_DIR)/bpftool $(HOST_BUILD_DIR)/bpftool		\
	       $(HOST_BUILD_DIR)/resolve_btfids				\
	       $(RUNQSLOWER_OUTPUT) $(INCLUDE_DIR))
$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

$(OUTPUT)/%.o: %.c
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $(filter %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/%:%.c
	$(call msg,BINARY,,$@)
	$(Q)$(LINK.c) $^ $(LDLIBS) -o $@

# LLVM's ld.lld doesn't support all the architectures, so use it only on x86
ifeq ($(SRCARCH),$(filter $(SRCARCH),x86 riscv))
LLD := lld
else
LLD := $(shell command -v $(LD))
endif

# Filter out -static for liburandom_read.so and its dependent targets so that static builds
# do not fail. Static builds leave urandom_read relying on system-wide shared libraries.
$(OUTPUT)/liburandom_read.so: urandom_read_lib1.c urandom_read_lib2.c liburandom_read.map
	$(call msg,LIB,,$@)
	$(Q)$(CLANG) $(CLANG_TARGET_ARCH) \
		     $(filter-out -static,$(CFLAGS) $(LDFLAGS)) \
		     $(filter %.c,$^) $(filter-out -static,$(LDLIBS)) \
		     -Wno-unused-command-line-argument \
		     -fuse-ld=$(LLD) -Wl,-znoseparate-code -Wl,--build-id=sha1 \
		     -Wl,--version-script=liburandom_read.map \
		     -fPIC -shared -o $@

$(OUTPUT)/urandom_read: urandom_read.c urandom_read_aux.c $(OUTPUT)/liburandom_read.so
	$(call msg,BINARY,,$@)
	$(Q)$(CLANG) $(CLANG_TARGET_ARCH) \
		     $(filter-out -static,$(CFLAGS) $(LDFLAGS)) $(filter %.c,$^) \
		     -Wno-unused-command-line-argument \
		     -lurandom_read $(filter-out -static,$(LDLIBS)) -L$(OUTPUT) \
		     -fuse-ld=$(LLD) -Wl,-znoseparate-code -Wl,--build-id=sha1 \
		     -Wl,-rpath=. -o $@

$(OUTPUT)/sign-file: ../../../../scripts/sign-file.c
	$(call msg,SIGN-FILE,,$@)
	$(Q)$(CC) $(shell $(PKG_CONFIG) --cflags libcrypto 2> /dev/null) \
		  $< -o $@ \
		  $(shell $(PKG_CONFIG) --libs libcrypto 2> /dev/null || echo -lcrypto)

# This should really be a grouped target, but make versions before 4.3 don't
# support that for regular rules. However, pattern matching rules are implicitly
# treated as grouped even with older versions of make, so as a workaround, the
# subst() turns the rule into a pattern matching rule
$(addprefix test_kmods/,$(subst .ko,%ko,$(TEST_KMODS))): $(VMLINUX_BTF) $(RESOLVE_BTFIDS) $(wildcard test_kmods/Makefile test_kmods/*.[ch])
	$(Q)$(RM) test_kmods/*.ko test_kmods/*.mod.o # force re-compilation
	$(Q)$(MAKE) $(submake_extras) -C test_kmods	\
		RESOLVE_BTFIDS=$(RESOLVE_BTFIDS)	\
		EXTRA_CFLAGS='' EXTRA_LDFLAGS=''

$(TEST_KMOD_TARGETS): $(addprefix test_kmods/,$(TEST_KMODS))
	$(call msg,MOD,,$@)
	$(Q)cp test_kmods/$(@F) $@


DEFAULT_BPFTOOL := $(HOST_SCRATCH_DIR)/sbin/bpftool
ifneq ($(CROSS_COMPILE),)
CROSS_BPFTOOL := $(SCRATCH_DIR)/sbin/bpftool
TRUNNER_BPFTOOL := $(CROSS_BPFTOOL)
USE_BOOTSTRAP := ""
else
TRUNNER_BPFTOOL := $(DEFAULT_BPFTOOL)
USE_BOOTSTRAP := "bootstrap/"
endif

$(OUTPUT)/runqslower: $(BPFOBJ) | $(DEFAULT_BPFTOOL) $(RUNQSLOWER_OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(TOOLSDIR)/bpf/runqslower	       \
		    OUTPUT=$(RUNQSLOWER_OUTPUT) VMLINUX_BTF=$(VMLINUX_BTF)     \
		    BPFTOOL_OUTPUT=$(HOST_BUILD_DIR)/bpftool/		       \
		    BPFOBJ_OUTPUT=$(BUILD_DIR)/libbpf/			       \
		    BPFOBJ=$(BPFOBJ) BPF_INCLUDE=$(INCLUDE_DIR)		       \
		    BPF_TARGET_ENDIAN=$(BPF_TARGET_ENDIAN)		       \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(SAN_CFLAGS) $(EXTRA_CFLAGS)' \
		    EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)' &&	       \
		    cp $(RUNQSLOWER_OUTPUT)runqslower $@

TEST_GEN_PROGS_EXTENDED += $(TRUNNER_BPFTOOL)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(BPFOBJ)

TESTING_HELPERS	:= $(OUTPUT)/testing_helpers.o
CGROUP_HELPERS	:= $(OUTPUT)/cgroup_helpers.o
UNPRIV_HELPERS  := $(OUTPUT)/unpriv_helpers.o
TRACE_HELPERS	:= $(OUTPUT)/trace_helpers.o
JSON_WRITER		:= $(OUTPUT)/json_writer.o
CAP_HELPERS	:= $(OUTPUT)/cap_helpers.o
NETWORK_HELPERS := $(OUTPUT)/network_helpers.o

$(OUTPUT)/test_sockmap: $(CGROUP_HELPERS) $(TESTING_HELPERS)
$(OUTPUT)/test_tcpnotify_user: $(CGROUP_HELPERS) $(TESTING_HELPERS) $(TRACE_HELPERS)
$(OUTPUT)/test_sock_fields: $(CGROUP_HELPERS) $(TESTING_HELPERS)
$(OUTPUT)/test_tag: $(TESTING_HELPERS)
$(OUTPUT)/test_lirc_mode2_user: $(TESTING_HELPERS)
$(OUTPUT)/xdping: $(TESTING_HELPERS)
$(OUTPUT)/flow_dissector_load: $(TESTING_HELPERS)
$(OUTPUT)/test_maps: $(TESTING_HELPERS)
$(OUTPUT)/test_verifier: $(TESTING_HELPERS) $(CAP_HELPERS) $(UNPRIV_HELPERS)
$(OUTPUT)/xsk.o: $(BPFOBJ)

BPFTOOL ?= $(DEFAULT_BPFTOOL)
$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)    \
		    $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)			       \
		    ARCH= CROSS_COMPILE= CC="$(HOSTCC)" LD="$(HOSTLD)" 	       \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(EXTRA_CFLAGS)'	       \
		    EXTRA_LDFLAGS='$(EXTRA_LDFLAGS)'			       \
		    OUTPUT=$(HOST_BUILD_DIR)/bpftool/			       \
		    LIBBPF_OUTPUT=$(HOST_BUILD_DIR)/libbpf/		       \
		    LIBBPF_DESTDIR=$(HOST_SCRATCH_DIR)/			       \
		    prefix= DESTDIR=$(HOST_SCRATCH_DIR)/ install-bin

ifneq ($(CROSS_COMPILE),)
$(CROSS_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)	\
		    $(BPFOBJ) | $(BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)				\
		    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)			\
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(EXTRA_CFLAGS)'		\
		    EXTRA_LDFLAGS='$(EXTRA_LDFLAGS)'				\
		    OUTPUT=$(BUILD_DIR)/bpftool/				\
		    LIBBPF_OUTPUT=$(BUILD_DIR)/libbpf/				\
		    LIBBPF_DESTDIR=$(SCRATCH_DIR)/				\
		    prefix= DESTDIR=$(SCRATCH_DIR)/ install-bin
endif

ifneq ($(SKIP_DOCS),1)
all: docs
endif

docs:
	$(Q)RST2MAN_OPTS="--exit-status=1" $(MAKE) $(submake_extras)	\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

docs-clean:
	$(Q)$(MAKE) $(submake_extras)					\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   $(APIDIR)/linux/bpf.h					       \
	   | $(BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(SAN_CFLAGS) $(EXTRA_CFLAGS)' \
		    EXTRA_LDFLAGS='$(SAN_LDFLAGS) $(EXTRA_LDFLAGS)'	       \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

ifneq ($(BPFOBJ),$(HOST_BPFOBJ))
$(HOST_BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
		$(APIDIR)/linux/bpf.h					       \
		| $(HOST_BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR)                             \
		    ARCH= CROSS_COMPILE=				       \
		    EXTRA_CFLAGS='-g $(OPT_FLAGS) $(EXTRA_CFLAGS)'	       \
		    EXTRA_LDFLAGS='$(EXTRA_LDFLAGS)'			       \
		    OUTPUT=$(HOST_BUILD_DIR)/libbpf/			       \
		    CC="$(HOSTCC)" LD="$(HOSTLD)"			       \
		    DESTDIR=$(HOST_SCRATCH_DIR)/ prefix= all install_headers
endif

# vmlinux.h is first dumped to a temporary file and then compared to
# the previous version. This helps to avoid unnecessary re-builds of
# $(TRUNNER_BPF_OBJS)
$(INCLUDE_DIR)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL) | $(INCLUDE_DIR)
ifeq ($(VMLINUX_H),)
	$(call msg,GEN,,$@)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $(INCLUDE_DIR)/.vmlinux.h.tmp
	$(Q)cmp -s $(INCLUDE_DIR)/.vmlinux.h.tmp $@ || mv $(INCLUDE_DIR)/.vmlinux.h.tmp $@
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
		CC="$(HOSTCC)" LD="$(HOSTLD)" AR="$(HOSTAR)" \
		LIBBPF_INCLUDE=$(HOST_INCLUDE_DIR) \
		OUTPUT=$(HOST_BUILD_DIR)/resolve_btfids/ BPFOBJ=$(HOST_BPFOBJ)

# Get Clang's default includes on this system, as opposed to those seen by
# '--target=bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__loongarch_grlen ' | awk '{printf("-D__BITS_PER_LONG=%d", $$3)}') \
$(shell $(1) $(2) -dM -E - </dev/null | grep -E 'MIPS(EL|EB)|_MIPS_SZ(PTR|LONG) |_MIPS_SIM |_ABI(O32|N32|64) ' | awk '{printf("-D%s=%s ", $$2, $$3)}')
endef

# Determine target endianness.
IS_LITTLE_ENDIAN := $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
MENDIAN:=$(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)
BPF_TARGET_ENDIAN:=$(if $(IS_LITTLE_ENDIAN),--target=bpfel,--target=bpfeb)

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))
BPF_CFLAGS = -g -Wall -Werror -D__TARGET_ARCH_$(SRCARCH) $(MENDIAN)	\
	     -I$(INCLUDE_DIR) -I$(CURDIR) -I$(APIDIR)			\
	     -I$(abspath $(OUTPUT)/../usr/include)			\
	     -std=gnu11		 					\
	     -fno-strict-aliasing 					\
	     -Wno-compare-distinct-pointer-types
# TODO: enable me -Wsign-compare

CLANG_CFLAGS = $(CLANG_SYS_INCLUDES)

$(OUTPUT)/test_l4lb_noinline.o: BPF_CFLAGS += -fno-inline
$(OUTPUT)/test_xdp_noinline.o: BPF_CFLAGS += -fno-inline

$(OUTPUT)/flow_dissector_load.o: flow_dissector_load.h
$(OUTPUT)/cgroup_getset_retval_hooks.o: cgroup_getset_retval_hooks.h

# Build BPF object using Clang
# $1 - input .c file
# $2 - output .o file
# $3 - CFLAGS
# $4 - binary name
define CLANG_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$4,$2)
	$(Q)$(CLANG) $3 -O2 $(BPF_TARGET_ENDIAN) -c $1 -mcpu=v3 -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but with disabled alu32
define CLANG_NOALU32_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$4,$2)
	$(Q)$(CLANG) $3 -O2 $(BPF_TARGET_ENDIAN) -c $1 -mcpu=v2 -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but with cpu-v4
define CLANG_CPUV4_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$4,$2)
	$(Q)$(CLANG) $3 -O2 $(BPF_TARGET_ENDIAN) -c $1 -mcpu=v4 -o $2
endef
# Build BPF object using GCC
define GCC_BPF_BUILD_RULE
	$(call msg,GCC-BPF,$4,$2)
	$(Q)$(BPF_GCC) $3 -DBPF_NO_PRESERVE_ACCESS_INDEX -Wno-attributes -O2 -c $1 -o $2
endef

SKEL_BLACKLIST := btf__% test_pinning_invalid.c test_sk_assign.c

LINKED_SKELS := test_static_linked.skel.h linked_funcs.skel.h		\
		linked_vars.skel.h linked_maps.skel.h 			\
		test_subskeleton.skel.h test_subskeleton_lib.skel.h	\
		test_usdt.skel.h

LSKELS := fexit_sleep.c trace_printk.c trace_vprintk.c map_ptr_kern.c 	\
	core_kern.c core_kern_overflow.c test_ringbuf.c			\
	test_ringbuf_n.c test_ringbuf_map_key.c test_ringbuf_write.c

LSKELS_SIGNED := fentry_test.c fexit_test.c atomics.c

# Generate both light skeleton and libbpf skeleton for these
LSKELS_EXTRA := test_ksyms_module.c test_ksyms_weak.c kfunc_call_test.c \
	kfunc_call_test_subprog.c
SKEL_BLACKLIST += $$(LSKELS) $$(LSKELS_SIGNED)

test_static_linked.skel.h-deps := test_static_linked1.bpf.o test_static_linked2.bpf.o
linked_funcs.skel.h-deps := linked_funcs1.bpf.o linked_funcs2.bpf.o
linked_vars.skel.h-deps := linked_vars1.bpf.o linked_vars2.bpf.o
linked_maps.skel.h-deps := linked_maps1.bpf.o linked_maps2.bpf.o
# In the subskeleton case, we want the test_subskeleton_lib.subskel.h file
# but that's created as a side-effect of the skel.h generation.
test_subskeleton.skel.h-deps := test_subskeleton_lib2.bpf.o test_subskeleton_lib.bpf.o test_subskeleton.bpf.o
test_subskeleton_lib.skel.h-deps := test_subskeleton_lib2.bpf.o test_subskeleton_lib.bpf.o
test_usdt.skel.h-deps := test_usdt.bpf.o test_usdt_multispec.bpf.o
xsk_xdp_progs.skel.h-deps := xsk_xdp_progs.bpf.o
xdp_hw_metadata.skel.h-deps := xdp_hw_metadata.bpf.o
xdp_features.skel.h-deps := xdp_features.bpf.o

LINKED_BPF_OBJS := $(foreach skel,$(LINKED_SKELS),$($(skel)-deps))
LINKED_BPF_SRCS := $(patsubst %.bpf.o,%.c,$(LINKED_BPF_OBJS))

HEADERS_FOR_BPF_OBJS := $(wildcard $(BPFDIR)/*.bpf.h)		\
			$(addprefix $(BPFDIR)/,	bpf_core_read.h	\
			                        bpf_endian.h	\
						bpf_helpers.h	\
			                        bpf_tracing.h)

# Set up extra TRUNNER_XXX "temporary" variables in the environment (relies on
# $eval()) and pass control to DEFINE_TEST_RUNNER_RULES.
# Parameters:
# $1 - test runner base binary name (e.g., test_progs)
# $2 - test runner extra "flavor" (e.g., no_alu32, cpuv4, bpf_gcc, etc)
define DEFINE_TEST_RUNNER

LSKEL_SIGN := -S -k $(PRIVATE_KEY) -i $(VERIFICATION_CERT)
TRUNNER_OUTPUT := $(OUTPUT)$(if $2,/)$2
TRUNNER_BINARY := $1$(if $2,-)$2
TRUNNER_TEST_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.test.o,	\
				 $$(notdir $$(wildcard $(TRUNNER_TESTS_DIR)/*.c)))
TRUNNER_EXTRA_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.o,		\
				 $$(filter %.c,$(TRUNNER_EXTRA_SOURCES)))
TRUNNER_EXTRA_HDRS := $$(filter %.h,$(TRUNNER_EXTRA_SOURCES))
TRUNNER_TESTS_HDR := $(TRUNNER_TESTS_DIR)/tests.h
TRUNNER_BPF_SRCS := $$(notdir $$(wildcard $(TRUNNER_BPF_PROGS_DIR)/*.c))
TRUNNER_BPF_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.bpf.o, $$(TRUNNER_BPF_SRCS))
TRUNNER_BPF_SKELS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.skel.h,	\
				 $$(filter-out $(SKEL_BLACKLIST) $(LINKED_BPF_SRCS),\
					       $$(TRUNNER_BPF_SRCS)))
TRUNNER_BPF_LSKELS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.lskel.h, $$(LSKELS) $$(LSKELS_EXTRA))
TRUNNER_BPF_SKELS_LINKED := $$(addprefix $$(TRUNNER_OUTPUT)/,$(LINKED_SKELS))
TRUNNER_BPF_LSKELS_SIGNED := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.lskel.h, $$(LSKELS_SIGNED))
TEST_GEN_FILES += $$(TRUNNER_BPF_OBJS)

# Evaluate rules now with extra TRUNNER_XXX variables above already defined
$$(eval $$(call DEFINE_TEST_RUNNER_RULES,$1,$2))

endef

# Using TRUNNER_XXX variables, provided by callers of DEFINE_TEST_RUNNER and
# set up by DEFINE_TEST_RUNNER itself, create test runner build rules with:
# $1 - test runner base binary name (e.g., test_progs)
# $2 - test runner extra "flavor" (e.g., no_alu32, cpuv4, bpf_gcc, etc)
define DEFINE_TEST_RUNNER_RULES

ifeq ($($(TRUNNER_OUTPUT)-dir),)
$(TRUNNER_OUTPUT)-dir := y
$(TRUNNER_OUTPUT):
	$$(call msg,MKDIR,,$$@)
	$(Q)mkdir -p $$@
endif

# ensure we set up BPF objects generation rule just once for a given
# input/output directory combination
ifeq ($($(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs),)
$(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs := y
$(TRUNNER_BPF_OBJS): $(TRUNNER_OUTPUT)/%.bpf.o:				\
		     $(TRUNNER_BPF_PROGS_DIR)/%.c			\
		     $(TRUNNER_BPF_PROGS_DIR)/*.h			\
		     $$(INCLUDE_DIR)/vmlinux.h				\
		     $(HEADERS_FOR_BPF_OBJS)				\
		     | $(TRUNNER_OUTPUT) $$(BPFOBJ)
	$$(call $(TRUNNER_BPF_BUILD_RULE),$$<,$$@,			\
					  $(TRUNNER_BPF_CFLAGS)         \
					  $$($$<-CFLAGS)		\
					  $$($$<-$2-CFLAGS),$(TRUNNER_BINARY))

$(TRUNNER_BPF_SKELS): %.skel.h: %.bpf.o $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked1.o) $$<
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked2.o) $$(<:.o=.linked1.o)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked3.o) $$(<:.o=.linked2.o)
	$(Q)diff $$(<:.o=.linked2.o) $$(<:.o=.linked3.o)
	$(Q)$$(BPFTOOL) gen skeleton $$(<:.o=.linked3.o) name $$(notdir $$(<:.bpf.o=)) > $$@
	$(Q)$$(BPFTOOL) gen subskeleton $$(<:.o=.linked3.o) name $$(notdir $$(<:.bpf.o=)) > $$(@:.skel.h=.subskel.h)
	$(Q)rm -f $$(<:.o=.linked1.o) $$(<:.o=.linked2.o) $$(<:.o=.linked3.o)

$(TRUNNER_BPF_LSKELS): %.lskel.h: %.bpf.o $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked1.o) $$<
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked2.o) $$(<:.o=.llinked1.o)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked3.o) $$(<:.o=.llinked2.o)
	$(Q)diff $$(<:.o=.llinked2.o) $$(<:.o=.llinked3.o)
	$(Q)$$(BPFTOOL) gen skeleton -L $$(<:.o=.llinked3.o) name $$(notdir $$(<:.bpf.o=_lskel)) > $$@
	$(Q)rm -f $$(<:.o=.llinked1.o) $$(<:.o=.llinked2.o) $$(<:.o=.llinked3.o)

$(TRUNNER_BPF_LSKELS_SIGNED): %.lskel.h: %.bpf.o $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY) (signed),$$@)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked1.o) $$<
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked2.o) $$(<:.o=.llinked1.o)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.llinked3.o) $$(<:.o=.llinked2.o)
	$(Q)diff $$(<:.o=.llinked2.o) $$(<:.o=.llinked3.o)
	$(Q)$$(BPFTOOL) gen skeleton $(LSKEL_SIGN) $$(<:.o=.llinked3.o) name $$(notdir $$(<:.bpf.o=_lskel)) > $$@
	$(Q)rm -f $$(<:.o=.llinked1.o) $$(<:.o=.llinked2.o) $$(<:.o=.llinked3.o)

$(LINKED_BPF_OBJS): %: $(TRUNNER_OUTPUT)/%

# .SECONDEXPANSION here allows to correctly expand %-deps variables as prerequisites
.SECONDEXPANSION:
$(TRUNNER_BPF_SKELS_LINKED): $(TRUNNER_OUTPUT)/%: $$$$(%-deps) $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,LINK-BPF,$(TRUNNER_BINARY),$$(@:.skel.h=.bpf.o))
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked1.o) $$(addprefix $(TRUNNER_OUTPUT)/,$$($$(@F)-deps))
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked2.o) $$(@:.skel.h=.linked1.o)
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked3.o) $$(@:.skel.h=.linked2.o)
	$(Q)diff $$(@:.skel.h=.linked2.o) $$(@:.skel.h=.linked3.o)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen skeleton $$(@:.skel.h=.linked3.o) name $$(notdir $$(@:.skel.h=)) > $$@
	$(Q)$$(BPFTOOL) gen subskeleton $$(@:.skel.h=.linked3.o) name $$(notdir $$(@:.skel.h=)) > $$(@:.skel.h=.subskel.h)
	$(Q)rm -f $$(@:.skel.h=.linked1.o) $$(@:.skel.h=.linked2.o) $$(@:.skel.h=.linked3.o)

# When the compiler generates a %.d file, only skel basenames (not
# full paths) are specified as prerequisites for corresponding %.o
# file. vpath directives below instruct make to search for skel files
# in TRUNNER_OUTPUT, if they are not present in the working directory.
vpath %.skel.h $(TRUNNER_OUTPUT)
vpath %.lskel.h $(TRUNNER_OUTPUT)
vpath %.subskel.h $(TRUNNER_OUTPUT)

endif

# ensure we set up tests.h header generation rule just once
ifeq ($($(TRUNNER_TESTS_DIR)-tests-hdr),)
$(TRUNNER_TESTS_DIR)-tests-hdr := y
$(TRUNNER_TESTS_HDR): $(TRUNNER_TESTS_DIR)/*.c
	$$(call msg,TEST-HDR,$(TRUNNER_BINARY),$$@)
	$$(shell (echo '/* Generated header, do not edit */';					\
		  sed -n -E 's/^void (serial_)?test_([a-zA-Z0-9_]+)\((void)?\).*/DEFINE_TEST(\2)/p'	\
			$(TRUNNER_TESTS_DIR)/*.c | sort ;	\
		 ) > $$@)
endif

# compile individual test files
# Note: we cd into output directory to ensure embedded BPF object is found
$(TRUNNER_TEST_OBJS): $(TRUNNER_OUTPUT)/%.test.o:			\
		      $(TRUNNER_TESTS_DIR)/%.c				\
		      | $(TRUNNER_OUTPUT)/%.test.d
	$$(call msg,TEST-OBJ,$(TRUNNER_BINARY),$$@)
	$(Q)cd $$(@D) && $$(CC) -I. $$(CFLAGS) -MMD -MT $$@ -c $(CURDIR)/$$< $$(LDLIBS) -o $$(@F)

$(TRUNNER_TEST_OBJS:.o=.d): $(TRUNNER_OUTPUT)/%.test.d:			\
			    $(TRUNNER_TESTS_DIR)/%.c			\
			    $(TRUNNER_EXTRA_HDRS)			\
			    $(TRUNNER_BPF_SKELS)			\
			    $(TRUNNER_BPF_LSKELS)			\
			    $(TRUNNER_BPF_LSKELS_SIGNED)		\
			    $(TRUNNER_BPF_SKELS_LINKED)			\
			    $$(BPFOBJ) | $(TRUNNER_OUTPUT)

ifeq ($(filter clean docs-clean,$(MAKECMDGOALS)),)
include $(wildcard $(TRUNNER_TEST_OBJS:.o=.d))
endif

# add per extra obj CFGLAGS definitions
$(foreach N,$(patsubst $(TRUNNER_OUTPUT)/%.o,%,$(TRUNNER_EXTRA_OBJS)),	\
	$(eval $(TRUNNER_OUTPUT)/$(N).o: CFLAGS += $($(N).c-CFLAGS)))

$(TRUNNER_EXTRA_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		       %.c						\
		       $(TRUNNER_EXTRA_HDRS)				\
		       $(VERIFY_SIG_HDR)				\
		       $(TRUNNER_TESTS_HDR)				\
		       $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(call msg,EXT-OBJ,$(TRUNNER_BINARY),$$@)
	$(Q)$$(CC) $$(CFLAGS) -c $$< $$(LDLIBS) -o $$@

# non-flavored in-srctree builds receive special treatment, in particular, we
# do not need to copy extra resources (see e.g. test_btf_dump_case())
$(TRUNNER_BINARY)-extras: $(TRUNNER_EXTRA_FILES) | $(TRUNNER_OUTPUT)
ifneq ($2:$(OUTPUT),:$(shell pwd))
	$$(call msg,EXT-COPY,$(TRUNNER_BINARY),$(TRUNNER_EXTRA_FILES))
	$(Q)rsync -aq $$^ $(TRUNNER_OUTPUT)/
endif

# some X.test.o files have runtime dependencies on Y.bpf.o files
$(OUTPUT)/$(TRUNNER_BINARY): | $(TRUNNER_BPF_OBJS)

$(OUTPUT)/$(TRUNNER_BINARY): $(TRUNNER_TEST_OBJS)			\
			     $(TRUNNER_EXTRA_OBJS) $$(BPFOBJ)		\
			     $(RESOLVE_BTFIDS)				\
			     $(TRUNNER_BPFTOOL)				\
			     $(OUTPUT)/veristat				\
			     | $(TRUNNER_BINARY)-extras
	$$(call msg,BINARY,,$$@)
	$(Q)$$(CC) $$(CFLAGS) $$(filter %.a %.o,$$^) $$(LDLIBS) $$(LLVM_LDLIBS) $$(LDFLAGS) $$(LLVM_LDFLAGS) -o $$@
	$(Q)$(RESOLVE_BTFIDS) --btf $(TRUNNER_OUTPUT)/btf_data.bpf.o $$@
	$(Q)ln -sf $(if $2,..,.)/tools/build/bpftool/$(USE_BOOTSTRAP)bpftool \
		   $(OUTPUT)/$(if $2,$2/)bpftool

endef

VERIFY_SIG_SETUP := $(CURDIR)/verify_sig_setup.sh
VERIFY_SIG_HDR := verification_cert.h
VERIFICATION_CERT   := $(BUILD_DIR)/signing_key.der
PRIVATE_KEY := $(BUILD_DIR)/signing_key.pem

$(VERIFICATION_CERT) $(PRIVATE_KEY): $(VERIFY_SIG_SETUP)
	$(Q)mkdir -p $(BUILD_DIR)
	$(Q)$(VERIFY_SIG_SETUP) genkey $(BUILD_DIR)

$(VERIFY_SIG_HDR): $(VERIFICATION_CERT)
	$(Q)xxd -i -n test_progs_verification_cert $< > $@

# Define test_progs test runner.
TRUNNER_TESTS_DIR := prog_tests
TRUNNER_BPF_PROGS_DIR := progs
TRUNNER_EXTRA_SOURCES := test_progs.c		\
			 cgroup_helpers.c	\
			 trace_helpers.c	\
			 network_helpers.c	\
			 testing_helpers.c	\
			 btf_helpers.c		\
			 cap_helpers.c		\
			 unpriv_helpers.c 	\
			 netlink_helpers.c	\
			 jit_disasm_helpers.c	\
			 io_helpers.c		\
			 test_loader.c		\
			 xsk.c			\
			 disasm.c		\
			 disasm_helpers.c	\
			 json_writer.c 		\
			 $(VERIFY_SIG_HDR)		\
			 flow_dissector_load.h	\
			 ip_check_defrag_frags.h
TRUNNER_EXTRA_FILES := $(OUTPUT)/urandom_read				\
		       $(OUTPUT)/liburandom_read.so			\
		       $(OUTPUT)/xdp_synproxy				\
		       $(OUTPUT)/sign-file				\
		       $(OUTPUT)/uprobe_multi				\
		       $(TEST_KMOD_TARGETS)				\
		       ima_setup.sh 					\
		       $(VERIFY_SIG_SETUP)				\
		       $(wildcard progs/btf_dump_test_case_*.c)		\
		       $(wildcard progs/*.bpf.o)
TRUNNER_BPF_BUILD_RULE := CLANG_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS) -DENABLE_ATOMICS_TESTS
$(eval $(call DEFINE_TEST_RUNNER,test_progs))

# Define test_progs-no_alu32 test runner.
TRUNNER_BPF_BUILD_RULE := CLANG_NOALU32_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS)
$(eval $(call DEFINE_TEST_RUNNER,test_progs,no_alu32))

# Define test_progs-cpuv4 test runner.
ifneq ($(CLANG_CPUV4),)
TRUNNER_BPF_BUILD_RULE := CLANG_CPUV4_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS) -DENABLE_ATOMICS_TESTS
$(eval $(call DEFINE_TEST_RUNNER,test_progs,cpuv4))
endif

# Define test_progs BPF-GCC-flavored test runner.
ifneq ($(BPF_GCC),)
TRUNNER_BPF_BUILD_RULE := GCC_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(call get_sys_includes,gcc,)
$(eval $(call DEFINE_TEST_RUNNER,test_progs,bpf_gcc))
endif

# Define test_maps test runner.
TRUNNER_TESTS_DIR := map_tests
TRUNNER_BPF_PROGS_DIR := progs
TRUNNER_EXTRA_SOURCES := test_maps.c
TRUNNER_EXTRA_FILES :=
TRUNNER_BPF_BUILD_RULE := $$(error no BPF objects should be built)
TRUNNER_BPF_CFLAGS :=
$(eval $(call DEFINE_TEST_RUNNER,test_maps))

# Define test_verifier test runner.
# It is much simpler than test_maps/test_progs and sufficiently different from
# them (e.g., test.h is using completely pattern), that it's worth just
# explicitly defining all the rules explicitly.
verifier/tests.h: verifier/*.c
	$(shell ( cd verifier/; \
		  echo '/* Generated header, do not edit */'; \
		  echo '#ifdef FILL_ARRAY'; \
		  ls *.c 2> /dev/null | sed -e 's@\(.*\)@#include \"\1\"@'; \
		  echo '#endif' \
		) > verifier/tests.h)
$(OUTPUT)/test_verifier: test_verifier.c verifier/tests.h $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

# Include find_bit.c to compile xskxceiver.
EXTRA_SRC := $(TOOLSDIR)/lib/find_bit.c
$(OUTPUT)/xskxceiver: $(EXTRA_SRC) xskxceiver.c xskxceiver.h $(OUTPUT)/network_helpers.o $(OUTPUT)/xsk.o $(OUTPUT)/xsk_xdp_progs.skel.h $(BPFOBJ) | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/xdp_hw_metadata: xdp_hw_metadata.c $(OUTPUT)/network_helpers.o $(OUTPUT)/xsk.o $(OUTPUT)/xdp_hw_metadata.skel.h | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/xdp_features: xdp_features.c $(OUTPUT)/network_helpers.o $(OUTPUT)/xdp_features.skel.h | $(OUTPUT)
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

# Make sure we are able to include and link libbpf against c++.
CXXFLAGS += $(CFLAGS)
CXXFLAGS := $(subst -D_GNU_SOURCE=,,$(CXXFLAGS))
CXXFLAGS := $(subst -std=gnu11,-std=gnu++11,$(CXXFLAGS))
$(OUTPUT)/test_cpp: test_cpp.cpp $(OUTPUT)/test_core_extern.skel.h $(BPFOBJ)
	$(call msg,CXX,,$@)
	$(Q)$(CXX) $(CXXFLAGS) $(filter %.a %.o %.cpp,$^) $(LDLIBS) -o $@

# Benchmark runner
$(OUTPUT)/bench_%.o: benchs/bench_%.c bench.h $(BPFOBJ)
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -O2 -c $(filter %.c,$^) $(LDLIBS) -o $@
$(OUTPUT)/bench_rename.o: $(OUTPUT)/test_overhead.skel.h
$(OUTPUT)/bench_trigger.o: $(OUTPUT)/trigger_bench.skel.h
$(OUTPUT)/bench_ringbufs.o: $(OUTPUT)/ringbuf_bench.skel.h \
			    $(OUTPUT)/perfbuf_bench.skel.h
$(OUTPUT)/bench_bloom_filter_map.o: $(OUTPUT)/bloom_filter_bench.skel.h
$(OUTPUT)/bench_bpf_loop.o: $(OUTPUT)/bpf_loop_bench.skel.h
$(OUTPUT)/bench_strncmp.o: $(OUTPUT)/strncmp_bench.skel.h
$(OUTPUT)/bench_bpf_hashmap_full_update.o: $(OUTPUT)/bpf_hashmap_full_update_bench.skel.h
$(OUTPUT)/bench_local_storage.o: $(OUTPUT)/local_storage_bench.skel.h
$(OUTPUT)/bench_local_storage_rcu_tasks_trace.o: $(OUTPUT)/local_storage_rcu_tasks_trace_bench.skel.h
$(OUTPUT)/bench_local_storage_create.o: $(OUTPUT)/bench_local_storage_create.skel.h
$(OUTPUT)/bench_bpf_hashmap_lookup.o: $(OUTPUT)/bpf_hashmap_lookup.skel.h
$(OUTPUT)/bench_htab_mem.o: $(OUTPUT)/htab_mem_bench.skel.h
$(OUTPUT)/bench_bpf_crypto.o: $(OUTPUT)/crypto_bench.skel.h
$(OUTPUT)/bench_sockmap.o: $(OUTPUT)/bench_sockmap_prog.skel.h
$(OUTPUT)/bench_lpm_trie_map.o: $(OUTPUT)/lpm_trie_bench.skel.h $(OUTPUT)/lpm_trie_map.skel.h
$(OUTPUT)/bench.o: bench.h testing_helpers.h $(BPFOBJ)
$(OUTPUT)/bench: LDLIBS += -lm
$(OUTPUT)/bench: $(OUTPUT)/bench.o \
		 $(TESTING_HELPERS) \
		 $(TRACE_HELPERS) \
		 $(CGROUP_HELPERS) \
		 $(OUTPUT)/bench_count.o \
		 $(OUTPUT)/bench_rename.o \
		 $(OUTPUT)/bench_trigger.o \
		 $(OUTPUT)/bench_ringbufs.o \
		 $(OUTPUT)/bench_bloom_filter_map.o \
		 $(OUTPUT)/bench_bpf_loop.o \
		 $(OUTPUT)/bench_strncmp.o \
		 $(OUTPUT)/bench_bpf_hashmap_full_update.o \
		 $(OUTPUT)/bench_local_storage.o \
		 $(OUTPUT)/bench_local_storage_rcu_tasks_trace.o \
		 $(OUTPUT)/bench_bpf_hashmap_lookup.o \
		 $(OUTPUT)/bench_local_storage_create.o \
		 $(OUTPUT)/bench_htab_mem.o \
		 $(OUTPUT)/bench_bpf_crypto.o \
		 $(OUTPUT)/bench_sockmap.o \
		 $(OUTPUT)/bench_lpm_trie_map.o \
		 #
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.a %.o,$^) $(LDLIBS) -o $@

# This works around GCC warning about snprintf truncating strings like:
#
#   char a[PATH_MAX], b[PATH_MAX];
#   snprintf(a, "%s/foo", b);      // triggers -Wformat-truncation
$(OUTPUT)/veristat.o: CFLAGS += -Wno-format-truncation
$(OUTPUT)/veristat.o: $(BPFOBJ)
$(OUTPUT)/veristat: $(OUTPUT)/veristat.o
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.a %.o,$^) $(LDLIBS) -o $@

# Linking uprobe_multi can fail due to relocation overflows on mips.
$(OUTPUT)/uprobe_multi: CFLAGS += $(if $(filter mips, $(ARCH)),-mxgot)
$(OUTPUT)/uprobe_multi: uprobe_multi.c uprobe_multi.ld
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(CFLAGS) -Wl,-T,uprobe_multi.ld -O0 $(LDFLAGS) 	\
		$(filter-out %.ld,$^) $(LDLIBS) -o $@

EXTRA_CLEAN := $(SCRATCH_DIR) $(HOST_SCRATCH_DIR)			\
	prog_tests/tests.h map_tests/tests.h verifier/tests.h		\
	feature bpftool $(TEST_KMOD_TARGETS)				\
	$(addprefix $(OUTPUT)/,*.o *.d *.skel.h *.lskel.h *.subskel.h	\
			       no_alu32 cpuv4 bpf_gcc			\
			       liburandom_read.so)			\
	$(OUTPUT)/FEATURE-DUMP.selftests

.PHONY: docs docs-clean

# Delete partially updated (corrupted) files on error
.DELETE_ON_ERROR:

DEFAULT_INSTALL_RULE := $(INSTALL_RULE)
override define INSTALL_RULE
	$(DEFAULT_INSTALL_RULE)
	@for DIR in $(TEST_INST_SUBDIRS); do		  \
		mkdir -p $(INSTALL_PATH)/$$DIR;   \
		rsync -a $(OUTPUT)/$$DIR/*.bpf.o $(INSTALL_PATH)/$$DIR;\
	done
endef
