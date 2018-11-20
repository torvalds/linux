# SPDX-License-Identifier: GPL-2.0

BPF_SAMPLES_PATH ?= $(abspath $(srctree)/$(src))
TOOLS_PATH := $(BPF_SAMPLES_PATH)/../../tools

# List of programs to build
hostprogs-y := test_lru_dist
hostprogs-y += sock_example
hostprogs-y += fds_example
hostprogs-y += sockex1
hostprogs-y += sockex2
hostprogs-y += sockex3
hostprogs-y += tracex1
hostprogs-y += tracex2
hostprogs-y += tracex3
hostprogs-y += tracex4
hostprogs-y += tracex5
hostprogs-y += tracex6
hostprogs-y += tracex7
hostprogs-y += test_probe_write_user
hostprogs-y += trace_output
hostprogs-y += lathist
hostprogs-y += offwaketime
hostprogs-y += spintest
hostprogs-y += map_perf_test
hostprogs-y += test_overhead
hostprogs-y += test_cgrp2_array_pin
hostprogs-y += test_cgrp2_attach
hostprogs-y += test_cgrp2_attach2
hostprogs-y += test_cgrp2_sock
hostprogs-y += test_cgrp2_sock2
hostprogs-y += xdp1
hostprogs-y += xdp2
hostprogs-y += xdp_router_ipv4
hostprogs-y += test_current_task_under_cgroup
hostprogs-y += trace_event
hostprogs-y += sampleip
hostprogs-y += tc_l2_redirect
hostprogs-y += lwt_len_hist
hostprogs-y += xdp_tx_iptunnel
hostprogs-y += test_map_in_map
hostprogs-y += per_socket_stats_example
hostprogs-y += load_sock_ops
hostprogs-y += xdp_redirect
hostprogs-y += xdp_redirect_map
hostprogs-y += xdp_redirect_cpu
hostprogs-y += xdp_monitor
hostprogs-y += xdp_rxq_info
hostprogs-y += syscall_tp
hostprogs-y += cpustat
hostprogs-y += xdp_adjust_tail
hostprogs-y += xdpsock
hostprogs-y += xdp_fwd
hostprogs-y += task_fd_query
hostprogs-y += xdp_sample_pkts

# Libbpf dependencies
LIBBPF = $(TOOLS_PATH)/lib/bpf/libbpf.a

CGROUP_HELPERS := ../../tools/testing/selftests/bpf/cgroup_helpers.o
TRACE_HELPERS := ../../tools/testing/selftests/bpf/trace_helpers.o

fds_example-objs := bpf_load.o fds_example.o
sockex1-objs := bpf_load.o sockex1_user.o
sockex2-objs := bpf_load.o sockex2_user.o
sockex3-objs := bpf_load.o sockex3_user.o
tracex1-objs := bpf_load.o tracex1_user.o
tracex2-objs := bpf_load.o tracex2_user.o
tracex3-objs := bpf_load.o tracex3_user.o
tracex4-objs := bpf_load.o tracex4_user.o
tracex5-objs := bpf_load.o tracex5_user.o
tracex6-objs := bpf_load.o tracex6_user.o
tracex7-objs := bpf_load.o tracex7_user.o
load_sock_ops-objs := bpf_load.o load_sock_ops.o
test_probe_write_user-objs := bpf_load.o test_probe_write_user_user.o
trace_output-objs := bpf_load.o trace_output_user.o $(TRACE_HELPERS)
lathist-objs := bpf_load.o lathist_user.o
offwaketime-objs := bpf_load.o offwaketime_user.o $(TRACE_HELPERS)
spintest-objs := bpf_load.o spintest_user.o $(TRACE_HELPERS)
map_perf_test-objs := bpf_load.o map_perf_test_user.o
test_overhead-objs := bpf_load.o test_overhead_user.o
test_cgrp2_array_pin-objs := test_cgrp2_array_pin.o
test_cgrp2_attach-objs := test_cgrp2_attach.o
test_cgrp2_attach2-objs := test_cgrp2_attach2.o $(CGROUP_HELPERS)
test_cgrp2_sock-objs := test_cgrp2_sock.o
test_cgrp2_sock2-objs := bpf_load.o test_cgrp2_sock2.o
xdp1-objs := xdp1_user.o
# reuse xdp1 source intentionally
xdp2-objs := xdp1_user.o
xdp_router_ipv4-objs := bpf_load.o xdp_router_ipv4_user.o
test_current_task_under_cgroup-objs := bpf_load.o $(CGROUP_HELPERS) \
				       test_current_task_under_cgroup_user.o
trace_event-objs := bpf_load.o trace_event_user.o $(TRACE_HELPERS)
sampleip-objs := bpf_load.o sampleip_user.o $(TRACE_HELPERS)
tc_l2_redirect-objs := bpf_load.o tc_l2_redirect_user.o
lwt_len_hist-objs := bpf_load.o lwt_len_hist_user.o
xdp_tx_iptunnel-objs := bpf_load.o xdp_tx_iptunnel_user.o
test_map_in_map-objs := bpf_load.o test_map_in_map_user.o
per_socket_stats_example-objs := cookie_uid_helper_example.o
xdp_redirect-objs := bpf_load.o xdp_redirect_user.o
xdp_redirect_map-objs := bpf_load.o xdp_redirect_map_user.o
xdp_redirect_cpu-objs := bpf_load.o xdp_redirect_cpu_user.o
xdp_monitor-objs := bpf_load.o xdp_monitor_user.o
xdp_rxq_info-objs := xdp_rxq_info_user.o
syscall_tp-objs := bpf_load.o syscall_tp_user.o
cpustat-objs := bpf_load.o cpustat_user.o
xdp_adjust_tail-objs := xdp_adjust_tail_user.o
xdpsock-objs := xdpsock_user.o
xdp_fwd-objs := xdp_fwd_user.o
task_fd_query-objs := bpf_load.o task_fd_query_user.o $(TRACE_HELPERS)
xdp_sample_pkts-objs := xdp_sample_pkts_user.o $(TRACE_HELPERS)

# Tell kbuild to always build the programs
always := $(hostprogs-y)
always += sockex1_kern.o
always += sockex2_kern.o
always += sockex3_kern.o
always += tracex1_kern.o
always += tracex2_kern.o
always += tracex3_kern.o
always += tracex4_kern.o
always += tracex5_kern.o
always += tracex6_kern.o
always += tracex7_kern.o
always += sock_flags_kern.o
always += test_probe_write_user_kern.o
always += trace_output_kern.o
always += tcbpf1_kern.o
always += tc_l2_redirect_kern.o
always += lathist_kern.o
always += offwaketime_kern.o
always += spintest_kern.o
always += map_perf_test_kern.o
always += test_overhead_tp_kern.o
always += test_overhead_raw_tp_kern.o
always += test_overhead_kprobe_kern.o
always += parse_varlen.o parse_simple.o parse_ldabs.o
always += test_cgrp2_tc_kern.o
always += xdp1_kern.o
always += xdp2_kern.o
always += xdp_router_ipv4_kern.o
always += test_current_task_under_cgroup_kern.o
always += trace_event_kern.o
always += sampleip_kern.o
always += lwt_len_hist_kern.o
always += xdp_tx_iptunnel_kern.o
always += test_map_in_map_kern.o
always += cookie_uid_helper_example.o
always += tcp_synrto_kern.o
always += tcp_rwnd_kern.o
always += tcp_bufs_kern.o
always += tcp_cong_kern.o
always += tcp_iw_kern.o
always += tcp_clamp_kern.o
always += tcp_basertt_kern.o
always += tcp_tos_reflect_kern.o
always += xdp_redirect_kern.o
always += xdp_redirect_map_kern.o
always += xdp_redirect_cpu_kern.o
always += xdp_monitor_kern.o
always += xdp_rxq_info_kern.o
always += xdp2skb_meta_kern.o
always += syscall_tp_kern.o
always += cpustat_kern.o
always += xdp_adjust_tail_kern.o
always += xdpsock_kern.o
always += xdp_fwd_kern.o
always += task_fd_query_kern.o
always += xdp_sample_pkts_kern.o

KBUILD_HOSTCFLAGS += -I$(objtree)/usr/include
KBUILD_HOSTCFLAGS += -I$(srctree)/tools/lib/
KBUILD_HOSTCFLAGS += -I$(srctree)/tools/testing/selftests/bpf/
KBUILD_HOSTCFLAGS += -I$(srctree)/tools/lib/ -I$(srctree)/tools/include
KBUILD_HOSTCFLAGS += -I$(srctree)/tools/perf

HOSTCFLAGS_bpf_load.o += -I$(objtree)/usr/include -Wno-unused-variable
HOSTCFLAGS_trace_helpers.o += -I$(srctree)/tools/lib/bpf/

HOSTCFLAGS_trace_output_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_offwaketime_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_spintest_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_trace_event_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_sampleip_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_task_fd_query_user.o += -I$(srctree)/tools/lib/bpf/
HOSTCFLAGS_xdp_sample_pkts_user.o += -I$(srctree)/tools/lib/bpf/

KBUILD_HOSTLDLIBS		+= $(LIBBPF) -lelf
HOSTLDLIBS_tracex4		+= -lrt
HOSTLDLIBS_trace_output	+= -lrt
HOSTLDLIBS_map_perf_test	+= -lrt
HOSTLDLIBS_test_overhead	+= -lrt
HOSTLDLIBS_xdpsock		+= -pthread

# Allows pointing LLC/CLANG to a LLVM backend with bpf support, redefine on cmdline:
#  make samples/bpf/ LLC=~/git/llvm/build/bin/llc CLANG=~/git/llvm/build/bin/clang
LLC ?= llc
CLANG ?= clang
LLVM_OBJCOPY ?= llvm-objcopy
BTF_PAHOLE ?= pahole

# Detect that we're cross compiling and use the cross compiler
ifdef CROSS_COMPILE
HOSTCC = $(CROSS_COMPILE)gcc
CLANG_ARCH_ARGS = -target $(ARCH)
endif

BTF_LLC_PROBE := $(shell $(LLC) -march=bpf -mattr=help 2>&1 | grep dwarfris)
BTF_PAHOLE_PROBE := $(shell $(BTF_PAHOLE) --help 2>&1 | grep BTF)
BTF_OBJCOPY_PROBE := $(shell $(LLVM_OBJCOPY) --help 2>&1 | grep -i 'usage.*llvm')

ifneq ($(and $(BTF_LLC_PROBE),$(BTF_PAHOLE_PROBE),$(BTF_OBJCOPY_PROBE)),)
	EXTRA_CFLAGS += -g
	LLC_FLAGS += -mattr=dwarfris
	DWARF2BTF = y
endif

# Trick to allow make to be run from this directory
all:
	$(MAKE) -C ../../ $(CURDIR)/ BPF_SAMPLES_PATH=$(CURDIR)

clean:
	$(MAKE) -C ../../ M=$(CURDIR) clean
	@rm -f *~

$(LIBBPF): FORCE
# Fix up variables inherited from Kbuild that tools/ build system won't like
	$(MAKE) -C $(dir $@) RM='rm -rf' LDFLAGS= srctree=$(BPF_SAMPLES_PATH)/../../ O=

$(obj)/syscall_nrs.s:	$(src)/syscall_nrs.c
	$(call if_changed_dep,cc_s_c)

$(obj)/syscall_nrs.h:	$(obj)/syscall_nrs.s FORCE
	$(call filechk,offsets,__SYSCALL_NRS_H__)

clean-files += syscall_nrs.h

FORCE:


# Verify LLVM compiler tools are available and bpf target is supported by llc
.PHONY: verify_cmds verify_target_bpf $(CLANG) $(LLC)

verify_cmds: $(CLANG) $(LLC)
	@for TOOL in $^ ; do \
		if ! (which -- "$${TOOL}" > /dev/null 2>&1); then \
			echo "*** ERROR: Cannot find LLVM tool $${TOOL}" ;\
			exit 1; \
		else true; fi; \
	done

verify_target_bpf: verify_cmds
	@if ! (${LLC} -march=bpf -mattr=help > /dev/null 2>&1); then \
		echo "*** ERROR: LLVM (${LLC}) does not support 'bpf' target" ;\
		echo "   NOTICE: LLVM version >= 3.7.1 required" ;\
		exit 2; \
	else true; fi

$(BPF_SAMPLES_PATH)/*.c: verify_target_bpf $(LIBBPF)
$(src)/*.c: verify_target_bpf $(LIBBPF)

$(obj)/tracex5_kern.o: $(obj)/syscall_nrs.h

# asm/sysreg.h - inline assembly used by it is incompatible with llvm.
# But, there is no easy way to fix it, so just exclude it since it is
# useless for BPF samples.
$(obj)/%.o: $(src)/%.c
	@echo "  CLANG-bpf " $@
	$(Q)$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) -I$(obj) \
		-I$(srctree)/tools/testing/selftests/bpf/ \
		-D__KERNEL__ -D__BPF_TRACING__ -Wno-unused-value -Wno-pointer-sign \
		-D__TARGET_ARCH_$(ARCH) -Wno-compare-distinct-pointer-types \
		-Wno-gnu-variable-sized-type-not-at-end \
		-Wno-address-of-packed-member -Wno-tautological-compare \
		-Wno-unknown-warning-option $(CLANG_ARCH_ARGS) \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf $(LLC_FLAGS) -filetype=obj -o $@
ifeq ($(DWARF2BTF),y)
	$(BTF_PAHOLE) -J $@
endif
