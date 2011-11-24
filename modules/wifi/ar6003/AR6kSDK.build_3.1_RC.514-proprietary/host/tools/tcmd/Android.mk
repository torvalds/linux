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

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../../include \
    $(LOCAL_PATH)/../../os/linux/include \
    $(LOCAL_PATH)/../../../include \

LOCAL_CFLAGS+= 
LOCAL_SRC_FILES:= athtestcmd.c
LOCAL_MODULE := athtestcmd
LOCAL_MODULE_TAGS := debug eng optional

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE = libathtestcmd
LOCAL_MODULE_TAGS := debug eng optional
LOCAL_CFLAGS = $(L_CFLAGS)
LOCAL_SRC_FILES = athtestcmd.c athtestcmdlib.c
LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/../../include \
    $(LOCAL_PATH)/../../os/linux/include \
    $(LOCAL_PATH)/../../../include \

LOCAL_CFLAGS+=-DATHTESTCMD_LIB

LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_COPY_HEADERS_TO := libathtestcmd
LOCAL_COPY_HEADERS := athtestcmdlib.h
include $(BUILD_STATIC_LIBRARY)

