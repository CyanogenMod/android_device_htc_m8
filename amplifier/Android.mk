LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	liblog libutils

LOCAL_C_INCLUDES := \
	hardware/libhardware/include

LOCAL_SRC_FILES := \
	rt5501.c \
	tfa9887.c \
	audio_amplifier.c

LOCAL_MODULE := audio_amplifier.msm8974
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional


LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
