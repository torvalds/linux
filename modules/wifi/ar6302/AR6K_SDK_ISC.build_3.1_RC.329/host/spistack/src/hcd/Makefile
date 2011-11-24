
# Copyright 2004-2006 Atheros Communications, Inc.

ifeq ($(CT_OS_TYPE),linux)

ifneq ($(CT_OS_SUB_TYPE),linux_2_4)
# For Linux 2.6 and higher
obj-m += $(CT_HC_DRIVERS) 
endif

ifeq ($(CT_OS_SUB_TYPE),linux_2_4)
# reset CFLAGs, for some reason on 2.4.20, the EXTRA_FLAGS is not propogated from our top-level makefile
EXTRA_CFLAGS += $(CT_PASS_CFLAGS)
subdir-m += $(CT_HC_DRIVERS)  
include $(CT_OS_TOP_LEVEL_RULE)
endif

endif  

ifeq ($(CT_OS_TYPE),qnx)
##QNX
LIST=VARIANT
ifndef QRECURSE
QRECURSE=recurse.mk
ifdef QCONFIG
QRDIR=$(dir $(QCONFIG))
endif
endif
include $(QRDIR)$(QRECURSE)
endif
