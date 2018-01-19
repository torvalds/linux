# SPDX-License-Identifier: GPL-2.0
# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

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
hostprogs-y += xdp_monitor
hostprogs-y += syscall_tp

# Libbpf dependencies
LIBBPF := ../../tools/lib/bpf/bpf.o

test_lru_dist-objs := test_lru_dist.o $(LIBBPF)
sock_example-objs := sock_example.o $(LIBBPF)
fds_example-objs := bpf_load.o $(LIBBPF) fds_example.o
sockex1-objs := bpf_load.o $(LIBBPF) sockex1_user.o
sockex2-objs := bpf_load.o $(LIBBPF) sockex2_user.o
sockex3-objs := bpf_load.o $(LIBBPF) sockex3_user.o
tracex1-objs := bpf_load.o $(LIBBPF) tracex1_user.o
tracex2-objs := bpf_load.o $(LIBBPF) tracex2_user.o
tracex3-objs := bpf_load.o $(LIBBPF) tracex3_user.o
tracex4-objs := bpf_load.o $(LIBBPF) tracex4_user.o
tracex5-objs := bpf_load.o $(LIBBPF) tracex5_user.o
tracex6-objs := bpf_load.o $(LIBBPF) tracex6_user.o
load_sock_ops-objs := bpf_load.o $(LIBBPF) load_sock_ops.o
test_probe_write_user-objs := bpf_load.o $(LIBBPF) test_probe_write_user_user.o
trace_output-objs := bpf_load.o $(LIBBPF) trace_output_user.o
lathist-objs := bpf_load.o $(LIBBPF) lathist_user.o
offwaketime-objs := bpf_load.o $(LIBBPF) offwaketime_user.o
spintest-objs := bpf_load.o $(LIBBPF) spintest_user.o
map_perf_test-objs := bpf_load.o $(LIBBPF) map_perf_test_user.o
test_overhead-objs := bpf_load.o $(LIBBPF) test_overhead_user.o
test_cgrp2_array_pin-objs := $(LIBBPF) test_cgrp2_array_pin.o
test_cgrp2_attach-objs := $(LIBBPF) test_cgrp2_attach.o
test_cgrp2_attach2-objs := $(LIBBPF) test_cgrp2_attach2.o cgroup_helpers.o
test_cgrp2_sock-objs := $(LIBBPF) test_cgrp2_sock.o
test_cgrp2_sock2-objs := bpf_load.o $(LIBBPF) test_cgrp2_sock2.o
xdp1-objs := bpf_load.o $(LIBBPF) xdp1_user.o
# reuse xdp1 source intentionally
xdp2-objs := bpf_load.o $(LIBBPF) xdp1_user.o
test_current_task_under_cgroup-objs := bpf_load.o $(LIBBPF) cgroup_helpers.o \
				       test_current_task_under_cgroup_user.o
trace_event-objs := bpf_load.o $(LIBBPF) trace_event_user.o
sampleip-objs := bpf_load.o $(LIBBPF) sampleip_user.o
tc_l2_redirect-objs := bpf_load.o $(LIBBPF) tc_l2_redirect_user.o
lwt_len_hist-objs := bpf_load.o $(LIBBPF) lwt_len_hist_user.o
xdp_tx_iptunnel-objs := bpf_load.o $(LIBBPF) xdp_tx_iptunnel_user.o
test_map_in_map-objs := bpf_load.o $(LIBBPF) test_map_in_map_user.o
per_socket_stats_example-objs := $(LIBBPF) cookie_uid_helper_example.o
xdp_redirect-objs := bpf_load.o $(LIBBPF) xdp_redirect_user.o
xdp_redirect_map-objs := bpf_load.o $(LIBBPF) xdp_redirect_map_user.o
xdp_monitor-objs := bpf_load.o $(LIBBPF) xdp_monitor_user.o
syscall_tp-objs := bpf_load.o $(LIBBPF) syscall_tp_user.o

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
always += sock_flags_kern.o
always += test_probe_write_user_kern.o
always += trace_output_kern.o
always += tcbpf1_kern.o
always += tcbpf2_kern.o
always += tc_l2_redirect_kern.o
always += lathist_kern.o
always += offwaketime_kern.o
always += spintest_kern.o
always += map_perf_test_kern.o
always += test_overhead_tp_kern.o
always += test_overhead_kprobe_kern.o
always += parse_varlen.o parse_simple.o parse_ldabs.o
always += test_cgrp2_tc_kern.o
always += xdp1_kern.o
always += xdp2_kern.o
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
always += xdp_redirect_kern.o
always += xdp_redirect_map_kern.o
always += xdp_monitor_kern.o
always += syscall_tp_kern.o

HOSTCFLAGS += -I$(objtree)/usr/include
HOSTCFLAGS += -I$(srctree)/tools/lib/
HOSTCFLAGS += -I$(srctree)/tools/testing/selftests/bpf/
HOSTCFLAGS += -I$(srctree)/tools/lib/ -I$(srctree)/tools/include
HOSTCFLAGS += -I$(srctree)/tools/perf

HOSTCFLAGS_bpf_load.o += -I$(objtree)/usr/include -Wno-unused-variable
HOSTLOADLIBES_fds_example += -lelf
HOSTLOADLIBES_sockex1 += -lelf
HOSTLOADLIBES_sockex2 += -lelf
HOSTLOADLIBES_sockex3 += -lelf
HOSTLOADLIBES_tracex1 += -lelf
HOSTLOADLIBES_tracex2 += -lelf
HOSTLOADLIBES_tracex3 += -lelf
HOSTLOADLIBES_tracex4 += -lelf -lrt
HOSTLOADLIBES_tracex5 += -lelf
HOSTLOADLIBES_tracex6 += -lelf
HOSTLOADLIBES_test_cgrp2_sock2 += -lelf
HOSTLOADLIBES_load_sock_ops += -lelf
HOSTLOADLIBES_test_probe_write_user += -lelf
HOSTLOADLIBES_trace_output += -lelf -lrt
HOSTLOADLIBES_lathist += -lelf
HOSTLOADLIBES_offwaketime += -lelf
HOSTLOADLIBES_spintest += -lelf
HOSTLOADLIBES_map_perf_test += -lelf -lrt
HOSTLOADLIBES_test_overhead += -lelf -lrt
HOSTLOADLIBES_xdp1 += -lelf
HOSTLOADLIBES_xdp2 += -lelf
HOSTLOADLIBES_test_current_task_under_cgroup += -lelf
HOSTLOADLIBES_trace_event += -lelf
HOSTLOADLIBES_sampleip += -lelf
HOSTLOADLIBES_tc_l2_redirect += -l elf
HOSTLOADLIBES_lwt_len_hist += -l elf
HOSTLOADLIBES_xdp_tx_iptunnel += -lelf
HOSTLOADLIBES_test_map_in_map += -lelf
HOSTLOADLIBES_xdp_redirect += -lelf
HOSTLOADLIBES_xdp_redirect_map += -lelf
HOSTLOADLIBES_xdp_monitor += -lelf
HOSTLOADLIBES_syscall_tp += -lelf

# Allows pointing LLC/CLANG to a LLVM backend with bpf support, redefine on cmdline:
#  make samples/bpf/ LLC=~/git/llvm/build/bin/llc CLANG=~/git/llvm/build/bin/clang
LLC ?= llc
CLANG ?= clang

# Trick to allow make to be run from this directory
all:
	$(MAKE) -C ../../ $(CURDIR)/

clean:
	$(MAKE) -C ../../ M=$(CURDIR) clean
	@rm -f *~

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

$(src)/*.c: verify_target_bpf

$(obj)/tracex5_kern.o: $(obj)/syscall_nrs.h

# asm/sysreg.h - inline assembly used by it is incompatible with llvm.
# But, there is no easy way to fix it, so just exclude it since it is
# useless for BPF samples.
$(obj)/%.o: $(src)/%.c
	$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) -I$(obj) \
		-I$(srctree)/tools/testing/selftests/bpf/ \
		-D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign \
		-Wno-compare-distinct-pointer-types \
		-Wno-gnu-variable-sized-type-not-at-end \
		-Wno-address-of-packed-member -Wno-tautological-compare \
		-Wno-unknown-warning-option \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=obj -o $@
