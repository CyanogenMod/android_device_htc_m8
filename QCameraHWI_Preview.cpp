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

// ---------------------------------------------------------------------------
// Preview Callback
// ---------------------------------------------------------------------------

static void preview_notify_cb(mm_camera_ch_data_buf_t *frame,
                                void *user_data)
{
  QCameraStream_preview *pme = (QCameraStream_preview *)user_data;
  mm_camera_ch_data_buf_t *bufs_used = 0;
  LOGV("%s: E", __func__);
  /* for peview data, there is no queue, so directly use*/
  if(pme==NULL) {
    LOGE("%s: X : Incorrect cookie",__func__);
    /*Call buf done*/
    return;
  }

  pme->processPreviewFrame(frame);
  LOGV("%s: X", __func__);
}

#if 0
int QCameraHardwareInterface::prepare_preview_buffers(cam_format_t fmt_type, camera_mode_t mode,
                                int num_preview_buf, int display_width,int display_height)
{
  /* now we hard code format */
  int i, rc = MM_CAMERA_OK;
    uint32_t frame_len, y_off, cbcr_off;
  mm_camera_reg_buf_t reg_buf;

  LOGE("%s: BEGIN\n", __func__);

    memset(previewframes,  0,  sizeof(struct msm_frame)*MM_CAMERA_MAX_NUM_FRAMES);

  frame_len = mm_camera_get_msm_frame_len(fmt_type, mode, display_width, display_height, &y_off, &cbcr_off, MM_CAMERA_PAD_WORD);

  for(i = 0; i < num_preview_buf; i++) {
    previewframes[i].buffer = (unsigned long) mm_camera_do_mmap(
      frame_len, &previewframes[i].fd );
    if (!previewframes[i].buffer) {
      LOGE("%s:no mem for video buf index %d\n", __func__, i);
        rc = -MM_CAMERA_E_NO_MEMORY;
        goto end;
    }
    previewframes[i].path = OUTPUT_TYPE_P;
        previewframes[i].y_off= y_off;
    previewframes[i].cbcr_off = cbcr_off;
  }
    memset(&reg_buf, 0, sizeof(reg_buf));
  reg_buf.ch_type = MM_CAMERA_CH_PREVIEW;
  reg_buf.preview.num = num_preview_buf;
  reg_buf.preview.frame = &previewframes[0];
  rc = HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
  if(rc != MM_CAMERA_OK) {
    LOGE("%s:reg preview buf err=%d\n", __func__, rc);
  }
end:
  LOGE("%s: END, rc=%d\n", __func__, rc);
  return rc;
}
#endif

status_t QCameraStream_preview::initDisplayBuffers()
{
    status_t ret = NO_ERROR;
    int width = 0;  /* width of channel  */
    int height = 0; /* height of channel */
    uint32_t frame_len = 0; /* frame planner length */
    int buffer_num = 4; /* number of buffers for display */
    const char *pmem_region;
    uint8_t num_planes = 0;
    uint32_t planes[VIDEO_MAX_PLANES];    

    cam_ctrl_dimension_t dim;

    LOGE("%s:BEGIN",__func__);
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
                          cbcr_off,
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
    memset(&mDisplayBuf, 0, sizeof(mDisplayBuf));
    /* allocate memory for mplanar frame struct. */
    mDisplayBuf.preview.buf.mp = new mm_camera_mp_buf_t[PREVIEW_BUFFER_COUNT *
                                    sizeof(mm_camera_mp_buf_t)];
    if (!mDisplayBuf.preview.buf.mp) {
      LOGE("%s Error allocating memory for mplanar struct ", __func__);
      mPreviewHeap.clear();
      mPreviewHeap = NULL;
      return NO_MEMORY;
    }
    memset(mDisplayBuf.preview.buf.mp, 0,
       PREVIEW_BUFFER_COUNT * sizeof(mm_camera_mp_buf_t));
    LOGE("PMEM Preview Buffer Allocation Successfull");
    previewframes = new msm_frame[PREVIEW_BUFFER_COUNT];
    if (previewframes != NULL) {
      memset(previewframes,0,sizeof(struct msm_frame) * PREVIEW_BUFFER_COUNT);
      for (int cnt = 0; cnt < PREVIEW_BUFFER_COUNT; cnt++) {
        previewframes[cnt].fd = mPreviewHeap->mHeap->getHeapID();
        previewframes[cnt].buffer =
          (uint32_t)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt;
        previewframes[cnt].y_off = 0;
        previewframes[cnt].cbcr_off = planes[0];
        previewframes[cnt].path = OUTPUT_TYPE_P;
        preview_offset[cnt] =  mPreviewHeap->mAlignedBufferSize * cnt;
        LOGE ("initDisplay :  Preview heap buffer=%lu fd=%d offset = %d \n",
          (unsigned long)previewframes[cnt].buffer, previewframes[cnt].fd, preview_offset[cnt]);
        mDisplayBuf.preview.buf.mp[cnt].frame = previewframes[cnt];
        mDisplayBuf.preview.buf.mp[cnt].frame_offset = preview_offset[cnt];
        mDisplayBuf.preview.buf.mp[cnt].num_planes = num_planes;
        /* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        mDisplayBuf.preview.buf.mp[cnt].planes[0].length = planes[0];
        mDisplayBuf.preview.buf.mp[cnt].planes[0].m.userptr = previewframes[cnt].fd;
        mDisplayBuf.preview.buf.mp[cnt].planes[0].reserved[0] =
          mDisplayBuf.preview.buf.mp[cnt].frame_offset;
        for (int j = 1; j < num_planes; j++) {
          mDisplayBuf.preview.buf.mp[cnt].planes[j].length = planes[j];
          mDisplayBuf.preview.buf.mp[cnt].planes[j].m.userptr =
            previewframes[cnt].fd;
          mDisplayBuf.preview.buf.mp[cnt].planes[j].reserved[0] =
            mDisplayBuf.preview.buf.mp[cnt].planes[j-1].reserved[0] +
            mDisplayBuf.preview.buf.mp[cnt].planes[j-1].length;
        }
      }
     /* register the streaming buffers for the channel*/
      mDisplayBuf.ch_type = MM_CAMERA_CH_PREVIEW;
      mDisplayBuf.preview.num = PREVIEW_BUFFER_COUNT;
      LOGE("Preview buf type =%d, offset[1] =%d, buffer[1] =%lx",
        mDisplayBuf.ch_type, preview_offset[1], previewframes[1].buffer);
    } else
      ret = NO_MEMORY;

    return ret;
}

status_t QCameraStream_preview::processPreviewFrame(mm_camera_ch_data_buf_t *frame)
{
  LOGV("%s",__func__);


  if(mHalCamCtrl==NULL) {
    LOGE("%s: X: HAL control object not set",__func__);
    /*Call buf done*/
    return BAD_VALUE;
  }

  if (UNLIKELY(mHalCamCtrl->mDebugFps)) {
      mHalCamCtrl->debugShowPreviewFPS();
  }
  mHalCamCtrl->dumpFrameToFile(frame->def.frame, HAL_DUMP_FRM_PREVIEW);

  // Find the offset within the heap of the current buffer.
  ssize_t offset_addr =
      (ssize_t)frame->def.frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();

  mHalCamCtrl->mOverlayLock.lock();
  if(mHalCamCtrl->mOverlay != NULL) {
    mHalCamCtrl->mOverlay->setFd(mPreviewHeap->mHeap->getHeapID());
    mHalCamCtrl->mOverlay->queueBuffer((void *)offset_addr);
  }
  mHalCamCtrl->mOverlayLock.unlock();

  mHalCamCtrl->mCallbackLock.lock();
  int msgEnabled = mHalCamCtrl->mMsgEnabled;
  data_callback pcb = mHalCamCtrl->mDataCb;
  void *pdata = mHalCamCtrl->mCallbackCookie;
  mHalCamCtrl->mCallbackLock.unlock();

  if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME)){
    LOGE("Buffer Callback to Service");
    pcb(CAMERA_MSG_PREVIEW_FRAME,mPreviewHeap->mBuffers[frame->def.idx],pdata);  
  }

  /* Save the last displayed frame. We'll be using it to fill the gap between
     when preview stops and postview start during snapshot.*/
  mLastQueuedFrame = frame->def.frame;

  if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, frame))
  {
      LOGE("BUF DONE FAILED");
      return BAD_VALUE;
  }

  return NO_ERROR;
}

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

QCameraStream_preview::
QCameraStream_preview(int cameraId, camera_mode_t mode)
  : QCameraStream(),
    mCameraId(cameraId),
    myMode (mode),
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
    LOGV("%s: X", __func__);

}
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

status_t QCameraStream_preview::init() {

  status_t ret = NO_ERROR;
  mm_camera_reg_buf_t *reg_buf=&mDisplayBuf;

  LOGV("%s: E", __func__);

  if(NO_ERROR!=initDisplayBuffers()){
    return BAD_VALUE;
  }

  ret = QCameraStream::initChannel (mCameraId, MM_CAMERA_CH_PREVIEW_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s E: can't init native cammera preview ch\n",__func__);
    return ret;
  }

  ret = cam_config_prepare_buf(mCameraId, reg_buf);
  if(ret != MM_CAMERA_OK) {
    LOGV("%s:reg preview buf err=%d\n", __func__, ret);
    ret = BAD_VALUE;
  }
  else
    ret = NO_ERROR;
  mInit = true;
  return ret;
}
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

status_t QCameraStream_preview::start() {

    status_t ret = NO_ERROR;

    LOGV("%s: E", __func__);
    /* call start() in parent class to start the monitor thread*/
    QCameraStream::start ();

    /* register a notify into the mmmm_camera_t object*/
    (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_PREVIEW,
                                                preview_notify_cb,
                                                this);



    /* For preview, the OP_MODE we set is dependent upon whether we are
       starting camera or camcorder. For snapshot, anyway we disable preview.
       However, for ZSL we need to set OP_MODE to OP_MODE_ZSL and not
       OP_MODE_VIDEO. We'll set that for now in CamCtrl. So in case of
       ZSL we skip setting Mode here */

    if (myMode != CAMERA_ZSL_MODE) {
        LOGE("Setting OP MODE to MM_CAMERA_OP_MODE_VIDEO");
        mm_camera_op_mode_type_t op_mode=MM_CAMERA_OP_MODE_VIDEO;
        ret = cam_config_set_parm (mCameraId, MM_CAMERA_PARM_OP_MODE,
                                        &op_mode);
        LOGE("OP Mode Set");

        if(MM_CAMERA_OK != ret) {
          LOGE("%s: X :set mode MM_CAMERA_OP_MODE_VIDEO err=%d\n", __func__, ret);
          return BAD_VALUE;
        }
    }

    /* call mm_camera action start(...)  */
    LOGE("Starting Preview/Video Stream. ");
    ret = cam_ops_action(mCameraId, TRUE, MM_CAMERA_OPS_PREVIEW, 0);

    if (MM_CAMERA_OK != ret) {
      LOGE ("%s: preview streaming start err=%d\n", __func__, ret);
      return BAD_VALUE;
    }

    LOGE("%s : Preview streaming Started",__func__);
    ret = NO_ERROR;

    mActive =  true;
    LOGE("%s: X", __func__);
    return NO_ERROR;
  }


// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------
  void QCameraStream_preview::stop() {
    LOGE("%s: E", __func__);
    int ret=MM_CAMERA_OK;

    if(!mActive) {
      return;
    }
    /* unregister the notify fn from the mmmm_camera_t object*/

    /* call stop() in parent class to stop the monitor thread*/
    ret = cam_ops_action(mCameraId, FALSE, MM_CAMERA_OPS_PREVIEW, 0);
    if(MM_CAMERA_OK != ret) {
      LOGE ("%s: camera preview stop err=%d\n", __func__, ret);
    }


    mActive =  false;
    LOGE("%s: X", __func__);

  }
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------
  void QCameraStream_preview::release() {

    LOGE("%s : BEGIN",__func__);
    int ret=MM_CAMERA_OK,i;

    if(!mInit)
    {
      LOGE("%s : Stream not Initalized",__func__);
      return;
    }

    if(mActive) {
      this->stop();
    }

    ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_PREVIEW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Unreg preview buf err=%d\n", __func__, ret);
      //ret = BAD_VALUE;
    }

    ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_PREVIEW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Deinit preview channel failed=%d\n", __func__, ret);
      //ret = BAD_VALUE;
    }

    /*Deallocate Display buffers*/
   delete[] previewframes;

   mPreviewHeap.clear();
   mPreviewHeap = NULL;

    mInit = false;
    /* yyan TODO: release all buffers ?? */
    LOGE("%s: END", __func__);

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
