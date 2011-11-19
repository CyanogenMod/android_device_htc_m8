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
#define LOG_TAG "QCameraHWI_Preview"
#include <utils/Log.h>
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "QCameraHAL.h"
#include "QCameraHWI.h"

#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)

/* QCameraHWI_Preview class implementation goes here*/
/* following code implement the preview mode's image capture & display logic of this class*/

namespace android {

status_t QCameraStream_preview::initDisplayBuffers()
{
    status_t ret = NO_ERROR;
    int width = 0;  /* width of channel  */
    int height = 0; /* height of channel */
    uint32_t frame_len = 0; /* frame planner length */
    int buffer_num = 4; /* number of buffers for display */
    const char *pmem_region;

    cam_ctrl_dimension_t dim;

    LOGV("%s:BEGIN",__func__);
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get camera dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }else {
      width =  dim.display_width,
      height = dim.display_height;
    }

    this->myMode=myMode; /*Need to assign this in constructor after translating from mask*/
    num_planes = 0;
    frame_len = mm_camera_get_msm_frame_len(CAMERA_YUV_420_NV21, this->myMode,
                                            width, height, OUTPUT_TYPE_P,
                                            &num_planes, planes);

    pmem_region = "/dev/pmem_adsp";
#ifdef USE_ION
    mPreviewHeap = new IonPool(
                          MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                          frame_len,
                          PREVIEW_BUFFER_COUNT,
                          frame_len,
                          planes[0],
                          0,
                          "preview");
#else
    mPreviewHeap = new PmemPool(pmem_region,
                          MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                          MSM_PMEM_PREVIEW,
                          frame_len,
                          PREVIEW_BUFFER_COUNT,
                          frame_len,
                          planes[0],
                          0,
                          "preview");
#endif
    if (!mPreviewHeap->initialized()) {
        mPreviewHeap.clear();
        mPreviewHeap = NULL;
        LOGE("%s: ERROR : could not initialize Preview heap.",__func__);
        return BAD_VALUE;
    }

    LOGE("PMEM Preview Buffer Allocation Successfull");
    return NO_ERROR;
}

status_t QCameraStream_preview::processPreviewFrame(mm_camera_ch_data_buf_t *frame)
{
  status_t ret = NO_ERROR;
  int msgEnabled;
  data_callback pcb;
  ssize_t offset_addr;
  void *pdata;

  LOGV("%s : E",__func__);

  Mutex::Autolock lock(mStopCallbackLock);
  if(!mActive) {
    LOGE("Preview Stopped. Returning callback");
    ret = NO_ERROR;
    goto end;
  }
  if(mHalCamCtrl==NULL) {
    LOGE("%s: X: HAL control object not set",__func__);
    ret = BAD_VALUE;
    goto end;
  }

  if (UNLIKELY(mHalCamCtrl->mDebugFps)) {
      mHalCamCtrl->debugShowPreviewFPS();
  }
  mHalCamCtrl->dumpFrameToFile(frame->def.frame, HAL_DUMP_FRM_PREVIEW);

  // Find the offset within the heap of the current buffer.
  offset_addr =
      (ssize_t)frame->def.frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();

  mHalCamCtrl->mOverlayLock.lock();
  if(mHalCamCtrl->mOverlay != NULL) {
    mHalCamCtrl->mOverlay->setFd(mPreviewHeap->mHeap->getHeapID());
    mHalCamCtrl->mOverlay->queueBuffer((void *)offset_addr);
  }
  mHalCamCtrl->mOverlayLock.unlock();

  mHalCamCtrl->mCallbackLock.lock();
  msgEnabled = mHalCamCtrl->mMsgEnabled;
  pcb = mHalCamCtrl->mDataCb;
  pdata = mHalCamCtrl->mCallbackCookie;
  mHalCamCtrl->mCallbackLock.unlock();

  if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME)){
    LOGE("Buffer Callback to Service");
    mStopCallbackLock.unlock();
		if(mActive) {
			pcb(CAMERA_MSG_PREVIEW_FRAME,mPreviewHeap->mBuffers[frame->def.idx],pdata);
		}
  }
  /* Save the last displayed frame. We'll be using it to fill the gap between
     when preview stops and postview start during snapshot.*/
  mLastQueuedFrame = frame->def.frame;

end:
  if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, frame))
  {
      LOGE("BUF DONE FAILED");
  }
	LOGV("%s : X",__func__);
  return ret;
}

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

QCameraStream_preview::
QCameraStream_preview(int cameraId, camera_mode_t mode)
  : QCameraStream(cameraId,mode),
    mLastQueuedFrame(NULL)
  {
    mHalCamCtrl = NULL;
    LOGV("%s: E", __func__);

    LOGV("%s: X", __func__);
  }
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

QCameraStream_preview::~QCameraStream_preview() {
  LOGV("%s: E", __func__);
  if(mActive) {
    stop();
  }
  if(mInit) {
    release();
  }
  mInit = false;
  mActive = false;

  LOGV("%s: X", __func__);

}
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

status_t QCameraStream_preview::init() {

  status_t ret = NO_ERROR;
  LOGV("%s: E", __func__);

  ret = QCameraStream::initChannel (mCameraId, MM_CAMERA_CH_PREVIEW_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s E: can't init native cammera preview ch\n",__func__);
    return ret;
  }
  mInit = true;
  LOGV("%s: X", __func__);
  return ret;
}
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

status_t QCameraStream_preview::start() {

    status_t ret = NO_ERROR;

    LOGV("%s: E", __func__);

    if(NO_ERROR!=initDisplayBuffers()){
      return BAD_VALUE;
    }
    ret = start_stream(MM_CAMERA_CH_PREVIEW_MASK,mPreviewHeap);
    if(NO_ERROR != ret){
      LOGE("%s: X - start stream error", __func__);
      mPreviewHeap.clear();
	  mPreviewHeap = NULL;
      return BAD_VALUE;
    }
    LOGI("%s : Preview streaming Started",__func__);

    mActive =  true;
    LOGV("%s: X", __func__);
    return NO_ERROR;
  }


// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------
  void QCameraStream_preview::stop() {
    LOGV("%s: E", __func__);
    int ret=MM_CAMERA_OK;

    if(!mActive) {
      return;
    }
    mActive =  false;
    Mutex::Autolock lock(mStopCallbackLock);
    stop_stream(MM_CAMERA_CH_PREVIEW_MASK);
    mPreviewHeap.clear();
    mPreviewHeap = NULL;

    LOGV("%s: X", __func__);

  }
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------
  void QCameraStream_preview::release() {

    LOGV("%s : BEGIN",__func__);
    int ret=MM_CAMERA_OK,i;

    if(!mInit)
    {
      LOGE("%s : Stream not Initalized",__func__);
      return;
    }

    if(mActive) {
      this->stop();
    }

    ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_PREVIEW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Deinit preview channel failed=%d\n", __func__, ret);
    }

    mInit = false;
    LOGV("%s: END", __func__);

  }

QCameraStream*
QCameraStream_preview::createInstance(int cameraId,
                                      camera_mode_t mode)
{

  QCameraStream* pme = new QCameraStream_preview(cameraId, mode);

  return pme;
}
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

void QCameraStream_preview::deleteInstance(QCameraStream *p)
{
  if (p){
    LOGV("%s: BEGIN", __func__);
    p->release();
    delete p;
    p = NULL;
    LOGV("%s: END", __func__);
  }
}


/* Temp helper function */
void *QCameraStream_preview::getLastQueuedFrame(void)
{
    return mLastQueuedFrame;
}



// ---------------------------------------------------------------------------
// No code beyone this line
// ---------------------------------------------------------------------------
}; // namespace android
