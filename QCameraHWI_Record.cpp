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
  }
  LOGV("%s: END", __func__);
}

// ---------------------------------------------------------------------------
// QCameraStream_record Constructor
// ---------------------------------------------------------------------------
QCameraStream_record::QCameraStream_record(int cameraId,
                                           camera_mode_t mode)
  :QCameraStream(),
  mCameraId(cameraId),
  myMode (mode),
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
  ret = QCameraStream::initChannel (mCameraId, MM_CAMERA_CH_VIDEO_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s ERROR: Can't init native cammera preview ch\n",__func__);
    return ret;
  }

  ret = cam_config_prepare_buf(mCameraId, &mRecordBuf);
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

	if(!mInit) {
    LOGE("%s ERROR: Record buffer not registered",__func__);
    return BAD_VALUE;
  }

  /*
  * Register the Callback with camera
  */
  (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_VIDEO,
                                            record_notify_cb,
                                            this);

	/*
  * Start Video Streaming
  */
  ret = cam_ops_action(mCameraId, TRUE, MM_CAMERA_OPS_VIDEO, 0);
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
#if 0 //mzhu, when stop recording, all frame will be dirty. no need to queue frame back to kernel any more
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
#if 0
  while (!mRecordFreeQueue.isEmpty()) {
        LOGE("%s : Waiting for Encoder to release all buffer!\n", __FUNCTION__);
  }
#endif
#endif // mzhu
  /* unregister the notify fn from the mmmm_camera_t object
   *  call stop() in parent class to stop the monitor thread */

  ret = cam_ops_action(mCameraId, FALSE, MM_CAMERA_OPS_VIDEO, 0);
  if (MM_CAMERA_OK != ret) {
    LOGE ("%s ERROR: Video streaming Stop err=%d\n", __func__, ret);
  }

 (void)cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_VIDEO,
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

  ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_VIDEO);
  if(ret != MM_CAMERA_OK){
    LOGE("%s ERROR: Ureg video buf \n", __func__);
  }

  ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_VIDEO);
  if(ret != MM_CAMERA_OK) {
    LOGE("%s:Deinit Video channel failed=%d\n", __func__, ret);
  }

  for(int cnt = 0; cnt < mHalCamCtrl->mRecordingMemory.buffer_count; cnt++) {
	  mHalCamCtrl->mRecordingMemory.camera_memory[cnt]->release(
		  mHalCamCtrl->mRecordingMemory.camera_memory[cnt]);
	  close(mHalCamCtrl->mRecordingMemory.fd[cnt]);
#ifdef USE_ION
    mHalCamCtrl->deallocate_ion_memory(&mHalCamCtrl->mRecordingMemory, cnt);
#endif
  }
  memset(&mHalCamCtrl->mRecordingMemory, 0, sizeof(mHalCamCtrl->mRecordingMemory));
  //mNumRecordFrames = 0;
  delete[] recordframes;
  if (mRecordBuf.video.video.buf.mp)
    delete[] mRecordBuf.video.video.buf.mp;

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


    //mHalCamCtrl->dumpFrameToFile(frame->video.video.frame, HAL_DUMP_FRM_VIDEO);
    mHalCamCtrl->mCallbackLock.lock();
    camera_data_timestamp_callback rcb = mHalCamCtrl->mDataCbTimestamp;
    void *rdata = mHalCamCtrl->mCallbackCookie;
    mHalCamCtrl->mCallbackLock.unlock();

    mRecordedFrames[frame->video.video.idx] = *frame;

    nsecs_t timeStamp = nsecs_t(frame->video.video.frame->ts.tv_sec)*1000000000LL + \
                      frame->video.video.frame->ts.tv_nsec;

  if(snapshot_enabled) {
    LOGE("Live Snapshot Enabled");
    frame->snapshot.main.frame = frame->video.video.frame;
    frame->snapshot.main.idx = frame->video.video.idx;
    frame->snapshot.thumbnail.frame = frame->video.video.frame;
    frame->snapshot.thumbnail.idx = frame->video.video.idx;

    dim.picture_width = mHalCamCtrl->mDimension.video_width;
    dim.picture_height = mHalCamCtrl->mDimension.video_height;
    dim.ui_thumbnail_width = mHalCamCtrl->mDimension.display_width;
    dim.ui_thumbnail_height = mHalCamCtrl->mDimension.display_height;

    mJpegMaxSize = mHalCamCtrl->mDimension.video_width * mHalCamCtrl->mDimension.video_width * 1.5;

    LOGE("Picture w = %d , h = %d, size = %d",dim.picture_width,dim.picture_height,mJpegMaxSize);
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
    mStreamSnap->setHALCameraControl(this->mHalCamCtrl);
    mStreamSnap->takePictureLiveshot(frame,&dim,mJpegMaxSize);

    snapshot_enabled = false;
  }

  LOGE("Send Video frame to services/encoder TimeStamp : %lld",timeStamp);
#if 1
   //rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[frame->video.video.idx], rdata);
	 rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME,
            mHalCamCtrl->mRecordingMemory.camera_memory[frame->video.video.idx],
            0, mHalCamCtrl->mCallbackCookie);
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
  LOGE("%s : END",__func__);
  return NO_ERROR;
}

//Record Related Functions
status_t QCameraStream_record::initEncodeBuffers()
{
  LOGE("%s : BEGIN",__func__);
  status_t ret = NO_ERROR;
  const char *pmem_region;
  uint32_t frame_len;
  uint8_t num_planes;
  uint32_t planes[VIDEO_MAX_PLANES];
  //cam_ctrl_dimension_t dim;
  int width = 0;  /* width of channel  */
  int height = 0; /* height of channel */

  pmem_region = "/dev/pmem_adsp";


  memset(&mHalCamCtrl->mRecordingMemory, 0, sizeof(mHalCamCtrl->mRecordingMemory));
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

  //myMode=CAMERA_MODE_2D; /*Need to assign this in constructor after translating from mask*/
  frame_len = mm_camera_get_msm_frame_len(dim.enc_format , CAMERA_MODE_2D,
                                 width,height, OUTPUT_TYPE_V,
                                 &num_planes, planes);

#if 0
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
   } else {
    /*if(mHFRMode == true) {
    LOGI("%s: register record buffers with camera driver", __FUNCTION__);
    register_record_buffers(true);
    mHFRMode = false;
    }*/
  }
	LOGE("PMEM Buffer Allocation Successfull");
#endif

#if 0
  memset(&mRecordBuf, 0, sizeof(mRecordBuf));
  /* allocate memory for mplanar frame struct. */
  mRecordBuf.video.video.buf.mp = new mm_camera_mp_buf_t[VIDEO_BUFFER_COUNT *
                                  sizeof(mm_camera_mp_buf_t)];
  if (!mRecordBuf.video.video.buf.mp) {
    LOGE("%s Error allocating memory for mplanar struct ", __func__);
    mRecordHeap.clear();
    mRecordHeap = NULL;
    return BAD_VALUE;
  }
  memset(mRecordBuf.video.video.buf.mp, 0,
         VIDEO_BUFFER_COUNT * sizeof(mm_camera_mp_buf_t));
  mRecordBuf.ch_type = MM_CAMERA_CH_VIDEO;
  mRecordBuf.video.video.num = VIDEO_BUFFER_COUNT;//kRecordBufferCount;
  recordframes = new msm_frame[VIDEO_BUFFER_COUNT];
  if(recordframes != NULL) {
    memset(recordframes,0,sizeof(struct msm_frame) * VIDEO_BUFFER_COUNT);
    for (int cnt = 0; cnt < VIDEO_BUFFER_COUNT; cnt++) {
      recordframes[cnt].fd = mRecordHeap->mHeap->getHeapID();
      recordframes[cnt].buffer =
          (uint32_t)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
      recordframes[cnt].y_off = 0;
      recordframes[cnt].cbcr_off = planes[0];
      recordframes[cnt].path = OUTPUT_TYPE_V;
      //record_buffers_tracking_flag[cnt] = false;
      record_offset[cnt] =  mRecordHeap->mAlignedBufferSize * cnt;
      LOGE ("initRecord :  record heap , video buffers  buffer=%lu fd=%d offset = %d \n",
        (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, record_offset[cnt]);
      mRecordBuf.video.video.buf.mp[cnt].frame = recordframes[cnt];
      mRecordBuf.video.video.buf.mp[cnt].frame_offset = record_offset[cnt];
      mRecordBuf.video.video.buf.mp[cnt].num_planes = num_planes;
      /* Plane 0 needs to be set seperately. Set other planes
       * in a loop. */
      mRecordBuf.video.video.buf.mp[cnt].planes[0].reserved[0] =
        mRecordBuf.video.video.buf.mp[cnt].frame_offset;
      mRecordBuf.video.video.buf.mp[cnt].planes[0].length = planes[0];
      mRecordBuf.video.video.buf.mp[cnt].planes[0].m.userptr =
        recordframes[cnt].fd;
      for (int j = 1; j < num_planes; j++) {
        mRecordBuf.video.video.buf.mp[cnt].planes[j].length = planes[j];
        mRecordBuf.video.video.buf.mp[cnt].planes[j].m.userptr =
          recordframes[cnt].fd;
        mRecordBuf.video.video.buf.mp[cnt].planes[j].reserved[0] =
          mRecordBuf.video.video.buf.mp[cnt].planes[j-1].reserved[0] +
          mRecordBuf.video.video.buf.mp[cnt].planes[j-1].length;
      }
    }
    LOGE("Record buf type =%d, offset[1] =%d, buffer[1] =%lx", mRecordBuf.ch_type, record_offset[1], recordframes[1].buffer);
    LOGE("%s : END",__func__);
  } else {
    ret = NO_MEMORY;
  }
  return ret;
#endif

    recordframes = new msm_frame[VIDEO_BUFFER_COUNT];
    memset(recordframes,0,sizeof(struct msm_frame) * VIDEO_BUFFER_COUNT);

		mRecordBuf.video.video.buf.mp = new mm_camera_mp_buf_t[VIDEO_BUFFER_COUNT *
                                  sizeof(mm_camera_mp_buf_t)];
		if (!mRecordBuf.video.video.buf.mp) {
			LOGE("%s Error allocating memory for mplanar struct ", __func__);
			return BAD_VALUE;
		}
		memset(mRecordBuf.video.video.buf.mp, 0,
					 VIDEO_BUFFER_COUNT * sizeof(mm_camera_mp_buf_t));

    memset(&mHalCamCtrl->mRecordingMemory, 0, sizeof(mHalCamCtrl->mRecordingMemory));
    mHalCamCtrl->mRecordingMemory.buffer_count = VIDEO_BUFFER_COUNT;

		mHalCamCtrl->mRecordingMemory.size = frame_len;
		mHalCamCtrl->mRecordingMemory.cbcr_offset = planes[0];

    for (int cnt = 0; cnt < mHalCamCtrl->mRecordingMemory.buffer_count; cnt++) {
#ifdef USE_ION
      if(mHalCamCtrl->allocate_ion_memory(&mHalCamCtrl->mRecordingMemory, cnt, ION_HEAP_ADSP_ID) < 0) {
        LOGE("%s ION alloc failed\n", __func__);
        return UNKNOWN_ERROR;
      }
#else
		  mHalCamCtrl->mRecordingMemory.fd[cnt] = open("/dev/pmem_adsp", O_RDWR|O_SYNC);
		  if(mHalCamCtrl->mRecordingMemory.fd[cnt] <= 0) {
			  LOGE("%s: no pmem for frame %d", __func__, cnt);
			  return UNKNOWN_ERROR;
		  }
#endif
		  mHalCamCtrl->mRecordingMemory.camera_memory[cnt] =
		    mHalCamCtrl->mGetMemory(mHalCamCtrl->mRecordingMemory.fd[cnt],
		  mHalCamCtrl->mRecordingMemory.size, 1, (void *)this);
    	recordframes[cnt].fd = mHalCamCtrl->mRecordingMemory.fd[cnt];
    	recordframes[cnt].buffer = (uint32_t)mHalCamCtrl->mRecordingMemory.camera_memory[cnt]->data;
	    recordframes[cnt].y_off = 0;
	    recordframes[cnt].cbcr_off = mHalCamCtrl->mRecordingMemory.cbcr_offset;
	    recordframes[cnt].path = OUTPUT_TYPE_V;
			//record_offset[cnt] =  mRecordHeap->mAlignedBufferSize * cnt;

	    //record_buffers_tracking_flag[cnt] = false;
	    //record_offset[cnt] =  0;
	    LOGE ("initRecord :  record heap , video buffers  buffer=%lu fd=%d y_off=%d cbcr_off=%d\n",
		    (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, recordframes[cnt].y_off,
		    recordframes[cnt].cbcr_off);
	    //mNumRecordFrames++;

			mRecordBuf.video.video.buf.mp[cnt].frame = recordframes[cnt];
      mRecordBuf.video.video.buf.mp[cnt].frame_offset = 0;
      mRecordBuf.video.video.buf.mp[cnt].num_planes = num_planes;
      /* Plane 0 needs to be set seperately. Set other planes
       * in a loop. */
      mRecordBuf.video.video.buf.mp[cnt].planes[0].reserved[0] =
        mRecordBuf.video.video.buf.mp[cnt].frame_offset;
      mRecordBuf.video.video.buf.mp[cnt].planes[0].length = planes[0];
      mRecordBuf.video.video.buf.mp[cnt].planes[0].m.userptr =
        recordframes[cnt].fd;
      for (int j = 1; j < num_planes; j++) {
        mRecordBuf.video.video.buf.mp[cnt].planes[j].length = planes[j];
        mRecordBuf.video.video.buf.mp[cnt].planes[j].m.userptr =
          recordframes[cnt].fd;
        mRecordBuf.video.video.buf.mp[cnt].planes[j].reserved[0] =
          mRecordBuf.video.video.buf.mp[cnt].planes[j-1].reserved[0] +
          mRecordBuf.video.video.buf.mp[cnt].planes[j-1].length;
      }
    }

    //memset(&mRecordBuf, 0, sizeof(mRecordBuf));
    mRecordBuf.ch_type = MM_CAMERA_CH_VIDEO;
    mRecordBuf.video.video.num = mHalCamCtrl->mRecordingMemory.buffer_count;//kRecordBufferCount;
    //mRecordBuf.video.video.frame_offset = &record_offset[0];
    //mRecordBuf.video.video.frame = &recordframes[0];
    LOGE("%s : END",__func__);
    return NO_ERROR;
}

void QCameraStream_record::releaseRecordingFrame(const void *opaque)
{
    LOGE("%s : BEGIN, opaque = 0x%p",__func__, opaque);
    if(!mActive)
    {
        LOGE("%s : Recording already stopped!!! Leak???",__func__);
        return;
    }
    for(int cnt = 0; cnt < mHalCamCtrl->mRecordingMemory.buffer_count; cnt++) {
        if(mHalCamCtrl->mRecordingMemory.camera_memory[cnt] &&
                mHalCamCtrl->mRecordingMemory.camera_memory[cnt]->data == opaque) {
            /* found the match */
            if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, &mRecordedFrames[cnt]))
                LOGE("%s : Buf Done Failed",__func__);
            LOGE("%s : END",__func__);
            return;
		}
    }
	LOGE("%s: cannot find the matched frame with opaue = 0x%p", __func__, opaque);
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

#if 0
sp<IMemoryHeap> QCameraStream_record::getHeap() const
{
  return mRecordHeap != NULL ? mRecordHeap->mHeap : NULL;
}

status_t  QCameraStream_record::takeLiveSnapshot()
{
  //snapshotframes = new msm_frame[1];
  //memset(snapshotframes,0,sizeof(struct msm_frame));
  //mJpegMaxSize = dim.video_width * dim.video_height * 1.5;
  LOGE("%s: BEGIN", __func__);
  snapshot_enabled = true;
  LOGE("%s: END", __func__);
  return true;
}
#endif
status_t  QCameraStream_record::takeLiveSnapshot(){
	return true;
}

}//namespace android

