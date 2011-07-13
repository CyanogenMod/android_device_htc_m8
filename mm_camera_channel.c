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

void mm_camera_ch_util_get_stream_objs(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type, 
															mm_camera_stream_t **stream1, 
															mm_camera_stream_t **stream2)
{
	*stream1 = NULL;
	*stream2 = NULL;

	switch(ch_type) {
	case MM_CAMERA_CH_RAW:
		*stream1 = &my_obj->ch[ch_type].raw.stream;
		break;
	case MM_CAMERA_CH_PREVIEW:
		*stream1 = &my_obj->ch[ch_type].preview.stream;
		break;
	case MM_CAMERA_CH_VIDEO:
		*stream1 = &my_obj->ch[ch_type].video.video;
		if(my_obj->ch[ch_type].video.has_main) {
			*stream2 = &my_obj->ch[ch_type].video.main;
		}
		break;
	case MM_CAMERA_CH_SNAPSHOT:
		*stream1 = &my_obj->ch[ch_type].snapshot.main;
		*stream2 = &my_obj->ch[ch_type].snapshot.thumbnail;
		break;
	default:
		break;
	}
}

static int32_t mm_camera_ch_util_set_fmt(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type, 
															mm_camera_ch_image_fmt_parm_t *fmt)
{
	int32_t rc = MM_CAMERA_OK;
	mm_camera_stream_t *stream1 = NULL;
	mm_camera_stream_t *stream2 = NULL;
	mm_camera_image_fmt_t *fmt1 = NULL;
	mm_camera_image_fmt_t *fmt2 = NULL;

	switch(ch_type) {
	case MM_CAMERA_CH_RAW:
		stream1 = &my_obj->ch[ch_type].raw.stream;
		fmt1 = &fmt->def;
		break;
	case MM_CAMERA_CH_PREVIEW:
		stream1 = &my_obj->ch[ch_type].preview.stream;
		fmt1 = &fmt->def;
		break;
	case MM_CAMERA_CH_VIDEO:
		stream1 = &my_obj->ch[ch_type].video.video;
		fmt1 = &fmt->video.video;
		if(my_obj->ch[ch_type].video.has_main) {
			CDBG("%s:video channel has main image stream\n", __func__);
			stream2 = &my_obj->ch[ch_type].video.main;
			fmt2 = &fmt->video.main;
		}
		break;
	case MM_CAMERA_CH_SNAPSHOT:
		stream1 = &my_obj->ch[ch_type].snapshot.main;
		fmt1 = &fmt->snapshot.main;
		stream2 = &my_obj->ch[ch_type].snapshot.thumbnail;
		fmt2 = &fmt->snapshot.thumbnail;
		break;
	default:
		rc = -1;
		break;
	}
	CDBG("%s:ch=%d, streams[0x%x,0x%x]\n", __func__, ch_type, 
			 (uint32_t)stream1, (uint32_t)stream2);
	if(stream1) 
		rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream1, 
																			MM_CAMERA_STATE_EVT_SET_FMT, fmt1);
	if(stream2 && !rc) 
		rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream2, 
																			MM_CAMERA_STATE_EVT_SET_FMT, fmt2);
	return rc;
}

static int32_t mm_camera_ch_util_acquire(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type)
{
	int32_t rc = MM_CAMERA_OK;
	mm_camera_stream_t *stream1 = NULL;
	mm_camera_stream_t *stream2 = NULL;
	mm_camera_stream_type_t type1;
	mm_camera_stream_type_t type2;

	if(my_obj->ch[ch_type].acquired) {
		rc = MM_CAMERA_OK;
		goto end;
	}
	pthread_mutex_init(&my_obj->ch[ch_type].mutex, NULL);
	switch(ch_type) {
	case MM_CAMERA_CH_RAW:
		stream1 = &my_obj->ch[ch_type].raw.stream;
		type1 = MM_CAMERA_STREAM_RAW;
		break;
	case MM_CAMERA_CH_PREVIEW:
		stream1 = &my_obj->ch[ch_type].preview.stream;
		type1 = MM_CAMERA_STREAM_PREVIEW;
		break;
	case MM_CAMERA_CH_VIDEO:
		stream1 = &my_obj->ch[ch_type].video.video;
		type1 = MM_CAMERA_STREAM_VIDEO;
		/* no full image live shot by default */
		my_obj->ch[ch_type].video.has_main = FALSE;
		break;
	case MM_CAMERA_CH_SNAPSHOT:
		stream1 = &my_obj->ch[ch_type].snapshot.main;
		type1 = MM_CAMERA_STREAM_SNAPSHOT;
		stream2 = &my_obj->ch[ch_type].snapshot.thumbnail;
		type2 = MM_CAMERA_STREAM_THUMBNAIL;
		break;
	default:
		return -1;
		break;
	}
	if(stream1)	rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream1, 
											MM_CAMERA_STATE_EVT_ACQUIRE, &type1);
	if(stream2 && !rc) rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream2, 
											MM_CAMERA_STATE_EVT_ACQUIRE, &type2);
	if(rc == MM_CAMERA_OK) {
		if(!my_obj->ch[ch_type].acquired)	my_obj->ch[ch_type].acquired = TRUE;
	}
end:
	return rc;
}
static int32_t mm_camera_ch_util_release(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type, 
															mm_camera_state_evt_type_t evt)
{
	mm_camera_stream_t *stream1, *stream2;

	if(!my_obj->ch[ch_type].acquired) return MM_CAMERA_OK;
	
	mm_camera_ch_util_get_stream_objs(my_obj,ch_type, &stream1, &stream2);
	if(stream1) 
		mm_camera_stream_fsm_fn_vtbl(my_obj, stream1, evt, NULL);
	if(stream2) 
		mm_camera_stream_fsm_fn_vtbl(my_obj, stream2, evt, NULL);
	pthread_mutex_destroy(&my_obj->ch[ch_type].mutex);
	memset(&my_obj->ch[ch_type],0,sizeof(my_obj->ch[ch_type]));
	return 0;	
}
static int32_t mm_camera_ch_util_stream_null_val(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type, 
															mm_camera_state_evt_type_t evt, void *val)
{
		int32_t rc = 0;
		switch(ch_type) {
		case MM_CAMERA_CH_RAW:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, &my_obj->ch[ch_type].raw.stream, 
																		 evt, NULL);
			break;
		case MM_CAMERA_CH_PREVIEW:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, &my_obj->ch[ch_type].preview.stream, 
																		 evt, NULL);
			break;
		case MM_CAMERA_CH_VIDEO:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].video.video, evt, 
							NULL);
			if(!rc && my_obj->ch[ch_type].video.main.fd) 
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].video.main, evt, 
								NULL);
			break;
		case MM_CAMERA_CH_SNAPSHOT:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].snapshot.main, evt, 
							NULL);
			if(!rc) 
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].snapshot.thumbnail, evt, 
								NULL);
			break;
		default:
			return -1;
			break;
		}
		return rc;
}
static int32_t mm_camera_ch_util_reg_buf(mm_camera_obj_t * my_obj, 
															mm_camera_channel_type_t ch_type, 
															mm_camera_state_evt_type_t evt, void *val)
{
		int32_t rc = 0;
		switch(ch_type) {
		case MM_CAMERA_CH_RAW:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
																		 &my_obj->ch[ch_type].raw.stream, evt, 
																		 (mm_camera_buf_def_t *)val);
			break;
		case MM_CAMERA_CH_PREVIEW:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
																		 &my_obj->ch[ch_type].preview.stream, evt, 
																		 (mm_camera_buf_def_t *)val);
			break;
		case MM_CAMERA_CH_VIDEO:
			{
				mm_camera_buf_video_t * buf = (mm_camera_buf_video_t *)val;
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].video.video, evt, 
								&buf->video);
				if(!rc && my_obj->ch[ch_type].video.has_main) {
					rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
									&my_obj->ch[ch_type].video.main, evt, 
									&buf->main);
				}
			}
			break;
		case MM_CAMERA_CH_SNAPSHOT:
			{
				mm_camera_buf_snapshot_t * buf = (mm_camera_buf_snapshot_t *)val;
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].snapshot.main, evt, 
								&buf->main);
				if(!rc) {
					rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
									&my_obj->ch[ch_type].snapshot.thumbnail, evt, 
									& buf->thumbnail);
				}
			}
			break;
		default:
			return -1;
			break;
		}
		return rc;
}

static int32_t mm_camera_ch_util_attr(mm_camera_obj_t *my_obj, 
																						mm_camera_channel_type_t ch_type,
																						mm_camera_channel_attr_t *val)
{
	int rc = -MM_CAMERA_E_NOT_SUPPORTED;
	if(ch_type != MM_CAMERA_CH_RAW) {
		CDBG("%s: attr type %d not support for ch %d\n", __func__, val->type, ch_type);
		return rc;
	}
	if(my_obj->ch[ch_type].acquired== 0) return -MM_CAMERA_E_INVALID_OPERATION;
	switch(val->type) {
	case MM_CAMERA_CH_ATTR_RAW_STREAMING_TYPE:
		if(val->raw_streaming_mode == MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE) {
			my_obj->ch[ch_type].raw.mode = val->raw_streaming_mode;
			rc = MM_CAMERA_OK;
		}
		break;
	case MM_CAMERA_CH_ATTR_BUFFERING_FRAME:
	  /* it's good to check the stream state. TBD later  */
	  memcpy(&my_obj->ch[ch_type].buffering_frame, &val->buffering_frame, sizeof(val->buffering_frame));
	  break;
	default:
		break; 
	}
	return MM_CAMERA_OK;
}

static int32_t mm_camera_ch_util_reg_buf_cb(mm_camera_obj_t *my_obj, 
																						mm_camera_channel_type_t ch_type,
																						mm_camera_buf_cb_t *val)
{
	pthread_mutex_lock(&my_obj->ch[ch_type].mutex);
	memcpy(&my_obj->ch[ch_type].buf_cb, val, sizeof(my_obj->ch[ch_type].buf_cb));
	pthread_mutex_unlock(&my_obj->ch[ch_type].mutex);
	return MM_CAMERA_OK;
}

static int32_t mm_camera_ch_util_qbuf(mm_camera_obj_t *my_obj, 
									mm_camera_channel_type_t ch_type, 
									mm_camera_state_evt_type_t evt,
									mm_camera_ch_data_buf_t *val)
{
	int32_t rc = -1;
	switch(ch_type) {
	case MM_CAMERA_CH_RAW:
		rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
																	 &my_obj->ch[ch_type].raw.stream, evt, 
																	 &val->def);
		break;
	case MM_CAMERA_CH_PREVIEW:
		rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
																	 &my_obj->ch[ch_type].preview.stream, evt, 
																	 &val->def);
		break;
	case MM_CAMERA_CH_VIDEO:
		{
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].video.video, evt, 
							&val->video.video);
			if(!rc && val->video.main.frame) 
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].video.main, evt, 
								&val->video.main);
		}
		break;
	case MM_CAMERA_CH_SNAPSHOT:
		{
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].snapshot.main, evt, 
							&val->snapshot.main);
			if(!rc) {
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].snapshot.thumbnail, evt, 
								&val->snapshot.thumbnail);
			}
		}
		break;
	default:
		return -1;
		break;
	}
	return rc;
}

static int mm_camera_ch_util_get_crop(mm_camera_obj_t *my_obj, 
								mm_camera_channel_type_t ch_type, 
								mm_camera_state_evt_type_t evt, 
								mm_camera_ch_crop_t *crop)
{
  int rc = MM_CAMERA_OK;
  switch(ch_type) {
  case MM_CAMERA_CH_RAW:
	  rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
									   &my_obj->ch[ch_type].raw.stream, evt, 
									   &crop->crop);
	  break;
  case MM_CAMERA_CH_PREVIEW:
	  rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
									&my_obj->ch[ch_type].preview.stream, evt, 
									&crop->crop);
	  break;
  case MM_CAMERA_CH_VIDEO:
	  rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
						  &my_obj->ch[ch_type].video.video, evt, 
						  &crop->crop);
	  break;
  case MM_CAMERA_CH_SNAPSHOT:
	  {
		  rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
						  &my_obj->ch[ch_type].snapshot.main, evt, 
						  &crop->snapshot.main_crop);
		  if(!rc) {
			  rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							  &my_obj->ch[ch_type].snapshot.thumbnail, evt, 
							  &crop->snapshot.thumbnail_crop);
		  }
	  }
	  break;
  default:
	  return -1;
	  break;
  }
  return rc;

}

static int mm_camera_ch_util_dispatch_buffered_frame(mm_camera_obj_t *my_obj, 
				mm_camera_channel_type_t ch_type)
{
  return mm_camera_poll_dispatch_buffered_frames(my_obj, ch_type);
}
static int mm_camera_dispatch_buffered_frames_multi(mm_camera_obj_t *my_obj, 
												mm_camera_ch_t *ch, 
												mm_camera_channel_type_t ch_type,
												mm_camera_stream_t *mstream,
												mm_camera_stream_t *sstream,
												mm_camera_frame_queue_t *mq,
												mm_camera_frame_queue_t *sq)
{
  int mcnt, i, rc = MM_CAMERA_OK;
  mm_camera_ch_data_buf_t data;
  mm_camera_frame_t *mframe, *sframe, *qmframe = NULL, *qsframe = NULL;

  pthread_mutex_lock(&ch->mutex);
  mcnt = mm_camera_stream_frame_get_q_cnt(mq);
  for(i = 0; i < mcnt; i++) {
	mframe = mm_camera_stream_frame_deq(mq);
	sframe = mm_camera_stream_frame_deq(sq);
	if(mframe && sframe) {
	  /* dispatch this pair of frames */
	  memset(&data, 0, sizeof(data));
	  data.type = ch_type;
	  data.snapshot.main.frame = &mframe->frame;
	  data.snapshot.main.idx = mframe->idx;
	  data.snapshot.thumbnail.frame = &sframe->frame;
	  data.snapshot.thumbnail.idx = sframe->idx;
	  mstream->frame.ref_count[data.snapshot.main.idx]++;
	  sstream->frame.ref_count[data.snapshot.thumbnail.idx]++;
	  ch->buf_cb.cb(&data, ch->buf_cb.user_data);
	} else {
	  qmframe = mframe; 
	  qsframe = sframe; 
	  rc = -1;
	  break;
	}
  }
  if(qmframe) {
	mm_camera_stream_frame_enq(mq, &mstream->frame.frame[qmframe->idx]);
	qmframe = NULL;
  }
  if(qsframe) {
	mm_camera_stream_frame_enq(sq, &sstream->frame.frame[qsframe->idx]);
	qsframe = NULL;
  }
  pthread_mutex_unlock(&ch->mutex);
  return rc;
}
int mm_camera_channel_get_time_diff(struct timespec *cur_ts, int usec_target, struct timespec *frame_ts)
{
  int dtusec = (cur_ts->tv_nsec - frame_ts->tv_nsec)/1000;
  dtusec += (cur_ts->tv_sec - frame_ts->tv_sec)*1000000 - usec_target;
  return dtusec;
}
static int mm_camera_dispatch_buffered_frames_single(mm_camera_obj_t *my_obj, 
												 mm_camera_ch_t *ch, 
												 mm_camera_channel_type_t ch_type,
												 mm_camera_stream_t *mstream,
                                                 mm_camera_stream_t *sstream,
												 mm_camera_frame_queue_t *mq,
												 mm_camera_frame_queue_t *sq)
{
  int mcnt, i, rc = MM_CAMERA_OK;
  mm_camera_ch_data_buf_t data;
  int dt1, dt2;
  mm_camera_frame_t *mframe1, *sframe1, *qmframe = NULL;
  mm_camera_frame_t *mframe2, *sframe2, *qsframe = NULL;
  struct timeval tv;
  struct timespec cur_ts;

  gettimeofday(&tv, NULL);
  cur_ts.tv_sec  = tv.tv_sec;
  cur_ts.tv_nsec = tv.tv_usec * 1000;

  pthread_mutex_lock(&ch->mutex);
  mcnt = mm_camera_stream_frame_get_q_cnt(mq);
  mframe1 = mm_camera_stream_frame_deq(mq);
  sframe1 = mm_camera_stream_frame_deq(sq);
  if(!mframe1 || !sframe1) {
	CDBG("%s: no frames raedy.\n", __func__);
	qmframe = mframe1; 
	qsframe = sframe1; 
	rc = -1;
	goto end;
  }
  dt1 = mm_camera_channel_get_time_diff(&cur_ts, ch->buffering_frame.ms, &mframe1->frame.ts);
  if (dt1 > 0) {
	for(i = 0; i < mcnt-1; i++) {
	  /* be sure to queue old frames back to kernel */
	  if(qmframe) {
		mm_camera_stream_qbuf(my_obj, mstream, qmframe->idx);
		qmframe = NULL;
	  }
	  if(qsframe) {
		mm_camera_stream_qbuf(my_obj, sstream, qsframe->idx);
		qsframe = NULL;
	  }
	  mframe2 = mm_camera_stream_frame_deq(mq);
	  sframe2 = mm_camera_stream_frame_deq(sq);
	  if(!mframe2 || !sframe2) {
		qmframe = mframe2; 
		qsframe = sframe2; 
		goto done;
	  } else {
		/* dispatch this pair of frames */
		dt2 = mm_camera_channel_get_time_diff(&cur_ts, ch->buffering_frame.ms, &mframe2->frame.ts);
		if(dt2 == 0) {
		  /* right on target */
		  qmframe = mframe1; 
		  qsframe = sframe1; 
		  mframe1 = mframe2;
		  sframe1 = sframe2;
		  goto done;
		}
		else if(dt2 > 0) {
		  /* not find the best match */
		  qmframe = mframe1; 
		  qsframe = sframe1; 
		  mframe1 = mframe2;
		  sframe1 = sframe2;
		  dt1 = dt2;
		  continue;
		}
		else if(dt2 < 0) {
		  dt2 = -dt2;
		  if(dt1 < dt2) {
			/* dt1 is better match */
			qmframe = mframe2; 
			qsframe = sframe2; 
			goto done;
		  } else {
			qmframe = mframe1; 
			qsframe = sframe1; 
			mframe1 = mframe2;
			sframe1 = sframe2;
			goto done;
		  }
		}
	  }
	}
	rc = -1;
	CDBG("%s: unexpected error. Should not hit here. bug!!\n", __func__);
	goto end;
  }
done:
  memset(&data, 0, sizeof(data));
  data.type = ch_type;
  data.snapshot.main.frame = &mframe1->frame;
  data.snapshot.main.idx = mframe1->idx;
  data.snapshot.thumbnail.frame = &sframe1->frame;
  data.snapshot.thumbnail.idx = sframe1->idx;
  mstream->frame.ref_count[data.snapshot.main.idx]++;
  sstream->frame.ref_count[data.snapshot.thumbnail.idx]++;
  ch->buf_cb.cb(&data, ch->buf_cb.user_data);
end:
  if(qmframe && qsframe) { 
	/* queue to kernel */
	if(qmframe) {
	  mm_camera_stream_qbuf(my_obj, mstream, qmframe->idx);
	  qmframe = NULL;
	}
	if(qsframe) {
	  mm_camera_stream_qbuf(my_obj, sstream, qsframe->idx);
	  qsframe = NULL;
	}
  } else {
	/* queue back ready queue */
	if(qmframe) {
	  mm_camera_stream_frame_enq(mq, &mstream->frame.frame[qmframe->idx]);
	  qmframe = NULL;
	}
	if(qsframe) {
	  mm_camera_stream_frame_enq(sq, &sstream->frame.frame[qsframe->idx]);
	  qsframe = NULL;
	}
  }
  pthread_mutex_unlock(&ch->mutex);
  return rc;

}
void mm_camera_dispatch_buffered_frames(mm_camera_obj_t *my_obj, 
				mm_camera_channel_type_t ch_type)
{
  int rc = 0;
  mm_camera_ch_t *ch = &my_obj->ch[ch_type];
  mm_camera_frame_queue_t *mq = NULL;
  mm_camera_frame_queue_t *sq = NULL;
  mm_camera_stream_t *stream1 = NULL;
  mm_camera_stream_t *stream2 = NULL;

  mm_camera_ch_util_get_stream_objs(my_obj, ch_type, &stream1, &stream2);
  mq = &stream1->frame.readyq;
  if(stream2) {
	sq = &stream2->frame.readyq;
  }

  if(ch->buffering_frame.multi_frame) {
	/* dispatch all frames to app */
	rc = mm_camera_dispatch_buffered_frames_multi(my_obj, ch, ch_type, 
												  stream1, stream2, mq, sq);
  } else {
	rc = mm_camera_dispatch_buffered_frames_single(my_obj, ch, ch_type, 
												   stream1, stream2, mq, sq);
  }
  if(rc == MM_CAMERA_OK) {
	/* call evt cb to send frame delivered event to app */
	mm_camera_evt_obj_t evtcb;
	mm_camera_event_t data;
	int i;
	pthread_mutex_lock(&my_obj->mutex);
	memcpy(&evtcb, &my_obj->evt[MM_CAMERA_EVT_TYPE_CH], sizeof(mm_camera_evt_obj_t));
	pthread_mutex_unlock(&my_obj->mutex);
	data.evt_type = MM_CAMERA_EVT_TYPE_CH;
	data.ch_evt.evt = MM_CAMERA_CH_EVT_DATA_DELIVERY_DONE;
	data.ch_evt.ch = ch_type;
	for(i = 0; i < MM_CAMERA_EVT_ENTRY_MAX; i++) {
	  if(evtcb.evt[i].evt_cb) {
		evtcb.evt[i].evt_cb(&data, evtcb.evt[i].user_data);
	  }
	}
  }
}
int32_t mm_camera_ch_fn(mm_camera_obj_t * my_obj, 
		mm_camera_channel_type_t ch_type, 
		mm_camera_state_evt_type_t evt, void *val)
{
	int32_t rc = MM_CAMERA_OK;

	CDBG("%s:ch = %d, evt=%d\n", __func__, ch_type, evt);
	switch(evt) {
	case MM_CAMERA_STATE_EVT_ACQUIRE:
		rc = mm_camera_ch_util_acquire(my_obj, ch_type);
		break;
	case MM_CAMERA_STATE_EVT_RELEASE:
	  /* safe code in case no stream off before release. */
		mm_camera_poll_thread_release(my_obj, ch_type);
		rc = mm_camera_ch_util_release(my_obj, ch_type, evt);
		break;
	case MM_CAMERA_STATE_EVT_ATTR:
		rc = mm_camera_ch_util_attr(my_obj, ch_type, 
																(mm_camera_channel_attr_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_REG_BUF_CB:
		rc = mm_camera_ch_util_reg_buf_cb(my_obj, ch_type, 
																			(mm_camera_buf_cb_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_SET_FMT:
		rc = mm_camera_ch_util_set_fmt(my_obj, ch_type, 
														(mm_camera_ch_image_fmt_parm_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_REG_BUF:
		rc = mm_camera_ch_util_reg_buf(my_obj, ch_type, evt, val);
		break;
	case MM_CAMERA_STATE_EVT_UNREG_BUF:
		rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
		break;
	case MM_CAMERA_STATE_EVT_STREAM_ON:
		if(ch_type == MM_CAMERA_CH_RAW && 
			 my_obj->ch[ch_type].raw.mode == MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE) {
			if( MM_CAMERA_OK != (rc = mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
				MSM_V4L2_PID_CAM_MODE, MSM_V4L2_CAM_OP_RAW))) {
				CDBG("%s:set MM_CAMERA_RAW_STREAMING_CAPTURE_SINGLE err=%d\n", __func__, rc);
				break;
			}
		}
		mm_camera_poll_thread_launch(my_obj, ch_type);
		rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
		if(rc < 0) 
		  mm_camera_poll_thread_release(my_obj, ch_type);
		break;
	case MM_CAMERA_STATE_EVT_STREAM_OFF:
		mm_camera_poll_thread_release(my_obj, ch_type);
		rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
		break;
	case MM_CAMERA_STATE_EVT_QBUF:
		rc = mm_camera_ch_util_qbuf(my_obj, ch_type, evt, 
									(mm_camera_ch_data_buf_t *)val);
		break;
	case MM_CAMERA_STATE_EVT_GET_CROP:
	  rc = mm_camera_ch_util_get_crop(my_obj, ch_type, evt, 
								  (mm_camera_ch_crop_t *)val);
	  break;
	case MM_CAMERA_STATE_EVT_DISPATCH_BUFFERED_FRAME:
	  rc = mm_camera_ch_util_dispatch_buffered_frame(my_obj, ch_type);
	  break;
	default:
		break;
	}
	return rc;
}

