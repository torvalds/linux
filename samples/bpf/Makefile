# SPDX-License-Identifier: GPL-2.0

BPF_SAMPLES_PATH ?= $(abspath $(srctree)/$(src))
TOOLS_PATH := $(BPF_SAMPLES_PATH)/../../tools

# List of programs to build
tprogs-y := test_lru_dist
tprogs-y += sock_example
tprogs-y += fds_example
tprogs-y += sockex1
tprogs-y += sockex2
tprogs-y += sockex3
tprogs-y += tracex1
tprogs-y += tracex2
tprogs-y += tracex3
tprogs-y += tracex4
tprogs-y += tracex5
tprogs-y += tracex6
tprogs-y += tracex7
tprogs-y += test_probe_write_user
tprogs-y += trace_output
tprogs-y += lathist
tprogs-y += offwaketime
tprogs-y += spintest
tprogs-y += map_perf_test
tprogs-y += test_overhead
tprogs-y += test_cgrp2_array_pin
tprogs-y += test_cgrp2_attach
tprogs-y += test_cgrp2_sock
tprogs-y += test_cgrp2_sock2
tprogs-y += xdp1
tprogs-y += xdp2
tprogs-y += xdp_router_ipv4
tprogs-y += test_current_task_under_cgroup
tprogs-y += trace_event
tprogs-y += sampleip
tprogs-y += tc_l2_redirect
tprogs-y += lwt_len_hist
tprogs-y += xdp_tx_iptunnel
tprogs-y += test_map_in_map
tprogs-y += per_socket_stats_example
tprogs-y += xdp_rxq_info
tprogs-y += syscall_tp
tprogs-y += cpustat
tprogs-y += xdp_adjust_tail
tprogs-y += xdpsock
tprogs-y += xdpsock_ctrl_proc
tprogs-y += xsk_fwd
tprogs-y += xdp_fwd
tprogs-y += task_fd_query
tprogs-y += xdp_sample_pkts
tprogs-y += ibumad
tprogs-y += hbm

tprogs-y += xdp_redirect_cpu
tprogs-y += xdp_redirect_map_multi
tprogs-y += xdp_redirect_map
tprogs-y += xdp_redirect
tprogs-y += xdp_monitor

# Libbpf dependencies
LIBBPF = $(TOOLS_PATH)/lib/bpf/libbpf.a

CGROUP_HELPERS := ../../tools/testing/selftests/bpf/cgroup_helpers.o
TRACE_HELPERS := ../../tools/testing/selftests/bpf/trace_helpers.o
XDP_SAMPLE := xdp_sample_user.o

fds_example-objs := fds_example.o
sockex1-objs := sockex1_user.o
sockex2-objs := sockex2_user.o
sockex3-objs := sockex3_user.o
tracex1-objs := tracex1_user.o $(TRACE_HELPERS)
tracex2-objs := tracex2_user.o
tracex3-objs := tracex3_user.o
tracex4-objs := tracex4_user.o
tracex5-objs := tracex5_user.o $(TRACE_HELPERS)
tracex6-objs := tracex6_user.o
tracex7-objs := tracex7_user.o
test_probe_write_user-objs := test_probe_write_user_user.o
trace_output-objs := trace_output_user.o
lathist-objs := lathist_user.o
offwaketime-objs := offwaketime_user.o $(TRACE_HELPERS)
spintest-objs := spintest_user.o $(TRACE_HELPERS)
map_perf_test-objs := map_perf_test_user.o
test_overhead-objs := test_overhead_user.o
test_cgrp2_array_pin-objs := test_cgrp2_array_pin.o
test_cgrp2_attach-objs := test_cgrp2_attach.o
test_cgrp2_sock-objs := test_cgrp2_sock.o
test_cgrp2_sock2-objs := test_cgrp2_sock2.o
xdp1-objs := xdp1_user.o
# reuse xdp1 source intentionally
xdp2-objs := xdp1_user.o
xdp_router_ipv4-objs := xdp_router_ipv4_user.o
test_current_task_under_cgroup-objs := $(CGROUP_HELPERS) \
				       test_current_task_under_cgroup_user.o
trace_event-objs := trace_event_user.o $(TRACE_HELPERS)
sampleip-objs := sampleip_user.o $(TRACE_HELPERS)
tc_l2_redirect-objs := tc_l2_redirect_user.o
lwt_len_hist-objs := lwt_len_hist_user.o
xdp_tx_iptunnel-objs := xdp_tx_iptunnel_user.o
test_map_in_map-objs := test_map_in_map_user.o
per_socket_stats_example-objs := cookie_uid_helper_example.o
xdp_rxq_info-objs := xdp_rxq_info_user.o
syscall_tp-objs := syscall_tp_user.o
cpustat-objs := cpustat_user.o
xdp_adjust_tail-objs := xdp_adjust_tail_user.o
xdpsock-objs := xdpsock_user.o
xdpsock_ctrl_proc-objs := xdpsock_ctrl_proc.o
xsk_fwd-objs := xsk_fwd.o
xdp_fwd-objs := xdp_fwd_user.o
task_fd_query-objs := task_fd_query_user.o $(TRACE_HELPERS)
xdp_sample_pkts-objs := xdp_sample_pkts_user.o
ibumad-objs := ibumad_user.o
hbm-objs := hbm.o $(CGROUP_HELPERS)

xdp_redirect_map_multi-objs := xdp_redirect_map_multi_user.o $(XDP_SAMPLE)
xdp_redirect_cpu-objs := xdp_redirect_cpu_user.o $(XDP_SAMPLE)
xdp_redirect_map-objs := xdp_redirect_map_user.o $(XDP_SAMPLE)
xdp_redirect-objs := xdp_redirect_user.o $(XDP_SAMPLE)
xdp_monitor-objs := xdp_monitor_user.o $(XDP_SAMPLE)

# Tell kbuild to always build the programs
always-y := $(tprogs-y)
always-y += sockex1_kern.o
always-y += sockex2_kern.o
always-y += sockex3_kern.o
always-y += tracex1_kern.o
always-y += tracex2_kern.o
always-y += tracex3_kern.o
always-y += tracex4_kern.o
always-y += tracex5_kern.o
always-y += tracex6_kern.o
always-y += tracex7_kern.o
always-y += sock_flags_kern.o
always-y += test_probe_write_user_kern.o
always-y += trace_output_kern.o
always-y += tcbpf1_kern.o
always-y += tc_l2_redirect_kern.o
always-y += lathist_kern.o
always-y += offwaketime_kern.o
always-y += spintest_kern.o
always-y += map_perf_test_kern.o
always-y += test_overhead_tp_kern.o
always-y += test_overhead_raw_tp_kern.o
always-y += test_overhead_kprobe_kern.o
always-y += parse_varlen.o parse_simple.o parse_ldabs.o
always-y += test_cgrp2_tc_kern.o
always-y += xdp1_kern.o
always-y += xdp2_kern.o
always-y += xdp_router_ipv4_kern.o
always-y += test_current_task_under_cgroup_kern.o
always-y += trace_event_kern.o
always-y += sampleip_kern.o
always-y += lwt_len_hist_kern.o
always-y += xdp_tx_iptunnel_kern.o
always-y += test_map_in_map_kern.o
always-y += tcp_synrto_kern.o
always-y += tcp_rwnd_kern.o
always-y += tcp_bufs_kern.o
always-y += tcp_cong_kern.o
always-y += tcp_iw_kern.o
always-y += tcp_clamp_kern.o
always-y += tcp_basertt_kern.o
always-y += tcp_tos_reflect_kern.o
always-y += tcp_dumpstats_kern.o
always-y += xdp_rxq_info_kern.o
always-y += xdp2skb_meta_kern.o
always-y += syscall_tp_kern.o
always-y += cpustat_kern.o
always-y += xdp_adjust_tail_kern.o
always-y += xdp_fwd_kern.o
always-y += task_fd_query_kern.o
always-y += xdp_sample_pkts_kern.o
always-y += ibumad_kern.o
always-y += hbm_out_kern.o
always-y += hbm_edt_kern.o
always-y += xdpsock_kern.o

ifeq ($(ARCH), arm)
# Strip all except -D__LINUX_ARM_ARCH__ option needed to handle linux
# headers when arm instruction set identification is requested.
ARM_ARCH_SELECTOR := $(filter -D__LINUX_ARM_ARCH__%, $(KBUILD_CFLAGS))
BPF_EXTRA_CFLAGS := $(ARM_ARCH_SELECTOR)
TPROGS_CFLAGS += $(ARM_ARCH_SELECTOR)
endif

ifeq ($(ARCH), mips)
TPROGS_CFLAGS += -D__SANE_USERSPACE_TYPES__
ifdef CONFIG_MACH_LOONGSON64
BPF_EXTRA_CFLAGS += -I$(srctree)/arch/mips/include/asm/mach-loongson64
BPF_EXTRA_CFLAGS += -I$(srctree)/arch/mips/include/asm/mach-generic
endif
endif

TPROGS_CFLAGS += -Wall -O2
TPROGS_CFLAGS += -Wmissing-prototypes
TPROGS_CFLAGS += -Wstrict-prototypes

TPROGS_CFLAGS += -I$(objtree)/usr/include
TPROGS_CFLAGS += -I$(srctree)/tools/testing/selftests/bpf/
TPROGS_CFLAGS += -I$(srctree)/tools/lib/
TPROGS_CFLAGS += -I$(srctree)/tools/include
TPROGS_CFLAGS += -I$(srctree)/tools/perf
TPROGS_CFLAGS += -DHAVE_ATTR_TEST=0

ifdef SYSROOT
TPROGS_CFLAGS += --sysroot=$(SYSROOT)
TPROGS_LDFLAGS := -L$(SYSROOT)/usr/lib
endif

TPROGS_LDLIBS			+= $(LIBBPF) -lelf -lz
TPROGLDLIBS_tracex4		+= -lrt
TPROGLDLIBS_trace_output	+= -lrt
TPROGLDLIBS_map_perf_test	+= -lrt
TPROGLDLIBS_test_overhead	+= -lrt
TPROGLDLIBS_xdpsock		+= -pthread -lcap
TPROGLDLIBS_xsk_fwd		+= -pthread

# Allows pointing LLC/CLANG to a LLVM backend with bpf support, redefine on cmdline:
# make M=samples/bpf LLC=~/git/llvm-project/llvm/build/bin/llc CLANG=~/git/llvm-project/llvm/build/bin/clang
LLC ?= llc
CLANG ?= clang
OPT ?= opt
LLVM_DIS ?= llvm-dis
LLVM_OBJCOPY ?= llvm-objcopy
BTF_PAHOLE ?= pahole

# Detect that we're cross compiling and use the cross compiler
ifdef CROSS_COMPILE
CLANG_ARCH_ARGS = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

# Don't evaluate probes and warnings if we need to run make recursively
ifneq ($(src),)
HDR_PROBE := $(shell printf "\#include <linux/types.h>\n struct list_head { int a; }; int main() { return 0; }" | \
	$(CC) $(TPROGS_CFLAGS) $(TPROGS_LDFLAGS) -x c - \
	-o /dev/null 2>/dev/null && echo okay)

ifeq ($(HDR_PROBE),)
$(warning WARNING: Detected possible issues with include path.)
$(warning WARNING: Please install kernel headers locally (make headers_install).)
endif

BTF_LLC_PROBE := $(shell $(LLC) -march=bpf -mattr=help 2>&1 | grep dwarfris)
BTF_PAHOLE_PROBE := $(shell $(BTF_PAHOLE) --help 2>&1 | grep BTF)
BTF_OBJCOPY_PROBE := $(shell $(LLVM_OBJCOPY) --help 2>&1 | grep -i 'usage.*llvm')
BTF_LLVM_PROBE := $(shell echo "int main() { return 0; }" | \
			  $(CLANG) -target bpf -O2 -g -c -x c - -o ./llvm_btf_verify.o; \
			  readelf -S ./llvm_btf_verify.o | grep BTF; \
			  /bin/rm -f ./llvm_btf_verify.o)

BPF_EXTRA_CFLAGS += -fno-stack-protector
ifneq ($(BTF_LLVM_PROBE),)
	BPF_EXTRA_CFLAGS += -g
else
ifneq ($(and $(BTF_LLC_PROBE),$(BTF_PAHOLE_PROBE),$(BTF_OBJCOPY_PROBE)),)
	BPF_EXTRA_CFLAGS += -g
	LLC_FLAGS += -mattr=dwarfris
	DWARF2BTF = y
endif
endif
endif

# Trick to allow make to be run from this directory
all:
	$(MAKE) -C ../../ M=$(CURDIR) BPF_SAMPLES_PATH=$(CURDIR)

clean:
	$(MAKE) -C ../../ M=$(CURDIR) clean
	@find $(CURDIR) -type f -name '*~' -delete

$(LIBBPF): FORCE
# Fix up variables inherited from Kbuild that tools/ build system won't like
	$(MAKE) -C $(dir $@) RM='rm -rf' EXTRA_CFLAGS="$(TPROGS_CFLAGS)" \
		LDFLAGS=$(TPROGS_LDFLAGS) srctree=$(BPF_SAMPLES_PATH)/../../ O=

BPFTOOLDIR := $(TOOLS_PATH)/bpf/bpftool
BPFTOOL := $(BPFTOOLDIR)/bpftool
$(BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile)
	    $(MAKE) -C $(BPFTOOLDIR) srctree=$(BPF_SAMPLES_PATH)/../../

$(obj)/syscall_nrs.h:	$(obj)/syscall_nrs.s FORCE
	$(call filechk,offsets,__SYSCALL_NRS_H__)

targets += syscall_nrs.s
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

$(obj)/xdp_redirect_cpu_user.o: $(obj)/xdp_redirect_cpu.skel.h
$(obj)/xdp_redirect_map_multi_user.o: $(obj)/xdp_redirect_map_multi.skel.h
$(obj)/xdp_redirect_map_user.o: $(obj)/xdp_redirect_map.skel.h
$(obj)/xdp_redirect_user.o: $(obj)/xdp_redirect.skel.h
$(obj)/xdp_monitor_user.o: $(obj)/xdp_monitor.skel.h

$(obj)/tracex5_kern.o: $(obj)/syscall_nrs.h
$(obj)/hbm_out_kern.o: $(src)/hbm.h $(src)/hbm_kern.h
$(obj)/hbm.o: $(src)/hbm.h
$(obj)/hbm_edt_kern.o: $(src)/hbm.h $(src)/hbm_kern.h

-include $(BPF_SAMPLES_PATH)/Makefile.target

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../../vmlinux				\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))

ifeq ($(VMLINUX_BTF),)
$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)")
endif

$(obj)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL)
ifeq ($(VMLINUX_H),)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@
else
	$(Q)cp "$(VMLINUX_H)" $@
endif

clean-files += vmlinux.h

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

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))

$(obj)/xdp_redirect_cpu.bpf.o: $(obj)/xdp_sample.bpf.o
$(obj)/xdp_redirect_map_multi.bpf.o: $(obj)/xdp_sample.bpf.o
$(obj)/xdp_redirect_map.bpf.o: $(obj)/xdp_sample.bpf.o
$(obj)/xdp_redirect.bpf.o: $(obj)/xdp_sample.bpf.o
$(obj)/xdp_monitor.bpf.o: $(obj)/xdp_sample.bpf.o

$(obj)/%.bpf.o: $(src)/%.bpf.c $(obj)/vmlinux.h $(src)/xdp_sample.bpf.h $(src)/xdp_sample_shared.h
	@echo "  CLANG-BPF " $@
	$(Q)$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(SRCARCH) \
		-Wno-compare-distinct-pointer-types -I$(srctree)/include \
		-I$(srctree)/samples/bpf -I$(srctree)/tools/include \
		-I$(srctree)/tools/lib $(CLANG_SYS_INCLUDES) \
		-c $(filter %.bpf.c,$^) -o $@

LINKED_SKELS := xdp_redirect_cpu.skel.h xdp_redirect_map_multi.skel.h \
		xdp_redirect_map.skel.h xdp_redirect.skel.h xdp_monitor.skel.h
clean-files += $(LINKED_SKELS)

xdp_redirect_cpu.skel.h-deps := xdp_redirect_cpu.bpf.o xdp_sample.bpf.o
xdp_redirect_map_multi.skel.h-deps := xdp_redirect_map_multi.bpf.o xdp_sample.bpf.o
xdp_redirect_map.skel.h-deps := xdp_redirect_map.bpf.o xdp_sample.bpf.o
xdp_redirect.skel.h-deps := xdp_redirect.bpf.o xdp_sample.bpf.o
xdp_monitor.skel.h-deps := xdp_monitor.bpf.o xdp_sample.bpf.o

LINKED_BPF_SRCS := $(patsubst %.bpf.o,%.bpf.c,$(foreach skel,$(LINKED_SKELS),$($(skel)-deps)))

BPF_SRCS_LINKED := $(notdir $(wildcard $(src)/*.bpf.c))
BPF_OBJS_LINKED := $(patsubst %.bpf.c,$(obj)/%.bpf.o, $(BPF_SRCS_LINKED))
BPF_SKELS_LINKED := $(addprefix $(obj)/,$(LINKED_SKELS))

$(BPF_SKELS_LINKED): $(BPF_OBJS_LINKED) $(BPFTOOL)
	@echo "  BPF GEN-OBJ " $(@:.skel.h=)
	$(Q)$(BPFTOOL) gen object $(@:.skel.h=.lbpf.o) $(addprefix $(obj)/,$($(@F)-deps))
	@echo "  BPF GEN-SKEL" $(@:.skel.h=)
	$(Q)$(BPFTOOL) gen skeleton $(@:.skel.h=.lbpf.o) name $(notdir $(@:.skel.h=)) > $@

# asm/sysreg.h - inline assembly used by it is incompatible with llvm.
# But, there is no easy way to fix it, so just exclude it since it is
# useless for BPF samples.
# below we use long chain of commands, clang | opt | llvm-dis | llc,
# to generate final object file. 'clang' compiles the source into IR
# with native target, e.g., x64, arm64, etc. 'opt' does bpf CORE IR builtin
# processing (llvm12) and IR optimizations. 'llvm-dis' converts
# 'opt' output to IR, and finally 'llc' generates bpf byte code.
$(obj)/%.o: $(src)/%.c
	@echo "  CLANG-bpf " $@
	$(Q)$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(BPF_EXTRA_CFLAGS) \
		-I$(obj) -I$(srctree)/tools/testing/selftests/bpf/ \
		-I$(srctree)/tools/lib/ \
		-D__KERNEL__ -D__BPF_TRACING__ -Wno-unused-value -Wno-pointer-sign \
		-D__TARGET_ARCH_$(SRCARCH) -Wno-compare-distinct-pointer-types \
		-Wno-gnu-variable-sized-type-not-at-end \
		-Wno-address-of-packed-member -Wno-tautological-compare \
		-Wno-unknown-warning-option $(CLANG_ARCH_ARGS) \
		-fno-asynchronous-unwind-tables \
		-I$(srctree)/samples/bpf/ -include asm_goto_workaround.h \
		-O2 -emit-llvm -Xclang -disable-llvm-passes -c $< -o - | \
		$(OPT) -O2 -mtriple=bpf-pc-linux | $(LLVM_DIS) | \
		$(LLC) -march=bpf $(LLC_FLAGS) -filetype=obj -o $@
ifeq ($(DWARF2BTF),y)
	$(BTF_PAHOLE) -J $@
endif
