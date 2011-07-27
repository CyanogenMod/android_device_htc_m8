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
/* QCameraHardwareInterface class implementation goes here*/
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
    LOGE("%s: X : Incorrect cookie");
    /*Call buf done*/
    return;
  }

  pme->processPreviewFrame(frame);


#if 0
  mm_camera_ch_data_buf_t* frame = (mm_camera_ch_data_buf_t*) data;

  mCallbackLock.lock();
  int msgEnabled = mMsgEnabled;
  data_callback pcb = mDataCallback;
  void *pdata = mCallbackCookie;
  data_callback_timestamp rcb = mDataCallbackTimestamp;
  void *rdata = mCallbackCookie;
  data_callback mcb = mDataCallback;
  void *mdata = mCallbackCookie;
  mCallbackLock.unlock();


#endif


#if 0
    /* data with no owner?*/
    if (!pme || !bufs_new) {
      /* do nothing*/
      return;
    } else
      bufs_used = (mm_camera_ch_data_buf_t *) pme->getUsedData();


                /* if there is used data bufs to free*/
            if (bufs_used) {
                /* add the new bufs into the stream and free used bufs */
                pme->newData(bufs_new);
                pme->usedData (bufs_used);
            } else {
                /* return the new data back as used already!*/
                pme->usedData (bufs_new);
            }
#endif
  LOGV("%s: X", __func__);
}



#if 0
/*maftab*/
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
    uint32_t y_off=0;
    uint32_t cbcr_off=0;

    cam_ctrl_dimension_t dim;

    LOGE("%s:BEGIN",__func__);
  /* yyan : get preview size, by qury mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    memset(&(this->mDisplayStreamBuf),0, sizeof(this->mDisplayStreamBuf));
    ret = mmCamera->cfg->get_parm(mmCamera, MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get camera dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }
  else {
    width =  dim.display_width,
    height = dim.display_height;
  }


  /* set 4 buffers for display */
  this->mDisplayStreamBuf.num = buffer_num;
  this->myMode=myMode; /*Need to assign this in constructor after translating from mask*/
  frame_len = mm_camera_get_msm_frame_len(CAMERA_YUV_420_NV21, this->myMode, width, height, &y_off, &cbcr_off, MM_CAMERA_PAD_WORD);

/*mnsr*/
#if 0

  /* yyan todo: consider format later*/
  if (CAMERA_MODE_3D == this->myMode)
    frame_len = (uint32_t)(PAD_TO_2K(width * height) * 3/2);
  else
    frame_len = (uint32_t)(width * height * 3/2);
#endif

  this->mDisplayStreamBuf.frame_len = frame_len;

  /*yyan: allocate memory for the buffers*/

  for(int i = 0; i < this->mDisplayStreamBuf.num; i++){

    struct msm_frame *frame = &(this->mDisplayStreamBuf.frame[i]);
    unsigned long buffer_addr = (unsigned long) mm_camera_do_mmap(frame_len,
                                                           &(frame->fd));
    if (!buffer_addr) {
       LOGE ("%s:no mem for stream buf index %d\n", __func__, i);
       ret = -MM_CAMERA_E_NO_MEMORY;
       goto end;
    }
    else{
      frame->buffer = buffer_addr;
      frame->path = OUTPUT_TYPE_P;
      frame->cbcr_off = cbcr_off; /* assume y_off is 0*/
      frame->y_off=y_off;
    }

  }/*end of for loop*/


 /* register the streaming buffers for the channel*/
  memset(&mDisplayBuf,  0,  sizeof(mDisplayBuf));
  mDisplayBuf.ch_type = MM_CAMERA_CH_PREVIEW;
  mDisplayBuf.preview.num = mDisplayStreamBuf.num;
  mDisplayBuf.preview.frame = &(mDisplayStreamBuf.frame[0]);
 /* Storing buffer info at this point actual call to register
    will be done in mstream->init()*/
  return NO_ERROR;

end:
  if (MM_CAMERA_OK == ret ) {
    LOGV("%s: X - NO_ERROR ", __func__);
    return NO_ERROR;
  }

    LOGV("%s: out of memory clean up", __func__);
  // yyan TODO: release the allocated memory

  LOGV("%s: X - BAD_VALUE ", __func__);
  return BAD_VALUE;

}

status_t QCameraStream_preview::processPreviewFrame(mm_camera_ch_data_buf_t *frame)
{
  LOGV("%s",__func__);


  if(mHalCamCtrl==NULL) {
    LOGE("%s: X: HAL control object not set");
    /*Call buf done*/
    return BAD_VALUE;
  }

  if (UNLIKELY(mHalCamCtrl->mDebugFps)) {
      mHalCamCtrl->debugShowPreviewFPS();
  }

  mHalCamCtrl->mOverlayLock.lock();
  if(mHalCamCtrl->mOverlay != NULL) {
    mHalCamCtrl->mOverlay->setFd(mDisplayStreamBuf.frame[frame->def.idx].fd);
    mHalCamCtrl->mOverlay->queueBuffer((void *)0/*offset_addr*/);
  }
  mHalCamCtrl->mOverlayLock.unlock();

  /* Save the last displayed frame. We'll be using it to fill the gap between
     when preview stops and postview start during snapshot.*/
  mLastQueuedFrame = &(mDisplayStreamBuf.frame[frame->def.idx]);

  if(MM_CAMERA_OK!=mmCamera->evt->buf_done(mmCamera,frame))
  {
      LOGE("###############BUF DONE FAILED");
      return BAD_VALUE;
  }

  return NO_ERROR;



#if 0
  mm_camera_ch_data_buf_t* frame = (mm_camera_ch_data_buf_t*) data;

  mCallbackLock.lock();
  int msgEnabled = mMsgEnabled;
  data_callback pcb = mDataCallback;
  void *pdata = mCallbackCookie;
  data_callback_timestamp rcb = mDataCallbackTimestamp;
  void *rdata = mCallbackCookie;
  data_callback mcb = mDataCallback;
  void *mdata = mCallbackCookie;
  mCallbackLock.unlock();

  #if 1
  mOverlayLock.lock();
  if(mOverlay != NULL) {
    mOverlay->setFd(mDisplayStreamBuf.frame[frame->def.idx].fd);
    mOverlay->queueBuffer((void *)0/*offset_addr*/);
  }
  mOverlayLock.unlock();
  if(MM_CAMERA_OK!=mmCamera->evt->buf_done(mmCamera,frame))
  {
      LOGE("###############BUF DONE FAILED");
  }
 #endif
#endif

}

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

QCameraStream_preview::
QCameraStream_preview(mm_camera_t *native_camera, camera_mode_t mode)
  : QCameraStream(),
    mmCamera(native_camera),
    myMode (mode),  open_flag(0)
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


  /* yyan TODO: open and config mm_camera preview channels*/
  ret = QCameraStream::initChannel (mmCamera, MM_CAMERA_CH_PREVIEW_MASK);
  if (NO_ERROR!=ret) {
    LOGE("%s E: can't init native cammera preview ch\n",__func__);
    return ret;
  }

  ret = mmCamera->cfg->prepare_buf(mmCamera, reg_buf);
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

    if(mmCamera==NULL) {
      LOGE("%s: X :Native camera not set",__func__);
      return BAD_VALUE;
    }

    /* yyan TODO: call start() in parent class to start the monitor thread*/
    QCameraStream::start ();

    /* yyan TODO: register a notify into the mmmm_camera_t object*/

    (void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_PREVIEW,
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
        ret = mmCamera->cfg->set_parm (mmCamera, MM_CAMERA_PARM_OP_MODE,
                                        &op_mode);
        LOGE("OP Mode Set");
    
        if(MM_CAMERA_OK != ret) {
          LOGE("%s: X :set mode MM_CAMERA_OP_MODE_VIDEO err=%d\n", __func__, ret);
          return BAD_VALUE;
        }
    }


    /* yyan: call mm_camera action start(...)  */
    LOGE("Starting Preview/Video Stream. ");
    ret = mmCamera->ops->action(mmCamera, TRUE, MM_CAMERA_OPS_PREVIEW, 0);

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
    /* yyan TODO: unregister the notify fn from the mmmm_camera_t object*/

    /* yyan TODO: call stop() in parent class to stop the monitor thread*/
    ret = mmCamera->ops->action(mmCamera, FALSE, MM_CAMERA_OPS_PREVIEW, 0);
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

    ret = mmCamera->cfg->unprepare_buf(mmCamera, MM_CAMERA_CH_PREVIEW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Unreg preview buf err=%d\n", __func__, ret);
      //ret = BAD_VALUE;
    }

    ret= QCameraStream::deinitChannel(mmCamera,MM_CAMERA_CH_PREVIEW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Deinit preview channel failed=%d\n", __func__, ret);
      //ret = BAD_VALUE;
    }

    /*Deallocate Display buffers*/

    for(i=0;i<mDisplayStreamBuf.num;i++) {
      mm_camera_do_munmap(mDisplayStreamBuf.frame[i].fd,(void *)mDisplayStreamBuf.frame[i].buffer,mDisplayStreamBuf.frame_len);
    }

    mInit = false;
    /* yyan TODO: release all buffers ?? */
    LOGE("%s: END", __func__);

  }

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

 void QCameraStream_preview::useData(void* data) {

    return;
}

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------
/* yyan: do something to the used data*/
  void QCameraStream_preview::usedData(void* data) {

    /* we now we only deal with the mm_camera_ch_data_buf_t type*/
    mm_camera_ch_data_buf_t *bufs_used =
    (mm_camera_ch_data_buf_t *)data;

    /* yyan: buf is used, release it! */
    if (mmCamera)
      (void) mmCamera->evt->buf_done(mmCamera, bufs_used);

  }
// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

  void* QCameraStream_preview::getUsedData() {

    /* yyan: return one piece of used data */
    return QCameraStream::getUsedData();
  }

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

  void QCameraStream_preview::newData(void* data) {
    /* yyan : new data, add into parent's busy queue, to by used by useData() */
    QCameraStream::newData(data);
  }

// ---------------------------------------------------------------------------
// QCameraStream_preview
// ---------------------------------------------------------------------------

QCameraStream*
QCameraStream_preview::createInstance(mm_camera_t *native_camera,
                                      camera_mode_t mode)
{

  QCameraStream* pme = new QCameraStream_preview(native_camera, mode);

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
