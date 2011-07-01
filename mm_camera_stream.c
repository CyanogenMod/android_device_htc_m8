/*
Copyright (c) 2011, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of Code Aurora Forum, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <pthread.h>
#include "mm_camera_dbg.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include "mm_camera_interface2.h"
#include "mm_camera.h"

static void mm_camera_stream_util_set_state(mm_camera_stream_t *stream,
																		  mm_camera_stream_state_type_t state);

int mm_camera_stream_init_q(mm_camera_frame_queue_t *q)
{
	pthread_mutex_init(&q->mutex, NULL);
	return MM_CAMERA_OK;
}
int mm_camera_stream_deinit_q(mm_camera_frame_queue_t *q)
{
	pthread_mutex_destroy(&q->mutex);
	return MM_CAMERA_OK;
}
int mm_camera_stream_frame_get_q_cnt(mm_camera_frame_queue_t *q)
{
	int cnt;
	pthread_mutex_lock(&q->mutex);
	cnt = q->cnt;
	pthread_mutex_unlock(&q->mutex);
	return cnt;
}
mm_camera_frame_t *mm_camera_stream_frame_deq(mm_camera_frame_queue_t *q)
{
	mm_camera_frame_t *tmp;

	pthread_mutex_lock(&q->mutex);
	tmp = q->head;

	if(tmp == NULL) goto end;
	if(q->head == q->tail) {
		q->head = NULL;
		q->tail = NULL;
	} else {
		q->head = tmp->next;
	}
	tmp->next = NULL;
	q->cnt--;
end:
	pthread_mutex_unlock(&q->mutex);
	return tmp;
}
void mm_camera_stream_frame_enq(mm_camera_frame_queue_t *q, mm_camera_frame_t *node)
{
	pthread_mutex_lock(&q->mutex);
	node->next = NULL;
	if(q->head == NULL) {
		q->head = node;
		q->tail = node;
	} else {
		q->tail->next = node;
		q->tail = node;
	}
	q->cnt++;
	pthread_mutex_unlock(&q->mutex);
}
void mm_stream_frame_flash_q(mm_camera_frame_queue_t *q)
{
	pthread_mutex_lock(&q->mutex);
	q->cnt = 0;
	q->head = NULL;
	q->tail = NULL;
	pthread_mutex_unlock(&q->mutex);
}
void mm_camera_stream_frame_refill_q(mm_camera_frame_queue_t *q, mm_camera_frame_t *node, int num)
{
	int i;

	mm_stream_frame_flash_q(q);
	for(i = 0; i < num; i++) 
		mm_camera_stream_frame_enq(q, &node[i]);
	CDBG("%s: q=0x%x, num = %d, q->cnt=%d\n",
			 __func__,(uint32_t)q,num, mm_camera_stream_frame_get_q_cnt(q));
}
void mm_camera_stream_deinit_frame(mm_camera_stream_frame_t *frame)
{
	pthread_mutex_destroy(&frame->mutex);
	mm_camera_stream_deinit_q(&frame->readyq);
	mm_camera_stream_deinit_q(&frame->freeq);
	memset(frame, 0, sizeof(mm_camera_stream_frame_t));
}
void mm_camera_stream_init_frame(mm_camera_stream_frame_t *frame)
{
	memset(frame, 0, sizeof(mm_camera_stream_frame_t));
	pthread_mutex_init(&frame->mutex, NULL);
	mm_camera_stream_init_q(&frame->readyq);
	mm_camera_stream_init_q(&frame->freeq);
}
void mm_camera_stream_release(mm_camera_stream_t *stream)
{
	mm_camera_stream_deinit_frame(&stream->frame);
	if(stream->fd > 0) close(stream->fd);
	memset(stream, 0, sizeof(*stream));
	//stream->fd = -1;
	mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_NOTUSED);
}
int mm_camera_stream_is_active(mm_camera_stream_t *stream)
{
	return (stream->state == MM_CAMERA_STREAM_STATE_ACTIVE)? TRUE : FALSE;
}
static void mm_camera_stream_util_set_state(mm_camera_stream_t *stream,
																		  mm_camera_stream_state_type_t state)
{
	CDBG("%s:stream fd=%d, stream type=%d, cur_state=%d,new_state=%d\n", 
			 __func__, stream->fd, stream->stream_type, stream->state, state);
	stream->state = state;
}
int mm_camera_read_msm_frame(mm_camera_obj_t * my_obj, 
						mm_camera_stream_t *stream)
{
	int idx = -1, rc = MM_CAMERA_OK;
	struct v4l2_buffer vb;


	memset(&vb,  0,  sizeof(vb));
	vb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vb.memory = V4L2_MEMORY_USERPTR;
	CDBG("%s: VIDIOC_DQBUF ioctl call\n", __func__);
	rc = ioctl(stream->fd, VIDIOC_DQBUF, &vb);
	if (rc < 0)
		return idx;
	idx = vb.index;
	stream->frame.frame[idx].frame.frame_id = vb.sequence;
  stream->frame.frame[idx].frame.ts.tv_sec  = vb.timestamp.tv_sec;
  stream->frame.frame[idx].frame.ts.tv_nsec = vb.timestamp.tv_usec * 1000;
	return idx;
}

int32_t mm_camera_util_s_ctrl( int32_t fd,	uint32_t id, int32_t value)
{
	int rc = MM_CAMERA_OK;
  struct v4l2_control control;

	memset(&control, 0, sizeof(control));
	control.id = id;
	control.value = value;
	rc = ioctl (fd, VIDIOC_S_CTRL, &control);
	if(rc) {
		CDBG("%s: fd=%d, S_CTRL, id=0x%x, value = 0x%x, rc = %d\n", 
				 __func__, fd, id, (uint32_t)value, rc);
		rc = MM_CAMERA_E_GENERAL;
	}
	return rc;
}

int32_t mm_camera_util_g_ctrl( int32_t fd, uint32_t id, int32_t *value)
{
	int rc = MM_CAMERA_OK;
  struct v4l2_control control;

	memset(&control, 0, sizeof(control));
	control.id = id;
	control.value = (int32_t)value;
	rc = ioctl (fd, VIDIOC_G_CTRL, &control);
	if(rc) {
		CDBG("%s: fd=%d, G_CTRL, id=0x%x, rc = %d\n", __func__, fd, id, rc);
		rc = MM_CAMERA_E_GENERAL;
	}
	*value = control.value;
	return rc;
}

static uint32_t mm_camera_util_get_v4l2_fmt(cam_format_t fmt)
{
	uint32_t val;
	switch(fmt) {
	case CAMERA_YUV_420_NV12:
		val = V4L2_PIX_FMT_NV12;
		break;
	case CAMERA_YUV_420_NV21:
		val = V4L2_PIX_FMT_NV21;
		break;
	case CAMERA_BAYER_SBGGR10:
		val= V4L2_PIX_FMT_SBGGR10;
		break;
	default:
		val = 0;
		break;
	}
	return val;
}

static int mm_camera_stream_util_set_ext_mode(mm_camera_stream_t *stream)
{
    int rc = 0;
    struct v4l2_streamparm s_parm;
    s_parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		switch(stream->stream_type) {
		case MM_CAMERA_STREAM_PREVIEW:
			s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_PREVIEW;
			break;
		case MM_CAMERA_STREAM_SNAPSHOT:
			s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_MAIN;
			break;
		case MM_CAMERA_STREAM_THUMBNAIL:
			s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_THUMBNAIL;
			break;
		case MM_CAMERA_STREAM_VIDEO:
			s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_VIDEO;
			break;
		case MM_CAMERA_STREAM_RAW:
				s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_MAIN; //MSM_V4L2_EXT_CAPTURE_MODE_RAW;
				break;
		case MM_CAMERA_STREAM_VIDEO_MAIN:
			//s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_VIDEO_MAIN;
			//break;
		case MM_CAMERA_STREAM_ZSL_MAIN:
			//s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_ZSL_MAIN;
			//break;
		case MM_CAMERA_STREAM_ZSL_POST_VIEW:
		//	s_parm.parm.capture.extendedmode = MSM_V4L2_EXT_CAPTURE_MODE_ZSL_POST;
		//	break;
		default: 
			return 0;
		}

    rc = ioctl(stream->fd, VIDIOC_S_PARM, &s_parm);
		CDBG("%s:stream fd=%d,type=%d,rc=%d,extended_mode=%d\n", 
				 __func__, stream->fd, stream->stream_type, rc,
				 s_parm.parm.capture.extendedmode);
    return rc;
}

static int mm_camera_util_set_op_mode(int fd, int opmode)
{
    int rc = 0;
    struct v4l2_control s_ctrl;
    s_ctrl.id = MSM_V4L2_PID_CAM_MODE;
    s_ctrl.value = opmode; 

		rc = ioctl(fd, VIDIOC_S_CTRL, &s_ctrl);
    if (rc < 0)
        CDBG("%s: VIDIOC_S_CTRL failed, rc=%d\n", 
						 __func__, rc);
    return rc;
}

int mm_camera_stream_qbuf(mm_camera_obj_t * my_obj, 
															mm_camera_stream_t *stream, 
															int idx)
{
	int32_t i, rc = MM_CAMERA_OK;
	int *ret;
	struct v4l2_buffer buffer;

	memset(&buffer, 0, sizeof(buffer));
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory = V4L2_MEMORY_USERPTR;
	buffer.index = idx;
	buffer.m.userptr = stream->frame.frame[idx].frame.fd;
	buffer.reserved = stream->frame.frame_offset[idx];
	buffer.length = stream->frame.frame_len;
	CDBG("%s:fd=%d,type=%d,frame idx=%d,userptr=%lu,reserved=%d,len=%d\n",
				 __func__, stream->fd, stream->stream_type,
				 idx, buffer.m.userptr, buffer.reserved, buffer.length);
	rc = ioctl(stream->fd, VIDIOC_QBUF, &buffer);
	if (rc < 0) {
		CDBG("%s: VIDIOC_QBUF error = %d\n", __func__, rc);
		return rc;
	}
	return rc;
}
static int mm_camera_stream_util_reg_buf(mm_camera_obj_t * my_obj, 
															mm_camera_stream_t *stream, 
															mm_camera_buf_def_t *vbuf)
{
	int32_t i, rc = MM_CAMERA_OK;
	int *ret;
	int num_qduf = 3;
	uint32_t video_buff_size;
  struct v4l2_requestbuffers bufreq;
	uint32_t y_off = 0;
	uint32_t cbcr_off = 0;
	mm_camera_pad_type_t cbcr_Pad;

	if(vbuf->num > MM_CAMERA_MAX_NUM_FRAMES) {
		rc = -MM_CAMERA_E_GENERAL;
		CDBG("%s: buf num %d > max limit %d\n", 
				 __func__, vbuf->num, MM_CAMERA_MAX_NUM_FRAMES);
		goto end;
	}
	if(stream->stream_type != MM_CAMERA_STREAM_VIDEO) 
		cbcr_Pad = MM_CAMERA_PAD_WORD;
	else 
		cbcr_Pad = MM_CAMERA_PAD_2K;
	video_buff_size = 
	stream->frame.frame_len = mm_camera_get_msm_frame_len(stream->cam_fmt, 
																	my_obj->current_mode, 
																	stream->fmt.fmt.pix.width, 
																	stream->fmt.fmt.pix.height, 
																	&y_off, &cbcr_off, cbcr_Pad);
	if(stream->frame.frame_len == 0) {
		CDBG("%s:incorrect frame size = %d\n", __func__, video_buff_size);
		rc = -1;
		goto end;
	}
	stream->frame.num_frame = vbuf->num;
  bufreq.count = stream->frame.num_frame;
  bufreq.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  bufreq.memory = V4L2_MEMORY_USERPTR;
	CDBG("%s: calling VIDIOC_REQBUFS - fd=%d, num_buf=%d, type=%d, memory=%d\n", 
			 __func__,stream->fd, bufreq.count, bufreq.type, bufreq.memory); 
  rc = ioctl(stream->fd, VIDIOC_REQBUFS, &bufreq);
  if (rc < 0) {
    CDBG("%s: fd=%d, ioctl VIDIOC_REQBUFS failed: rc=%d\n",
      __func__, stream->fd, rc);
    goto end;
  }
	CDBG("%s: stream fd=%d, ioctl VIDIOC_REQBUFS: memtype = %d, num_frames = %d, rc=%d\n",
		__func__, stream->fd, bufreq.memory, bufreq.count, rc);

	for(i = 0; i < vbuf->num; i++){
		memcpy(&stream->frame.frame[i].frame, &vbuf->frame[i], 
					 sizeof(vbuf->frame[i]));
		stream->frame.frame[i].idx = i;
		if(vbuf->frame_offset) {
			stream->frame.frame_offset[i] = vbuf->frame_offset[i];
		} else {
			stream->frame.frame_offset[i] = 0;
		}
		CDBG("%s: stream_fd = %d, frame_fd = %d, frame ID = %d, offset = %d\n", 
				 __func__, stream->fd, stream->frame.frame[i].frame.fd, 
				 i, stream->frame.frame_offset[i]);
		mm_camera_stream_frame_enq(&stream->frame.freeq, &stream->frame.frame[i]);
	}
	/* only push max num_qduf frames to kernel. */
	for(i = 0; i < num_qduf; i++) {
		mm_camera_frame_t *frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
		if(frame == NULL) break;
		rc = mm_camera_stream_qbuf(my_obj, stream, frame->idx);
		if (rc < 0) {
			CDBG("%s: VIDIOC_QBUF rc = %d\n", __func__, rc);
			goto end;
		}
	}
	stream->frame.qbuf = 1;
end:
	return rc;
}
static int mm_camera_stream_util_unreg_buf(mm_camera_obj_t * my_obj, 
															mm_camera_stream_t *stream)
{
  struct v4l2_requestbuffers bufreq;
	int32_t i, rc = MM_CAMERA_OK;

  bufreq.count = 0;
  bufreq.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  bufreq.memory = V4L2_MEMORY_USERPTR;
  rc = ioctl(stream->fd, VIDIOC_REQBUFS, &bufreq);
  if (rc < 0) {
    CDBG("%s: fd=%d, VIDIOC_REQBUFS failed, rc=%d\n",
      __func__, stream->fd, rc);
    return rc;
  }
	mm_stream_frame_flash_q(&stream->frame.readyq);
	mm_stream_frame_flash_q(&stream->frame.freeq);
	//mm_camera_stream_deinit_frame(&stream->frame);
	stream->frame.qbuf = 0;
	CDBG("%s:fd=%d,type=%d,rc=%d\n",
			 __func__, stream->fd, stream->stream_type, rc);
	return rc;
}

static int32_t mm_camera_stream_fsm_notused(mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = 0;
	char dev_name[MM_CAMERA_DEV_NAME_LEN];
	
	switch(evt) {
	case MM_CAMERA_STATE_EVT_ACQUIRE: 
		sprintf(dev_name, "/dev/%s", mm_camera_util_get_dev_name(my_obj));
		CDBG("%s: open dev '%s', stream type = %d\n",
				 __func__, dev_name, *((mm_camera_stream_type_t *)val)); 
		//stream->fd = -1;
		//rc = mm_camera_dev_open(&stream->fd, dev_name);
		stream->fd = open(dev_name,	O_RDWR | O_NONBLOCK);
		if(stream->fd <= 0){
			CDBG("%s: open dev returned %d\n", __func__, stream->fd);
			return -1;
		}
		stream->stream_type = *((mm_camera_stream_type_t *)val);
		rc = mm_camera_stream_util_set_ext_mode(stream);
		CDBG("%s: fd=%d, stream type=%d, mm_camera_stream_util_set_ext_mode() err=%d\n",
				 __func__, stream->fd, stream->stream_type, rc); 
		if(rc == MM_CAMERA_OK) {
			mm_camera_stream_init_frame(&stream->frame);
			mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_ACQUIRED);
		} else if(stream->fd > 0) {
			close(stream->fd);
			stream->fd = 0;
		}
		break;
	default:
		return -1;
	}
	return rc;
}

static int32_t mm_camera_stream_util_proc_fmt(mm_camera_obj_t *my_obj, 
	mm_camera_stream_t *stream,
	mm_camera_image_fmt_t *fmt)
{
	int32_t rc = MM_CAMERA_OK;

	if(fmt->dim.width == 0 || fmt->dim.height == 0) {
		rc = -MM_CAMERA_E_INVALID_INPUT;
		CDBG("%s:invalid input[w=%d,h=%d,fmt=%d]\n", 
				 __func__, fmt->dim.width, fmt->dim.height, fmt->fmt);
		goto end;
	}
  CDBG("%s: dw=%d,dh=%d,vw=%d,vh=%d,pw=%d,ph=%d,tw=%d,th=%d,raw_w=%d,raw_h=%d,fmt=%d\n",
       __func__,
       my_obj->dim.display_width,my_obj->dim.display_height,
       my_obj->dim.video_width,my_obj->dim.video_height,
       my_obj->dim.picture_width,my_obj->dim.picture_height,
       my_obj->dim.ui_thumbnail_width,my_obj->dim.ui_thumbnail_height,
       my_obj->dim.raw_picture_width,my_obj->dim.raw_picture_height,fmt->fmt);
	stream->cam_fmt = fmt->fmt;
	stream->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	stream->fmt.fmt.pix.width = fmt->dim.width;
	stream->fmt.fmt.pix.height= fmt->dim.height;
	stream->fmt.fmt.pix.field = V4L2_FIELD_NONE;
	stream->fmt.fmt.pix.priv = (uint32_t)&my_obj->dim; 
	stream->fmt.fmt.pix.pixelformat = 
	mm_camera_util_get_v4l2_fmt(stream->cam_fmt);
	rc = ioctl(stream->fd, VIDIOC_S_FMT, &stream->fmt);
	if (rc < 0) { 
		CDBG("%s: ioctl VIDIOC_S_FMT failed: rc=%d\n", __func__, rc);
		rc = -MM_CAMERA_E_GENERAL;
	}
end:
	CDBG("%s:fd=%d,type=%d,rc=%d\n", 
			 __func__, stream->fd, stream->stream_type, rc); 
	return rc;
}
static int32_t mm_camera_stream_fsm_acquired(mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = 0;

	switch(evt) {
	case MM_CAMERA_STATE_EVT_SET_FMT:
		rc = mm_camera_stream_util_proc_fmt(my_obj,stream,
					(mm_camera_image_fmt_t *)val);
		if(!rc) mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_CFG);
		break;
	case MM_CAMERA_STATE_EVT_RELEASE:
		mm_camera_stream_release(stream);
		break;
	default:
		return -1;
	}
	return rc;
}
static int32_t mm_camera_stream_fsm_cfg(mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = 0;
	switch(evt) {
	case MM_CAMERA_STATE_EVT_RELEASE:
		mm_camera_stream_release(stream);
		break;
	case MM_CAMERA_STATE_EVT_SET_FMT:
		rc = mm_camera_stream_util_proc_fmt(my_obj,stream,
					(mm_camera_image_fmt_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_REG_BUF:
		rc = mm_camera_stream_util_reg_buf(my_obj, stream, (mm_camera_buf_def_t *)val);
		if(!rc) mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_REG);
		break;
	default:
		return -1;
	}
	return rc;
}

static int32_t mm_camera_stream_util_buf_done(mm_camera_obj_t * my_obj,
																							 mm_camera_stream_t *stream, 
																							 mm_camera_notify_frame_t *frame)
{
	int32_t rc = MM_CAMERA_OK;

	stream->frame.ref_count[frame->idx]--;
	if(0 == stream->frame.ref_count[frame->idx]) {
		mm_camera_stream_frame_enq(&stream->frame.freeq, &stream->frame.frame[frame->idx]);
	}
	pthread_mutex_lock(&stream->frame.mutex);
	if(stream->frame.no_buf == 1) {
		mm_camera_frame_t *frame;
		frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
		if(frame) {
			rc = mm_camera_stream_qbuf(my_obj, stream, 
																	frame->idx);
			if(rc < 0) {
				CDBG("%s: mm_camera_stream_qbuf(idx=%d) err=%d\n", __func__, frame->idx, rc);
			} else stream->frame.no_buf = 0;
		} else
			stream->frame.no_buf = 1;
	}
	pthread_mutex_unlock(&stream->frame.mutex);
	return rc;
}

static int32_t mm_camera_stream_fsm_reg(mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = 0;
	switch(evt) {
	case MM_CAMERA_STATE_EVT_QBUF:
		rc = mm_camera_stream_util_buf_done(my_obj, stream, 
																				(mm_camera_notify_frame_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_RELEASE:
		mm_camera_stream_release(stream);
		break;
	case MM_CAMERA_STATE_EVT_UNREG_BUF:
		rc = mm_camera_stream_util_unreg_buf(my_obj, stream);
		if(!rc)
			mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_CFG);
		break;
	case MM_CAMERA_STATE_EVT_STREAM_ON:
		{
			enum v4l2_buf_type buf_type;
			int i = 0;
			mm_camera_frame_t *frame;
			if(stream->frame.qbuf == 0) {
			mm_camera_stream_frame_refill_q(&stream->frame.freeq, stream->frame.frame, stream->frame.num_frame);
			for(i = 0; i < 3; i++) {
				frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
				if(frame == NULL) break;
				rc = mm_camera_stream_qbuf(my_obj, stream, frame->idx);
				if (rc < 0) {
					CDBG("%s: ioctl VIDIOC_QBUF error = %d\n", __func__, rc);
					return rc;
				}
				stream->frame.ref_count[i] = 0;
			}
				stream->frame.qbuf = 1;
			}
			rc = mm_camera_poll_add_stream(my_obj, stream);
			buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			CDBG("%s: STREAMON,fd=%d,stream_type=%d\n",
					 __func__, stream->fd, stream->stream_type);
			rc = ioctl(stream->fd, VIDIOC_STREAMON, &buf_type);
			if (rc < 0) {
					CDBG("%s: ioctl VIDIOC_STREAMON failed: rc=%d\n", 
						__func__, rc);
					mm_camera_poll_del_stream(my_obj, stream);
			}
			else
				mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_ACTIVE);
		}
		break;
	default:
		return -1;
	}
	return rc;
}
static int32_t mm_camera_stream_fsm_active(mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = 0;
	switch(evt) {
	case MM_CAMERA_STATE_EVT_QBUF:
		rc = mm_camera_stream_util_buf_done(my_obj, stream, 
																				(mm_camera_notify_frame_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_RELEASE:
		mm_camera_stream_release(stream);
		break;
	case MM_CAMERA_STATE_EVT_STREAM_OFF:
		{
			enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			mm_camera_poll_del_stream(my_obj, stream);
			CDBG("%s: STREAMOFF,fd=%d,type=%d\n", 
				__func__, stream->fd, stream->stream_type);
			rc = ioctl(stream->fd, VIDIOC_STREAMOFF, &buf_type);
			if (rc < 0) {
					CDBG("%s: STREAMOFF failed: %s\n", 
						__func__, strerror(errno));
			}
			else {
				stream->frame.qbuf = 0;
				mm_camera_stream_util_set_state(stream, MM_CAMERA_STREAM_STATE_REG);
			}
		}
		break;
	default:
		return -1;
	}
	return rc;
}

typedef int32_t (*mm_camera_stream_fsm_fn_t) (mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val);

static mm_camera_stream_fsm_fn_t mm_camera_stream_fsm_fn[MM_CAMERA_STREAM_STATE_MAX] = {
	mm_camera_stream_fsm_notused,
	mm_camera_stream_fsm_acquired,
	mm_camera_stream_fsm_cfg,
	mm_camera_stream_fsm_reg,
	mm_camera_stream_fsm_active
};
int32_t mm_camera_stream_fsm_fn_vtbl (mm_camera_obj_t * my_obj, 
															 mm_camera_stream_t *stream, 
															mm_camera_state_evt_type_t evt, void *val)
{
	/*CDBG("%s: stream fd=%d, type = %d, state=%d\n", 
				 __func__, stream->fd, stream->stream_type, stream->state);*/
	return mm_camera_stream_fsm_fn[stream->state] (my_obj, stream, evt, val);
}

