# SPDX-License-Identifier: GPL-2.0
include ../../../../scripts/Kbuild.include
include ../../../scripts/Makefile.arch

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

CLANG		?= clang
LLC		?= llc
LLVM_OBJCOPY	?= llvm-objcopy
BPF_GCC		?= $(shell command -v bpf-gcc;)
CFLAGS += -g -rdynamic -Wall -O2 $(GENFLAGS) -I$(CURDIR) -I$(APIDIR)	\
	  -I$(INCLUDE_DIR) -I$(GENDIR) -I$(LIBDIR) -I$(TOOLSINCDIR)	\
	  -Dbpf_prog_load=bpf_prog_test_load				\
	  -Dbpf_load_program=bpf_test_load_program
LDLIBS += -lcap -lelf -lz -lrt -lpthread

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align test_verifier_log test_dev_cgroup test_tcpbpf_user \
	test_sock test_btf test_sockmap get_cgroup_id_user test_socket_cookie \
	test_cgroup_storage \
	test_netcnt test_tcpnotify_user test_sock_fields test_sysctl test_hashmap \
	test_progs-no_alu32

# Also test bpf-gcc, if present
ifneq ($(BPF_GCC),)
TEST_GEN_PROGS += test_progs-bpf_gcc
endif

TEST_GEN_FILES =
TEST_FILES = test_lwt_ip_encap.o \
	test_tc_edt.o

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_xdp_redirect.sh \
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
	test_bpftool.sh

TEST_PROGS_EXTENDED := with_addr.sh \
	with_tunnels.sh \
	tcp_client.py \
	tcp_server.py \
	test_xdp_vlan.sh

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = test_sock_addr test_skb_cgroup_id_user \
	flow_dissector_load test_flow_dissector test_tcp_check_syncookie_user \
	test_lirc_mode2_user xdping test_cpp runqslower

TEST_CUSTOM_PROGS = urandom_read

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
	$(RM) -r $(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES) $(EXTRA_CLEAN)
endef

include ../lib.mk

SCRATCH_DIR := $(OUTPUT)/tools
BUILD_DIR := $(SCRATCH_DIR)/build
INCLUDE_DIR := $(SCRATCH_DIR)/include
BPFOBJ := $(BUILD_DIR)/libbpf/libbpf.a

# Define simple and short `make test_progs`, `make test_sysctl`, etc targets
# to build individual tests.
# NOTE: Semicolon at the end is critical to override lib.mk's default static
# rule for binaries.
$(notdir $(TEST_GEN_PROGS)						\
	 $(TEST_PROGS)							\
	 $(TEST_PROGS_EXTENDED)						\
	 $(TEST_GEN_PROGS_EXTENDED)					\
	 $(TEST_CUSTOM_PROGS)): %: $(OUTPUT)/% ;

$(OUTPUT)/%:%.c
	$(call msg,BINARY,,$@)
	$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/urandom_read: urandom_read.c
	$(call msg,BINARY,,$@)
	$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS) -Wl,--build-id

$(OUTPUT)/test_stub.o: test_stub.c $(BPFOBJ)
	$(call msg,CC,,$@)
	$(CC) -c $(CFLAGS) -o $@ $<

VMLINUX_BTF_PATHS := $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF:= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))

$(OUTPUT)/runqslower: $(BPFOBJ)
	$(Q)$(MAKE) $(submake_extras) -C $(TOOLSDIR)/bpf/runqslower	\
		    OUTPUT=$(SCRATCH_DIR)/ VMLINUX_BTF=$(VMLINUX_BTF)   \
		    BPFOBJ=$(BPFOBJ) BPF_INCLUDE=$(INCLUDE_DIR)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/test_stub.o $(BPFOBJ)

$(OUTPUT)/test_dev_cgroup: cgroup_helpers.c
$(OUTPUT)/test_skb_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_sock: cgroup_helpers.c
$(OUTPUT)/test_sock_addr: cgroup_helpers.c
$(OUTPUT)/test_socket_cookie: cgroup_helpers.c
$(OUTPUT)/test_sockmap: cgroup_helpers.c
$(OUTPUT)/test_tcpbpf_user: cgroup_helpers.c
$(OUTPUT)/test_tcpnotify_user: cgroup_helpers.c trace_helpers.c
$(OUTPUT)/get_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_cgroup_storage: cgroup_helpers.c
$(OUTPUT)/test_netcnt: cgroup_helpers.c
$(OUTPUT)/test_sock_fields: cgroup_helpers.c
$(OUTPUT)/test_sysctl: cgroup_helpers.c

DEFAULT_BPFTOOL := $(SCRATCH_DIR)/sbin/bpftool
BPFTOOL ?= $(DEFAULT_BPFTOOL)
$(DEFAULT_BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)    \
		    $(BPFOBJ) | $(BUILD_DIR)/bpftool
	$(Q)$(MAKE) $(submake_extras)  -C $(BPFTOOLDIR)			       \
		    OUTPUT=$(BUILD_DIR)/bpftool/			       \
		    prefix= DESTDIR=$(SCRATCH_DIR)/ install

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   ../../../include/uapi/linux/bpf.h                                   \
	   | $(INCLUDE_DIR) $(BUILD_DIR)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

$(BUILD_DIR)/libbpf $(BUILD_DIR)/bpftool $(INCLUDE_DIR):
	$(call msg,MKDIR,,$@)
	mkdir -p $@

# Get Clang's default includes on this system, as opposed to those seen by
# '-target bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')
endef

# Determine target endianness.
IS_LITTLE_ENDIAN = $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
MENDIAN=$(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))
BPF_CFLAGS = -g -D__TARGET_ARCH_$(SRCARCH) $(MENDIAN) 			\
	     -I$(INCLUDE_DIR) -I$(CURDIR) -I$(CURDIR)/include/uapi	\
	     -I$(APIDIR) -I$(abspath $(OUTPUT)/../usr/include)

CLANG_CFLAGS = $(CLANG_SYS_INCLUDES) \
	       -Wno-compare-distinct-pointer-types

$(OUTPUT)/test_l4lb_noinline.o: BPF_CFLAGS += -fno-inline
$(OUTPUT)/test_xdp_noinline.o: BPF_CFLAGS += -fno-inline

$(OUTPUT)/flow_dissector_load.o: flow_dissector_load.h

# Build BPF object using Clang
# $1 - input .c file
# $2 - output .o file
# $3 - CFLAGS
# $4 - LDFLAGS
define CLANG_BPF_BUILD_RULE
	$(call msg,CLNG-LLC,$(TRUNNER_BINARY),$2)
	($(CLANG) $3 -O2 -target bpf -emit-llvm				\
		-c $1 -o - || echo "BPF obj compilation failed") | 	\
	$(LLC) -mattr=dwarfris -march=bpf -mcpu=v3 $4 -filetype=obj -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but with disabled alu32
define CLANG_NOALU32_BPF_BUILD_RULE
	$(call msg,CLNG-LLC,$(TRUNNER_BINARY),$2)
	($(CLANG) $3 -O2 -target bpf -emit-llvm				\
		-c $1 -o - || echo "BPF obj compilation failed") | 	\
	$(LLC) -march=bpf -mcpu=v2 $4 -filetype=obj -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but using native Clang and bpf LLC
define CLANG_NATIVE_BPF_BUILD_RULE
	$(call msg,CLNG-BPF,$(TRUNNER_BINARY),$2)
	($(CLANG) $3 -O2 -emit-llvm					\
		-c $1 -o - || echo "BPF obj compilation failed") | 	\
	$(LLC) -march=bpf -mcpu=v3 $4 -filetype=obj -o $2
endef
# Build BPF object using GCC
define GCC_BPF_BUILD_RULE
	$(call msg,GCC-BPF,$(TRUNNER_BINARY),$2)
	$(BPF_GCC) $3 $4 -O2 -c $1 -o $2
endef

SKEL_BLACKLIST := btf__% test_pinning_invalid.c

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
				 $$(filter-out $(SKEL_BLACKLIST),	\
					       $$(TRUNNER_BPF_SRCS)))

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
	mkdir -p $$@
endif

# ensure we set up BPF objects generation rule just once for a given
# input/output directory combination
ifeq ($($(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs),)
$(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs := y
$(TRUNNER_BPF_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		     $(TRUNNER_BPF_PROGS_DIR)/%.c			\
		     $(TRUNNER_BPF_PROGS_DIR)/*.h			\
		     $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(call $(TRUNNER_BPF_BUILD_RULE),$$<,$$@,			\
					  $(TRUNNER_BPF_CFLAGS),	\
					  $(TRUNNER_BPF_LDFLAGS))

$(TRUNNER_BPF_SKELS): $(TRUNNER_OUTPUT)/%.skel.h:			\
		      $(TRUNNER_OUTPUT)/%.o				\
		      | $(BPFTOOL) $(TRUNNER_OUTPUT)
	$$(call msg,GEN-SKEL,$(TRUNNER_BINARY),$$@)
	$$(BPFTOOL) gen skeleton $$< > $$@
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
		      $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(call msg,TEST-OBJ,$(TRUNNER_BINARY),$$@)
	cd $$(@D) && $$(CC) $$(CFLAGS) -c $(CURDIR)/$$< $$(LDLIBS) -o $$(@F)

$(TRUNNER_EXTRA_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		       %.c						\
		       $(TRUNNER_EXTRA_HDRS)				\
		       $(TRUNNER_TESTS_HDR)				\
		       $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(call msg,EXT-OBJ,$(TRUNNER_BINARY),$$@)
	$$(CC) $$(CFLAGS) -c $$< $$(LDLIBS) -o $$@

# only copy extra resources if in flavored build
$(TRUNNER_BINARY)-extras: $(TRUNNER_EXTRA_FILES) | $(TRUNNER_OUTPUT)
ifneq ($2,)
	$$(call msg,EXT-COPY,$(TRUNNER_BINARY),$(TRUNNER_EXTRA_FILES))
	cp -a $$^ $(TRUNNER_OUTPUT)/
endif

$(OUTPUT)/$(TRUNNER_BINARY): $(TRUNNER_TEST_OBJS)			\
			     $(TRUNNER_EXTRA_OBJS) $$(BPFOBJ)		\
			     | $(TRUNNER_BINARY)-extras
	$$(call msg,BINARY,,$$@)
	$$(CC) $$(CFLAGS) $$(filter %.a %.o,$$^) $$(LDLIBS) -o $$@

endef

# Define test_progs test runner.
TRUNNER_TESTS_DIR := prog_tests
TRUNNER_BPF_PROGS_DIR := progs
TRUNNER_EXTRA_SOURCES := test_progs.c cgroup_helpers.c trace_helpers.c	\
			 flow_dissector_load.h
TRUNNER_EXTRA_FILES := $(OUTPUT)/urandom_read				\
		       $(wildcard progs/btf_dump_test_case_*.c)
TRUNNER_BPF_BUILD_RULE := CLANG_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(CLANG_CFLAGS)
TRUNNER_BPF_LDFLAGS := -mattr=+alu32
$(eval $(call DEFINE_TEST_RUNNER,test_progs))

# Define test_progs-no_alu32 test runner.
TRUNNER_BPF_BUILD_RULE := CLANG_NOALU32_BPF_BUILD_RULE
TRUNNER_BPF_LDFLAGS :=
$(eval $(call DEFINE_TEST_RUNNER,test_progs,no_alu32))

# Define test_progs BPF-GCC-flavored test runner.
ifneq ($(BPF_GCC),)
TRUNNER_BPF_BUILD_RULE := GCC_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(call get_sys_includes,gcc)
TRUNNER_BPF_LDFLAGS :=
$(eval $(call DEFINE_TEST_RUNNER,test_progs,bpf_gcc))
endif

# Define test_maps test runner.
TRUNNER_TESTS_DIR := map_tests
TRUNNER_BPF_PROGS_DIR := progs
TRUNNER_EXTRA_SOURCES := test_maps.c
TRUNNER_EXTRA_FILES :=
TRUNNER_BPF_BUILD_RULE := $$(error no BPF objects should be built)
TRUNNER_BPF_CFLAGS :=
TRUNNER_BPF_LDFLAGS :=
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
	$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

# Make sure we are able to include and link libbpf against c++.
$(OUTPUT)/test_cpp: test_cpp.cpp $(OUTPUT)/test_core_extern.skel.h $(BPFOBJ)
	$(call msg,CXX,,$@)
	$(CXX) $(CFLAGS) $^ $(LDLIBS) -o $@

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS) $(SCRATCH_DIR)			\
	prog_tests/tests.h map_tests/tests.h verifier/tests.h		\
	feature								\
	$(addprefix $(OUTPUT)/,*.o *.skel.h no_alu32 bpf_gcc)
