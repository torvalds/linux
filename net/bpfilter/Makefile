# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux BPFILTER layer.
#

userprogs := bpfilter_umh
bpfilter_umh-objs := main.o
userccflags += -I $(srctree)/tools/include/ -I $(srctree)/tools/include/uapi

ifeq ($(CONFIG_BPFILTER_UMH), y)
# builtin bpfilter_umh should be linked with -static
# since rootfs isn't mounted at the time of __init
# function is called and do_execv won't find elf interpreter
userldflags += -static
endif

$(obj)/bpfilter_umh_blob.o: $(obj)/bpfilter_umh

obj-$(CONFIG_BPFILTER_UMH) += bpfilter.o
bpfilter-objs += bpfilter_kern.o bpfilter_umh_blob.o
