LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := system/core/init
LOCAL_CFLAGS := -Wall -DANDROID_TARGET=\"$(TARGET_BOARD_PLATFORM)\"
LOCAL_SRC_FILES := init_m8.cpp
LOCAL_MODULE := libinit_m8
include $(BUILD_STATIC_LIBRARY)
