# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := test_verifier test_maps
hostprogs-y += sock_example
hostprogs-y += sockex1

test_verifier-objs := test_verifier.o libbpf.o
test_maps-objs := test_maps.o libbpf.o
sock_example-objs := sock_example.o libbpf.o
sockex1-objs := bpf_load.o libbpf.o sockex1_user.o

# Tell kbuild to always build the programs
always := $(hostprogs-y)
always += sockex1_kern.o

HOSTCFLAGS += -I$(objtree)/usr/include

HOSTCFLAGS_bpf_load.o += -I$(objtree)/usr/include -Wno-unused-variable
HOSTLOADLIBES_sockex1 += -lelf

# point this to your LLVM backend with bpf support
LLC=$(srctree)/tools/bpf/llvm/bld/Debug+Asserts/bin/llc

%.o: %.c
	clang $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
		-D__KERNEL__ -Wno-unused-value -Wno-pointer-sign \
		-O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=obj -o $@
