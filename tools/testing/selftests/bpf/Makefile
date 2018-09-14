# SPDX-License-Identifier: GPL-2.0

LIBDIR := ../../../lib
BPFDIR := $(LIBDIR)/bpf
APIDIR := ../../../include/uapi
GENDIR := ../../../../include/generated
GENHDR := $(GENDIR)/autoconf.h

ifneq ($(wildcard $(GENHDR)),)
  GENFLAGS := -DHAVE_GENHDR
endif

CFLAGS += -Wall -O2 -I$(APIDIR) -I$(LIBDIR) -I$(BPFDIR) -I$(GENDIR) $(GENFLAGS) -I../../../include
LDLIBS += -lcap -lelf -lrt -lpthread

TEST_CUSTOM_PROGS = $(OUTPUT)/urandom_read
all: $(TEST_CUSTOM_PROGS)

$(TEST_CUSTOM_PROGS): $(OUTPUT)/%: %.c
	$(CC) -o $(TEST_CUSTOM_PROGS) -static $< -Wl,--build-id

# Order correspond to 'make run_tests' order
TEST_GEN_PROGS = test_verifier test_tag test_maps test_lru_map test_lpm_map test_progs \
	test_align test_verifier_log test_dev_cgroup test_tcpbpf_user \
	test_sock test_btf test_sockmap test_lirc_mode2_user get_cgroup_id_user \
	test_socket_cookie test_cgroup_storage test_select_reuseport

TEST_GEN_FILES = test_pkt_access.o test_xdp.o test_l4lb.o test_tcp_estats.o test_obj_id.o \
	test_pkt_md_access.o test_xdp_redirect.o test_xdp_meta.o sockmap_parse_prog.o     \
	sockmap_verdict_prog.o dev_cgroup.o sample_ret0.o test_tracepoint.o \
	test_l4lb_noinline.o test_xdp_noinline.o test_stacktrace_map.o \
	sample_map_ret0.o test_tcpbpf_kern.o test_stacktrace_build_id.o \
	sockmap_tcp_msg_prog.o connect4_prog.o connect6_prog.o test_adjust_tail.o \
	test_btf_haskv.o test_btf_nokv.o test_sockmap_kern.o test_tunnel_kern.o \
	test_get_stack_rawtp.o test_sockmap_kern.o test_sockhash_kern.o \
	test_lwt_seg6local.o sendmsg4_prog.o sendmsg6_prog.o test_lirc_mode2_kern.o \
	get_cgroup_id_kern.o socket_cookie_prog.o test_select_reuseport_kern.o \
	test_skb_cgroup_id_kern.o bpf_flow.o

# Order correspond to 'make run_tests' order
TEST_PROGS := test_kmod.sh \
	test_libbpf.sh \
	test_xdp_redirect.sh \
	test_xdp_meta.sh \
	test_offload.py \
	test_sock_addr.sh \
	test_tunnel.sh \
	test_lwt_seg6local.sh \
	test_lirc_mode2.sh \
	test_skb_cgroup_id.sh

# Compile but not part of 'make run_tests'
TEST_GEN_PROGS_EXTENDED = test_libbpf_open test_sock_addr test_skb_cgroup_id_user

include ../lib.mk

BPFOBJ := $(OUTPUT)/libbpf.a

$(TEST_GEN_PROGS): $(BPFOBJ)

$(TEST_GEN_PROGS_EXTENDED): $(OUTPUT)/libbpf.a

$(OUTPUT)/test_dev_cgroup: cgroup_helpers.c
$(OUTPUT)/test_skb_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_sock: cgroup_helpers.c
$(OUTPUT)/test_sock_addr: cgroup_helpers.c
$(OUTPUT)/test_socket_cookie: cgroup_helpers.c
$(OUTPUT)/test_sockmap: cgroup_helpers.c
$(OUTPUT)/test_tcpbpf_user: cgroup_helpers.c
$(OUTPUT)/test_progs: trace_helpers.c
$(OUTPUT)/get_cgroup_id_user: cgroup_helpers.c
$(OUTPUT)/test_cgroup_storage: cgroup_helpers.c

.PHONY: force

# force a rebuild of BPFOBJ when its dependencies are updated
force:

$(BPFOBJ): force
	$(MAKE) -C $(BPFDIR) OUTPUT=$(OUTPUT)/

CLANG ?= clang
LLC   ?= llc
LLVM_OBJCOPY ?= llvm-objcopy
BTF_PAHOLE ?= pahole

PROBE := $(shell $(LLC) -march=bpf -mcpu=probe -filetype=null /dev/null 2>&1)

# Let newer LLVM versions transparently probe the kernel for availability
# of full BPF instruction set.
ifeq ($(PROBE),)
  CPU ?= probe
else
  CPU ?= generic
endif

# Get Clang's default includes on this system, as opposed to those seen by
# '-target bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
CLANG_SYS_INCLUDES := $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

CLANG_FLAGS = -I. -I./include/uapi -I../../../include/uapi \
	      $(CLANG_SYS_INCLUDES) \
	      -Wno-compare-distinct-pointer-types

$(OUTPUT)/test_l4lb_noinline.o: CLANG_FLAGS += -fno-inline
$(OUTPUT)/test_xdp_noinline.o: CLANG_FLAGS += -fno-inline

BTF_LLC_PROBE := $(shell $(LLC) -march=bpf -mattr=help 2>&1 | grep dwarfris)
BTF_PAHOLE_PROBE := $(shell $(BTF_PAHOLE) --help 2>&1 | grep BTF)
BTF_OBJCOPY_PROBE := $(shell $(LLVM_OBJCOPY) --help 2>&1 | grep -i 'usage.*llvm')

ifneq ($(BTF_LLC_PROBE),)
ifneq ($(BTF_PAHOLE_PROBE),)
ifneq ($(BTF_OBJCOPY_PROBE),)
	CLANG_FLAGS += -g
	LLC_FLAGS += -mattr=dwarfris
	DWARF2BTF = y
endif
endif
endif

$(OUTPUT)/%.o: %.c
	$(CLANG) $(CLANG_FLAGS) \
		 -O2 -target bpf -emit-llvm -c $< -o - |      \
	$(LLC) -march=bpf -mcpu=$(CPU) $(LLC_FLAGS) -filetype=obj -o $@
ifeq ($(DWARF2BTF),y)
	$(BTF_PAHOLE) -J $@
endif

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS)
