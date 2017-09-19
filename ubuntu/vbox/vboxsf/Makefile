KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile.module $
## @file
# VirtualBox Guest Additions Module Makefile.
#
# (For 2.6.x this file must be 'Makefile'!)
#

#
# Copyright (C) 2006-2017 Oracle Corporation
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

MOD_NAME = vboxsf
MOD_OBJS   = \
	vfsmod.o \
	dirops.o \
	lnkops.o \
	regops.o \
	utils.o \
	VBoxGuestR0LibHGCM.o \
	VBoxGuestR0LibIdc.o \
	VBoxGuestR0LibIdc-unix.o \
	VBoxGuestR0LibSharedFolders.o
ifeq ($(BUILD_TARGET_ARCH),x86)
MOD_OBJS  += \
	divdi3.o \
	moddi3.o \
	udivdi3.o \
	udivmoddi4.o \
	umoddi3.o \
	qdivrem.o
endif

MOD_INCL = \
    $(addprefix -I$(KBUILD_EXTMOD),/ /include /r0drv/linux) \
    $(addprefix -I$(KBUILD_EXTMOD)/vboxsf,/ /include /r0drv/linux)

ifneq ($(wildcard $(KBUILD_EXTMOD)/vboxsf),)
 MANGLING := $(KBUILD_EXTMOD)/vboxsf/include/VBox/VBoxGuestMangling.h
else
 MANGLING := $(KBUILD_EXTMOD)/include/VBox/VBoxGuestMangling.h
endif

MOD_DEFS   = -DRT_OS_LINUX -DIN_RING0 -DIN_RT_R0 \
	    -DIN_SUP_R0 -DVBOX -DVBOX_WITH_HGCM -DIN_MODULE -DIN_GUEST_R0
# our module does not export any symbol
MOD_DEFS  += -DRT_NO_EXPORT_SYMBOL
ifeq ($(BUILD_TARGET_ARCH),amd64)
 MOD_DEFS += -DRT_ARCH_AMD64 -DVBOX_WITH_64_BITS_GUESTS
else
 MOD_DEFS += -DRT_ARCH_X86
endif

ifeq ($(KERN_VERSION), 24)
 MOD_CFLAGS =
else
 MOD_CFLAGS = -Wno-declaration-after-statement -fshort-wchar -include $(MANGLING) -fno-pie

# special hack for Fedora Core 6 2.6.18 (fc6), rhel5 2.6.18 (el5),
# ClarkConnect 4.3 (cc4) and ClarkConnect 5 (v5)
 ifeq ($(KERNELRELEASE),)
  MOD_EXTRA += $(foreach inc,$(KERN_INCL),\
              $(if $(wildcard $(inc)/linux/utsrelease.h),\
                $(if $(shell grep '"2.6.18.*fc6.*"' $(inc)/linux/utsrelease.h; \
			    grep '"2.6.18.*el5.*"' $(inc)/linux/utsrelease.h; \
			    grep '"2.6.18.*v5.*"'  $(inc)/linux/utsrelease.h; \
			    grep '"2.6.18.*cc4.*"' $(inc)/linux/utsrelease.h),\
		-DKERNEL_FC6,),))
 else
  MOD_EXTRA += $(if $(shell echo "$(KERNELRELEASE)"|grep '2.6.18.*fc6.*';\
			echo "$(KERNELRELEASE)"|grep '2.6.18.*el5.*';\
			echo "$(KERNELRELEASE)"|grep '2.6.18.*v5.*';\
			echo "$(KERNELRELEASE)"|grep '2.6.18.*cc4.*'),\
		-DKERNEL_FC6,)
 endif
endif

MOD_CLEAN = . linux r0drv r0drv/linux

include $(obj)/Makefile.include.footer
