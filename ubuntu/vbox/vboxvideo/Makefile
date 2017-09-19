KBUILD_EXTMOD=${srctree}/ubuntu/vbox
# $Id: Makefile.module.kms $
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

BUILD =

# We want to build on Linux 3.11 and later and on all EL 7 kernels.
ifneq ($(filter-out 1.% 2.% 3.0.% 3.1.% 3.2.% 3.3.% 3.4.% 3.5.% 3.6.% 3.7.% \
                    3.8.% 3.9.% 3.10.%,$(KERN_VER)),)
 BUILD = 1
endif
ifeq ($(filter-out %.el7.x86_64,$(KERN_VER)),)
 BUILD = 1
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
