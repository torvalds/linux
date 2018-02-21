LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES:= mmc.c mmc_cmds.c
LOCAL_SRC_FILES += 3rdparty/hmac_sha/sha2.c 3rdparty/hmac_sha/hmac_sha2.c
LOCAL_MODULE := mmc_utils
LOCAL_SHARED_LIBRARIES := libcutils libc
LOCAL_C_INCLUDES+= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/mmc-utils
include $(BUILD_EXECUTABLE)
