# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

hostprogs-$(CONFIG_SECCOMP_FILTER) := bpf-fancy dropper bpf-direct

HOSTCFLAGS_bpf-fancy.o += -I$(objtree)/usr/include
HOSTCFLAGS_bpf-fancy.o += -idirafter $(objtree)/include
HOSTCFLAGS_bpf-helper.o += -I$(objtree)/usr/include
HOSTCFLAGS_bpf-helper.o += -idirafter $(objtree)/include
bpf-fancy-objs := bpf-fancy.o bpf-helper.o

HOSTCFLAGS_dropper.o += -I$(objtree)/usr/include
HOSTCFLAGS_dropper.o += -idirafter $(objtree)/include
dropper-objs := dropper.o

HOSTCFLAGS_bpf-direct.o += -I$(objtree)/usr/include
HOSTCFLAGS_bpf-direct.o += -idirafter $(objtree)/include
bpf-direct-objs := bpf-direct.o

# Try to match the kernel target.
ifeq ($(CONFIG_64BIT),)
HOSTCFLAGS_bpf-direct.o += -m32
HOSTCFLAGS_dropper.o += -m32
HOSTCFLAGS_bpf-helper.o += -m32
HOSTCFLAGS_bpf-fancy.o += -m32
HOSTLOADLIBES_bpf-direct += -m32
HOSTLOADLIBES_bpf-fancy += -m32
HOSTLOADLIBES_dropper += -m32
endif

# Tell kbuild to always build the programs
always := $(hostprogs-y)
