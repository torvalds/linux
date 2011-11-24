#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
#  
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================
#
# link or copy whole olca driver into external/athwlan/olca/
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(BT_ALT_STACK),true)
# Define AR6K_PREBUILT_HCIUTILS_LIB:= true if you want to force to enable HCIUTILS
# Otherwise, we will try to load the hciutil.so dynamically to determinate whether we use hciutils or hci socket
# Uncomment the following if you want to use static library

#AR6K_PREBUILT_HCIUTILS_LIB := true

endif

include $(CLEAR_VARS)

LOCAL_SRC_FILES := abtfilt_bluez_dbus.c \
		abtfilt_core.c \
		abtfilt_main.c \
		abtfilt_utils.c \
		abtfilt_wlan.c \
		btfilter_action.c \
		btfilter_core.c

ifeq ($(BT_ALT_STACK),true)
LOCAL_SRC_FILES += abtfilt_bluez_hciutils.c

ifeq ($(AR6K_PREBUILT_HCIUTILS_LIB),true)
LOCAL_STATIC_LIBRARIES := hciutils
LOCAL_CFLAGS += -DSTATIC_LINK_HCILIBS
endif
AR6K_PREBUILT_HCIUTILS_LIB :=

else
LOCAL_CFLAGS += -DCONFIG_NO_HCILIBS
endif

LOCAL_SHARED_LIBRARIES := \
	 	libdbus \
		libbluetooth \
		libcutils \
		libdl

ifeq ($(AR6K_PREBUILT_HCIUTILS_LIB),true)
LOCAL_STATIC_LIBRARIES := hciutils
LOCAL_CFLAGS += -DSTATIC_LINK_HCILIBS
endif
AR6K_PREBUILT_HCIUTILS_LIB :=

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	$(LOCAL_PATH)/../../../include \
	$(LOCAL_PATH)/../../../tools/athbtfilter/bluez \
	$(LOCAL_PATH)/../../../../include \
	$(LOCAL_PATH)/../../../os/linux/include \
        $(LOCAL_PATH)/../../../btfilter \
        $(LOCAL_PATH)/../../.. \
	$(call include-path-for, dbus) \
	$(call include-path-for, bluez-libs)

ifneq ($(PLATFORM_VERSION),$(filter $(PLATFORM_VERSION),1.5 1.6))
LOCAL_C_INCLUDES += external/bluetooth/bluez/include/bluetooth external/bluetooth/bluez/lib/bluetooth 
LOCAL_CFLAGS+=-DBLUEZ4_3
else
LOCAL_C_INCLUDES += external/bluez/libs/include/bluetooth
endif

LOCAL_CFLAGS+= \
	-DDBUS_COMPILATION -DABF_DEBUG


LOCAL_MODULE := abtfilt
LOCAL_MODULE_TAGS := debug eng optional
#LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)

include $(BUILD_EXECUTABLE)
