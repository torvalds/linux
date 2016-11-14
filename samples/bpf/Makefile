# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := test_verifier test_maps
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
hostprogs-y += xdp1
hostprogs-y += xdp2
hostprogs-y += test_current_task_under_cgroup
hostprogs-y += trace_event
hostprogs-y += sampleip
hostprogs-y += tc_l2_redirect

test_verifier-objs := test_verifier.o libbpf.o
test_maps-objs := test_maps.o libbpf.o
sock_example-objs := sock_example.o libbpf.o
fds_example-objs := bpf_load.o libbpf.o fds_example.o
sockex1-objs := bpf_load.o libbpf.o sockex1_user.o
sockex2-objs := bpf_load.o libbpf.o sockex2_user.o
sockex3-objs := bpf_load.o libbpf.o sockex3_user.o
tracex1-objs := bpf_load.o libbpf.o tracex1_user.o
tracex2-objs := bpf_load.o libbpf.o tracex2_user.o
tracex3-objs := bpf_load.o libbpf.o tracex3_user.o
tracex4-objs := bpf_load.o libbpf.o tracex4_user.o
tracex5-objs := bpf_load.o libbpf.o tracex5_user.o
tracex6-objs := bpf_load.o libbpf.o tracex6_user.o
test_probe_write_user-objs := bpf_load.o libbpf.o test_probe_write_user_user.o
trace_output-objs := bpf_load.o libbpf.o trace_output_user.o
lathist-objs := bpf_load.o libbpf.o lathist_user.o
offwaketime-objs := bpf_load.o libbpf.o offwaketime_user.o
spintest-objs := bpf_load.o libbpf.o spintest_user.o
map_perf_test-objs := bpf_load.o libbpf.o map_perf_test_user.o
test_overhead-objs := bpf_load.o libbpf.o test_overhead_user.o
test_cgrp2_array_pin-objs := libbpf.o test_cgrp2_array_pin.o
xdp1-objs := bpf_load.o libbpf.o xdp1_user.o
# reuse xdp1 source intentionally
xdp2-objs := bpf_load.o libbpf.o xdp1_user.o
test_current_task_under_cgroup-objs := bpf_load.o libbpf.o \
				       test_current_task_under_cgroup_user.o
trace_event-objs := bpf_load.o libbpf.o trace_event_user.o
sampleip-objs := bpf_load.o libbpf.o sampleip_user.o
tc_l2_redirect-objs := bpf_load.o libbpf.o tc_l2_redirect_user.o

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

HOSTCFLAGS += -I$(objtree)/usr/include

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

# Allows pointing LLC/CLANG to a LLVM backend with bpf support, redefine on cmdline:
#  make samples/bpf/ LLC=~/git/llvm/build/bin/llc CLANG=~/git/llvm/build/bin/clang
LLC ?= llc
CLANG ?= clang

# Trick to allow make to be run from this directory
all:
	$(MAKE) -C ../../ $$PWD/

clean:
	$(MAKE) -C ../../ M=$$PWD clean
	@rm -f *~

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

# asm/sysreg.h - inline assembly used by it is incompatible with llvm.
# But, there is no easy way to fix it, so just exclude it since it is
# useless for BPF samples.
$(obj)/%.o: $(src)/%.c
	$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
		-D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign \
		-Wno-compare-distinct-pointer-types \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=obj -o $@
