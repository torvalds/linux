KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile.module.kms $
## @file
# VirtualBox Guest Additions Module Makefile.
#
# (For 2.6.x this file must be 'Makefile'!)
#

#
# Copyright (C) 2006-2010 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

# Linux kbuild sets this to our source directory if we are called from there
obj ?= $(CURDIR)
include $(obj)/Makefile.include.header

MOD_NAME   = vboxvideo

MY_KERNELRELEASE := $(KERNELRELEASE)
ifeq ($(MY_KERNELRELEASE),)
  # as of 2.6.16 KERNELRELEASE is only set if the kernel is configured
  MY_KERNELRELEASE := $(KERNELVERSION)
endif
ifeq ($(KERN_VERSION),24)
  # sledgehammer, see the 2.4 handling in Makefile.include.footer
  MY_KERNELRELEASE := 2.4.0
endif
ifeq ($(filter 1.% 2.% 3.0.% 3.1.% 3.2.% 3.3.% 3.4.% 3.5.% 3.6.% 3.7.% \
3.8.% 3.9.%,$(MY_KERNELRELEASE)),)
MOD_OBJS   = HGSMIBase.o HGSMICommon.o HGSMIMemAlloc.o heapoffset.o \
             Modesetting.o vbox_drv.o vbox_fb.o vbox_irq.o vbox_main.o \
             vbox_mode.o vbox_ttm.o VBVABase.o vbox_prime.o
else
MOD_OBJS   = vbox_dummy.o
endif

ifneq ($(wildcard $(KBUILD_EXTMOD)/vboxvideo),)
 MANGLING := $(KBUILD_EXTMOD)/vboxvideo/include/VBox/VBoxGuestMangling.h
else
 MANGLING := $(KBUILD_EXTMOD)/include/VBox/VBoxGuestMangling.h
endif
MOD_CFLAGS = -Wno-declaration-after-statement -fshort-wchar -fno-pie
ifneq ($(KERN_VERSION),24)
MOD_CFLAGS += -include $(MANGLING)
endif
MOD_INCL   = $(addprefix -I$(KBUILD_EXTMOD),/ /include)
# What on earth is this?
MOD_INCL  += $(addprefix -I$(KBUILD_EXTMOD)/vboxvideo,/ /include)
MOD_INCL  += -Iinclude/drm
MOD_DEFS  := -DRT_OS_LINUX -DIN_RING0 -DIN_RT_R0 \
	     -DIN_SUP_R0 -DVBOX -DVBOX_WITH_HGCM -DLOG_TO_BACKDOOR -DIN_MODULE \
	     -DIN_GUEST_R0
# our module does not export any symbol
MOD_DEFS  += -DRT_NO_EXPORT_SYMBOL
ifeq ($(BUILD_TARGET_ARCH),amd64)
 MOD_DEFS += -DRT_ARCH_AMD64 -DVBOX_WITH_64_BITS_GUESTS
else
 MOD_DEFS += -DRT_ARCH_X86
endif
MOD_CLEAN  = . linux r0drv r0drv/linux

include $(obj)/Makefile.include.footer
