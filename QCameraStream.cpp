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
#define LOG_TAG __FILE__
#include <utils/Log.h>
#include <utils/threads.h>


#include "QCameraStream.h"

/* QCameraStream class implementation goes here*/
/* following code implement the control logic of this class*/

namespace android {

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

static void preview_notify_cb(mm_camera_ch_data_buf_t *frame,
                                void *user_data)
{
    LOGV("%s : E",__func__);
    QCameraStream_preview *pme = (QCameraStream_preview *)user_data;
    if((frame->type == MM_CAMERA_CH_PREVIEW) && (pme != NULL)) {
        pme->processPreviewFrame(frame);
    }else{
        LOGE("Invalid stream Callback");
    }
    LOGV("%s : X",__func__);
}

static void record_notify_cb(mm_camera_ch_data_buf_t *frame,
                                void *user_data)
{
    LOGV("%s : E",__func__);
    QCameraStream_record *pme = (QCameraStream_record *)user_data;
    if((frame->type == MM_CAMERA_CH_VIDEO) && (pme != NULL)) {
        pme->processRecordFrame(frame);
    }else{
        LOGE("Invalid stream Callback");
    }
    LOGV("%s : X",__func__);

}

static void snapshot_notify_cb(mm_camera_ch_data_buf_t *frame,
                                void *user_data)
{
    LOGV("%s : E",__func__);
    QCameraStream_Snapshot *pme = (QCameraStream_Snapshot *)user_data;
    if((frame->type == MM_CAMERA_CH_SNAPSHOT) && (pme != NULL)) {
        pme->processSnapshotFrame(frame);
    }else{
        LOGE("Invalid stream Callback");
    }
    LOGV("%s : X",__func__);

}

#if 0
/* initialize a streaming channel*/
status_t QCameraStream::initChannel(int cameraId,
                                    uint32_t ch_type_mask)
{
    int rc = MM_CAMERA_OK;
    status_t ret = NO_ERROR;
    int i;
    LOGV("QCameraStream::initChannel : E");

    rc = cam_ops_ch_acquire(cameraId, MM_CAMERA_CH_PREVIEW);
    LOGV("%s:ch_acquire MM_CAMERA_CH_PREVIEW, rc=%d\n",__func__, rc);
    if(MM_CAMERA_OK != rc) {
            LOGE("%s: preview channel acquir error =%d\n", __func__, rc);
            LOGE("%s: X", __func__);
            return BAD_VALUE;
    }
    /*Callback register*/
    /* register a notify into the mmmm_camera_t object*/
    ret = cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_PREVIEW,
                                            stream_notify_cb,
                                            this);
    LOGV("Buf notify MM_CAMERA_CH_PREVIEW, rc=%d\n",rc);

    rc = cam_ops_ch_acquire(cameraId, MM_CAMERA_CH_VIDEO);
    LOGV("%s:ch_acquire MM_CAMERA_CH_VIDEO, rc=%d\n",__func__, rc);
    if(MM_CAMERA_OK != rc) {
            LOGE("%s: preview channel acquir error =%d\n", __func__, rc);
            LOGE("%s: X", __func__);
            return BAD_VALUE;
    }
    /*Callback register*/
    /* register a notify into the mmmm_camera_t object*/
    ret = cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_VIDEO,
                                            stream_notify_cb,
                                            this);
    LOGV("Buf notify MM_CAMERA_CH_VIDEO, rc=%d\n",rc);

    LOGV("%s: Acquire Snapshot Channel", __func__);
    ret = cam_ops_ch_acquire(mCameraId, MM_CAMERA_CH_SNAPSHOT);
    if (NO_ERROR != ret) {
        LOGE("%s: Failure Acquiring Snapshot Channel error =%d\n", __func__, ret);
        return BAD_VALUE;
    }
    (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_RAW,
                                    stream_notify_cb, this);
    LOGV("Buf notify MM_CAMERA_CH_RAW, rc=%d\n",rc);
    (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_SNAPSHOT,
                                    stream_notify_cb, this);
    LOGV("Buf notify MM_CAMERA_CH_SNAPSHOT, rc=%d\n",rc);

    ret = (MM_CAMERA_OK==rc)? NO_ERROR : BAD_VALUE;
    LOGV("QCameraStream::initChannel : X, ret = %d",ret);
    return ret;
}

status_t QCameraStream::deinitChannel(int cameraId,
                                    mm_camera_channel_type_t ch_type)
{
    int rc = MM_CAMERA_OK;
    LOGV("%s: E, channel = %d\n", __func__, ch_type);

    if (MM_CAMERA_CH_MAX <= ch_type) {
        LOGE("%s: X: BAD_VALUE", __func__);
        return BAD_VALUE;
    }
    (void)cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_RAW,
                                                NULL,
                                                NULL);
    (void)cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_SNAPSHOT,
                                                NULL,
                                                NULL);

    LOGE("release channel");
    cam_ops_ch_release(cameraId, MM_CAMERA_CH_SNAPSHOT);

    (void)cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_VIDEO,
                                                NULL,
                                                NULL);

    LOGE("release channel");
    cam_ops_ch_release(cameraId, MM_CAMERA_CH_VIDEO);

    (void)cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_PREVIEW,
                                                NULL,
                                                NULL);

    LOGE("release channel");
    cam_ops_ch_release(cameraId, MM_CAMERA_CH_PREVIEW);

    LOGV("%s: X, channel = %d\n", __func__, ch_type);
    return NO_ERROR;
}
#endif
// ---------------------------------------------------------------------------
// QCameraStream
// ---------------------------------------------------------------------------

#if 1
/* initialize a streaming channel*/
status_t QCameraStream::initChannel(int cameraId,
                                    uint32_t ch_type_mask)
{
    int rc = MM_CAMERA_OK;
    status_t ret = NO_ERROR;
    mm_camera_op_mode_type_t op_mode=MM_CAMERA_OP_MODE_VIDEO;
    int i;

    LOGV("QCameraStream::initChannel : E");
    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type_mask){
        rc = cam_ops_ch_acquire(cameraId, MM_CAMERA_CH_PREVIEW);
        LOGV("%s:ch_acquire MM_CAMERA_CH_PREVIEW, rc=%d\n",__func__, rc);
        if(MM_CAMERA_OK != rc) {
                LOGE("%s: preview channel acquir error =%d\n", __func__, rc);
                LOGE("%s: X", __func__);
                return BAD_VALUE;
        }
        /*Callback register*/
        /* register a notify into the mmmm_camera_t object*/
        ret = cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_PREVIEW,
                                                preview_notify_cb,
                                                this);
        LOGV("Buf notify MM_CAMERA_CH_PREVIEW, rc=%d\n",rc);
    }else if(MM_CAMERA_CH_VIDEO_MASK & ch_type_mask){
        rc = cam_ops_ch_acquire(cameraId, MM_CAMERA_CH_VIDEO);
        LOGV("%s:ch_acquire MM_CAMERA_CH_VIDEO, rc=%d\n",__func__, rc);
        if(MM_CAMERA_OK != rc) {
                LOGE("%s: preview channel acquir error =%d\n", __func__, rc);
                LOGE("%s: X", __func__);
                return BAD_VALUE;
        }
        /*Callback register*/
        /* register a notify into the mmmm_camera_t object*/
        ret = cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_VIDEO,
                                                record_notify_cb,
                                                this);
        LOGV("Buf notify MM_CAMERA_CH_VIDEO, rc=%d\n",rc);
    }else if(MM_CAMERA_CH_SNAPSHOT_MASK & ch_type_mask){
        //if( mHalCamCtrl->isRawSnapshot()){
            ret = cam_ops_ch_acquire(mCameraId, MM_CAMERA_CH_RAW);
            if (NO_ERROR != ret) {
                LOGE("%s: Failure Acquiring Snapshot Channel error =%d\n", __func__, ret);
                return BAD_VALUE;
            }
            (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_RAW,
                                            snapshot_notify_cb, this);
            LOGV("Buf notify MM_CAMERA_CH_RAW, rc=%d\n",rc);
        //}else{
            LOGV("%s: Acquire Snapshot Channel", __func__);
            ret = cam_ops_ch_acquire(mCameraId, MM_CAMERA_CH_SNAPSHOT);
            if (NO_ERROR != ret) {
                LOGE("%s: Failure Acquiring Snapshot Channel error =%d\n", __func__, ret);
                return BAD_VALUE;
            }
            (void) cam_evt_register_buf_notify(mCameraId, MM_CAMERA_CH_SNAPSHOT,
                                        snapshot_notify_cb, this);
        //}

        LOGV("Buf notify MM_CAMERA_CH_SNAPSHOT, rc=%d\n",rc);
    }

    ret = (MM_CAMERA_OK==rc)? NO_ERROR : BAD_VALUE;
    LOGV("QCameraStream::initChannel : X, ret = %d",ret);
    return ret;
}

status_t QCameraStream::deinitChannel(int cameraId,
                                    mm_camera_channel_type_t ch_type)
{
    int rc = MM_CAMERA_OK;
    LOGV("%s: E, channel = %d\n", __func__, ch_type);

    if (MM_CAMERA_CH_MAX <= ch_type) {
        LOGE("%s: X: BAD_VALUE", __func__);
        return BAD_VALUE;
    }
    cam_ops_ch_release(cameraId, ch_type);
    LOGE("release channel");

    (void)cam_evt_register_buf_notify(mCameraId, ch_type,
                                                NULL,
                                                NULL);
    LOGV("%s: X, channel = %d\n", __func__, ch_type);
    return NO_ERROR;
}
#endif

QCameraStream::QCameraStream (){
    mInit = false;
    mActive = false;
    memset(&mCrop, 0, sizeof(mm_camera_ch_crop_t));
}

QCameraStream::QCameraStream (int cameraId, camera_mode_t mode)
              :mCameraId(cameraId),
               myMode(mode)
{
    mInit = false;
    mActive = false;

    /* memset*/
    memset(&mCrop, 0, sizeof(mm_camera_ch_crop_t));

}

QCameraStream::~QCameraStream () {;}


status_t QCameraStream::init() {
    return NO_ERROR;
}

status_t QCameraStream::start() {
    return NO_ERROR;
}

void QCameraStream::stop() {
    return;
}

void QCameraStream::release() {
    return;
}

void QCameraStream::setHALCameraControl(QCameraHardwareInterface* ctrl) {

    /* provide a frame data user,
    for the  queue monitor thread to call the busy queue is not empty*/
    mHalCamCtrl = ctrl;
}

status_t QCameraStream::initBuffers(uint8_t ch_type_mask,sp<MemPool> mHeap)
{
    uint8_t buffer_count = 0;
    status_t ret = NO_ERROR;
    int output_type = 0;
    int ch_type = 0;
    uint32_t y_off=0;
    uint32_t cbcr_off=0;

    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type_mask) {
        buffer_count = PREVIEW_BUFFER_COUNT;
        output_type = OUTPUT_TYPE_P;
        ch_type = MM_CAMERA_CH_PREVIEW;
    }else if(MM_CAMERA_CH_VIDEO_MASK & ch_type_mask){
        buffer_count = VIDEO_BUFFER_COUNT;
        output_type = OUTPUT_TYPE_V;
        ch_type = MM_CAMERA_CH_VIDEO;
    }/*else if(MM_CAMERA_CH_SNAPSHOT_MASK & ch_type_mask){
        buffer_count = PREVIEW_BUFFER_COUNT;
        output_type = OUTPUT_TYPE_S;
        ch_type = MM_CAMERA_CH_SNAPSHOT;
    }*/

    mp = new mm_camera_mp_buf_t[buffer_count];
    if(mp == NULL) {
        return NO_MEMORY;
    }
    offset = new uint32_t[buffer_count];
    if(offset == NULL) {
        delete[] mp;
        mp = NULL;
        return NO_MEMORY;
    }

    frames = new msm_frame[buffer_count];
    if(frames == NULL) {
        delete[] mp;
        delete[] offset;
        mp = NULL;
        offset = NULL;
        return NO_MEMORY;
    }

    memset(mp,0,sizeof(mm_camera_mp_buf_t) * buffer_count);
    memset(offset,0,sizeof(uint32_t) * buffer_count);
    memset(frames,0,sizeof(struct msm_frame) * buffer_count);

    for (int cnt = 0; cnt < buffer_count; cnt++) {
        frames[cnt].fd = mHeap->mHeap->getHeapID();
        frames[cnt].buffer =
          (uint32_t)mHeap->mHeap->base() + mHeap->mAlignedBufferSize * cnt;
        frames[cnt].y_off = 0;
        frames[cnt].cbcr_off = planes[0];
        frames[cnt].path = output_type;
        offset[cnt] =  mHeap->mAlignedBufferSize * cnt;
        LOGE ("initDisplay :  Preview heap buffer=%lu fd=%d offset = %d \n",
          (unsigned long)frames[cnt].buffer, frames[cnt].fd, offset[cnt]);
        mp[cnt].frame = frames[cnt];
        mp[cnt].frame_offset = offset[cnt];
        mp[cnt].num_planes = num_planes;
        /* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        mp[cnt].planes[0].length = planes[0];
        mp[cnt].planes[0].m.userptr = frames[cnt].fd;
        mp[cnt].planes[0].reserved[0] = mp[cnt].frame_offset;
        for (int j = 1; j < num_planes; j++) {
          mp[cnt].planes[j].length = planes[j];
          mp[cnt].planes[j].m.userptr = frames[cnt].fd;
          mp[cnt].planes[j].reserved[0] =
              mp[cnt].planes[j-1].reserved[0] + mp[cnt].planes[j-1].length;
        }
    }

    /* register the streaming buffers for the channel*/
    memset(&mStreamBuf,  0,  sizeof(mStreamBuf));
    mStreamBuf.ch_type = (mm_camera_channel_type_t)ch_type;

    if(ch_type == MM_CAMERA_CH_PREVIEW ) {
        mStreamBuf.preview.num = buffer_count;
        mStreamBuf.preview.buf.mp = mp;
    } else if(ch_type == MM_CAMERA_CH_VIDEO){
        mStreamBuf.video.video.num = buffer_count;
        mStreamBuf.video.video.buf.mp = mp;
    }

    LOGE("Stream buf type =%d, offset[1] =%d, buffer[1] =%lx", mStreamBuf.ch_type, offset[1], frames[1].buffer);
    ret = cam_config_prepare_buf(mCameraId, &mStreamBuf);
    if(ret != MM_CAMERA_OK) {
        LOGV("%s:reg buf err=%d\n", __func__, ret);
        ret = BAD_VALUE;
    }else
        ret = NO_ERROR;

    return ret;
}

status_t QCameraStream::setFormat(uint8_t ch_type_mask)
{
    int rc = MM_CAMERA_OK;
    status_t ret = NO_ERROR;
    int width = 0;  /* width of channel      */
    int height = 0; /* height of channel */
    cam_ctrl_dimension_t dim;
    mm_camera_ch_image_fmt_parm_t fmt;

    LOGE("%s: E",__func__);

    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));
    rc = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
    if (MM_CAMERA_OK != rc) {
      LOGE("%s: error - can't get camera dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }

    memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type_mask){
        fmt.ch_type = MM_CAMERA_CH_PREVIEW;
        fmt.def.fmt = CAMERA_YUV_420_NV12; //dim.prev_format;
        fmt.def.dim.width = dim.display_width;
        fmt.def.dim.height =  dim.display_height;
    }else if(MM_CAMERA_CH_VIDEO_MASK & ch_type_mask){
        fmt.ch_type = MM_CAMERA_CH_VIDEO;
        fmt.video.video.fmt = CAMERA_YUV_420_NV21; //dim.enc_format;
        fmt.video.video.dim.width = dim.video_width;
        fmt.video.video.dim.height = dim.video_height;
    }/*else if(MM_CAMERA_CH_SNAPSHOT_MASK & ch_type_mask){
        if(mHalCamCtrl->isRawSnapshot()) {
            fmt.ch_type = MM_CAMERA_CH_RAW;
            fmt.def.fmt = CAMERA_BAYER_SBGGR10;
            fmt.def.dim.width = dim.raw_picture_width;
            fmt.def.dim.height = dim.raw_picture_height;
        }else{
            //Jpeg???
            fmt.ch_type = MM_CAMERA_CH_SNAPSHOT;
            fmt.snapshot.main.fmt = dim.main_img_format;
            fmt.snapshot.main.dim.width = dim.picture_width;
            fmt.snapshot.main.dim.height = dim.picture_height;

            fmt.snapshot.thumbnail.fmt = dim.thumb_format;
            fmt.snapshot.thumbnail.dim.width = dim.ui_thumbnail_width;
            fmt.snapshot.thumbnail.dim.height = dim.ui_thumbnail_height;
        }
    }*/

    rc = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt);
    LOGV("%s: Stream MM_CAMERA_PARM_CH_IMAGE_FMT rc = %d\n", __func__, rc);
    if(MM_CAMERA_OK != rc) {
            LOGE("%s:set stream channel format err=%d\n", __func__, ret);
            LOGE("%s: X", __func__);
            ret = BAD_VALUE;
    }
    LOGE("%s: X",__func__);
    return ret;
}

status_t QCameraStream::start_stream(uint8_t ch_type, sp<MemPool> mHeap)
{
    status_t ret = NO_ERROR;
    mm_camera_op_mode_type_t op_mode=MM_CAMERA_OP_MODE_VIDEO; //TODO : Check  Opmode

     ret = setFormat(ch_type);
    if(ret != NO_ERROR) {
        LOGE("%s : Error while setting Format",__func__);
        return BAD_VALUE;
    }
    /*Initialize Buffer*/
    ret = initBuffers(ch_type,mHeap);
    if(ret != NO_ERROR) {
        LOGE("%s : Error while initlizing stream buffer",__func__);
        return BAD_VALUE;
    }

    //Set Op Mode
    if (!(myMode & CAMERA_ZSL_MODE))
    {
        LOGE("Video Mode");
        op_mode = MM_CAMERA_OP_MODE_VIDEO;
        ret = cam_config_set_parm (mCameraId, MM_CAMERA_PARM_OP_MODE,
                                        &op_mode);
    }else{
        LOGE("ZSL mode...  setting Op mode");
        op_mode = MM_CAMERA_OP_MODE_ZSL;
        ret = cam_config_set_parm (mCameraId, MM_CAMERA_PARM_OP_MODE,
                                        &op_mode);
    }

    if(MM_CAMERA_OK != ret) {
        LOGE("%s: X :set mode MM_CAMERA_OP_MODE_VIDEO err=%d\n", __func__, ret);
        return BAD_VALUE;
    }

    /* call mm_camera action start(...)  */
    LOGE("Starting Stream. ");
    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type) {
        ret = cam_ops_action(mCameraId, TRUE, MM_CAMERA_OPS_PREVIEW, 0);
    }else if(MM_CAMERA_CH_VIDEO_MASK & ch_type){
        ret = cam_ops_action(mCameraId, TRUE, MM_CAMERA_OPS_VIDEO, 0);
    }else if(MM_CAMERA_CH_SNAPSHOT_MASK & ch_type){
        ret = cam_ops_action(mCameraId, TRUE, MM_CAMERA_OPS_SNAPSHOT, 0);
    }
    if (MM_CAMERA_OK != ret) {
      LOGE ("%s: preview streaming start err=%d\n", __func__, ret);
      return BAD_VALUE;
    }
    return NO_ERROR;

}

status_t QCameraStream::stop_stream(uint8_t ch_type)
{
    status_t ret = NO_ERROR;

    /*Steam OFF*/
    /* call mm_camera action start(...)  */
    LOGE("Stop Stream. ");
    if(MM_CAMERA_CH_PREVIEW_MASK & ch_type) {
        ret = cam_ops_action(mCameraId, FALSE, MM_CAMERA_OPS_PREVIEW, 0);
        LOGE("Stop action");

        ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_PREVIEW);
        if(ret != MM_CAMERA_OK) {
            LOGE("%s:Unreg Stream buf err=%d\n", __func__, ret);
        }
    } else if(MM_CAMERA_CH_VIDEO_MASK & ch_type) {
        ret = cam_ops_action(mCameraId, FALSE, MM_CAMERA_OPS_VIDEO, 0);
        ret = cam_config_unprepare_buf(mCameraId, MM_CAMERA_CH_VIDEO);
        if(ret != MM_CAMERA_OK) {
            LOGE("%s:Unreg Stream buf err=%d\n", __func__, ret);
        }
    }
    /*Clean Buffer*/
    if(frames){
        delete[] frames;
        frames = NULL;
    }
    if(offset){
        delete[] offset;
        offset = NULL;
    }
    if(mp) {
        delete[] mp;
        mp = NULL;
    }

    if (MM_CAMERA_OK != ret) {
        LOGE ("%s ERROR: streaming Stop err=%d\n", __func__, ret);
    }
    return ret;
}

}; // namespace android
