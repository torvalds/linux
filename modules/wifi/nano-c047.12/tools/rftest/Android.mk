LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    nrxtest.c \
    wlandutlib.c \
    

LOCAL_SHARED_LIBRARIES := \
	libcutils \

LOCAL_MODULE:= nrxtest

LOCAL_C_INCLUDES := \
	../../kernel/ic

LOCAL_PRELINK_MODULE := false

include $(BUILD_EXECUTABLE)
