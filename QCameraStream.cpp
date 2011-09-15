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
#define LOG_TAG __FILE__
#include <utils/Log.h>
#include <utils/threads.h>


#include "QCameraStream.h"

/* QCameraStream class implementation goes here*/
/* following code implement the control logic of this class*/

namespace android {

#if 1
StreamQueue::StreamQueue(){
    mInitialized = false;
}

StreamQueue::~StreamQueue(){
    flush();
}

void StreamQueue::init(){
    Mutex::Autolock l(&mQueueLock);
    mInitialized = true;
    mQueueWait.signal();
}

void StreamQueue::deinit(){
    Mutex::Autolock l(&mQueueLock);
    mInitialized = false;
    mQueueWait.signal();
}

bool StreamQueue::isInitialized(){
   Mutex::Autolock l(&mQueueLock);
   return mInitialized;
}

bool StreamQueue::enqueue(
                 void * element){
    Mutex::Autolock l(&mQueueLock);
    if(mInitialized == false)
        return false;

    mContainer.add(element);
    mQueueWait.signal();
    return true;
}

bool StreamQueue::isEmpty(){
    return (mInitialized && mContainer.isEmpty());
}
void* StreamQueue::dequeue(){

    void *frame;
    mQueueLock.lock();
    while(mInitialized && mContainer.isEmpty()){
        mQueueWait.wait(mQueueLock);
    }

    if(!mInitialized){
        mQueueLock.unlock();
        return NULL;
    }

    frame = mContainer.itemAt(0);
    mContainer.removeAt(0);
    mQueueLock.unlock();
    return frame;
}

void StreamQueue::flush(){
    Mutex::Autolock l(&mQueueLock);
    mContainer.clear();
}
#endif


// ---------------------------------------------------------------------------
// QCameraStream
// ---------------------------------------------------------------------------

/* initialize a streaming channel*/
status_t QCameraStream::initChannel(mm_camera_t *native_camera,
                                    uint32_t ch_type_mask)
{

    int rc = MM_CAMERA_OK;
    int i;
    status_t ret = NO_ERROR;

#if 0

    LOGV("%s: E, channel = %d\n", __func__, ch_type);

    if (!native_camera || MM_CAMERA_CH_MAX<=ch_type) {
        LOGV("%s: BAD_VALUE", __func__);
        return BAD_VALUE;
    }
    /*yyan: first open the channel*/
    if(ch_type < MM_CAMERA_CH_MAX) {
        rc = native_camera->ops->ch_acquire(native_camera, ch_type);
        LOGV("%s:cam ch_open rc=%d\n",__func__, rc);
    }
    else {
        /* here we open all available channels */
        for(i = 0; i < MM_CAMERA_CH_MAX; i++) {
            if( MM_CAMERA_OK !=
                (rc = native_camera->ops->ch_acquire(native_camera, (mm_camera_channel_type_t)i))) {
                LOGE("%s:cam ch_open err=%d\n",__func__, rc);
                break;
            }
        }
    }
    if(MM_CAMERA_OK != rc) {
      LOGE("%s:open channel error err=%d\n", __func__, rc);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }
#endif
    int width = 0;  /* width of channel      */
    int height = 0; /* height of channel */
    cam_ctrl_dimension_t dim;
    mm_camera_ch_image_fmt_parm_t fmt;

  /* yyan : first get all sizes, by querying mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    rc = native_camera->cfg->get_parm(native_camera,\
                                      MM_CAMERA_PARM_DIMENSION,&dim);
    if (MM_CAMERA_OK != rc) {
      LOGE("%s: error - can't get camera dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    /*yyan: second init the channeles requested*/

    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type_mask) {
        rc = native_camera->ops->ch_acquire(native_camera, MM_CAMERA_CH_PREVIEW);
        LOGV("%s:ch_acquire MM_CAMERA_CH_PREVIEW, rc=%d\n",__func__, rc);

        if(MM_CAMERA_OK != rc) {
                LOGE("%s: preview channel acquir error =%d\n", __func__, rc);
                LOGE("%s: X", __func__);
                return BAD_VALUE;
        }
        else{
            memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
            fmt.ch_type = MM_CAMERA_CH_PREVIEW;
            fmt.def.fmt = CAMERA_YUV_420_NV12; //dim.prev_format;
            fmt.def.dim.width = dim.display_width;
            fmt.def.dim.height =  dim.display_height;
            LOGV("%s: preview channel fmt = %d", __func__,
                     dim.prev_format);
            LOGV("%s: preview channel resolution = %d X %d", __func__,
                     dim.display_width, dim.display_height);

            rc = native_camera->cfg->set_parm(native_camera,
                                                                                MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt);
            LOGV("%s: preview MM_CAMERA_PARM_CH_IMAGE_FMT rc = %d\n", __func__, rc);
            if(MM_CAMERA_OK != rc) {
                    LOGE("%s:set preview channel format err=%d\n", __func__, ret);
                    LOGE("%s: X", __func__);
                    ret = BAD_VALUE;
            }
        }
    }


    if(MM_CAMERA_CH_VIDEO_MASK & ch_type_mask)
    {
        rc = native_camera->ops->ch_acquire(native_camera, MM_CAMERA_CH_VIDEO);
        LOGV("%s:ch_acquire MM_CAMERA_CH_VIDEO, rc=%d\n",__func__, rc);

        if(MM_CAMERA_OK != rc) {
                LOGE("%s: video channel acquir error =%d\n", __func__, rc);
                LOGE("%s: X", __func__);
                ret = BAD_VALUE;
        }
        else {
            memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
            fmt.ch_type = MM_CAMERA_CH_VIDEO;
            fmt.video.video.fmt = CAMERA_YUV_420_NV12; //dim.enc_format;
            fmt.video.video.dim.width = dim.video_width;
            fmt.video.video.dim.height = dim.video_height;
            LOGV("%s: video channel fmt = %d", __func__,
                     dim.enc_format);
            LOGV("%s: video channel resolution = %d X %d", __func__,
                 dim.video_width, dim.video_height);

            rc = native_camera->cfg->set_parm(native_camera,
                                                                                MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt);

            LOGV("%s: video MM_CAMERA_PARM_CH_IMAGE_FMT rc = %d\n", __func__, rc);
            if(MM_CAMERA_OK != rc) {
                LOGE("%s:set video channel format err=%d\n", __func__, rc);
                LOGE("%s: X", __func__);
                ret= BAD_VALUE;
            }
        }

  } /*MM_CAMERA_CH_VIDEO*/


    ret = (MM_CAMERA_OK==rc)? NO_ERROR : BAD_VALUE;
    LOGV("%s: X, ret = %d", __func__, ret);
    return ret;
}

status_t QCameraStream::deinitChannel(mm_camera_t *native_camera,
                                    mm_camera_channel_type_t ch_type)
{

    int rc = MM_CAMERA_OK;

    LOGV("%s: E, channel = %d\n", __func__, ch_type);

    if (!native_camera || MM_CAMERA_CH_MAX<=ch_type) {
        LOGE("%s: X: BAD_VALUE", __func__);
        return BAD_VALUE;
    }

    native_camera->ops->ch_release(native_camera,ch_type);

    LOGV("%s: X, channel = %d\n", __func__, ch_type);
    return NO_ERROR;
}


QCameraStream::QCameraStream (){
    mInit = false;
    mActive = false;
    /* memset*/
    memset(&mCrop, 0, sizeof(mm_camera_ch_crop_t));
}

QCameraStream::~QCameraStream () {;}


status_t QCameraStream::init() {

    /* yyan TODO: init the free queue and busy queue*/

    return NO_ERROR;
}

status_t QCameraStream::start() {

    /* yyan TODO: with th data user object, launch the worker thread
       who monitors the busy queue and call data user's useData()*/


    return NO_ERROR;
}

void QCameraStream::stop() {

    /* yyan TODO: stop the queue worker thread who monitors the busy queue*/

}

void QCameraStream::release() {

    /* yyan TODO: kill the streaming thread who monitors the busy queue*/

    /* yyan TODO: release the queues ?? */

}

void QCameraStream::newData(void* data) {

    /* yyan : new data, add into busy queue*/
    if (data) {
        this->mBusyQueue.enqueue(data);
    }
}

void* QCameraStream::getUsedData() {

    /* yyan TODO:  after the frame data is used,
    this function should be called to return data into free queue */
    if (this->mFreeQueue.isEmpty())
        return NULL;
    else
        return this->mFreeQueue.dequeue();
}

void QCameraStream::usedData(void* data) {

    /* yyan:  after the frame data is used,
    this function should be called to return data into free queue */
  this->mFreeQueue.enqueue(data);
}


void QCameraStream::setHALCameraControl(QCameraHardwareInterface* ctrl) {

    /* yyan TODO:  provide a frame data user,
    for the  queue monitor thread to call the busy queue is not empty*/
    mHalCamCtrl = ctrl;
}


// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

#if 0

// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

QCameraStream_noneZSL::
QCameraStream_noneZSL(mm_camera_t *native_camera, camera_mode_t mode)
  :mmCamera(native_camera), mActive(false),
   myMode (mode),  open_flag(0)

  {
    mHalCamCtrl = NULL;
    LOGV("%s: E", __func__);

    LOGV("%s: X", __func__);
  }
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

QCameraStream_noneZSL::~QCameraStream_noneZSL() {
    LOGV("%s: E", __func__);
    LOGV("%s: X", __func__);

}
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

status_t QCameraStream_noneZSL::init(mm_camera_reg_buf_t *reg_buf)
{
    status_t ret = NO_ERROR;
    LOGV("%s: E", __func__);

    ch_type = reg_buf->ch_type;

    if (MM_CAMERA_CH_PREVIEW == ch_type) {
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
    }
    else if (MM_CAMERA_CH_VIDEO == ch_type)//For Video
    {
      ret = QCameraStream::initChannel (mmCamera, MM_CAMERA_CH_VIDEO_MASK);
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

    }else{
        //TODO : Need snap shot Code?
    }

  return ret;
}
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

status_t QCameraStream_noneZSL::start() {
    LOGV("%s: E", __func__);

    /* yyan TODO: call start() in parent class to start the monitor thread*/
    QCameraStream::start ();

    /* yyan TODO: register a notify into the mmmm_camera_t object*/
    if (mmCamera) {
      if(ch_type == MM_CAMERA_CH_PREVIEW) {
        (void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_PREVIEW,
                                                  preview_notify_cb,
                                                  this);
      }else if (ch_type == MM_CAMERA_CH_VIDEO) {
        //(void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_VIDEO,
        //                                          record_notify_cb,
        //                                          this);
      }else{
        //TODO : Need snap shot Code?
      }
    }
    mActive =  true;
    LOGV("%s: X", __func__);
    return NO_ERROR;
  }
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------
  void QCameraStream_noneZSL::stop() {
    LOGV("%s: E", __func__);

    /* yyan TODO: unregister the notify fn from the mmmm_camera_t object*/

    /* yyan TODO: call stop() in parent class to stop the monitor thread*/
     if (mmCamera) {
      if(ch_type == MM_CAMERA_CH_PREVIEW) {
        (void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_PREVIEW,
                                                  NULL,
                                                  NULL);
      }else if (ch_type == MM_CAMERA_CH_VIDEO) {
        (void) mmCamera->evt->register_buf_notify(mmCamera, MM_CAMERA_CH_VIDEO,
                                                  NULL,
                                                  NULL);
      }else{
        //TODO : Need snap shot Code?
      }
    }

    mActive =  false;
    LOGV("%s: X", __func__);

  }
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------
  void QCameraStream_noneZSL::release() {

    LOGV("%s: E", __func__);
    /* yyan TODO: disconnect from display piplines, e.g.
       free IOVerlay*/

    /* yyan TODO: release all buffers ?? */
    LOGV("%s: X", __func__);

  }

// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

 void QCameraStream_noneZSL::useData(void* data) {

    return;
}

// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------
/* yyan: do something to the used data*/
  void QCameraStream_noneZSL::usedData(void* data) {

    /* we now we only deal with the mm_camera_ch_data_buf_t type*/
    mm_camera_ch_data_buf_t *bufs_used =
    (mm_camera_ch_data_buf_t *)data;

    /* yyan: buf is used, release it! */
    if (mmCamera)
      (void) mmCamera->evt->buf_done(mmCamera, bufs_used);

  }
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

  void* QCameraStream_noneZSL::getUsedData() {

    /* yyan: return one piece of used data */
    return QCameraStream::getUsedData();
  }

// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

  void QCameraStream_noneZSL::newData(void* data) {
    /* yyan : new data, add into parent's busy queue, to by used by useData() */
    QCameraStream::newData(data);
  }

// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

QCameraStream*
QCameraStream_noneZSL::createInstance(mm_camera_t *native_camera,
                                      camera_mode_t mode)
{

  QCameraStream* pme = new QCameraStream_noneZSL(native_camera, mode);

  return pme;
}
// ---------------------------------------------------------------------------
// QCameraStream_noneZSL
// ---------------------------------------------------------------------------

void QCameraStream_noneZSL::deleteInstance(QCameraStream *p)
{
  if (p){
    p->release();
    delete p;
  }
}



// ---------------------------------------------------------------------------
// QCameraStream_ZSL
// ---------------------------------------------------------------------------

QCameraStream_ZSL::
QCameraStream_ZSL(mm_camera_t *native_camera, camera_mode_t mode)
    :mmCamera(native_camera)
{

}

QCameraStream_ZSL::~QCameraStream_ZSL() {

}

status_t QCameraStream_ZSL::init(mm_camera_reg_buf_t*) {

    /* yyan TODO: create streaming buffers*/

    /* yyan TODO: open mm_camera video channels*/


    /* yyan TODO: register buffers*/

    return NO_ERROR;
}


void QCameraStream_ZSL::release() {

    /* yyan TODO: disconnect from eoncoder piplines*/

    /* yyan TODO: release all buffers ?? */

}
/*maftab none/zsl methods*/
#endif

}; // namespace android
