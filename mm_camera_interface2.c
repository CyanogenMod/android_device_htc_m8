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

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static mm_camera_ctrl_t g_cam_ctrl;

static int mm_camera_util_opcode_2_ch_type(mm_camera_obj_t *my_obj, 
                                        mm_camera_ops_type_t opcode)
{
	switch(opcode) {
			case MM_CAMERA_OPS_PREVIEW:
        return MM_CAMERA_CH_PREVIEW;
			case MM_CAMERA_OPS_SNAPSHOT:
				return MM_CAMERA_CH_SNAPSHOT;
			case MM_CAMERA_OPS_PREPARE_SNAPSHOT:
				return MM_CAMERA_CH_SNAPSHOT;
      case MM_CAMERA_OPS_ZSL:
				return MM_CAMERA_CH_ZSL;
			case MM_CAMERA_OPS_RAW:
				return MM_CAMERA_CH_RAW;
	default:
		break;
	}
	return -1;
}

const char *mm_camera_util_get_dev_name(mm_camera_obj_t * my_obj)
{
	CDBG("%s: Returning %s at index :%d\n",__func__,g_cam_ctrl.camera[my_obj->my_id].video_dev_name,my_obj->my_id);
	return g_cam_ctrl.camera[my_obj->my_id].video_dev_name;
}

/* used for querying the camera_info of the given camera_id */
static const camera_info_t * mm_camera_cfg_query_camera_info (int8_t camera_id)
{
	if(camera_id >= MSM_MAX_CAMERA_SENSORS)
		return NULL;
	return &g_cam_ctrl.camera[camera_id].camera_info;
}
/* check if the parm is supported */
static uint8_t mm_camera_cfg_is_parm_supported (mm_camera_t * camera, 
																							 mm_camera_parm_type_t parm_type)
{
	switch(parm_type) {
  case MM_CAMERA_PARM_CH_IMAGE_FMT:
  case MM_CAMERA_PARM_OP_MODE:
	case MM_CAMERA_PARM_DIMENSION:
		return MM_CAMERA_PARM_SUPPORT_BOTH;
	case MM_CAMERA_PARM_HDR:
	case MM_CAMERA_PARM_HISTOGRAM:
  case MM_CAMERA_PARM_SNAPSHOT_BURST_NUM:
		return MM_CAMERA_PARM_SUPPORT_SET;
	case MM_CAMERA_PARM_MAXZOOM:
	case MM_CAMERA_PARM_ZOOM_RATIO:
		return MM_CAMERA_PARM_SUPPORT_GET;
	default:
		break;
	}
	return 0;
}

/* check if the channel is supported */
static uint8_t mm_camera_cfg_is_ch_supported (mm_camera_t * camera, mm_camera_channel_type_t ch_type)
{
	switch(ch_type) {
	case MM_CAMERA_CH_PREVIEW:
	case MM_CAMERA_CH_VIDEO:
	case MM_CAMERA_CH_SNAPSHOT:
	case MM_CAMERA_CH_RAW:
		return TRUE;
	case MM_CAMERA_CH_ZSL:
	case MM_CAMERA_CH_MAX:
	default:
		return FALSE;
	}
	return FALSE;
}

/* set a parm’s current value */
static int32_t mm_camera_cfg_set_parm (mm_camera_t * camera, mm_camera_parm_type_t parm_type,
																void *p_value)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	uint32_t tmp;
	mm_camera_obj_t * my_obj = NULL;
	mm_camera_parm_t parm = {.parm_type = parm_type, .p_value = p_value};

	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_set_parm(my_obj, &parm);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}

/* get a parm’s current value */
static int32_t mm_camera_cfg_get_parm (mm_camera_t * camera, 
																			 mm_camera_parm_type_t parm_type,
																			 void* p_value)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	uint32_t tmp;
	mm_camera_obj_t * my_obj = NULL;
	mm_camera_parm_t parm = {.parm_type = parm_type, .p_value = p_value};

	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc =  mm_camera_get_parm(my_obj, &parm);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}
static int32_t mm_camera_cfg_prepare_buf(mm_camera_t * camera, mm_camera_reg_buf_t *buf)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	uint32_t tmp;
	mm_camera_obj_t * my_obj = NULL;

	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc =  mm_camera_prepare_buf(my_obj, buf);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}
static int32_t mm_camera_cfg_unprepare_buf(mm_camera_t * camera, mm_camera_channel_type_t ch_type)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	uint32_t tmp;
	mm_camera_obj_t * my_obj = NULL;

	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc =  mm_camera_unprepare_buf(my_obj,ch_type);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}

static mm_camera_config_t mm_camera_cfg = {
  .is_parm_supported = mm_camera_cfg_is_parm_supported,
  .is_ch_supported = mm_camera_cfg_is_ch_supported,
  .set_parm = mm_camera_cfg_set_parm,
  .get_parm = mm_camera_cfg_get_parm,
	.prepare_buf = mm_camera_cfg_prepare_buf,
	.unprepare_buf = mm_camera_cfg_unprepare_buf
};

static uint8_t mm_camera_ops_is_op_supported (mm_camera_t * camera, 
																						 mm_camera_ops_type_t opcode)
{
	return TRUE;
}
static int32_t mm_camera_ops_action (mm_camera_t * camera, uint8_t start, 
																mm_camera_ops_type_t opcode, uint32_t val)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	mm_camera_obj_t * my_obj = NULL;
	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_action(my_obj, start, opcode, val);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}

/* open uses flags to optionally disable jpeg/vpe interface. */
static int32_t mm_camera_ops_open (mm_camera_t * camera, 
														mm_camera_op_mode_type_t op_mode)
{
	int8_t camera_id = camera->camera_info.camera_id;
	int32_t rc = MM_CAMERA_OK;

	CDBG("%s: BEGIN\n", __func__);
	pthread_mutex_lock(&g_mutex);
	/* not first open */
	if(g_cam_ctrl.cam_obj[camera_id]) {
		g_cam_ctrl.cam_obj[camera_id]->ref_count++;
		goto end;
	}
	g_cam_ctrl.cam_obj[camera_id] = 
	(mm_camera_obj_t *)malloc(sizeof(mm_camera_obj_t));
	if(!g_cam_ctrl.cam_obj[camera_id]) {
		rc = -MM_CAMERA_E_NO_MEMORY;
		goto end;
	}
	memset(g_cam_ctrl.cam_obj[camera_id], 0, 
				 sizeof(mm_camera_obj_t));
	//g_cam_ctrl.cam_obj[camera_id]->ctrl_fd = -1;
	g_cam_ctrl.cam_obj[camera_id]->ref_count++;
	g_cam_ctrl.cam_obj[camera_id]->my_id=camera_id;

	/* TODO: this per obj mutex is not used so far. 
	 	 Need to see if g_mutex add significent delay or not.
		 If not the per obj mutex needs to be removed */
	pthread_mutex_init(&g_cam_ctrl.cam_obj[camera_id]->mutex, NULL);
	/* init condv, mutex, fork worker thread */
	pthread_mutex_init(&g_cam_ctrl.cam_obj[camera_id]->work_ctrl.mutex, NULL);
	pthread_cond_init(&g_cam_ctrl.cam_obj[camera_id]->work_ctrl.cond_v, NULL);
	//g_cam_ctrl.cam_obj[camera_id]->work_ctrl.pfds[0] = -1;
	//g_cam_ctrl.cam_obj[camera_id]->work_ctrl.pfds[1] = -1;
	//rc = mm_camera_open_pipe(g_cam_ctrl.cam_obj[camera_id]->work_ctrl.pfds);
	rc = pipe(g_cam_ctrl.cam_obj[camera_id]->work_ctrl.pfds);
	if(rc < 0) {
		CDBG("%s: camera_id = %d, pipe open rc=%d\n", __func__, camera_id, rc);
		rc = - MM_CAMERA_E_GENERAL;
	} else {
		(void)mm_camera_poll_task_launch(g_cam_ctrl.cam_obj[camera_id]);
		CDBG("%s: calling mm_camera_open()\n", __func__);
		rc = mm_camera_open(g_cam_ctrl.cam_obj[camera_id], op_mode);
		if(rc < 0) CDBG("%s: open failed\n", __func__);
	}
end:
	pthread_mutex_unlock(&g_mutex);

	CDBG("%s: END, rc=%d\n", __func__, rc);
	return rc;
}

static void mm_camera_ops_close (mm_camera_t * camera)
{
	mm_camera_obj_t * my_obj;
	int i;
	int8_t camera_id = camera->camera_info.camera_id;

	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera_id];
	if(!my_obj)
		goto err;
	my_obj->ref_count--;
	if(my_obj->ref_count > 0) {
		goto end;
	}
	if(my_obj->work_ctrl.pfds[0] > 0) {
		mm_camera_poll_task_release(my_obj);
		/* close pipe */
		close(my_obj->work_ctrl.pfds[0]);
		if(my_obj->work_ctrl.pfds[1] > 0)	close(my_obj->work_ctrl.pfds[1]);
		(void)mm_camera_close(g_cam_ctrl.cam_obj[camera_id]);
	}
	/* obj clean up */
	pthread_mutex_destroy(&my_obj->work_ctrl.mutex);
	pthread_cond_destroy(&my_obj->work_ctrl.cond_v);
	pthread_mutex_destroy(&my_obj->mutex);
	free(my_obj);
	g_cam_ctrl.cam_obj[camera_id] = NULL;
end:
	CDBG("%s: ref_count=%d\n", __func__, my_obj->ref_count);
err:
	pthread_mutex_unlock(&g_mutex);
}
static int32_t mm_camera_ops_ch_acquire(mm_camera_t * camera, mm_camera_channel_type_t ch_type)
{
	int32_t rc = -MM_CAMERA_E_GENERAL;
	mm_camera_obj_t * my_obj = NULL;
	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_ch_acquire(my_obj, ch_type);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;

}
static void mm_camera_ops_ch_release(mm_camera_t * camera, mm_camera_channel_type_t ch_type)
{
	mm_camera_obj_t * my_obj = NULL;
	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		mm_camera_ch_release(my_obj, ch_type);
		pthread_mutex_unlock(&my_obj->mutex);
	}
}

static int32_t mm_camera_ops_ch_attr(mm_camera_t * camera, 
																	mm_camera_channel_type_t ch_type, 
																	mm_camera_channel_attr_t *attr)
{
	mm_camera_obj_t * my_obj = NULL;
	int32_t rc;
	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_ch_fn(my_obj, ch_type, MM_CAMERA_STATE_EVT_ATTR, 
																	(void *)attr);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}

static mm_camera_ops_t mm_camera_ops = { 
  .is_op_supported = mm_camera_ops_is_op_supported,
	.action = mm_camera_ops_action,
	.open = mm_camera_ops_open,
	.close = mm_camera_ops_close,
	.ch_acquire = mm_camera_ops_ch_acquire,
	.ch_release = mm_camera_ops_ch_release,
	.ch_set_attr = mm_camera_ops_ch_attr
};

static uint8_t mm_camera_notify_is_event_supported(mm_camera_t * camera, 
																 mm_camera_event_type_t evt_type)
{
	return FALSE;
}

static int32_t mm_camera_notify_register_event_cb(mm_camera_t * camera, 
																mm_camera_event_notify_t evt_cb, 
																void * user_data, mm_camera_channel_type_t ch_type)
{
	return -1;
}

static int32_t mm_camera_register_buf_notify (mm_camera_t * camera, 
														 mm_camera_channel_type_t ch_type, 
														 mm_camera_buf_notify_t  buf_cb, 
														 void * user_data)
{
	mm_camera_obj_t * my_obj = NULL;
	mm_camera_buf_cb_t reg ;
	int rc = -1;

	reg.cb = buf_cb;
	reg.user_data = user_data;
	pthread_mutex_lock(&g_mutex);
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	pthread_mutex_unlock(&g_mutex);
	if(my_obj) {
		pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_ch_fn(my_obj,ch_type,
							MM_CAMERA_STATE_EVT_REG_BUF_CB,	(void *)&reg);
		pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}
static int32_t mm_camera_buf_done(mm_camera_t * camera, mm_camera_ch_data_buf_t * bufs)
{
	mm_camera_obj_t * my_obj = NULL;
	int rc = -1;
	my_obj = g_cam_ctrl.cam_obj[camera->camera_info.camera_id];
	if(my_obj) {
		//pthread_mutex_lock(&my_obj->mutex);
		rc = mm_camera_ch_fn(my_obj, bufs->type,
																	MM_CAMERA_STATE_EVT_QBUF,	(void *)bufs);
		//pthread_mutex_unlock(&my_obj->mutex);
	}
	return rc;
}

static mm_camera_notify_t mm_camera_notify = { 
	.is_event_supported = mm_camera_notify_is_event_supported,
  .register_event_notify = mm_camera_notify_register_event_cb,
	.register_buf_notify = mm_camera_register_buf_notify,
	.buf_done = mm_camera_buf_done
}; 

static uint8_t mm_camera_jpeg_is_jpeg_supported (mm_camera_t * camera)
{
	return FALSE;
}
static int32_t mm_camera_jpeg_set_parm (mm_camera_t * camera, 
																				mm_camera_jpeg_parm_type_t parm_type,	
																				void* p_value)
{
	return -1;
}
static int32_t mm_camera_jpeg_get_parm (mm_camera_t * camera, 
																				mm_camera_jpeg_parm_type_t parm_type,
																				void* p_value)
{
	return -1;
}
static int32_t mm_camera_jpeg_register_event_cb(mm_camera_t * camera, 
																								mm_camera_jpeg_cb_t * evt_cb, 
																								void * user_data)
{
	return -1;
}
static int32_t mm_camera_jpeg_encode (mm_camera_t * camera, uint8_t start, 
																 mm_camera_jpeg_encode_t *data)
{
	return -1;
}

static mm_camera_jpeg_t mm_camera_jpeg =  { 
  .is_jpeg_supported = mm_camera_jpeg_is_jpeg_supported,
  .set_parm = mm_camera_jpeg_set_parm,
  .get_parm = mm_camera_jpeg_get_parm,
  .register_event_cb = mm_camera_jpeg_register_event_cb,
	.encode = mm_camera_jpeg_encode,
}; 

extern mm_camera_t * mm_camera_query (uint8_t *num_cameras)
{
  int i = 0, rc = MM_CAMERA_OK;
  int server_fd = 0;
  struct msm_camera_info cameraInfo;

	if (!num_cameras)
			return NULL;
	/* lock the mutex */
	pthread_mutex_lock(&g_mutex);
	*num_cameras = 0;
	for(i = 0; i < MSM_MAX_CAMERA_SENSORS; i++) {
		cameraInfo.video_dev_name[i] = g_cam_ctrl.video_dev_name[i];
	}
	server_fd = open(MSM_CAMERA_SERVER, O_RDWR);
  if (server_fd <= 0) {
    CDBG_HIGH("%s: open '%s' failed. err='%s'", 
							__func__, MSM_CAMERA_SERVER, strerror(errno));
    goto end;
  }

  rc = ioctl(server_fd, MSM_CAM_IOCTL_GET_CAMERA_INFO, &cameraInfo);
	if(rc < 0) {
    CDBG_HIGH("%s: MSM_CAM_IOCTL_GET_CAMERA_INFO err=(%s)", 
							__func__, strerror(errno));
    goto end;
  }

  if(cameraInfo.num_cameras == 0) {
		CDBG("%s: num_camera_info=%d\n", __func__, cameraInfo.num_cameras);
		rc = -1;
		goto end;
	}
  CDBG("%s: num_camera_info=%d\n", __func__, cameraInfo.num_cameras);
  for (i=0; i < cameraInfo.num_cameras; i++) {
    g_cam_ctrl.camera[i].camera_info.camera_id = i;
    g_cam_ctrl.camera[i].camera_info.modes_supported =
      (!cameraInfo.has_3d_support[i]) ? CAMERA_MODE_2D :
      CAMERA_MODE_2D | CAMERA_MODE_3D;
    g_cam_ctrl.camera[i].camera_info.position =
      (cameraInfo.is_internal_cam[i]) ? FRONT_CAMERA : BACK_CAMERA;
    g_cam_ctrl.camera[i].camera_info.sensor_mount_angle = 
			cameraInfo.s_mount_angle[i];
		g_cam_ctrl.camera[i].video_dev_name = (char *)cameraInfo.video_dev_name[i];
		g_cam_ctrl.camera[i].sensor_type = cameraInfo.sensor_type[i];
		g_cam_ctrl.camera[i].cfg = &mm_camera_cfg; 
		g_cam_ctrl.camera[i].ops = &mm_camera_ops; 
		g_cam_ctrl.camera[i].evt = &mm_camera_notify; 
		g_cam_ctrl.camera[i].jpeg_ops = NULL;
		CDBG("%s: dev_info[id=%d,name='%s',pos=%d,modes=0x%x,sensor=%d]\n", 
				 __func__, i,
				 g_cam_ctrl.camera[i].video_dev_name, 
				 g_cam_ctrl.camera[i].camera_info.position, 
				 g_cam_ctrl.camera[i].camera_info.modes_supported, 
				 g_cam_ctrl.camera[i].sensor_type);
  }
	*num_cameras = cameraInfo.num_cameras; 
	g_cam_ctrl.num_cam = *num_cameras;
end:
	/* unlock the mutex */
	pthread_mutex_unlock(&g_mutex);
  if (server_fd > 0) 
		close(server_fd);
  CDBG("%s: num_cameras=%d\n", __func__, g_cam_ctrl.num_cam);
	if(rc == 0) 
		return &g_cam_ctrl.camera[0];
	else 
		return NULL;
}

