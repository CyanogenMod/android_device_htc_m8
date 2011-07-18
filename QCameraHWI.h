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

extern "C" {
#include <linux/android_pmem.h>
#include <camera.h>
#include <camera_defs_i.h>
#include <mm_camera_interface2.h>

#include "mm_jpeg_encoder.h"

/*yyan: stream buffer type;
allocate by the QCameraStream class and pass to mm_cameara*/
typedef struct {
    int num;
    uint32_t frame_len;
    struct msm_frame frame[MM_CAMERA_MAX_NUM_FRAMES];
} mm_cameara_stream_buf_t;

} //extern C

#define VIDEO_BUFFER_COUNT 8
#define PREVIEW_BUFFER_COUNT 4

#ifdef Q12
#undef Q12
#endif

#define Q12 4096

#include "QCameraStream.h"
//#include "QCameraHWI_Mem.h"
struct str_map {
    const char *const desc;
    int val;
};

typedef enum {
    TARGET_MSM7625,
    TARGET_MSM7625A,
    TARGET_MSM7627,
    TARGET_MSM7627A,
    TARGET_QSD8250,
    TARGET_MSM7630,
    TARGET_MSM8660,
    TARGET_MSM8960,
    TARGET_MAX
}targetType;

struct board_property{
    targetType target;
    unsigned int previewSizeMask;
    bool hasSceneDetect;
    bool hasSelectableZoneAf;
    bool hasFaceDetect;
};
typedef enum {
  CAMERA_STATE_UNINITED,
  CAMERA_STATE_READY,
  CAMERA_STATE_PREVIEW_START_CMD_SENT,
  CAMERA_STATE_PREVIEW_STOP_CMD_SENT,
  CAMERA_STATE_PREVIEW,
  CAMERA_STATE_RECORD_START_CMD_SENT,
  CAMERA_STATE_RECORD_STOP_CMD_SENT,
  CAMERA_STATE_RECORD,
  CAMERA_STATE_SNAP_START_CMD_SENT,
  CAMERA_STATE_SNAP_STOP_CMD_SENT,
  CAMERA_STATE_SNAP_CMD_ACKED,  /*snapshot comd acked, snapshot not done yet*/
  CAMERA_STATE_ERROR,

  /*Add any new state above*/
  CAMERA_STATE_MAX
} HAL_camera_state_type_t;

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
    
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual status_t getBufferInfo( sp<IMemory>& Frame, size_t *alignedSize);
    virtual void     encodeData();
    virtual void release();

    static sp<CameraHardwareInterface> createInstance(mm_camera_t *, int);
    virtual status_t    takeLiveSnapshot();
    virtual bool useOverlay(void);
    virtual status_t setOverlay(const sp<Overlay> &overlay);
    void        useData(void*);
    void        processEvent(mm_camera_event_t *);
    int getJpegQuality() const;
    void getPictureSize(int *picture_width, int *picture_height) const;
    void getPreviewSize(int *preview_width, int *preview_height) const;
    int getThumbSizesFromAspectRatio(uint32_t aspect_ratio,
                                     int *picture_width,
                                     int *picture_height);
    bool isRawSnapshot();
    bool mUseOverlay;
    cam_format_t getPreviewFormat() const;
private:
                        QCameraHardwareInterface(mm_camera_t *, int);
    virtual             ~QCameraHardwareInterface();

    static const int kBufferCount = 4;


    void initBasicValues();
    void initDefaultParameters();




    bool native_set_parms(mm_camera_parm_type_t type, uint16_t length, void *value);
    bool native_set_parms( mm_camera_parm_type_t type, uint16_t length, void *value, int *result);

    void hasAutoFocusSupport();
    void debugShowPreviewFPS() const;
    void prepareSnapshotAndWait();

    bool isPreviewRunning();
    bool isRecordingRunning();
    bool isSnapshotRunning();

    void processChannelEvent(mm_camera_ch_event_t *);
    void processPreviewChannelEvent(mm_camera_ch_event_type_t channelEvent);
    void processRecordChannelEvent(mm_camera_ch_event_type_t channelEvent);
    void processSnapshotChannelEvent(mm_camera_ch_event_type_t channelEvent);

    void processCtrlEvent(mm_camera_ctrl_event_t *);
    void processStatsEvent(mm_camera_stats_t *);

    void processprepareSnapshotEvent(cam_ctrl_status_t *);
    status_t autoFocusEvent(cam_ctrl_status_t *);
    void filterPictureSizes();
    bool supportsSceneDetection();
    bool supportsSelectableZoneAf();
    bool supportsFaceDetection();
	bool preview_parm_config (cam_ctrl_dimension_t* dim,CameraParameters& parm);

    void stopPreviewInternal();
    void stopRecordingInternal();
    status_t cancelPictureInternal();

    status_t setPreviewSize(const CameraParameters& params);
    status_t setJpegThumbnailSize(const CameraParameters& params);
    status_t setPreviewFpsRange(const CameraParameters& params);
    status_t setPreviewFrameRate(const CameraParameters& params);
    status_t setPreviewFrameRateMode(const CameraParameters& params);
    status_t setRecordSize(const CameraParameters& params);
    status_t setPictureSize(const CameraParameters& params);
    status_t setJpegQuality(const CameraParameters& params);
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

    void zoomEvent(cam_ctrl_status_t *status);

    isp3a_af_mode_t getAutoFocusMode(const CameraParameters& params);
    bool isValidDimension(int w, int h);
    String8 create_values_str(const str_map *values, int len);

    mutable Mutex       mLock;
    Mutex mParametersLock;
    Mutex mCamframeTimeoutLock;
    bool camframe_timeout_flag;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryBase>      mBuffers[kBufferCount];

    mm_camera_t         *mmCamera;
    bool                mPreviewRunning;
    bool                mRecordRunning;
    int                 mPreviewFrameSize;
    bool                mAutoFocusRunning;

    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    // only used from PreviewThread
    int                 mCurrentPreviewFrame;

    // yyan mode passed in
    int                 mMode;
    QCameraStream       *mStreamDisplay;
    QCameraStream       *mStreamRecord;
    QCameraStream       *mStreamSnap;

     int mVideoWidth;
     int mVideoHeight;
     int mVideoBitrate;
     int mVideoFps;
     camera_mode_t        myMode;


     sp<Overlay>  mOverlay;

     Mutex mCallbackLock;
     Mutex mOverlayLock;

     /*mm_camera_reg_buf_t mRecordBuf;
     sp<PmemPool> mRecordHeap;
     struct msm_frame *recordframes;
     uint32_t record_offset[VIDEO_BUFFER_COUNT];
     Mutex mRecordFreeQueueLock;
     Vector<mm_camera_ch_data_buf_t> mRecordFreeQueue;
     int g_record_frame_len; //Need to remove*/

     cam_ctrl_dimension_t mDimension;

     int previewWidth, previewHeight;
     int videoWidth, videoHeight;

     bool mHasAutoFocusSupport;
     int mDebugFps;
     int mZslEnable;
     bool mDisEnabled;
     int maxSnapshotWidth;
     int maxSnapshotHeight;
     bool mInitialized;
     int mBrightness;
     int mSkinToneEnhancement;
     int mHJR;
     bool strTexturesOn;
     bool mIs3DModeOn;
     int mRotation;

     int mTargetSmoothZoom;
     int mSmoothZoomStep;
     int mCurrentZoom;
     bool mSmoothZoomRunning;
     bool mPreparingSnapshot;

     //For Face Detection
     int mFaceDetectOn;
     bool mSendMetaData;
     Mutex mMetaDataWaitLock;

     HAL_camera_state_type_t mCameraState;
     pthread_mutex_t mAsyncCmdMutex;
     pthread_cond_t mAsyncCmdWait;

     void setMyMode(int mode);

     friend class QCameraStream;
     friend class QCameraStream_record;
     friend class QCameraStream_preview;
     friend class QCameraStream_Snapshot;
     //cam_ctrl_dimension_t mDimension;
};

}; // namespace android

#endif


