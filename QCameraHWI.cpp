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

//#define LOG_NDEBUG 0
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

int32_t QCameraHardwareInterface::createRecord()
{
    int32_t ret = MM_CAMERA_OK;
    LOGV("%s : BEGIN",__func__);

    /*
    * Creating Instance of record stream.
    */
    LOGE("Mymode Record = %d",myMode);
    mStreamRecord = QCameraStream_record::createInstance(mCameraId,
            myMode);

    if (!mStreamRecord) {
        LOGE("%s: error - can't creat record stream!", __func__);
        return BAD_VALUE;
    }

    /* Store HAL object in record stream Object */
    mStreamRecord->setHALCameraControl(this);

     /*Init Channel */
     ret =  mStreamRecord->init();
     if (MM_CAMERA_OK != ret){
         LOGE("%s: error - can't init Record channel!", __func__);
         return BAD_VALUE;
     }
     LOGV("%s : END",__func__);
     return ret;
}

int32_t QCameraHardwareInterface::createSnapshot()
{
    int32_t ret = MM_CAMERA_OK;
    LOGV("%s : BEGIN",__func__);

    /*
    * Creating Instance of Snapshot stream.
    */
    LOGE("Mymode Snap = %d",myMode);
    mStreamSnap = QCameraStream_Snapshot::createInstance(mCameraId,
            myMode);

    if (!mStreamSnap) {
        LOGE("%s: error - can't creat snapshot stream!", __func__);
        return BAD_VALUE;
    }

    /* Store HAL object in Snapshot stream Object */
    mStreamSnap->setHALCameraControl(this);

    /*Init Channel */
     ret =  mStreamSnap->init();
     if (MM_CAMERA_OK != ret){
         LOGE("%s: error - can't init Snapshot channel!", __func__);
         return BAD_VALUE;
     }
     LOGV("%s : END",__func__);
     return ret;
}

int32_t QCameraHardwareInterface::createPreview()
{
    int32_t ret = MM_CAMERA_OK;
    LOGV("%s : BEGIN",__func__);

    LOGE("Mymode Preview = %d",myMode);
    mStreamDisplay = QCameraStream_preview::createInstance(mCameraId,
                                                      myMode);

    if (!mStreamDisplay) {
      LOGE("%s: error - can't creat preview stream!", __func__);
      return BAD_VALUE;
    }

    mStreamDisplay->setHALCameraControl(this);

    /*now init all the buffers and send to steam object*/
    ret = mStreamDisplay->init();
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't init Preview channel!", __func__);
      return BAD_VALUE;
    }
    LOGV("%s : END",__func__);
    return ret;
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
                    mPreviewHeap(0),
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
                    mPostPreviewHeap(NULL)

{
    LOGI("QCameraHardwareInterface: E");
    int32_t result = MM_CAMERA_E_GENERAL;
    char value[PROPERTY_VALUE_MAX];

    pthread_mutex_init(&mAsyncCmdMutex, NULL);
    pthread_cond_init(&mAsyncCmdWait, NULL);

    property_get("persist.debug.sf.showfps", value, "0");
    mDebugFps = atoi(value);

    property_get("camera.hal.fps", value, "0");
    mFps = atoi(value);

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
          return;
    }

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

    //Create Stream Objects
    //Preview
    result = createPreview();
    if(result != MM_CAMERA_OK) {
        LOGE("%s X: Failed to create Preview Object",__func__);
        return;
    }

    //Record
    result = createRecord();
    if(result != MM_CAMERA_OK) {
        LOGE("%s X: Failed to create Record Object",__func__);
        return;
    }

    //Snapshot
    result = createSnapshot();
    if(result != MM_CAMERA_OK) {
        LOGE("%s X: Failed to create Record Object",__func__);
        return;
    }
    mCameraState = CAMERA_STATE_READY;
    LOGI("QCameraHardwareInterface: X");
}


QCameraHardwareInterface::~QCameraHardwareInterface()
{
    LOGI("~QCameraHardwareInterface: E");
    int result;

    freePictureTable();
    if(mStatHeap != NULL) {
      mStatHeap.clear( );
      mStatHeap = NULL;
    }

   cam_ops_close(mCameraId);

   if(mStreamRecord) {
        QCameraStream_record::deleteInstance (mStreamRecord);
        mStreamRecord = NULL;
    }
    if(mStreamSnap) {
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
        mStreamSnap = NULL;
    }
    if(mStreamDisplay){
        QCameraStream_preview::deleteInstance (mStreamDisplay);
        mStreamDisplay = NULL;
    }
    LOGI("~QCameraHardwareInterface: X");
}

void QCameraHardwareInterface::release()
{
    LOGI("release: E");
    Mutex::Autolock l(&mLock);

    if (isRecordingRunning()) {
        stopRecordingInternal();
        LOGI("release: stopRecordingInternal done.");
    }
    if (isPreviewRunning()) {
        stopPreviewInternal();
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
    LOGI("release: X");
}


sp<IMemoryHeap> QCameraHardwareInterface::getPreviewHeap() const
{
    /* TBD - Need to access buffer with an interface to relevant class
    LOGI("%s: E", __func__);
    if(mIs3DModeOn != true) {
        //TBD - Need to pass mPreviewHeap after its implemented in QCameraHWI_Preview.cpp
        return mStreamRecord->mRecordHeap != NULL ? mStreamRecord->mRecordHeap->mHeap : NULL;
    } else {
        return mStreamRecord->mRecordHeap != NULL ? mStreamRecord->mRecordHeap->mHeap : NULL;
    }*/
    return mPreviewHeap;
}

sp<IMemoryHeap> QCameraHardwareInterface::getRawHeap() const
{
    LOGI("getRawHeap: E");
    return (mStreamSnap)? mStreamSnap->getRawHeap() : NULL;
}

void QCameraHardwareInterface::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void* user)
{
    LOGV("setCallbacks: E");
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
    LOGV("setCallbacks: X");
}

void QCameraHardwareInterface::enableMsgType(int32_t msgType)
{
    LOGV("enableMsgType: E");
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
    LOGV("enableMsgType: X");
}

void QCameraHardwareInterface::disableMsgType(int32_t msgType)
{
    LOGV("disableMsgType: E");
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
    LOGV("disableMsgType: X");
}

bool QCameraHardwareInterface::msgTypeEnabled(int32_t msgType)
{
    LOGV("msgTypeEnabled: E");
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
    LOGV("msgTypeEnabled: X");
}

status_t QCameraHardwareInterface::dump(int fd, const Vector<String16>& args) const
{
    LOGV("dump: E");
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);
    write(fd, result.string(), result.size());
    LOGV("dump: E");
    return NO_ERROR;
}

status_t QCameraHardwareInterface::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    LOGI("sendCommand: E");
    status_t rc = NO_ERROR;
    Mutex::Autolock l(&mLock);

    switch (command) {
        case CAMERA_CMD_HISTOGRAM_ON:
            LOGI("histogram set to on");
            rc = setHistogram(1);
            break;
        case CAMERA_CMD_HISTOGRAM_OFF:
            LOGI("histogram set to off");
            rc = setHistogram(0);
            break;
        case CAMERA_CMD_HISTOGRAM_SEND_DATA:
            LOGI("histogram send data");
            mSendData = true;
            rc = NO_ERROR;
            break;
        case CAMERA_CMD_FACE_DETECTION_ON:
           if(supportsFaceDetection() == false){
                LOGI("Face detection support is not available");
                return NO_ERROR;
           }
           setFaceDetection("on");
           return runFaceDetection();
        case CAMERA_CMD_FACE_DETECTION_OFF:
           if(supportsFaceDetection() == false){
                LOGI("Face detection support is not available");
                return NO_ERROR;
           }
           setFaceDetection("off");
           return runFaceDetection();
        case CAMERA_CMD_SEND_META_DATA:
           mMetaDataWaitLock.lock();
           if(mFaceDetectOn == true) {
               mSendMetaData = true;
           }
           mMetaDataWaitLock.unlock();
           return NO_ERROR;
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

status_t QCameraHardwareInterface::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize) {
    LOGI("getBufferInfo: E");
    status_t ret = MM_CAMERA_OK;
    if (mStreamRecord)
        mStreamRecord->getBufferInfo(Frame,alignedSize);
    LOGI("getBufferInfo: X");
    return ret;
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
sp<CameraHardwareInterface> QCameraHardwareInterface::createInstance(int cameraId, int mode)
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
      sp<CameraHardwareInterface> hardware(cam);
      LOGI("createInstance: X");
      return hardware;
    } else {
      return NULL;
    }
}

/* external plug in function */
extern "C" sp<CameraHardwareInterface>
QCameraHAL_openCameraHardware(int  cameraId, int mode)
{
    LOGI("QCameraHAL_openCameraHardware: E");
    return QCameraHardwareInterface::createInstance(cameraId, mode);
}

bool QCameraHardwareInterface::useOverlay(void)
{
    LOGI("useOverlay: E");
    mUseOverlay = TRUE;
    LOGI("useOverlay: X");
    return mUseOverlay;
}

status_t QCameraHardwareInterface::setOverlay(const sp<Overlay> &Overlay)
{
    LOGI("setOverlay: E");
    if( Overlay != NULL) {
        LOGV(" Valid overlay object ");
        mOverlayLock.lock();
        mOverlay = Overlay;
        mOverlayLock.unlock();
    } else {
        LOGV(" Overlay Object cleared ");
        mOverlayLock.lock();
        mOverlay = NULL;
        mOverlayLock.unlock();
        return UNKNOWN_ERROR;
    }
    LOGI("setOverlay: X");
    return NO_ERROR;
}

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
    LOGI("isRecordingRunning: E");
    bool ret = false;
    if((mCameraState == CAMERA_STATE_RECORD) ||
       (mCameraState == CAMERA_STATE_RECORD_START_CMD_SENT)) {
       return true;
    }
    LOGI("isRecordingRunning: X");
    return ret;
}

bool QCameraHardwareInterface::isSnapshotRunning() {
    LOGI("isSnapshotRunning: E");
    bool ret = false;
    if((mCameraState == CAMERA_STATE_SNAP_CMD_ACKED) ||
       (mCameraState == CAMERA_STATE_SNAP_START_CMD_SENT)) {
        return true;
    }
    return ret;
    LOGI("isSnapshotRunning: X");
}

bool QCameraHardwareInterface::isZSLMode() {
    return (myMode & CAMERA_ZSL_MODE);
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
    LOGI("processStatsEvent E");
    if (!isPreviewRunning( )) {
        LOGE("preview is not running");
        return;
    }

    switch (event->event_id) {
        case MM_CAMERA_STATS_EVT_HISTO:
        {
        LOGI("HAL process Histo: mMsgEnabled=0x%x, mStatsOn=%d, mSendData=%d, mDataCb=%p ",
             (mMsgEnabled & CAMERA_MSG_STATS_DATA), mStatsOn, mSendData, mDataCb);
        int msgEnabled = mMsgEnabled;
        camera_preview_histogram_info* hist_info = (camera_preview_histogram_info*)
                                                    event->e.stats_histo.histo_info;
        if(mStatsOn == QCAMERA_PARM_ENABLE && mSendData &&
           mDataCb && (msgEnabled & CAMERA_MSG_STATS_DATA) ) {
            uint32_t *dest;
            mSendData = false;
            mCurrentHisto = (mCurrentHisto + 1) % 3;
            // The first element of the array will contain the maximum hist value provided by driver.
            dest = (uint32_t *) ((unsigned int)mStatHeap->mHeap->base() +
              (mStatHeap->mBufferSize * mCurrentHisto));
            *dest = hist_info->max_value;
            dest++;
            memcpy(dest , (uint32_t *)hist_info->buffer,(sizeof(int32_t) * 256));
            mDataCb(CAMERA_MSG_STATS_DATA, mStatHeap->mBuffers[mCurrentHisto], mCallbackCookie);
        }
        }
        break;
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
    LOGI("startPreview: E");
    Mutex::Autolock lock(mLock);
    status_t ret = NO_ERROR;

    cam_ctrl_dimension_t dim;
    bool initPreview = false;

    if (isPreviewRunning()){
        LOGE("%s:Preview already started  mCameraState = %d!", __func__, mCameraState);
        LOGE("%s: X", __func__);
        return NO_ERROR;
    }

    /* If it's ZSL, start preview in ZSL mode*/
    if (isZSLMode()) {
        LOGE("Start Preview in ZSL Mode");
        ret = startPreviewZSL();
        goto end;
    }

    /*  get existing preview information, by qury mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);

    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get preview dimension!", __func__);
      LOGE("%s: X", __func__);
      goto end;
    }

    /* config the parmeters and see if we need to re-init the stream*/
    initPreview = preview_parm_config (&dim, mParameters);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't config preview parms!", __func__);
      LOGE("%s: X", __func__);
      goto end;
    }

    ret = mStreamDisplay->start();
    if (MM_CAMERA_OK != ret){
      LOGE("%s: error - can't start nonZSL stream!", __func__);
      LOGE("%s: X", __func__);
      goto end;
    }

end :
    if(mPostPreviewHeap != NULL) {
        mPostPreviewHeap.clear();
        mPostPreviewHeap = NULL;
    }

    if(MM_CAMERA_OK == ret)
        mCameraState = CAMERA_STATE_PREVIEW_START_CMD_SENT;
    else
        mCameraState = CAMERA_STATE_ERROR;

    LOGI("startPreview: X");
    return ret;
}

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


void QCameraHardwareInterface::stopPreview()
{
    LOGI("stopPreview: E");
    Mutex::Autolock lock(mLock);
    if (isPreviewRunning()) {
        if (isZSLMode()) {
            stopPreviewZSL();
        }
        else {
            stopPreviewInternal();
        }
    } else {
        LOGE("%s: Preview already stopped",__func__);
    }
    LOGI("stopPreview: X");
}

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

    mCameraState = CAMERA_STATE_PREVIEW_STOP_CMD_SENT;

    LOGI("stopPreviewZSL: X");
}

void QCameraHardwareInterface::stopPreviewInternal()
{
    LOGI("stopPreviewInternal: E");
    status_t ret = NO_ERROR;

    if(!mStreamDisplay) {
        LOGE("mStreamDisplay is null");
        return;
    }

    mStreamDisplay->stop();

    mCameraState = CAMERA_STATE_PREVIEW_STOP_CMD_SENT;
    LOGI("stopPreviewInternal: X");
}

bool QCameraHardwareInterface::previewEnabled()
{
    LOGI("previewEnabled: E");
    Mutex::Autolock lock(mLock);
    LOGV("%s: mCameraState = %d", __func__, mCameraState);
    LOGI("previewEnabled: X");

    //TBD - Need to check on target if this flag is enough
    return isPreviewRunning();
}

status_t QCameraHardwareInterface::startRecording()
{
    LOGI("startRecording: E");
    status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if (isRecordingRunning()) {
        LOGV("%s: X - record already running", __func__);
        return NO_ERROR;
    }

    /*
     * call Record start() :
     * Register Callback, action start
     */
    ret =  mStreamRecord->start();
    if (MM_CAMERA_OK != ret){
        LOGE("%s: error - can't start nonZSL stream!", __func__);
        return BAD_VALUE;
    }

    if(MM_CAMERA_OK == ret)
        mCameraState = CAMERA_STATE_RECORD_START_CMD_SENT;
    else
        mCameraState = CAMERA_STATE_ERROR;

    LOGI("startRecording: X");
    return ret;
}

void QCameraHardwareInterface::stopRecording()
{
    LOGI("stopRecording: E");
    Mutex::Autolock lock(mLock);
    if (isRecordingRunning()) {
        stopRecordingInternal();
    } else {
        LOGE("%s: Recording already stopped",__func__);
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

    mCameraState = CAMERA_STATE_PREVIEW;  //TODO : Apurva : Hacked for 2nd time Recording

    LOGI("stopRecordingInternal: X");
    return;
}

bool QCameraHardwareInterface::recordingEnabled()
{
  Mutex::Autolock lock(mLock);
  LOGV("%s: E", __func__);
  return isRecordingRunning();
  LOGV("%s: X", __func__);
}

/**
* Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
*/

void QCameraHardwareInterface::releaseRecordingFrame(const sp<IMemory>& mem)
{
  LOGV("%s : BEGIN",__func__);
  if(mStreamRecord == NULL) {
    LOGE("Record stream Not Initialized");
    return;
  }
  mStreamRecord->releaseRecordingFrame(mem);
  LOGV("%s : END",__func__);
  return;
}

status_t QCameraHardwareInterface::autoFocusEvent(cam_ctrl_status_t *status)
{
    LOGV("autoFocusEvent: E");
    int ret = NO_ERROR;
/**************************************************************
  BEGIN MUTEX CODE
  *************************************************************/

    LOGI("%s:%d: Trying to acquire AF bit lock",__func__,__LINE__);
    mAutofocusLock.lock();
    LOGI("%s:%d: Acquired AF bit lock",__func__,__LINE__);

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
      LOGI("%s:Issuing callback to service",__func__);

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

    LOGV("autoFocusEvent: X");
    return ret;

}

status_t QCameraHardwareInterface::cancelPicture()
{
    LOGV("cancelPicture: E");
    status_t ret = MM_CAMERA_OK;
    Mutex::Autolock lock(mLock);

    ret = cancelPictureInternal();
    LOGV("cancelPicture: X");
    return ret;
}

status_t QCameraHardwareInterface::cancelPictureInternal()
{
    LOGV("cancelPictureInternal: E");
    status_t ret = MM_CAMERA_OK;
    if(mCameraState != CAMERA_STATE_READY) {
        if(mStreamSnap) {
            mStreamSnap->stop();
            mCameraState = CAMERA_STATE_SNAP_STOP_CMD_SENT;
        }
    } else {
        LOGE("%s: Cannot process cancel picture as snapshot is already done",__func__);
    }
    LOGV("cancelPictureInternal: X");
    return ret;
}

status_t  QCameraHardwareInterface::takePicture()
{
    LOGV("takePicture: E");
    status_t ret = MM_CAMERA_OK;
    Mutex::Autolock lock(mLock);

    /* TBD: disable this check till event is properly handled*/
/*    if (isSnapshotRunning()) {
        LOGV("%s: X - Snapshot already running", __func__);
        return NO_ERROR;
    }

    if((mCameraState != CAMERA_STATE_PREVIEW)
            || (mCameraState != CAMERA_STATE_PREVIEW_START_CMD_SENT))
    {
        LOGE("%s:Preview is not Initialized. Cannot take picture",__func__);
        return NO_ERROR;
    }
*/
    if(mStreamSnap == NULL){
        LOGE("Snapshot is not initialized");
        return BAD_VALUE;
    }
    if (isZSLMode()) {
        ret = mStreamSnap->takePictureZSL();
        if (ret != MM_CAMERA_OK) {
            LOGE("%s: Error taking ZSL snapshot!", __func__);
            ret = BAD_VALUE;
        }
        LOGI("takePicture ZSL: X %d ",mCameraState);
        return ret;
    }
    /* Call prepareSnapshot before stopping preview */
    mStreamSnap->prepareHardware();

    /* There's an issue where we have a glimpse of corrupted data between
       a time we stop a preview and display the postview. It happens because
       when we call stopPreview we deallocate the preview buffers hence overlay
       displays garbage value till we enqueue postview buffer to be displayed.
       Hence for temporary fix, we'll do memcopy of the last frame displayed and
       queue it to overlay*/
    storePreviewFrameForPostview();

    /* stop preview */
    stopPreviewInternal();

    /* call Snapshot start() :*/
    ret =  mStreamSnap->start();
    if (MM_CAMERA_OK != ret){
        LOGE("%s: error - can't start Snapshot stream!", __func__);
        return BAD_VALUE;
    }

 end:
    if(MM_CAMERA_OK == ret)
        mCameraState = CAMERA_STATE_SNAP_START_CMD_SENT;
    else
        mCameraState = CAMERA_STATE_ERROR;

    LOGV("takePicture: X");
    return ret;
}

void  QCameraHardwareInterface::encodeData()
{
    LOGV("encodeData: E");
    LOGV("encodeData: X");
}

status_t  QCameraHardwareInterface::takeLiveSnapshot()
{
    status_t ret = MM_CAMERA_OK;
    LOGV("takeLiveSnapshot: E");
    mStreamRecord->takeLiveSnapshot();
    LOGV("takeLiveSnapshot: X");
    return ret;
}

status_t QCameraHardwareInterface::autoFocus()
{
    LOGV("autoFocus: E");
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
    LOGV("autoFocus: X");
    return ret;
}

status_t QCameraHardwareInterface::cancelAutoFocus()
{
    LOGV("cancelAutoFocus: E");
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

    LOGV("cancelAutoFocus: X");
    return NO_ERROR;
}
/*==========================================================================
 * FUNCTION    - prepareSnapshotAndWait -
 *
 * DESCRIPTION:  invoke preparesnapshot and wait for it done
                 it can be called within takepicture, so no need
                 to grab mLock.
 *=========================================================================*/
void QCameraHardwareInterface::prepareSnapshotAndWait()
{
    LOGV("prepareSnapshotAndWait: E");
    int rc = 0;
    /*To Do: call mm camera preparesnapshot */
    if(!rc ) {
        mPreparingSnapshot = true;
        pthread_mutex_lock(&mAsyncCmdMutex);
        pthread_cond_wait(&mAsyncCmdWait, &mAsyncCmdMutex);
        pthread_mutex_unlock(&mAsyncCmdMutex);
        mPreparingSnapshot = false;
    }
    LOGV("prepareSnapshotAndWait: X");
}

/*==========================================================================
 * FUNCTION    - processprepareSnapshotEvent -
 *
 * DESCRIPTION:  Process the event of preparesnapshot done msg
                 unblock prepareSnapshotAndWait( )
 *=========================================================================*/
void QCameraHardwareInterface::processprepareSnapshotEvent(cam_ctrl_status_t *status)
{
    LOGV("processprepareSnapshotEvent: E");
    pthread_mutex_lock(&mAsyncCmdMutex);
    pthread_cond_signal(&mAsyncCmdWait);
    pthread_mutex_unlock(&mAsyncCmdMutex);
    LOGV("processprepareSnapshotEvent: X");
}

void QCameraHardwareInterface::roiEvent(fd_roi_t roi)
{
    LOGV("roiEvent: E");
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
    LOGV("roiEvent: X");
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

        LOGI("%s: Crop info received: %d, %d, %d, %d ", __func__,
             v4l2_crop.crop.left,
             v4l2_crop.crop.top,
             v4l2_crop.crop.width,
             v4l2_crop.crop.height);

        mOverlayLock.lock();
        if(mOverlay != NULL){
            LOGI("%s: Setting crop", __func__);
            if ((v4l2_crop.crop.width != 0) && (v4l2_crop.crop.height != 0)) {
                mOverlay->setCrop(v4l2_crop.crop.left, v4l2_crop.crop.top,
                                  v4l2_crop.crop.width, v4l2_crop.crop.height);
                LOGI("%s: Done setting crop", __func__);
            } else {
                LOGI("%s: Resetting crop", __func__);
                mOverlay->setCrop(0, 0, previewWidth, previewHeight);
            }
        }
        mOverlayLock.unlock();
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
    LOGV("zoomEvent: state:%d E",mCameraState);
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
    LOGV("zoomEvent: X");
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

    LOGV("%s: E", __func__);

    if (mStreamDisplay == NULL) {
        ret = FAILED_TRANSACTION;
        goto end;
    }

    mPostPreviewHeap = NULL;
    /* get preview size */
    getPreviewSize(&width, &height);
    LOGI("%s: Preview Size: %dX%d", __func__, width, height);

    frame_len = mm_camera_get_msm_frame_len(getPreviewFormat(),
                                            myMode,
                                            width,
                                            height,
                                            OUTPUT_TYPE_P,
                                            &num_planes,
                                            planes);

    LOGI("%s: Frame Length calculated: %d", __func__, frame_len);
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

    LOGI("%s: Get last queued preview frame", __func__);
    preview_frame = (struct msm_frame *)mStreamDisplay->getLastQueuedFrame();
    if (preview_frame == NULL) {
        LOGE("%s: Error retrieving preview frame.", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }
    LOGI("%s: Copy the frame buffer. buffer: %x  preview_buffer: %x",
         __func__, (uint32_t)mPostPreviewHeap->mBuffers[0]->pointer(),
         (uint32_t)preview_frame->buffer);

    /* Copy the frame */
    memcpy((void *)mPostPreviewHeap->mHeap->base(),
               (const void *)preview_frame->buffer, frame_len );

    LOGI("%s: Queue the buffer for display.", __func__);
    mOverlayLock.lock();
    if (mOverlay != NULL) {
        mOverlay->setFd(mPostPreviewHeap->mHeap->getHeapID());
        mOverlay->queueBuffer((void *)0);
    }
    mOverlayLock.unlock();
end:
    LOGV("%s: X", __func__);
    return ret;
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
          cam_ctrl_dimension_t dim = mDimension;
          status_t ret = cam_config_get_parm(mCameraId,
            MM_CAMERA_PARM_DIMENSION, &dim);
          int file_fd;
          switch (frm_type) {
          case  HAL_DUMP_FRM_PREVIEW:
            w = dim.display_width;
            h = dim.display_height;
            snprintf(buf, sizeof(buf), "/data/%dp_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_VIDEO:
            w = dim.video_width;
            h = dim.video_height;
            snprintf(buf, sizeof(buf),"/data/%dv_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_MAIN:
            w = dim.picture_width;
            h = dim.picture_height;
            snprintf(buf, sizeof(buf), "/data/%dm_%dx%d.yuv", mDumpFrmCnt, w, h);
            file_fd = open(buf, O_RDWR | O_CREAT, 0777);
            break;
          case HAL_DUMP_FRM_THUMBNAIL:
            w = dim.ui_thumbnail_width;
            h = dim.ui_thumbnail_height;
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

}; // namespace android

