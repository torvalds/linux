#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
# 
# The software source and binaries included in this development package are
# licensed, not sold. You, or your company, received the package under one
# or more license agreements. The rights granted to you are specifically
# listed in these license agreement(s). All other rights remain with Atheros
# Communications, Inc., its subsidiaries, or the respective owner including
# those listed on the included copyright notices.  Distribution of any
# portion of this package must be in strict compliance with the license
# agreement(s) terms.
# </copyright>
# 
# <summary>
# 	Wifi driver for AR6002
# </summary>
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

#MY_SIGMA_DUT := sigma-dut
MY_SIGMA_DUT :=

INCLUDES = $(LOCAL_PATH)
INCLUDES += $(LOCAL_PATH)/../src
INCLUDES += $(LOCAL_PATH)/../src/utils

OBJS_c = wfa_tlv.c    
OBJS_c += wfa_tg.c
OBJS_c += wfa_cs.c
OBJS_c += wfa_cmdtbl.c
OBJS_c += wfa_wmmps.c
OBJS_c += wfa_thr.c
OBJS_c += sigma_dut.c
OBJS_c += wpa_ctrl.c
OBJS_c += wpa_helpers.c

OBJS_c += cmds_reg.c
OBJS_c += basic.c
OBJS_c += sta.c
OBJS_c += traffic.c
OBJS_c += p2p.c
OBJS_c += ap.c
OBJS_c += powerswitch.c
OBJS_c += atheros.c

L_CFLAGS += -DCONFIG_CTRL_IFACE
L_CFLAGS += -DCONFIG_CTRL_IFACE_UNIX

ifndef NO_TRAFFIC_AGENT
L_CFLAGS += -DCONFIG_TRAFFIC_AGENT
OBJS_c += traffic_agent.c
LIBS += -lpthread
endif

ifndef NO_WLANTEST
L_CFLAGS += -DCONFIG_WLANTEST
OBJS_c += wlantest.c
endif

#L_CFLAGS += -I../src/utils/

include $(CLEAR_VARS)
LOCAL_MODULE := athsigma_dut
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_SHARED_LIBRARIES := libc libcutils
LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(addprefix $(MY_SIGMA_DUT),$(OBJS_c))
LOCAL_C_INCLUDES := $(addprefix $(MY_SIGMA_DUT),$(INCLUDES))
include $(BUILD_EXECUTABLE)
