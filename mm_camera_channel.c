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

/* static functions prototype declarations */
static int mm_camera_channel_deq_x_frames(mm_camera_obj_t *my_obj,
                                          mm_camera_frame_queue_t *mq,
                                          mm_camera_frame_queue_t *sq,
                                          mm_camera_stream_t *mstream,
                                          mm_camera_stream_t *sstream,
                                          int count);
static int mm_camera_channel_get_starting_frame(mm_camera_obj_t *my_obj,
                                                mm_camera_ch_t *ch,
                                                mm_camera_stream_t *mstream,
                                                mm_camera_stream_t *sstream,
                                                mm_camera_frame_queue_t *mq,
                                                mm_camera_frame_queue_t *sq,
                                                mm_camera_frame_t **mframe,
                                                mm_camera_frame_t **sframe);
static int mm_camera_ch_search_frame_based_on_time(mm_camera_obj_t *my_obj,
                                                   mm_camera_ch_t *ch,
                                                   mm_camera_stream_t *mstream,
                                                   mm_camera_stream_t *sstream,
                                                   mm_camera_frame_queue_t *mq,
                                                   mm_camera_frame_queue_t *sq,
                                                   mm_camera_frame_t **mframe,
                                                   mm_camera_frame_t **sframe);



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
    if(stream1) rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream1,
                                            MM_CAMERA_STATE_EVT_ACQUIRE, &type1);
    if(stream2 && !rc) rc = mm_camera_stream_fsm_fn_vtbl(my_obj, stream2,
                                            MM_CAMERA_STATE_EVT_ACQUIRE, &type2);
    if(rc == MM_CAMERA_OK) {
        if(!my_obj->ch[ch_type].acquired)   my_obj->ch[ch_type].acquired = TRUE;
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
    /*if(ch_type != MM_CAMERA_CH_RAW) {
        CDBG("%s: attr type %d not support for ch %d\n", __func__, val->type, ch_type);
        return rc;
    }*/
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

int mm_camera_channel_get_time_diff(struct timespec *cur_ts, int usec_target, struct timespec *frame_ts)
{
    int dtusec = (cur_ts->tv_nsec - frame_ts->tv_nsec)/1000;
    dtusec += (cur_ts->tv_sec - frame_ts->tv_sec)*1000000 - usec_target;
    return dtusec;
}

static int mm_camera_channel_deq_x_frames(mm_camera_obj_t *my_obj,
                                          mm_camera_frame_queue_t *mq,
                                          mm_camera_frame_queue_t *sq,
                                          mm_camera_stream_t *mstream,
                                          mm_camera_stream_t *sstream,
                                          int count)
{
    int rc = MM_CAMERA_OK;
    int i = 0;
    mm_camera_frame_t *mframe, *sframe;
    CDBG("%s: Dequeue %d frames from the queue", __func__, count);
    for(i=0; i < count; i++) {
        mframe = mm_camera_stream_frame_deq(mq);
        if(mframe) {
            mm_camera_stream_qbuf(my_obj, mstream, mframe->idx);
        }
        sframe = mm_camera_stream_frame_deq(sq);
        if(sframe) {
            mm_camera_stream_qbuf(my_obj, sstream, sframe->idx);
        }
    }

    return rc;
}

static int mm_camera_ch_search_frame_based_on_time(mm_camera_obj_t *my_obj,
                                                   mm_camera_ch_t *ch,
                                                   mm_camera_stream_t *mstream,
                                                   mm_camera_stream_t *sstream,
                                                   mm_camera_frame_queue_t *mq,
                                                   mm_camera_frame_queue_t *sq,
                                                   mm_camera_frame_t **mframe,
                                                   mm_camera_frame_t **sframe)
{
    int mcnt, i, rc = MM_CAMERA_OK;
    int enq_count = 0;
    int dt1, dt2;
    mm_camera_frame_t *mframe1, *sframe1, *qmframe = NULL;
    mm_camera_frame_t *mframe2, *sframe2, *qsframe = NULL;
    mm_camera_frame_t *mframe_a[ZSL_INTERNAL_QUEUE_SIZE];
    mm_camera_frame_t *sframe_a[ZSL_INTERNAL_QUEUE_SIZE];
    struct timespec base_ts;


    memset(mframe_a, 0, sizeof(mframe_a));
    memset(sframe_a, 0, sizeof(sframe_a));

    *mframe = *sframe = NULL;

    mcnt = mm_camera_stream_frame_get_q_cnt(mq);

    /* We'll need to dequeue all the frames first so that we'll be able to
       view their time stamp
       Note: We have to do this because we need a base timestamp. We cannot
       use gettimeofday() to get base timestamp because it doesn't give similar
       value as the one used to create frame timestamp -
       kernel uses ktime_get_ts() to create frame timestamp.*/

    for(i=0; i<mcnt; i++) {
        mframe_a[i] = mm_camera_stream_frame_deq(mq);
        sframe_a[i] = mm_camera_stream_frame_deq(sq);
        /* CDBG("%s: Frame deq'd: main: %d  thumb: %d", __func__,
             mframe_a[i]->idx, sframe_a[i]->idx); */
    }

    /* We'll be using timestamp of the latest frame as
       base timestamp. */
    base_ts = mframe_a[mcnt-1]->frame.ts;

    /* Once we get the right frame, we'll need to enqueue remaining
       frames again for the calling function. enq_count will be used
       to keep track of how many valid frames we need to enqueue again */
    enq_count = 1;

    mframe1 = mframe_a[0];
    sframe1 = sframe_a[0];

    if(!mframe1 || !sframe1) {
        CDBG("%s: no frames raedy.\n", __func__);
        qmframe = mframe1;
        qsframe = sframe1;
        rc = -1;
        goto end;
    }

    dt1 = mm_camera_channel_get_time_diff(&base_ts,
                                          ch->buffering_frame.look_back * 1000,
                                          &mframe1->frame.ts);

    /*CDBG("%s:Base time: %lu usec look-back time: %d us",
         __func__, (unsigned long)(base_ts.tv_sec * 1000000 + (base_ts.tv_nsec/1000)),
         ch->buffering_frame.look_back * 1000);
    CDBG("%s: Frame (id:%d)timestamp: %lu usec", __func__, mframe1->idx,
         (unsigned long)((mframe1->frame.ts.tv_sec * 1000000) +
                         (mframe1->frame.ts.tv_nsec/1000)));
    CDBG("%s: dt1= %d", __func__, dt1);*/

    if (dt1 > 0) {
        for(i = 1; i < mcnt; i++) {
            enq_count = i + 1;

            /* be sure to queue old frames back to kernel */
            if(qmframe) {
                mm_camera_stream_qbuf(my_obj, mstream, qmframe->idx);
                qmframe = NULL;
            }
            if(qsframe) {
                mm_camera_stream_qbuf(my_obj, sstream, qsframe->idx);
                qsframe = NULL;
            }

            mframe2 = mframe_a[i];
            sframe2 = sframe_a[i];
            if(!mframe2 || !sframe2) {
                qmframe = mframe2;
                qsframe = sframe2;
                goto done;
            } else {
                /*CDBG("%s: Frame (id: %d) timestamp: %lu usec", __func__,
                     mframe2->idx,
                     (unsigned long)((mframe2->frame.ts.tv_sec * 1000000) +
                                     (mframe2->frame.ts.tv_nsec/1000)));*/
                dt2 = mm_camera_channel_get_time_diff(&base_ts,
                                                      ch->buffering_frame.look_back * 1000,
                                                      &mframe2->frame.ts);
                /*CDBG("%s: dt2= %d", __func__, dt2);*/
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
    }


done:
    *mframe = mframe1;
    *sframe = sframe1;

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

    /* Enqueuing rest of the valid frames again in the queue*/
    for(i = enq_count; i < mcnt; i++) {
        /*CDBG("%s: Enqueuing frame %d", __func__, mframe_a[i]->idx);*/
        mm_camera_stream_frame_enq(mq, &mstream->frame.frame[mframe_a[i]->idx]);
        mm_camera_stream_frame_enq(sq, &sstream->frame.frame[sframe_a[i]->idx]);
    }

    return rc;
}

static int mm_camera_channel_get_starting_frame(mm_camera_obj_t *my_obj,
                                                mm_camera_ch_t *ch,
                                                mm_camera_stream_t *mstream,
                                                mm_camera_stream_t *sstream,
                                                mm_camera_frame_queue_t *mq,
                                                mm_camera_frame_queue_t *sq,
                                                mm_camera_frame_t **mframe,
                                                mm_camera_frame_t **sframe)
{
    int mcnt = 0;
    int rc = MM_CAMERA_OK;

    *mframe = *sframe = NULL;
    mcnt = mm_camera_stream_frame_get_q_cnt(mq);

    CDBG("%s: Total frames in the queue: %d", __func__, mcnt);

    if(!mcnt) {
        CDBG("%s: Currently ZSL queue empty! Returning!", __func__);
        goto end;
    }
    /* If user just wants us to give top of the queue without any drama,
       just dequeu and give it back */
    if(ch->buffering_frame.give_top_of_queue) {
        CDBG("%s: Giving back top of the queue", __func__);
        goto dequeue_frame;
    }
    /* If look_back value is 0, we'll just return the most recent frame in
       the queue */
    if(ch->buffering_frame.look_back == 0) {
        CDBG("%s: Look-back value is 0. Dequeue all the frames except last", __func__);
        mm_camera_channel_deq_x_frames(my_obj,
                                       mq, sq,
                                       mstream, sstream,
                                       mcnt-1);
        /* We have now right frame at top of the queue. We'll dequeue it
           now*/
        goto dequeue_frame;
    }
    else {
        /* If we are supposed to find the frame based on user specified
           frame count */
        if(ch->buffering_frame.dispatch_type == ZSL_LOOK_BACK_MODE_COUNT){
            CDBG("%s: Look-back based on frame count.", __func__);
            /* if number of frames currently available in the queue is less
               than the lookback value, we'll just return the top of the queue
               (or shall we return error?) */
            if(mcnt < ch->buffering_frame.look_back) {
                goto dequeue_frame;
            }
            else {
                /* dequeue and release the buffers to kernel from top of the
                   queue till we reach the one we want*/
                mm_camera_channel_deq_x_frames(my_obj,
                                               mq, sq,
                                               mstream, sstream,
                                               (mcnt - ch->buffering_frame.look_back));
                goto dequeue_frame;
            }
        }
        /* search frame based on timestamps*/
        else {
            CDBG("%s: Look-back based on timestamp.", __func__);
            mm_camera_ch_search_frame_based_on_time(my_obj, ch,
                                                   mstream, sstream,
                                                   mq, sq,
                                                   mframe, sframe);
            goto end;
        }
    }

dequeue_frame:
    *mframe = mm_camera_stream_frame_deq(mq);
    *sframe = mm_camera_stream_frame_deq(sq);

    CDBG("%s: Finally: frame id of: main - %d  thumbnail - %d", __func__, (*mframe)->idx, (*sframe)->idx);
end:
    return rc;
}
/*for ZSL mode to send the image pair to client*/
void mm_camera_dispatch_buffered_frames(mm_camera_obj_t *my_obj,
                                        mm_camera_channel_type_t ch_type)
{
    int mcnt, i, rc = MM_CAMERA_E_GENERAL;
    int num_of_req_frame = 0;
    int cb_sent = 0;
    mm_camera_ch_data_buf_t data;
    mm_camera_frame_t *mframe = NULL, *sframe = NULL;
    mm_camera_frame_t *qmframe = NULL, *qsframe = NULL;
    mm_camera_ch_t *ch = &my_obj->ch[ch_type];
    mm_camera_frame_queue_t *mq = NULL;
    mm_camera_frame_queue_t *sq = NULL;
    mm_camera_stream_t *stream1 = NULL;
    mm_camera_stream_t *stream2 = NULL;

    mm_camera_ch_util_get_stream_objs(my_obj, ch_type, &stream1, &stream2);
    if(stream1) {
      mq = &stream1->frame.readyq;
    }
    if(stream2) {
      sq = &stream2->frame.readyq;
    }

    pthread_mutex_lock(&ch->mutex);

    if (mq && sq && stream1 && stream2) {
      /* Some requirements are such we'll first need to empty the buffered
         queue - like for HDR. If we need to empty the buffered queue first,
         let's empty it here.*/
      if(ch->buffering_frame.empty_queue) {
          CDBG("%s: Emptying the queue first before dispatching!", __func__);
          mcnt = mm_camera_stream_frame_get_q_cnt(mq);
          mm_camera_channel_deq_x_frames(my_obj, mq, sq, stream1, stream2, mcnt);
          ch->buffering_frame.empty_queue = 0;
          goto end;
      }

      rc = mm_camera_channel_get_starting_frame(my_obj, ch,
                                                stream1, stream2,
                                                mq, sq, &mframe, &sframe);
      if(rc != MM_CAMERA_OK) {
          CDBG_ERROR("%s: Error getting right frame!", __func__);
          goto end;
      }

      if((mframe == NULL) || (sframe == NULL)) {
          CDBG("%s: Failed to get correct main and thumbnail frames!", __func__);
          goto end;
      }

      CDBG("%s: Received frame id of: main - %d  thumbnail - %d",
           __func__, mframe->idx, sframe->idx);

      /* So total number of frames available - total in the queue plus already dequeud one*/
      mcnt = mm_camera_stream_frame_get_q_cnt(mq) + 1;
      num_of_req_frame = ch->snapshot.num_shots;

      CDBG("%s: Number of shots requested: %d Frames available: %d",
           __func__, num_of_req_frame, mcnt);

      /* Limit number of callbacks to actual frames in the queue */
      if (num_of_req_frame > mcnt) {
          num_of_req_frame = mcnt;
      }
      for(i = 0; i < num_of_req_frame; i++) {
          if(mframe && sframe) {
              CDBG("%s: Dequeued frame: main frame-id: %d thumbnail frame-id: %d", __func__, mframe->idx, sframe->idx);
              /* dispatch this pair of frames */
              memset(&data, 0, sizeof(data));
              data.type = ch_type;
              data.snapshot.main.frame = &mframe->frame;
              data.snapshot.main.idx = mframe->idx;
              data.snapshot.thumbnail.frame = &sframe->frame;
              data.snapshot.thumbnail.idx = sframe->idx;
              stream1->frame.ref_count[data.snapshot.main.idx]++;
              stream2->frame.ref_count[data.snapshot.thumbnail.idx]++;
              ch->buf_cb.cb(&data, ch->buf_cb.user_data);
              cb_sent++;
          } else {
              qmframe = mframe;
              qsframe = sframe;
              rc = -1;
              break;
          }

          mframe = mm_camera_stream_frame_deq(mq);
          sframe = mm_camera_stream_frame_deq(sq);
      }
      if(qmframe) {
          mm_camera_stream_frame_enq(mq, &stream1->frame.frame[qmframe->idx]);
          qmframe = NULL;
      }
      if(qsframe) {
          mm_camera_stream_frame_enq(sq, &stream2->frame.frame[qsframe->idx]);
          qsframe = NULL;
      }

      /* Save number of remaining snapshots */
      ch->snapshot.num_shots -= cb_sent;
    }

    CDBG("%s: Number of remaining snapshots: %d", __func__,
        ch->snapshot.num_shots);
end:
    pthread_mutex_unlock(&ch->mutex);

    /* If there are still some more images left to be captured, we'll call this
       function again. That time we don't need to find the right frame based
       on time or frame count. We just need to start from top of the frame */
    if(ch->snapshot.num_shots) {
        ch->buffering_frame.give_top_of_queue = 1;
        mm_camera_event_t data;
        data.event_type = MM_CAMERA_EVT_TYPE_CH;
        data.e.ch.evt = MM_CAMERA_CH_EVT_DATA_REQUEST_MORE;
        data.e.ch.ch = ch_type;
        mm_camera_poll_send_ch_event(my_obj, &data);
    }

    /* If we are done sending callbacks for all the requested number of snapshots
       send data delivery done event*/
    if((rc == MM_CAMERA_OK) && (!ch->snapshot.num_shots)) {
        mm_camera_event_t data;
        data.event_type = MM_CAMERA_EVT_TYPE_CH;
        data.e.ch.evt = MM_CAMERA_CH_EVT_DATA_DELIVERY_DONE;
        data.e.ch.ch = ch_type;
        mm_camera_poll_send_ch_event(my_obj, &data);
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
    case MM_CAMERA_STATE_EVT_STREAM_ON: {
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
        if(rc < 0) {
          mm_camera_poll_thread_release(my_obj, ch_type);
        }
        break;
    }
    case MM_CAMERA_STATE_EVT_STREAM_OFF: {
        mm_camera_poll_thread_release(my_obj, ch_type);
        rc = mm_camera_ch_util_stream_null_val(my_obj, ch_type, evt, NULL);
        break;
    }
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
