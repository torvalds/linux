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
LDLIBS += -lcap -lelf -lrt

TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align test_verifier_log test_dev_cgroup

TEST_GEN_FILES = test_pkt_access.o test_xdp.o test_l4lb.o test_tcp_estats.o test_obj_id.o \
	test_pkt_md_access.o test_xdp_redirect.o test_xdp_meta.o sockmap_parse_prog.o     \
	sockmap_verdict_prog.o dev_cgroup.o

TEST_PROGS := test_kmod.sh test_xdp_redirect.sh test_xdp_meta.sh

include ../lib.mk

BPFOBJ := $(OUTPUT)/libbpf.a $(OUTPUT)/cgroup_helpers.c

$(TEST_GEN_PROGS): $(BPFOBJ)

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/

CLANG ?= clang
LLC   ?= llc

PROBE := $(shell llc -march=bpf -mcpu=probe -filetype=null /dev/null 2>&1)

# Let newer LLVM versions transparently probe the kernel for availability
# of full BPF instruction set.
ifeq ($(PROBE),)
  CPU ?= probe
else
  CPU ?= generic
endif

%.o: %.c
	$(CLANG) -I. -I./include/uapi -I../../../include/uapi \
		 -Wno-compare-distinct-pointer-types          \
		 -O2 -target bpf -emit-llvm -c $< -o - |      \
	$(LLC) -march=bpf -mcpu=$(CPU) -filetype=obj -o $@
