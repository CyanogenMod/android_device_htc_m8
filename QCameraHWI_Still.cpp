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

/* Once we give frame for encoding, we get encoded jpeg image
   fragments by fragment. We'll need to store them in a buffer
   to form complete JPEG image */
static void snapshot_jpeg_fragment_cb(uint8_t *ptr,
                                      uint32_t size,
                                      void *user_data)
{
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)user_data;

    LOGD("%s: E",__func__);
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
    LOGD("%s: E ",__func__);

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
    LOGD("%s: E", __func__);

    if (mJpegHeap != NULL) {
        memcpy((uint8_t *)mJpegHeap->mHeap->base()+ mJpegOffset, ptr, size);
        mJpegOffset += size;
    }
    else {
        LOGE("%s: mJpegHeap is NULL!", __func__);
    }

    LOGD("%s: X", __func__);
}


void QCameraStream_Snapshot::
receiveCompleteJpegPicture(jpeg_event_t event)
{
    int msg_type = CAMERA_MSG_COMPRESSED_IMAGE;
    LOGD("%s: E", __func__);

    // Save jpeg for debugging
/*  static int loop = 0;
    char buf[25];
    memset(buf, 0, sizeof(buf));
    snprintf(buf,sizeof(buf), "/data/snapshot_%d.jpg",loop++);
    mm_app_dump_snapshot_frame(buf,(const void *)mJpegHeap->mHeap->base(),
                               mJpegOffset);
*/

    /* If for some reason jpeg heap is NULL we'll just return */
    if (mJpegHeap == NULL) {
        return;
    }

    LOGD("%s: Calling upperlayer callback to store JPEG image", __func__);
    msg_type = isLiveSnapshot() ?
        (int)MEDIA_RECORDER_MSG_COMPRESSED_IMAGE : (int)CAMERA_MSG_COMPRESSED_IMAGE;
    if (mHalCamCtrl->mDataCb &&
        (mHalCamCtrl->mMsgEnabled & msg_type)) {
      // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
      // that the JPEG image's size will probably change from one snapshot
      // to the next, so we cannot reuse the MemoryBase object.
      sp<MemoryBase> buffer = new MemoryBase(mJpegHeap->mHeap, 0, mJpegOffset);
      mHalCamCtrl->mDataCb(msg_type,
                           buffer,
                           mHalCamCtrl->mCallbackCookie);
        buffer = NULL;

    } else {
      LOGW("%s: JPEG callback was cancelled--not delivering image.", __func__);
    }


    //reset jpeg_offset
    mJpegOffset = 0;

    if(mJpegHeap != 0) {
        mJpegHeap.clear();
        mJpegHeap = NULL;
    }

    /* this will free up the resources used for previous encoding task */
    mm_jpeg_encoder_join();

    /* Tell lower layer that we are done with this buffer.
       If it's live snapshot, we don't need to call it. Recording
       object will take care of it */
    if (!isLiveSnapshot()) {
        LOGD("%s: Calling buf done for frame id %d buffer: %x", __func__,
             mCurrentFrameEncoded->snapshot.main.idx,
             (unsigned int)mCurrentFrameEncoded->snapshot.main.frame->buffer);
        cam_evt_buf_done(mCameraId, mCurrentFrameEncoded);
    }

    /* free the resource we allocated to maintain the structure */
    //mm_camera_do_munmap(main_fd, (void *)main_buffer_addr, mSnapshotStreamBuf.frame_len);
    free(mCurrentFrameEncoded);
    setSnapshotState(SNAPSHOT_STATE_JPEG_ENCODE_DONE);

    /* Before leaving check the jpeg queue. If it's not empty give the available
       frame for encoding*/
    if (!mSnapshotQueue.isEmpty()) {
        LOGD("%s: JPEG Queue not empty. Dequeue and encode.", __func__);
        mm_camera_ch_data_buf_t* buf =
            (mm_camera_ch_data_buf_t *)mSnapshotQueue.dequeue();
        /*no need to check buf since the Q is not empty, however clockwork tool
          is complaining it.
          */
        if (buf) {
          encodeDisplayAndSave(buf, 1);
        }
    }
    else
    {
        /* getRemainingSnapshots call will give us number of snapshots still
           remaining after flushing current zsl buffer once*/
        int remaining_cb = mHalCamCtrl->getRemainingSnapshots();
        if (!remaining_cb) {
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
                       int num_of_snapshots)
{
    status_t ret = NO_ERROR;
    mm_camera_ch_image_fmt_parm_t fmt;
    mm_camera_channel_attr_t ch_attr;
    mm_camera_raw_streaming_type_t raw_stream_type =
        MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE;

    LOGD("%s: E", __func__);

    /* Snapshot channel is acquired */
    setSnapshotState(SNAPSHOT_STATE_CH_ACQUIRED);

    /* Initialize stream - set format, acquire channel */
    /*TBD: Currently we only support single raw capture*/
    LOGE("num_of_snapshots = %d",num_of_snapshots);
    if (num_of_snapshots == 1) {
        raw_stream_type = MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE;
    }

    /* Set channel attribute */
    LOGD("%s: Set Raw Snapshot Channel attribute %d", __func__,raw_stream_type);
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
initSnapshotFormat(cam_ctrl_dimension_t *dim)
{
    status_t ret = NO_ERROR;
    mm_camera_ch_image_fmt_parm_t fmt;

    LOGD("%s: E", __func__);

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

    if ((num_of_buf == 0) || (num_of_buf > MM_CAMERA_MAX_NUM_FRAMES)) {
        LOGE("%s: Invalid number of buffers (=%d) requested!", __func__, num_of_buf);
        ret = BAD_VALUE;
        goto end;
    }

    memset(&mSnapshotStreamBuf, 0, sizeof(mm_cameara_stream_buf_t));

    /* Number of buffers to be set*/
    mSnapshotStreamBuf.num = num_of_buf;

    /* Get a frame len for buffer to be allocated*/
    frame_len = mm_camera_get_msm_frame_len(CAMERA_BAYER_SBGGR10,
                                            myMode,
                                            dim->raw_picture_width,
                                            dim->raw_picture_height,
                                            OUTPUT_TYPE_S,
                                            &num_planes, planes);

    mSnapshotStreamBuf.frame_len = frame_len;

    /* Allocate Memory to store snapshot image */
#ifdef USE_ION
    mRawSnapShotHeap =
        new IonPool(MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     planes[0],
                     0,
                     "snapshot camera");
#else
    mRawSnapShotHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_RAW_MAINIMG,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     planes[0],
                     0,
                     "snapshot camera");
#endif
    if (!mRawSnapShotHeap->initialized()) {
        mRawSnapShotHeap.clear();
        LOGE("%s: Error allocating buffer for raw snapshot", __func__);
        ret = NO_MEMORY;
        goto end;
    }
    memset(&reg_buf,  0,  sizeof(mm_camera_reg_buf_t));
    reg_buf.def.buf.mp = new mm_camera_mp_buf_t[mSnapshotStreamBuf.num];
    if (!reg_buf.def.buf.mp) {
      LOGE("%s Error allocating memory for mplanar struct ", __func__);
      mRawSnapShotHeap.clear();
      ret = NO_MEMORY;
      goto end;
    }
    memset(reg_buf.def.buf.mp, 0,
           mSnapshotStreamBuf.num * sizeof(mm_camera_mp_buf_t));
    for(int i = 0; i < mSnapshotStreamBuf.num; i++) {
        frame = &(mSnapshotStreamBuf.frame[i]);
        memset(frame, 0, sizeof(struct msm_frame));
        frame->fd = mRawSnapShotHeap->mHeap->getHeapID();
        frame->buffer = (uint32_t) mRawSnapShotHeap->mHeap->base() +
            mRawSnapShotHeap->mAlignedBufferSize * i;
        frame->path = OUTPUT_TYPE_S;
        frame->cbcr_off = planes[0];
        frame->y_off = 0;
        reg_buf.def.buf.mp[i].frame = *frame;
        reg_buf.def.buf.mp[i].num_planes = num_planes;
        /* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        reg_buf.def.buf.mp[i].planes[0].length = planes[0];
        reg_buf.def.buf.mp[i].planes[0].m.userptr = frame->fd;
        reg_buf.def.buf.mp[i].planes[0].reserved[0] = 0;
        for (int j = 1; j < num_planes; j++) {
          reg_buf.def.buf.mp[i].planes[j].length = planes[j];
          reg_buf.def.buf.mp[i].planes[j].m.userptr = frame->fd;
          reg_buf.def.buf.mp[i].planes[j].reserved[0] =
            reg_buf.def.buf.mp[i].planes[j-1].length;
        }
    }/*end of for loop*/

    /* register the streaming buffers for the channel*/
    reg_buf.ch_type = MM_CAMERA_CH_RAW;
    reg_buf.def.num = mSnapshotStreamBuf.num;

    ret = cam_config_prepare_buf(mCameraId, &reg_buf);
    if(ret != NO_ERROR) {
        LOGV("%s:reg snapshot buf err=%d\n", __func__, ret);
        mRawSnapShotHeap.clear();
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

        /* Clear raw heap*/
        if (mRawSnapShotHeap != NULL) {
            mRawSnapShotHeap.clear();
            mRawSnapShotHeap = NULL;
        }
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
    uint32_t main_frame_offset[MM_CAMERA_MAX_NUM_FRAMES];
    uint32_t thumb_frame_offset[MM_CAMERA_MAX_NUM_FRAMES];

    LOGD("%s: E", __func__);

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


    memset(&mSnapshotStreamBuf, 0, sizeof(mm_cameara_stream_buf_t));
    memset(&mPostviewStreamBuf, 0, sizeof(mm_cameara_stream_buf_t));
    memset(&reg_buf,  0,  sizeof(mm_camera_reg_buf_t));
    /* Number of buffers to be set*/
    mSnapshotStreamBuf.num = num_of_buf;
    mPostviewStreamBuf.num = num_of_buf;
    /* Set the JPEG Rotation here since get_buffer_offset needs
     * the value of rotation.*/
    mHalCamCtrl->setJpegRotation();

    /*TBD: to be modified for 3D*/
    mm_jpeg_encoder_get_buffer_offset( dim->picture_width, dim->picture_height,
                               &y_off, &cbcr_off, &frame_len, &num_planes, planes);
    mSnapshotStreamBuf.frame_len = frame_len;

    /* Allocate Memory to store snapshot image */
#ifdef USE_ION
    mRawHeap =
        new IonPool(
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     cbcr_off,
                     y_off,
                     "snapshot camera");
#else
    mRawHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_MAINIMG,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     cbcr_off,
                     y_off,
                     "snapshot camera");
#endif
    if (!mRawHeap->initialized()) {
        mRawHeap.clear();
        LOGE("%s: Error allocating buffer for snapshot", __func__);
        ret = NO_MEMORY;
        goto end;
    }
    reg_buf.snapshot.main.buf.mp = new mm_camera_mp_buf_t[num_of_buf];
    if (!reg_buf.snapshot.main.buf.mp) {
      LOGE("%s Error allocating memory for mplanar struct ", __func__);
      mRawHeap.clear();
      ret = NO_MEMORY;
      goto end;
    }
    memset(reg_buf.snapshot.main.buf.mp, 0,
           num_of_buf * sizeof(mm_camera_mp_buf_t));
    for(int i = 0; i < mSnapshotStreamBuf.num; i++) {
        frame = &(mSnapshotStreamBuf.frame[i]);
        memset(frame, 0, sizeof(struct msm_frame));
        frame->fd = mRawHeap->mHeap->getHeapID();
        main_frame_offset[i] = mRawHeap->mAlignedBufferSize * i;
        frame->phy_offset = main_frame_offset[i];
        frame->buffer = (uint32_t) mRawHeap->mHeap->base() +
            main_frame_offset[i];
        frame->path = OUTPUT_TYPE_S;
        frame->cbcr_off = cbcr_off;
        frame->y_off = y_off;
        LOGD("%s: Buffer idx: %d  addr: %x fd: %d phy_offset: %d"
             "cbcr_off: %d y_off: %d frame_len: %d", __func__,
             i, (unsigned int)frame->buffer, frame->fd,
             frame->phy_offset, cbcr_off, y_off, frame_len);
        reg_buf.snapshot.main.buf.mp[i].frame = *frame;
        reg_buf.snapshot.main.buf.mp[i].frame_offset = main_frame_offset[i];
        reg_buf.snapshot.main.buf.mp[i].num_planes = num_planes;
        /* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        reg_buf.snapshot.main.buf.mp[i].planes[0].length = planes[0];
        reg_buf.snapshot.main.buf.mp[i].planes[0].m.userptr = frame->fd;
        reg_buf.snapshot.main.buf.mp[i].planes[0].data_offset = y_off;
        reg_buf.snapshot.main.buf.mp[i].planes[0].reserved[0] =
            reg_buf.snapshot.main.buf.mp[i].frame_offset;
        for (int j = 1; j < num_planes; j++) {
          reg_buf.snapshot.main.buf.mp[i].planes[j].length = planes[j];
          reg_buf.snapshot.main.buf.mp[i].planes[j].m.userptr = frame->fd;
          reg_buf.snapshot.main.buf.mp[i].planes[j].data_offset = cbcr_off;
          reg_buf.snapshot.main.buf.mp[i].planes[j].reserved[0] =
            reg_buf.snapshot.main.buf.mp[i].planes[j-1].reserved[0] +
            reg_buf.snapshot.main.buf.mp[i].planes[j-1].length;
        }
    }

    /* allocate memory for postview*/
    frame_len = mm_camera_get_msm_frame_len(dim->thumb_format, myMode,
                                            dim->ui_thumbnail_width,
                                            dim->ui_thumbnail_height,
                                            OUTPUT_TYPE_T,
                                            &num_planes, planes);
    mPostviewStreamBuf.frame_len = frame_len;


     //Postview Image
#ifdef USE_ION
     mPostviewHeap =
        new IonPool(MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     planes[0],
                     0,
                     "thumbnail");
#else
     mPostviewHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_THUMBNAIL,
                     frame_len,
                     num_of_buf,
                     frame_len,
                     planes[0],
                     0,
                     "thumbnail");
#endif
    if (!mPostviewHeap->initialized()) {
        mRawHeap.clear();
        mPostviewHeap.clear();
        LOGE("%s: Error initializing Postview buffer", __func__);
        ret = NO_MEMORY;
        goto end;
    }
    reg_buf.snapshot.thumbnail.buf.mp = new mm_camera_mp_buf_t[num_of_buf];
    if (!reg_buf.snapshot.thumbnail.buf.mp) {
      LOGE("%s Error allocating memory for mplanar struct ", __func__);
      mRawHeap.clear();
      mPostviewHeap.clear();
      ret = NO_MEMORY;
      goto end;
    }
    memset(reg_buf.snapshot.thumbnail.buf.mp, 0,
           num_of_buf * sizeof(mm_camera_mp_buf_t));
    for(int i = 0; i < mPostviewStreamBuf.num; i++) {
        frame = &(mPostviewStreamBuf.frame[i]);
        memset(frame, 0, sizeof(struct msm_frame));
        frame->fd = mPostviewHeap->mHeap->getHeapID();
        thumb_frame_offset[i] = mPostviewHeap->mAlignedBufferSize * i;
        frame->phy_offset = thumb_frame_offset[i];
        frame->buffer = (uint32_t)mPostviewHeap->mHeap->base() +
            thumb_frame_offset[i];
        frame->path = OUTPUT_TYPE_T;
        frame->cbcr_off = planes[0];
        frame->y_off = 0;
        LOGD("%s: Buffer idx: %d  addr: %x fd: %d phy_offset: %d"
             "frame_len: %d", __func__,
             i, (unsigned int)frame->buffer, frame->fd,
             frame->phy_offset, frame_len);
        reg_buf.snapshot.thumbnail.buf.mp[i].frame = *frame;
        reg_buf.snapshot.thumbnail.buf.mp[i].frame_offset =
            thumb_frame_offset[i];
        reg_buf.snapshot.thumbnail.buf.mp[i].num_planes = num_planes;
        /* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        reg_buf.snapshot.thumbnail.buf.mp[i].planes[0].length = planes[0];
        reg_buf.snapshot.thumbnail.buf.mp[i].planes[0].m.userptr = frame->fd;
        reg_buf.snapshot.thumbnail.buf.mp[i].planes[0].reserved[0] =
            reg_buf.snapshot.thumbnail.buf.mp[i].frame_offset;
        for (int j = 1; j < num_planes; j++) {
          reg_buf.snapshot.thumbnail.buf.mp[i].planes[j].length = planes[j];
          reg_buf.snapshot.thumbnail.buf.mp[i].planes[j].m.userptr = frame->fd;
          reg_buf.snapshot.thumbnail.buf.mp[i].planes[j].reserved[0] =
            reg_buf.snapshot.thumbnail.buf.mp[i].planes[j-1].reserved[0] +
            reg_buf.snapshot.thumbnail.buf.mp[i].planes[j-1].length;
        }
    }/*end of for loop*/

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
        if (mRawHeap != NULL){
            mRawHeap.clear();
            mRawHeap = NULL;
        }
        if (mPostviewHeap != NULL) {
            mPostviewHeap.clear();
            mPostviewHeap = NULL;
        }

    }
end:
    LOGD("%s: X", __func__);
    return ret;
}

void QCameraStream_Snapshot::deInitBuffer(void)
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
    }
    else
    {
        deinitSnapshotBuffers();
    }

    /* deinit jpeg buffer if allocated */
    if(mJpegHeap != NULL) mJpegHeap.clear();
    mJpegHeap = NULL;

    /* memset some global structure */
    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));
    memset(&mPostviewStreamBuf, 0, sizeof(mPostviewStreamBuf));
    mSnapshotQueue.flush();

    mNumOfSnapshot = 0;
    setSnapshotState(SNAPSHOT_STATE_UNINIT);

    LOGD("%s: X", __func__);
}

/*Temp: to be removed once event handling is enabled in mm-camera.
  We need two events - one to call notifyShutter and other event for
  stream-off to disable OPS_SNAPSHOT*/
void QCameraStream_Snapshot::runSnapshotThread(void *data)
{
    LOGD("%s: E", __func__);
    /* play shutter sound */
    LOGD("%s:Play shutter sound only", __func__);

    notifyShutter(NULL, TRUE);

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
    ret = initSnapshotFormat(&dim);
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
    mm_camera_reg_buf_t reg_buf;

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
       depends on sensor to sensor. We call setDimension which will
       give us raw width and height */
    LOGD("%s: Get Raw Snapshot Dimension", __func__);
    ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
    if (NO_ERROR != ret) {
      LOGE("%s: error - can't set snapshot parms!", __func__);
      ret = FAILED_TRANSACTION;
      goto end;
    }
    LOGD("%s: Raw Snapshot dimension: %dx%d", __func__,
         dim.raw_picture_width,
         dim.raw_picture_height);

    
    ret = initRawSnapshotChannel(&dim, num_of_snapshots);
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

    /* config the parmeters and see if we need to re-init the stream*/
    LOGD("%s: Configure Snapshot Dimension", __func__);
    ret = configSnapshotDimension(&dim);
    if (ret != NO_ERROR) {
        LOGE("%s: Setting snapshot dimension failed", __func__);
        goto end;
    }

    /* Initialize stream - set format, acquire channel */
    ret = initSnapshotFormat(&dim);
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

    LOGD("%s: E", __func__);

    /* set flag to indicate we are doing livesnapshot */
    setModeLiveSnapshot(true);

    LOGD("%s:Passed picture size: %d X %d", __func__,
         dim->picture_width, dim->picture_height);
    LOGD("%s:Passed thumbnail size: %d X %d", __func__,
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
            mHalCamCtrl->mDataCb(MEDIA_RECORDER_MSG_COMPRESSED_IMAGE,
                                       NULL,
                                       mHalCamCtrl->mCallbackCookie);
        }
        setModeLiveSnapshot(false);
        goto end;
    }

end:
    LOGD("%s: X", __func__);
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

    LOGD("%s: E", __func__);

    /* If it's the only frame, we directly pass to encoder.
       If not, we'll queue it and check during next jpeg callback.
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

        LOGD("%s:Allocating memory to store jpeg image."
             "main image size: %dX%d frame_len: %d", __func__,
             mPictureWidth, mPictureHeight, frame_len);
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

        /*TBD: Move JPEG handling to the mm-camera library */
        LOGD("Setting callbacks, initializing encoder and start encoding.");
        set_callbacks(snapshot_jpeg_fragment_cb, snapshot_jpeg_cb, this);
        mm_jpeg_encoder_init();
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

        /* mm_jpeg_encoder returns FALSE if there's any problem with encoding */
        if (!mm_jpeg_encoder_encode((const cam_ctrl_dimension_t *)&dimension,
                          (uint8_t *)postviewframe->buffer,
                          postviewframe->fd,
                          postviewframe->phy_offset,
                          (uint8_t *)mainframe->buffer,
                          mainframe->fd,
                          mainframe->phy_offset,
                          &crop,
                          NULL,
                          0,
                          -1,
                          &main_crop_offset,
                          &thumb_crop_offset)){
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
    image_rect_type size;
    LOGD("%s: E", __func__);

    if(mPlayShutterSoundOnly) {
        /* At this point, invoke Notify Callback to play shutter sound only.
         * We want to call notify callback again when we have the
         * yuv picture ready. This is to reduce blanking at the time
         * of displaying postview frame. Using ext2 to indicate whether
         * to play shutter sound only or register the postview buffers.
         */
        LOGD("%s: Calling callback to play shutter sound", __func__);
        mHalCamCtrl->mNotifyCb(CAMERA_MSG_SHUTTER, 0,
                                     mPlayShutterSoundOnly,
                                     mHalCamCtrl->mCallbackCookie);
        return;
    }

    if (mHalCamCtrl->mNotifyCb &&
        (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_SHUTTER)) {
        mDisplayHeap = mPostviewHeap;
        if (crop != NULL && (crop->in1_w != 0 && crop->in1_h != 0)) {
            size.width = crop->in1_w;
            size.height = crop->in1_h;
            LOGD("%s: Size from cropinfo: %dX%d", __func__,
                 size.width, size.height);
        }
        else {
            size.width = mPostviewWidth;
            size.height = mPostviewHeight;
            LOGD("%s: Size from global: %dX%d", __func__,
                 size.width, size.height);
        }
        /*if(strTexturesOn == true) {
            mDisplayHeap = mRawHeap;
            size.width = mPictureWidth;
            size.height = mPictureHeight;
        }*/
        /* Now, invoke Notify Callback to unregister preview buffer
         * and register postview buffer with surface flinger. Set ext2
         * as 0 to indicate not to play shutter sound.
         */

        mHalCamCtrl->mNotifyCb(CAMERA_MSG_SHUTTER, (int32_t)&size, 0,
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
    LOGD("%s: Send frame for encoding", __func__);
    /*TBD: Pass 0 as cropinfo for now as v4l2 doesn't provide
      cropinfo. It'll be changed later.*/
    if(!mActive) {
        LOGE("Cancel Picture.. Stop is called");
        return BAD_VALUE;
    }
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
                                       NULL,
                                       mHalCamCtrl->mCallbackCookie);
        }
        goto end;
    }

    /* Display postview image*/
    /* If it's burst mode, we won't be displaying postview of all the captured
       images - only the first one */
    LOGD("%s: Burst mode flag  %d", __func__, mBurstModeFlag);
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

    // send upperlayer callback
     if (mHalCamCtrl->mDataCb &&
         (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_RAW_IMAGE)){
         buf_index = recvd_frame->snapshot.main.idx;
         mHalCamCtrl->mDataCb(CAMERA_MSG_RAW_IMAGE,
                                    mRawHeap->mBuffers[buf_index],
                                    mHalCamCtrl->mCallbackCookie);
     }

end:
    LOGD("%s: X", __func__);
    return ret;
}


status_t QCameraStream_Snapshot::processSnapshotFrame(mm_camera_ch_data_buf_t* recvd_frame)
{
    int buf_index = 0;
    common_crop_t crop;
    LOGD("%s: E ", __func__);
    Mutex::Autolock lock(mStopCallbackLock);
    if(!mActive) {
        return NO_ERROR;
    }


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
        notifyShutter(NULL, FALSE);
        LOGD("%s: Sending Raw Snapshot Callback to Upperlayer", __func__);
        buf_index = recvd_frame->def.idx;
        if (mHalCamCtrl->mDataCb &&
            (mHalCamCtrl->mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)){
                mStopCallbackLock.unlock();
                mHalCamCtrl->mDataCb(
                    CAMERA_MSG_COMPRESSED_IMAGE,
                    mRawSnapShotHeap->mBuffers[buf_index],
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
        notifyShutter(&crop, FALSE);

        /* The recvd_frame structre we receive from lower library is a local
           variable. So we'll need to save this structure so that we won't
           be later pointing to garbage data when that variable goes out of
           scope */
        mm_camera_ch_data_buf_t* frame =
            (mm_camera_ch_data_buf_t *)malloc(sizeof(mm_camera_ch_data_buf_t));
        if (frame == NULL) {
            LOGE("%s: Error allocating memory to save received_frame structure.", __func__);
            cam_evt_buf_done(mCameraId, recvd_frame);
            return BAD_VALUE;
        }
        memcpy(frame, recvd_frame, sizeof(mm_camera_ch_data_buf_t));
        mStopCallbackLock.unlock();

        if ( NO_ERROR != encodeDisplayAndSave(frame, 0)){
            LOGE("%s: Error while encoding/displaying/saving image", __func__);
            if (frame != NULL) {
                free(frame);
            }
        }

    }

    LOGD("%s: X", __func__);
    return NO_ERROR;
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

    case SNAPSHOT_STATE_BUF_INITIALIZED:
        if (mSnapshotFormat == PICTURE_FORMAT_JPEG) {
            deinitSnapshotBuffers();
        }else
        {
            deinitRawSnapshotBuffers();
        }
    case SNAPSHOT_STATE_BUF_NOTIF_REGD:
    case SNAPSHOT_STATE_CH_ACQUIRED:
    case SNAPSHOT_STATE_INITIALIZED:
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
  : QCameraStream(cameraId,mode),
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
    mDisplayHeap(NULL),mRawHeap(NULL),
    mPostviewHeap(NULL), mRawSnapShotHeap(NULL),
    mCurrentFrameEncoded(NULL)
  {
    LOGV("%s: E", __func__);

    /*initialize snapshot queue*/
    mSnapshotQueue.init();

    memset(&mSnapshotStreamBuf, 0, sizeof(mSnapshotStreamBuf));
    memset(&mPostviewStreamBuf, 0, sizeof(mPostviewStreamBuf));

    LOGV("%s: X", __func__);
  }


QCameraStream_Snapshot::~QCameraStream_Snapshot() {
    LOGV("%s: E", __func__);

    /* deinit snapshot queue */
    if (mSnapshotQueue.isInitialized()) {
        mSnapshotQueue.deinit();
    }
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

//------------------------------------------------------------------
// Public Members
//------------------------------------------------------------------
status_t QCameraStream_Snapshot::init()
{
    status_t ret = NO_ERROR;
    mm_camera_op_mode_type_t op_mode;

    LOGV("%s: E", __func__);

     /* Check the state. If we have already started snapshot
       process just return*/
    if (getSnapshotState() != SNAPSHOT_STATE_UNINIT) {
        ret = isZSLMode() ? NO_ERROR : INVALID_OPERATION;
        LOGE("%s: Trying to take picture while snapshot is in progress",
             __func__);
        goto end;
    }

    ret = QCameraStream::initChannel (mCameraId, MM_CAMERA_CH_SNAPSHOT_MASK);
    if (NO_ERROR!=ret) {
        LOGE("%s E: can't init native cammera preview ch\n",__func__);
        return ret;
    }
    mInit = true;

end:
    if (ret == NO_ERROR) {
        setSnapshotState(SNAPSHOT_STATE_INITIALIZED);
    }
    LOGV("%s: X", __func__);
    return ret;
}

status_t QCameraStream_Snapshot::start(void)
{
    status_t ret = NO_ERROR;
    LOGV("%s: E", __func__);

     /* Keep track of number of snapshots to take - in case of
       multiple snapshot/burst mode */
    mNumOfSnapshot = mHalCamCtrl->getNumOfSnapshots();
    if (mNumOfSnapshot == 0) {
        /* If by chance returned value is 0, we'll just take one snapshot */
        mNumOfSnapshot = 1;
    }
    LOGD("%s: Number of images to be captured: %d", __func__, mNumOfSnapshot);

    /* Check if it's a ZSL mode */
    if (isZSLMode()) {
        prepareHardware();
        ret = initZSLSnapshot();
        if(ret != NO_ERROR) {
            LOGE("%s : Error while Initializing ZSL snapshot",__func__);
            goto end;
        }

        /* In case of ZSL, start will only start snapshot stream and
           continuously queue the frames in a queue. When user clicks
           shutter we'll call get buffer from the queue and pass it on */
        ret = startStreamZSL();
        goto end;
    }

    /* Check if it's a raw snapshot or JPEG*/
    if( mHalCamCtrl->isRawSnapshot()) {
        mSnapshotFormat = PICTURE_FORMAT_RAW;
        ret = initRawSnapshot(mNumOfSnapshot);
    }else{
        //JPEG
        mSnapshotFormat = PICTURE_FORMAT_JPEG;
        ret = initJPEGSnapshot(mNumOfSnapshot);
    }
    if(ret != NO_ERROR) {
        LOGE("%s : Error while Initializing snapshot",__func__);
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
        mActive = true;
    }
    LOGV("%s: X", __func__);
    return ret;
  }

void QCameraStream_Snapshot::stopPolling(void)
{
    mm_camera_ops_type_t ops_type;

    if (mSnapshotFormat == PICTURE_FORMAT_JPEG) {
        ops_type = isZSLMode() ? MM_CAMERA_OPS_ZSL : MM_CAMERA_OPS_SNAPSHOT;
    }else
        ops_type = MM_CAMERA_OPS_RAW;

    if( NO_ERROR != cam_ops_action(mCameraId, FALSE,
                                          ops_type, this)) {
        LOGE("%s: Failure stopping snapshot", __func__);
    }
}

void QCameraStream_Snapshot::stop(void)
{
    //mm_camera_ops_type_t ops_type;
    LOGV("%s: E", __func__);

    if(!mActive) {
      return;
    }
    mActive = false;
    /* if it's live snapshot, we don't need to deinit anything
       as recording object will handle everything. We just set
       the state to UNINIT and return */
    Mutex::Autolock lock(mStopCallbackLock);
    if (isLiveSnapshot()) {
        setSnapshotState(SNAPSHOT_STATE_UNINIT);
    }
    else {

        if (getSnapshotState() != SNAPSHOT_STATE_UNINIT) {
            /* Stop polling for further frames */
            stopPolling();

            if(getSnapshotState() == SNAPSHOT_STATE_JPEG_ENCODING) {
                mm_jpeg_encoder_cancel();
            }

            /* Depending upon current state, we'll need to allocate-deallocate-deinit*/
            deInitBuffer();
        }
    }

    LOGV("%s: X", __func__);

}

void QCameraStream_Snapshot::release()
{
    status_t ret = NO_ERROR;

    LOGV("%s: E", __func__);
    if(!mInit)
    {
      LOGE("%s : Stream not Initalized",__func__);
      return;
    }

    if(mActive) {
      this->stop();
      mActive = FALSE;
    }
    ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_RAW);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Deinit RAW channel failed=%d\n", __func__, ret);
    }
    ret= QCameraStream::deinitChannel(mCameraId, MM_CAMERA_CH_SNAPSHOT);
    if(ret != MM_CAMERA_OK) {
      LOGE("%s:Deinit Snapshot channel failed=%d\n", __func__, ret);
    }
    
    mInit = false;
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
    p = NULL;
  }
}

}; // namespace android

