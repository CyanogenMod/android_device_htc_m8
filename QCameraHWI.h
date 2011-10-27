/*
** Copyright (c) 2011 Code Aurora Forum. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_QCAMERA_HARDWARE_INTERFACE_H
#define ANDROID_HARDWARE_QCAMERA_HARDWARE_INTERFACE_H


#include <utils/threads.h>
#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>
#include <utils/threads.h>
#include <cutils/properties.h>

extern "C" {
#include <linux/android_pmem.h>
#include <linux/ion.h>
#include <camera.h>
#include <camera_defs_i.h>
#include <mm_camera_interface2.h>

#include "mm_jpeg_encoder.h"

} //extern C

#include "QCameraStream.h"
#include "QCameraHWI_Mem.h"

//Error codes
#define  NOT_FOUND -1

#define VIDEO_BUFFER_COUNT 8
#define PREVIEW_BUFFER_COUNT 4
#define MAX_ZOOM_RATIOS 62

#ifdef Q12
#undef Q12
#endif

#define Q12 4096
#define QCAMERA_PARM_ENABLE   1
#define QCAMERA_PARM_DISABLE  0

struct str_map {
    const char *const desc;
    int val;
};

typedef enum {
  CAMERA_STATE_UNINITED,
  CAMERA_STATE_READY,
  CAMERA_STATE_PREVIEW_START_CMD_SENT,
  CAMERA_STATE_PREVIEW_STOP_CMD_SENT,
  CAMERA_STATE_PREVIEW,
  CAMERA_STATE_RECORD_START_CMD_SENT,  /*5*/
  CAMERA_STATE_RECORD_STOP_CMD_SENT,
  CAMERA_STATE_RECORD,
  CAMERA_STATE_SNAP_START_CMD_SENT,
  CAMERA_STATE_SNAP_STOP_CMD_SENT,
  CAMERA_STATE_SNAP_CMD_ACKED,  /*10 - snapshot comd acked, snapshot not done yet*/
  CAMERA_STATE_ZSL_START_CMD_SENT,
  CAMERA_STATE_ZSL,
  CAMERA_STATE_AF_START_CMD_SENT,
  CAMERA_STATE_AF_STOP_CMD_SENT,
  CAMERA_STATE_ERROR, /*15*/

  /*Add any new state above*/
  CAMERA_STATE_MAX
} HAL_camera_state_type_t;


typedef enum {
  HAL_DUMP_FRM_PREVIEW = 1,
  HAL_DUMP_FRM_VIDEO = 1<<1,
  HAL_DUMP_FRM_MAIN = 1<<2,
  HAL_DUMP_FRM_THUMBNAIL = 1<<3,

  /*8 bits mask*/
  HAL_DUMP_FRM_MAX = 1 << 8
} HAL_cam_dump_frm_type_t;

#define HAL_DUMP_FRM_MASK_ALL ( HAL_DUMP_FRM_PREVIEW + HAL_DUMP_FRM_VIDEO + \
    HAL_DUMP_FRM_MAIN + HAL_DUMP_FRM_THUMBNAIL)

namespace android {

class QCameraStream;

class QCameraHardwareInterface : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    takeLiveSnapshot();

    virtual status_t          setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t          getBufferInfo( sp<IMemory>& Frame,
    size_t *alignedSize);
    void         getPictureSize(int *picture_width, int *picture_height) const;
    void         getPreviewSize(int *preview_width, int *preview_height) const;
    cam_format_t getPreviewFormat() const;

    virtual bool     useOverlay(void);
    virtual status_t setOverlay(const sp<Overlay> &overlay);

    virtual void release();

    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void        encodeData();

    void processEvent(mm_camera_event_t *);
    int  getJpegQuality() const;
    int  getNumOfSnapshots(void) const;
    int  getRemainingSnapshots(void);
    int  getThumbSizesFromAspectRatio(uint32_t aspect_ratio,
                                     int *picture_width,
                                     int *picture_height);
    bool isRawSnapshot();

    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    void                dumpFrameToFile(struct msm_frame*, HAL_cam_dump_frm_type_t);

    static sp<CameraHardwareInterface> createInstance(int, int);
    status_t setZSLLookBack(int mode, int value);
    void getZSLLookBack(int *mode, int *value);
    void setZSLEmptyQueueFlag(bool flag);
    void getZSLEmptyQueueFlag(bool *flag);

private:
                        QCameraHardwareInterface(int  cameraId, int);
    virtual             ~QCameraHardwareInterface();

    int16_t  zoomRatios[MAX_ZOOM_RATIOS];
    bool mUseOverlay;

    void initDefaultParameters();

    bool native_set_parms(mm_camera_parm_type_t type, uint16_t length, void *value);
    bool native_set_parms( mm_camera_parm_type_t type, uint16_t length, void *value, int *result);

    void hasAutoFocusSupport();
    void debugShowPreviewFPS() const;
    void prepareSnapshotAndWait();

    bool isPreviewRunning();
    bool isRecordingRunning();
    bool isSnapshotRunning();
    status_t storePreviewFrameForPostview(void);

    void processChannelEvent(mm_camera_ch_event_t *);
    void processPreviewChannelEvent(mm_camera_ch_event_type_t channelEvent);
    void processRecordChannelEvent(mm_camera_ch_event_type_t channelEvent);
    void processSnapshotChannelEvent(mm_camera_ch_event_type_t channelEvent);
    void processCtrlEvent(mm_camera_ctrl_event_t *);
    void processStatsEvent(mm_camera_stats_event_t *);
    void processInfoEvent(mm_camera_info_event_t *event);
    void processprepareSnapshotEvent(cam_ctrl_status_t *);
    void roiEvent(fd_roi_t roi);
    void zoomEvent(cam_ctrl_status_t *status);
    void autofocusevent(cam_ctrl_status_t *status);
    void handleZoomEventForPreview(void);
    void handleZoomEventForSnapshot(void);
    status_t autoFocusEvent(cam_ctrl_status_t *);

    void filterPictureSizes();
    bool supportsSceneDetection();
    bool supportsSelectableZoneAf();
    bool supportsFaceDetection();
    bool preview_parm_config (cam_ctrl_dimension_t* dim,CameraParameters& parm);

    void stopPreviewInternal();
    void stopRecordingInternal();
    void stopPreviewZSL();
    status_t cancelPictureInternal();
    status_t startPreviewZSL();

    status_t runFaceDetection();
    status_t setPictureSizeTable(void);
    status_t setPreviewSizeTable(void);
    status_t setPreviewSize(const CameraParameters& params);
    status_t setJpegThumbnailSize(const CameraParameters& params);
    status_t setPreviewFpsRange(const CameraParameters& params);
    status_t setPreviewFrameRate(const CameraParameters& params);
    status_t setPreviewFrameRateMode(const CameraParameters& params);
    status_t setRecordSize(const CameraParameters& params);
    status_t setPictureSize(const CameraParameters& params);
    status_t setJpegQuality(const CameraParameters& params);
    status_t setNumOfSnapshot(const CameraParameters& params);
    status_t setJpegRotation(void);
    status_t setAntibanding(const CameraParameters& params);
    status_t setEffect(const CameraParameters& params);
    status_t setExposureCompensation(const CameraParameters &params);
    status_t setAutoExposure(const CameraParameters& params);
    status_t setWhiteBalance(const CameraParameters& params);
    status_t setFlash(const CameraParameters& params);
    status_t setGpsLocation(const CameraParameters& params);
    status_t setRotation(const CameraParameters& params);
    status_t setZoom(const CameraParameters& params);
    status_t setFocusMode(const CameraParameters& params);
    status_t setBrightness(const CameraParameters& params);
    status_t setSkinToneEnhancement(const CameraParameters& params);
    status_t setOrientation(const CameraParameters& params);
    status_t setLensshadeValue(const CameraParameters& params);
    status_t setMCEValue(const CameraParameters& params);
    status_t setISOValue(const CameraParameters& params);
    status_t setPictureFormat(const CameraParameters& params);
    status_t setSharpness(const CameraParameters& params);
    status_t setContrast(const CameraParameters& params);
    status_t setSaturation(const CameraParameters& params);
    status_t setWaveletDenoise(const CameraParameters& params);
    status_t setSceneMode(const CameraParameters& params);
    status_t setContinuousAf(const CameraParameters& params);
    status_t setTouchAfAec(const CameraParameters& params);
    status_t setFaceDetection(const char *str);
    status_t setSceneDetect(const CameraParameters& params);
    status_t setStrTextures(const CameraParameters& params);
    status_t setPreviewFormat(const CameraParameters& params);
    status_t setSelectableZoneAf(const CameraParameters& params);
    status_t setOverlayFormats(const CameraParameters& params);
    status_t setHighFrameRate(const CameraParameters& params);
    status_t setRedeyeReduction(const CameraParameters& params);
    status_t setDenoise(const CameraParameters& params);
    status_t setHistogram(int histogram_en);

    isp3a_af_mode_t getAutoFocusMode(const CameraParameters& params);
    bool isValidDimension(int w, int h);

    String8 create_values_str(const str_map *values, int len);

    void setMyMode(int mode);
    bool isZSLMode();

    void freePictureTable(void);

    int32_t createPreview();
    int32_t createRecord();
    int32_t createSnapshot();

    int           mCameraId;
    camera_mode_t myMode;

    CameraParameters    mParameters;
    sp<Overlay>         mOverlay;
    int32_t             mMsgEnabled;

    notify_callback         mNotifyCb;
    data_callback           mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void                    *mCallbackCookie;

    sp<MemoryHeapBase>  mPreviewHeap;  //@Guru : Need to remove
    sp<AshmemPool>      mMetaDataHeap;

    mutable Mutex       mLock;
    //mutable Mutex       eventLock;
    Mutex         mCallbackLock;
    Mutex         mOverlayLock;
    Mutex         mAutofocusLock;
    Mutex         mMetaDataWaitLock;
    pthread_mutex_t     mAsyncCmdMutex;
    pthread_cond_t      mAsyncCmdWait;

    QCameraStream       *mStreamDisplay;
    QCameraStream       *mStreamRecord;
    QCameraStream       *mStreamSnap;

    cam_ctrl_dimension_t mDimension;
    int  previewWidth, previewHeight;
    int  videoWidth, videoHeight;
    int  maxSnapshotWidth, maxSnapshotHeight;
    int  mPreviewFormat;
    int  mFps;
    int  mDebugFps;
    int  mBrightness;
    int  mSkinToneEnhancement;
    int  mDenoiseValue;
    int  mHJR;
    int  mRotation;
    int  mTargetSmoothZoom;
    int  mSmoothZoomStep;
    int  mMaxZoom;
    int  mCurrentZoom;
    int  mSupportedPictureSizesCount;
    int  mFaceDetectOn;
    int  mDumpFrmCnt;
    int  mDumpSkipCnt;
    unsigned int mPictureSizeCount;
    unsigned int mPreviewSizeCount;

    bool mAutoFocusRunning;
    bool mMultiTouch;
    bool mHasAutoFocusSupport;
    bool mInitialized;
    bool mDisEnabled;
    bool strTexturesOn;
    bool mIs3DModeOn;
    bool mSmoothZoomRunning;
    bool mPreparingSnapshot;
    bool mParamStringInitialized;
    bool mZoomSupported;
    bool mSendMetaData;

/*for histogram*/
    int            mStatsOn;
    int            mCurrentHisto;
    bool           mSendData;
    sp<AshmemPool> mStatHeap;
    bool mZslLookBackMode;
    int mZslLookBackValue;
    bool mZslEmptyQueueFlag;
    String8 mEffectValues;
    String8 mIsoValues;
    String8 mSceneModeValues;
    String8 mSceneDetectValues;
    String8 mFocusModeValues;
    String8 mSelectableZoneAfValues;
    String8 mAutoExposureValues;
    String8 mWhitebalanceValues;
    String8 mAntibandingValues;
    String8 mFrameRateModeValues;
    String8 mTouchAfAecValues;
    String8 mPreviewSizeValues;
    String8 mPictureSizeValues;
    String8 mFlashValues;
    String8 mLensShadeValues;
    String8 mMceValues;
    String8 mHistogramValues;
    String8 mSkinToneEnhancementValues;
    String8 mPictureFormatValues;
    String8 mDenoiseValues;
    String8 mZoomRatioValues;
    String8 mPreviewFrameRateValues;
    String8 mPreviewFormatValues;
    String8 mFaceDetectionValues;
    String8 mHfrValues;
    String8 mHfrSizeValues;
    String8 mRedeyeReductionValues;
    String8 denoise_value;
    String8 mFpsRangesSupportedValues;

    friend class QCameraStream;
    friend class QCameraStream_record;
    friend class QCameraStream_preview;
    friend class QCameraStream_Snapshot;

    camera_size_type* mPictureSizes;
    camera_size_type* mPreviewSizes;
    const camera_size_type * mPictureSizesPtr;
    HAL_camera_state_type_t mCameraState;

     /* Temporary - can be removed after Honeycomb*/
#ifdef USE_ION
    sp<IonPool>  mPostPreviewHeap;
#else
    sp<PmemPool> mPostPreviewHeap;
#endif
};

}; // namespace android

#endif


