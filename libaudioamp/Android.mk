LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	liblog libutils

LOCAL_SRC_FILES := \
	rt5501.cpp \
	tfa9887.cpp \
	audio_amplifier.cpp

LOCAL_MODULE := libaudioamp

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
