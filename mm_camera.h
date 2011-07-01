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

#ifndef __MM_CAMERA_H__
#define __MM_CAMERA_H__

typedef enum {
	MM_CAMERA_STREAM_STATE_NOTUSED,		/* not used */
	MM_CAMERA_STREAM_STATE_ACQUIRED,	/* acquired, fd opened  */
	MM_CAMERA_STREAM_STATE_CFG,				/* fmt & dim configured */
	MM_CAMERA_STREAM_STATE_REG,				/* buf regged, stream off */
	MM_CAMERA_STREAM_STATE_ACTIVE,		/* stream on */
	MM_CAMERA_STREAM_STATE_MAX
} mm_camera_stream_state_type_t;

typedef enum {
	MM_CAMERA_STATE_EVT_NOTUSED,
	MM_CAMERA_STATE_EVT_ACQUIRE,
	MM_CAMERA_STATE_EVT_ATTR,
	MM_CAMERA_STATE_EVT_RELEASE,
	MM_CAMERA_STATE_EVT_REG_BUF_CB,
	MM_CAMERA_STATE_EVT_SET_FMT,
	MM_CAMERA_STATE_EVT_SET_DIM,
	MM_CAMERA_STATE_EVT_REG_BUF,
	MM_CAMERA_STATE_EVT_UNREG_BUF,
	MM_CAMERA_STATE_EVT_STREAM_ON,
	MM_CAMERA_STATE_EVT_STREAM_OFF,
	MM_CAMERA_STATE_EVT_QBUF,
	MM_CAMERA_STATE_EVT_MAX
} mm_camera_state_evt_type_t;

typedef struct {
	mm_camera_event_notify_t evt_cb;
	void * user_data;
} mm_camera_notify_cb_t;

typedef struct {
	mm_camera_buf_notify_t cb;
	void *user_data;
} mm_camera_buf_cb_t;

typedef enum {
	MM_CAMERA_STREAM_PIPE,
	MM_CAMERA_STREAM_PREVIEW,
	MM_CAMERA_STREAM_VIDEO,
	MM_CAMERA_STREAM_SNAPSHOT,
	MM_CAMERA_STREAM_THUMBNAIL,
	MM_CAMERA_STREAM_RAW,
	MM_CAMERA_STREAM_ZSL_MAIN,
	MM_CAMERA_STREAM_ZSL_POST_VIEW,
	MM_CAMERA_STREAM_VIDEO_MAIN,
	MM_CAMERA_STREAM_MAX
} mm_camera_stream_type_t;

typedef struct mm_camera_frame_t mm_camera_frame_t;
struct mm_camera_frame_t{
	struct msm_frame frame;
	int idx;
	mm_camera_frame_t *next;
};

typedef struct {
	pthread_mutex_t mutex;
	int cnt;
	mm_camera_frame_t *head;
	mm_camera_frame_t *tail;
} mm_camera_frame_queue_t;

typedef struct {
	mm_camera_frame_queue_t readyq;
	mm_camera_frame_queue_t freeq;
	int32_t num_frame;
	uint32_t frame_len;
	int8_t reg_flag;
	uint32_t frame_offset[MM_CAMERA_MAX_NUM_FRAMES];
	mm_camera_frame_t frame[MM_CAMERA_MAX_NUM_FRAMES];
	int8_t ref_count[MM_CAMERA_MAX_NUM_FRAMES];
	int32_t use_multi_fd;
	int qbuf;
  int no_buf;
	pthread_mutex_t mutex;
} mm_camera_stream_frame_t;

typedef struct {
	int32_t fd;
	mm_camera_stream_state_type_t state;
	mm_camera_stream_type_t stream_type;
	struct v4l2_format fmt;
	cam_format_t cam_fmt; 
	mm_camera_stream_frame_t frame;
} mm_camera_stream_t;

typedef struct {
	mm_camera_stream_t stream;
	mm_camera_raw_streaming_type_t mode;
} mm_camera_ch_raw_t;

typedef struct {
	mm_camera_stream_t stream;
} mm_camera_ch_preview_t;

typedef struct {
	mm_camera_stream_t thumbnail;
	mm_camera_stream_t main;
	int8_t num_shots;
} mm_camera_ch_snapshot_t;

typedef struct {
	int8_t fifo[MM_CAMERA_MAX_FRAME_NUM];
	int8_t low;
	int8_t high;
	int8_t len;
	int8_t water_mark;
} mm_camera_circule_fifo_t;

typedef struct {
	mm_camera_stream_t main;
	mm_camera_stream_t postview;
} mm_camera_ch_zsl_t;

typedef struct {
	mm_camera_stream_t video;
	mm_camera_stream_t main;
	uint8_t has_main;
} mm_camera_ch_video_t;

typedef struct {
	mm_camera_channel_type_t type;
	pthread_mutex_t mutex;
	uint8_t acquired;
	mm_camera_buf_cb_t buf_cb;
	union {
		mm_camera_ch_raw_t raw;
		mm_camera_ch_preview_t preview;
		mm_camera_ch_snapshot_t snapshot;
		mm_camera_ch_zsl_t zsl;
		mm_camera_ch_video_t video;
	};
} mm_camera_ch_t;

typedef struct {
	int32_t pfds[2];
	int state;
	pthread_t pid;
	pthread_mutex_t mutex;
	pthread_cond_t cond_v;
	uint8_t cmd;
	mm_camera_stream_t *poll_streams[MM_CAMERA_STREAM_MAX];
  int fds[MM_CAMERA_STREAM_MAX];
	mm_camera_stream_type_t stream_type[MM_CAMERA_STREAM_MAX];
	int fd_cnt;
	int32_t worker_status;
	int timeoutms;
} mm_camera_work_ctrl_t;

typedef struct {
	int8_t my_id;
	camera_mode_t current_mode;
	mm_camera_op_mode_type_t op_mode;
	mm_camera_notify_cb_t *notify;
	mm_camera_ch_t ch[MM_CAMERA_CH_MAX];
	mm_camera_work_ctrl_t work_ctrl;
	int ref_count;
	uint32_t ch_streaming_mask;
	int32_t ctrl_fd;
	cam_ctrl_dimension_t dim;
	pthread_mutex_t mutex;
} mm_camera_obj_t;

#define MM_CAMERA_DEV_NAME_LEN 32

typedef struct {
	mm_camera_t camera[MSM_MAX_CAMERA_SENSORS];
	int8_t num_cam;
	char video_dev_name[MSM_MAX_CAMERA_SENSORS][MM_CAMERA_DEV_NAME_LEN];
	mm_camera_obj_t *cam_obj[MSM_MAX_CAMERA_SENSORS]; 
} mm_camera_ctrl_t;

typedef struct {
 mm_camera_parm_type_t parm_type;
 void *p_value;
} mm_camera_parm_t;

extern int32_t mm_camera_stream_fsm_fn_vtbl (mm_camera_obj_t * my_obj, 
											mm_camera_stream_t *stream, 
											mm_camera_state_evt_type_t evt, void *val);
extern const char *mm_camera_util_get_dev_name(mm_camera_obj_t * my_obj);
extern int32_t mm_camera_util_s_ctrl( int32_t fd, 
											uint32_t id, int32_t value);
extern int32_t mm_camera_util_g_ctrl( int32_t fd, 
											uint32_t id, int32_t *value);
extern int32_t mm_camera_ch_fn(mm_camera_obj_t * my_obj,
											mm_camera_channel_type_t ch_type, 
											mm_camera_state_evt_type_t evt, void *val);
extern int32_t mm_camera_action(mm_camera_obj_t *my_obj, uint8_t start,
											mm_camera_ops_type_t opcode, void *parm);
extern int32_t mm_camera_open(mm_camera_obj_t *my_obj, 
											mm_camera_op_mode_type_t op_mode);
extern int32_t mm_camera_close(mm_camera_obj_t *my_obj);
extern int32_t mm_camera_start(mm_camera_obj_t *my_obj, 
											mm_camera_ops_type_t opcode, void *parm);
extern int32_t mm_camera_stop(mm_camera_obj_t *my_obj, 
											mm_camera_ops_type_t opcode, void *parm);
extern int32_t mm_camera_get_parm(mm_camera_obj_t * my_obj, 
											mm_camera_parm_t *parm);
extern int32_t mm_camera_set_parm(mm_camera_obj_t * my_obj, 
											mm_camera_parm_t *parm);
extern int32_t mm_camera_prepare_buf(mm_camera_obj_t * my_obj, mm_camera_reg_buf_t *buf);
extern int32_t mm_camera_unprepare_buf(mm_camera_obj_t * my_obj, mm_camera_channel_type_t ch_type);
extern int mm_camera_poll_busy(mm_camera_obj_t * my_obj);
//extern int32_t mm_camera_send_poll_task_sig(mm_camera_obj_t * my_obj, 
//											mm_camera_pipe_cmd_type_t cmd);
//extern void *mm_camera_poll_task(void *data);
extern int mm_camera_poll_task_launch(mm_camera_obj_t * my_obj);
extern int mm_camera_poll_task_release(mm_camera_obj_t * my_obj);
extern void mm_camera_msm_data_notify(mm_camera_obj_t * my_obj, int fd, 
											mm_camera_stream_type_t stream_type);
extern int mm_camera_read_msm_frame(mm_camera_obj_t * my_obj, 
						mm_camera_stream_t *stream);
extern int32_t mm_camera_ch_acquire(mm_camera_obj_t *my_obj, mm_camera_channel_type_t ch_type);
extern void mm_camera_ch_release(mm_camera_obj_t *my_obj, mm_camera_channel_type_t ch_type);
extern int mm_camera_ch_is_active(mm_camera_obj_t * my_obj, mm_camera_channel_type_t ch_type);

extern int mm_camera_stream_qbuf(mm_camera_obj_t * my_obj, 
															mm_camera_stream_t *stream, 
															int idx);
extern int mm_camera_stream_frame_get_q_cnt(mm_camera_frame_queue_t *q);
extern mm_camera_frame_t *mm_camera_stream_frame_deq(mm_camera_frame_queue_t *q);
extern void mm_camera_stream_frame_enq(mm_camera_frame_queue_t *q, mm_camera_frame_t *node);
extern void mm_camera_stream_frame_refill_q(mm_camera_frame_queue_t *q, mm_camera_frame_t *node, int num);
extern int mm_camera_stream_is_active(mm_camera_stream_t *stream);
extern int mm_camera_poll_add_stream(mm_camera_obj_t * my_obj, mm_camera_stream_t *stream);
extern int mm_camera_poll_del_stream(mm_camera_obj_t * my_obj, mm_camera_stream_t *stream);
extern int mm_camera_dev_open(int *fd, char *dev_name);

#endif /* __MM_CAMERA_H__ */









