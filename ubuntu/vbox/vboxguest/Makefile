KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile $
## @file
# VirtualBox Guest Additions Module Makefile.
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
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL) only, as it comes in the "COPYING.CDDL" file of the
# VirtualBox OSE distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#

# Linux kbuild sets this to our source directory if we are called from there
obj ?= $(CURDIR)
include $(obj)/Makefile-header.gmk
VBOXGUEST_DIR = $(VBOX_MODULE_SRC_DIR)

VBOXMOD_NAME = vboxguest
VBOXMOD_OBJS = \
	VBoxGuest-linux.o \
	VBoxGuest.o \
	VBoxGuestR0LibGenericRequest.o \
	VBoxGuestR0LibHGCMInternal.o \
	VBoxGuestR0LibInit.o \
	VBoxGuestR0LibPhysHeap.o \
	VBoxGuestR0LibVMMDev.o \
	r0drv/alloc-r0drv.o \
	r0drv/initterm-r0drv.o \
	r0drv/memobj-r0drv.o \
	r0drv/mpnotification-r0drv.o \
	r0drv/powernotification-r0drv.o \
	r0drv/linux/alloc-r0drv-linux.o \
	r0drv/linux/assert-r0drv-linux.o \
	r0drv/linux/initterm-r0drv-linux.o \
	r0drv/linux/memobj-r0drv-linux.o \
	r0drv/linux/memuserkernel-r0drv-linux.o \
	r0drv/linux/mp-r0drv-linux.o \
	r0drv/linux/mpnotification-r0drv-linux.o \
	r0drv/linux/process-r0drv-linux.o \
	r0drv/linux/semevent-r0drv-linux.o \
	r0drv/linux/semeventmulti-r0drv-linux.o \
	r0drv/linux/semfastmutex-r0drv-linux.o \
	r0drv/linux/semmutex-r0drv-linux.o \
	r0drv/linux/spinlock-r0drv-linux.o \
	r0drv/linux/thread-r0drv-linux.o \
	r0drv/linux/thread2-r0drv-linux.o \
	r0drv/linux/time-r0drv-linux.o \
	r0drv/linux/timer-r0drv-linux.o \
	r0drv/linux/RTLogWriteDebugger-r0drv-linux.o \
	r0drv/generic/semspinmutex-r0drv-generic.o \
	common/alloc/alloc.o \
	common/err/RTErrConvertFromErrno.o \
	common/err/RTErrConvertToErrno.o \
	common/err/errinfo.o \
	common/log/log.o \
	common/log/logellipsis.o \
	common/log/logrel.o \
	common/log/logrelellipsis.o \
	common/log/logcom.o \
	common/log/logformat.o \
	common/misc/RTAssertMsg1Weak.o \
	common/misc/RTAssertMsg2.o \
	common/misc/RTAssertMsg2Add.o \
	common/misc/RTAssertMsg2AddWeak.o \
	common/misc/RTAssertMsg2AddWeakV.o \
	common/misc/RTAssertMsg2Weak.o \
	common/misc/RTAssertMsg2WeakV.o \
	common/misc/assert.o \
	common/misc/thread.o \
	common/string/RTStrCat.o \
	common/string/RTStrCmp.o \
	common/string/RTStrCopy.o \
	common/string/RTStrCopyEx.o \
	common/string/RTStrCopyP.o \
	common/string/RTStrICmpAscii.o \
	common/string/RTStrNICmpAscii.o \
	common/string/RTStrNCmp.o \
	common/string/RTStrNLen.o \
	common/string/stringalloc.o \
	common/string/strformat.o \
	common/string/strformatnum.o \
	common/string/strformatrt.o \
	common/string/strformattype.o \
	common/string/strprintf.o \
	common/string/strtonum.o \
	common/string/utf-8.o \
	common/table/avlpv.o \
	common/time/time.o \
	generic/RTAssertShouldPanic-generic.o \
	generic/RTLogWriteStdErr-stub-generic.o \
	generic/RTLogWriteStdOut-stub-generic.o \
	generic/RTMpGetCoreCount-generic.o \
	generic/RTSemEventWait-2-ex-generic.o \
	generic/RTSemEventWaitNoResume-2-ex-generic.o \
	generic/RTSemEventMultiWait-2-ex-generic.o \
	generic/RTSemEventMultiWaitNoResume-2-ex-generic.o \
	generic/rtStrFormatKernelAddress-generic.o \
	generic/errvars-generic.o \
	generic/mppresent-generic.o \
	VBox/log-vbox.o \
	VBox/logbackdoor.o
ifeq ($(BUILD_TARGET_ARCH),x86)
VBOXMOD_OBJS += \
	common/math/gcc/divdi3.o \
	common/math/gcc/moddi3.o \
	common/math/gcc/udivdi3.o \
	common/math/gcc/udivmoddi4.o \
	common/math/gcc/umoddi3.o \
	common/math/gcc/qdivrem.o
endif
ifeq ($(BUILD_TARGET_ARCH),amd64)
VBOXMOD_OBJS += common/alloc/heapsimple.o
endif

VBOXMOD_DEFS = \
	VBOX \
	RT_OS_LINUX \
	IN_RING0 \
	IN_RT_R0 \
	IN_GUEST \
	IN_GUEST_R0 \
	IN_MODULE \
	RT_WITH_VBOX \
	VBGL_VBOXGUEST \
	VBOX_WITH_HGCM
ifeq ($(BUILD_TARGET_ARCH),amd64)
VBOXMOD_DEFS += VBOX_WITH_64_BITS_GUESTS
endif
ifeq ($(KERN_VERSION),24)
VBOXMOD_DEFS += EXPORT_SYMTAB
endif

VBOXMOD_INCL = \
	$(VBOXGUEST_DIR) \
	$(VBOXGUEST_DIR)include \
	$(VBOXGUEST_DIR)r0drv/linux

VBOXMOD_CFLAGS := $(call VBOX_GCC_CHECK_CC,-Wno-declaration-after-statement,-Wno-declaration-after-statement,,)
VBOXMOD_CFLAGS += $(call VBOX_GCC_CHECK_CC,-fno-pie,-fno-pie,,)
ifneq ($(KERN_VERSION),24)
VBOXMOD_CFLAGS += -include $(VBOXGUEST_DIR)include/VBox/VBoxGuestMangling.h
endif

VBOXMOD_CLEAN = \
	. \
	linux \
	r0drv \
	generic \
	r0drv/linux \
	r0drv/generic \
	VBox \
	common/alloc \
	common/err \
	common/log \
	common/math/gcc \
	common/misc \
	common/string \
	common/table \
	common/time

include $(obj)/Makefile-footer.gmk

check: $(VBOXMOD_NAME)
	@if ! readelf -p __ksymtab_strings vboxguest.ko | grep -E "\[.*\]  *(RT|g_..*RT.*)"; then \
	    echo "All exported IPRT symbols are properly renamed!"; \
	 else \
	    echo "error: Some exported IPRT symbols was not properly renamed! See above." >&2; \
	    false; \
	 fi

