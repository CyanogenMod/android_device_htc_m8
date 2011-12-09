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
#include <gralloc_priv.h>
#include <genlock.h>

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

status_t QCameraStream_preview::setPreviewWindow(preview_stream_ops_t* window)
{
    status_t retVal = NO_ERROR;
    LOGE(" %s: E ", __FUNCTION__);
    if( window == NULL) {
        LOGW(" Setting NULL preview window ");
        /* TODO: Current preview window will be invalidated.
         * Release all the buffers back */
       // relinquishBuffers();
    }
    mDisplayLock.lock();
    mPreviewWindow = window;
    mDisplayLock.unlock();
    LOGV(" %s : X ", __FUNCTION__ );
    return retVal;
}

status_t QCameraStream_preview::getBufferFromSurface() {
    int err = 0;
    int numMinUndequeuedBufs = 0;
	int format = 0;
	status_t ret = NO_ERROR;

    LOGI(" %s : E ", __FUNCTION__);

    if( mPreviewWindow == NULL) {
		LOGE("%s: mPreviewWindow = NULL", __func__);
        return INVALID_OPERATION;
	}
    cam_ctrl_dimension_t dim;

	//mDisplayLock.lock();
    cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);

	format = HAL_PIXEL_FORMAT_YCrCb_420_SP;
	if(ret != NO_ERROR) {
        LOGE("%s: display format %d is not supported", __func__, dim.prev_format);
		goto end;
	}
	numMinUndequeuedBufs = 0;
	if(mPreviewWindow->get_min_undequeued_buffer_count) {
    err = mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow, &numMinUndequeuedBufs);
		if (err != 0) {
			 LOGE("get_min_undequeued_buffer_count  failed: %s (%d)",
						strerror(-err), -err);
			 ret = UNKNOWN_ERROR;
			 goto end;
		}
	}
    mHalCamCtrl->mPreviewMemoryLock.lock();
    mHalCamCtrl->mPreviewMemory.buffer_count = kPreviewBufferCount + numMinUndequeuedBufs;// + 1;
    err = mPreviewWindow->set_buffer_count(mPreviewWindow, mHalCamCtrl->mPreviewMemory.buffer_count + 1);
    if (err != 0) {
         LOGE("set_buffer_count failed: %s (%d)",
                    strerror(-err), -err);
         ret = UNKNOWN_ERROR;
		 goto end;
    }
    err = mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                dim.display_width, dim.display_height, format);
    if (err != 0) {
         LOGE("set_buffers_geometry failed: %s (%d)",
                    strerror(-err), -err);
         ret = UNKNOWN_ERROR;
		 goto end;
    }
    err = mPreviewWindow->set_usage(mPreviewWindow, GRALLOC_USAGE_PRIVATE_ADSP_HEAP | GRALLOC_USAGE_PRIVATE_UNCACHED);
	if(err != 0) {
        /* set_usage error out */
		LOGE("%s: set_usage rc = %d", __func__, err);
		ret = UNKNOWN_ERROR;
		goto end;
	}
	for (int cnt = 0; cnt < mHalCamCtrl->mPreviewMemory.buffer_count + 1; cnt++) {
		int stride;
		err = mPreviewWindow->dequeue_buffer(mPreviewWindow,
										&mHalCamCtrl->mPreviewMemory.buffer_handle[cnt],
										&mHalCamCtrl->mPreviewMemory.stride[cnt]);
		if(!err) {
           err = mPreviewWindow->lock_buffer(this->mPreviewWindow,
                     mHalCamCtrl->mPreviewMemory.buffer_handle[cnt]);

            // lock the buffer using genlock
            LOGD("%s: camera call genlock_lock", __FUNCTION__);
            if (GENLOCK_NO_ERROR != genlock_lock_buffer((native_handle_t *)(*mHalCamCtrl->mPreviewMemory.buffer_handle[cnt]),
                                                      GENLOCK_WRITE_LOCK, GENLOCK_MAX_TIMEOUT)) {
                LOGE("%s: genlock_lock_buffer(WRITE) failed", __FUNCTION__);
            }
		   mHalCamCtrl->mPreviewMemory.local_flag[cnt] = 1;
		} else
			LOGE("%s: dequeue_buffer idx = %d err = %d", __func__, cnt, err);

		LOGE("%s: dequeue buf: %u\n", __func__, mHalCamCtrl->mPreviewMemory.buffer_handle[cnt]);

		if(err != 0) {
            LOGE("%s: dequeue_buffer failed: %s (%d)", __func__,
                    strerror(-err), -err);
            ret = UNKNOWN_ERROR;
			for(int i = 0; i < cnt; i++) {
                        LOGD("%s: camera call genlock_unlock", __FUNCTION__);
                        if (GENLOCK_FAILURE == genlock_unlock_buffer((native_handle_t *)(*(mHalCamCtrl->mPreviewMemory.buffer_handle[i])))) {
                                LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
                        }
		        err = mPreviewWindow->cancel_buffer(mPreviewWindow,
										mHalCamCtrl->mPreviewMemory.buffer_handle[i]);
				mHalCamCtrl->mPreviewMemory.buffer_handle[i] = NULL;
				mHalCamCtrl->mPreviewMemory.local_flag[i] = 0;
			}
			goto end;
		}
		mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt] =
		    (struct private_handle_t *)(*mHalCamCtrl->mPreviewMemory.buffer_handle[cnt]);
		mHalCamCtrl->mPreviewMemory.camera_memory[cnt] =
		    mHalCamCtrl->mGetMemory(mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt]->fd,
			mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt]->size, 1, (void *)this);
		LOGE("%s: idx = %d, fd = %d, size = %d, offset = %d", __func__,
            cnt, mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt]->fd,
			mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt]->size,
			mHalCamCtrl->mPreviewMemory.private_buffer_handle[cnt]->offset);
	}


	memset(&mHalCamCtrl->mMetadata, 0, sizeof(mHalCamCtrl->mMetadata));
	memset(mHalCamCtrl->mFace, 0, sizeof(mHalCamCtrl->mFace));

    LOGI(" %s : X ",__FUNCTION__);
end:
	//mDisplayLock.unlock();
	mHalCamCtrl->mPreviewMemoryLock.unlock();

    return NO_ERROR;
}

status_t QCameraStream_preview::putBufferToSurface() {
    int err = 0;
	status_t ret = NO_ERROR;

    LOGI(" %s : E ", __FUNCTION__);

    //mDisplayLock.lock();
    mHalCamCtrl->mPreviewMemoryLock.lock();
	for (int cnt = 0; cnt < mHalCamCtrl->mPreviewMemory.buffer_count + 1; cnt++) {
        mHalCamCtrl->mPreviewMemory.camera_memory[cnt]->release(mHalCamCtrl->mPreviewMemory.camera_memory[cnt]);
        LOGD("%s: camera call genlock_unlock", __FUNCTION__);
	    if (GENLOCK_FAILURE == genlock_unlock_buffer((native_handle_t *)(*(mHalCamCtrl->mPreviewMemory.buffer_handle[cnt])))) {
            LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
        }
        err = mPreviewWindow->cancel_buffer(mPreviewWindow, mHalCamCtrl->mPreviewMemory.buffer_handle[cnt]);
		LOGE(" put buffer %d successfully", cnt);
	}
	memset(&mHalCamCtrl->mPreviewMemory, 0, sizeof(mHalCamCtrl->mPreviewMemory));
	mHalCamCtrl->mPreviewMemoryLock.unlock();
	//mDisplayLock.unlock();
    LOGI(" %s : X ",__FUNCTION__);
    return NO_ERROR;
}

void QCameraStream_preview::notifyROIEvent(fd_roi_t roi)
{
    int faces_detected = roi.rect_num;
    if(faces_detected > MAX_ROI)
      faces_detected = MAX_ROI;
    LOGI("%s, width = %d height = %d", __func__,
       mHalCamCtrl->mDimension.display_width,
       mHalCamCtrl->mDimension.display_height);
    mDisplayLock.lock();
    for (int i = 0; i < faces_detected; i++) {
       mHalCamCtrl->mFace[i].rect[0] =
           roi.faces[i].x*2000/mHalCamCtrl->mDimension.display_width - 1000;
       mHalCamCtrl->mFace[i].rect[1] =
           roi.faces[i].y*2000/mHalCamCtrl->mDimension.display_height - 1000;
       mHalCamCtrl->mFace[i].rect[2] =
           roi.faces[i].dx*2000/mHalCamCtrl->mDimension.display_width;
       mHalCamCtrl->mFace[i].rect[3] =
           roi.faces[i].dy*2000/mHalCamCtrl->mDimension.display_height;
    }
    mHalCamCtrl->mMetadata.number_of_faces = faces_detected;
    mHalCamCtrl->mMetadata.faces = mHalCamCtrl->mFace;
    mDisplayLock.unlock();
}

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
	memset(&mHalCamCtrl->mMetadata, 0, sizeof(camera_frame_metadata_t));
	mHalCamCtrl->mPreviewMemoryLock.lock();
	memset(&mHalCamCtrl->mPreviewMemory, 0, sizeof(mHalCamCtrl->mPreviewMemory));
	mHalCamCtrl->mPreviewMemoryLock.unlock();
	memset(&mNotifyBuffer, 0, sizeof(mNotifyBuffer));

  /* get preview size, by qury mm_camera*/
    memset(&dim, 0, sizeof(cam_ctrl_dimension_t));

    memset(&(this->mDisplayStreamBuf),0, sizeof(this->mDisplayStreamBuf));

    ret = cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION, &dim);
    if (MM_CAMERA_OK != ret) {
      LOGE("%s: error - can't get camera dimension!", __func__);
      LOGE("%s: X", __func__);
      return BAD_VALUE;
    }else {
      width =  dim.display_width,
      height = dim.display_height;
    }

 ret = getBufferFromSurface();
 if(ret != NO_ERROR) {
     LOGE("%s: cannot get memory from surface texture client, ret = %d", __func__, ret);
     return ret;
 }

  /* set 4 buffers for display */
  memset(&mDisplayStreamBuf, 0, sizeof(mDisplayStreamBuf));
  mHalCamCtrl->mPreviewMemoryLock.lock();
  this->mDisplayStreamBuf.num = mHalCamCtrl->mPreviewMemory.buffer_count;
  this->myMode=myMode; /*Need to assign this in constructor after translating from mask*/
  frame_len = mm_camera_get_msm_frame_len(CAMERA_YUV_420_NV21, this->myMode,
                                          width, height, OUTPUT_TYPE_P,
                                          &num_planes, planes);
  this->mDisplayStreamBuf.frame_len = frame_len;

  mDisplayBuf.preview.buf.mp = new mm_camera_mp_buf_t[mDisplayStreamBuf.num];
  if (!mDisplayBuf.preview.buf.mp) {
	  LOGE("%s Error allocating memory for mplanar struct ", __func__);
  }
  memset(mDisplayBuf.preview.buf.mp, 0,
    mDisplayStreamBuf.num * sizeof(mm_camera_mp_buf_t));

  /*allocate memory for the buffers*/
  void *vaddr = NULL;
  for(int i = 0; i < mDisplayStreamBuf.num; i++){
	  if (mHalCamCtrl->mPreviewMemory.private_buffer_handle[i] == NULL)
		  continue;
      mDisplayStreamBuf.frame[i].fd = mHalCamCtrl->mPreviewMemory.private_buffer_handle[i]->fd;
      mDisplayStreamBuf.frame[i].cbcr_off = planes[0];
      mDisplayStreamBuf.frame[i].y_off = 0;
      mDisplayStreamBuf.frame[i].path = OUTPUT_TYPE_P;
	  mHalCamCtrl->mPreviewMemory.addr_offset[i] =
	      mHalCamCtrl->mPreviewMemory.private_buffer_handle[i]->offset;
      mDisplayStreamBuf.frame[i].buffer =
          (long unsigned int)mHalCamCtrl->mPreviewMemory.camera_memory[i]->data;

	  LOGE("%s: idx = %d, fd = %d, size = %d, cbcr_offset = %d, y_offset = %d, offset = %d, vaddr = 0x%x",
		  __func__, i,
		  mDisplayStreamBuf.frame[i].fd,
		  mHalCamCtrl->mPreviewMemory.private_buffer_handle[i]->size,
		  mDisplayStreamBuf.frame[i].cbcr_off,
		  mDisplayStreamBuf.frame[i].y_off,
		  mHalCamCtrl->mPreviewMemory.addr_offset[i],
		  (uint32_t)mDisplayStreamBuf.frame[i].buffer);


        mDisplayBuf.preview.buf.mp[i].frame = mDisplayStreamBuf.frame[i];
        mDisplayBuf.preview.buf.mp[i].frame_offset = mHalCamCtrl->mPreviewMemory.addr_offset[i];
        mDisplayBuf.preview.buf.mp[i].num_planes = num_planes;

		/* Plane 0 needs to be set seperately. Set other planes
         * in a loop. */
        mDisplayBuf.preview.buf.mp[i].planes[0].length = planes[0];
        mDisplayBuf.preview.buf.mp[i].planes[0].m.userptr = mDisplayStreamBuf.frame[i].fd;
        mDisplayBuf.preview.buf.mp[i].planes[0].data_offset = 0;
        mDisplayBuf.preview.buf.mp[i].planes[0].reserved[0] =
          mDisplayBuf.preview.buf.mp[i].frame_offset;
        for (int j = 1; j < num_planes; j++) {
          mDisplayBuf.preview.buf.mp[i].planes[j].length = planes[j];
          mDisplayBuf.preview.buf.mp[i].planes[j].m.userptr =
            mDisplayStreamBuf.frame[i].fd;
		  mDisplayBuf.preview.buf.mp[i].planes[j].data_offset = 0;
          mDisplayBuf.preview.buf.mp[i].planes[j].reserved[0] =
            mDisplayBuf.preview.buf.mp[i].planes[j-1].reserved[0] +
            mDisplayBuf.preview.buf.mp[i].planes[j-1].length;
        }

		for (int j = 0; j < num_planes; j++) {
			LOGE("Planes: %d length: %d userptr: %d offset: %d\n",
				 j, mDisplayBuf.preview.buf.mp[i].planes[j].length,
				 mDisplayBuf.preview.buf.mp[i].planes[j].m.userptr,
				 mDisplayBuf.preview.buf.mp[i].planes[j].reserved[0]);
		}

  }/*end of for loop*/

 /* register the streaming buffers for the channel*/
  mDisplayBuf.ch_type = MM_CAMERA_CH_PREVIEW;
  mDisplayBuf.preview.num = mDisplayStreamBuf.num;
  mHalCamCtrl->mPreviewMemoryLock.unlock();
  LOGE("%s:END",__func__);
  return NO_ERROR;

end:
  if (MM_CAMERA_OK == ret ) {
    LOGV("%s: X - NO_ERROR ", __func__);
    return NO_ERROR;
  }

    LOGV("%s: out of memory clean up", __func__);
  /* release the allocated memory */

  LOGV("%s: X - BAD_VALUE ", __func__);
  return BAD_VALUE;
}

void QCameraStream_preview::dumpFrameToFile(struct msm_frame* newFrame)
{
  int32_t enabled = 0;
  int frm_num;
  uint32_t  skip_mode;
  char value[PROPERTY_VALUE_MAX];
  char buf[32];
  int w, h;
  static int count = 0;
  cam_ctrl_dimension_t dim;
  int file_fd;
  int rc = 0;
  int len;
  unsigned long addr;
  unsigned long * tmp = (unsigned long *)newFrame->buffer;
  addr = *tmp;
//#if 0 // mzhu
//    addr = mmap(NULL,
//    mPreviewBufferHandlePtr[0]->size,
//    PROT_READ  | PROT_WRITE,
//    MAP_SHARED,
 //   newFrame->fd,
//    0);
//#endif // mzhu
    status_t ret = cam_config_get_parm(mHalCamCtrl->mCameraId,
	                 MM_CAMERA_PARM_DIMENSION, &dim);

	w = dim.display_width;
	h = dim.display_height;
	len = (w * h)*3/2;
	count++;
	if(count < 100) {
		snprintf(buf, sizeof(buf), "/data/mzhu%d.yuv", count);
		file_fd = open(buf, O_RDWR | O_CREAT, 0777);

		rc = write(file_fd, (const void *)addr, len);
		LOGE("%s: file='%s', vaddr_old=0x%x, addr_map = 0x%p, len = %d, rc = %d",
			__func__, buf, (uint32_t)newFrame->buffer, (void *)addr, len, rc);
		close(file_fd);
		//munmap(addr, mPreviewBufferHandlePtr[0]->size);
		LOGE("%s: dump %s, rc = %d, len = %d", __func__, buf, rc, len);
	}
}

status_t QCameraStream_preview::processPreviewFrame(mm_camera_ch_data_buf_t *frame)
{
  LOGV("%s",__func__);
  int err = 0;
  int msgType = 0;
  camera_memory_t *data = NULL;
  camera_frame_metadata_t *metadata = NULL;

  if(mHalCamCtrl==NULL) {
    LOGE("%s: X: HAL control object not set",__func__);
    /*Call buf done*/
    return BAD_VALUE;
  }

  if (UNLIKELY(mHalCamCtrl->mDebugFps)) {
      mHalCamCtrl->debugShowPreviewFPS();
  }
  //dumpFrameToFile(frame->def.frame);
  //mHalCamCtrl->dumpFrameToFile(frame->def.frame, HAL_DUMP_FRM_PREVIEW);

  mHalCamCtrl->mPreviewMemoryLock.lock();
  mNotifyBuffer[frame->def.idx] = *frame;
  // mzhu fix me, need to check meta data also.

  LOGI("Enqueue buf handle %u\n",
	   mHalCamCtrl->mPreviewMemory.buffer_handle[frame->def.idx]);
  LOGD("%s: camera call genlock_unlock", __FUNCTION__);
  if (GENLOCK_FAILURE == genlock_unlock_buffer((native_handle_t*)
	            (*mHalCamCtrl->mPreviewMemory.buffer_handle[frame->def.idx]))) {
       LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
  }
  err = this->mPreviewWindow->enqueue_buffer(this->mPreviewWindow,
		    (buffer_handle_t *)mHalCamCtrl->mPreviewMemory.buffer_handle[frame->def.idx]);
  if(err != 0) {
    LOGE("%s: enqueue_buffer failed, err = %d", __func__, err);
  }
  mHalCamCtrl->mPreviewMemory.local_flag[frame->def.idx] = 0;
  buffer_handle_t *buffer_handle = NULL;
  int tmp_stride = 0;
  err = this->mPreviewWindow->dequeue_buffer(this->mPreviewWindow,
	              &buffer_handle, &tmp_stride);
  if (err == NO_ERROR && buffer_handle != NULL) {
      err = this->mPreviewWindow->lock_buffer(this->mPreviewWindow, buffer_handle);
      LOGD("%s: camera call genlock_lock", __FUNCTION__);
      if (GENLOCK_FAILURE == genlock_lock_buffer((native_handle_t*)(*buffer_handle), GENLOCK_WRITE_LOCK,
                                                 GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(WRITE) failed", __FUNCTION__);
      }
      for(int i = 0; i < mHalCamCtrl->mPreviewMemory.buffer_count; i++) {
		  LOGE("h1: %u h2: %u\n", mHalCamCtrl->mPreviewMemory.buffer_handle[i], buffer_handle);
		  if(mHalCamCtrl->mPreviewMemory.buffer_handle[i] == buffer_handle) {
	          mm_camera_ch_data_buf_t tmp_frame;
              if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, &mNotifyBuffer[i])) {
                  LOGE("BUF DONE FAILED");
				  mDisplayLock.unlock();
                  return BAD_VALUE;
              }
			   mHalCamCtrl->mPreviewMemory.local_flag[i] = 1;
			  break;
		  }
	  }
  } else
	  LOGE("%s: error in dequeue_buffer, enqueue_buffer idx = %d, no free buffer now", __func__, frame->def.idx);
  /* Save the last displayed frame. We'll be using it to fill the gap between
     when preview stops and postview start during snapshot.*/
  mLastQueuedFrame = &(mDisplayStreamBuf.frame[frame->def.idx]);
  mHalCamCtrl->mPreviewMemoryLock.unlock();

  mHalCamCtrl->mCallbackLock.lock();
  camera_data_callback pcb = mHalCamCtrl->mDataCb;
  mHalCamCtrl->mCallbackLock.unlock();
  LOGI("Message enabled = 0x%x", mHalCamCtrl->mMsgEnabled);

  if (pcb != NULL) {
      LOGE("prepare buffer for face detection");
      if(mHalCamCtrl->mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
      LOGE("in preview case ");
          msgType |=  CAMERA_MSG_PREVIEW_FRAME;
          data = mHalCamCtrl->mPreviewMemory.camera_memory[frame->def.idx];//mPreviewHeap->mBuffers[frame->def.idx];
      } else {
          data = NULL;
      }
      if(mHalCamCtrl->mFaceDetectOn) {
          msgType  |= CAMERA_MSG_PREVIEW_METADATA;
          metadata = &mHalCamCtrl->mMetadata;
      }
      LOGE("Buffer Callback to Service FD = %d msgType = 0x%x", mHalCamCtrl->mFaceDetectOn, msgType);
      pcb(msgType, data, 0, metadata, mHalCamCtrl->mCallbackCookie);
      LOGE("end of cb");
  }

  /* Save the last displayed frame. We'll be using it to fill the gap between
     when preview stops and postview start during snapshot.*/
  //mLastQueuedFrame = frame->def.frame;
/*
  if(MM_CAMERA_OK != cam_evt_buf_done(mCameraId, frame))
  {
      LOGE("BUF DONE FAILED");
      return BAD_VALUE;
  }
*/
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
    LOGE("%s: E", __func__);
    LOGE("%s: X", __func__);
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

  buffer_handle_t *buffer_handle = NULL;
  int tmp_stride = 0;
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

    //for(i=0;i<mDisplayStreamBuf.num;i++) {
    //  mm_camera_do_munmap(mDisplayStreamBuf.frame[i].fd,(void *)mDisplayStreamBuf.frame[i].buffer,mDisplayStreamBuf.frame_len);
    //}
	if (mDisplayBuf.preview.buf.mp != NULL) {
		delete[] mDisplayBuf.preview.buf.mp;
	}
	/*free camera_memory handles and return buffer back to surface*/
    putBufferToSurface();
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
