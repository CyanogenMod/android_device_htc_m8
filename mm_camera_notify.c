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
#include "mm_camera_interface2.h"
#include "mm_camera.h"

static int mm_camera_qbuf_to_kernel(mm_camera_obj_t * my_obj, mm_camera_stream_t *stream)
{
	int rc = MM_CAMERA_OK;
	mm_camera_frame_t *frame;
	frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
	pthread_mutex_lock(&stream->frame.mutex);
	if(frame) {
		rc = mm_camera_stream_qbuf(my_obj, stream, 
																frame->idx);
		if(rc < 0) {
			CDBG("%s: mm_camera_stream_qbuf(idx=%d) err=%d\n", __func__, frame->idx, rc);
			} else stream->frame.no_buf = 0;
	} else {
		CDBG("%s:no free frame, fd=%d,type=%d\n", 
				 __func__, stream->fd, stream->stream_type);
		stream->frame.no_buf = 1;
	}
	pthread_mutex_unlock(&stream->frame.mutex);
	return rc;
}
static void mm_camera_read_raw_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	int idx;
	mm_camera_stream_t *stream;

	stream = &my_obj->ch[MM_CAMERA_CH_RAW].raw.stream;
	rc = mm_camera_qbuf_to_kernel(my_obj, stream);
	idx =  mm_camera_read_msm_frame(my_obj, stream);
	if (idx < 0) {
		return;
	}
	pthread_mutex_lock(&my_obj->ch[MM_CAMERA_CH_RAW].mutex);
	if(my_obj->ch[MM_CAMERA_CH_RAW].buf_cb.cb) {
		mm_camera_ch_data_buf_t data;
		data.type = MM_CAMERA_CH_RAW;
		data.def.idx = idx;
		data.def.frame = &my_obj->ch[MM_CAMERA_CH_RAW].raw.stream.frame.frame[idx].frame;
		my_obj->ch[MM_CAMERA_CH_RAW].raw.stream.frame.ref_count[idx]++;
		CDBG("%s:calling data notify cb 0x%x, 0x%x\n", __func__,
				 (uint32_t)my_obj->ch[MM_CAMERA_CH_RAW].buf_cb.cb,
				 (uint32_t)my_obj->ch[MM_CAMERA_CH_RAW].buf_cb.user_data);  
		my_obj->ch[MM_CAMERA_CH_RAW].buf_cb.cb(&data, 
								my_obj->ch[MM_CAMERA_CH_RAW].buf_cb.user_data);
	} 
	pthread_mutex_unlock(&my_obj->ch[MM_CAMERA_CH_RAW].mutex);
}

static void mm_camera_read_preview_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	int idx;
	mm_camera_stream_t *stream;

	stream = &my_obj->ch[MM_CAMERA_CH_PREVIEW].preview.stream;
	rc = mm_camera_qbuf_to_kernel(my_obj, stream);
	idx =  mm_camera_read_msm_frame(my_obj, stream);
	if (idx < 0) {
		return;
	}
	pthread_mutex_lock(&my_obj->ch[MM_CAMERA_CH_PREVIEW].mutex);
	if(my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.cb) {
		mm_camera_ch_data_buf_t data;
		data.type = MM_CAMERA_CH_PREVIEW;
		data.def.idx = idx;
		data.def.frame = &my_obj->ch[MM_CAMERA_CH_PREVIEW].preview.stream.frame.frame[idx].frame;
		my_obj->ch[MM_CAMERA_CH_PREVIEW].preview.stream.frame.ref_count[idx]++;
		CDBG("%s:calling data notify cb 0x%x, 0x%x\n", __func__,
				 (uint32_t)my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.cb,
				 (uint32_t)my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.user_data);  
		my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.cb(&data, 
								my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.user_data);
	} 
	pthread_mutex_unlock(&my_obj->ch[MM_CAMERA_CH_PREVIEW].mutex);
}
static void mm_camera_snapshot_send_snapshot_notify(mm_camera_obj_t * my_obj)
{
	mm_camera_frame_queue_t *s_q, *t_q;
	mm_camera_ch_data_buf_t data;
	mm_camera_frame_t *frame;
	s_q =	&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.main.frame.readyq;
	t_q =	&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.thumbnail.frame.readyq;
	pthread_mutex_lock(&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].mutex);
	if(s_q->cnt && t_q->cnt && my_obj->ch[MM_CAMERA_CH_SNAPSHOT].buf_cb.cb) {
		data.type = MM_CAMERA_CH_SNAPSHOT;
		frame = mm_camera_stream_frame_deq(s_q);
		data.snapshot.main.frame = &frame->frame;
		data.snapshot.main.idx = frame->idx;
		frame = mm_camera_stream_frame_deq(t_q);
		data.snapshot.thumbnail.frame = &frame->frame;
		data.snapshot.thumbnail.idx = frame->idx;
		my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.main.frame.ref_count[data.snapshot.main.idx]++;
		my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.thumbnail.frame.ref_count[data.snapshot.thumbnail.idx]++;
		my_obj->ch[MM_CAMERA_CH_SNAPSHOT].buf_cb.cb(&data, 
								my_obj->ch[MM_CAMERA_CH_PREVIEW].buf_cb.user_data);
	}
	pthread_mutex_unlock(&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].mutex);
}
static void mm_camera_read_snapshot_main_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	int idx;
	mm_camera_stream_t *stream;
	mm_camera_frame_queue_t *q;
	mm_camera_frame_t *frame;

	q =	&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.main.frame.readyq;
	stream = &my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.main;

	frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
	if(frame) {
		rc = mm_camera_stream_qbuf(my_obj, stream, 
																frame->idx);
		if(rc < 0) {
			CDBG("%s: mm_camera_stream_qbuf(idx=%d) err=%d\n", __func__, frame->idx, rc);
			return;
		}
	}

	idx =  mm_camera_read_msm_frame(my_obj,stream);
	if (idx < 0)
		return;
	/* send to HAL */
	mm_camera_stream_frame_enq(q, &stream->frame.frame[idx]);
	mm_camera_snapshot_send_snapshot_notify(my_obj);
}
static void mm_camera_read_snapshot_thumbnail_frame(mm_camera_obj_t * my_obj)
{
	int idx, rc = 0;
	mm_camera_stream_t *stream;
	mm_camera_frame_queue_t *q;
	mm_camera_frame_t *frame;

	q =	&my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.thumbnail.frame.readyq;
	stream = &my_obj->ch[MM_CAMERA_CH_SNAPSHOT].snapshot.thumbnail;
	frame = mm_camera_stream_frame_deq(&stream->frame.freeq);
	if(frame) {
		rc = mm_camera_stream_qbuf(my_obj, stream, 
																frame->idx);
		if(rc < 0) {
			CDBG("%s: mm_camera_stream_qbuf(idx=%d) err=%d\n", __func__, frame->idx, rc);
			return;
		}
	}

	idx =  mm_camera_read_msm_frame(my_obj,stream);
	if (idx < 0)
		return;
	mm_camera_stream_frame_enq(q, &stream->frame.frame[idx]);
	mm_camera_snapshot_send_snapshot_notify(my_obj);
}

static void mm_camera_read_video_frame(mm_camera_obj_t * my_obj)
{
	int idx, rc = 0;
	mm_camera_stream_t *stream;
	mm_camera_frame_queue_t *q;

	stream = &my_obj->ch[MM_CAMERA_CH_VIDEO].video.video;
	rc = mm_camera_qbuf_to_kernel(my_obj, stream);
	idx =  mm_camera_read_msm_frame(my_obj,stream);
	if (idx < 0)
		return;
	pthread_mutex_lock(&my_obj->ch[MM_CAMERA_CH_VIDEO].mutex);
	if(my_obj->ch[MM_CAMERA_CH_VIDEO].buf_cb.cb) {
		mm_camera_ch_data_buf_t data;
		data.type = MM_CAMERA_CH_VIDEO;
		data.video.main.frame = NULL;
		data.video.main.idx = -1;
		data.video.video.idx = idx;
		data.video.video.frame = &my_obj->ch[MM_CAMERA_CH_VIDEO].video.video.
			frame.frame[idx].frame;
		my_obj->ch[MM_CAMERA_CH_VIDEO].video.video.frame.ref_count[idx]++;
		my_obj->ch[MM_CAMERA_CH_VIDEO].buf_cb.cb(&data, 
								my_obj->ch[MM_CAMERA_CH_VIDEO].buf_cb.user_data);
	} 
	pthread_mutex_unlock(&my_obj->ch[MM_CAMERA_CH_VIDEO].mutex);
}

static void mm_camera_read_video_main_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	return;rc;
}

static void mm_camera_read_zsl_main_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	return;rc;
}

static void mm_camera_read_zsl_postview_frame(mm_camera_obj_t * my_obj)
{
	int rc = 0;
	return;rc;
}

void mm_camera_msm_data_notify(mm_camera_obj_t * my_obj, int fd, 
																			mm_camera_stream_type_t stream_type)
{
	switch(stream_type) {
	case MM_CAMERA_STREAM_RAW:
		mm_camera_read_raw_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_PREVIEW:
		mm_camera_read_preview_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_SNAPSHOT:
		mm_camera_read_snapshot_main_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_THUMBNAIL:
		mm_camera_read_snapshot_thumbnail_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_VIDEO:
		mm_camera_read_video_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_VIDEO_MAIN:
		mm_camera_read_video_main_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_ZSL_MAIN:
		mm_camera_read_zsl_main_frame(my_obj);
		break;
	case MM_CAMERA_STREAM_ZSL_POST_VIEW:
		mm_camera_read_zsl_postview_frame(my_obj);
		break;
	default:
		break;
	}
}

