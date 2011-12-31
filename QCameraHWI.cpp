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

/*#error uncomment this for compiler test!*/

#define LOG_NDEBUG 0
#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHWI"
#include <utils/Log.h>
#include <utils/threads.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <sys/mman.h>


#include "QCameraHAL.h"
#include "QCameraHWI.h"


/* QCameraHardwareInterface class implementation goes here*/
/* following code implement the contol logic of this class*/

namespace android {

static void HAL_event_cb(mm_camera_event_t *evt, void *user_data)
{
  QCameraHardwareInterface *obj = (QCameraHardwareInterface *)user_data;
  if (obj) {
    obj->processEvent(evt);
  } else {
    LOGE("%s: NULL user_data", __func__);
  }
}

/* constructor */
QCameraHardwareInterface::
QCameraHardwareInterface(int cameraId, int mode)
                  : mCameraId(cameraId),
                    mParameters(),
                    mMsgEnabled(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    //mPreviewHeap(0),
                    mStreamDisplay (NULL), mStreamRecord(NULL), mStreamSnap(NULL),
                    mPreviewFormat(0),
                    mFps(0),
                    mDebugFps(0),
                    mMaxZoom(0),
                    mCurrentZoom(0),
                    mSupportedPictureSizesCount(15),
                    mDumpFrmCnt(0), mDumpSkipCnt(0),
                    mPictureSizeCount(15),
                    mPreviewSizeCount(13),
                    mAutoFocusRunning(false),
                    mHasAutoFocusSupport(false),
                    mInitialized(false),
                    mIs3DModeOn(0),
                    mSmoothZoomRunning(false),
                    mParamStringInitialized(false),
                    mZoomSupported(false),
                    mStatsOn(0), mCurrentHisto(-1), mSendData(false), mStatHeap(NULL),
                    mZslLookBackMode(ZSL_LOOK_BACK_MODE_TIME),
                    mZslLookBackValue(0),
                    mZslEmptyQueueFlag(FALSE),
                    mPictureSizes(NULL),
                    mCameraState(CAMERA_STATE_UNINITED),
                    mPostPreviewHeap(NULL),
                    mFaceDetectOn(0)

{
    LOGI("QCameraHardwareInterface: E");
    int32_t result = MM_CAMERA_E_GENERAL;
    char value[PROPERTY_VALUE_MAX];

    pthread_mutex_init(&mAsyncCmdMutex, NULL);
    pthread_cond_init(&mAsyncCmdWait, NULL);

    property_get("persist.debug.sf.showfps", value, "0");
    mDebugFps = atoi(value);
    mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;
    mPreviewWindow = NULL;
    property_get("camera.hal.fps", value, "0");
    mFps = atoi(value);

	LOGI("Init mPreviewState = %d", mPreviewState);

    property_get("persist.camera.hal.multitouchaf", value, "0");
    mMultiTouch = atoi(value);

    /* Open camera stack! */
    result=cam_ops_open(mCameraId, MM_CAMERA_OP_MODE_NOTUSED);
    if (result == MM_CAMERA_OK) {
      int i;
      mm_camera_event_type_t evt;
      for (i = 0; i < MM_CAMERA_EVT_TYPE_MAX; i++) {
        evt = (mm_camera_event_type_t) i;
        if (cam_evt_is_event_supported(mCameraId, evt)){
            cam_evt_register_event_notify(mCameraId,
              HAL_event_cb, (void *)this, evt);
        }
      }
    }
    LOGV("Cam open returned %d",result);
    if(MM_CAMERA_OK != result) {
          LOGE("startCamera: cam_ops_open failed: id = %d", mCameraId);
    } else {
      mCameraState = CAMERA_STATE_READY;

      /* Setup Picture Size and Preview size tables */
      setPictureSizeTable();
      LOGD("%s: Picture table size: %d", __func__, mPictureSizeCount);
      LOGD("%s: Picture table: ", __func__);
      for(unsigned int i=0; i < mPictureSizeCount;i++) {
          LOGD(" %d  %d", mPictureSizes[i].width, mPictureSizes[i].height);
      }

      setPreviewSizeTable();
      LOGD("%s: Preview table size: %d", __func__, mPreviewSizeCount);
      LOGD("%s: Preview table: ", __func__);
      for(unsigned int i=0; i < mPreviewSizeCount;i++) {
          LOGD(" %d  %d", mPreviewSizes[i].width, mPreviewSizes[i].height);
      }

      /* set my mode - update myMode member variable due to difference in
         enum definition between upper and lower layer*/
      setMyMode(mode);
      initDefaultParameters();
    }
    LOGI("QCameraHardwareInterface: X");
}


QCameraHardwareInterface::~QCameraHardwareInterface()
{
    LOGI("~QCameraHardwareInterface: E");
    int result;

    switch(mPreviewState) {
    case QCAMERA_HAL_PREVIEW_STOPPED:
        break;
    case QCAMERA_HAL_PREVIEW_START:
        break;
    case QCAMERA_HAL_PREVIEW_STARTED:
        stopPreview();
    break;
    case QCAMERA_HAL_RECORDING_STARTED:
        stopRecordingInternal();
        stopPreview();
        break;
    case QCAMERA_HAL_TAKE_PICTURE:
        cancelPictureInternal();
        break;
    default:
        break;
    }
    mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;

    freePictureTable();
    if(mStatHeap != NULL) {
      mStatHeap.clear( );
      mStatHeap = NULL;
    }

    if(mStreamDisplay){
        QCameraStream_preview::deleteInstance (mStreamDisplay);
        mStreamDisplay = NULL;
    }
    if(mStreamRecord) {
        QCameraStream_record::deleteInstance (mStreamRecord);
        mStreamRecord = NULL;
    }
    if(mStreamSnap) {
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
        mStreamSnap = NULL;
    }
    cam_ops_close(mCameraId);
    LOGI("~QCameraHardwareInterface: X");
}

void QCameraHardwareInterface::release()
{
    LOGI("release: E");
    Mutex::Autolock l(&mLock);

    switch(mPreviewState) {
    case QCAMERA_HAL_PREVIEW_STOPPED:
        break;
    case QCAMERA_HAL_PREVIEW_START:
        break;
    case QCAMERA_HAL_PREVIEW_STARTED:
        stopPreview();
    break;
    case QCAMERA_HAL_RECORDING_STARTED:
        stopRecordingInternal();
        stopPreview();
        break;
    case QCAMERA_HAL_TAKE_PICTURE:
        cancelPictureInternal();
        break;
    default:
        break;
    }
#if 0
    if (isRecordingRunning()) {
        stopRecordingInternal();
        LOGI("release: stopRecordingInternal done.");
    }
    if (isPreviewRunning()) {
        stopPreview(); //stopPreviewInternal();
        LOGI("release: stopPreviewInternal done.");
    }
    if (isSnapshotRunning()) {
        cancelPictureInternal();
        LOGI("release: cancelPictureInternal done.");
    }
    if (mCameraState == CAMERA_STATE_ERROR) {
        //TBD: If Error occurs then tear down
        LOGI("release: Tear down.");
    }
#endif
    mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;
    LOGI("release: X");
}

void QCameraHardwareInterface::setCallbacks(
    camera_notify_callback notify_cb,
    camera_data_callback data_cb,
    camera_data_timestamp_callback data_cb_timestamp,
    camera_request_memory get_memory,
    void *user)
{
    LOGE("setCallbacks: E");
    Mutex::Autolock lock(mLock);
    mNotifyCb        = notify_cb;
    mDataCb          = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemory       = get_memory;
    mCallbackCookie  = user;
    LOGI("setCallbacks: X");
}

void QCameraHardwareInterface::enableMsgType(int32_t msgType)
{
    LOGI("enableMsgType: E");
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
    LOGI("enableMsgType: X");
}

void QCameraHardwareInterface::disableMsgType(int32_t msgType)
{
    LOGI("disableMsgType: E");
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
    LOGI("disableMsgType: X");
}

int QCameraHardwareInterface::msgTypeEnabled(int32_t msgType)
{
    LOGI("msgTypeEnabled: E");
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
    LOGI("msgTypeEnabled: X");
}
#if 0
status_t QCameraHardwareInterface::dump(int fd, const Vector<String16>& args) const
{
    LOGI("dump: E");
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);
    write(fd, result.string(), result.size());
    LOGI("dump: E");
    return NO_ERROR;
}
#endif

int QCameraHardwareInterface::dump(int fd)
{
    LOGE("%s: not supported yet", __func__);
    return -1;
}

status_t QCameraHardwareInterface::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    LOGI("sendCommand: E");
    status_t rc = NO_ERROR;
    Mutex::Autolock l(&mLock);

    switch (command) {
        case CAMERA_CMD_HISTOGRAM_ON:
            LOGE("histogram set to on");
            rc = setHistogram(1);
            break;
        case CAMERA_CMD_HISTOGRAM_OFF:
            LOGE("histogram set to off");
            rc = setHistogram(0);
            break;
        case CAMERA_CMD_HISTOGRAM_SEND_DATA:
            LOGE("histogram send data");
            mSendData = true;
            rc = NO_ERROR;
            break;
        case CAMERA_CMD_START_FACE_DETECTION:
           if(supportsFaceDetection() == false){
                LOGE("Face detection support is not available");
                return NO_ERROR;
           }
           //setFaceDetection("on");
           return runFaceDetection();
        case CAMERA_CMD_STOP_FACE_DETECTION:
           if(supportsFaceDetection() == false){
                LOGE("Face detection support is not available");
                return NO_ERROR;
           }
           //setFaceDetection("off");
           return runFaceDetection();
#if 0
        case CAMERA_CMD_SEND_META_DATA:
           mMetaDataWaitLock.lock();
           if(mFaceDetectOn == true) {
               mSendMetaData = true;
           }
           mMetaDataWaitLock.unlock();
           return NO_ERROR;
#endif
#if 0 /* To Do: will enable it later */
        case CAMERA_CMD_START_SMOOTH_ZOOM :
            LOGV("HAL sendcmd start smooth zoom %d %d", arg1 , arg2);
            /*TO DO: get MaxZoom from parameter*/
            int MaxZoom = 100;

            switch(mCameraState ) {
                case CAMERA_STATE_PREVIEW:
                case CAMERA_STATE_RECORD_CMD_SENT:
                case CAMERA_STATE_RECORD:
                    mTargetSmoothZoom = arg1;
                    mCurrentZoom = mParameters.getInt("zoom");
                    mSmoothZoomStep = (mCurrentZoom > mTargetSmoothZoom)? -1: 1;
                   if(mCurrentZoom == mTargetSmoothZoom) {
                        LOGV("Smoothzoom target zoom value is same as "
                        "current zoom value, return...");
                        mNotifyCallback(CAMERA_MSG_ZOOM,
                        mCurrentZoom, 1, mCallbackCookie);
                    } else if(mCurrentZoom < 0 || mCurrentZoom > MaxZoom ||
                        mTargetSmoothZoom < 0 || mTargetSmoothZoom > MaxZoom)  {
                        LOGE(" ERROR : beyond supported zoom values, break..");
                        mNotifyCallback(CAMERA_MSG_ZOOM,
                        mCurrentZoom, 0, mCallbackCookie);
                    } else {
                        mSmoothZoomRunning = true;
                        mCurrentZoom += mSmoothZoomStep;
                        if ((mSmoothZoomStep < 0 && mCurrentZoom < mTargetSmoothZoom)||
                        (mSmoothZoomStep > 0 && mCurrentZoom > mTargetSmoothZoom )) {
                            mCurrentZoom = mTargetSmoothZoom;
                        }
                        mParameters.set("zoom", mCurrentZoom);
                        setZoom(mParameters);
                    }
                    break;
                default:
                    LOGV(" No preview, no smoothzoom ");
                    break;
            }
            rc = NO_ERROR;
            break;

        case CAMERA_CMD_STOP_SMOOTH_ZOOM:
            if(mSmoothZoomRunning) {
                mSmoothZoomRunning = false;
                /*To Do: send cmd to stop zooming*/
            }
            LOGV("HAL sendcmd stop smooth zoom");
            rc = NO_ERROR;
            break;
#endif
        default:
            break;
    }
    LOGI("sendCommand: X");
    return rc;
}

void QCameraHardwareInterface::setMyMode(int mode)
{
    LOGI("setMyMode: E");
    if (mode & CAMERA_SUPPORT_MODE_3D) {
        myMode = CAMERA_MODE_3D;
    }else {
        /* default mode is 2D */
        myMode = CAMERA_MODE_2D;
    }

    if (mode & CAMERA_SUPPORT_MODE_ZSL) {
        myMode = (camera_mode_t)(myMode |CAMERA_ZSL_MODE);
    }else {
        myMode = (camera_mode_t) (myMode | CAMERA_NONZSL_MODE);
    }
    LOGI("setMyMode: Set mode to %d (passed mode: %d)", myMode, mode);
    LOGI("setMyMode: X");
}
/* static factory function */
QCameraHardwareInterface *QCameraHardwareInterface::createInstance(int cameraId, int mode)
{
    LOGI("createInstance: E");
    QCameraHardwareInterface *cam = new QCameraHardwareInterface(cameraId, mode);
    if (cam ) {
      if (cam->mCameraState != CAMERA_STATE_READY) {
        LOGE("createInstance: Failed");
        delete cam;
        cam = NULL;
      }
    }

    if (cam) {
      //sp<CameraHardwareInterface> hardware(cam);
      LOGI("createInstance: X");
      return cam;
    } else {
      return NULL;
    }
}
/* external plug in function */
extern "C" void *
QCameraHAL_openCameraHardware(int  cameraId, int mode)
{
    LOGI("QCameraHAL_openCameraHardware: E");
    return (void *) QCameraHardwareInterface::createInstance(cameraId, mode);
}

#if 0
bool QCameraHardwareInterface::useOverlay(void)
{
    LOGI("useOverlay: E");
    mUseOverlay = TRUE;
    LOGI("useOverlay: X");
    return mUseOverlay;
}
#endif

bool QCameraHardwareInterface::isPreviewRunning() {
    LOGI("isPreviewRunning: E");
    bool ret = false;
    LOGI("isPreviewRunning: camera state:%d", mCameraState);

    if((mCameraState == CAMERA_STATE_PREVIEW) ||
       (mCameraState == CAMERA_STATE_PREVIEW_START_CMD_SENT) ||
       (mCameraState == CAMERA_STATE_RECORD) ||
       (mCameraState == CAMERA_STATE_RECORD_START_CMD_SENT) ||
       (mCameraState == CAMERA_STATE_ZSL) ||
       (mCameraState == CAMERA_STATE_ZSL_START_CMD_SENT)){
       return true;
    }
    LOGI("isPreviewRunning: X");
    return ret;
}

bool QCameraHardwareInterface::isRecordingRunning() {
    LOGE("isRecordingRunning: E");
    bool ret = false;
    if(QCAMERA_HAL_RECORDING_STARTED == mPreviewState)
      ret = true;
    //if((mCameraState == CAMERA_STATE_RECORD) ||
    //   (mCameraState == CAMERA_STATE_RECORD_START_CMD_SENT)) {
    //   return true;
    //}
    LOGE("isRecordingRunning: X");
    return ret;
}

bool QCameraHardwareInterface::isSnapshotRunning() {
    LOGI("isSnapshotRunning: E");
    bool ret = false;
    //if((mCameraState == CAMERA_STATE_SNAP_CMD_ACKED) ||
    //   (mCameraState == CAMERA_STATE_SNAP_START_CMD_SENT)) {
    //    return true;
    //}
    switch(mPreviewState) {
    case QCAMERA_HAL_PREVIEW_STOPPED:
    case QCAMERA_HAL_PREVIEW_START:
    case QCAMERA_HAL_PREVIEW_STARTED:
    case QCAMERA_HAL_RECORDING_STARTED:
    default:
        break;
    case QCAMERA_HAL_TAKE_PICTURE:
        ret = true;
        break;
    }
    LOGI("isSnapshotRunning: X");
    return ret;
}

bool QCameraHardwareInterface::isZSLMode() {
#if 1
    return (myMode & CAMERA_ZSL_MODE);
#else
    return 1;
#endif
}

void QCameraHardwareInterface::debugShowPreviewFPS() const
{
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (diff > ms2ns(250)) {
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        LOGI("Preview Frames Per Second: %.4f", mFps);
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}

void QCameraHardwareInterface::
processPreviewChannelEvent(mm_camera_ch_event_type_t channelEvent) {
    LOGI("processPreviewChannelEvent: E");
    switch(channelEvent) {
        case MM_CAMERA_CH_EVT_STREAMING_ON:
            mCameraState =
                isZSLMode() ? CAMERA_STATE_ZSL : CAMERA_STATE_PREVIEW;
            break;
        case MM_CAMERA_CH_EVT_STREAMING_OFF:
            mCameraState = CAMERA_STATE_READY;
            break;
        case MM_CAMERA_CH_EVT_DATA_DELIVERY_DONE:
            break;
        default:
            break;
    }
    LOGI("processPreviewChannelEvent: X");
    return;
}

void QCameraHardwareInterface::processRecordChannelEvent(mm_camera_ch_event_type_t channelEvent) {
    LOGI("processRecordChannelEvent: E");
    switch(channelEvent) {
        case MM_CAMERA_CH_EVT_STREAMING_ON:
            mCameraState = CAMERA_STATE_RECORD;
            break;
        case MM_CAMERA_CH_EVT_STREAMING_OFF:
            mCameraState = CAMERA_STATE_PREVIEW;
            break;
        case MM_CAMERA_CH_EVT_DATA_DELIVERY_DONE:
            break;
        default:
            break;
    }
    LOGI("processRecordChannelEvent: X");
    return;
}

void QCameraHardwareInterface::
processSnapshotChannelEvent(mm_camera_ch_event_type_t channelEvent) {
    LOGI("processSnapshotChannelEvent: E");
    switch(channelEvent) {
        case MM_CAMERA_CH_EVT_STREAMING_ON:
            mCameraState =
                isZSLMode() ? CAMERA_STATE_ZSL : CAMERA_STATE_SNAP_CMD_ACKED;
            break;
        case MM_CAMERA_CH_EVT_STREAMING_OFF:
            mCameraState = CAMERA_STATE_READY;
            break;
        case MM_CAMERA_CH_EVT_DATA_DELIVERY_DONE:
            break;
        case MM_CAMERA_CH_EVT_DATA_REQUEST_MORE:
            if (isZSLMode()) {
                /* ZSL Mode: In ZSL Burst Mode, users may request for number of
                snapshots larger than internal size of ZSL queue. So we'll need
                process the remaining frames as they become available.
                In such case, we'll get this event */
                mStreamSnap->takePictureZSL();
            }
            break;
        default:
            break;
    }
    LOGI("processSnapshotChannelEvent: X");
    return;
}

void QCameraHardwareInterface::processChannelEvent(mm_camera_ch_event_t *event)
{
    LOGI("processChannelEvent: E");
    Mutex::Autolock lock(mLock);
    switch(event->ch) {
        case MM_CAMERA_CH_PREVIEW:
            processPreviewChannelEvent(event->evt);
            break;
        case MM_CAMERA_CH_VIDEO:
            processRecordChannelEvent(event->evt);
            break;
        case MM_CAMERA_CH_SNAPSHOT:
            processSnapshotChannelEvent(event->evt);
            break;
        default:
            break;
    }
    LOGI("processChannelEvent: X");
    return;
}

void QCameraHardwareInterface::processCtrlEvent(mm_camera_ctrl_event_t *event)
{
    LOGI("processCtrlEvent: %d, E",event->evt);
    Mutex::Autolock lock(mLock);
    switch(event->evt)
    {
        case MM_CAMERA_CTRL_EVT_ZOOM_DONE:
            zoomEvent(&event->status);
            break;
        case MM_CAMERA_CTRL_EVT_AUTO_FOCUS_DONE:
            autoFocusEvent(&event->status);
            break;
        case MM_CAMERA_CTRL_EVT_PREP_SNAPSHOT:
            break;
        default:
            break;
    }
    LOGI("processCtrlEvent: X");
    return;
}

void  QCameraHardwareInterface::processStatsEvent(mm_camera_stats_event_t *event)
{
    LOGI("processStatsEvent: E");
    //Mutex::Autolock lock(eventLock); //Guru
    if (!isPreviewRunning( )) {
        LOGE("preview is not running");
        return;
    }

    switch (event->event_id) {
        case MM_CAMERA_STATS_EVT_HISTO:
        {
            LOGE("HAL process Histo: mMsgEnabled=0x%x, mStatsOn=%d, mSendData=%d, mDataCb=%p ",
            (mMsgEnabled & CAMERA_MSG_STATS_DATA), mStatsOn, mSendData, mDataCb);
            int msgEnabled = mMsgEnabled;
            camera_preview_histogram_info* hist_info =
                (camera_preview_histogram_info*) event->e.stats_histo.histo_info;

            if(mStatsOn == QCAMERA_PARM_ENABLE && mSendData &&
                            mDataCb && (msgEnabled & CAMERA_MSG_STATS_DATA) ) {
                uint32_t *dest;
                mSendData = false;
                mCurrentHisto = (mCurrentHisto + 1) % 3;
                // The first element of the array will contain the maximum hist value provided by driver.
                *(uint32_t *)((unsigned int)(mStatsMapped[mCurrentHisto]->data)) = hist_info->max_value;
                memcpy((uint32_t *)((unsigned int)mStatsMapped[mCurrentHisto]->data + sizeof(int32_t)),
                                                    (uint32_t *)hist_info->buffer,(sizeof(int32_t) * 256));
                mDataCb(CAMERA_MSG_STATS_DATA, mStatsMapped[mCurrentHisto], 0, NULL, (void*)mCallbackCookie);
            }
            break;
        }
        default:
        break;
    }
  LOGV("receiveCameraStats X");
}

void  QCameraHardwareInterface::processInfoEvent(mm_camera_info_event_t *event) {
    LOGI("processInfoEvent: %d, E",event->event_id);
    //Mutex::Autolock lock(eventLock);
    switch(event->event_id)
    {
        case MM_CAMERA_INFO_EVT_ROI:
            roiEvent(event->e.roi);
            break;
        default:
            break;
    }
    LOGI("processInfoEvent: X");
    return;
}

void  QCameraHardwareInterface::processEvent(mm_camera_event_t *event)
{
    LOGI("processEvent: type :%d E",event->event_type);
    switch(event->event_type)
    {
        case MM_CAMERA_EVT_TYPE_CH:
            processChannelEvent(&event->e.ch);
            break;
        case MM_CAMERA_EVT_TYPE_CTRL:
            processCtrlEvent(&event->e.ctrl);
            break;
        case MM_CAMERA_EVT_TYPE_STATS:
            processStatsEvent(&event->e.stats);
            break;
        case MM_CAMERA_EVT_TYPE_INFO:
            processInfoEvent(&event->e.info);
            break;
        default:
            break;
    }
    LOGI("processEvent: X");
    return;
}

bool QCameraHardwareInterface::preview_parm_config (cam_ctrl_dimension_t* dim,
                                   CameraParameters& parm)
{
    LOGI("preview_parm_config: E");
    bool matching = true;
    int display_width = 0;  /* width of display      */
    int display_height = 0; /* height of display */
    uint16_t video_width = 0;  /* width of the video  */
    uint16_t video_height = 0; /* height of the video */
    const char *str = parm.getPreviewFormat();

    /* First check if the preview resolution is the same, if not, change it*/
    parm.getPreviewSize(&display_width,  &display_height);
    if (display_width && display_height) {
        matching = (display_width == dim->display_width) &&
            (display_height == dim->display_height);

        if (!matching) {
            dim->display_width  = display_width;
            dim->display_height = display_height;
        }
    }
    else
        matching = false;

    cam_format_t value = getPreviewFormat();

    if(value != NOT_FOUND && value != dim->prev_format ) {
        //Setting to Parameter requested by the Upper layer
        dim->prev_format = value;
    }else{
        //Setting to default Format.
        dim->prev_format = CAMERA_YUV_420_NV21;
    }
    dim->prev_padding_format =  getPreviewPadding( );

    dim->enc_format = CAMERA_YUV_420_NV12;
    dim->orig_video_width = mDimension.orig_video_width;
    dim->orig_video_height = mDimension.orig_video_height;
    dim->video_width = mDimension.video_width;
    dim->video_height = mDimension.video_height;
    dim->video_chroma_width = mDimension.video_width;
    dim->video_chroma_height  = mDimension.video_height;

    LOGI("preview_parm_config: X");
    return matching;
}

status_t QCameraHardwareInterface::startPreview()
{
    status_t retVal = NO_ERROR;

    LOGE("%s: mPreviewState =%d", __func__, mPreviewState);
    switch(mPreviewState) {
    case QCAMERA_HAL_PREVIEW_STOPPED:
        mPreviewState = QCAMERA_HAL_PREVIEW_START;
            LOGE("%s:  HAL::startPreview begin", __func__);

        if(QCAMERA_HAL_PREVIEW_START == mPreviewState && mPreviewWindow) {
            LOGE("%s:  start preview now", __func__);
            retVal = startPreview2();
            if(retVal == NO_ERROR)
                mPreviewState = QCAMERA_HAL_PREVIEW_STARTED;
        } else {
            LOGE("%s:  received startPreview, but preview window = null", __func__);
        }
        break;
    case QCAMERA_HAL_PREVIEW_START:
    case QCAMERA_HAL_PREVIEW_STARTED:
    break;
    case QCAMERA_HAL_RECORDING_STARTED:
        LOGE("%s: cannot start preview in recording state", __func__);
        break;
    case QCAMERA_HAL_TAKE_PICTURE:
        LOGE("%s: cannot start preview in SNAPSHOT state", __func__);
        //mStreamSnap->release( );
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
        mStreamSnap = NULL;
        mPreviewState = QCAMERA_HAL_PREVIEW_START;
        retVal = startPreview2();
        if(retVal == NO_ERROR)
            mPreviewState = QCAMERA_HAL_PREVIEW_STARTED;
        break;
    default:
        LOGE("%s: unknow state %d received", __func__, mPreviewState);
        retVal = UNKNOWN_ERROR;
        break;
    }
    return retVal;
}

status_t QCameraHardwareInterface::startPreview2()
{
    LOGI("startPreview2: E");
    Mutex::Autolock lock(mLock);
    status_t ret = NO_ERROR;

    cam_ctrl_dimension_t dim;
    bool initPreview = false;

    if (mPreviewState == QCAMERA_HAL_PREVIEW_STARTED) { //isPreviewRunning()){
        LOGE("%s:Preview already started  mCameraState = %d!", __func__, mCameraState);
        LOGE("%s: X", __func__);
        return NO_ERROR;
    }
    /* rest the preview memory struct */
    mPreviewMemoryLock.lock();
    memset(&mPreviewMemory, 0, sizeof(mPreviewMemory));
    mPreviewMemoryLock.unlock();
    /* TODO: need to do later after bring up */
#if 0 //mzhu
    if (isZSLMode()) {
        return startPreviewZSL();
    }
#endif //mzhu

    /*  get existing preview information, by qury mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);

    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get preview dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    /* config the parmeters and see if we need to re-init the stream*/
    initPreview = preview_parm_config (&dim, mParameters);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't config preview parms!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

	//if(NO_ERROR != getBufferFromSurfaceTextureClient()) {
    //    return BAD_VALUE;
	//}
    //if (mPrevForPostviewBuf.frame[0].buffer) {
    //    mm_camera_do_munmap(mPrevForPostviewBuf.frame[0].fd,
    //                        (void *)mPrevForPostviewBuf.frame[0].buffer,
    //                        mPrevForPostviewBuf.frame_len);
    //    memset(&mPrevForPostviewBuf, 0, sizeof(mPrevForPostviewBuf));
    //}
    /* config the parmeters and see if we need to re-init the stream*/

    /*if stream object exists but it needs to re-init, delete it now.
       otherwise just call mPreviewStream->start() later ,
      */
    if (mStreamDisplay && initPreview){
      LOGE("%s:Deleting old stream instance",__func__);
      QCameraStream_preview::deleteInstance (mStreamDisplay);
      mStreamDisplay = NULL;
    }

    /*if stream object doesn't exists, create and init it now.
       and call mPreviewStream->start() later ,
      */
    if (!mStreamDisplay){
		if(!isZSLMode())
            mStreamDisplay = QCameraStream_preview::createInstance(mCameraId,
                                                      CAMERA_MODE_2D);
        else {
            mStreamDisplay = QCameraStream_preview::createInstance(mCameraId,
	                                                  CAMERA_ZSL_MODE);
	        mStreamSnap = QCameraStream_Snapshot::createInstance(mCameraId,
                                                         CAMERA_ZSL_MODE);
            if (!mStreamSnap) {
                LOGE("%s: error - can't creat snapshot stream!", __func__);
            }
		}
      LOGE("%s:Creating new stream instance",__func__);
    }

    // pass HAL HWI obj to preview obj
    mStreamDisplay->setHALCameraControl(this);
	LOGE("%s: setPreviewWindow", __func__);
	mStreamDisplay->setPreviewWindow(mPreviewWindow);

    /*now init all the buffers and send to steam object*/
    ret = mStreamDisplay->init();
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't init preview stream!", __func__);
      LOGE("%s: X", __func__);
      /*shall we delete it? */
      return BAD_VALUE;
    }

    if(isZSLMode()) {
        /* Store HAL object in snapshot stream Object */
        mStreamSnap->setHALCameraControl(this);

        /* Call snapshot init*/
        ret =  mStreamSnap->init();
        if (MM_CAMERA_OK != ret){
            LOGE("%s: error - can't init Snapshot stream!", __func__);
            return BAD_VALUE;
        }

		/* Start preview streaming */
		ret = mStreamDisplay->start();
		if (MM_CAMERA_OK != ret){
		  LOGE("%s: error - can't start nonZSL stream!", __func__);
		  LOGE("%s: X", __func__);
		  return BAD_VALUE;
		}

		/* Start ZSL stream */
		ret =  mStreamSnap->start();
		if (MM_CAMERA_OK != ret){
			LOGE("%s: error - can't start Snapshot stream!", __func__);
			return BAD_VALUE;
		}
    } else
        ret = mStreamDisplay->start();

    /*call QCameraStream_noneZSL::start() */
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't start nonZSL stream!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }
    if(MM_CAMERA_OK == ret)
        mCameraState = CAMERA_STATE_PREVIEW_START_CMD_SENT;
    else
        mCameraState = CAMERA_STATE_ERROR;

    if(mPostPreviewHeap != NULL) {
        mPostPreviewHeap.clear();
        mPostPreviewHeap = NULL;
    }

    LOGI("startPreview: X");
    return ret;
}

#if 0 // mzhu
status_t QCameraHardwareInterface::startPreviewZSL()
{
    LOGI("startPreviewZSL: E");
    status_t ret = NO_ERROR;

    cam_ctrl_dimension_t dim;
    bool initPreview = false;

    if (isPreviewRunning()){
        LOGE("%s:Preview already started  mCameraState = %d!", __func__, mCameraState);
        LOGE("%s: X", __func__);
        return NO_ERROR;
    }

    /* Get existing preview information by querying mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);

    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get preview dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    /* config the parmeters and see if we need to re-init the stream*/
    initPreview = preview_parm_config (&dim, mParameters);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't config preview parms!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    /*If stream object exists but it needs to re-init, delete it now.
       otherwise just call mPreviewStream->start() later ,
    */
    if (mStreamDisplay && initPreview){
      LOGD("%s:Deleting old stream instance",__func__);
      QCameraStream_preview::deleteInstance (mStreamDisplay);
      mStreamDisplay = NULL;
    }

    /* If stream object doesn't exists, create and init it now.
       and call mPreviewStream->start() later ,
    */
    if (!mStreamDisplay){
      mStreamDisplay = QCameraStream_preview::createInstance(mCameraId,
                                                             CAMERA_ZSL_MODE);
      LOGD("%s:Creating new stream instance",__func__);
    }

    if (!mStreamDisplay) {
      LOGE("%s: error - can't creat preview stream!", __func__);
      return BAD_VALUE;
    }

    mStreamDisplay->setHALCameraControl(this);
	mStreamDisplay->setPreviewWindow(mPreviewWindow);

    /* Init all the buffers and send to steam object*/
    ret = mStreamDisplay->init();
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't init preview stream!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    if (mStreamSnap){
        LOGD("%s:Deleting old Snapshot stream instance",__func__);
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
        mStreamSnap = NULL;
    }

    /*
     * Creating Instance of snapshot stream.
     */
    mStreamSnap = QCameraStream_Snapshot::createInstance(mCameraId,
                                                         CAMERA_ZSL_MODE);

    if (!mStreamSnap) {
        LOGE("%s: error - can't creat snapshot stream!", __func__);
        return BAD_VALUE;
    }

    /* Store HAL object in snapshot stream Object */
    mStreamSnap->setHALCameraControl(this);

    /* Call prepareSnapshot before stopping preview */
    mStreamSnap->prepareHardware();

    /* Call snapshot init*/
    ret =  mStreamSnap->init();
    if (MM_CAMERA_OK != ret){
        LOGE("%s: error - can't init Snapshot stream!", __func__);
        return BAD_VALUE;
    }

    /* Start preview streaming */
    ret = mStreamDisplay->start();
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't start nonZSL stream!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    /* Start ZSL stream */
    ret =  mStreamSnap->start();
    if (MM_CAMERA_OK != ret){
        LOGE("%s: error - can't start Snapshot stream!", __func__);
        return BAD_VALUE;
    }

    if(MM_CAMERA_OK == ret)
        mCameraState = CAMERA_STATE_ZSL_START_CMD_SENT;
    else
        mCameraState = CAMERA_STATE_ERROR;

    LOGI("startPreviewZSL: setting state: %d", mCameraState);
    LOGI("startPreviewZSL: X");
    return ret;
}
#endif //mzhu

void QCameraHardwareInterface::stopPreview()
{
    LOGI("%s: stopPreview: E", __func__);
    Mutex::Autolock lock(mLock);
    mFaceDetectOn = false;
	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_START:
		//mPreviewWindow = NULL;
		mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;
	    break;
    case QCAMERA_HAL_PREVIEW_STARTED:
//#if 0 //mzhu
 //       if (isZSLMode()) {
 //           stopPreviewZSL();
  //      }
  //      else {
  //          stopPreviewInternal();
  //      }
//#else
        stopPreviewInternal();
//#endif //mzhu
		mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;
		break;

    case QCAMERA_HAL_RECORDING_STARTED:
        stopRecordingInternal();
        stopPreview();
        mPreviewState = QCAMERA_HAL_PREVIEW_STOPPED;
        break;
	case QCAMERA_HAL_TAKE_PICTURE:
	case QCAMERA_HAL_PREVIEW_STOPPED:
		default:
		break;
	}
    LOGI("stopPreview: X, mPreviewState = %d", mPreviewState);
}

#if 0 //mzhu
void QCameraHardwareInterface::stopPreviewZSL()
{
    LOGI("stopPreviewZSL: E");

    if(!mStreamDisplay || !mStreamSnap) {
        LOGE("mStreamDisplay/mStreamSnap is null");
        return;
    }

    /* Stop Preview streaming */
    mStreamDisplay->stop();

    /* Stop ZSL snapshot channel streaming*/
    mStreamSnap->stop();

    /* Realease all the resources*/
    mStreamDisplay->release();
    mStreamSnap->release();

    /* Delete mStreamDisplay & mStreamSnap instance*/
    QCameraStream_Snapshot::deleteInstance (mStreamSnap);
    mStreamSnap = NULL;
    QCameraStream_preview::deleteInstance (mStreamDisplay);
    mStreamDisplay = NULL;

    mCameraState = CAMERA_STATE_PREVIEW_STOP_CMD_SENT;

    LOGI("stopPreviewZSL: X");
}
#endif //mzhu

void QCameraHardwareInterface::stopPreviewInternal()
{
    LOGI("stopPreviewInternal: E");
    status_t ret = NO_ERROR;

    if(!mStreamDisplay) {
        LOGE("mStreamDisplay is null");
        return;
    }

    mStreamDisplay->stop();

    if(isZSLMode()) {
		/* take care snapshot object for ZSL mode */
        mStreamSnap->stop();
        mStreamSnap->release();
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
		mStreamSnap = NULL;
	}
    mStreamDisplay->release();

    /* Delete mStreamDisplay instance*/
    QCameraStream_preview::deleteInstance (mStreamDisplay);
    mStreamDisplay = NULL;

    mCameraState = CAMERA_STATE_PREVIEW_STOP_CMD_SENT;
    LOGI("stopPreviewInternal: X");
}

int QCameraHardwareInterface::previewEnabled()
{
    LOGI("previewEnabled: E");
    Mutex::Autolock lock(mLock);
    LOGE("%s: mCameraState = %d", __func__, mCameraState);
    switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STOPPED:
	case QCAMERA_HAL_TAKE_PICTURE:
	default:
	    return false;
	    break;
	case QCAMERA_HAL_PREVIEW_START:
	case QCAMERA_HAL_PREVIEW_STARTED:
	case QCAMERA_HAL_RECORDING_STARTED:
        return true;
        break;
	}
	return false;
}

status_t QCameraHardwareInterface::startRecording()
{
    LOGI("startRecording: E");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);

	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STOPPED:
		LOGE("%s: preview has not been started", __func__);
		ret = UNKNOWN_ERROR;
	    break;
	case QCAMERA_HAL_PREVIEW_START:
		LOGE("%s: no preview native window", __func__);
		ret = UNKNOWN_ERROR;
	    break;
	case QCAMERA_HAL_PREVIEW_STARTED:

		if (mStreamRecord){
			LOGE("%s:Deleting old Record stream instance",__func__);
			QCameraStream_record::deleteInstance (mStreamRecord);
			mStreamRecord = NULL;
		}
		mStreamRecord = QCameraStream_record::createInstance(mCameraId,
            CAMERA_MODE_2D);
		LOGE("%s:  mStreamRecord = 0x%p", __func__, mStreamRecord);

		if (!mStreamRecord) {
			LOGE("%s: error - can't creat record stream!", __func__);
			ret = BAD_VALUE;
			break;
		}
		/* Store HAL object in record stream Object */
		mStreamRecord->setHALCameraControl(this);
        ret =  mStreamRecord->init();
        if (MM_CAMERA_OK != ret){
			ret = BAD_VALUE;
			break;
        }
		ret =  mStreamRecord->start();
		if (MM_CAMERA_OK != ret){
			LOGE("%s: error - mStreamRecord->start!", __func__);
			ret = BAD_VALUE;
			break;
		}
		LOGE("%s:  started", __func__);
		if(MM_CAMERA_OK == ret)
			mCameraState = CAMERA_STATE_RECORD_START_CMD_SENT;
		else
			mCameraState = CAMERA_STATE_ERROR;
		mPreviewState = QCAMERA_HAL_RECORDING_STARTED;
		break;
	case QCAMERA_HAL_RECORDING_STARTED:
		LOGE("%s: ", __func__);
        break;
	case QCAMERA_HAL_TAKE_PICTURE:
 	default:
       ret = BAD_VALUE;
		break;
	}
    LOGI("startRecording: X");
    return ret;
}

void QCameraHardwareInterface::stopRecording()
{
    LOGI("stopRecording: E");
    Mutex::Autolock lock(mLock);
	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STOPPED:
	case QCAMERA_HAL_PREVIEW_START:
	case QCAMERA_HAL_PREVIEW_STARTED:
		break;
	case QCAMERA_HAL_RECORDING_STARTED:
		stopRecordingInternal();
		mPreviewState = QCAMERA_HAL_PREVIEW_STARTED;
        break;
	case QCAMERA_HAL_TAKE_PICTURE:
 	default:
		break;
	}
    LOGI("stopRecording: X");

}
void QCameraHardwareInterface::stopRecordingInternal()
{
    LOGI("stopRecordingInternal: E");
    status_t ret = NO_ERROR;

    if(!mStreamRecord) {
        LOGE("mStreamRecord is null");
        return;
    }

    /*
     * call QCameraStream_record::stop()
     * Unregister Callback, action stop
     */
    mStreamRecord->stop();

    /*
     * call QCameraStream_record::release()
     * Buffer dellocation, Channel release, UnPrepare Buff
     */
    mStreamRecord->release();

    QCameraStream_record::deleteInstance (mStreamRecord);
    mStreamRecord = NULL;

    //mCameraState = CAMERA_STATE_RECORD_STOP_CMD_SENT;
    mCameraState = CAMERA_STATE_PREVIEW;  //TODO : Apurva : Hacked for 2nd time Recording
    mPreviewState = QCAMERA_HAL_PREVIEW_STARTED;
    LOGI("stopRecordingInternal: X");
    return;
}

int QCameraHardwareInterface::recordingEnabled()
{
	int ret = 0;
  Mutex::Autolock lock(mLock);
  LOGV("%s: E", __func__);
	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STOPPED:
	case QCAMERA_HAL_PREVIEW_START:
	case QCAMERA_HAL_PREVIEW_STARTED:
		break;
	case QCAMERA_HAL_RECORDING_STARTED:
		ret = 1;
        break;
	case QCAMERA_HAL_TAKE_PICTURE:
 	default:
		break;
	}
  LOGV("%s: X, ret = %d", __func__, ret);
  return ret;   //isRecordingRunning();
}

/**
* Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
*/

void QCameraHardwareInterface::releaseRecordingFrame(const void *opaque)
{
  LOGE("%s : BEGIN",__func__);
  if(mStreamRecord == NULL) {
    LOGE("Record stream Not Initialized");
    return;
  }
  mStreamRecord->releaseRecordingFrame(opaque);
  LOGE("%s : END",__func__);
  return;
}

status_t QCameraHardwareInterface::autoFocusEvent(cam_ctrl_status_t *status)
{
    LOGE("autoFocusEvent: E");
    int ret = NO_ERROR;
/**************************************************************
  BEGIN MUTEX CODE
  *************************************************************/

    LOGE("%s:%d: Trying to acquire AF bit lock",__func__,__LINE__);
    mAutofocusLock.lock();
    LOGE("%s:%d: Acquired AF bit lock",__func__,__LINE__);

    if(mAutoFocusRunning==false) {
      LOGE("%s:AF not running, discarding stale event",__func__);
      mAutofocusLock.unlock();
      return ret;
    }

    mAutoFocusRunning = false;
    mAutofocusLock.unlock();

/**************************************************************
  END MUTEX CODE
  *************************************************************/


    if(status==NULL) {
      LOGE("%s:NULL ptr received for status",__func__);
      return BAD_VALUE;
    }





    /*(Do?) we need to make sure that the call back is the
      last possible step in the execution flow since the same
      context might be used if a fail triggers another round
      of AF then the mAutoFocusRunning flag and other state
      variables' validity will be under question*/

    if (mNotifyCb && ( mMsgEnabled & CAMERA_MSG_FOCUS)){
      LOGE("%s:Issuing callback to service",__func__);

      /* "Accepted" status is not appropriate it should be used for
        initial cmd, event reporting should only give use SUCCESS/FAIL
        */
      if(*status==CAM_CTRL_SUCCESS || *status==CAM_CTRL_ACCEPTED) {
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
      }
      else if(*status==CAM_CTRL_FAILED){
        mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
      }
      else{
        LOGE("%s:Unknown AF status (%d) received",__func__,*status);
      }


    }/*(mNotifyCb && ( mMsgEnabled & CAMERA_MSG_FOCUS))*/
    else{
      LOGE("%s:Call back not enabled",__func__);
    }



    LOGE("autoFocusEvent: X");
    return ret;

}

status_t QCameraHardwareInterface::cancelPicture()
{
    LOGI("cancelPicture: E");
    status_t ret = MM_CAMERA_OK;
    Mutex::Autolock lock(mLock);

	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STOPPED:
	case QCAMERA_HAL_PREVIEW_START:
	case QCAMERA_HAL_PREVIEW_STARTED:
	case QCAMERA_HAL_RECORDING_STARTED:
	default:
		break;
	case QCAMERA_HAL_TAKE_PICTURE:
        ret = cancelPictureInternal();
		break;
	}
    LOGI("cancelPicture: X");
    return ret;
}

status_t QCameraHardwareInterface::cancelPictureInternal()
{
    LOGI("cancelPictureInternal: E");
    status_t ret = MM_CAMERA_OK;
    if(mCameraState != CAMERA_STATE_READY) {
        if(mStreamSnap) {
            mStreamSnap->stop();
            mCameraState = CAMERA_STATE_SNAP_STOP_CMD_SENT;
        }
    } else {
        LOGE("%s: Cannot process cancel picture as snapshot is already done",__func__);
    }
    LOGI("cancelPictureInternal: X");
    return ret;
}

void QCameraHardwareInterface::pausePreviewForSnapshot()
{
    stopPreviewInternal( ); //mStreamDisplay->stop();
}
status_t QCameraHardwareInterface::resumePreviewAfterSnapshot()
{
	status_t ret = NO_ERROR;

    ret = mStreamDisplay->start();

	return ret;
}

status_t  QCameraHardwareInterface::takePicture()
{
    LOGI("takePicture: E");
    status_t ret = MM_CAMERA_OK;
    Mutex::Autolock lock(mLock);

	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_STARTED:
		if (isZSLMode()) {
			if (mStreamSnap != NULL) {
				ret = mStreamSnap->takePictureZSL();
				if (ret != MM_CAMERA_OK) {
					LOGE("%s: Error taking ZSL snapshot!", __func__);
					ret = BAD_VALUE;
				}
			}
			else {
				LOGE("%s: ZSL stream not active! Failure!!", __func__);
				ret = BAD_VALUE;
			}
			return ret;
		}
		if (mStreamSnap){
			LOGE("%s:Deleting old Snapshot stream instance",__func__);
			QCameraStream_Snapshot::deleteInstance (mStreamSnap);
			mStreamSnap = NULL;
		}
		mStreamSnap = QCameraStream_Snapshot::createInstance(mCameraId, myMode);

		if (!mStreamSnap) {
			LOGE("%s: error - can't creat snapshot stream!", __func__);
			/* mzhu: fix me, restore preview */
			return BAD_VALUE;
		}

		/* Store HAL object in snapshot stream Object */
		mStreamSnap->setHALCameraControl(this);

		/* Call prepareSnapshot before stopping preview */
		mStreamSnap->prepareHardware();

		/* There's an issue where we have a glimpse of corrupted data between
		   a time we stop a preview and display the postview. It happens because
		   when we call stopPreview we deallocate the preview buffers hence overlay
		   displays garbage value till we enqueue postview buffer to be displayed.
		   Hence for temporary fix, we'll do memcopy of the last frame displayed and
		   queue it to overlay*/
		// mzhu storePreviewFrameForPostview();

		/* stop preview */
		pausePreviewForSnapshot();

		/* Call snapshot init*/
		ret =  mStreamSnap->init();
		if (MM_CAMERA_OK != ret){
			LOGE("%s: error - can't init Snapshot stream!", __func__);
			return BAD_VALUE;
		}

		/* call Snapshot start() :*/
		ret =  mStreamSnap->start();
		if (MM_CAMERA_OK != ret){
			/* mzhu: fix me, restore preview */
			LOGE("%s: error - can't start Snapshot stream!", __func__);
			return BAD_VALUE;
		}

		if(MM_CAMERA_OK == ret)
			mCameraState = CAMERA_STATE_SNAP_START_CMD_SENT;
		else
			mCameraState = CAMERA_STATE_ERROR;
        mPreviewState = QCAMERA_HAL_TAKE_PICTURE;
		break;
	case QCAMERA_HAL_RECORDING_STARTED:
        LOGI("Got request for live snapshot");
        takeLiveSnapshot();
        break;
	case QCAMERA_HAL_PREVIEW_STOPPED:
	case QCAMERA_HAL_PREVIEW_START:
 	default:
		ret = UNKNOWN_ERROR;
        break;
	case QCAMERA_HAL_TAKE_PICTURE:
        break;
	}
    LOGI("takePicture: X");
    return ret;
}

void  QCameraHardwareInterface::encodeData()
{
    LOGI("encodeData: E");
    LOGI("encodeData: X");
}

status_t  QCameraHardwareInterface::takeLiveSnapshot()
{
    status_t ret = NO_ERROR;
    LOGI("takeLiveSnapshot: E");
    mStreamRecord->takeLiveSnapshot();
    LOGI("takeLiveSnapshot: X");
    return ret;
}

status_t QCameraHardwareInterface::autoFocus()
{
    LOGI("autoFocus: E");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);
    LOGI("autoFocus: Got lock");
    bool status = true;
    isp3a_af_mode_t afMode = getAutoFocusMode(mParameters);

    if(mAutoFocusRunning==true){
      LOGE("%s:AF already running should not have got this call",__func__);
      return UNKNOWN_ERROR;
    }

    // Skip autofocus if focus mode is infinity.
    if (afMode == AF_MODE_MAX ) {
      LOGE("%s:Invalid AF mode (%d)",__func__,afMode);

      /*Issue a call back to service that AF failed
      or the upper layer app might hang waiting for a callback msg*/
      if (mNotifyCb && ( mMsgEnabled & CAMERA_MSG_FOCUS)){
          mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
      }
      return UNKNOWN_ERROR;
    }


    LOGI("%s:AF start (mode %d)",__func__,afMode);
    if(MM_CAMERA_OK!=cam_ops_action(mCameraId,TRUE,MM_CAMERA_OPS_FOCUS,&afMode )) {
      LOGE("%s: AF command failed err:%d error %s",__func__, errno,strerror(errno));
      return UNKNOWN_ERROR;
    }

    mAutoFocusRunning = true;
    LOGI("autoFocus: X");
    return ret;
}

status_t QCameraHardwareInterface::cancelAutoFocus()
{
    LOGE("cancelAutoFocus: E");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);

/**************************************************************
  BEGIN MUTEX CODE
*************************************************************/

    mAutofocusLock.lock();
    if(mAutoFocusRunning) {

      mAutoFocusRunning = false;
      mAutofocusLock.unlock();

    }else/*(!mAutoFocusRunning)*/{

      mAutofocusLock.unlock();
      LOGE("%s:Af not running",__func__);
      return NO_ERROR;
    }
/**************************************************************
  END MUTEX CODE
*************************************************************/


    if(MM_CAMERA_OK!=cam_ops_action(mCameraId,FALSE,MM_CAMERA_OPS_FOCUS,NULL )) {
      LOGE("%s: AF command failed err:%d error %s",__func__, errno,strerror(errno));
    }

    LOGE("cancelAutoFocus: X");
    return NO_ERROR;
}

#if 0 //mzhu
/*==========================================================================
 * FUNCTION    - prepareSnapshotAndWait -
 *
 * DESCRIPTION:  invoke preparesnapshot and wait for it done
                 it can be called within takepicture, so no need
                 to grab mLock.
 *=========================================================================*/
void QCameraHardwareInterface::prepareSnapshotAndWait()
{
    LOGI("prepareSnapshotAndWait: E");
    int rc = 0;
    /*To Do: call mm camera preparesnapshot */
    if(!rc ) {
        mPreparingSnapshot = true;
        pthread_mutex_lock(&mAsyncCmdMutex);
        pthread_cond_wait(&mAsyncCmdWait, &mAsyncCmdMutex);
        pthread_mutex_unlock(&mAsyncCmdMutex);
        mPreparingSnapshot = false;
    }
    LOGI("prepareSnapshotAndWait: X");
}
#endif //mzhu

/*==========================================================================
 * FUNCTION    - processprepareSnapshotEvent -
 *
 * DESCRIPTION:  Process the event of preparesnapshot done msg
                 unblock prepareSnapshotAndWait( )
 *=========================================================================*/
void QCameraHardwareInterface::processprepareSnapshotEvent(cam_ctrl_status_t *status)
{
    LOGI("processprepareSnapshotEvent: E");
    pthread_mutex_lock(&mAsyncCmdMutex);
    pthread_cond_signal(&mAsyncCmdWait);
    pthread_mutex_unlock(&mAsyncCmdMutex);
    LOGI("processprepareSnapshotEvent: X");
}

void QCameraHardwareInterface::roiEvent(fd_roi_t roi)
{
    LOGE("roiEvent: E");

    if(mStreamDisplay) mStreamDisplay->notifyROIEvent(roi);
#if 0 //TODO: move to preview obj
    mCallbackLock.lock();
    data_callback mcb = mDataCb;
    void *mdata = mCallbackCookie;
    int msgEnabled = mMsgEnabled;
    mCallbackLock.unlock();

    mMetaDataWaitLock.lock();
    if (mFaceDetectOn == true && mSendMetaData == true) {
        mSendMetaData = false;
        int faces_detected = roi.rect_num;
        int max_faces_detected = MAX_ROI * 4;
        int array[max_faces_detected + 1];

        array[0] = faces_detected * 4;
        for (int i = 1, j = 0;j < MAX_ROI; j++, i = i + 4) {
            if (j < faces_detected) {
                array[i]   = roi.faces[j].x;
                array[i+1] = roi.faces[j].y;
                array[i+2] = roi.faces[j].dx;
                array[i+3] = roi.faces[j].dy;
            } else {
                array[i]   = -1;
                array[i+1] = -1;
                array[i+2] = -1;
                array[i+3] = -1;
            }
        }
        if(mMetaDataHeap != NULL){
            LOGV("mMetaDataHEap is non-NULL");
            memcpy((uint32_t *)mMetaDataHeap->mHeap->base(), (uint32_t *)array, (sizeof(int)*(MAX_ROI*4+1)));
            mMetaDataWaitLock.unlock();

            if  (mcb != NULL && (msgEnabled & CAMERA_MSG_META_DATA)) {
                mcb(CAMERA_MSG_META_DATA, mMetaDataHeap->mBuffers[0], mdata);
            }
        } else {
            mMetaDataWaitLock.unlock();
            LOGE("runPreviewThread mMetaDataHeap is NULL");
        }
    } else {
        mMetaDataWaitLock.unlock();
    }
#endif // mzhu
    LOGE("roiEvent: X");
}


void QCameraHardwareInterface::handleZoomEventForSnapshot(void)
{
    mm_camera_ch_crop_t v4l2_crop;


    LOGD("%s: E", __func__);

    memset(&v4l2_crop,0,sizeof(v4l2_crop));
    v4l2_crop.ch_type=MM_CAMERA_CH_SNAPSHOT;

    LOGI("%s: Fetching crop info", __func__);
    cam_config_get_parm(mCameraId,MM_CAMERA_PARM_CROP,&v4l2_crop);

    LOGI("%s: Crop info received for main: %d, %d, %d, %d ", __func__,
         v4l2_crop.snapshot.main_crop.left,
         v4l2_crop.snapshot.main_crop.top,
         v4l2_crop.snapshot.main_crop.width,
         v4l2_crop.snapshot.main_crop.height);
    LOGI("%s: Crop info received for main: %d, %d, %d, %d ",__func__,
         v4l2_crop.snapshot.thumbnail_crop.left,
         v4l2_crop.snapshot.thumbnail_crop.top,
         v4l2_crop.snapshot.thumbnail_crop.width,
         v4l2_crop.snapshot.thumbnail_crop.height);

    if(mStreamSnap) {
        LOGD("%s: Setting crop info for snapshot", __func__);
        memcpy(&(mStreamSnap->mCrop), &v4l2_crop, sizeof(v4l2_crop));
    }

    LOGD("%s: X", __func__);
}

void QCameraHardwareInterface::handleZoomEventForPreview(void)
{
    mm_camera_ch_crop_t v4l2_crop;

    LOGI("%s: E", __func__);

    /*regular zooming or smooth zoom stopped*/
    if (!mSmoothZoomRunning) {
        memset(&v4l2_crop, 0, sizeof(v4l2_crop));
        v4l2_crop.ch_type = MM_CAMERA_CH_PREVIEW;

        LOGI("%s: Fetching crop info", __func__);
        cam_config_get_parm(mCameraId,MM_CAMERA_PARM_CROP,&v4l2_crop);

        LOGE("%s: Crop info received: %d, %d, %d, %d ", __func__,
             v4l2_crop.crop.left,
             v4l2_crop.crop.top,
             v4l2_crop.crop.width,
             v4l2_crop.crop.height);

        if ((v4l2_crop.crop.width != 0) && (v4l2_crop.crop.height != 0)) {
             mPreviewWindow->set_crop(mPreviewWindow,
                            v4l2_crop.crop.left,
                            v4l2_crop.crop.top,
                            v4l2_crop.crop.width,
                            v4l2_crop.crop.height);
             LOGI("%s: Done setting crop", __func__);
        }
        LOGI("%s: Currrent zoom :%d",__func__, mCurrentZoom);
    }
#if 0
    else {
        if (mCurrentZoom == mTargetSmoothZoom) {
            mSmoothZoomRunning = false;
            mNotifyCb(CAMERA_MSG_ZOOM,
                      mCurrentZoom, 1, mCallbackCookie);
        } else {
            mCurrentZoom += mSmoothZoomStep;
            if ((mSmoothZoomStep < 0 && mCurrentZoom < mTargetSmoothZoom)||
                (mSmoothZoomStep > 0 && mCurrentZoom > mTargetSmoothZoom )) {
                mCurrentZoom = mTargetSmoothZoom;
        }
            mParameters.set("zoom", mCurrentZoom);
            setZoom(mParameters);
        }
    }
#endif

    LOGI("%s: X", __func__);
}

void QCameraHardwareInterface::zoomEvent(cam_ctrl_status_t *status)
{
    LOGE("zoomEvent: state:%d E",mCameraState);
    switch (mCameraState) {
        case CAMERA_STATE_SNAP_CMD_ACKED:
            handleZoomEventForSnapshot();
            break;
        case CAMERA_STATE_ZSL:
            /* In ZSL mode, we start preview and snapshot stream at
               the same time */
            handleZoomEventForSnapshot();
            handleZoomEventForPreview();
            break;

        case CAMERA_STATE_PREVIEW:
        case CAMERA_STATE_RECORD_START_CMD_SENT:
        case CAMERA_STATE_RECORD:
        default:
            handleZoomEventForPreview();
            break;
    }
    LOGI("zoomEvent: X");
}

/* This is temporary solution to hide the garbage screen seen during
   snapshot between the time preview stops and postview is queued to
   overlay for display. We won't have this problem after honeycomb.*/
status_t QCameraHardwareInterface::storePreviewFrameForPostview(void)
{
    int width = 0;  /* width of channel  */
    int height = 0; /* height of channel */
    uint32_t frame_len = 0; /* frame planner length */
    int buffer_num = 4; /* number of buffers for display */
    status_t ret = NO_ERROR;
    struct msm_frame *preview_frame;
    unsigned long buffer_addr = 0;
    uint32_t planes[VIDEO_MAX_PLANES];
    uint8_t num_planes = 0;

    LOGI("%s: E", __func__);

    if (mStreamDisplay == NULL) {
        ret = FAILED_TRANSACTION;
        goto end;
    }

    mPostPreviewHeap = NULL;
    /* get preview size */
    getPreviewSize(&width, &height);
    LOGE("%s: Preview Size: %d X %d", __func__, width, height);

    frame_len = mm_camera_get_msm_frame_len(getPreviewFormat(),
                                            myMode,
                                            width,
                                            height,
                                            OUTPUT_TYPE_P,
                                            &num_planes,
                                            planes);

    LOGE("%s: Frame Length calculated: %d", __func__, frame_len);
#ifdef USE_ION
    mPostPreviewHeap =
        new IonPool( MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     frame_len,
                     1,
                     frame_len,
                     planes[0],
                     0,
                     "thumbnail");
#else
    mPostPreviewHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_THUMBNAIL,
                     frame_len,
                     1,
                     frame_len,
                     planes[0],
                     0,
                     "thumbnail");
#endif
    if (!mPostPreviewHeap->initialized()) {
        LOGE("%s: Error initializing mPostPreviewHeap buffer", __func__);
        ret = NO_MEMORY;
        goto end;
    }

    LOGE("%s: Get last queued preview frame", __func__);
    preview_frame = (struct msm_frame *)mStreamDisplay->getLastQueuedFrame();
    if (preview_frame == NULL) {
        LOGE("%s: Error retrieving preview frame.", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }
    LOGE("%s: Copy the frame buffer. buffer: %x  preview_buffer: %x",
         __func__, (uint32_t)mPostPreviewHeap->mBuffers[0]->pointer(),
         (uint32_t)preview_frame->buffer);

    /* Copy the frame */
    memcpy((void *)mPostPreviewHeap->mHeap->base(),
               (const void *)preview_frame->buffer, frame_len );

    LOGE("%s: Queue the buffer for display.", __func__);
#if 0 // mzhu
    mOverlayLock.lock();
    if (mOverlay != NULL) {
        mOverlay->setFd(mPostPreviewHeap->mHeap->getHeapID());
        mOverlay->queueBuffer((void *)0);
    }
    mOverlayLock.unlock();
#endif //mzhu

end:
    LOGI("%s: X", __func__);
    return ret;
}

void QCameraHardwareInterface::dumpFrameToFile(const void * data, uint32_t size, char* name, char* ext, int index)
{
	  char buf[32];
	  int file_fd;
	if ( data != NULL) {
		char * str;
		snprintf(buf, sizeof(buf), "/data/%s_%d.%s", name, index, ext);
		LOGE("marvin, %s size =%d", buf, size);
		file_fd = open(buf, O_RDWR | O_CREAT, 0777);
		write(file_fd, data, size);
		close(file_fd);
	}
}

void QCameraHardwareInterface::dumpFrameToFile(struct msm_frame* newFrame,
  HAL_cam_dump_frm_type_t frm_type)
{
  int32_t enabled = 0;
  int frm_num;
  uint32_t  skip_mode;
  char value[PROPERTY_VALUE_MAX];
  char buf[32];
  property_get("persist.camera.dumpimg", value, "0");
  enabled = atoi(value);

  LOGV(" newFrame =%p, frm_type = %d", newFrame, frm_type);
  if(enabled & HAL_DUMP_FRM_MASK_ALL) {
    if((enabled & frm_type) && newFrame) {
      frm_num = ((enabled & 0xffff0000) >> 16);
      if(frm_num == 0) frm_num = 10; /*default 10 frames*/
      if(frm_num > 256) frm_num = 256; /*256 buffers cycle around*/
      skip_mode = ((enabled & 0x0000ff00) >> 8);
      if(skip_mode == 0) skip_mode = 1; /*no -skip */

      if( mDumpSkipCnt % skip_mode == 0) {
        if (mDumpFrmCnt >= 0 && mDumpFrmCnt <= frm_num) {
          int w, h;
          int file_fd;
          switch (frm_type) {
          case  HAL_DUMP_FRM_PREVIEW:
            w = mDimension.display_width;
            h = mDimension.display_height;
            snprintf(buf, sizeof(buf), "/data/%dp_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_VIDEO:
            w = mDimension.video_width;
            h = mDimension.video_height;
            snprintf(buf, sizeof(buf),"/data/%dv_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_MAIN:
            w = mDimension.picture_width;
            h = mDimension.picture_height;
            snprintf(buf, sizeof(buf), "/data/%dm_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_THUMBNAIL:
            w = mDimension.ui_thumbnail_width;
            h = mDimension.ui_thumbnail_height;
            snprintf(buf, sizeof(buf),"/data/%dt_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          default:
            w = h = 0;
            file_fd = -1;
            break;
          }

          if (file_fd < 0) {
            LOGE("%s: cannot open file:type=%d\n", __func__, frm_type);
          } else {
            LOGE("%s: %d %d", __func__, newFrame->y_off, newFrame->cbcr_off);
            write(file_fd, (const void *)(newFrame->buffer+newFrame->y_off), w * h);
            write(file_fd, (const void *)
              (newFrame->buffer + newFrame->cbcr_off), w * h / 2);
            close(file_fd);
            LOGE("dump %s", buf);
          }
        } else if(frm_num == 256){
          mDumpFrmCnt = 0;
        }
        mDumpFrmCnt++;
      }
      mDumpSkipCnt++;
    }
  }  else {
    mDumpFrmCnt = 0;
  }
}

status_t QCameraHardwareInterface::setPreviewWindow(preview_stream_ops_t* window)
{
    status_t retVal = NO_ERROR;
    LOGE(" %s: E mPreviewState = %d, mStreamDisplay = 0x%p", __FUNCTION__, mPreviewState, mStreamDisplay);
    if( window == NULL) {
        LOGE("%s:Received Setting NULL preview window", __func__);
    }
	switch(mPreviewState) {
	case QCAMERA_HAL_PREVIEW_START:
		mPreviewWindow = window;
		if(mPreviewWindow) {
			/* we have valid surface now, start preview */
			LOGE("%s:  calling startPreview2", __func__);
			retVal = startPreview2();
			if(retVal == NO_ERROR)
				mPreviewState = QCAMERA_HAL_PREVIEW_STARTED;
			LOGE("%s:  startPreview2 done, mPreviewState = %d", __func__, mPreviewState);

		} else
			LOGE("%s: null window received, mPreviewState = %d", __func__, mPreviewState);
	    break;
    case QCAMERA_HAL_PREVIEW_STARTED:
		/* new window comes */
	    LOGE("%s: bug, cannot handle new window in started state", __func__);
		//retVal = UNKNOWN_ERROR;
		break;
	case QCAMERA_HAL_PREVIEW_STOPPED:
        mPreviewWindow = window;
		LOGE("%s: mPreviewWindow = 0x%p, mStreamDisplay = 0x%p",
			__func__, mPreviewWindow, mStreamDisplay);
	    if(mStreamDisplay)
            retVal = mStreamDisplay->setPreviewWindow(window);
		break;
	default:
	    LOGE("%s: bug, cannot handle new window in state %d", __func__, mPreviewState);
		retVal = UNKNOWN_ERROR;
		break;
	}
    LOGE(" %s : X, mPreviewState = %d", __FUNCTION__, mPreviewState);
    return retVal;
}

int QCameraHardwareInterface::storeMetaDataInBuffers(int enable)
{
	/* this is a dummy func now. fix me later */
    mStoreMetaDataInFrame = enable;
	return 0;
}

int QCameraHardwareInterface::allocate_ion_memory(QCameraHalHeap_t *p_camera_memory, int cnt, int ion_type)
{
  int rc = 0;
  struct ion_handle_data handle_data;

  p_camera_memory->main_ion_fd[cnt] = open("/dev/ion", O_RDONLY | O_SYNC);
  if (p_camera_memory->main_ion_fd[cnt] < 0) {
    LOGE("Ion dev open failed\n");
    LOGE("Error is %s\n", strerror(errno));
    goto ION_OPEN_FAILED;
  }
  p_camera_memory->alloc[cnt].len = p_camera_memory->size;
  /* to make it page size aligned */
  p_camera_memory->alloc[cnt].len = (p_camera_memory->alloc[cnt].len + 4095) & (~4095);
  p_camera_memory->alloc[cnt].align = 4096;
  p_camera_memory->alloc[cnt].flags = 0x1 << ion_type;

  rc = ioctl(p_camera_memory->main_ion_fd[cnt], ION_IOC_ALLOC, &p_camera_memory->alloc[cnt]);
  if (rc < 0) {
    LOGE("ION allocation failed\n");
    goto ION_ALLOC_FAILED;
  }

  p_camera_memory->ion_info_fd[cnt].handle = p_camera_memory->alloc[cnt].handle;
  rc = ioctl(p_camera_memory->main_ion_fd[cnt], ION_IOC_SHARE, &p_camera_memory->ion_info_fd[cnt]);
  if (rc < 0) {
    LOGE("ION map failed %s\n", strerror(errno));
    goto ION_MAP_FAILED;
  }
  p_camera_memory->fd[cnt] = p_camera_memory->ion_info_fd[cnt].fd;
  return 0;

ION_MAP_FAILED:
  handle_data.handle = p_camera_memory->ion_info_fd[cnt].handle;
  ioctl(p_camera_memory->main_ion_fd[cnt], ION_IOC_FREE, &handle_data);
ION_ALLOC_FAILED:
  close(p_camera_memory->main_ion_fd[cnt]);
ION_OPEN_FAILED:
  return -1;
}

int QCameraHardwareInterface::deallocate_ion_memory(QCameraHalHeap_t *p_camera_memory, int cnt)
{
  struct ion_handle_data handle_data;
  int rc = 0;

  handle_data.handle = p_camera_memory->ion_info_fd[cnt].handle;
  ioctl(p_camera_memory->main_ion_fd[cnt], ION_IOC_FREE, &handle_data);
  close(p_camera_memory->main_ion_fd[cnt]);
  return rc;
}

int QCameraHardwareInterface::initHeapMem( QCameraHalHeap_t *heap,
                            int num_of_buf,
                            int buf_len,
                            int y_off,
                            int cbcr_off,
                            int pmem_type,
                            mm_cameara_stream_buf_t *StreamBuf,
                            mm_camera_buf_def_t *buf_def,
                            uint8_t num_planes,
                            uint32_t *planes
)
{
	int rc = 0;
	int i;
	int path;
	struct msm_frame *frame;
	LOGE("Init Heap =%p. stream_buf =%p, pmem_type =%d, num_of_buf=%d. buf_len=%d, cbcr_off=%d",
	    heap, StreamBuf, pmem_type, num_of_buf, buf_len, cbcr_off);
	if(num_of_buf > MM_CAMERA_MAX_NUM_FRAMES || heap == NULL ||
	  mGetMemory == NULL ) {
		LOGE("Init Heap error");
		rc = -1;
	    return rc;
    }
	memset(heap, 0, sizeof(QCameraHalHeap_t));
	heap->buffer_count = num_of_buf;
	heap->size = buf_len;
	heap->y_offset = y_off;
	heap->cbcr_offset = cbcr_off;

    if (StreamBuf != NULL) {
		StreamBuf->num = num_of_buf;
                StreamBuf->frame_len = buf_len;
		switch (pmem_type) {
			case  MSM_PMEM_MAINIMG:
			case  MSM_PMEM_RAW_MAINIMG:
				path = OUTPUT_TYPE_S;
				break;

			case  MSM_PMEM_THUMBNAIL:
				path = OUTPUT_TYPE_T;
				break;

			default:
			  rc = -1;
			  return rc;
		}
	}


	for(i = 0; i < num_of_buf; i++) {
#ifdef USE_ION
        rc = allocate_ion_memory(heap, i, ION_HEAP_ADSP_ID);
        if (rc < 0) {
            LOGE("%sION allocation failed\n", __func__);
            break;
        }
#else
		heap->fd[i] = open("/dev/pmem_adsp", O_RDWR|O_SYNC);
		if ( heap->fd[i] <= 0) {
			rc = -1;
			LOGE("Open fail: heap->fd[%d] =%d", i, heap->fd[i]);
			break;
		}
#endif
		heap->camera_memory[i] =  mGetMemory( heap->fd[i], buf_len, 1, (void *)this);

		if (heap->camera_memory[i] == NULL ) {
			LOGE("Getmem fail %d: ", i);
			rc = -1;
			break;
		}
		if (StreamBuf != NULL) {
			frame = &(StreamBuf->frame[i]);
			memset(frame, 0, sizeof(struct msm_frame));
			frame->fd = heap->fd[i];
			frame->phy_offset = 0;
			frame->buffer = (uint32_t) heap->camera_memory[i]->data;
			frame->path = path;
			frame->cbcr_off =  planes[0]+heap->cbcr_offset;
			frame->y_off =  heap->y_offset;
			LOGD("%s: Buffer idx: %d  addr: %x fd: %d phy_offset: %d"
				 "cbcr_off: %d y_off: %d frame_len: %d", __func__,
				 i, (unsigned int)frame->buffer, frame->fd,
				 frame->phy_offset, cbcr_off, y_off, buf_len);

                        buf_def->buf.mp[i].frame = *frame;
                        buf_def->buf.mp[i].frame_offset = 0;
                        buf_def->buf.mp[i].num_planes = num_planes;
                        /* Plane 0 needs to be set seperately. Set other planes
                         * in a loop. */
                        buf_def->buf.mp[i].planes[0].length = planes[0];
            	        buf_def->buf.mp[i].planes[0].m.userptr = frame->fd;
                        buf_def->buf.mp[i].planes[0].data_offset = y_off;
         	        buf_def->buf.mp[i].planes[0].reserved[0] =
              	            buf_def->buf.mp[i].frame_offset;
           	        for (int j = 1; j < num_planes; j++) {
                             buf_def->buf.mp[i].planes[j].length = planes[j];
                             buf_def->buf.mp[i].planes[j].m.userptr = frame->fd;
                             buf_def->buf.mp[i].planes[j].data_offset = cbcr_off;
                             buf_def->buf.mp[i].planes[j].reserved[0] =
               	                 buf_def->buf.mp[i].planes[j-1].reserved[0] +
	                         buf_def->buf.mp[i].planes[j-1].length;
	                }
		 } else {
		 }

		LOGE("heap->fd[%d] =%d, camera_memory=%p", i, heap->fd[i], heap->camera_memory[i]);
		heap->local_flag[i] = 1;
	}
	if( rc < 0) {
		releaseHeapMem(heap);
	}
    return rc;

}

int QCameraHardwareInterface::releaseHeapMem( QCameraHalHeap_t *heap)
{
	int rc = 0;
	LOGE("Release %p", heap);
	if (heap != NULL) {

		for (int i = 0; i < heap->buffer_count; i++) {
			if(heap->camera_memory[i] != NULL) {
				heap->camera_memory[i]->release( heap->camera_memory[i] );
				heap->camera_memory[i] = NULL;
			} else if (heap->fd[i] <= 0) {
				LOGE("impossible: amera_memory[%d] = %p, fd = %d",
				i, heap->camera_memory[i], heap->fd[i]);
			}

			if(heap->fd[i] > 0) {
				close(heap->fd[i]);
				heap->fd[i] = -1;
			}
#ifdef USE_ION
            deallocate_ion_memory(heap, i);
#endif
		}
        heap->buffer_count = 0;
        heap->size = 0;
        heap->y_offset = 0;
        heap->cbcr_offset = 0;
	}
	return rc;
}

preview_format_info_t  QCameraHardwareInterface::getPreviewFormatInfo( )
{
  return mPreviewFormatInfo;
}

}; // namespace android

