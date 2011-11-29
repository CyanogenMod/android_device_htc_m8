/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <fcntl.h>
#include "OMX_Types.h"
#include "OMX_Index.h"
#include "OMX_Core.h"
#include "OMX_Component.h"
#include "omx_debug.h"
#include "omx_jpeg_ext.h"
#include "mm_omx_jpeg_encoder.h"

#ifdef HW_ENCODE
static uint8_t hw_encode = true;
#else
static uint8_t hw_encode = false;
#endif

static int jpegRotation = 0;
static int jpegThumbnailQuality = 75;
static int jpegMainimageQuality = 85;
static uint32_t phy_offset;
void *user_data;

static int encoding = 0;

jpegfragment_callback_t mmcamera_jpegfragment_callback;
jpeg_callback_t mmcamera_jpeg_callback;


#define INPUT_PORT 0
#define OUTPUT_PORT 1
#define INPUT_PORT1 2
#define DEFAULT_COLOR_FORMAT YCRCBLP_H2V2


static OMX_HANDLETYPE pHandle;
static OMX_CALLBACKTYPE callbacks;
static OMX_INDEXTYPE type;
static omx_jpeg_exif_info_tag tag;
static OMX_CONFIG_ROTATIONTYPE rotType;
static omx_jpeg_thumbnail thumbnail;
static OMX_CONFIG_RECTTYPE recttype;
static OMX_PARAM_PORTDEFINITIONTYPE * inputPort;
static OMX_PARAM_PORTDEFINITIONTYPE * outputPort;
static OMX_PARAM_PORTDEFINITIONTYPE * inputPort1;
static OMX_BUFFERHEADERTYPE* pInBuffers;
static OMX_BUFFERHEADERTYPE* pOutBuffers;
static OMX_BUFFERHEADERTYPE* pInBuffers1;
OMX_INDEXTYPE user_preferences;
omx_jpeg_user_preferences userpreferences;


static pthread_mutex_t lock;
static pthread_cond_t cond;
static int expectedEvent = 0;
static int expectedValue1 = 0;
static int expectedValue2 = 0;
static omx_jpeg_pmem_info pmem_info;
static omx_jpeg_pmem_info pmem_info1;
static OMX_IMAGE_PARAM_QFACTORTYPE qFactor;
static omx_jpeg_thumbnail_quality thumbnailQuality;
static OMX_INDEXTYPE thumbnailQualityType;
static void *out_buffer;
static int * out_buffer_size;
static OMX_INDEXTYPE buffer_offset;
static omx_jpeg_buffer_offset bufferoffset;

void set_callbacks(
    jpegfragment_callback_t fragcallback,
    jpeg_callback_t eventcallback, void* userdata,
    void* output_buffer,
    int * outBufferSize) {
    mmcamera_jpegfragment_callback = fragcallback;
    mmcamera_jpeg_callback = eventcallback;
    user_data = userdata;
    out_buffer = output_buffer;
    out_buffer_size = outBufferSize;
}


OMX_ERRORTYPE etbdone(OMX_OUT OMX_HANDLETYPE hComponent,
                      OMX_OUT OMX_PTR pAppData,
                      OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
{
    pthread_mutex_lock(&lock);
    expectedEvent = OMX_EVENT_ETB_DONE;
    expectedValue1 = 0;
    expectedValue2 = 0;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
    return 0;
}

OMX_ERRORTYPE ftbdone(OMX_OUT OMX_HANDLETYPE hComponent,
                      OMX_OUT OMX_PTR pAppData,
                      OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
{
    LOGE("%s", __func__);
    *out_buffer_size = pBuffer->nFilledLen;
    pthread_mutex_lock(&lock);
    expectedEvent = OMX_EVENT_FTB_DONE;
    expectedValue1 = 0;
    expectedValue2 = 0;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
    LOGE("filled len = %u", (uint32_t)pBuffer->nFilledLen);
    if (mmcamera_jpeg_callback && encoding)
        mmcamera_jpeg_callback(0, user_data);
    return 0;
}

OMX_ERRORTYPE handleError(OMX_IN OMX_EVENTTYPE eEvent, OMX_IN OMX_U32 error)
{
    LOGE("%s", __func__);
    if (error == OMX_EVENT_JPEG_ERROR) {
        if (mmcamera_jpeg_callback && encoding) {
            LOGE("handleError : OMX_EVENT_JPEG_ERROR\n");
            mmcamera_jpeg_callback(JPEG_EVENT_ERROR, user_data);
        }
    } else if (error == OMX_EVENT_THUMBNAIL_DROPPED) {
        if (mmcamera_jpeg_callback && encoding) {
            LOGE("handleError :(OMX_EVENT_THUMBNAIL_DROPPED\n");
            mmcamera_jpeg_callback(JPEG_EVENT_THUMBNAIL_DROPPED, user_data);
        }
    }
    return 0;
}

OMX_ERRORTYPE eventHandler( OMX_IN OMX_HANDLETYPE hComponent,
                            OMX_IN OMX_PTR pAppData, OMX_IN OMX_EVENTTYPE eEvent,
                            OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                            OMX_IN OMX_PTR pEventData)
{
    LOGE("%s", __func__);
    LOGE("event handler got event %u ndata1 %u ndata2 %u", eEvent, nData1, nData2);
    pthread_mutex_lock(&lock);
    expectedEvent = eEvent;
    expectedValue1 = nData1;
    expectedValue2 = nData2;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
    if ((nData1== OMX_EVENT_JPEG_ERROR)||(nData1== OMX_EVENT_THUMBNAIL_DROPPED))
        handleError(eEvent,nData1);
    return 0;
}

void waitForEvent(int event, int value1, int value2 ){
    pthread_mutex_lock(&lock);
    while (! (expectedEvent == event &&
    expectedValue1 == value1 && expectedValue2 == value2)) {
        pthread_cond_wait(&cond, &lock);
    }
    pthread_mutex_unlock(&lock);
}

int8_t mm_jpeg_encoder_get_buffer_offset(uint32_t width, uint32_t height,
                                         uint32_t* p_y_offset, uint32_t* p_cbcr_offset,
                                         uint32_t* p_buf_size, uint8_t *num_planes,
                                         uint32_t planes[])
{
    LOGE("jpeg_encoder_get_buffer_offset");
    if ((NULL == p_y_offset) || (NULL == p_cbcr_offset)) {
        return FALSE;
    }
    /* Hardcode num planes and planes array for now. TBD Check if this
     needs to be set based on format. */
    *num_planes = 2;
    if (hw_encode) {
        int cbcr_offset = 0;
        uint32_t actual_size = width*height;
        uint32_t padded_size = width * CEILING16(height);
        *p_y_offset = 0;
        *p_cbcr_offset = 0;
        if ((jpegRotation == 90) || (jpegRotation == 180)) {
            *p_y_offset = padded_size - actual_size;
            *p_cbcr_offset = ((padded_size - actual_size) >> 1);
        }
        *p_buf_size = padded_size * 3/2;
        planes[0] = width * CEILING16(height);
        planes[1] = width * CEILING16(height)/2;
    } else {
        *p_y_offset = 0;
        *p_cbcr_offset = PAD_TO_WORD(width*height);
        *p_buf_size = *p_cbcr_offset * 3/2;
        planes[0] = PAD_TO_WORD(width*CEILING16(height));
        planes[1] = PAD_TO_WORD(width*CEILING16(height)/2);
    }
    return TRUE;
}

int8_t omxJpegInit()
{
    LOGE("%s", __func__);
    callbacks.EmptyBufferDone = etbdone;
    callbacks.FillBufferDone = ftbdone;
    callbacks.EventHandler = eventHandler;
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);

    OMX_ERRORTYPE ret = OMX_GetHandle(&pHandle,
    "OMX.qcom.image.jpeg.encoder",
    NULL,
    &callbacks);
    return 0;
}

int8_t omxJpegEncode(omx_jpeg_encode_params *encode_params) {

    encoding = 1;
    inputPort = malloc(sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    outputPort = malloc(sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    inputPort1 = malloc(sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    int size = 0;
    uint8_t num_planes;
    uint32_t planes[10];
    inputPort->nPortIndex = INPUT_PORT;
    outputPort->nPortIndex = OUTPUT_PORT;
    inputPort1->nPortIndex = INPUT_PORT1;
    OMX_GetParameter(pHandle, OMX_IndexParamPortDefinition, inputPort);
    OMX_GetParameter(pHandle, OMX_IndexParamPortDefinition, outputPort);

    bufferoffset.width = encode_params->dimension->orig_picture_dx;
    bufferoffset.height = encode_params->dimension->orig_picture_dy;
    mm_jpeg_encoder_get_buffer_offset( bufferoffset.width, bufferoffset.height,&bufferoffset.yOffset,
                                       &bufferoffset.cbcrOffset,
                                       &bufferoffset.totalSize,&num_planes,planes);

    OMX_GetExtensionIndex(pHandle,"omx.qcom.jpeg.exttype.buffer_offset",&buffer_offset);
    OMX_DBG_INFO(" Buffer width = %d, Buffer  height = %d, yOffset =%d, cbcrOffset =%d, totalSize = %d\n",
                 bufferoffset.width, bufferoffset.height, bufferoffset.yOffset,
                 bufferoffset.cbcrOffset,bufferoffset.totalSize);
    OMX_SetParameter(pHandle, buffer_offset, &bufferoffset);

    inputPort->format.image.nFrameWidth = encode_params->dimension->orig_picture_dx;
    inputPort->format.image.nFrameHeight = encode_params->dimension->orig_picture_dy;
    inputPort->format.image.nStride = encode_params->dimension->orig_picture_dx;
    inputPort->format.image.nSliceHeight = encode_params->dimension->orig_picture_dy;
    inputPort->nBufferSize = bufferoffset.totalSize;

    inputPort1->format.image.nFrameWidth = encode_params->dimension->thumbnail_width;
    inputPort1->format.image.nFrameHeight = encode_params->dimension->thumbnail_height;
    inputPort1->format.image.nStride = encode_params->dimension->thumbnail_width;
    inputPort1->format.image.nSliceHeight = encode_params->dimension->thumbnail_height;

    OMX_SetParameter(pHandle, OMX_IndexParamPortDefinition, inputPort);
    OMX_GetParameter(pHandle, OMX_IndexParamPortDefinition, inputPort);
    size = inputPort->nBufferSize;
    thumbnail.width = encode_params->dimension->thumbnail_width;
    thumbnail.height = encode_params->dimension->thumbnail_height;

    OMX_SetParameter(pHandle, OMX_IndexParamPortDefinition, inputPort1);
    OMX_GetParameter(pHandle, OMX_IndexParamPortDefinition, inputPort1);
    LOGE("thumbnail widht %d height %d", encode_params->dimension->thumbnail_width,
    encode_params->dimension->thumbnail_height);

    userpreferences.color_format = DEFAULT_COLOR_FORMAT;
    userpreferences.thumbnail_color_format = DEFAULT_COLOR_FORMAT;
    if (hw_encode)
        userpreferences.preference = OMX_ENCODER_PREF_HW_ACCELERATED_PREFERRED;
    else
        userpreferences.preference = OMX_ENCODER_PREF_SOFTWARE_ONLY;


    OMX_DBG_ERROR("Scaling params thumb in1_w %d in1_h %d out1_w %d out1_h %d "
                  "main_img in2_w %d in2_h %d out2_w %d out2_h %d\n",
                  encode_params->scaling_params->in1_w, encode_params->scaling_params->in1_h,
                  encode_params->scaling_params->out1_w, encode_params->scaling_params->out1_h,
                  encode_params->scaling_params->in2_w, encode_params->scaling_params->in2_h,
                  encode_params->scaling_params->out2_w, encode_params->scaling_params->out2_h);
   /*Main image scaling*/
    OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);


    if (encode_params->scaling_params->in2_w && encode_params->scaling_params->in2_h) {
        OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);
        if (jpegRotation) {
            userpreferences.preference = OMX_ENCODER_PREF_SOFTWARE_ONLY;
            OMX_DBG_INFO("HAL: Scaling and roation true: setting pref to sw\n");
        }

      /* Scaler information  for main image */
        recttype.nWidth = CEILING2(encode_params->scaling_params->in2_w);
        recttype.nHeight = CEILING2(encode_params->scaling_params->in2_h);
        OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        if (encode_params->main_crop_offset) {
            recttype.nLeft = encode_params->main_crop_offset->x;
            recttype.nTop = encode_params->main_crop_offset->y;
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        } else {
            recttype.nLeft = 0;
            recttype.nTop = 0;
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        }
        OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        recttype.nPortIndex = 1;
        OMX_SetConfig(pHandle, OMX_IndexConfigCommonInputCrop, &recttype);
        OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        if (encode_params->scaling_params->out2_w && encode_params->scaling_params->out2_h) {
            recttype.nWidth = (encode_params->scaling_params->out2_w);
            recttype.nHeight = (encode_params->scaling_params->out2_h);
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);


            recttype.nPortIndex = 1;
            OMX_SetConfig(pHandle, OMX_IndexConfigCommonOutputCrop, &recttype);
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        }

    } else {
        OMX_DBG_ERROR("There is no scaling information for JPEG main image scaling.");
    }
  /*case of thumbnail*/
    if (encode_params->scaling_params->in1_w && encode_params->scaling_params->in1_h) {
        thumbnail.scaling = 0;
        thumbnail.cropWidth = CEILING2(encode_params->scaling_params->in1_w);
        thumbnail.cropHeight = CEILING2(encode_params->scaling_params->in1_h);
        thumbnail.width  = encode_params->scaling_params->out1_w;
        thumbnail.height = encode_params->scaling_params->out1_h;
        /*jpege_config.main_cfg.scale_cfg.input_width = CEILING2(scaling_params->in2_w);
        jpege_config.main_cfg.scale_cfg.input_height = CEILING2(scaling_params->in2_w);*/

        if (encode_params->thumb_crop_offset) {
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

            thumbnail.left = encode_params->thumb_crop_offset->x;
            thumbnail.top = encode_params->thumb_crop_offset->y;
            thumbnail.scaling = 1;
        } else {
            OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

            thumbnail.left = 0;
            thumbnail.top = 0;
        }
    } else {
        OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);

        thumbnail.scaling = 0;
        OMX_DBG_ERROR("There is no scaling information for thumbnail");
    }
    OMX_DBG_ERROR("%s:%d/n",__func__,__LINE__);
    OMX_GetExtensionIndex(pHandle,"omx.qcom.jpeg.exttype.user_preferences",&user_preferences);
    OMX_DBG_INFO("HAL: User Preferences: color_format =%d  thumbnail_color_format = %d encoder preference =%d\n",
    userpreferences.color_format,userpreferences.thumbnail_color_format,
    userpreferences.preference);
    OMX_SetParameter(pHandle,user_preferences,&userpreferences);

    OMX_GetExtensionIndex(pHandle, "omx.qcom.jpeg.exttype.thumbnail", &type);
    OMX_SetParameter(pHandle, type, &thumbnail);

    qFactor.nPortIndex = INPUT_PORT;
    OMX_GetParameter(pHandle, OMX_IndexParamQFactor, &qFactor);
    qFactor.nQFactor = jpegMainimageQuality;
    OMX_SetParameter(pHandle, OMX_IndexParamQFactor, &qFactor);

    OMX_GetExtensionIndex(pHandle, "omx.qcom.jpeg.exttype.thumbnail_quality",
    &thumbnailQualityType);

    LOGE("thumbnail quality %u %d", thumbnailQualityType, jpegThumbnailQuality);
    OMX_GetParameter(pHandle, thumbnailQualityType, &thumbnailQuality);
    thumbnailQuality.nQFactor = jpegThumbnailQuality;
    OMX_SetParameter(pHandle, thumbnailQualityType, &thumbnailQuality);

    rotType.nPortIndex = OUTPUT_PORT;
    rotType.nRotation = jpegRotation;
    OMX_SetConfig(pHandle, OMX_IndexConfigCommonRotate, &rotType);
    LOGE("Set rotation to %d\n",jpegRotation);

    pmem_info.fd = encode_params->snapshot_fd;
    pmem_info.offset = 0;
    OMX_UseBuffer(pHandle, &pInBuffers, 0, &pmem_info, size,
    (void *) encode_params->snapshot_buf);

    pmem_info1.fd = encode_params->thumbnail_fd;
    pmem_info1.offset = 0;

    OMX_DBG_INFO("KK pinput1 buff size %d", inputPort1->nBufferSize);
    OMX_UseBuffer(pHandle, &pInBuffers1, 2, &pmem_info1, inputPort1->nBufferSize,
    (void *) encode_params->thumbnail_buf);

    OMX_UseBuffer(pHandle, &pOutBuffers, 1, NULL, size, (void *) out_buffer);


    waitForEvent(OMX_EventCmdComplete, OMX_CommandStateSet, OMX_StateIdle);
    LOGE("In LibCamera : State changed to OMX_StateIdle\n");
    OMX_SendCommand(pHandle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    waitForEvent(OMX_EventCmdComplete, OMX_CommandStateSet, OMX_StateExecuting);

    OMX_EmptyThisBuffer(pHandle, pInBuffers);
    OMX_EmptyThisBuffer(pHandle, pInBuffers1);
    OMX_FillThisBuffer(pHandle, pOutBuffers);
    return 1;
}

void omxJpegJoin(){
    if (encoding) {
        LOGE("%s", __func__);
        encoding = 0;
        OMX_SendCommand(pHandle, OMX_CommandStateSet, OMX_StateIdle, NULL);
        /*waitForEvent(OMX_EventCmdComplete, OMX_CommandStateSet, OMX_StateIdle);*/
        OMX_SendCommand(pHandle, OMX_CommandStateSet, OMX_StateLoaded, NULL);
        OMX_FreeBuffer(pHandle, 0, pInBuffers);
        OMX_FreeBuffer(pHandle, 2, pInBuffers1);
        OMX_FreeBuffer(pHandle, 1, pOutBuffers);
        /*waitForEvent(OMX_EventCmdComplete, OMX_CommandStateSet, OMX_StateLoaded);*/
        OMX_Deinit();

    }
}

void omxJpegClose(){
    LOGE("%s", __func__);
}


void omxJpegCancel()
{
    /*TODO: need to enable it.*/
    /*pthread_mutex_lock(&jpegcb_mutex);*/
    mmcamera_jpegfragment_callback = NULL;
    mmcamera_jpeg_callback = NULL;
    user_data = NULL;
    /*TODO: need to enable it.*/
    /*pthread_mutex_unlock(&jpegcb_mutex);*/
    omxJpegJoin();
}


int8_t mm_jpeg_encoder_setMainImageQuality(uint32_t quality)
{
    /*pthread_mutex_lock(&jpege_mutex);*/
    LOGE("%s: current main inage quality %d ," \
    " new quality : %d\n", __func__, jpegMainimageQuality, quality);
    if (quality <= 100)
        jpegMainimageQuality = quality;
   /*pthread_mutex_unlock(&jpege_mutex);*/
    return TRUE;
}

int8_t mm_jpeg_encoder_setThumbnailQuality(uint32_t quality)
{
    /*pthread_mutex_lock(&jpege_mutex);*/
    LOGE("%s: current thumbnail quality %d ," \
    " new quality : %d\n", __func__, jpegThumbnailQuality, quality);
    if (quality <= 100)
        jpegThumbnailQuality = quality;
    /*pthread_mutex_unlock(&jpege_mutex);*/
    return TRUE;
}

int8_t mm_jpeg_encoder_setRotation(int rotation)
{
    /*pthread_mutex_lock(&jpege_mutex);*/
    /* Set rotation configuration */
    switch (rotation) {
    case 0:
    case 90:
    case 180:
    case 270:
        jpegRotation = rotation;
        break;
    default:
        /* Invalid rotation mode, set to default */
        LOGE(" Setting Default rotation mode ");
        jpegRotation = 0;
        break;
    }
    /*pthread_mutex_unlock(&jpege_mutex);*/
    return TRUE;
}

/*===========================================================================
FUNCTION      jpege_event_handler

DESCRIPTION   Set physical offset for the buffer
===========================================================================*/
void mm_jpege_set_phy_offset(uint32_t a_phy_offset)
{
    phy_offset = a_phy_offset;
}
