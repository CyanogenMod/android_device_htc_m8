ifneq ($(USE_CAMERA_STUB),true)
ifeq ($(strip $(BOARD_USES_QCOM_HARDWARE)), true)
BUILD_LIBCAMERA:=true
ifeq ($(BUILD_LIBCAMERA),true)

# When zero we link against libmmcamera; when 1, we dlopen libmmcamera.
DLOPEN_LIBMMCAMERA:=1

ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= QualcommCameraHardware.cpp

LOCAL_CFLAGS:= -DDLOPEN_LIBMMCAMERA=$(DLOPEN_LIBMMCAMERA)


ifeq "$(findstring msm7627,$(QCOM_TARGET_PRODUCT))" "msm7627"
LOCAL_CFLAGS+= -DNUM_PREVIEW_BUFFERS=6 -D_ANDROID_
else
LOCAL_CFLAGS+= -DNUM_PREVIEW_BUFFERS=4 -D_ANDROID_
endif

LOCAL_C_INCLUDES+= \
    $(TARGET_OUT_HEADERS)/mm-camera \
    $(TARGET_OUT_HEADERS)/mm-still/jpeg \

LOCAL_C_INCLUDES+= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/media

LOCAL_SHARED_LIBRARIES:= libutils libui libcamera_client liblog libcutils

LOCAL_SHARED_LIBRARIES+= libbinder
ifneq ($(DLOPEN_LIBMMCAMERA),1)
LOCAL_SHARED_LIBRARIES+= liboemcamera
else
LOCAL_SHARED_LIBRARIES+= libdl
endif

LOCAL_MODULE:= libcamera
include $(BUILD_SHARED_LIBRARY)

endif # BUILD_TINY_ANDROID
endif # BUILD_LIBCAMERA
endif # BOARD_USES_QCOM_HARDWARE
endif # USE_CAMERA_STUB
