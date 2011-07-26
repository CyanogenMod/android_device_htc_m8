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

QCameraStream* QCameraStream_record::createInstance(mm_camera_t *native_camera,
                                      camera_mode_t mode)
{
  LOGV("%s: BEGIN", __func__);
  QCameraStream* pme = new QCameraStream_record(native_camera, mode);
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
  }
  LOGV("%s: END", __func__);
}

// ---------------------------------------------------------------------------
// QCameraStream_record Constructor
// ---------------------------------------------------------------------------
QCameraStream_record::QCameraStream_record(mm_camera_t *native_camera,
                                           camera_mode_t mode)
  :QCameraStream(),
  mmCamera(native_camera),
  myMode (mode),
  mDebugFps(false)
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
// QCameraStream_record Callback from mm_camera
// ---------------------------------------------------------------------------
static void record_notify_cb(mm_camera_ch_data_buf_t *bufs_new,
                              void *user_data)
{
  QCameraStream_record *pme = (QCameraStream_record *)user_data;
  mm_camera_ch_data_buf_t *bufs_used = 0;
  LOGV("%s: BEGIN", __func__);

  /*
  * Call Function Process Video Data
  */
  pme->processRecordFrame(bufs_new);
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
  *  Allocating Encoder Frame Buffers
  */
  ret = initEncodeBuffers();
  if (NO_ERROR!=ret) {
    LOGE("%s ERROR: Buffer Allocation Failed\n",__func__);
    return ret;
  }

  /*
  *  Acquiring Video Channel
  */
  ret = QCameraStream::initChannel (mmCamera, MM_CAMERA_CH_VIDEO_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s ERROR: Can't init native cammera preview ch\n",__func__);
    return ret;
  }

  ret = mmCamera->cfg->prepare_buf(mmCamera, &mRecordBuf);
  if(ret != MM_CAMERA_OK) {
    LOGV("%s ERROR: Reg preview buf err=%d\n", __func__, ret);
    ret = BAD_VALUE;
  }else{
    ret = NO_ERROR;
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

  /* yyan TODO: call start() in parent class to start the monitor thread*/
  //QCameraStream::start ();

  /* yyan TODO: register a notify into the mmmm_camera_t object*/
  if(!mInit) {
    LOGE("%s ERROR: Record buffer not registered",__func__);
    return BAD_VALUE;
  }

  /*
  * Register the Callback with mmcamera
  */
  (void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_VIDEO,
                                            record_notify_cb,
                                            this);

  /*
  * Start Video Streaming
  */
  ret = mmCamera->ops->action(mmCamera, TRUE, MM_CAMERA_OPS_VIDEO, 0);
  if (MM_CAMERA_OK != ret) {
    LOGE ("%s ERROR: Video streaming start err=%d\n", __func__, ret);
    ret = BAD_VALUE;
  }else{
    LOGE("%s : Video streaming Started",__func__);
    ret = NO_ERROR;
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
    LOGE("%s : Pre-releasing of Encoder buffers!\n", __FUNCTION__);
    mm_camera_ch_data_buf_t releasedBuf = mRecordFreeQueue.itemAt(0);
    mRecordFreeQueue.removeAt(0);
    mRecordFreeQueueLock.unlock();
    LOGE("%s (%d): releasedBuf.idx = %d\n", __FUNCTION__, __LINE__,
                                              releasedBuf.video.video.idx);
    if(MM_CAMERA_OK!=mmCamera->evt->buf_done(mmCamera,&releasedBuf))
        LOGE("%s : Buf Done Failed",__func__);
  }
  mRecordFreeQueueLock.unlock();
#if 0
  while (!mRecordFreeQueue.isEmpty()) {
        LOGE("%s : Waiting for Encoder to release all buffer!\n", __FUNCTION__);
  }
#endif
  /* yyan TODO: unregister the notify fn from the mmmm_camera_t object*/

  /* yyan TODO: call stop() in parent class to stop the monitor thread*/

  ret = mmCamera->ops->action(mmCamera, FALSE, MM_CAMERA_OPS_VIDEO, 0);
  if (MM_CAMERA_OK != ret) {
    LOGE ("%s ERROR: Video streaming Stop err=%d\n", __func__, ret);
  }

 (void)mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_VIDEO,
                                            NULL,
                                            NULL);
  mActive = false;
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

  ret = mmCamera->cfg->unprepare_buf(mmCamera,MM_CAMERA_CH_VIDEO);
  if(ret != MM_CAMERA_OK){
    LOGE("%s ERROR: Ureg video buf \n", __func__);
  }

  ret= QCameraStream::deinitChannel(mmCamera,MM_CAMERA_CH_VIDEO);
  if(ret != MM_CAMERA_OK) {
    LOGE("%s:Deinit Video channel failed=%d\n", __func__, ret);
  }

  mRecordHeap.clear();
  mRecordHeap = NULL;

  delete[] recordframes;
  mInit = false;
  LOGV("%s: END", __func__);
}

status_t QCameraStream_record::processRecordFrame(void *data)
{
  LOGE("%s : BEGIN",__func__);
  mm_camera_ch_data_buf_t* frame = (mm_camera_ch_data_buf_t*) data;


  if (UNLIKELY(mDebugFps)) {
    debugShowVideoFPS();
  }

  mHalCamCtrl->mCallbackLock.lock();
  data_callback_timestamp rcb = mHalCamCtrl->mDataCbTimestamp;
  void *rdata = mHalCamCtrl->mCallbackCookie;
  mHalCamCtrl->mCallbackLock.unlock();

  mRecordFreeQueueLock.lock();
  mRecordFreeQueue.add(*frame);
  mRecordFreeQueueLock.unlock();

  nsecs_t timeStamp = nsecs_t(frame->video.video.frame->ts.tv_sec)*1000000000LL + \
                      frame->video.video.frame->ts.tv_nsec;

  LOGE("Send Video frame to services/encoder TimeStamp : %lld",timeStamp);
#if 1
   rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[frame->video.video.idx], rdata);
#else  //Dump the Frame
    {
      static int frameCnt = 0;
      if (frameCnt <= 13 ) {
        char buf[128];
        sprintf(buf, "/data/%d_video.yuv", frameCnt);
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
    if(MM_CAMERA_OK!=mmCamera->evt->buf_done(mmCamera,frame))
      LOGE("%s : BUF DONE FAILED",__func__);
#endif
  LOGE("%s : END",__func__);
  return NO_ERROR;
}

//Record Related Functions
status_t QCameraStream_record::initEncodeBuffers()
{
  LOGE("%s : BEGIN",__func__);
  status_t ret = NO_ERROR;
  const char *pmem_region;
  uint32_t y_off,cbcr_off,frame_len;
  //cam_ctrl_dimension_t dim;
  int width = 0;  /* width of channel  */
  int height = 0; /* height of channel */

  if(mRecordHeap == NULL)
  {
    pmem_region = "/dev/pmem_adsp";
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = mmCamera->cfg->get_parm(mmCamera, MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: ERROR - can't get camera dimension!", __func__);
      return BAD_VALUE;
    }
    else {
      width =  dim.video_width;
      height = dim.video_height;
    }

    //myMode=CAMERA_MODE_2D; /*Need to assign this in constructor after translating from mask*/
    frame_len = mm_camera_get_msm_frame_len(dim.enc_format , CAMERA_MODE_2D,
                                   width,height, &y_off, &cbcr_off, MM_CAMERA_PAD_2K);
    record_frame_len = frame_len;
    mRecordHeap = new PmemPool(pmem_region,
                        MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                        MSM_PMEM_VIDEO,
                        frame_len,
                        VIDEO_BUFFER_COUNT,
                        frame_len,
                        cbcr_off,
                        0,
                        "record");
    if (!mRecordHeap->initialized()) {
      mRecordHeap.clear();
      mRecordHeap = NULL;
      LOGE("%s: ERROR : could not initialize record heap.",__func__);
      return BAD_VALUE;
    }
   } else {
    /*if(mHFRMode == true) {
    LOGI("%s: register record buffers with camera driver", __FUNCTION__);
    register_record_buffers(true);
    mHFRMode = false;
    }*/
  }
  LOGE("PMEM Buffer Allocation Successfull");
  recordframes = new msm_frame[VIDEO_BUFFER_COUNT];
  memset(recordframes,0,sizeof(struct msm_frame) * VIDEO_BUFFER_COUNT);
  for (int cnt = 0; cnt < VIDEO_BUFFER_COUNT; cnt++) {
    recordframes[cnt].fd = mRecordHeap->mHeap->getHeapID();
    recordframes[cnt].buffer =
        (uint32_t)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
    recordframes[cnt].y_off = y_off;
    recordframes[cnt].cbcr_off = cbcr_off;
    recordframes[cnt].path = OUTPUT_TYPE_V;
    //record_buffers_tracking_flag[cnt] = false;
    record_offset[cnt] =  mRecordHeap->mAlignedBufferSize * cnt;
    LOGE ("initRecord :  record heap , video buffers  buffer=%lu fd=%d y_off=%d cbcr_off=%d, offset = %d \n",
      (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, recordframes[cnt].y_off,
      recordframes[cnt].cbcr_off, record_offset[cnt]);
  }
  memset(&mRecordBuf, 0, sizeof(mRecordBuf));
  mRecordBuf.ch_type = MM_CAMERA_CH_VIDEO;
  mRecordBuf.video.video.num = VIDEO_BUFFER_COUNT;//kRecordBufferCount;
  mRecordBuf.video.video.frame_offset = &record_offset[0];
  mRecordBuf.video.video.frame = &recordframes[0];
  LOGE("Record buf type =%d, offset[1] =%d, buffer[1] =%lx", mRecordBuf.ch_type, record_offset[1], recordframes[1].buffer);
  LOGE("%s : END",__func__);
  return NO_ERROR;
}

void QCameraStream_record::releaseRecordingFrame(const sp<IMemory>& mem)
{
  LOGE("%s : BEGIN",__func__);
  if(!mActive)
  {
    LOGE("%s : Recording already stopped!!! Leak???",__func__);
    return;
  }
  mRecordFreeQueueLock.lock();
  if (mRecordFreeQueue.isEmpty()) {
        LOGE("%s (%d): mRecordFreeQueue is empty!\n", __FUNCTION__, __LINE__);
        mRecordFreeQueueLock.unlock();
        return;
  }
  LOGE("%s (%d): mRecordFreeQueue has %d entries.\n", __FUNCTION__, __LINE__,
                                            mRecordFreeQueue.size());
  mm_camera_ch_data_buf_t releasedBuf = mRecordFreeQueue.itemAt(0);
  mRecordFreeQueue.removeAt(0);
  mRecordFreeQueueLock.unlock();
  LOGI("%s (%d): releasedBuf.idx = %d\n", __FUNCTION__, __LINE__,
                                            releasedBuf.video.video.idx);
  if(MM_CAMERA_OK!=mmCamera->evt->buf_done(mmCamera,&releasedBuf))
      LOGE("%s : Buf Done Failed",__func__);
  LOGE("%s : END",__func__);
  return;
}

status_t QCameraStream_record::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize)
{
  LOGE("%s: BEGIN", __func__);
  status_t ret;
  if( mRecordHeap != NULL){
    LOGE(" Setting valid buffer information allignedSize ");
    Frame = mRecordHeap->mBuffers[0];
    if( alignedSize != NULL) {
      *alignedSize = mRecordHeap->mAlignedBufferSize;
      LOGE(" HAL : alignedSize = %d ", *alignedSize);
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
  LOGE("%s: X", __func__);
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
}//namespace android

