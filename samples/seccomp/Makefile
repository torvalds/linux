# SPDX-License-Identifier: GPL-2.0
userprogs := bpf-fancy dropper bpf-direct user-trap

bpf-fancy-objs := bpf-fancy.o bpf-helper.o

userccflags += -I usr/include

always-y := $(userprogs)
