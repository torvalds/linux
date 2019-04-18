KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile.module $
## @file
# VBox Linux Shared Folders VFS Module Makefile.
#
# (For 2.6.x this file must be 'Makefile'!)
#

#
# Copyright (C) 2006-2019 Oracle Corporation
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
include $(obj)/Makefile-header.gmk
VBOXSF_DIR = $(VBOX_MODULE_SRC_DIR)

VBOXMOD_NAME = vboxsf
VBOXMOD_OBJS = \
	vfsmod.o \
	dirops.o \
	lnkops.o \
	regops.o \
	utils.o \
	VBoxGuestR0LibGenericRequest.o \
	VBoxGuestR0LibHGCM.o \
	VBoxGuestR0LibIdc.o \
	VBoxGuestR0LibIdc-unix.o \
	VBoxGuestR0LibInit.o \
	VBoxGuestR0LibPhysHeap.o \
	VBoxGuestR0LibSharedFolders.o
ifeq ($(BUILD_TARGET_ARCH),x86)
VBOXMOD_OBJS += \
	divdi3.o \
	moddi3.o \
	udivdi3.o \
	udivmoddi4.o \
	umoddi3.o \
	qdivrem.o
endif
VBOXMOD_INCL = \
	$(VBOXSF_DIR) \
	$(VBOXSF_DIR)include \
	$(VBOXSF_DIR)r0drv/linux
VBOXMOD_DEFS = \
	RT_OS_LINUX \
	IN_RING0 \
	IN_RT_R0 \
	IN_SUP_R0 \
	VBOX \
	VBOX_WITH_HGCM \
	IN_MODULE \
	IN_GUEST \
	IN_GUEST_R0 \
	RT_NO_EXPORT_SYMBOL
ifeq ($(BUILD_TARGET_ARCH),amd64)
VBOXMOD_DEFS += VBOX_WITH_64_BITS_GUESTS
endif
ifneq ($(filter %uek.x86_64,$(KERN_VER)),)
VBOXMOD_DEFS += VBOX_UEK
endif
VBOXMOD_CFLAGS := $(call VBOX_GCC_CHECK_CC,-Wno-declaration-after-statement,-Wno-declaration-after-statement,,)
VBOXMOD_CFLAGS += $(call VBOX_GCC_CHECK_CC,-fno-pie,-fno-pie,,)
ifneq ($(KERN_VERSION),24)
VBOXMOD_CFLAGS += -include $(VBOXSF_DIR)/include/VBox/VBoxGuestMangling.h
## @todo r-bird: What's with -fshort-wchar here?? We either need that or we dont, right? It should be 2.6+ only.
VBOXMOD_CFLAGS += -fshort-wchar
endif
ifdef VBOX_NO_OMIT_FRAME_POINTER
VBOXMOD_CFLAGS += -fno-omit-frame-pointer
endif

ifneq ($(KERN_VERSION),24)
# special hack for Fedora Core 6 2.6.18 (fc6), rhel5 2.6.18 (el5),
# ClarkConnect 4.3 (cc4) and ClarkConnect 5 (v5)
 ifeq ($(KERNELRELEASE),)
VBOXMOD_CFLAGS += $(foreach inc,$(KERN_INCL),\
               $(if $(wildcard $(inc)/linux/utsrelease.h),\
                 $(if $(shell grep '"2.6.18.*fc6.*"' $(inc)/linux/utsrelease.h; \
                              grep '"2.6.18.*el5.*"' $(inc)/linux/utsrelease.h; \
                              grep '"2.6.18.*v5.*"'  $(inc)/linux/utsrelease.h; \
                              grep '"2.6.18.*cc4.*"' $(inc)/linux/utsrelease.h),\
                   -DKERNEL_FC6,),))
 else
VBOXMOD_CFLAGS += $(if $(shell echo "$(KERNELRELEASE)"|grep '2.6.18.*fc6.*';\
                          echo "$(KERNELRELEASE)"|grep '2.6.18.*el5.*';\
                          echo "$(KERNELRELEASE)"|grep '2.6.18.*v5.*';\
                          echo "$(KERNELRELEASE)"|grep '2.6.18.*cc4.*'),\
		-DKERNEL_FC6,)
 endif
endif

VBOXMOD_CLEAN = . linux r0drv r0drv/linux

include $(obj)/Makefile-footer.gmk

