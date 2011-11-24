ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	kernel/include \
	external/btfilter/host/btfilter/include \
	external/btfilter/include \
	external/btfilter/host/include \
	external/btfitler/host/os/linux/include \

LOCAL_SRC_FILES:= \
btfilter_action.c \
btfilter_core.c \

LOCAL_MODULE:=libbtfilt
LOCAL_SHARED_LIBRARIES += $(common_SHARED_LIBRARIES)


include $(BUILD_SHARED_LIBRARY)

endif
