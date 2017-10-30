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

BUILD = 1

# We want to build on Linux 3.11 and later, plus the 3.10 EL 7.3 and later
# kernels.  This file was added in 3.11 and back-ported to the EL 7.3 kernel.
ifeq ($(wildcard $(KERN_INCL)/drm/drm_rect.h),)
 #BUILD =
endif

# And we do not want to build on EL6, regardless of what may have been
# backported.
ifneq ($(wildcard /etc/redhat-release),)
 ifneq ($(shell grep "Enterprise.*release 6" /etc/redhat-release),)
  BUILD =
 endif
endif

ifneq ($(BUILD),)

MOD_NAME   = vboxvideo
MOD_OBJS   = hgsmi_base.o \
             modesetting.o vbox_drv.o vbox_fb.o vbox_irq.o vbox_main.o \
             vbox_mode.o vbox_ttm.o vbva_base.o vbox_prime.o vbox_hgsmi.o
MOD_INCL   = -I$(KBUILD_EXTMOD) -Iinclude/drm

include $(obj)/Makefile.include.footer

else  # ! wildcard $(KERN_INCL)/drm/drm_rect.h

 all:
 install:
 clean:

endif  # ! wildcard $(KERN_INCL)/drm/drm_rect.h
