LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	liblog libutils

LOCAL_C_INCLUDES := \
	hardware/libhardware/include

LOCAL_SRC_FILES := \
	rt5501.cpp \
	tfa9887.cpp \
	audio_amplifier.cpp

LOCAL_CFLAGS += \
	-DHAS_AUDIO_AMPLIFIER_IMPL

LOCAL_MODULE := libaudioamp

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
