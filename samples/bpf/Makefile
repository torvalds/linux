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
hostprogs-y += trace_output
hostprogs-y += lathist
hostprogs-y += offwaketime
hostprogs-y += spintest
hostprogs-y += map_perf_test

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
trace_output-objs := bpf_load.o libbpf.o trace_output_user.o
lathist-objs := bpf_load.o libbpf.o lathist_user.o
offwaketime-objs := bpf_load.o libbpf.o offwaketime_user.o
spintest-objs := bpf_load.o libbpf.o spintest_user.o
map_perf_test-objs := bpf_load.o libbpf.o map_perf_test_user.o

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
always += trace_output_kern.o
always += tcbpf1_kern.o
always += lathist_kern.o
always += offwaketime_kern.o
always += spintest_kern.o
always += map_perf_test_kern.o

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
HOSTLOADLIBES_trace_output += -lelf -lrt
HOSTLOADLIBES_lathist += -lelf
HOSTLOADLIBES_offwaketime += -lelf
HOSTLOADLIBES_spintest += -lelf
HOSTLOADLIBES_map_perf_test += -lelf -lrt

# point this to your LLVM backend with bpf support
LLC=$(srctree)/tools/bpf/llvm/bld/Debug+Asserts/bin/llc

# asm/sysreg.h inline assmbly used by it is incompatible with llvm.
# But, ehere is not easy way to fix it, so just exclude it since it is
# useless for BPF samples.
$(obj)/%.o: $(src)/%.c
	clang $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
		-D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=obj -o $@
	clang $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
		-D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=asm -o $@.s
