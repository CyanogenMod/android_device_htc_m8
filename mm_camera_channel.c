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

static void mm_camera_ch_util_get_stream_objs(mm_camera_obj_t * my_obj, 
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
	case MM_CAMERA_CH_ZSL:
		*stream1 = &my_obj->ch[ch_type].zsl.main;
		*stream2 = &my_obj->ch[ch_type].zsl.postview;
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
	case MM_CAMERA_CH_ZSL:
		stream1 = &my_obj->ch[ch_type].zsl.main;
		fmt1 = &fmt->zsl.main;
		stream2 = &my_obj->ch[ch_type].zsl.postview;
		fmt2 = &fmt->zsl.postview;
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
	case MM_CAMERA_CH_ZSL:
		stream1 = &my_obj->ch[ch_type].zsl.main;
		type1 = MM_CAMERA_STREAM_ZSL_MAIN;
		stream2 = &my_obj->ch[ch_type].zsl.postview;
		type2 = MM_CAMERA_STREAM_ZSL_POST_VIEW;
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
		case MM_CAMERA_CH_ZSL:
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].zsl.main, evt, 
							NULL);
			if(!rc) 
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].zsl.postview, evt, 
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
		case MM_CAMERA_CH_ZSL:
			{
				mm_camera_buf_zsl_t * buf = (mm_camera_buf_zsl_t *)val;
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].zsl.main, evt, 
								&buf->main);
				if(!rc) {
					rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
									&my_obj->ch[ch_type].zsl.postview, evt, 
									&buf->postview);
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
	case MM_CAMERA_CH_ZSL:
		{
			rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
							&my_obj->ch[ch_type].zsl.main, evt, 
							&val->zsl.main);
			if(!rc) 
				rc = mm_camera_stream_fsm_fn_vtbl(my_obj, 
								&my_obj->ch[ch_type].zsl.postview, evt, 
								&val->zsl.postview);
		}
		break;
	default:
		return -1;
		break;
	}
	return rc;
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
		rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
		break;
	case MM_CAMERA_STATE_EVT_STREAM_OFF:
		rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
		break;
	case MM_CAMERA_STATE_EVT_QBUF:
		rc = mm_camera_ch_util_qbuf(my_obj, ch_type, evt, 
																(mm_camera_ch_data_buf_t *)val);
		break;
	default:
		break;
	}
	return rc;
}

