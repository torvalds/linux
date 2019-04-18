KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile.module.kms $
## @file
# VirtualBox Guest Additions Module Makefile.
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
VBOXDRM_DIR = $(VBOX_MODULE_SRC_DIR)

# We want to build on Linux 3.11 and later and on all EL 7 kernels.
VBOX_BUILD =
ifneq ($(filter-out 1.% 2.% 3.0.% 3.1.% 3.2.% 3.3.% 3.4.% 3.5.% 3.6.% 3.7.% 3.8.% 3.9.% 3.10.%,$(KERN_VER)),)
 VBOX_BUILD = 1
endif
ifeq ($(filter-out %.el7.x86_64,$(KERN_VER)),)
 VBOX_BUILD = 1
endif

ifneq ($(VBOX_BUILD),)

VBOXMOD_NAME = vboxvideo
VBOXMOD_OBJS = \
	hgsmi_base.o \
	modesetting.o \
	vbox_drv.o \
	vbox_fb.o \
	vbox_irq.o \
	vbox_main.o \
	vbox_mode.o \
	vbox_ttm.o \
	vbva_base.o \
	vbox_prime.o \
	vbox_hgsmi.o
VBOXMOD_INCL = \
	$(VBOXDRM_DIR) \
	include/drm
## @todo r=bird: -Iinclude/drm is ambigious.

include $(obj)/Makefile-footer.gmk

else  # !VBOX_BUILD

 all:
 install:
 clean:

endif # !VBOX_BUILD

