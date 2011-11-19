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
//#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHWI_Record"
#include <utils/Log.h>
#include <utils/threads.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "QCameraStream.h"


#define LIKELY(exp)   __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)

/* QCameraStream_record class implementation goes here*/
/* following code implement the video streaming capture & encoding logic of this class*/
// ---------------------------------------------------------------------------
// QCameraStream_record createInstance()
// ---------------------------------------------------------------------------
namespace android {

QCameraStream* QCameraStream_record::createInstance(int cameraId,
                                      camera_mode_t mode)
{
  LOGV("%s: BEGIN", __func__);
  QCameraStream* pme = new QCameraStream_record(cameraId, mode);
  LOGV("%s: END", __func__);
  return pme;
}

// ---------------------------------------------------------------------------
// QCameraStream_record deleteInstance()
// ---------------------------------------------------------------------------
void QCameraStream_record::deleteInstance(QCameraStream *ptr)
{
  LOGV("%s: BEGIN", __func__);
  if (ptr){
    ptr->release();
    delete ptr;
    ptr = NULL;
  }
  LOGV("%s: END", __func__);
}

// ---------------------------------------------------------------------------
// QCameraStream_record Constructor
// ---------------------------------------------------------------------------
QCameraStream_record::QCameraStream_record(int cameraId,
                                           camera_mode_t mode)
  :QCameraStream(cameraId,mode),
  mDebugFps(false),
  snapshot_enabled(false)
{
  mHalCamCtrl = NULL;
  char value[PROPERTY_VALUE_MAX];
  LOGV("%s: BEGIN", __func__);

  property_get("persist.debug.sf.showfps", value, "0");
  mDebugFps = atoi(value);

  LOGV("%s: END", __func__);
}

// ---------------------------------------------------------------------------
// QCameraStream_record Destructor
// ---------------------------------------------------------------------------
QCameraStream_record::~QCameraStream_record() {
  LOGV("%s: BEGIN", __func__);
  if(mActive) {
    stop();
  }
  if(mInit) {
    release();
  }
  mInit = false;
  mActive = false;
  LOGV("%s: END", __func__);

}

// ---------------------------------------------------------------------------
// QCameraStream_record
// ---------------------------------------------------------------------------
status_t QCameraStream_record::init()
{
  status_t ret = NO_ERROR;
  LOGV("%s: BEGIN", __func__);

/*
  *  Acquiring Video Channel
  */
  ret = QCameraStream::initChannel (mCameraId, MM_CAMERA_CH_VIDEO_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s ERROR: Can't init native cammera preview ch\n",__func__);
    return ret;
  }
  mInit = true;
  LOGV("%s: END", __func__);
  return ret;
}
// ---------------------------------------------------------------------------
// QCameraStream_record
// ---------------------------------------------------------------------------

status_t QCameraStream_record::start()
{
  status_t ret = NO_ERROR;
  LOGV("%s: BEGIN", __func__);

  if(!mInit) {
    LOGE("%s ERROR: Record buffer not registered",__func__);
    return BAD_VALUE;
  }

  mRecordFreeQueueLock.lock();
  mRecordFreeQueue.clear();
  mRecordFreeQueueLock.unlock();

  /*
  *  Allocating Encoder Frame Buffers
  */
  ret = initEncodeBuffers();
  if (NO_ERROR!=ret) {
    LOGE("%s ERROR: Buffer Allocation Failed\n",__func__);
    return ret;
  }

  ret = start_stream(MM_CAMERA_CH_VIDEO_MASK,mRecordHeap);
  if(NO_ERROR != ret){
    LOGE("%s: X - start stream error", __func__);
    mRecordHeap.clear();
    mRecordHeap = NULL;
    return BAD_VALUE;
  }
  mActive = true;
  LOGV("%s: END", __func__);
  return ret;
}

// ---------------------------------------------------------------------------
// QCameraStream_record
// ---------------------------------------------------------------------------
void QCameraStream_record::stop()
{
  status_t ret = NO_ERROR;
  LOGV("%s: BEGIN", __func__);

  if(!mActive) {
    LOGE("%s : Record stream not started",__func__);
    return;
  }

  mRecordFreeQueueLock.lock();
  while(!mRecordFreeQueue.isEmpty()) {
    LOGV("%s : Pre-releasing of Encoder buffers!\n", __FUNCTION__);
    mm_camera_ch_data_buf_t releasedBuf = mRecordFreeQueue.itemAt(0);
    mRecordFreeQueue.removeAt(0);
    mRecordFreeQueueLock.unlock();
    LOGV("%s (%d): releasedBuf.idx = %d\n", __FUNCTION__, __LINE__,
                                              releasedBuf.video.video.idx);
    if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId,&releasedBuf))
        LOGE("%s : Buf Done Failed",__func__);
  }
  mRecordFreeQueueLock.unlock();

  mActive = false;
  Mutex::Autolock lock(mStopCallbackLock);
  stop_stream(MM_CAMERA_CH_VIDEO_MASK);
  if(mRecordHeap != NULL) {
    mRecordHeap.clear();
    mRecordHeap = NULL;
  }

  LOGV("%s: END", __func__);

}
// ---------------------------------------------------------------------------
// QCameraStream_record
// ---------------------------------------------------------------------------
void QCameraStream_record::release()
{
  status_t ret = NO_ERROR;
  LOGV("%s: BEGIN", __func__);

  if(mActive) {
    stop();
  }
  if(!mInit) {
    LOGE("%s : Record stream not initialized",__func__);
    return;
  }

  ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_VIDEO);
  if(ret != MM_CAMERA_OK) {
    LOGE("%s:Deinit Video channel failed=%d\n", __func__, ret);
  }

  mInit = false;
  LOGV("%s: END", __func__);
}

status_t QCameraStream_record::processRecordFrame(void *data)
{
  LOGV("%s : BEGIN",__func__);
  mm_camera_ch_data_buf_t* frame = (mm_camera_ch_data_buf_t*) data;
  Mutex::Autolock lock(mStopCallbackLock);

  if(!mActive) {
      if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, frame)){
          LOGE("%s : BUF DONE FAILED",__func__);
      }
      return NO_ERROR;
  }

  if (UNLIKELY(mDebugFps)) {
    debugShowVideoFPS();
  }

  mHalCamCtrl->dumpFrameToFile(frame->video.video.frame, HAL_DUMP_FRM_VIDEO);

  mHalCamCtrl->mCallbackLock.lock();
  data_callback_timestamp rcb = mHalCamCtrl->mDataCbTimestamp;
  void *rdata = mHalCamCtrl->mCallbackCookie;
  mHalCamCtrl->mCallbackLock.unlock();

  mRecordFreeQueueLock.lock();
  mRecordFreeQueue.add(*frame);
  mRecordFreeQueueLock.unlock();

  nsecs_t timeStamp = nsecs_t(frame->video.video.frame->ts.tv_sec)*1000000000LL + \
                      frame->video.video.frame->ts.tv_nsec;

  if(snapshot_enabled) {
    LOGI("Live Snapshot Enabled");
    frame->snapshot.main.frame = frame->video.video.frame;
    frame->snapshot.main.idx = frame->video.video.idx;
    frame->snapshot.thumbnail.frame = frame->video.video.frame;
    frame->snapshot.thumbnail.idx = frame->video.video.idx;

    dim.picture_width = mHalCamCtrl->mDimension.video_width;
    dim.picture_height = mHalCamCtrl->mDimension.video_height;
    dim.ui_thumbnail_width = mHalCamCtrl->mDimension.display_width;
    dim.ui_thumbnail_height = mHalCamCtrl->mDimension.display_height;

    mJpegMaxSize = mHalCamCtrl->mDimension.video_width * mHalCamCtrl->mDimension.video_width * 1.5;
#if 0
    LOGI("Picture w = %d , h = %d, size = %d",dim.picture_width,dim.picture_height,mJpegMaxSize);
     if (mStreamSnap){
        LOGE("%s:Deleting old Snapshot stream instance",__func__);
        QCameraStream_Snapshot::deleteInstance (mStreamSnap);
        mStreamSnap = NULL;
    }

    mStreamSnap = QCameraStream_Snapshot::createInstance(mCameraId,
                                                       myMode);

    if (!mStreamSnap) {
        LOGE("%s: error - can't creat snapshot stream!", __func__);
        return BAD_VALUE;
    }
    //mStreamSnap->setHALCameraControl(this->mHalCamCtrl);
    //mStreamSnap->takePictureLiveshot(frame,&dim,mJpegMaxSize);
#endif
    mHalCamCtrl->mStreamSnap->takePictureLiveshot(frame,&dim,mJpegMaxSize);
    LOGI("Calling takePictureLiveshot %d",record_frame_len);
    snapshot_enabled = false;
  }

#if 1
  if(rcb != NULL && (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
      LOGE("Send Video frame to services/encoder idx = %d, TimeStamp : %lld",frame->video.video.idx,timeStamp);
      mStopCallbackLock.unlock();
      if(mActive) {
          rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[frame->video.video.idx], rdata);
      }
      LOGE("rcb returned");
  }else{
      if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, frame)){
        LOGE("%s : BUF DONE FAILED",__func__);
      }
      return NO_ERROR;
  }
#else  //Dump the Frame
    {
      static int frameCnt = 0;
      if (frameCnt <= 13 ) {
        char buf[128];
        snprintf(buf, sizeof(buf), "/data/%d_video.yuv", frameCnt);
        int file_fd = open(buf, O_RDWR | O_CREAT, 0777);
        LOGE("dumping video frame %d", frameCnt);
        if (file_fd < 0) {
          LOGE("cannot open file\n");
        }
        else
        {
          LOGE("Dump Frame size = %d",record_frame_len);
          write(file_fd, (const void *)(const void *)frame->video.video.frame->buffer,
          record_frame_len);
        }
        close(file_fd);
      }
      frameCnt++;
    }
    if(MM_CAMERA_OK! = cam_evt_buf_done(mCameraId, frame))
      LOGE("%s : BUF DONE FAILED",__func__);
#endif
  LOGV("%s : END",__func__);
  return NO_ERROR;
}

//Record Related Functions
status_t QCameraStream_record::initEncodeBuffers()
{
  LOGV("%s : BEGIN",__func__);
  status_t ret = NO_ERROR;
  const char *pmem_region;
  uint32_t frame_len;
  //cam_ctrl_dimension_t dim;
  int width = 0;  /* width of channel  */
  int height = 0; /* height of channel */

  pmem_region = "/dev/pmem_adsp";
  memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
  ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
  if (MM_CAMERA_OK != ret) {
    LOGE("%s: ERROR - can't get camera dimension!", __func__);
    return BAD_VALUE;
  }
  else {
    width =  dim.video_width;
    height = dim.video_height;
  }

  num_planes = 0;
  frame_len = mm_camera_get_msm_frame_len(dim.enc_format , CAMERA_MODE_2D,
                                 width,height, OUTPUT_TYPE_V,
                                 &num_planes, planes);
  record_frame_len = frame_len;
  if(mRecordHeap == NULL)
  {
#ifdef USE_ION
    mRecordHeap = new IonPool(
                        MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                        frame_len,
                        VIDEO_BUFFER_COUNT,
                        frame_len,
                        planes[0],
                        0,
                        "record");
#else
    mRecordHeap = new PmemPool(pmem_region,
                        MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                        MSM_PMEM_VIDEO,
                        frame_len,
                        VIDEO_BUFFER_COUNT,
                        frame_len,
                        planes[0],
                        0,
                        "record");
#endif
    if (!mRecordHeap->initialized()) {
      mRecordHeap.clear();
      mRecordHeap = NULL;
      LOGE("%s: ERROR : could not initialize record heap.",__func__);
      return BAD_VALUE;
    }
  }
  LOGI("PMEM Buffer Allocation Successfull");
  LOGI("%s : END",__func__);
	return ret;
}

void QCameraStream_record::releaseRecordingFrame(const sp<IMemory>& mem)
{
  ssize_t offset;
  size_t size;
  int i = 0;
  unsigned int dst = 0;
  bool found = false;

  LOGV("%s : BEGIN",__func__);
  if(mem == NULL) {
    return;
  }
  if(!mActive)
  {
    LOGE("%s : Recording already stopped!!! Leak???",__func__);
    return;
  }

  sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);
  dst = (unsigned int)heap->base()+ offset;

  mm_camera_ch_data_buf_t releasedBuf;
  mRecordFreeQueueLock.lock();
  LOGI("%s (%d): mRecordFreeQueue has %d entries.\n", __FUNCTION__, __LINE__,
                                            mRecordFreeQueue.size());
  while(!mRecordFreeQueue.isEmpty()){
    LOGD("src = %d dst = %d",((unsigned int)(mRecordFreeQueue.itemAt(i)).video.video.frame->buffer),dst);
    if(dst ==	((unsigned int)(mRecordFreeQueue.itemAt(i)).video.video.frame->buffer)) {
      releasedBuf = mRecordFreeQueue.itemAt(i);
      mRecordFreeQueue.removeAt(i);
      found = true;
      break;
    }
    i++;
  }
  mRecordFreeQueueLock.unlock();
  if(!found){
    LOGE(" No Matching Buffer.. return");
    return;
  }
  LOGI("%s (%d): releasedBuf.idx = %d\n", __FUNCTION__, __LINE__,
                                            releasedBuf.video.video.idx);

  if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, &releasedBuf)){
      LOGE("%s : Buf Done Failed",__func__);
  }
  LOGV("%s : END",__func__);
  return;
}

status_t QCameraStream_record::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize)
{
  LOGV("%s: BEGIN", __func__);
  status_t ret;
  if( mRecordHeap != NULL){
    LOGE(" Setting valid buffer information allignedSize ");
    Frame = mRecordHeap->mBuffers[0];
    if( alignedSize != NULL) {
      *alignedSize = mRecordHeap->mAlignedBufferSize;
      LOGV(" HAL : alignedSize = %d ", *alignedSize);
      ret = NO_ERROR;
    } else {
      LOGE(" HAL : alignedSize is NULL. Cannot update alignedSize ");
      ret = UNKNOWN_ERROR;
    }
  } else {
    LOGE(" RecordHeap is null. Buffer information wont be updated ");
    Frame = NULL;
    ret = UNKNOWN_ERROR;
  }
  LOGV("%s: X", __func__);
  return ret;
}

void QCameraStream_record::debugShowVideoFPS() const
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
    LOGI("Video Frames Per Second: %.4f", mFps);
    mLastFpsTime = now;
    mLastFrameCount = mFrameCount;
  }
}

sp<IMemoryHeap> QCameraStream_record::getHeap() const
{
  return mRecordHeap != NULL ? mRecordHeap->mHeap : NULL;
}

status_t  QCameraStream_record::takeLiveSnapshot()
{
  //snapshotframes = new msm_frame[1];
  //memset(snapshotframes,0,sizeof(struct msm_frame));
  //mJpegMaxSize = dim.video_width * dim.video_height * 1.5;
  LOGV("%s: BEGIN", __func__);
  snapshot_enabled = true;
  LOGV("%s: END", __func__);
  return true;
}

}//namespace android

