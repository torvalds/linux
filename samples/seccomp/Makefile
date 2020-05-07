# SPDX-License-Identifier: GPL-2.0
ifndef CROSS_COMPILE
hostprogs := bpf-fancy dropper bpf-direct user-trap

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

HOSTCFLAGS_user-trap.o += -I$(objtree)/usr/include
HOSTCFLAGS_user-trap.o += -idirafter $(objtree)/include
user-trap-objs := user-trap.o

# Try to match the kernel target.
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
HOSTCFLAGS_user-trap.o += $(MFLAG)
HOSTLDLIBS_bpf-direct += $(MFLAG)
HOSTLDLIBS_bpf-fancy += $(MFLAG)
HOSTLDLIBS_dropper += $(MFLAG)
HOSTLDLIBS_user-trap += $(MFLAG)
endif
always-y := $(hostprogs)
endif
