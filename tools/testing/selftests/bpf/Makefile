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
BPFTOOLDIR := $(TOOLSDIR)/bpf/bpftool
APIDIR := $(TOOLSINCDIR)/uapi
GENDIR := $(abspath ../../../../include/generated)
GENHDR := $(GENDIR)/autoconf.h

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

BPF_GCC		?= $(shell command -v bpf-gcc;)
SAN_CFLAGS	?=
CFLAGS += -g -O0 -rdynamic -Wall $(GENFLAGS) $(SAN_CFLAGS)		\
	  -I$(CURDIR) -I$(INCLUDE_DIR) -I$(GENDIR) -I$(LIBDIR)		\
	  -I$(TOOLSINCDIR) -I$(APIDIR) -I$(OUTPUT)			\
	  -Dbpf_prog_load=bpf_prog_test_load				\
	  -Dbpf_load_program=bpf_test_load_program
LDLIBS += -lcap -lelf -lz -lrt -lpthread

# Silence some warnings when compiled with clang
ifneq ($(LLVM),)
CFLAGS += -Wno-unused-command-line-argument
endif

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_verifier_log test_dev_cgroup \
	test_sock test_sockmap get_cgroup_id_user \
	test_cgroup_storage \
	test_tcpnotify_user test_sysctl \
	test_progs-no_alu32

# Also test bpf-gcc, if present
ifneq ($(BPF_GCC),)
TEST_GEN_PROGS += test_progs-bpf_gcc
endif

TEST_GEN_FILES = test_lwt_ip_encap.o \
	test_tc_edt.o
TEST_FILES = xsk_prereqs.sh \
	$(wildcard progs/btf_dump_test_case_*.c)

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_xdp_redirect.sh \
	test_xdp_redirect_multi.sh \
	test_xdp_meta.sh \
	test_xdp_veth.sh \
	test_offload.py \
	test_sock_addr.sh \
	test_tunnel.sh \
	test_lwt_seg6local.sh \
	test_lirc_mode2.sh \
	test_skb_cgroup_id.sh \
	test_flow_dissector.sh \
	test_xdp_vlan_mode_generic.sh \
	test_xdp_vlan_mode_native.sh \
	test_lwt_ip_encap.sh \
	test_tcp_check_syncookie.sh \
	test_tc_tunnel.sh \
	test_tc_edt.sh \
	test_xdping.sh \
	test_bpftool_build.sh \
	test_bpftool.sh \
	test_bpftool_metadata.sh \
	test_doc_build.sh \
	test_xsk.sh

TEST_PROGS_EXTENDED := with_addr.sh \
	with_tunnels.sh ima_setup.sh \
	test_xdp_vlan.sh test_bpftool.py

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = test_sock_addr test_skb_cgroup_id_user \
	flow_dissector_load test_flow_dissector test_tcp_check_syncookie_user \
	test_lirc_mode2_user xdping test_cpp runqslower bench bpf_testmod.ko \
	xdpxceiver xdp_redirect_multi

TEST_CUSTOM_PROGS = $(OUTPUT)/urandom_read

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
	$(Q)$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)
	$(Q)$(MAKE) -C bpf_testmod clean
	$(Q)$(MAKE) docs-clean
endef

include ../lib.mk

SCRATCH_DIR := $(OUTPUT)/tools
BUILD_DIR := $(SCRATCH_DIR)/build
INCLUDE_DIR := $(SCRATCH_DIR)/include
BPFOBJ := $(BUILD_DIR)/libbpf/libbpf.a
ifneq ($(CROSS_COMPILE),)
HOST_BUILD_DIR		:= $(BUILD_DIR)/host
HOST_SCRATCH_DIR	:= $(OUTPUT)/host-tools
else
HOST_BUILD_DIR		:= $(BUILD_DIR)
HOST_SCRATCH_DIR	:= $(SCRATCH_DIR)
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
$(notdir $(TEST_GEN_PROGS)						\
	 $(TEST_PROGS)							\
	 $(TEST_PROGS_EXTENDED)						\
	 $(TEST_GEN_PROGS_EXTENDED)					\
	 $(TEST_CUSTOM_PROGS)): %: $(OUTPUT)/% ;

# sort removes libbpf duplicates when not cross-building
MAKE_DIRS := $(sort $(BUILD_DIR)/libbpf $(HOST_BUILD_DIR)/libbpf	       \
	       $(HOST_BUILD_DIR)/bpftool $(HOST_BUILD_DIR)/resolve_btfids      \
	       $(INCLUDE_DIR))
$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

$(OUTPUT)/%.o: %.c
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $(filter %.c,$^) $(LDLIBS) -o $@

$(OUTPUT)/%:%.c
	$(call msg,BINARY,,$@)
	$(Q)$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/urandom_read: urandom_read.c
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS) -Wl,--build-id=sha1

$(OUTPUT)/bpf_testmod.ko: $(VMLINUX_BTF) $(wildcard bpf_testmod/Makefile bpf_testmod/*.[ch])
	$(call msg,MOD,,$@)
	$(Q)$(RM) bpf_testmod/bpf_testmod.ko # force re-compilation
	$(Q)$(MAKE) $(submake_extras) -C bpf_testmod
	$(Q)cp bpf_testmod/bpf_testmod.ko $@

$(OUTPUT)/test_stub.o: test_stub.c $(BPFOBJ)
	$(call msg,CC,,$@)
	$(Q)$(CC) -c $(CFLAGS) -o $@ $<

DEFAULT_BPFTOOL := $(HOST_SCRATCH_DIR)/sbin/bpftool

$(OUTPUT)/runqslower: $(BPFOBJ) | $(DEFAULT_BPFTOOL)
	$(Q)$(MAKE) $(submake_extras) -C $(TOOLSDIR)/bpf/runqslower	\
		    OUTPUT=$(SCRATCH_DIR)/ VMLINUX_BTF=$(VMLINUX_BTF)   \
		    BPFOBJ=$(BPFOBJ) BPF_INCLUDE=$(INCLUDE_DIR) &&	\
		    cp $(SCRATCH_DIR)/runqslower $@

TEST_GEN_PROGS_EXTENDED += $(DEFAULT_BPFTOOL)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/test_stub.o $(BPFOBJ)

$(OUTPUT)/test_dev_cgroup: cgroup_helpers.c
$(OUTPUT)/test_skb_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_sock: cgroup_helpers.c
$(OUTPUT)/test_sock_addr: cgroup_helpers.c
$(OUTPUT)/test_sockmap: cgroup_helpers.c
$(OUTPUT)/test_tcpnotify_user: cgroup_helpers.c trace_helpers.c
$(OUTPUT)/get_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_cgroup_storage: cgroup_helpers.c
$(OUTPUT)/test_sock_fields: cgroup_helpers.c
$(OUTPUT)/test_sysctl: cgroup_helpers.c

BPFTOOL ?= $(DEFAULT_BPFTOOL)
$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)    \
		    $(HOST_BPFOBJ) | $(HOST_BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)			       \
		    CC=$(HOSTCC) LD=$(HOSTLD)				       \
		    EXTRA_CFLAGS='-g -O0'				       \
		    OUTPUT=$(HOST_BUILD_DIR)/bpftool/			       \
		    prefix= DESTDIR=$(HOST_SCRATCH_DIR)/ install

all: docs

docs:
	$(Q)RST2MAN_OPTS="--exit-status=1" $(MAKE) $(submake_extras)	\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

docs-clean:
	$(Q)$(MAKE) $(submake_extras)					\
	            -f Makefile.docs					\
	            prefix= OUTPUT=$(OUTPUT)/ DESTDIR=$(OUTPUT)/ $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   ../../../include/uapi/linux/bpf.h                                   \
	   | $(INCLUDE_DIR) $(BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    EXTRA_CFLAGS='-g -O0'				       \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

ifneq ($(BPFOBJ),$(HOST_BPFOBJ))
$(HOST_BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)                \
	   ../../../include/uapi/linux/bpf.h                                   \
	   | $(INCLUDE_DIR) $(HOST_BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR)                             \
		    EXTRA_CFLAGS='-g -O0'				       \
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
		OUTPUT=$(HOST_BUILD_DIR)/resolve_btfids/ BPFOBJ=$(HOST_BPFOBJ)

# Get Clang's default includes on this system, as opposed to those seen by
# '-target bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) -dM -E - </dev/null | grep '#define __riscv_xlen ' | sed 's/#define /-D/' | sed 's/ /=/')
endef

# Determine target endianness.
IS_LITTLE_ENDIAN = $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
MENDIAN=$(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))
BPF_CFLAGS = -g -D__TARGET_ARCH_$(SRCARCH) $(MENDIAN) 			\
	     -I$(INCLUDE_DIR) -I$(CURDIR) -I$(APIDIR)			\
	     -I$(abspath $(OUTPUT)/../usr/include)

CLANG_CFLAGS = $(CLANG_SYS_INCLUDES) \
	       -Wno-compare-distinct-pointer-types

$(OUTPUT)/test_l4lb_noinline.o: BPF_CFLAGS += -fno-inline
$(OUTPUT)/test_xdp_noinline.o: BPF_CFLAGS += -fno-inline

$(OUTPUT)/flow_dissector_load.o: flow_dissector_load.h

# Build BPF object using Clang
# $1 - input .c file
# $2 - output .o file
# $3 - CFLAGS
define CLANG_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(CLANG) $3 -O2 -target bpf -c $1 -o $2 -mcpu=v3
endef
# Similar to CLANG_BPF_BUILD_RULE, but with disabled alu32
define CLANG_NOALU32_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(CLANG) $3 -O2 -target bpf -c $1 -o $2 -mcpu=v2
endef
# Build BPF object using GCC
define GCC_BPF_BUILD_RULE
	$(call msg,GCC-BPF,$(TRUNNER_BINARY),$2)
	$(Q)$(BPF_GCC) $3 -O2 -c $1 -o $2
endef

SKEL_BLACKLIST := btf__% test_pinning_invalid.c test_sk_assign.c

LINKED_SKELS := test_static_linked.skel.h linked_funcs.skel.h		\
		linked_vars.skel.h linked_maps.skel.h

LSKELS := kfunc_call_test.c fentry_test.c fexit_test.c fexit_sleep.c \
	test_ksyms_module.c test_ringbuf.c atomics.c trace_printk.c
SKEL_BLACKLIST += $$(LSKELS)

test_static_linked.skel.h-deps := test_static_linked1.o test_static_linked2.o
linked_funcs.skel.h-deps := linked_funcs1.o linked_funcs2.o
linked_vars.skel.h-deps := linked_vars1.o linked_vars2.o
linked_maps.skel.h-deps := linked_maps1.o linked_maps2.o

LINKED_BPF_SRCS := $(patsubst %.o,%.c,$(foreach skel,$(LINKED_SKELS),$($(skel)-deps)))

# Set up extra TRUNNER_XXX "temporary" variables in the environment (relies on
# $eval()) and pass control to DEFINE_TEST_RUNNER_RULES.
# Parameters:
# $1 - test runner base binary name (e.g., test_progs)
# $2 - test runner extra "flavor" (e.g., no_alu32, gcc-bpf, etc)
define DEFINE_TEST_RUNNER

TRUNNER_OUTPUT := $(OUTPUT)$(if $2,/)$2
TRUNNER_BINARY := $1$(if $2,-)$2
TRUNNER_TEST_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.test.o,	\
				 $$(notdir $$(wildcard $(TRUNNER_TESTS_DIR)/*.c)))
TRUNNER_EXTRA_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.o,		\
				 $$(filter %.c,$(TRUNNER_EXTRA_SOURCES)))
TRUNNER_EXTRA_HDRS := $$(filter %.h,$(TRUNNER_EXTRA_SOURCES))
TRUNNER_TESTS_HDR := $(TRUNNER_TESTS_DIR)/tests.h
TRUNNER_BPF_SRCS := $$(notdir $$(wildcard $(TRUNNER_BPF_PROGS_DIR)/*.c))
TRUNNER_BPF_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.o, $$(TRUNNER_BPF_SRCS))
TRUNNER_BPF_SKELS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.skel.h,	\
				 $$(filter-out $(SKEL_BLACKLIST) $(LINKED_BPF_SRCS),\
					       $$(TRUNNER_BPF_SRCS)))
TRUNNER_BPF_LSKELS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.lskel.h, $$(LSKELS))
TRUNNER_BPF_SKELS_LINKED := $$(addprefix $$(TRUNNER_OUTPUT)/,$(LINKED_SKELS))
TEST_GEN_FILES += $$(TRUNNER_BPF_OBJS)

# Evaluate rules now with extra TRUNNER_XXX variables above already defined
$$(eval $$(call DEFINE_TEST_RUNNER_RULES,$1,$2))

endef

# Using TRUNNER_XXX variables, provided by callers of DEFINE_TEST_RUNNER and
# set up by DEFINE_TEST_RUNNER itself, create test runner build rules with:
# $1 - test runner base binary name (e.g., test_progs)
# $2 - test runner extra "flavor" (e.g., no_alu32, gcc-bpf, etc)
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
$(TRUNNER_BPF_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		     $(TRUNNER_BPF_PROGS_DIR)/%.c			\
		     $(TRUNNER_BPF_PROGS_DIR)/*.h			\
		     $$(INCLUDE_DIR)/vmlinux.h				\
		     $(wildcard $(BPFDIR)/bpf_*.h)			\
		     | $(TRUNNER_OUTPUT) $$(BPFOBJ)
	$$(call $(TRUNNER_BPF_BUILD_RULE),$$<,$$@,			\
					  $(TRUNNER_BPF_CFLAGS))

$(TRUNNER_BPF_SKELS): %.skel.h: %.o $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked1.o) $$<
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked2.o) $$(<:.o=.linked1.o)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked3.o) $$(<:.o=.linked2.o)
	$(Q)diff $$(<:.o=.linked2.o) $$(<:.o=.linked3.o)
	$(Q)$$(BPFTOOL) gen skeleton $$(<:.o=.linked3.o) name $$(notdir $$(<:.o=)) > $$@

$(TRUNNER_BPF_LSKELS): %.lskel.h: %.o $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked1.o) $$<
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked2.o) $$(<:.o=.linked1.o)
	$(Q)$$(BPFTOOL) gen object $$(<:.o=.linked3.o) $$(<:.o=.linked2.o)
	$(Q)diff $$(<:.o=.linked2.o) $$(<:.o=.linked3.o)
	$(Q)$$(BPFTOOL) gen skeleton -L $$(<:.o=.linked3.o) name $$(notdir $$(<:.o=)) > $$@

$(TRUNNER_BPF_SKELS_LINKED): $(TRUNNER_BPF_OBJS) $(BPFTOOL) | $(TRUNNER_OUTPUT)
	$$(call msg,LINK-BPF,$(TRUNNER_BINARY),$$(@:.skel.h=.o))
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked1.o) $$(addprefix $(TRUNNER_OUTPUT)/,$$($$(@F)-deps))
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked2.o) $$(@:.skel.h=.linked1.o)
	$(Q)$$(BPFTOOL) gen object $$(@:.skel.h=.linked3.o) $$(@:.skel.h=.linked2.o)
	$(Q)diff $$(@:.skel.h=.linked2.o) $$(@:.skel.h=.linked3.o)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$(Q)$$(BPFTOOL) gen skeleton $$(@:.skel.h=.linked3.o) name $$(notdir $$(@:.skel.h=)) > $$@
endif

# ensure we set up tests.h header generation rule just once
ifeq ($($(TRUNNER_TESTS_DIR)-tests-hdr),)
$(TRUNNER_TESTS_DIR)-tests-hdr := y
$(TRUNNER_TESTS_HDR): $(TRUNNER_TESTS_DIR)/*.c
	$$(call msg,TEST-HDR,$(TRUNNER_BINARY),$$@)
	$$(shell ( cd $(TRUNNER_TESTS_DIR);				\
		  echo '/* Generated header, do not edit */';		\
		  ls *.c 2> /dev/null |					\
			sed -e 's@\([^\.]*\)\.c@DEFINE_TEST(\1)@';	\
		 ) > $$@)
endif

# compile individual test files
# Note: we cd into output directory to ensure embedded BPF object is found
$(TRUNNER_TEST_OBJS): $(TRUNNER_OUTPUT)/%.test.o:			\
		      $(TRUNNER_TESTS_DIR)/%.c				\
		      $(TRUNNER_EXTRA_HDRS)				\
		      $(TRUNNER_BPF_OBJS)				\
		      $(TRUNNER_BPF_SKELS)				\
		      $(TRUNNER_BPF_LSKELS)				\
		      $(TRUNNER_BPF_SKELS_LINKED)			\
		      $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(call msg,TEST-OBJ,$(TRUNNER_BINARY),$$@)
	$(Q)cd $$(@D) && $$(CC) -I. $$(CFLAGS) -c $(CURDIR)/$$< $$(LDLIBS) -o $$(@F)

$(TRUNNER_EXTRA_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		       %.c						\
		       $(TRUNNER_EXTRA_HDRS)				\
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

$(OUTPUT)/$(TRUNNER_BINARY): $(TRUNNER_TEST_OBJS)			\
			     $(TRUNNER_EXTRA_OBJS) $$(BPFOBJ)		\
			     $(RESOLVE_BTFIDS)				\
			     | $(TRUNNER_BINARY)-extras
	$$(call msg,BINARY,,$$@)
	$(Q)$$(CC) $$(CFLAGS) $$(filter %.a %.o,$$^) $$(LDLIBS) -o $$@
	$(Q)$(RESOLVE_BTFIDS) --no-fail --btf $(TRUNNER_OUTPUT)/btf_data.o $$@

endef

# Define test_progs test runner.
TRUNNER_TESTS_DIR := prog_tests
TRUNNER_BPF_PROGS_DIR := progs
TRUNNER_EXTRA_SOURCES := test_progs.c cgroup_helpers.c trace_helpers.c	\
			 network_helpers.c testing_helpers.c		\
			 btf_helpers.c	flow_dissector_load.h
TRUNNER_EXTRA_FILES := $(OUTPUT)/urandom_read $(OUTPUT)/bpf_testmod.ko	\
		       ima_setup.sh					\
		       $(wildcard progs/btf_dump_test_case_*.c)
TRUNNER_BPF_BUILD_RULE := CLANG_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS)
TRUNNER_BPF_CFLAGS += -DENABLE_ATOMICS_TESTS
$(eval $(call DEFINE_TEST_RUNNER,test_progs))

# Define test_progs-no_alu32 test runner.
TRUNNER_BPF_BUILD_RULE := CLANG_NOALU32_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS)
$(eval $(call DEFINE_TEST_RUNNER,test_progs,no_alu32))

# Define test_progs BPF-GCC-flavored test runner.
ifneq ($(BPF_GCC),)
TRUNNER_BPF_BUILD_RULE := GCC_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(call get_sys_includes,gcc)
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

# Make sure we are able to include and link libbpf against c++.
$(OUTPUT)/test_cpp: test_cpp.cpp $(OUTPUT)/test_core_extern.skel.h $(BPFOBJ)
	$(call msg,CXX,,$@)
	$(Q)$(CXX) $(CFLAGS) $(filter %.a %.o %.cpp,$^) $(LDLIBS) -o $@

# Benchmark runner
$(OUTPUT)/bench_%.o: benchs/bench_%.c bench.h
	$(call msg,CC,,$@)
	$(Q)$(CC) $(CFLAGS) -c $(filter %.c,$^) $(LDLIBS) -o $@
$(OUTPUT)/bench_rename.o: $(OUTPUT)/test_overhead.skel.h
$(OUTPUT)/bench_trigger.o: $(OUTPUT)/trigger_bench.skel.h
$(OUTPUT)/bench_ringbufs.o: $(OUTPUT)/ringbuf_bench.skel.h \
			    $(OUTPUT)/perfbuf_bench.skel.h
$(OUTPUT)/bench.o: bench.h testing_helpers.h
$(OUTPUT)/bench: LDLIBS += -lm
$(OUTPUT)/bench: $(OUTPUT)/bench.o $(OUTPUT)/testing_helpers.o \
		 $(OUTPUT)/bench_count.o \
		 $(OUTPUT)/bench_rename.o \
		 $(OUTPUT)/bench_trigger.o \
		 $(OUTPUT)/bench_ringbufs.o
	$(call msg,BINARY,,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.a %.o,$^) $(LDLIBS)

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS) $(SCRATCH_DIR) $(HOST_SCRATCH_DIR)	\
	prog_tests/tests.h map_tests/tests.h verifier/tests.h		\
	feature								\
	$(addprefix $(OUTPUT)/,*.o *.skel.h *.lskel.h no_alu32 bpf_gcc bpf_testmod.ko)

.PHONY: docs docs-clean
