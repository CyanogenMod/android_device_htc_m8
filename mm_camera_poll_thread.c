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

typedef enum {
	MM_CAMERA_PIPE_CMD_REFRESH_FD,	/* add/del a stream into/from polling */
	MM_CAMERA_PIPE_CMD_EXIT,				/* exit */
	MM_CAMERA_PIPE_CMD_MAX					/* max count */
} mm_camera_pipe_cmd_type_t;

typedef enum {
	MM_CAMERA_POLL_TASK_STATE_POLL,		/* polling pid in polling state. */
	MM_CAMERA_POLL_TASK_STATE_MAX
} mm_camera_poll_task_state_type_t;

static int32_t mm_camera_poll_task_sig(mm_camera_obj_t * my_obj, 
																	mm_camera_pipe_cmd_type_t cmd)
{
	/* send through pipe */
	uint8_t tmp = (uint8_t)cmd;

	/* get the mutex */
	pthread_mutex_lock(&my_obj->work_ctrl.mutex);
	my_obj->work_ctrl.cmd = (uint8_t)cmd;
	/* reset the statue to false */
	my_obj->work_ctrl.worker_status = FALSE;
	/* send cmd to worker */
	write(my_obj->work_ctrl.pfds[1], &tmp,
		 sizeof(tmp));
	/* wait till worker task gives positive signal */
	if(FALSE == my_obj->work_ctrl.worker_status) {
		pthread_cond_wait(&my_obj->work_ctrl.cond_v, 
											&my_obj->work_ctrl.mutex);
	}
	/* done */
	pthread_mutex_unlock(&my_obj->work_ctrl.mutex);
	return MM_CAMERA_OK;
}

static void mm_camera_poll_task_sig_done(mm_camera_obj_t * my_obj)
{
  pthread_mutex_lock(&my_obj->work_ctrl.mutex);
  my_obj->work_ctrl.worker_status = TRUE;
  pthread_cond_signal(&my_obj->work_ctrl.cond_v);
  pthread_mutex_unlock(&my_obj->work_ctrl.mutex);
}

static void cm_camera_poll_task_set_state(	mm_camera_obj_t * my_obj, 
																			mm_camera_poll_task_state_type_t state)
{
	my_obj->work_ctrl.state = state;
}
static void mm_camera_poll_refresh_fd(mm_camera_obj_t * my_obj)
{
	int32_t i = 0;

	my_obj->work_ctrl.fd_cnt = 0;
	my_obj->work_ctrl.stream_type[my_obj->work_ctrl.fd_cnt] = MM_CAMERA_STREAM_PIPE;
	my_obj->work_ctrl.fds[my_obj->work_ctrl.fd_cnt++] = my_obj->work_ctrl.pfds[0];
	for(i = 1; i < MM_CAMERA_STREAM_MAX; i++) {
		if(my_obj->work_ctrl.poll_streams[i] == NULL)
			continue;
		my_obj->work_ctrl.stream_type[my_obj->work_ctrl.fd_cnt] = 
			my_obj->work_ctrl.poll_streams[i]->stream_type;
		my_obj->work_ctrl.fds[my_obj->work_ctrl.fd_cnt++] = 
			my_obj->work_ctrl.poll_streams[i]->fd;
	}
}

static int32_t mm_camera_poll_task_fn_poll_proc_msm(mm_camera_obj_t * my_obj, 
																						 struct pollfd *fds, int num_fds, 
																						 mm_camera_stream_type_t *type)
{
	int i;

	for(i = 1; i < num_fds; i++) {
		if((fds[i].revents & POLLIN) && (fds[i].revents & POLLRDNORM)) {
			CDBG("%s:data stream type=%d,fd=%d\n",__func__, type[i], fds[i].fd); 
			mm_camera_msm_data_notify(my_obj, fds[i].fd, type[i]);
		}
	}
	return 0;
}

static void mm_camera_poll_task_fn_poll_proc_pipe(mm_camera_obj_t * my_obj)
{
	ssize_t read_len;
	uint8_t cmd;
	read_len = read(my_obj->work_ctrl.pfds[0], &cmd, 
									sizeof(cmd));
	cmd = my_obj->work_ctrl.cmd;
	switch(cmd) {
	case MM_CAMERA_PIPE_CMD_REFRESH_FD:
		mm_camera_poll_refresh_fd(my_obj);
		mm_camera_poll_task_sig_done(my_obj);
		break;
	case MM_CAMERA_PIPE_CMD_EXIT:
	default:
		cm_camera_poll_task_set_state(my_obj, MM_CAMERA_POLL_TASK_STATE_MAX);
		mm_camera_poll_task_sig_done(my_obj);
		break;
	}
}

static void *mm_camera_poll_task_fn_poll(mm_camera_obj_t * my_obj)
{
  int rc = 0, i;
  struct pollfd fds[MM_CAMERA_STREAM_MAX];
  int timeoutms;
	ssize_t read_len;

	do {
		for(i = 0; i < my_obj->work_ctrl.fd_cnt; i++) {
			fds[i].fd = my_obj->work_ctrl.fds[i];
			fds[i].events = POLLIN|POLLRDNORM;
		}
		timeoutms = my_obj->work_ctrl.timeoutms;
		/* poll fds */
    rc = poll(fds, my_obj->work_ctrl.fd_cnt, timeoutms);
		if(rc > 0) {
			if((fds[0].revents & POLLIN) && (fds[0].revents & POLLRDNORM)) 
				mm_camera_poll_task_fn_poll_proc_pipe(my_obj);
			else 
				mm_camera_poll_task_fn_poll_proc_msm(my_obj, fds, 
																						 my_obj->work_ctrl.fd_cnt, 
																						 my_obj->work_ctrl.stream_type);
		} else {
			/* in error case sleep 10 us and then continue. hard coded here */
			usleep(10);
			continue;
		}
	} while (my_obj->work_ctrl.state == MM_CAMERA_POLL_TASK_STATE_POLL);
	return NULL;
}

static void *mm_camera_poll_task(void *data)
{
  int rc = 0;
	void *ret = NULL;
	mm_camera_obj_t * my_obj = (mm_camera_obj_t *)data;

	mm_camera_poll_refresh_fd(my_obj);
	cm_camera_poll_task_set_state(my_obj, MM_CAMERA_POLL_TASK_STATE_POLL);
  mm_camera_poll_task_sig_done(my_obj);
	do {
		ret = mm_camera_poll_task_fn_poll(my_obj);
	} while (my_obj->work_ctrl.state < MM_CAMERA_POLL_TASK_STATE_MAX);
	return ret;
}

int mm_camera_poll_task_launch(mm_camera_obj_t * my_obj)
{
	pthread_mutex_lock(&my_obj->work_ctrl.mutex);
	pthread_create(&my_obj->work_ctrl.pid, NULL, 
								 mm_camera_poll_task, 
								 (void *)my_obj);
	if(!my_obj->work_ctrl.worker_status) {
		pthread_cond_wait(&my_obj->work_ctrl.cond_v, 
											&my_obj->work_ctrl.mutex);
	}
	pthread_mutex_unlock(&my_obj->work_ctrl.mutex);
	return MM_CAMERA_OK;
}

int mm_camera_poll_task_release(mm_camera_obj_t * my_obj)
{
	CDBG("%s, my_obj=0x%x\n", __func__, (uint32_t)my_obj);
	mm_camera_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_EXIT);
	if (pthread_join(my_obj->work_ctrl.pid, NULL) != 0) {
		CDBG("%s: pthread dead already\n", __func__);
	}
	return MM_CAMERA_OK;
}

int mm_camera_poll_add_stream(mm_camera_obj_t * my_obj, mm_camera_stream_t *stream)
{
	my_obj->work_ctrl.poll_streams[stream->stream_type] = stream;
	mm_camera_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_REFRESH_FD);
	CDBG("%s:fd=%d,type=%d,done\n", __func__, stream->fd, stream->stream_type);
	return MM_CAMERA_OK;
}

int mm_camera_poll_del_stream(mm_camera_obj_t * my_obj, mm_camera_stream_t *stream)
{
	my_obj->work_ctrl.poll_streams[stream->stream_type] = NULL;
	mm_camera_poll_task_sig(my_obj, MM_CAMERA_PIPE_CMD_REFRESH_FD);
	CDBG("%s: fd=%d,type=%d, done\n", __func__, stream->fd, stream->stream_type);
	return MM_CAMERA_OK;
}

int mm_camera_poll_busy(mm_camera_obj_t * my_obj)
{
	int busy = 0;
	busy = (my_obj->work_ctrl.fd_cnt > 1)? TRUE:FALSE;
	return busy;
}




