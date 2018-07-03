# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux BPFILTER layer.
#

hostprogs-y := bpfilter_umh
bpfilter_umh-objs := main.o
HOSTCFLAGS += -I. -Itools/include/ -Itools/include/uapi
HOSTCC := $(CC)

ifeq ($(CONFIG_BPFILTER_UMH), y)
# builtin bpfilter_umh should be compiled with -static
# since rootfs isn't mounted at the time of __init
# function is called and do_execv won't find elf interpreter
HOSTLDFLAGS += -static
endif

# a bit of elf magic to convert bpfilter_umh binary into a binary blob
# inside bpfilter_umh.o elf file referenced by
# _binary_net_bpfilter_bpfilter_umh_start symbol
# which bpfilter_kern.c passes further into umh blob loader at run-time
quiet_cmd_copy_umh = GEN $@
      cmd_copy_umh = echo ':' > $(obj)/.bpfilter_umh.o.cmd; \
      $(OBJCOPY) -I binary \
          `LC_ALL=C $(OBJDUMP) -f net/bpfilter/bpfilter_umh \
          |awk -F' |,' '/file format/{print "-O",$$NF} \
          /^architecture:/{print "-B",$$2}'` \
      --rename-section .data=.init.rodata $< $@

$(obj)/bpfilter_umh.o: $(obj)/bpfilter_umh
	$(call cmd,copy_umh)

obj-$(CONFIG_BPFILTER_UMH) += bpfilter.o
bpfilter-objs += bpfilter_kern.o bpfilter_umh.o
