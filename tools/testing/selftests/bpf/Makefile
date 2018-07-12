# SPDX-License-Identifier: GPL-2.0

LIBDIR := ../../../lib
BPFDIR := $(LIBDIR)/bpf
APIDIR := ../../../include/uapi
GENDIR := ../../../../include/generated
GENHDR := $(GENDIR)/autoconf.h

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

CFLAGS += -Wall -O2 -I$(APIDIR) -I$(LIBDIR) -I$(GENDIR) $(GENFLAGS) -I../../../include
LDLIBS += -lcap -lelf -lrt -lpthread

TEST_CUSTOM_PROGS = $(OUTPUT)/urandom_read
all: $(TEST_CUSTOM_PROGS)

$(TEST_CUSTOM_PROGS): urandom_read

urandom_read: urandom_read.c
	$(CC) -o $(TEST_CUSTOM_PROGS) -static $<

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align test_verifier_log test_dev_cgroup test_tcpbpf_user \
	test_sock test_sock_addr

TEST_GEN_FILES = test_pkt_access.o test_xdp.o test_l4lb.o test_tcp_estats.o test_obj_id.o \
	test_pkt_md_access.o test_xdp_redirect.o test_xdp_meta.o sockmap_parse_prog.o     \
	sockmap_verdict_prog.o dev_cgroup.o sample_ret0.o test_tracepoint.o \
	test_l4lb_noinline.o test_xdp_noinline.o test_stacktrace_map.o \
	sample_map_ret0.o test_tcpbpf_kern.o test_stacktrace_build_id.o \
	sockmap_tcp_msg_prog.o connect4_prog.o connect6_prog.o

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_libbpf.sh \
	test_xdp_redirect.sh \
	test_xdp_meta.sh \
	test_offload.py \
	test_sock_addr.sh

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = test_libbpf_open

include ../lib.mk

BPFOBJ := $(OUTPUT)/libbpf.a

$(TEST_GEN_PROGS): $(BPFOBJ)

$(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/libbpf.a

$(OUTPUT)/test_dev_cgroup: cgroup_helpers.c
$(OUTPUT)/test_sock: cgroup_helpers.c
$(OUTPUT)/test_sock_addr: cgroup_helpers.c

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/

CLANG ?= clang
LLC   ?= llc

PROBE := $(shell $(LLC) -march=bpf -mcpu=probe -filetype=null /dev/null 2>&1)

# Let newer LLVM versions transparently probe the kernel for availability
# of full BPF instruction set.
ifeq ($(PROBE),)
  CPU ?= probe
else
  CPU ?= generic
endif

CLANG_FLAGS = -I. -I./include/uapi -I../../../include/uapi \
	      -Wno-compare-distinct-pointer-types

$(OUTPUT)/test_l4lb_noinline.o: CLANG_FLAGS += -fno-inline
$(OUTPUT)/test_xdp_noinline.o: CLANG_FLAGS += -fno-inline

$(OUTPUT)/%.o: %.c
	$(CLANG) $(CLANG_FLAGS) \
		 -O2 -target bpf -emit-llvm -c $< -o - |      \
	$(LLC) -march=bpf -mcpu=$(CPU) -filetype=obj -o $@

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS)
