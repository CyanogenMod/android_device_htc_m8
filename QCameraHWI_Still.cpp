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
#define LOG_NDDEBUG 0
#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHWI_Still"
#include <utils/Log.h>
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <media/mediarecorder.h>
#include "QCameraHAL.h"
#include "QCameraHWI.h"


/* following code implement the still image capture & encoding logic of this class*/
namespace android {

typedef enum {
    SNAPSHOT_STATE_ERROR,
    SNAPSHOT_STATE_UNINIT,
    SNAPSHOT_STATE_CH_ACQUIRED,
    SNAPSHOT_STATE_BUF_NOTIF_REGD,
    SNAPSHOT_STATE_BUF_INITIALIZED,
    SNAPSHOT_STATE_INITIALIZED,
    SNAPSHOT_STATE_IMAGE_CAPTURE_STRTD,
    SNAPSHOT_STATE_YUV_RECVD,
    SNAPSHOT_STATE_JPEG_ENCODING,
    SNAPSHOT_STATE_JPEG_ENCODE_DONE,
    SNAPSHOT_STATE_JPEG_COMPLETE_ENCODE_DONE,

    /*Add any new state above*/
    SNAPSHOT_STATE_MAX
} snapshot_state_type_t;


//-----------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------
static const int PICTURE_FORMAT_JPEG = 1;
static const int PICTURE_FORMAT_RAW = 2;
static const int POSTVIEW_SMALL_HEIGHT = 144;

// ---------------------------------------------------------------------------
/* static functions*/
// ---------------------------------------------------------------------------

/* TBD: Temp: to be removed*/
static pthread_mutex_t g_s_mutex;
static int g_status = 0;
static pthread_cond_t g_s_cond_v;

static void mm_app_snapshot_done()
{
  pthread_mutex_lock(&g_s_mutex);
  g_status = TRUE;
  pthread_cond_signal(&g_s_cond_v);
  pthread_mutex_unlock(&g_s_mutex);
}

static void mm_app_snapshot_wait()
{
        pthread_mutex_lock(&g_s_mutex);
        if(FALSE == g_status) pthread_cond_wait(&g_s_cond_v, &g_s_mutex);
        pthread_mutex_unlock(&g_s_mutex);
    g_status = FALSE;
}

static int mm_app_dump_snapshot_frame(char *filename,
                                      const void *buffer,
                                      uint32_t len)
{
    char bufp[128];
    int file_fdp;
    int rc = 0;

    file_fdp = open(filename, O_RDWR | O_CREAT, 0777);

    if (file_fdp < 0) {
        rc = -1;
        goto end;
    }
    write(file_fdp,
        (const void *)buffer, len);
    close(file_fdp);
end:
    return rc;
}

/* Callback received when a frame is available after snapshot*/
static void snapshot_notify_cb(mm_camera_ch_data_buf_t *recvd_frame,
                               void *user_data)
{
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)user_data;

    LOGD("%s: E", __func__);

    if (pme != NULL) {
        pme->receiveRawPicture(recvd_frame);
    }
    else{
        LOGW("%s: Snapshot obj NULL in callback", __func__);
    }

    LOGD("%s: X", __func__);

}

/* Once we give frame for encoding, we get encoded jpeg image
   fragments by fragment. We'll need to store them in a buffer
   to form complete JPEG image */
static void snapshot_jpeg_fragment_cb(uint8_t *ptr,
                                      uint32_t size,
                                      void *user_data)
{
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)user_data;

    LOGE("%s: E",__func__);
    if (pme != NULL) {
        pme->receiveJpegFragment(ptr,size);
    }
    else
        LOGW("%s: Receive jpeg fragment cb obj Null", __func__);

    LOGD("%s: X",__func__);
}

/* This callback is received once the complete JPEG encoding is done */
static void snapshot_jpeg_cb(jpeg_event_t event, void *user_data)
{
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)user_data;
    LOGE("%s: E ",__func__);

    if (event != JPEG_EVENT_DONE) {
        if (event == JPEG_EVENT_THUMBNAIL_DROPPED) {
            LOGE("%s: Error in thumbnail encoding (event: %d)!!!",
                 __func__, event);
            LOGD("%s: X",__func__);
            return;
        }
        else {
            LOGE("%s: Error (event: %d) while jpeg encoding!!!",
                 __func__, event);
        }
    }

    if (pme != NULL) {
       pme->receiveCompleteJpegPicture(event);
       /* deinit only if we are done taking requested number of snapshots */
       if (pme->getSnapshotState() == SNAPSHOT_STATE_JPEG_COMPLETE_ENCODE_DONE) {
       /* If it's ZSL Mode, we don't deinit now. We'll stop the polling thread and
          deinit the channel/buffers only when we change the mode from zsl to
          non-zsl. */
           if (!(pme->isZSLMode())) {
               pme->stop();
           }
        }
    }
    else
        LOGW("%s: Receive jpeg cb Obj Null", __func__);


    LOGD("%s: X",__func__);

}

// ---------------------------------------------------------------------------
/* private functions*/
// ---------------------------------------------------------------------------

void QCameraStream_Snapshot::
receiveJpegFragment(uint8_t *ptr, uint32_t size)
{
    LOGE("%s: E", __func__);
#if 0
    if (mJpegHeap != NULL) {
        LOGE("%s: Copy jpeg...", __func__);
        memcpy((uint8_t *)mJpegHeap->mHeap->base()+ mJpegOffset, ptr, size);
        mJpegOffset += size;
    }
    else {
        LOGE("%s: mJpegHeap is NULL!", __func__);
    }
    #else
    if(mHalCamCtrl->mJpegMemory.camera_memory[0] != NULL && ptr != NULL && size > 0) {
        memcpy((uint8_t *)((uint32_t)mHalCamCtrl->mJpegMemory.camera_memory[0]->data + mJpegOffset), ptr, size);
        mJpegOffset += size;


        /*
                memcpy((uint8_t *)((uint32_t)mHalCamCtrl->mJpegMemory.camera_memory[0]->data + mJpegOffset), ptr, size);
                mJpegOffset += size;
        */
    } else {
        LOGE("%s: mJpegHeap is NULL!", __func__);
    }


    #endif

    LOGD("%s: X", __func__);
}


void QCameraStream_Snapshot::
receiveCompleteJpegPicture(jpeg_event_t event)
{
    int msg_type = CAMERA_MSG_COMPRESSED_IMAGE;
    LOGE("%s: E", __func__);
    Mutex::Autolock l(&snapshotLock);

    // Save jpeg for debugging
/*  static int loop = 0;
    char buf[25];
    memset(buf, 0, sizeof(buf));
    snprintf(buf,sizeof(buf), "/data/snapshot_%d.jpg",loop++);
    mm_app_dump_snapshot_frame(buf,(const void *)mJpegHeap->mHeap->base(),
                               mJpegOffset);
*/

    /* If for some reason jpeg heap is NULL we'll just return */
    #if 0
    if (mJpegHeap == NULL) {
        return;
    }
    #endif

    LOGE("%s: Calling upperlayer callback to store JPEG image", __func__);
  msg_type = isLiveSnapshot() ?
        (int)MEDIA_RECORDER_MSG_COMPRESSED_IMAGE : (int)CAMERA_MSG_COMPRESSED_IMAGE;

  LOGE("<DEBUG> Msg type:%d",msg_type);
    msg_type = CAMERA_MSG_COMPRESSED_IMAGE;
    LOGE("<DEBUG> Msg type:%d",msg_type);

    mHalCamCtrl->dumpFrameToFile(mHalCamCtrl->mJpegMemory.camera_memory[0]->data, mJpegOffset, (char *)"marvin", (char *)"jpg", 0);
    LOGE("<DEBUG> Done with dumping");
    if (mHalCamCtrl->mDataCb &&
        (mHalCamCtrl->mMsgEnabled & msg_type)) {

        LOGE("<DEBUG> Callback is enabled");
        // Create camera_memory_t object backed by the same physical
        // memory but with actual bitstream size.
        camera_memory_t *encodedMem = mHalCamCtrl->mGetMemory(
            mHalCamCtrl->mJpegMemory.fd[0], mJpegOffset, 1,
            mHalCamCtrl);
        if (!encodedMem || !encodedMem->data) {
            LOGE("%s: mGetMemory failed.\n", __func__);
            return;
        }

        LOGE("<DEBUG> Issue callback");
      mHalCamCtrl->mDataCb(msg_type,
                           encodedMem, 0, NULL,
                           mHalCamCtrl->mCallbackCookie);
      LOGE("<DEBUG>Release Memory");
        encodedMem->release(encodedMem);
    } else {
      LOGE("%s: JPEG callback was cancelled--not delivering image.", __func__);
    }


    //reset jpeg_offset
    mJpegOffset = 0;

    /* this will free up the resources used for previous encoding task */


    /* Tell lower layer that we are done with this buffer.
       If it's live snapshot, we don't need to call it. Recording
       object will take care of it */
    if (!isLiveSnapshot()) {
        LOGD("%s: Calling buf done for frame id %d buffer: %x", __func__,
             mCurrentFrameEncoded->snapshot.main.idx,
             (unsigned int)mCurrentFrameEncoded->snapshot.main.frame->buffer);
        cam_evt_buf_done(mCameraId, mCurrentFrameEncoded);
    }
    LOGD("%s: Before omxJpegJoin", __func__);
    omxJpegJoin();
    LOGD("%s: After omxJpegJoin", __func__);
    omxJpegClose();
    LOGD("%s: After omxJpegClose", __func__);
    /* free the resource we allocated to maintain the structure */
    //mm_camera_do_munmap(main_fd, (void *)main_buffer_addr, mSnapshotStreamBuf.frame_len);
    if (!isLiveSnapshot())
        free(mCurrentFrameEncoded);
    setSnapshotState(SNAPSHOT_STATE_JPEG_ENCODE_DONE);

    mNumOfRecievedJPEG++;

    /* Before leaving check the jpeg queue. If it's not empty give the available
       frame for encoding*/
    if (!mSnapshotQueue.isEmpty()) {
        LOGI("%s: JPEG Queue not empty. Dequeue and encode.", __func__);
        mm_camera_ch_data_buf_t* buf =
            (mm_camera_ch_data_buf_t *)mSnapshotQueue.dequeue();
        encodeDisplayAndSave(buf, 1);
    }
    else
    {
        /* getRemainingSnapshots call will give us number of snapshots still
           remaining after flushing current zsl buffer once*/
        if (mNumOfRecievedJPEG == mNumOfSnapshot) {
            LOGD("%s: Complete JPEG Encoding Done!", __func__);
            setSnapshotState(SNAPSHOT_STATE_JPEG_COMPLETE_ENCODE_DONE);
            mBurstModeFlag = false;
            /* in case of zsl, we need to reset some of the zsl attributes */
            if (isZSLMode()){
                LOGD("%s: Resetting the ZSL attributes", __func__);
                setZSLChannelAttribute();
            }
        }
    }

    LOGD("%s: X", __func__);
}


status_t QCameraStream_Snapshot::
configSnapshotDimension(cam_ctrl_dimension_t* dim)
{
    bool matching = true;
    cam_format_t img_format;
    status_t ret = NO_ERROR;
    LOGD("%s: E", __func__);

    LOGD("%s:Passed picture size: %d X %d", __func__,
         dim->picture_width, dim->picture_height);
    LOGD("%s:Passed postview size: %d X %d", __func__,
         dim->ui_thumbnail_width, dim->ui_thumbnail_height);

    /* First check if the picture resolution is the same, if not, change it*/
    mHalCamCtrl->getPictureSize(&mPictureWidth, &mPictureHeight);
    LOGD("%s: Picture size received: %d x %d", __func__,
         mPictureWidth, mPictureHeight);

    mHalCamCtrl->getPreviewSize(&mPostviewWidth, &mPostviewHeight);
    LOGD("%s: Postview size received: %d x %d", __func__,
         mPostviewWidth, mPostviewHeight);

    matching = (mPictureWidth == dim->picture_width) &&
        (mPictureHeight == dim->picture_height);
    matching &= (dim->ui_thumbnail_width == mPostviewWidth) &&
        (dim->ui_thumbnail_height == mPostviewHeight);

    /* picture size currently set do not match with the one wanted
       by user.*/
    if (!matching) {
        if (mPictureHeight < mPostviewHeight) {
            //Changes to Handle VFE limitation.
            mActualPictureWidth = mPictureWidth;
            mActualPictureHeight = mPictureHeight;
            mPictureWidth = mPostviewWidth;
            mPictureHeight = mPostviewHeight;
            mJpegDownscaling = TRUE;
        }else{
            mJpegDownscaling = FALSE;
        }
        dim->picture_width  = mPictureWidth;
        dim->picture_height = mPictureHeight;
        dim->ui_thumbnail_height = mThumbnailHeight = mPostviewHeight;
        dim->ui_thumbnail_width = mThumbnailWidth = mPostviewWidth;
    }
    img_format = mHalCamCtrl->getPreviewFormat();
    if (img_format) {
        matching &= (img_format == dim->main_img_format);
        if (!matching) {
            dim->main_img_format = img_format;
            dim->thumb_format = img_format;
        }
    }
    if (!matching) {
         LOGD("%s: Image Sizes before set parm call: main: %dx%d thumbnail: %dx%d",
              __func__,
              dim->picture_width, dim->picture_height,
              dim->ui_thumbnail_width, dim->ui_thumbnail_height);

        ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,dim);
        if (NO_ERROR != ret) {
            LOGE("%s: error - can't config snapshot parms!", __func__);
            ret = FAILED_TRANSACTION;
            goto end;
        }
    }
    /* set_parm will return corrected dimension based on aspect ratio and
       ceiling size */
    mPictureWidth = dim->picture_width;
    mPictureHeight = dim->picture_height;
    mPostviewHeight = mThumbnailHeight = dim->ui_thumbnail_height;
    mPostviewWidth = mThumbnailWidth = dim->ui_thumbnail_width;

    LOGD("%s: Image Format: %d", __func__, dim->main_img_format);
    LOGD("%s: Image Sizes: main: %dx%d thumbnail: %dx%d", __func__,
         dim->picture_width, dim->picture_height,
         dim->ui_thumbnail_width, dim->ui_thumbnail_height);
end:
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
initRawSnapshotChannel(cam_ctrl_dimension_t *dim,
                       mm_camera_raw_streaming_type_t raw_stream_type)
{
    status_t ret = NO_ERROR;
    mm_camera_ch_image_fmt_parm_t fmt;
    mm_camera_channel_attr_t ch_attr;

    LOGD("%s: E", __func__);

    LOGD("%s: Acquire Raw Snapshot Channel", __func__);
    ret = cam_ops_ch_acquire(mCameraId, MM_CAMERA_CH_RAW);
    if (NO_ERROR != ret) {
        LOGE("%s: Failure Acquiring Raw Snapshot Channel error =%d\n",
             __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* Snapshot channel is acquired */
    setSnapshotState(SNAPSHOT_STATE_CH_ACQUIRED);

    /* Set channel attribute */
    LOGD("%s: Set Raw Snapshot Channel attribute", __func__);
    memset(&ch_attr, 0, sizeof(ch_attr));
    ch_attr.type = MM_CAMERA_CH_ATTR_RAW_STREAMING_TYPE;
    ch_attr.raw_streaming_mode = raw_stream_type;

    if( NO_ERROR !=
        cam_ops_ch_set_attr(mCameraId, MM_CAMERA_CH_RAW, &ch_attr)) {
        LOGD("%s: Failure setting Raw channel attribute.", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
    fmt.ch_type = MM_CAMERA_CH_RAW;
    fmt.def.fmt = CAMERA_BAYER_SBGGR10;
    fmt.def.dim.width = dim->raw_picture_width;
    fmt.def.dim.height = dim->raw_picture_height;


    LOGV("%s: Raw snapshot channel fmt: %d", __func__,
         fmt.def.fmt);
    LOGV("%s: Raw snapshot resolution: %dX%d", __func__,
         dim->raw_picture_width, dim->raw_picture_height);

    LOGD("%s: Set Raw Snapshot channel image format", __func__);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt);
    if (NO_ERROR != ret) {
        LOGE("%s: Set Raw Snapshot Channel format err=%d\n", __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    LOGD("%s: Register buffer notification. My object: %x",
         __func__, (unsigned int) this);
    (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_RAW,
                                        snapshot_notify_cb, this);
    /* Set the state to buffer notification completed */
    setSnapshotState(SNAPSHOT_STATE_BUF_NOTIF_REGD);

end:
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGE("%s: X", __func__);
    return ret;

}

status_t QCameraStream_Snapshot::
setZSLChannelAttribute(void)
{
    status_t ret = NO_ERROR;
    mm_camera_channel_attr_t ch_attr;
    mm_camera_channel_attr_buffering_frame_t attr_val;
    int mode = ZSL_LOOK_BACK_MODE_TIME;
    int value = 0;
    bool empty_queue_flag = FALSE;

    LOGD("%s: E", __func__);

    mHalCamCtrl->getZSLLookBack(&mode, &value);
    mHalCamCtrl->getZSLEmptyQueueFlag(&empty_queue_flag);
    LOGD("%s: ZSL dispatch_mode: %d value: %d empty_queue: %d",
         __func__, mode, value, empty_queue_flag);
    memset(&attr_val, 0, sizeof(attr_val));
    attr_val.water_mark = ZSL_INTERNAL_QUEUE_SIZE;
    attr_val.dispatch_type = mode;
    attr_val.look_back = value;
    attr_val.give_top_of_queue = 0;
    /* Sometime, for example HDR, we need to empty the ZSL queue first and then
       get new frames for processing */
    attr_val.empty_queue = empty_queue_flag;

    LOGD("%s: ZSL attribute value to set: water_mark: %d empty_queue: %d"
         "dispatch_type: %d look_back_value: %d", __func__, attr_val.water_mark,
         attr_val.empty_queue, attr_val.dispatch_type, attr_val.look_back);

    memset(&ch_attr, 0, sizeof(mm_camera_channel_attr_t));

    if ((attr_val.dispatch_type == ZSL_LOOK_BACK_MODE_COUNT) &&
        (attr_val.look_back > attr_val.water_mark)) {
        LOGE("%s: Cannot have look_back frame count greather than water mark!",
             __func__);
        ret = BAD_VALUE;
        goto end;
    }

    LOGD("%s: Set ZSL Snapshot Channel attribute", __func__);
    ch_attr.type = MM_CAMERA_CH_ATTR_BUFFERING_FRAME;
    ch_attr.buffering_frame = attr_val;
    if( NO_ERROR !=
        cam_ops_ch_set_attr(mCameraId, MM_CAMERA_CH_SNAPSHOT, &ch_attr)) {
        LOGD("%s: Failure setting ZSL channel attribute.", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

end:
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
initSnapshotChannel(cam_ctrl_dimension_t *dim)
{
    status_t ret = NO_ERROR;
    mm_camera_ch_image_fmt_parm_t fmt;

    LOGD("%s: E", __func__);

    LOGD("%s: Acquire Snapshot Channel", __func__);
    ret = cam_ops_ch_acquire(mCameraId, MM_CAMERA_CH_SNAPSHOT);
    if (NO_ERROR != ret) {
        LOGE("%s: Failure Acquiring Snapshot Channel error =%d\n", __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* Snapshot channel is acquired */
    setSnapshotState(SNAPSHOT_STATE_CH_ACQUIRED);

    /* For ZSL mode we'll need to set channel attribute */
    if (isZSLMode()) {
        ret = setZSLChannelAttribute();
        if (ret != NO_ERROR) {
            goto end;
        }
    }

    memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
    fmt.ch_type = MM_CAMERA_CH_SNAPSHOT;
    fmt.snapshot.main.fmt = dim->main_img_format;
    fmt.snapshot.main.dim.width = dim->picture_width;
    fmt.snapshot.main.dim.height = dim->picture_height;

    fmt.snapshot.thumbnail.fmt = dim->thumb_format;
    fmt.snapshot.thumbnail.dim.width = dim->ui_thumbnail_width;
    fmt.snapshot.thumbnail.dim.height = dim->ui_thumbnail_height;

    LOGV("%s: Snapshot channel fmt = main: %d thumbnail: %d", __func__,
         dim->main_img_format, dim->thumb_format);
    LOGV("%s: Snapshot channel resolution = main: %dX%d  thumbnail: %dX%d",
         __func__, dim->picture_width, dim->picture_height,
         dim->ui_thumbnail_width, dim->ui_thumbnail_height);

    LOGD("%s: Set Snapshot channel image format", __func__);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt);
    if (NO_ERROR != ret) {
        LOGE("%s: Set Snapshot Channel format err=%d\n", __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    LOGD("%s: Register buffer notification. My object: %x",
         __func__, (unsigned int) this);
    (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_SNAPSHOT,
                                        snapshot_notify_cb, this);
    /* Set the state to buffer notification completed */
    setSnapshotState(SNAPSHOT_STATE_BUF_NOTIF_REGD);

end:
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGE("%s: X", __func__);
    return ret;

}

void QCameraStream_Snapshot::
deinitSnapshotChannel(mm_camera_channel_type_t ch_type)
{
    LOGD("%s: E", __func__);

    /* unreg buf notify*/
    if (getSnapshotState() >= SNAPSHOT_STATE_BUF_NOTIF_REGD){
        if (NO_ERROR != cam_evt_register_buf_notify(mCameraId,
                        ch_type, NULL, this)) {
            LOGE("%s: Failure to unregister buf notification", __func__);
        }
    }

    if (getSnapshotState() >= SNAPSHOT_STATE_CH_ACQUIRED) {
        LOGD("%s: Release snapshot channel", __func__);
        cam_ops_ch_release(mCameraId, ch_type);
    }

    LOGD("%s: X",__func__);
}

status_t QCameraStream_Snapshot::
initRawSnapshotBuffers(cam_ctrl_dimension_t *dim, int num_of_buf)
{
    status_t ret = NO_ERROR;
    struct msm_frame *frame;
    uint32_t frame_len;
    uint8_t num_planes;
    uint32_t planes[VIDEO_MAX_PLANES];
    mm_camera_reg_buf_t reg_buf;

    LOGD("%s: E", __func__);
    memset(&reg_buf,  0,  sizeof(mm_camera_reg_buf_t));
    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));

    if ((num_of_buf == 0) || (num_of_buf > MM_CAMERA_MAX_NUM_FRAMES)) {
        LOGE("%s: Invalid number of buffers (=%d) requested!", __func__, num_of_buf);
        ret = BAD_VALUE;
        goto end;
    }

    reg_buf.def.buf.mp = new mm_camera_mp_buf_t[num_of_buf];
    if (!reg_buf.def.buf.mp) {
      LOGE("%s Error allocating memory for mplanar struct ", __func__);
      ret = NO_MEMORY;
      goto end;
    }
    memset(reg_buf.def.buf.mp, 0, num_of_buf * sizeof(mm_camera_mp_buf_t));

    /* Get a frame len for buffer to be allocated*/
    frame_len = mm_camera_get_msm_frame_len(CAMERA_BAYER_SBGGR10,
                                            myMode,
                                            dim->raw_picture_width,
                                            dim->raw_picture_height,
                                            OUTPUT_TYPE_S,
                                            &num_planes, planes);

    if (mHalCamCtrl->initHeapMem(&mHalCamCtrl->mRawMemory, num_of_buf,
                                        frame_len, 0, planes[0], MSM_PMEM_RAW_MAINIMG,
                                        &mSnapshotStreamBuf, &reg_buf.def,
                                        num_planes, planes) < 0) {
        ret = NO_MEMORY;
        goto end;
    }

    /* register the streaming buffers for the channel*/
    reg_buf.ch_type = MM_CAMERA_CH_RAW;
    reg_buf.def.num = mSnapshotStreamBuf.num;

    ret = cam_config_prepare_buf(mCameraId, &reg_buf);
    if(ret != NO_ERROR) {
        LOGV("%s:reg snapshot buf err=%d\n", __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* If we have reached here successfully, we have allocated buffer.
       Set state machine.*/
    setSnapshotState(SNAPSHOT_STATE_BUF_INITIALIZED);

end:
    /* If it's error, we'll need to do some needful */
    if (ret != NO_ERROR) {
        handleError();
    }
    if (reg_buf.def.buf.mp)
      delete []reg_buf.def.buf.mp;
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::deinitRawSnapshotBuffers(void)
{
    int ret = NO_ERROR;

    LOGD("%s: E", __func__);

    /* deinit buffers only if we have already allocated */
    if (getSnapshotState() >= SNAPSHOT_STATE_BUF_INITIALIZED ){

        LOGD("%s: Unpreparing Snapshot Buffer", __func__);
        ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_RAW);
        if(ret != NO_ERROR) {
            LOGE("%s:Unreg Raw snapshot buf err=%d\n", __func__, ret);
            ret = FAILED_TRANSACTION;
            goto end;
        }
        mHalCamCtrl->releaseHeapMem(&mHalCamCtrl->mRawMemory);
    }

end:
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
initSnapshotBuffers(cam_ctrl_dimension_t *dim, int num_of_buf)
{
    status_t ret = NO_ERROR;
    struct msm_frame *frame;
    uint32_t frame_len, y_off, cbcr_off;
    uint8_t num_planes;
    uint32_t planes[VIDEO_MAX_PLANES];
    mm_camera_reg_buf_t reg_buf;
    int rotation = 0;

    LOGD("%s: E", __func__);
    memset(&reg_buf,  0,  sizeof(mm_camera_reg_buf_t));
    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));

    if ((num_of_buf == 0) || (num_of_buf > MM_CAMERA_MAX_NUM_FRAMES)) {
        LOGE("%s: Invalid number of buffers (=%d) requested!",
             __func__, num_of_buf);
        ret = BAD_VALUE;
        goto end;
    }

    LOGD("%s: Mode: %d Num_of_buf: %d ImageSizes: main: %dx%d thumb: %dx%d",
         __func__, myMode, num_of_buf,
         dim->picture_width, dim->picture_height,
         dim->ui_thumbnail_width, dim->ui_thumbnail_height);

    reg_buf.snapshot.main.buf.mp = new mm_camera_mp_buf_t[num_of_buf];
    if (!reg_buf.snapshot.main.buf.mp) {
          LOGE("%s Error allocating memory for mplanar struct ", __func__);
          ret = NO_MEMORY;
          goto end;
        }
    memset(reg_buf.snapshot.main.buf.mp, 0,
               num_of_buf * sizeof(mm_camera_mp_buf_t));
    reg_buf.snapshot.thumbnail.buf.mp = new mm_camera_mp_buf_t[num_of_buf];
    if (!reg_buf.snapshot.thumbnail.buf.mp) {
          LOGE("%s Error allocating memory for mplanar struct ", __func__);
          ret = NO_MEMORY;
          goto end;
        }
    memset(reg_buf.snapshot.thumbnail.buf.mp, 0,
               num_of_buf * sizeof(mm_camera_mp_buf_t));

    /* Number of buffers to be set*/
    /* Set the JPEG Rotation here since get_buffer_offset needs
     * the value of rotation.*/
    mHalCamCtrl->setJpegRotation();
    rotation = mHalCamCtrl->getJpegRotation();
    if(rotation != dim->rotation) {
        dim->rotation = rotation;
        ret = cam_config_set_parm(mHalCamCtrl->mCameraId, MM_CAMERA_PARM_DIMENSION, dim);
    }
    num_planes = 2;
    planes[0] = dim->picture_frame_offset.mp[0].len;
    planes[1] = dim->picture_frame_offset.mp[1].len;
    frame_len = dim->picture_frame_offset.frame_len;
    y_off = dim->picture_frame_offset.mp[0].offset;
    cbcr_off = dim->picture_frame_offset.mp[1].offset;
    LOGE("%s: main image: rotation = %d, yoff = %d, cbcroff = %d, size = %d, width = %d, height = %d",
         __func__, dim->rotation, y_off, cbcr_off, frame_len, dim->picture_width, dim->picture_height);
	if (mHalCamCtrl->initHeapMem (&mHalCamCtrl->mJpegMemory, 1, frame_len, 0, 0,
                                  MSM_PMEM_MAX, NULL, NULL, num_planes, planes) < 0) {
		LOGE("%s: Error allocating JPEG memory", __func__);
		ret = NO_MEMORY;
		goto end;
	}

	if (mHalCamCtrl->initHeapMem(&mHalCamCtrl->mSnapshotMemory, num_of_buf,
	   frame_len, y_off, cbcr_off, MSM_PMEM_MAINIMG, &mSnapshotStreamBuf,
                                 &reg_buf.snapshot.main, num_planes, planes) < 0) {
				ret = NO_MEMORY;
				goto end;
	};
    num_planes = 2;
    planes[0] = dim->thumb_frame_offset.mp[0].len;
    planes[1] = dim->thumb_frame_offset.mp[1].len;
    frame_len = planes[0] + planes[1];
    y_off = dim->thumb_frame_offset.mp[0].offset;
    cbcr_off = dim->thumb_frame_offset.mp[1].offset;
    LOGE("%s: thumbnail: rotation = %d, yoff = %d, cbcroff = %d, size = %d, width = %d, height = %d",
         __func__, dim->rotation, y_off, cbcr_off, frame_len,
         dim->thumbnail_width, dim->thumbnail_height);

    if (mHalCamCtrl->initHeapMem(&mHalCamCtrl->mThumbnailMemory, num_of_buf,
    frame_len, y_off, cbcr_off, MSM_PMEM_THUMBNAIL, &mPostviewStreamBuf,
        &reg_buf.snapshot.thumbnail, num_planes, planes) < 0) {
        ret = NO_MEMORY;
        goto end;
    };


    /* register the streaming buffers for the channel*/
    reg_buf.ch_type = MM_CAMERA_CH_SNAPSHOT;
    reg_buf.snapshot.main.num = mSnapshotStreamBuf.num;
    reg_buf.snapshot.thumbnail.num = mPostviewStreamBuf.num;

    ret = cam_config_prepare_buf(mCameraId, &reg_buf);
    if(ret != NO_ERROR) {
        LOGV("%s:reg snapshot buf err=%d\n", __func__, ret);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* If we have reached here successfully, we have allocated buffer.
       Set state machine.*/
    setSnapshotState(SNAPSHOT_STATE_BUF_INITIALIZED);
end:
    if (ret != NO_ERROR) {
        handleError();
    }
    if (reg_buf.snapshot.main.buf.mp)
      delete []reg_buf.snapshot.main.buf.mp;
    if (reg_buf.snapshot.thumbnail.buf.mp)
      delete []reg_buf.snapshot.thumbnail.buf.mp;
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
deinitSnapshotBuffers(void)
{
    int ret = NO_ERROR;

    LOGD("%s: E", __func__);

    /* Deinit only if we have already initialized*/
    if (getSnapshotState() >= SNAPSHOT_STATE_BUF_INITIALIZED ){

        LOGD("%s: Unpreparing Snapshot Buffer", __func__);
        ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_SNAPSHOT);
        if(ret != NO_ERROR) {
            LOGE("%s:unreg snapshot buf err=%d\n", __func__, ret);
            ret = FAILED_TRANSACTION;
            goto end;
        }

        /* Clear main and thumbnail heap*/
        mHalCamCtrl->releaseHeapMem(&mHalCamCtrl->mSnapshotMemory);
        mHalCamCtrl->releaseHeapMem(&mHalCamCtrl->mThumbnailMemory);
        mHalCamCtrl->releaseHeapMem(&mHalCamCtrl->mJpegMemory);
    }
end:
    LOGD("%s: X", __func__);
    return ret;
}

void QCameraStream_Snapshot::deinit(void)
{
    mm_camera_channel_type_t ch_type;

    LOGD("%s: E", __func__);

    if( getSnapshotState() == SNAPSHOT_STATE_UNINIT) {
        LOGD("%s: Already deinit'd!", __func__);
        return;
    }

    if (mSnapshotFormat == PICTURE_FORMAT_RAW) {
        /* deinit buffer */
        deinitRawSnapshotBuffers();
        /* deinit channel */
        deinitSnapshotChannel(MM_CAMERA_CH_RAW);
    }
    else
    {
        deinitSnapshotBuffers();
        deinitSnapshotChannel(MM_CAMERA_CH_SNAPSHOT);
    }


    /* deinit jpeg buffer if allocated */
    if(mJpegHeap != NULL) mJpegHeap.clear();
    mJpegHeap = NULL;

    /* memset some global structure */
    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));
    memset(&mPostviewStreamBuf, 0, sizeof(mPostviewStreamBuf));
    mSnapshotQueue.flush();

    mNumOfSnapshot = 0;
    mNumOfRecievedJPEG = 0;
    setSnapshotState(SNAPSHOT_STATE_UNINIT);

    LOGD("%s: X", __func__);
}

/*Temp: to be removed once event handling is enabled in mm-camera.
  We need an event - one event for
  stream-off to disable OPS_SNAPSHOT*/
void QCameraStream_Snapshot::runSnapshotThread(void *data)
{
    LOGD("%s: E", __func__);

    if (mSnapshotFormat == PICTURE_FORMAT_RAW) {
       /* TBD: Temp: Needs to be removed once event handling is enabled.
          We cannot call mm-camera interface to stop snapshot from callback
          function as it causes deadlock. Hence handling it here temporarily
          in this thread. Later mm-camera intf will give us event in separate
          thread context */
        mm_app_snapshot_wait();
        /* Send command to stop snapshot polling thread*/
        stop();
    }
    LOGD("%s: X", __func__);
}

/*Temp: to be removed once event handling is enabled in mm-camera*/
static void *snapshot_thread(void *obj)
{
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)obj;
    LOGD("%s: E", __func__);
    if (pme != 0) {
        pme->runSnapshotThread(obj);
    }
    else LOGW("not starting snapshot thread: the object went away!");
    LOGD("%s: X", __func__);
    return NULL;
}

/*Temp: to be removed later*/
static pthread_t mSnapshotThread;

status_t QCameraStream_Snapshot::initJPEGSnapshot(int num_of_snapshots)
{
    status_t ret = NO_ERROR;
    cam_ctrl_dimension_t dim;
    mm_camera_op_mode_type_t op_mode;

    LOGV("%s: E", __func__);

    LOGD("%s: Get current dimension", __func__);
    /* Query mm_camera to get current dimension */
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId,
                              MM_CAMERA_PARM_DIMENSION, &dim);
    if (NO_ERROR != ret) {
        LOGE("%s: error - can't get preview dimension!", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* Set camera op mode to MM_CAMERA_OP_MODE_CAPTURE */
    LOGD("Setting OP_MODE_CAPTURE");
    op_mode = MM_CAMERA_OP_MODE_CAPTURE;
    if( NO_ERROR != cam_config_set_parm(mCameraId,
            MM_CAMERA_PARM_OP_MODE, &op_mode)) {
        LOGE("%s: MM_CAMERA_OP_MODE_CAPTURE failed", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* config the parmeters and see if we need to re-init the stream*/
    LOGD("%s: Configure Snapshot Dimension", __func__);
    ret = configSnapshotDimension(&dim);
    if (ret != NO_ERROR) {
        LOGE("%s: Setting snapshot dimension failed", __func__);
        goto end;
    }

    /* Initialize stream - set format, acquire channel */
    ret = initSnapshotChannel(&dim);
    if (NO_ERROR != ret) {
        LOGE("%s: error - can't init nonZSL stream!", __func__);
        goto end;
    }

    ret = initSnapshotBuffers(&dim, num_of_snapshots);
    if ( NO_ERROR != ret ){
        LOGE("%s: Failure allocating memory for Snapshot buffers", __func__);
        goto end;
    }

end:
    /* Based on what state we are in, we'll need to handle error -
       like deallocating memory if we have already allocated */
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGV("%s: X", __func__);
    return ret;

}

status_t QCameraStream_Snapshot::initRawSnapshot(int num_of_snapshots)
{
    status_t ret = NO_ERROR;
    cam_ctrl_dimension_t dim;
    bool initSnapshot = false;
    mm_camera_op_mode_type_t op_mode;
    mm_camera_raw_streaming_type_t raw_stream_type =
        MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE;

    LOGV("%s: E", __func__);

    /* Set camera op mode to MM_CAMERA_OP_MODE_CAPTURE */
    LOGD("%s: Setting OP_MODE_CAPTURE", __func__);
    op_mode = MM_CAMERA_OP_MODE_CAPTURE;
    if( NO_ERROR != cam_config_set_parm(mCameraId,
            MM_CAMERA_PARM_OP_MODE, &op_mode)) {
        LOGE("%s: MM_CAMERA_OP_MODE_CAPTURE failed", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* For raw snapshot, we do not know the dimension as it
       depends on sensor to sensor. We call getDimension which will
       give us raw width and height */
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get dimension!", __func__);
      LOGE("%s: X", __func__);
      goto end;
    }
    LOGD("%s: Raw Snapshot dimension: %dx%d", __func__,
         dim.raw_picture_width,
         dim.raw_picture_height);

    /* Initialize stream - set format, acquire channel */
    /*TBD: Currently we only support single raw capture*/
    if (num_of_snapshots == 1) {
        raw_stream_type = MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE;
    }

    ret = initRawSnapshotChannel(&dim, raw_stream_type);
    if (NO_ERROR != ret) {
        LOGE("%s: error - can't init nonZSL stream!", __func__);
        goto end;
    }

    ret = initRawSnapshotBuffers(&dim, num_of_snapshots);
    if ( NO_ERROR != ret ){
        LOGE("%s: Failure allocating memory for Raw Snapshot buffers",
             __func__);
        goto end;
    }

end:
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGV("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::initZSLSnapshot(void)
{
    status_t ret = NO_ERROR;
    cam_ctrl_dimension_t dim;
    mm_camera_op_mode_type_t op_mode;

    LOGV("%s: E", __func__);

    LOGD("%s: Get current dimension", __func__);
    /* Query mm_camera to get current dimension */
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    ret = cam_config_get_parm(mCameraId,
                              MM_CAMERA_PARM_DIMENSION, &dim);
    if (NO_ERROR != ret) {
        LOGE("%s: error - can't get preview dimension!", __func__);
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* Set camera op mode to MM_CAMERA_OP_MODE_ZSL */
    LOGD("Setting OP_MODE_ZSL");
    op_mode = MM_CAMERA_OP_MODE_ZSL;
    if( NO_ERROR != cam_config_set_parm(mCameraId,
                                        MM_CAMERA_PARM_OP_MODE, &op_mode)) {
        LOGE("SET MODE: MM_CAMERA_OP_MODE_ZSL failed");
        ret = FAILED_TRANSACTION;
        goto end;
    }

    /* config the parmeters and see if we need to re-init the stream*/
    LOGD("%s: Configure Snapshot Dimension", __func__);
    ret = configSnapshotDimension(&dim);
    if (ret != NO_ERROR) {
        LOGE("%s: Setting snapshot dimension failed", __func__);
        goto end;
    }

    /* Initialize stream - set format, acquire channel */
    ret = initSnapshotChannel(&dim);
    if (NO_ERROR != ret) {
        LOGE("%s: error - can't init nonZSL stream!", __func__);
        goto end;
    }

    /* For ZSL we'll have to allocate buffers for internal queue
       maintained by mm-camera lib plus around 3 buffers used for
       data handling by lower layer.*/

    ret = initSnapshotBuffers(&dim, ZSL_INTERNAL_QUEUE_SIZE + 3);
    if ( NO_ERROR != ret ){
        LOGE("%s: Failure allocating memory for Snapshot buffers", __func__);
        goto end;
    }

end:
    /* Based on what state we are in, we'll need to handle error -
       like deallocating memory if we have already allocated */
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGV("%s: X", __func__);
    return ret;

}

status_t QCameraStream_Snapshot::
takePictureJPEG(void)
{
    status_t ret = NO_ERROR;

    LOGD("%s: E", __func__);

    /* Take snapshot */
    LOGD("%s: Call MM_CAMERA_OPS_SNAPSHOT", __func__);
    if (NO_ERROR != cam_ops_action(mCameraId,
                                              TRUE,
                                              MM_CAMERA_OPS_SNAPSHOT,
                                              this)) {
           LOGE("%s: Failure taking snapshot", __func__);
           ret = FAILED_TRANSACTION;
           goto end;
    }

    /* TBD: Temp: to be removed once event callback
       is implemented in mm-camera lib  */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&mSnapshotThread,&attr,
                   snapshot_thread, (void *)this);

end:
    if (ret != NO_ERROR) {
        handleError();
    }

    LOGD("%s: X", __func__);
    return ret;

}

status_t QCameraStream_Snapshot::
takePictureRaw(void)
{
    status_t ret = NO_ERROR;

    LOGD("%s: E", __func__);

    /* Take snapshot */
    LOGD("%s: Call MM_CAMERA_OPS_SNAPSHOT", __func__);
    if (NO_ERROR != cam_ops_action(mCameraId,
                                  TRUE,
                                  MM_CAMERA_OPS_RAW,
                                  this)) {
           LOGE("%s: Failure taking snapshot", __func__);
           ret = FAILED_TRANSACTION;
           goto end;
    }

    /* TBD: Temp: to be removed once event callback
       is implemented in mm-camera lib  */
    /* Wait for snapshot frame callback to return*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&mSnapshotThread,&attr,
                   snapshot_thread, (void *)this);

end:
    if (ret != NO_ERROR) {
        handleError();
    }
    LOGD("%s: X", __func__);
    return ret;

}

/* This is called from vide stream object */
status_t QCameraStream_Snapshot::
takePictureLiveshot(mm_camera_ch_data_buf_t* recvd_frame,
                    cam_ctrl_dimension_t *dim,
                    int frame_len)
{
    status_t ret = NO_ERROR;
    common_crop_t crop_info;
    uint32_t aspect_ratio;

    LOGE("%s: E", __func__);

    /* set flag to indicate we are doing livesnapshot */
    setModeLiveSnapshot(true);

    LOGE("%s:Passed picture size: %d X %d", __func__,
         dim->picture_width, dim->picture_height);
    LOGE("%s:Passed thumbnail size: %d X %d", __func__,
         dim->ui_thumbnail_width, dim->ui_thumbnail_height);

    mPictureWidth = dim->picture_width;
    mPictureHeight = dim->picture_height;
    mThumbnailWidth = dim->ui_thumbnail_width;
    mThumbnailHeight = dim->ui_thumbnail_height;

    memset(&crop_info, 0, sizeof(common_crop_t));
    crop_info.in1_w = mPictureWidth;
    crop_info.in1_h = mPictureHeight;
    crop_info.out1_w = mThumbnailWidth;
    crop_info.out1_h = mThumbnailHeight;
    ret = encodeData(recvd_frame, &crop_info, frame_len, 0);
    if (ret != NO_ERROR) {
        LOGE("%s: Failure configuring JPEG encoder", __func__);

        /* Failure encoding this frame. Just notify upper layer
           about it.*/
        if(mHalCamCtrl->mDataCb &&
            (mHalCamCtrl->mMsgEnabled & MEDIA_RECORDER_MSG_COMPRESSED_IMAGE)) {
            /* get picture failed. Give jpeg callback with NULL data
             * to the application to restore to preview mode
             */
#if 1 //mzhu, fix me in snapshot bring up
            mHalCamCtrl->mDataCb(MEDIA_RECORDER_MSG_COMPRESSED_IMAGE,
                                       NULL, 0, NULL,
                                       mHalCamCtrl->mCallbackCookie);
#endif //mzhu
        }
        setModeLiveSnapshot(false);
        goto end;
    }

end:
    LOGE("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
takePictureZSL(void)
{
    status_t ret = NO_ERROR;
    mm_camera_ops_parm_get_buffered_frame_t param;

    LOGD("%s: E", __func__);

    memset(&param, 0, sizeof(param));
    param.ch_type = MM_CAMERA_CH_SNAPSHOT;

    /* Take snapshot */
    LOGD("%s: Call MM_CAMERA_OPS_GET_BUFFERED_FRAME", __func__);
    if (NO_ERROR != cam_ops_action(mCameraId,
                                          TRUE,
                                          MM_CAMERA_OPS_GET_BUFFERED_FRAME,
                                          &param)) {
           LOGE("%s: Failure getting zsl frame(s)", __func__);
           ret = FAILED_TRANSACTION;
           goto end;
    }

    /* TBD: Temp: to be removed once event callback
       is implemented in mm-camera lib  */
/*    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&mSnapshotThread,&attr,
                   snapshot_thread, (void *)this);
*/
end:
    LOGD("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::
startStreamZSL(void)
{
    status_t ret = NO_ERROR;

    LOGD("%s: E", __func__);

    /* Start ZSL - it'll start queuing the frames */
    LOGD("%s: Call MM_CAMERA_OPS_ZSL", __func__);
    if (NO_ERROR != cam_ops_action(mCameraId,
                                          TRUE,
                                          MM_CAMERA_OPS_ZSL,
                                          this)) {
           LOGE("%s: Failure starting ZSL stream", __func__);
           ret = FAILED_TRANSACTION;
           goto end;
    }

end:
    LOGD("%s: X", __func__);
    return ret;

}

status_t  QCameraStream_Snapshot::
encodeData(mm_camera_ch_data_buf_t* recvd_frame,
           common_crop_t *crop_info,
           int frame_len,
           bool enqueued)
{
    status_t ret = NO_ERROR;
    cam_ctrl_dimension_t dimension;
    struct msm_frame *postviewframe;
    struct msm_frame *mainframe;
    common_crop_t crop;
    cam_point_t main_crop_offset;
    cam_point_t thumb_crop_offset;
    int width, height;
    uint8_t *thumbnail_buf;
    uint32_t thumbnail_fd;

    omx_jpeg_encode_params encode_params;

    /* If it's the only frame, we directly pass to encoder.
       If not, we'll queue it and check during next jpeg .
       Also, if the queue isn't empty then we need to queue this
       one too till its turn comes (only if it's not already
       queued up there)*/
    if((getSnapshotState() == SNAPSHOT_STATE_JPEG_ENCODING) ||
       (!mSnapshotQueue.isEmpty() && !enqueued)){
        /* encoding is going on. Just queue the frame for now.*/
        LOGD("%s: JPEG encoding in progress."
             "Enqueuing frame id(%d) for later processing.", __func__,
             recvd_frame->snapshot.main.idx);
        mSnapshotQueue.enqueue((void *)recvd_frame);
    }
    else {
        postviewframe = recvd_frame->snapshot.thumbnail.frame;
        mainframe = recvd_frame->snapshot.main.frame;

        dimension.orig_picture_dx = mPictureWidth;
        dimension.orig_picture_dy = mPictureHeight;
        dimension.thumbnail_width = mThumbnailWidth;
        dimension.thumbnail_height = mThumbnailHeight;

        #if 1

        #else
        mJpegHeap = new AshmemPool(frame_len,
                                   1,
                                   0, // we do not know how big the picture will be
                                   "jpeg");
        if (!mJpegHeap->initialized()) {
            mJpegHeap.clear();
            mJpegHeap = NULL;
            LOGE("%s: Error allocating JPEG memory", __func__);
            ret = NO_MEMORY;
            goto end;
        }
        #endif

        /*TBD: Move JPEG handling to the mm-camera library */
        LOGD("Setting callbacks, initializing encoder and start encoding.");
        LOGD(" Passing my obj: %x", (unsigned int) this);
        set_callbacks(snapshot_jpeg_fragment_cb, snapshot_jpeg_cb, this,
             mHalCamCtrl->mJpegMemory.camera_memory[0]->data, &mJpegOffset);
        omxJpegInit();
        mm_jpeg_encoder_setMainImageQuality(mHalCamCtrl->getJpegQuality());

        LOGD("%s: Dimension to encode: main: %dx%d thumbnail: %dx%d", __func__,
             dimension.orig_picture_dx, dimension.orig_picture_dy,
             dimension.thumbnail_width, dimension.thumbnail_height);

        /*TBD: Pass 0 as cropinfo for now as v4l2 doesn't provide
          cropinfo. It'll be changed later.*/
        memset(&crop,0,sizeof(common_crop_t));
        memset(&main_crop_offset,0,sizeof(cam_point_t));
        memset(&thumb_crop_offset,0,sizeof(cam_point_t));

        /* Setting crop info */

        /*Main image*/
        crop.in2_w=mCrop.snapshot.main_crop.width;// dimension.picture_width
        crop.in2_h=mCrop.snapshot.main_crop.height;// dimension.picture_height;
        if (!mJpegDownscaling) {
            crop.out2_w = mPictureWidth;
            crop.out2_h = mPictureHeight;
        } else {
            crop.out2_w = mActualPictureWidth;
            crop.out2_h = mActualPictureHeight;
            if (!crop.in2_w || !crop.in2_h) {
                crop.in2_w = mPictureWidth;
                crop.in2_h = mPictureHeight;
            }
        }
        main_crop_offset.x=mCrop.snapshot.main_crop.left;
        main_crop_offset.y=mCrop.snapshot.main_crop.top;
        /*Thumbnail image*/
        crop.in1_w=mCrop.snapshot.thumbnail_crop.width; //dimension.thumbnail_width;
        crop.in1_h=mCrop.snapshot.thumbnail_crop.height; // dimension.thumbnail_height;
        crop.out1_w=mThumbnailWidth;
        crop.out1_h=mThumbnailHeight;
        thumb_crop_offset.x=mCrop.snapshot.thumbnail_crop.left;
        thumb_crop_offset.y=mCrop.snapshot.thumbnail_crop.top;

        /*Fill in the encode parameters*/
        encode_params.dimension = (const cam_ctrl_dimension_t *)&dimension;
        encode_params.thumbnail_buf = (uint8_t *)postviewframe->buffer;
        encode_params.thumbnail_fd = postviewframe->fd;
        encode_params.thumbnail_offset = postviewframe->phy_offset;
        encode_params.snapshot_buf = (uint8_t *)mainframe->buffer;
        encode_params.snapshot_fd = mainframe->fd;
        encode_params.snapshot_offset = mainframe->phy_offset;
        encode_params.scaling_params = &crop;
        encode_params.exif_data = NULL;
        encode_params.exif_numEntries = 0;
        encode_params.a_cbcroffset = -1;
        encode_params.main_crop_offset = &main_crop_offset;
        encode_params.thumb_crop_offset = &thumb_crop_offset;

        if (!omxJpegEncode(&encode_params)){
            LOGE("%s: Failure! JPEG encoder returned error.", __func__);
            ret = FAILED_TRANSACTION;
            goto end;
        }

        /* Save the pointer to the frame sent for encoding. we'll need it to
           tell kernel that we are done with the frame.*/
        mCurrentFrameEncoded = recvd_frame;
        setSnapshotState(SNAPSHOT_STATE_JPEG_ENCODING);
    }

end:
    LOGD("%s: X", __func__);
    return ret;
}

/* Called twice - 1st to play shutter sound and 2nd to configure
   overlay/surfaceflinger for postview */
void QCameraStream_Snapshot::notifyShutter(common_crop_t *crop,
                                           bool mPlayShutterSoundOnly)
{
    //image_rect_type size;
    int32_t ext1 = 0, ext2 = 0;
    LOGD("%s: E", __func__);

    ext2 = mPlayShutterSoundOnly;
    LOGD("%s: Calling callback to play shutter sound", __func__);
    mHalCamCtrl->mNotifyCb(CAMERA_MSG_SHUTTER, ext1, ext2,
                                 mHalCamCtrl->mCallbackCookie);
    if(mPlayShutterSoundOnly) {
        /* At this point, invoke Notify Callback to play shutter sound only.
         * We want to call notify callback again when we have the
         * yuv picture ready. This is to reduce blanking at the time
         * of displaying postview frame. Using ext2 to indicate whether
         * to play shutter sound only or register the postview buffers.
         */
         ext2 = mPlayShutterSoundOnly;
        LOGD("%s: Calling callback to play shutter sound", __func__);
        mHalCamCtrl->mNotifyCb(CAMERA_MSG_SHUTTER, ext1, ext2,
                                     mHalCamCtrl->mCallbackCookie);
        return;
    }

    if (mHalCamCtrl->mNotifyCb &&
        (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_SHUTTER)) {
        mDisplayHeap = mPostviewHeap;
        if (crop != NULL && (crop->in1_w != 0 && crop->in1_h != 0)) {
            LOGD("%s: Size from cropinfo: %dX%d", __func__,
                 crop->in1_w, crop->in1_h);
        }
        else {
            LOGD("%s: Size from global: %dX%d", __func__,
                 mPostviewWidth, mPostviewHeight);
        }
        /*if(strTexturesOn == true) {
            size.width = mPictureWidth;
            size.height = mPictureHeight;
        }*/
        /* Now, invoke Notify Callback to unregister preview buffer
         * and register postview buffer with surface flinger. Set ext2
         * as 0 to indicate not to play shutter sound.
         */

        mHalCamCtrl->mNotifyCb(CAMERA_MSG_SHUTTER, ext1, ext2,
                                     mHalCamCtrl->mCallbackCookie);
    }
    LOGD("%s: X", __func__);
}

status_t  QCameraStream_Snapshot::
encodeDisplayAndSave(mm_camera_ch_data_buf_t* recvd_frame,
                     bool enqueued)
{
    status_t ret = NO_ERROR;
    struct msm_frame *postview_frame;
    int buf_index = 0;
    ssize_t offset_addr = 0;
    common_crop_t dummy_crop;
    /* send frame for encoding */
    LOGE("%s: Send frame for encoding", __func__);
    /*TBD: Pass 0 as cropinfo for now as v4l2 doesn't provide
      cropinfo. It'll be changed later.*/
    memset(&dummy_crop,0,sizeof(common_crop_t));
    ret = encodeData(recvd_frame, &dummy_crop, mSnapshotStreamBuf.frame_len,
                     enqueued);
    if (ret != NO_ERROR) {
        LOGE("%s: Failure configuring JPEG encoder", __func__);

        /* Failure encoding this frame. Just notify upper layer
           about it.*/
        if(mHalCamCtrl->mDataCb &&
            (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
            /* get picture failed. Give jpeg callback with NULL data
             * to the application to restore to preview mode
             */
            mHalCamCtrl->mDataCb(CAMERA_MSG_COMPRESSED_IMAGE,
                                       NULL, 0, NULL,
                                       mHalCamCtrl->mCallbackCookie);
        }
        goto end;
    }

    /* Display postview image*/
    /* If it's burst mode, we won't be displaying postview of all the captured
       images - only the first one */
    LOGD("%s: Burst mode flag  %d", __func__, mBurstModeFlag);
    #if 0
    if (!mBurstModeFlag) {
        postview_frame = recvd_frame->snapshot.thumbnail.frame;
        LOGD("%s: Displaying Postview Image", __func__);
        offset_addr = (ssize_t)postview_frame->buffer -
            (ssize_t)mPostviewHeap->mHeap->base();
        if(mHalCamCtrl->mUseOverlay) {
            mHalCamCtrl->mOverlayLock.lock();
            if(mHalCamCtrl->mOverlay != NULL) {
                mHalCamCtrl->mOverlay->setFd(postview_frame->fd);
                    if (mCrop.snapshot.thumbnail_crop.height != 0 &&
                        mCrop.snapshot.thumbnail_crop.width != 0) {
                        mHalCamCtrl->mOverlay->setCrop(
                            mCrop.snapshot.thumbnail_crop.left,
                            mCrop.snapshot.thumbnail_crop.top,
                            mCrop.snapshot.thumbnail_crop.width,
                            mCrop.snapshot.thumbnail_crop.height);
                    }else {
                        mHalCamCtrl->mOverlay->setCrop(0, 0,
                                                       mPostviewWidth,
                                                       mPostviewHeight);
                    }
                LOGD("%s: Queueing Postview for display", __func__);
                mHalCamCtrl->mOverlay->queueBuffer((void *)offset_addr);
            }
            mHalCamCtrl->mOverlayLock.unlock();
        }

        /* Set flag so that we won't display postview for all the captured
           images in case of burst mode */
        LOGD("%s: Setting burst mode flag to true - current: %d", __func__, mBurstModeFlag);
        mBurstModeFlag = true;
    }
    #endif

    // send upperlayer callback
     if (mHalCamCtrl->mDataCb &&
         (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_RAW_IMAGE)){
         buf_index = recvd_frame->snapshot.main.idx;
         mHalCamCtrl->mDataCb(CAMERA_MSG_RAW_IMAGE,
                                    mHalCamCtrl->mSnapshotMemory.camera_memory[0], 1, NULL,
                                    mHalCamCtrl->mCallbackCookie);
     }

end:
    LOGD("%s: X", __func__);
    return ret;
}

void QCameraStream_Snapshot::receiveRawPicture(mm_camera_ch_data_buf_t* recvd_frame)
{
    int buf_index = 0;
    common_crop_t crop;

    LOGD("%s: E ", __func__);


/*    char buf[25];
    static int loop = 0;
    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf), "/data/main_frame-%d.yuv",loop);
    mm_app_dump_snapshot_frame(buf,
                               (const void *)recvd_frame->snapshot.main.frame->buffer,
                               mSnapshotStreamBuf.frame_len);

    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf), "/data/thumb_frame-%d.yuv",loop++);
    mm_app_dump_snapshot_frame(buf,
                               (const void *)recvd_frame->snapshot.thumbnail.frame->buffer,
                               mPostviewStreamBuf.frame_len);
*/

    mHalCamCtrl->dumpFrameToFile(recvd_frame->snapshot.main.frame, HAL_DUMP_FRM_MAIN);
    mHalCamCtrl->dumpFrameToFile(recvd_frame->snapshot.thumbnail.frame, HAL_DUMP_FRM_THUMBNAIL);

    LOGE("%s: after dump", __func__);
    /* If it's raw snapshot, we just want to tell upperlayer to save the image*/
    if(mSnapshotFormat == PICTURE_FORMAT_RAW) {
        LOGD("%s: Call notifyShutter 2nd time", __func__);
        if(!mHalCamCtrl->mShutterSoundPlayed) {
            notifyShutter(&crop, TRUE);
        }
        notifyShutter(&crop, FALSE);
        mHalCamCtrl->mShutterSoundPlayed = FALSE;

        LOGD("%s: Sending Raw Snapshot Callback to Upperlayer", __func__);
        buf_index = recvd_frame->def.idx;

        if (mHalCamCtrl->mDataCb &&
            (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)){
                mHalCamCtrl->mDataCb(
                    CAMERA_MSG_COMPRESSED_IMAGE,
                    mHalCamCtrl->mRawMemory.camera_memory[buf_index], 0, NULL,
                    mHalCamCtrl->mCallbackCookie);
        }
        /* TBD: Temp: To be removed once event handling is enabled */
        mm_app_snapshot_done();
    }
    else{
        /*TBD: v4l2 doesn't have support to provide cropinfo along with
          frame. We'll need to query.*/
        memset(&crop, 0, sizeof(common_crop_t));

        /*maftab*/
        #if 0
        crop.in1_w=mCrop.snapshot.thumbnail_crop.width;
        crop.in1_h=mCrop.snapshot.thumbnail_crop.height;
        crop.out1_w=mThumbnailWidth;
        crop.out1_h=mThumbnailHeight;
        #endif

        LOGD("%s: Call notifyShutter 2nd time", __func__);
        if(!mHalCamCtrl->mShutterSoundPlayed) {
            notifyShutter(&crop, TRUE);
        }
        notifyShutter(&crop, FALSE);
        mHalCamCtrl->mShutterSoundPlayed = FALSE;

        /* The recvd_frame structre we receive from lower library is a local
           variable. So we'll need to save this structure so that we won't
           be later pointing to garbage data when that variable goes out of
           scope */
        mm_camera_ch_data_buf_t* frame =
            (mm_camera_ch_data_buf_t *)malloc(sizeof(mm_camera_ch_data_buf_t));
        if (frame == NULL) {
            LOGE("%s: Error allocating memory to save received_frame structure.", __func__);
            cam_evt_buf_done(mCameraId, recvd_frame);
            return;
        }
        memcpy(frame, recvd_frame, sizeof(mm_camera_ch_data_buf_t));

        if ( NO_ERROR != encodeDisplayAndSave(frame, 0)){
            LOGE("%s: Error while encoding/displaying/saving image", __func__);
            if (frame != NULL) {
                free(frame);
            }
        }

    }

    LOGD("%s: X", __func__);
}

//-------------------------------------------------------------------
// Helper Functions
//-------------------------------------------------------------------
void QCameraStream_Snapshot::handleError()
{
    mm_camera_channel_type_t ch_type;
    LOGD("%s: E", __func__);

    /* Depending upon the state we'll have to
       handle error */
    switch(getSnapshotState()) {
    case SNAPSHOT_STATE_JPEG_ENCODING:
        if(mJpegHeap != NULL) mJpegHeap.clear();
        mJpegHeap = NULL;

    case SNAPSHOT_STATE_YUV_RECVD:
    case SNAPSHOT_STATE_IMAGE_CAPTURE_STRTD:
        stopPolling();
    case SNAPSHOT_STATE_INITIALIZED:
    case SNAPSHOT_STATE_BUF_INITIALIZED:
        if (mSnapshotFormat == PICTURE_FORMAT_JPEG) {
            deinitSnapshotBuffers();
        }else
        {
            deinitRawSnapshotBuffers();
        }
    case SNAPSHOT_STATE_BUF_NOTIF_REGD:
    case SNAPSHOT_STATE_CH_ACQUIRED:
        if (mSnapshotFormat == PICTURE_FORMAT_JPEG) {
            deinitSnapshotChannel(MM_CAMERA_CH_SNAPSHOT);
        }else
        {
            deinitSnapshotChannel(MM_CAMERA_CH_RAW);
        }
    default:
        /* Set the state to ERROR */
        setSnapshotState(SNAPSHOT_STATE_ERROR);
        break;
    }

    LOGD("%s: X", __func__);
}

void QCameraStream_Snapshot::setSnapshotState(int state)
{
    LOGD("%s: Setting snapshot state to: %d",
         __func__, state);
    mSnapshotState = state;
}

int QCameraStream_Snapshot::getSnapshotState()
{
    return mSnapshotState;
}

void QCameraStream_Snapshot::setModeLiveSnapshot(bool value)
{
    mModeLiveSnapshot = value;
}

bool QCameraStream_Snapshot::isLiveSnapshot(void)
{
    return mModeLiveSnapshot;
}
bool QCameraStream_Snapshot::isZSLMode()
{
    return (myMode & CAMERA_ZSL_MODE);
}

//------------------------------------------------------------------
// Constructor and Destructor
//------------------------------------------------------------------
QCameraStream_Snapshot::
QCameraStream_Snapshot(int cameraId, camera_mode_t mode)
  : mCameraId(cameraId),
    myMode (mode),
    mSnapshotFormat(PICTURE_FORMAT_JPEG),
    mPictureWidth(0), mPictureHeight(0),
    mPostviewWidth(0), mPostviewHeight(0),
    mThumbnailWidth(0), mThumbnailHeight(0),
    mJpegOffset(0),
    mSnapshotState(SNAPSHOT_STATE_UNINIT),
    mNumOfSnapshot(0),
    mModeLiveSnapshot(false),
    mBurstModeFlag(false),
    mActualPictureWidth(0),
    mActualPictureHeight(0),
    mJpegDownscaling(false),
    mJpegHeap(NULL),
    mDisplayHeap(NULL),
    mPostviewHeap(NULL),
    mCurrentFrameEncoded(NULL)
  {
    LOGV("%s: E", __func__);

    /*initialize snapshot queue*/
    mSnapshotQueue.init();

    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));
    memset(&mPostviewStreamBuf, 0, sizeof(mPostviewStreamBuf));
    mSnapshotBufferNum = 0;
    mMainSize = 0;
    mThumbSize = 0;
    for(int i = 0; i < mMaxSnapshotBufferCount; i++) {
        mMainfd[i] = 0;
        mThumbfd[i] = 0;
        mCameraMemoryPtrMain[i] = NULL;
        mCameraMemoryPtrThumb[i] = NULL;
    }
    LOGV("%s: X", __func__);
  }


QCameraStream_Snapshot::~QCameraStream_Snapshot() {
    LOGV("%s: E", __func__);

    /* deinit snapshot queue */
    if (mSnapshotQueue.isInitialized()) {
        mSnapshotQueue.deinit();
    }

    LOGV("%s: X", __func__);

}

//------------------------------------------------------------------
// Public Members
//------------------------------------------------------------------
status_t QCameraStream_Snapshot::init()
{
    status_t ret = NO_ERROR;

    LOGV("%s: E", __func__);

    /* Check the state. If we have already started snapshot
       process just return*/
    if (getSnapshotState() != SNAPSHOT_STATE_UNINIT) {
        ret = isZSLMode() ? NO_ERROR : INVALID_OPERATION;
        LOGE("%s: Trying to take picture while snapshot is in progress",
             __func__);
        goto end;
    }

    /* Keep track of number of snapshots to take - in case of
       multiple snapshot/burst mode */
    mNumOfSnapshot = mHalCamCtrl->getNumOfSnapshots();
    if (mNumOfSnapshot == 0) {
        /* If by chance returned value is 0, we'll just take one snapshot */
        mNumOfSnapshot = 1;
    }
    LOGD("%s: Number of images to be captured: %d", __func__, mNumOfSnapshot);

    mNumOfRecievedJPEG = 0;
    /* Check if it's a ZSL mode */
    if (isZSLMode()) {
        ret = initZSLSnapshot();
        goto end;
    }
    /* Check if it's a raw snapshot or JPEG*/
    if( mHalCamCtrl->isRawSnapshot()) {
        mSnapshotFormat = PICTURE_FORMAT_RAW;
        ret = initRawSnapshot(mNumOfSnapshot);
        goto end;
    }
    else{
        mSnapshotFormat = PICTURE_FORMAT_JPEG;
        ret = initJPEGSnapshot(mNumOfSnapshot);
        goto end;
    }

end:
    if (ret == NO_ERROR) {
        setSnapshotState(SNAPSHOT_STATE_INITIALIZED);
    }
    LOGV("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::start(void) {
    status_t ret = NO_ERROR;

    LOGV("%s: E", __func__);
    if (isZSLMode()) {
        /* In case of ZSL, start will only start snapshot stream and
           continuously queue the frames in a queue. When user clicks
           shutter we'll call get buffer from the queue and pass it on */
        ret = startStreamZSL();
        goto end;
    }
    if (mSnapshotFormat == PICTURE_FORMAT_RAW) {
        ret = takePictureRaw();
        goto end;
    }
    else{
        ret = takePictureJPEG();
        goto end;
    }

end:
    if (ret == NO_ERROR) {
        setSnapshotState(SNAPSHOT_STATE_IMAGE_CAPTURE_STRTD);
    }
    LOGV("%s: X", __func__);
    return ret;
  }

void QCameraStream_Snapshot::stopPolling(void)
{
    mm_camera_ops_type_t ops_type;

    if (mSnapshotFormat == PICTURE_FORMAT_JPEG) {
        ops_type = isZSLMode() ? MM_CAMERA_OPS_ZSL : MM_CAMERA_OPS_SNAPSHOT;
        }
    else
        ops_type = MM_CAMERA_OPS_RAW;

    if( NO_ERROR != cam_ops_action(mCameraId, FALSE,
                                          ops_type, this)) {
        LOGE("%s: Failure stopping snapshot", __func__);
    }
}

void QCameraStream_Snapshot::stop(void)
{
    mm_camera_ops_type_t ops_type;

    LOGV("%s: E", __func__);
    Mutex::Autolock l(&snapshotLock);

    /* if it's live snapshot, we don't need to deinit anything
       as recording object will handle everything. We just set
       the state to UNINIT and return */
    if (isLiveSnapshot()) {
        setSnapshotState(SNAPSHOT_STATE_UNINIT);
    }
    else {

        if (getSnapshotState() != SNAPSHOT_STATE_UNINIT) {
            /* Stop polling for further frames */
            stopPolling();

            if(getSnapshotState() == SNAPSHOT_STATE_JPEG_ENCODING) {
                omxJpegCancel();
            }

            /* Depending upon current state, we'll need to allocate-deallocate-deinit*/
            deinit();
        }
    }

    LOGV("%s: X", __func__);

}

void QCameraStream_Snapshot::release()
{
    LOGV("%s: E", __func__);
    Mutex::Autolock l(&snapshotLock);
    /* release is generally called in case of explicit call from
       upper-layer during disconnect. So we need to deinit everything
       whatever state we are in */
    LOGV("Calling omxjpegjoin from release\n");
    omxJpegJoin();
    omxJpegClose();
    deinit();

    LOGV("%s: X", __func__);

}

void QCameraStream_Snapshot::prepareHardware()
{
    LOGV("%s: E", __func__);

    /* Prepare snapshot*/
    cam_ops_action(mCameraId,
                          TRUE,
                          MM_CAMERA_OPS_PREPARE_SNAPSHOT,
                          this);
    LOGV("%s: X", __func__);
}

sp<IMemoryHeap> QCameraStream_Snapshot::getRawHeap() const
{
    return ((mDisplayHeap != NULL) ? mDisplayHeap->mHeap : NULL);
}

QCameraStream*
QCameraStream_Snapshot::createInstance(int cameraId,
                                      camera_mode_t mode)
{

  QCameraStream* pme = new QCameraStream_Snapshot(cameraId, mode);

  return pme;
}

void QCameraStream_Snapshot::deleteInstance(QCameraStream *p)
{
  if (p){
    p->release();
    delete p;
  }
}
}; // namespace android

