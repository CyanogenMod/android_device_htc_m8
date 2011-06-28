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

static int32_t mm_camera_ctrl_set_specialEffect (mm_camera_obj_t *my_obj, int effect) {
  struct v4l2_control ctrl;
  if (effect == CAMERA_EFFECT_MAX)
    effect = CAMERA_EFFECT_OFF;
  int rc = 0;

  ctrl.id = MSM_V4L2_PID_EFFECT;
  ctrl.value = effect;
  rc = ioctl(my_obj->ctrl_fd, VIDIOC_S_CTRL, &ctrl);
  return rc;
}

static int32_t mm_camera_ctrl_set_antibanding (mm_camera_obj_t *my_obj, int antibanding) {
  int rc = 0;
  struct v4l2_control ctrl;
  switch (antibanding) {
    case CAMERA_ANTIBANDING_OFF:
      printf("Set anti flicking 50Hz\n");
      antibanding = CAMERA_ANTIBANDING_50HZ;
      break;
    case CAMERA_ANTIBANDING_50HZ:
      printf("Set anti flicking 60Hz\n");
      antibanding = CAMERA_ANTIBANDING_60HZ;
      break;
    case CAMERA_ANTIBANDING_60HZ:
      printf("Turn off anti flicking\n");
      antibanding = CAMERA_ANTIBANDING_OFF;
      break;
    default:
      break;
  }
  ctrl.id = V4L2_CID_POWER_LINE_FREQUENCY;
  ctrl.value = antibanding;
  rc = ioctl(my_obj->ctrl_fd, VIDIOC_S_CTRL, &ctrl);
  return rc;
}

static int32_t mm_camera_ctrl_set_auto_focus (mm_camera_obj_t *my_obj)
{
	int rc = 0;
  struct v4l2_queryctrl queryctrl;

  memset (&queryctrl, 0, sizeof (queryctrl));
  queryctrl.id = V4L2_CID_FOCUS_AUTO;

  if (-1 == ioctl (my_obj->ctrl_fd, VIDIOC_QUERYCTRL, &queryctrl)) {
      CDBG ("V4L2_CID_FOCUS_AUTO is not supported\n");
	} else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
    CDBG ("%s:V4L2_CID_FOCUS_AUTO is not supported\n", __func__);
	} else {
		if(0 != (rc =  mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
				V4L2_CID_FOCUS_AUTO, AF_MODE_MACRO))){
			CDBG("%s: error, id=0x%x, value=%d, rc = %d\n", 
					 __func__, V4L2_CID_FOCUS_AUTO, AF_MODE_MACRO, rc);
			rc = -1;
		}
	}
  return rc;
}

static int32_t mm_camera_ctrl_set_whitebalance (mm_camera_obj_t *my_obj, int mode) {

	int rc = 0,	value;
	uint32_t id;

	switch(mode) {
	case MM_CAMERA_WHITE_BALANCE_AUTO:
		id = V4L2_CID_AUTO_WHITE_BALANCE;
		value = 1; /* TRUE */
		break;
	case MM_CAMERA_WHITE_BALANCE_OFF:
		id = V4L2_CID_AUTO_WHITE_BALANCE;
		value = 0; /* FALSE */
		break;
	default:
		id = V4L2_CID_WHITE_BALANCE_TEMPERATURE;
		if(mode == WHITE_BALANCE_DAYLIGHT) value = 6500;
		else if(mode == WHITE_BALANCE_INCANDESCENT) value = 2800;
		else if(mode ==WHITE_BALANCE_FLUORESCENT ) value = 4200;
		else
			value = 4200;
	}
	if(0 != (rc =  mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
			MSM_V4L2_PID_EXP_METERING, value))){
		CDBG("%s: error, exp_metering_action_param=%d, rc = %d\n", __func__, value, rc);
		goto end;
	}
end:
	return rc;
}

static int32_t mm_camera_ctrl_set_toggle_afr (mm_camera_obj_t *my_obj) {
  int rc = 0;
	int value = 0;
	if(0 != (rc =  mm_camera_util_g_ctrl(my_obj->ctrl_fd, 
			V4L2_CID_EXPOSURE_AUTO, &value))){
		goto end;
	}
  /* V4L2_CID_EXPOSURE_AUTO needs to be AUTO or SHUTTER_PRIORITY */
  if (value != V4L2_EXPOSURE_AUTO && value != V4L2_EXPOSURE_SHUTTER_PRIORITY) {
    CDBG("%s: V4L2_CID_EXPOSURE_AUTO needs to be AUTO/SHUTTER_PRIORITY\n",
        __func__);
    return -1;
  }
	if(0 != (rc =  mm_camera_util_g_ctrl(my_obj->ctrl_fd, 
			V4L2_CID_EXPOSURE_AUTO_PRIORITY, &value))){
		goto end;
	}
  value = !value;
	if(0 != (rc =  mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
			V4L2_CID_EXPOSURE_AUTO_PRIORITY, value))){
		goto end;
	}
end:
	CDBG("%s: end, rc = %d\n", __func__, rc);
  return rc;
}

static mm_camera_channel_type_t mm_camera_util_opcode_2_ch_type(
	mm_camera_obj_t *my_obj, 
  mm_camera_ops_type_t opcode)
{
	mm_camera_channel_type_t type = MM_CAMERA_CH_MAX;
	switch(opcode) {
	case MM_CAMERA_OPS_PREVIEW:
		return MM_CAMERA_CH_PREVIEW;
	case MM_CAMERA_OPS_VIDEO:
		return MM_CAMERA_CH_VIDEO;
	case MM_CAMERA_OPS_SNAPSHOT:
		return MM_CAMERA_CH_SNAPSHOT;
	case MM_CAMERA_OPS_PREPARE_SNAPSHOT:
		return MM_CAMERA_CH_SNAPSHOT;
	case MM_CAMERA_OPS_ZSL:
		return MM_CAMERA_CH_ZSL;
	default:
		break;
	}
	return type;
}

static int32_t mm_camera_util_set_op_mode(mm_camera_obj_t * my_obj, 
	mm_camera_op_mode_type_t *op_mode)
{
	int32_t rc = MM_CAMERA_OK;
	uint32_t v4l2_op_mode = MSM_V4L2_CAM_OP_DEFAULT;

	CDBG("%s:BEGIN\n",__func__);
	if (my_obj->op_mode == *op_mode)
		goto end;
	if(mm_camera_poll_busy(my_obj) == TRUE) {
		CDBG("%s: cannot change op_mode while stream on\n", __func__);
		rc = -MM_CAMERA_E_INVALID_OPERATION;
		goto end;
	}
	switch(*op_mode) {
	case MM_CAMERA_OP_MODE_CAPTURE:
		v4l2_op_mode = MSM_V4L2_CAM_OP_CAPTURE;
			break;
	case MM_CAMERA_OP_MODE_VIDEO:
		v4l2_op_mode = MSM_V4L2_CAM_OP_VIDEO;
			break;
	case MM_CAMERA_OP_MODE_ZSL:
		v4l2_op_mode = MSM_V4L2_CAM_OP_ZSL;
			break;
	default:
		rc = - MM_CAMERA_E_INVALID_INPUT;
		goto end;
		break;
	}
	if(0 != (rc =  mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
			MSM_V4L2_PID_CAM_MODE, v4l2_op_mode))){
		CDBG("%s: input op_mode=%d, s_ctrl err=%d\n", __func__, *op_mode, rc);
		goto end;
	}
	/* if success update mode field */
	my_obj->op_mode = *op_mode;
end:
	CDBG("%s:END, rc=%d\n", __func__, rc);
	return rc;
}

int32_t mm_camera_set_general_parm(mm_camera_obj_t * my_obj, mm_camera_parm_t *parm)
{
	int rc = -MM_CAMERA_E_NOT_SUPPORTED;

	switch(parm->parm_type)  {
	case MM_CAMERA_PARM_EXPOSURE:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd,  
																		MSM_V4L2_PID_EXP_METERING, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_SHARPNESS:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, V4L2_CID_SHARPNESS, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_CONTRAST:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, V4L2_CID_CONTRAST, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_SATURATION:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, V4L2_CID_SATURATION, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_BRIGHTNESS:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, V4L2_CID_BRIGHTNESS, 
																			*((int *)(parm->p_value)));
  case MM_CAMERA_PARM_WHITE_BALANCE:
		return mm_camera_ctrl_set_whitebalance (my_obj,	*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_ISO:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, MSM_V4L2_PID_ISO, 
																			*((int *)(parm->p_value)) -1);
	case MM_CAMERA_PARM_ZOOM:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, V4L2_CID_ZOOM_ABSOLUTE, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_LUMA_ADAPTATION:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, MSM_V4L2_PID_LUMA_ADAPTATION, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_ANTIBANDING:
		return mm_camera_ctrl_set_antibanding (my_obj, *((int *)(parm->p_value)));
  case MM_CAMERA_PARM_CONTINUOUS_AF:
		return mm_camera_ctrl_set_auto_focus(my_obj);
	case MM_CAMERA_PARM_HJR:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, MSM_V4L2_PID_HJR, 
																			*((int *)(parm->p_value)));
	case MM_CAMERA_PARM_EFFECT:
		return mm_camera_ctrl_set_specialEffect (my_obj, *((int *)(parm->p_value)));
	case MM_CAMERA_PARM_FPS:
  case MM_CAMERA_PARM_FPS_MODE:
  case MM_CAMERA_PARM_EXPOSURE_COMPENSATION:
  case MM_CAMERA_PARM_LED_MODE:
  case MM_CAMERA_PARM_ROLLOFF:
  case MM_CAMERA_PARM_FOCUS_RECT:
  case MM_CAMERA_PARM_AEC_ROI:
	case MM_CAMERA_PARM_AF_ROI:
  case MM_CAMERA_PARM_BL_DETECTION:
	case MM_CAMERA_PARM_SNOW_DETECTION:
		break;
	case MM_CAMERA_PARM_BESTSHOT_MODE:
		return mm_camera_util_s_ctrl(my_obj->ctrl_fd, MSM_V4L2_PID_BEST_SHOT, 
																				*((int *)(parm->p_value)));
		break;
  case MM_CAMERA_PARM_VIDEO_DIS:
  case MM_CAMERA_PARM_VIDEO_ROT:
  case MM_CAMERA_PARM_SCE_FACTOR:
  case MM_CAMERA_PARM_FD:
  case MM_CAMERA_PARM_MODE:
  case MM_CAMERA_PARM_3D_FRAME_FORMAT:
  case MM_CAMERA_PARM_QUERY_FALSH4SNAP:
  case MM_CAMERA_PARM_FOCUS_DISTANCES:
  case MM_CAMERA_PARM_BUFFER_INFO:
  case MM_CAMERA_PARM_JPEG_ROTATION:
  case MM_CAMERA_PARM_JPEG_MAINIMG_QUALITY:
  case MM_CAMERA_PARM_JPEG_THUMB_QUALITY:
  case MM_CAMERA_PARM_FOCAL_LENGTH:
  case MM_CAMERA_PARM_HORIZONTAL_VIEW_ANGLE:
  case MM_CAMERA_PARM_VERTICAL_VIEW_ANGLE:
  case MM_CAMERA_PARM_MCE:
  case MM_CAMERA_PARM_RESET_LENS_TO_INFINITY:
  case MM_CAMERA_PARM_SNAPSHOTDATA:
  case MM_CAMERA_PARM_HFR:
  case MM_CAMERA_PARM_REDEYE_REDUCTION:
  case MM_CAMERA_PARM_WAVELET_DENOISE:
  case MM_CAMERA_PARM_3D_DISPLAY_DISTANCE:
  case MM_CAMERA_PARM_3D_VIEW_ANGLE:
  case MM_CAMERA_PARM_HFR_SIZE:
	case MM_CAMERA_PARM_ZOOM_RATIO:
  case MM_CAMERA_PARM_HISTOGRAM:
	default:
		CDBG("%s:parm %d not supported\n", __func__, parm->parm_type);
		break; 
	}
	return rc;
}
int32_t mm_camera_set_parm(mm_camera_obj_t * my_obj, 
	mm_camera_parm_t *parm)
{
	int32_t rc = -1;

	CDBG("%s:BEGIN, parm_type=%d\n",__func__, parm->parm_type);
	switch(parm->parm_type) {
	case MM_CAMERA_PARM_OP_MODE:
		rc = mm_camera_util_set_op_mode(my_obj, 
						(mm_camera_op_mode_type_t *)parm->p_value);
		break;
	case MM_CAMERA_PARM_DIMENSION:
		memcpy(&my_obj->dim, (cam_ctrl_dimension_t *)parm->p_value, 
					 sizeof(cam_ctrl_dimension_t));
		CDBG("%s: dw=%d,dh=%d,vw=%d,vh=%d,pw=%d,ph=%d,tw=%d,th=%d,ovx=%x,ovy=%d,opx=%d,opy=%d\n",
				 __func__,
				 my_obj->dim.display_width,my_obj->dim.display_height,
				 my_obj->dim.video_width,my_obj->dim.video_height,
				 my_obj->dim.picture_width,my_obj->dim.picture_height,
				 my_obj->dim.ui_thumbnail_width,my_obj->dim.ui_thumbnail_height,
				 my_obj->dim.orig_video_width,my_obj->dim.orig_video_height,
				 my_obj->dim.orig_picture_dx,my_obj->dim.orig_picture_dy);
		rc = MM_CAMERA_OK;
		break;
	case MM_CAMERA_PARM_SNAPSHOT_BURST_NUM:
		break;
	case MM_CAMERA_PARM_CH_IMAGE_FMT:
		{
			mm_camera_ch_image_fmt_parm_t *fmt;
			fmt = (mm_camera_ch_image_fmt_parm_t *)parm->p_value;
			rc = mm_camera_ch_fn(my_obj,	fmt->ch_type, 
							MM_CAMERA_STATE_EVT_SET_FMT, fmt);
		}
		break;
	default:
		rc = mm_camera_set_general_parm(my_obj, parm);
		break;
	}
	CDBG("%s:END, rc=%d\n",__func__, rc);
	return rc;
}

int32_t mm_camera_get_parm(mm_camera_obj_t * my_obj, 
																								mm_camera_parm_t *parm)
{
	int32_t rc = MM_CAMERA_OK;

	switch(parm->parm_type) {
	case MM_CAMERA_PARM_DIMENSION:
		memcpy(parm->p_value, &my_obj->dim, sizeof(my_obj->dim));
		CDBG("%s: dw=%d,dh=%d,vw=%d,vh=%d,pw=%d,ph=%d,tw=%d,th=%d,ovx=%x,ovy=%d,opx=%d,opy=%d\n",
				 __func__,
				 my_obj->dim.display_width,my_obj->dim.display_height,
				 my_obj->dim.video_width,my_obj->dim.video_height,
				 my_obj->dim.picture_width,my_obj->dim.picture_height,
				 my_obj->dim.ui_thumbnail_width,my_obj->dim.ui_thumbnail_height,
				 my_obj->dim.orig_video_width,my_obj->dim.orig_video_height,
				 my_obj->dim.orig_picture_dx,my_obj->dim.orig_picture_dy);
		break;
	case MM_CAMERA_PARM_OP_MODE:
		*((mm_camera_op_mode_type_t *)parm->p_value) = my_obj->op_mode;
		break;
	default:
		/* needs to add more implementation */
		rc = -1;
		break;
	}
	return rc;
}

int32_t mm_camera_prepare_buf(mm_camera_obj_t * my_obj, mm_camera_reg_buf_t *buf)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	rc = mm_camera_ch_fn(my_obj,	buf->ch_type, 
					MM_CAMERA_STATE_EVT_REG_BUF, (void *)&buf->preview);
	return rc;
}
int32_t mm_camera_unprepare_buf(mm_camera_obj_t * my_obj, mm_camera_channel_type_t ch_type)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	rc = mm_camera_ch_fn(my_obj, ch_type, 
					MM_CAMERA_STATE_EVT_UNREG_BUF, NULL);
	return rc;
}

int32_t mm_camera_start(mm_camera_obj_t *my_obj, 
															 mm_camera_ops_type_t opcode, uint32_t parm)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	mm_camera_channel_type_t ch_type;
	ch_type = mm_camera_util_opcode_2_ch_type(my_obj, opcode);
	CDBG("%s:ch=%d,op_mode=%d,opcode=%d\n",
			 __func__,ch_type,my_obj->op_mode,opcode);
	switch(my_obj->op_mode) {
	case MM_CAMERA_OP_MODE_CAPTURE:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_SNAPSHOT:
			rc = mm_camera_ch_fn(my_obj, ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_ON, NULL);
			break;
		case MM_CAMERA_OPS_PREPARE_SNAPSHOT:
			/* TBD: do g_ext_ctrl */
			rc = MM_CAMERA_OK;
			break;
		default:
			break;
		}
		break;
	case MM_CAMERA_OP_MODE_VIDEO:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_VIDEO:
			rc = mm_camera_ch_fn(my_obj,	ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_ON, NULL);
		//	if(!rc) rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_POLL);
			CDBG("%s: op_mode=%d, ch %d, rc=%d\n",
					 __func__, MM_CAMERA_OP_MODE_VIDEO, ch_type ,rc);
			break;
		default:
			break;
		}
		break;
	case MM_CAMERA_OP_MODE_ZSL:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_ZSL:
			rc = mm_camera_ch_fn(my_obj,	ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_ON, NULL);
		//	if(!rc) rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_POLL);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return rc;
}
int32_t mm_camera_stop(mm_camera_obj_t *my_obj, 
	mm_camera_ops_type_t opcode, uint32_t parm)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	mm_camera_channel_type_t ch_type;

	ch_type = mm_camera_util_opcode_2_ch_type(my_obj, opcode);
	CDBG("%s:BEGIN, ch=%d\n",__func__, ch_type);
	switch(my_obj->op_mode) {
	case MM_CAMERA_OP_MODE_CAPTURE:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_SNAPSHOT:
			rc = mm_camera_ch_fn(my_obj, ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_OFF, NULL);
			CDBG("%s:CAPTURE mode STREAM_OFF rc=%d\n",__func__, rc);
	//		if(!rc) {
	//			rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_WAIT);
	//			CDBG("%s:CAPTURE mode MM_CAMERA_PIPE_CMD_WAIT rc=%d\n",__func__, rc);
	//		}
			break;
		default:
			break;
		}
		break;
	case MM_CAMERA_OP_MODE_VIDEO:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_VIDEO:
			rc = mm_camera_ch_fn(my_obj , ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_OFF, NULL);
			CDBG("%s:VIDEO mode STREAM_OFF rc=%d\n",__func__, rc);
		//	if(!rc) {
		//		if(FALSE == mm_camera_ch_is_active(my_obj, 
		//			(MM_CAMERA_OPS_PREVIEW == opcode)?MM_CAMERA_CH_VIDEO:MM_CAMERA_CH_PREVIEW)) {
		//			rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_WAIT);
		//			CDBG("%s:VIDEO mode MM_CAMERA_PIPE_CMD_WAIT cmd rc=%d\n",__func__, rc);
		//		} else {
		//			rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_REFRESH);
		//			CDBG("%s:VIDEO mode MM_CAMERA_PIPE_CMD_REFRESH cmd rc=%d\n",__func__, rc);
		//		}
		//	}
			break;
		default:
			break;
		}
		break;
	case MM_CAMERA_OP_MODE_ZSL:
		switch(opcode) {
		case MM_CAMERA_OPS_PREVIEW:
		case MM_CAMERA_OPS_ZSL:
			rc = mm_camera_ch_fn(my_obj,	ch_type, 
							MM_CAMERA_STATE_EVT_STREAM_OFF, NULL);
			//if(!rc) {
			//	rc = mm_camera_send_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_WAIT);
		//	}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return rc;
}
int32_t mm_camera_open(mm_camera_obj_t *my_obj, 
																						mm_camera_op_mode_type_t op_mode)
{
	char dev_name[MM_CAMERA_DEV_NAME_LEN];
	int32_t rc = MM_CAMERA_OK;

	CDBG("%s: BEGIN - open_mode=%d\n", __func__, op_mode);

	if(my_obj->op_mode != MM_CAMERA_OP_MODE_NOTUSED) {
		CDBG("%s: not allowed in existing op mode %d\n", 
				 __func__, my_obj->op_mode); 
		return -MM_CAMERA_E_INVALID_OPERATION;
	}
	if(op_mode >= MM_CAMERA_OP_MODE_MAX) {
		CDBG("%s: invalid input %d\n", 
				 __func__, op_mode); 
		return -MM_CAMERA_E_INVALID_INPUT;		
	}
	sprintf(dev_name, "/dev/%s", mm_camera_util_get_dev_name(my_obj));
	CDBG("%s: open dev '%s'\n",__func__, dev_name); 
	my_obj->ctrl_fd = open(dev_name,
										O_RDWR | O_NONBLOCK);
	if (my_obj->ctrl_fd <= 0) {
		CDBG("%s: cannot open control fd of '%s'\n",
				 __func__, mm_camera_util_get_dev_name(my_obj)); 
		return -MM_CAMERA_E_GENERAL;
	}
	if(op_mode != MM_CAMERA_OP_MODE_NOTUSED) 
		rc =  mm_camera_util_s_ctrl(my_obj->ctrl_fd, 
							MSM_V4L2_PID_CAM_MODE, op_mode); 
	if(!rc)	{
		my_obj->op_mode = op_mode;
		my_obj->current_mode = CAMERA_MODE_2D; /* set geo mode to 2D by default */
	}
	return rc;
}
int32_t mm_camera_close(mm_camera_obj_t *my_obj)
{
	int i;

	for(i = 0; i < MM_CAMERA_CH_MAX; i++){ 
		mm_camera_ch_fn(my_obj, (mm_camera_channel_type_t)i, 
																	MM_CAMERA_STATE_EVT_RELEASE, NULL);
	}
	my_obj->op_mode = MM_CAMERA_OP_MODE_NOTUSED;
	if(my_obj->ctrl_fd) {
		close(my_obj->ctrl_fd);
		my_obj->ctrl_fd = 0;
	}
	return MM_CAMERA_OK;
}
int32_t mm_camera_action(mm_camera_obj_t *my_obj, uint8_t start,
																 mm_camera_ops_type_t opcode, uint32_t parm)
{
	int32_t rc = - MM_CAMERA_E_INVALID_OPERATION;

	CDBG("%s:BEGIN,start=%d,opcode=%d,parm=%d\n",__func__,start,opcode,parm);
	if(start)	rc = mm_camera_start(my_obj, opcode, parm);
	else rc = mm_camera_stop(my_obj, opcode, parm);
	CDBG("%s:END,rc=%d\n",__func__,rc);
	return rc;
}
int32_t mm_camera_ch_acquire(mm_camera_obj_t *my_obj, mm_camera_channel_type_t ch_type)
{
	return mm_camera_ch_fn(my_obj,ch_type, MM_CAMERA_STATE_EVT_ACQUIRE, 0);
}
void mm_camera_ch_release(mm_camera_obj_t *my_obj, mm_camera_channel_type_t ch_type)
{
	mm_camera_ch_fn(my_obj,ch_type, MM_CAMERA_STATE_EVT_RELEASE, 0);
}
