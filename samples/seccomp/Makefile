# SPDX-License-Identifier: GPL-2.0
# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

hostprogs-$(CONFIG_SAMPLE_SECCOMP) := bpf-fancy dropper bpf-direct

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
ifndef CROSS_COMPILE
ifndef CONFIG_64BIT

# s390 has -m31 flag to build 31 bit binaries
ifndef CONFIG_S390
MFLAG = -m32
else
MFLAG = -m31
endif

HOSTCFLAGS_bpf-direct.o += $(MFLAG)
HOSTCFLAGS_dropper.o += $(MFLAG)
HOSTCFLAGS_bpf-helper.o += $(MFLAG)
HOSTCFLAGS_bpf-fancy.o += $(MFLAG)
HOSTLOADLIBES_bpf-direct += $(MFLAG)
HOSTLOADLIBES_bpf-fancy += $(MFLAG)
HOSTLOADLIBES_dropper += $(MFLAG)
endif
always := $(hostprogs-m)
else
# MIPS system calls are defined based on the -mabi that is passed
# to the toolchain which may or may not be a valid option
# for the host toolchain. So disable tests if target architecture
# is MIPS but the host isn't.
ifndef CONFIG_MIPS
always := $(hostprogs-m)
endif
endif
