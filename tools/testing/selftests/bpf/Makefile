# SPDX-License-Identifier: GPL-2.0
include ../../../../scripts/Kbuild.include
include ../../../scripts/Makefile.arch

CURDIR := $(abspath .)
LIBDIR := $(abspath ../../../lib)
BPFDIR := $(LIBDIR)/bpf
TOOLSDIR := $(abspath ../../../include)
APIDIR := $(TOOLSDIR)/uapi
GENDIR := $(abspath ../../../../include/generated)
GENHDR := $(GENDIR)/autoconf.h

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

CLANG		?= clang
LLC		?= llc
LLVM_OBJCOPY	?= llvm-objcopy
BPF_GCC		?= $(shell command -v bpf-gcc;)
CFLAGS += -g -Wall -O2 $(GENFLAGS) -I$(APIDIR) -I$(LIBDIR) -I$(BPFDIR)	\
	  -I$(GENDIR) -I$(TOOLSDIR) -I$(CURDIR)				\
	  -Dbpf_prog_load=bpf_prog_test_load				\
	  -Dbpf_load_program=bpf_test_load_program
LDLIBS += -lcap -lelf -lrt -lpthread

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align test_verifier_log test_dev_cgroup test_tcpbpf_user \
	test_sock test_btf test_sockmap get_cgroup_id_user test_socket_cookie \
	test_cgroup_storage test_select_reuseport \
	test_netcnt test_tcpnotify_user test_sock_fields test_sysctl test_hashmap \
	test_cgroup_attach xdping test_progs-no_alu32

# Also test bpf-gcc, if present
ifneq ($(BPF_GCC),)
TEST_GEN_PROGS += test_progs-bpf_gcc
endif

TEST_GEN_FILES =
TEST_FILES =

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
	test_bpftool_build.sh

TEST_PROGS_EXTENDED := with_addr.sh \
	with_tunnels.sh \
	tcp_client.py \
	tcp_server.py \
	test_xdp_vlan.sh

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = test_sock_addr test_skb_cgroup_id_user \
	flow_dissector_load test_flow_dissector test_tcp_check_syncookie_user \
	test_lirc_mode2_user

TEST_CUSTOM_PROGS = urandom_read

include ../lib.mk

# Define simple and short `make test_progs`, `make test_sysctl`, etc targets
# to build individual tests.
# NOTE: Semicolon at the end is critical to override lib.mk's default static
# rule for binaries.
$(notdir $(TEST_GEN_PROGS)						\
	 $(TEST_PROGS)							\
	 $(TEST_PROGS_EXTENDED)						\
	 $(TEST_GEN_PROGS_EXTENDED)					\
	 $(TEST_CUSTOM_PROGS)): %: $(OUTPUT)/% ;

$(OUTPUT)/urandom_read: urandom_read.c
	$(CC) -o $@ $< -Wl,--build-id

BPFOBJ := $(OUTPUT)/libbpf.a

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
$(OUTPUT)/test_cgroup_attach: cgroup_helpers.c

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/

BPF_HELPERS := $(BPFDIR)/bpf_helper_defs.h $(wildcard $(BPFDIR)/bpf_*.h)
$(BPFDIR)/bpf_helper_defs.h:
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/ bpf_helper_defs.h

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

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))
BPF_CFLAGS = -g -D__TARGET_ARCH_$(SRCARCH) 				\
	     -I. -I./include/uapi -I$(APIDIR)				\
	     -I$(BPFDIR) -I$(abspath $(OUTPUT)/../usr/include)

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
	($(CLANG) $3 -O2 -target bpf -emit-llvm				\
		-c $1 -o - || echo "BPF obj compilation failed") | 	\
	$(LLC) -march=bpf -mcpu=probe $4 -filetype=obj -o $2
endef
# Similar to CLANG_BPF_BUILD_RULE, but using native Clang and bpf LLC
define CLANG_NATIVE_BPF_BUILD_RULE
	($(CLANG) $3 -O2 -emit-llvm					\
		-c $1 -o - || echo "BPF obj compilation failed") | 	\
	$(LLC) -march=bpf -mcpu=probe $4 -filetype=obj -o $2
endef
# Build BPF object using GCC
define GCC_BPF_BUILD_RULE
	$(BPF_GCC) $3 $4 -O2 -c $1 -o $2
endef

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
TRUNNER_BPF_OBJS := $$(patsubst %.c,$$(TRUNNER_OUTPUT)/%.o,		\
				$$(notdir $$(wildcard $(TRUNNER_BPF_PROGS_DIR)/*.c)))

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
	mkdir -p $$@
endif

# ensure we set up BPF objects generation rule just once for a given
# input/output directory combination
ifeq ($($(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs),)
$(TRUNNER_BPF_PROGS_DIR)$(if $2,-)$2-bpfobjs := y
$(TRUNNER_BPF_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		     $(TRUNNER_BPF_PROGS_DIR)/%.c			\
		     $(TRUNNER_BPF_PROGS_DIR)/*.h			\
		     $$(BPF_HELPERS) | $(TRUNNER_OUTPUT)
	$$(call $(TRUNNER_BPF_BUILD_RULE),$$<,$$@,			\
					  $(TRUNNER_BPF_CFLAGS),	\
					  $(TRUNNER_BPF_LDFLAGS))
endif

# ensure we set up tests.h header generation rule just once
ifeq ($($(TRUNNER_TESTS_DIR)-tests-hdr),)
$(TRUNNER_TESTS_DIR)-tests-hdr := y
$(TRUNNER_TESTS_HDR): $(TRUNNER_TESTS_DIR)/*.c
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
		      $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	cd $$(@D) && $$(CC) $$(CFLAGS) -c $(CURDIR)/$$< $$(LDLIBS) -o $$(@F)

$(TRUNNER_EXTRA_OBJS): $(TRUNNER_OUTPUT)/%.o:				\
		       %.c						\
		       $(TRUNNER_EXTRA_HDRS)				\
		       $(TRUNNER_TESTS_HDR)				\
		       $$(BPFOBJ) | $(TRUNNER_OUTPUT)
	$$(CC) $$(CFLAGS) -c $$< $$(LDLIBS) -o $$@

$(TRUNNER_BINARY)-extras: $(TRUNNER_EXTRA_FILES) | $(TRUNNER_OUTPUT)
ifneq ($2,)
	# only copy extra resources if in flavored build
	cp -a $$^ $(TRUNNER_OUTPUT)/
endif

$(OUTPUT)/$(TRUNNER_BINARY): $(TRUNNER_TEST_OBJS)			\
			     $(TRUNNER_EXTRA_OBJS) $$(BPFOBJ)		\
			     | $(TRUNNER_BINARY)-extras
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
TRUNNER_BPF_CFLAGS := -I. -I$(OUTPUT) $(BPF_CFLAGS) $(CLANG_CFLAGS)
TRUNNER_BPF_LDFLAGS := -mattr=+alu32
$(eval $(call DEFINE_TEST_RUNNER,test_progs))

# Define test_progs-no_alu32 test runner.
TRUNNER_BPF_LDFLAGS :=
$(eval $(call DEFINE_TEST_RUNNER,test_progs,no_alu32))

# Define test_progs BPF-GCC-flavored test runner.
ifneq ($(BPF_GCC),)
IS_LITTLE_ENDIAN = $(shell $(CC) -dM -E - </dev/null | \
			grep 'define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__')
MENDIAN=$(if $(IS_LITTLE_ENDIAN),-mlittle-endian,-mbig-endian)

TRUNNER_BPF_BUILD_RULE := GCC_BPF_BUILD_RULE
TRUNNER_BPF_CFLAGS := $(BPF_CFLAGS) $(call get_sys_includes,gcc) $(MENDIAN)
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
	$(CC) $(CFLAGS) $(filter %.a %.o %.c,$^) $(LDLIBS) -o $@

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS)					\
	prog_tests/tests.h map_tests/tests.h verifier/tests.h		\
	feature $(OUTPUT)/*.o $(OUTPUT)/no_alu32 $(OUTPUT)/bpf_gcc
