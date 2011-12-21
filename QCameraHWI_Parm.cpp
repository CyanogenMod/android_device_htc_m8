/*
** Copyright (c) 2011 Code Aurora Forum. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHWI_Parm"
#include <utils/Log.h>

#include <utils/Errors.h>
#include <utils/threads.h>
#include <binder/MemoryHeapPmem.h>
#ifdef USE_ION
#include <binder/MemoryHeapIon.h>
#endif
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <math.h>
#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif
#include <linux/ioctl.h>
#include <camera/CameraParameters.h>
#include <media/mediarecorder.h>
#include <gralloc_priv.h>

#include "linux/msm_mdp.h"
#include <linux/fb.h>

extern "C" {
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <stdlib.h>
#include <linux/ion.h>
#include <camera.h>
#include <cam_fifo.h>
#include <liveshot.h>
#include <mm-still/jpeg/jpege.h>
#include <jpeg_encoder.h>

} // extern "C"

#include "QCameraHWI.h"

/* QCameraHardwareInterface class implementation goes here*/
/* following code implements the parameter logic of this class*/
#define EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR 12
#define EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR -12
#define EXPOSURE_COMPENSATION_DEFAULT_NUMERATOR 0
#define EXPOSURE_COMPENSATION_DENOMINATOR 6
#define EXPOSURE_COMPENSATION_STEP ((float (1))/EXPOSURE_COMPENSATION_DENOMINATOR)

//Default FPS
#define MINIMUM_FPS 5
#define MAXIMUM_FPS 31
#define DEFAULT_FPS MAXIMUM_FPS

//Default Picture Width
#define DEFAULT_PICTURE_WIDTH  640
#define DEFAULT_PICTURE_HEIGHT 480

//Default Preview Width
#define DEFAULT_PREVIEW_WIDTH 640
#define DEFAULT_PREVIEW_HEIGHT 480

#define THUMBNAIL_SIZE_COUNT (sizeof(thumbnail_sizes)/sizeof(thumbnail_size_type))
#define DEFAULT_THUMBNAIL_SETTING 2
#define THUMBNAIL_WIDTH_STR "512"
#define THUMBNAIL_HEIGHT_STR "384"
#define THUMBNAIL_SMALL_HEIGHT 144

#define JPEG_THUMBNAIL_SIZE_COUNT (sizeof(jpeg_thumbnail_sizes)/sizeof(camera_size_type))
#define DONT_CARE_COORDINATE -1

//for histogram stats
#define HISTOGRAM_STATS_SIZE 257

//Supported preview fps ranges should be added to this array in the form (minFps,maxFps)
static  android::FPSRange FpsRangesSupported[] = {
            android::FPSRange(MINIMUM_FPS*1000,MAXIMUM_FPS*1000)
        };
#define FPS_RANGES_SUPPORTED_COUNT (sizeof(FpsRangesSupported)/sizeof(FpsRangesSupported[0]))


typedef struct {
    uint32_t aspect_ratio;
    uint32_t width;
    uint32_t height;
} thumbnail_size_type;

static thumbnail_size_type thumbnail_sizes[] = {
    { 7281, 512, 288 }, //1.777778
    { 6826, 480, 288 }, //1.666667
    { 6808, 256, 154 }, //1.662337
    { 6144, 432, 288 }, //1.5
    { 5461, 512, 384 }, //1.333333
    { 5006, 352, 288 }, //1.222222
};
static camera_size_type jpeg_thumbnail_sizes[]  = {
    { 512, 288 },
    { 480, 288 },
    { 432, 288 },
    { 512, 384 },
    { 352, 288 },
    {0,0}
};

static camera_size_type default_preview_sizes[] = {
  { 1920, 1088}, //1080p
  { 1280, 720}, // 720P, reserved
  { 800, 480}, // WVGA
  { 768, 432},
  { 720, 480},
  { 640, 480}, // VGA
  { 576, 432},
  { 480, 320}, // HVGA
  { 384, 288},
  { 352, 288}, // CIF
  { 320, 240}, // QVGA
  { 240, 160}, // SQVGA
  { 176, 144}, // QCIF
};

static struct camera_size_type zsl_picture_sizes[] = {
  { 1024, 768}, // 1MP XGA
  { 800, 600}, //SVGA
  { 800, 480}, // WVGA
  { 640, 480}, // VGA
  { 352, 288}, //CIF
  { 320, 240}, // QVGA
  { 176, 144} // QCIF
};

static camera_size_type default_picture_sizes[] = {
  { 4000, 3000}, // 12MP
  { 3200, 2400}, // 8MP
  { 2592, 1944}, // 5MP
  { 2048, 1536}, // 3MP QXGA
  { 1920, 1080}, //HD1080
  { 1600, 1200}, // 2MP UXGA
  { 1280, 768}, //WXGA
  { 1280, 720}, //HD720
  { 1024, 768}, // 1MP XGA
  { 800, 600}, //SVGA
  { 800, 480}, // WVGA
  { 640, 480}, // VGA
  { 352, 288}, //CIF
  { 320, 240}, // QVGA
  { 176, 144} // QCIF
};

static camera_size_type hfr_sizes[] = {
  { 800, 480}, // WVGA
  { 640, 480} // VGA
};


extern int HAL_numOfCameras;
extern camera_info_t HAL_cameraInfo[MSM_MAX_CAMERA_SENSORS];
extern mm_camera_t * HAL_camerahandle[MSM_MAX_CAMERA_SENSORS];

namespace android {

static uint32_t  HFR_SIZE_COUNT=2;
static const int PICTURE_FORMAT_JPEG = 1;
static const int PICTURE_FORMAT_RAW = 2;

/********************************************************************/
static const str_map effects[] = {
    { CameraParameters::EFFECT_NONE,       CAMERA_EFFECT_OFF },
    { CameraParameters::EFFECT_MONO,       CAMERA_EFFECT_MONO },
    { CameraParameters::EFFECT_NEGATIVE,   CAMERA_EFFECT_NEGATIVE },
    { CameraParameters::EFFECT_SOLARIZE,   CAMERA_EFFECT_SOLARIZE },
    { CameraParameters::EFFECT_SEPIA,      CAMERA_EFFECT_SEPIA },
    { CameraParameters::EFFECT_POSTERIZE,  CAMERA_EFFECT_POSTERIZE },
    { CameraParameters::EFFECT_WHITEBOARD, CAMERA_EFFECT_WHITEBOARD },
    { CameraParameters::EFFECT_BLACKBOARD, CAMERA_EFFECT_BLACKBOARD },
    { CameraParameters::EFFECT_AQUA,       CAMERA_EFFECT_AQUA },
    { CameraParameters::EFFECT_EMBOSS,     CAMERA_EFFECT_EMBOSS },
    { CameraParameters::EFFECT_SKETCH,     CAMERA_EFFECT_SKETCH },
    { CameraParameters::EFFECT_NEON,       CAMERA_EFFECT_NEON }
};

static const str_map iso[] = {
    { CameraParameters::ISO_AUTO,  CAMERA_ISO_AUTO},
    { CameraParameters::ISO_HJR,   CAMERA_ISO_DEBLUR},
    { CameraParameters::ISO_100,   CAMERA_ISO_100},
    { CameraParameters::ISO_200,   CAMERA_ISO_200},
    { CameraParameters::ISO_400,   CAMERA_ISO_400},
    { CameraParameters::ISO_800,   CAMERA_ISO_800 },
    { CameraParameters::ISO_1600,  CAMERA_ISO_1600 }
};

static const str_map scenemode[] = {
    { CameraParameters::SCENE_MODE_AUTO,           CAMERA_BESTSHOT_OFF },
    { CameraParameters::SCENE_MODE_ACTION,         CAMERA_BESTSHOT_ACTION },
    { CameraParameters::SCENE_MODE_PORTRAIT,       CAMERA_BESTSHOT_PORTRAIT },
    { CameraParameters::SCENE_MODE_LANDSCAPE,      CAMERA_BESTSHOT_LANDSCAPE },
    { CameraParameters::SCENE_MODE_NIGHT,          CAMERA_BESTSHOT_NIGHT },
    { CameraParameters::SCENE_MODE_NIGHT_PORTRAIT, CAMERA_BESTSHOT_NIGHT_PORTRAIT },
    { CameraParameters::SCENE_MODE_THEATRE,        CAMERA_BESTSHOT_THEATRE },
    { CameraParameters::SCENE_MODE_BEACH,          CAMERA_BESTSHOT_BEACH },
    { CameraParameters::SCENE_MODE_SNOW,           CAMERA_BESTSHOT_SNOW },
    { CameraParameters::SCENE_MODE_SUNSET,         CAMERA_BESTSHOT_SUNSET },
    { CameraParameters::SCENE_MODE_STEADYPHOTO,    CAMERA_BESTSHOT_ANTISHAKE },
    { CameraParameters::SCENE_MODE_FIREWORKS ,     CAMERA_BESTSHOT_FIREWORKS },
    { CameraParameters::SCENE_MODE_SPORTS ,        CAMERA_BESTSHOT_SPORTS },
    { CameraParameters::SCENE_MODE_PARTY,          CAMERA_BESTSHOT_PARTY },
    { CameraParameters::SCENE_MODE_CANDLELIGHT,    CAMERA_BESTSHOT_CANDLELIGHT },
    { CameraParameters::SCENE_MODE_BACKLIGHT,      CAMERA_BESTSHOT_BACKLIGHT },
    { CameraParameters::SCENE_MODE_FLOWERS,        CAMERA_BESTSHOT_FLOWERS },
    { CameraParameters::SCENE_MODE_AR,             CAMERA_BESTSHOT_AR },
};

static const str_map scenedetect[] = {
    { CameraParameters::SCENE_DETECT_OFF, FALSE  },
    { CameraParameters::SCENE_DETECT_ON, TRUE },
};

#define DONT_CARE AF_MODE_MAX
static const str_map focus_modes[] = {
    { CameraParameters::FOCUS_MODE_AUTO,     AF_MODE_AUTO},
    { CameraParameters::FOCUS_MODE_INFINITY, DONT_CARE },
    { CameraParameters::FOCUS_MODE_NORMAL,   AF_MODE_NORMAL },
    { CameraParameters::FOCUS_MODE_MACRO,    AF_MODE_MACRO },
    { CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE, AF_MODE_CAF},
    { CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO, DONT_CARE }
};

static const str_map selectable_zone_af[] = {
    { CameraParameters::SELECTABLE_ZONE_AF_AUTO,  AUTO },
    { CameraParameters::SELECTABLE_ZONE_AF_SPOT_METERING, SPOT },
    { CameraParameters::SELECTABLE_ZONE_AF_CENTER_WEIGHTED, CENTER_WEIGHTED },
    { CameraParameters::SELECTABLE_ZONE_AF_FRAME_AVERAGE, AVERAGE }
};

// from qcamera/common/camera.h
static const str_map autoexposure[] = {
    { CameraParameters::AUTO_EXPOSURE_FRAME_AVG,  CAMERA_AEC_FRAME_AVERAGE },
    { CameraParameters::AUTO_EXPOSURE_CENTER_WEIGHTED, CAMERA_AEC_CENTER_WEIGHTED },
    { CameraParameters::AUTO_EXPOSURE_SPOT_METERING, CAMERA_AEC_SPOT_METERING }
};

// from aeecamera.h
static const str_map whitebalance[] = {
    { CameraParameters::WHITE_BALANCE_AUTO,            CAMERA_WB_AUTO },
    { CameraParameters::WHITE_BALANCE_INCANDESCENT,    CAMERA_WB_INCANDESCENT },
    { CameraParameters::WHITE_BALANCE_FLUORESCENT,     CAMERA_WB_FLUORESCENT },
    { CameraParameters::WHITE_BALANCE_DAYLIGHT,        CAMERA_WB_DAYLIGHT },
    { CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT, CAMERA_WB_CLOUDY_DAYLIGHT }
};

static const str_map antibanding[] = {
    { CameraParameters::ANTIBANDING_OFF,  CAMERA_ANTIBANDING_OFF },
    { CameraParameters::ANTIBANDING_50HZ, CAMERA_ANTIBANDING_50HZ },
    { CameraParameters::ANTIBANDING_60HZ, CAMERA_ANTIBANDING_60HZ },
    { CameraParameters::ANTIBANDING_AUTO, CAMERA_ANTIBANDING_AUTO }
};

static const str_map frame_rate_modes[] = {
        {CameraParameters::KEY_PREVIEW_FRAME_RATE_AUTO_MODE, FPS_MODE_AUTO},
        {CameraParameters::KEY_PREVIEW_FRAME_RATE_FIXED_MODE, FPS_MODE_FIXED}
};

static const str_map touchafaec[] = {
    { CameraParameters::TOUCH_AF_AEC_OFF, FALSE },
    { CameraParameters::TOUCH_AF_AEC_ON, TRUE }
};

static const str_map hfr[] = {
    { CameraParameters::VIDEO_HFR_OFF, CAMERA_HFR_MODE_OFF },
    { CameraParameters::VIDEO_HFR_2X, CAMERA_HFR_MODE_60FPS },
    { CameraParameters::VIDEO_HFR_3X, CAMERA_HFR_MODE_90FPS },
    { CameraParameters::VIDEO_HFR_4X, CAMERA_HFR_MODE_120FPS },
};

static const str_map flash[] = {
    { CameraParameters::FLASH_MODE_OFF,  LED_MODE_OFF },
    { CameraParameters::FLASH_MODE_AUTO, LED_MODE_AUTO },
    { CameraParameters::FLASH_MODE_ON, LED_MODE_ON },
    { CameraParameters::FLASH_MODE_TORCH, LED_MODE_TORCH}
};

static const str_map lensshade[] = {
    { CameraParameters::LENSSHADE_ENABLE, TRUE },
    { CameraParameters::LENSSHADE_DISABLE, FALSE }
};

static const str_map mce[] = {
    { CameraParameters::MCE_ENABLE, TRUE },
    { CameraParameters::MCE_DISABLE, FALSE }
};

static const str_map histogram[] = {
    { CameraParameters::HISTOGRAM_ENABLE, TRUE },
    { CameraParameters::HISTOGRAM_DISABLE, FALSE }
};

static const str_map skinToneEnhancement[] = {
    { CameraParameters::SKIN_TONE_ENHANCEMENT_ENABLE, TRUE },
    { CameraParameters::SKIN_TONE_ENHANCEMENT_DISABLE, FALSE }
};

static const str_map denoise[] = {
    { CameraParameters::DENOISE_OFF, FALSE },
    { CameraParameters::DENOISE_ON, TRUE }
};

static const str_map facedetection[] = {
    { CameraParameters::FACE_DETECTION_OFF, FALSE },
    { CameraParameters::FACE_DETECTION_ON, TRUE }
};

static const str_map redeye_reduction[] = {
    { CameraParameters::REDEYE_REDUCTION_ENABLE, TRUE },
    { CameraParameters::REDEYE_REDUCTION_DISABLE, FALSE }
};

static const str_map picture_formats[] = {
        {CameraParameters::PIXEL_FORMAT_JPEG, PICTURE_FORMAT_JPEG},
        {CameraParameters::PIXEL_FORMAT_RAW, PICTURE_FORMAT_RAW}
};

static const str_map recording_Hints[] = {
        {"false", FALSE},
        {"true",  TRUE}
};

static const str_map preview_formats[] = {
        {CameraParameters::PIXEL_FORMAT_YUV420SP,   HAL_PIXEL_FORMAT_YCrCb_420_SP},
        {CameraParameters::PIXEL_FORMAT_YUV420SP_ADRENO, HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO},
        {CameraParameters::PIXEL_FORMAT_YV12, HAL_PIXEL_FORMAT_YV12},
        {CameraParameters::PIXEL_FORMAT_YUV420P,HAL_PIXEL_FORMAT_YV12},
        {CameraParameters::PIXEL_FORMAT_NV12, HAL_PIXEL_FORMAT_YCbCr_420_SP}
};

static const preview_format_info_t preview_format_info_list[] = {
  {HAL_PIXEL_FORMAT_YCrCb_420_SP, CAMERA_YUV_420_NV21, CAMERA_PAD_TO_WORD, 2},
  {HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO, CAMERA_YUV_420_NV21, CAMERA_PAD_TO_4K, 2},
  {HAL_PIXEL_FORMAT_YCbCr_420_SP, CAMERA_YUV_420_NV12, CAMERA_PAD_TO_WORD, 2},
  {HAL_PIXEL_FORMAT_YV12,         CAMERA_YUV_420_YV12, CAMERA_PAD_TO_WORD, 3}
};

static const str_map zsl_modes[] = {
    { CameraParameters::ZSL_OFF, FALSE },
    { CameraParameters::ZSL_ON, TRUE },
};

/**************************************************************************/
static int attr_lookup(const str_map arr[], int len, const char *name)
{
    if (name) {
        for (int i = 0; i < len; i++) {
            if (!strcmp(arr[i].desc, name))
                return arr[i].val;
        }
    }
    return NOT_FOUND;
}

bool QCameraHardwareInterface::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value)
{
    LOGE("%s : type : %d Value : %d",__func__,type,*((int *)value));
    if(MM_CAMERA_OK != cam_config_set_parm(mCameraId, type,value )) {
        LOGE("native_set_parms failed: type %d length %d error %s",
            type, length, strerror(errno));
        return false;
    }

    return true;

}

bool QCameraHardwareInterface::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value, int *result)
{

    *result= cam_config_set_parm(mCameraId, type,value );
    if(MM_CAMERA_OK!=*result) {
        LOGE("native_set_parms failed: type %d length %d error %s",
            type, length, strerror(errno));
        return false;
    }
    return true;
}

//Filter Picture sizes based on max width and height
/* TBD: do we still need this - except for ZSL? */
void QCameraHardwareInterface::filterPictureSizes(){
    unsigned int i;
    if(mPictureSizeCount <= 0)
        return;
    maxSnapshotWidth = mPictureSizes[0].width;
    maxSnapshotHeight = mPictureSizes[0].height;
   // Iterate through all the width and height to find the max value
    for(i =0; i<mPictureSizeCount;i++){
        if(((maxSnapshotWidth < mPictureSizes[i].width) &&
            (maxSnapshotHeight <= mPictureSizes[i].height))){
            maxSnapshotWidth = mPictureSizes[i].width;
            maxSnapshotHeight = mPictureSizes[i].height;
        }
    }
    if(myMode & CAMERA_ZSL_MODE){
        // due to lack of PMEM we restrict to lower resolution
        mPictureSizesPtr = zsl_picture_sizes;
        mSupportedPictureSizesCount = 7;
    }else{
        mPictureSizesPtr = mPictureSizes;
        mSupportedPictureSizesCount = mPictureSizeCount;
    }
}

static String8 create_sizes_str(const camera_size_type *sizes, int len) {
    String8 str;
    char buffer[32];

    if (len > 0) {
        snprintf(buffer, sizeof(buffer), "%dx%d", sizes[0].width, sizes[0].height);
        str.append(buffer);
    }
    for (int i = 1; i < len; i++) {
        snprintf(buffer, sizeof(buffer), ",%dx%d", sizes[i].width, sizes[i].height);
        str.append(buffer);
    }
    return str;
}

String8 QCameraHardwareInterface::create_values_str(const str_map *values, int len) {
    String8 str;

    if (len > 0) {
        str.append(values[0].desc);
    }
    for (int i = 1; i < len; i++) {
        str.append(",");
        str.append(values[i].desc);
    }
    return str;
}

static String8 create_fps_str(const android:: FPSRange* fps, int len) {
    String8 str;
    char buffer[32];

    if (len > 0) {
        snprintf(buffer, sizeof(buffer), "(%d,%d)", fps[0].minFPS, fps[0].maxFPS);
        str.append(buffer);
    }
    for (int i = 1; i < len; i++) {
        snprintf(buffer, sizeof(buffer), ",(%d,%d)", fps[i].minFPS, fps[i].maxFPS);
        str.append(buffer);
    }
    return str;
}

static String8 create_values_range_str(int min, int max){
    String8 str;
    char buffer[32];

    if(min <= max){
        snprintf(buffer, sizeof(buffer), "%d", min);
        str.append(buffer);

        for (int i = min + 1; i <= max; i++) {
            snprintf(buffer, sizeof(buffer), ",%d", i);
            str.append(buffer);
        }
    }
    return str;
}

static int parse_size(const char *str, int &width, int &height)
{
    // Find the width.
    char *end;
    int w = (int)strtol(str, &end, 10);
    // If an 'x' or 'X' does not immediately follow, give up.
    if ( (*end != 'x') && (*end != 'X') )
        return -1;

    // Find the height, immediately after the 'x'.
    int h = (int)strtol(end+1, 0, 10);

    width = w;
    height = h;

    return 0;
}

bool QCameraHardwareInterface::isValidDimension(int width, int height) {
    bool retVal = FALSE;
    /* This function checks if a given resolution is valid or not.
     * A particular resolution is considered valid if it satisfies
     * the following conditions:
     * 1. width & height should be multiple of 16.
     * 2. width & height should be less than/equal to the dimensions
     *    supported by the camera sensor.
     * 3. the aspect ratio is a valid aspect ratio and is among the
     *    commonly used aspect ratio as determined by the thumbnail_sizes
     *    data structure.
     */

    if( (width == CEILING16(width)) && (height == CEILING16(height))
     && (width <= maxSnapshotWidth)
    && (height <= maxSnapshotHeight) )
    {
        uint32_t pictureAspectRatio = (uint32_t)((width * Q12)/height);
        for(uint32_t i = 0; i < THUMBNAIL_SIZE_COUNT; i++ ) {
            if(thumbnail_sizes[i].aspect_ratio == pictureAspectRatio) {
                retVal = TRUE;
                break;
            }
        }
    }
    return retVal;
}

void QCameraHardwareInterface::hasAutoFocusSupport(){

    LOGV("%s",__func__);

    if(isZSLMode()){
        mHasAutoFocusSupport = false;
        return;
    }

    if(cam_ops_is_op_supported (mCameraId, MM_CAMERA_OPS_FOCUS )) {
        mHasAutoFocusSupport = true;
    }
    else {
        LOGE("AutoFocus is not supported");
        mHasAutoFocusSupport = false;
    }

    LOGV("%s:rc= %d",__func__, mHasAutoFocusSupport);

}

bool QCameraHardwareInterface::supportsSceneDetection() {
   bool rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_ASD_ENABLE);
   return rc;
}

bool QCameraHardwareInterface::supportsFaceDetection() {
    bool rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_FD);
    return rc;
}

bool QCameraHardwareInterface::supportsSelectableZoneAf() {
   bool rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_FOCUS_RECT); //@Guru
   return rc;
}

bool QCameraHardwareInterface::supportsRedEyeReduction() {
   bool rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_REDEYE_REDUCTION);
   return rc;
}

static String8 create_str(int16_t *arr, int length){
    String8 str;
    char buffer[32];

    if(length > 0){
        snprintf(buffer, sizeof(buffer), "%d", arr[0]);
        str.append(buffer);
    }

    for (int i =1;i<length;i++){
        snprintf(buffer, sizeof(buffer), ",%d",arr[i]);
        str.append(buffer);
    }
    return str;
}

void QCameraHardwareInterface::initDefaultParameters()
{
    bool ret;
    LOGI("%s: E", __func__);

    //cam_ctrl_dimension_t dim;
    memset(&mDimension, 0, sizeof(cam_ctrl_dimension_t));
    memset(&mPreviewFormatInfo, 0, sizeof(preview_format_info_t));
    mDimension.video_width     = DEFAULT_STREAM_WIDTH;
    mDimension.video_height    = DEFAULT_STREAM_HEIGHT;
    mDimension.picture_width   = DEFAULT_STREAM_WIDTH;
    mDimension.picture_height  = DEFAULT_STREAM_HEIGHT;
    mDimension.display_width   = DEFAULT_STREAM_WIDTH;
    mDimension.display_height  = DEFAULT_STREAM_HEIGHT;
    mDimension.orig_picture_dx = mDimension.picture_width;
    mDimension.orig_picture_dy = mDimension.picture_height;
    mDimension.ui_thumbnail_width = DEFAULT_STREAM_WIDTH;
    mDimension.ui_thumbnail_height = DEFAULT_STREAM_HEIGHT;
    mDimension.orig_video_width = DEFAULT_STREAM_WIDTH;
    mDimension.orig_video_height = DEFAULT_STREAM_HEIGHT;

    mDimension.prev_format     = CAMERA_YUV_420_NV21;
    mDimension.enc_format      = CAMERA_YUV_420_NV12;
    mDimension.main_img_format = CAMERA_YUV_420_NV21;
    mDimension.thumb_format    = CAMERA_YUV_420_NV21;
    mDimension.prev_padding_format = CAMERA_PAD_TO_WORD;

    ret = native_set_parms(MM_CAMERA_PARM_DIMENSION,
                              sizeof(cam_ctrl_dimension_t), (void *) &mDimension);
    if(!ret) {
      LOGE("MM_CAMERA_PARM_DIMENSION Failed.");
      return;
    }

    hasAutoFocusSupport();

    //Disable DIS for Web Camera
    #if 0
        if( !mCfgControl.mm_camera_is_supported(MM_CAMERA_PARM_VIDEO_DIS)){
            LOGV("DISABLE DIS");
            mDisEnabled = 0;
        }else {
            LOGV("Enable DIS");
        }
    #else
        mDisEnabled = 0;
    #endif

    // Initialize constant parameter strings. This will happen only once in the
    // lifetime of the mediaserver process.
    if (true/*!mParamStringInitialized*/) {
        //filter picture sizes
        filterPictureSizes();
        mPictureSizeValues = create_sizes_str(
                mPictureSizesPtr, mSupportedPictureSizesCount);
        mPreviewSizeValues = create_sizes_str(
                mPreviewSizes,  mPreviewSizeCount);
        mHfrSizeValues = create_sizes_str(
                hfr_sizes, HFR_SIZE_COUNT);
        mHfrValues = create_values_str(
            hfr,sizeof(hfr)/sizeof(str_map));
        mFpsRangesSupportedValues = create_fps_str(
            FpsRangesSupported,FPS_RANGES_SUPPORTED_COUNT );
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
            mFpsRangesSupportedValues);
        mParameters.setPreviewFpsRange(MINIMUM_FPS*1000,MAXIMUM_FPS*1000);
        mFlashValues = create_values_str(
            flash, sizeof(flash) / sizeof(str_map));
        mLensShadeValues = create_values_str(
            lensshade,sizeof(lensshade)/sizeof(str_map));
        mMceValues = create_values_str(
            mce,sizeof(mce)/sizeof(str_map));
        mEffectValues = create_values_str(effects, sizeof(effects) / sizeof(str_map));
        mAntibandingValues = create_values_str(
            antibanding, sizeof(antibanding) / sizeof(str_map));
        mIsoValues = create_values_str(iso,sizeof(iso)/sizeof(str_map));
        mAutoExposureValues = create_values_str(
            autoexposure, sizeof(autoexposure) / sizeof(str_map));
        mWhitebalanceValues = create_values_str(
            whitebalance, sizeof(whitebalance) / sizeof(str_map));

        if(mHasAutoFocusSupport){
            mFocusModeValues = create_values_str(
                    focus_modes, sizeof(focus_modes) / sizeof(str_map));
        }

        mSceneModeValues = create_values_str(scenemode, sizeof(scenemode) / sizeof(str_map));

        if(mHasAutoFocusSupport){
            mTouchAfAecValues = create_values_str(
                touchafaec,sizeof(touchafaec)/sizeof(str_map));
        }
        //Currently Enabling Histogram for 8x60
        mHistogramValues = create_values_str(
            histogram,sizeof(histogram)/sizeof(str_map));

        mSkinToneEnhancementValues = create_values_str(
            skinToneEnhancement,sizeof(skinToneEnhancement)/sizeof(str_map));

        mPictureFormatValues = create_values_str(
            picture_formats, sizeof(picture_formats)/sizeof(str_map));

        mZoomSupported=false;
        mMaxZoom=0;
        mm_camera_zoom_tbl_t zmt;
        if(MM_CAMERA_OK != cam_config_get_parm(mCameraId,
                             MM_CAMERA_PARM_MAXZOOM, &mMaxZoom)){
            LOGE("%s:Failed to get max zoom",__func__);
        }else{

            LOGE("Max Zoom:%d",mMaxZoom);
            /* Kernel driver limits the max amount of data that can be retreived through a control
            command to 260 bytes hence we conservatively limit to 110 zoom ratios */
            if(mMaxZoom>MAX_ZOOM_RATIOS) {
                LOGE("%s:max zoom is larger than sizeof zoomRatios table",__func__);
                mMaxZoom=MAX_ZOOM_RATIOS-1;
            }
            zmt.size=mMaxZoom;
            zmt.zoom_ratio_tbl=&zoomRatios[0];
            if(MM_CAMERA_OK != cam_config_get_parm(mCameraId,
                                 MM_CAMERA_PARM_ZOOM_RATIO, &zmt)){
                LOGE("%s:Failed to get max zoom ratios",__func__);
            }else{
                mZoomSupported=true;
                mZoomRatioValues =  create_str(zoomRatios, mMaxZoom);
            }
        }

        LOGE("Zoom supported:%d",mZoomSupported);

        denoise_value = create_values_str(
            denoise, sizeof(denoise) / sizeof(str_map));

       if(mHasAutoFocusSupport && supportsFaceDetection()) {
            mFaceDetectionValues = create_values_str(
                facedetection, sizeof(facedetection) / sizeof(str_map));
        }

        if(mHasAutoFocusSupport){
            mSelectableZoneAfValues = create_values_str(
                selectable_zone_af, sizeof(selectable_zone_af) / sizeof(str_map));
        }

        mSceneDetectValues = create_values_str(scenedetect, sizeof(scenedetect) / sizeof(str_map));

        mRedeyeReductionValues = create_values_str(
            redeye_reduction, sizeof(redeye_reduction) / sizeof(str_map));

        mZslValues = create_values_str(
            zsl_modes,sizeof(zsl_modes)/sizeof(str_map));

        mParamStringInitialized = true;
    }

    //Set Preview size
    mParameters.setPreviewSize(DEFAULT_PREVIEW_WIDTH, DEFAULT_PREVIEW_HEIGHT);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                    mPreviewSizeValues.string());
    mDimension.display_width = DEFAULT_PREVIEW_WIDTH;
    mDimension.display_height = DEFAULT_PREVIEW_HEIGHT;

    //Set Preview Frame Rate
    if(mFps >= MINIMUM_FPS && mFps <= MAXIMUM_FPS) {
        mPreviewFrameRateValues = create_values_range_str(
        MINIMUM_FPS, mFps);
    }else{
        mPreviewFrameRateValues = create_values_range_str(
        MINIMUM_FPS, MAXIMUM_FPS);
    }


    if (cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_FPS)) {
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
                        mPreviewFrameRateValues.string());
     } /* else {
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            DEFAULT_FPS);
    }*/

    //Set Preview Frame Rate Modes
    mParameters.setPreviewFrameRateMode("frame-rate-auto");
    mFrameRateModeValues = create_values_str(
            frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map));
      if(cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_FPS_MODE)){
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATE_MODES,
                    mFrameRateModeValues.string());
    }

    //Set Preview Format
    //mParameters.setPreviewFormat("yuv420sp"); // informative
    mParameters.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);

    mPreviewFormatValues = create_values_str(
        preview_formats, sizeof(preview_formats) / sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
            mPreviewFormatValues.string());

    //Set Overlay Format
    mParameters.set("overlay-format", HAL_PIXEL_FORMAT_YCbCr_420_SP);
    mParameters.set("max-num-detected-faces-hw", "2");

    //Set Picture Size
    mParameters.setPictureSize(DEFAULT_PICTURE_WIDTH, DEFAULT_PICTURE_HEIGHT);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    mPictureSizeValues.string());

    //Set Preview Frame Rate
    if(mFps >= MINIMUM_FPS && mFps <= MAXIMUM_FPS) {
        mParameters.setPreviewFrameRate(mFps);
    }else{
        mParameters.setPreviewFrameRate(DEFAULT_FPS);
    }

    //Set Picture Format
    mParameters.setPictureFormat("jpeg"); // informative
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
                    mPictureFormatValues);

    mParameters.set(CameraParameters::KEY_JPEG_QUALITY, "85"); // max quality
    //Set Video Format
    mParameters.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, "yuv420sp");

    //Set Thumbnail parameters
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,
                    THUMBNAIL_WIDTH_STR); // informative
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
                    THUMBNAIL_HEIGHT_STR); // informative
    mDimension.ui_thumbnail_width =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].width;
    mDimension.ui_thumbnail_height =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;
    mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "90");
    String8 valuesStr = create_sizes_str(jpeg_thumbnail_sizes, JPEG_THUMBNAIL_SIZE_COUNT);
    mParameters.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
                valuesStr.string());

    // Define CAMERA_SMOOTH_ZOOM in Android.mk file , to enable smoothzoom
#ifdef CAMERA_SMOOTH_ZOOM
    mParameters.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED, "true");
#endif
    if(mZoomSupported){
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
        LOGE("max zoom is %d", mMaxZoom-1);
        /* mMaxZoom value that the query interface returns is the size
        LOGV("max zoom is %d", mMaxZoom-1);
        * mMaxZoom value that the query interface returns is the size
         * of zoom table. So the actual max zoom value will be one
         * less than that value.          */

        mParameters.set("max-zoom",mMaxZoom-1);
        mParameters.set(CameraParameters::KEY_ZOOM_RATIOS,
                            mZoomRatioValues);
    } else
        {
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");
    }

    /* Enable zoom support for video application if VPE enabled */
    if(mZoomSupported) {
        mParameters.set("video-zoom-support", "true");
    } else {
        mParameters.set("video-zoom-support", "false");
    }

    //Set Live Snapshot support
    mParameters.set("video-snapshot-supported", "true");
    
    //Set Camera Mode
    mParameters.set(CameraParameters::KEY_CAMERA_MODE,0);

    //Set Antibanding
    mParameters.set(CameraParameters::KEY_ANTIBANDING,
                    CameraParameters::ANTIBANDING_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
                    mAntibandingValues);

    //Set Effect
    mParameters.set(CameraParameters::KEY_EFFECT,
                    CameraParameters::EFFECT_NONE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_EFFECTS, mEffectValues);

    //Set Auto Exposure
    mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE,
                    CameraParameters::AUTO_EXPOSURE_FRAME_AVG);
    mParameters.set(CameraParameters::KEY_SUPPORTED_AUTO_EXPOSURE, mAutoExposureValues);

    //Set WhiteBalance
    mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
                    CameraParameters::WHITE_BALANCE_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,mWhitebalanceValues);

    //Set Focus Mode
    if(mHasAutoFocusSupport){
       mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                    CameraParameters::FOCUS_MODE_AUTO);
       mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                    mFocusModeValues);
    } else {
       mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                   CameraParameters::FOCUS_MODE_INFINITY);
       mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                   CameraParameters::FOCUS_MODE_INFINITY);
    }
    //Set Flash
    if (cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_LED_MODE)) {
        mParameters.set(CameraParameters::KEY_FLASH_MODE,
                        CameraParameters::FLASH_MODE_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
                        mFlashValues);
    }

    //Set Sharpness
    mParameters.set(CameraParameters::KEY_MAX_SHARPNESS,
            CAMERA_MAX_SHARPNESS);
    mParameters.set(CameraParameters::KEY_SHARPNESS,
                    CAMERA_DEF_SHARPNESS);

    //Set Contrast
    mParameters.set(CameraParameters::KEY_MAX_CONTRAST,
            CAMERA_MAX_CONTRAST);
    mParameters.set(CameraParameters::KEY_CONTRAST,
                    CAMERA_DEF_CONTRAST);

    //Set Saturation
    mParameters.set(CameraParameters::KEY_MAX_SATURATION,
            CAMERA_MAX_SATURATION);
    mParameters.set(CameraParameters::KEY_SATURATION,
                    CAMERA_DEF_SATURATION);

    //Set Brightness/luma-adaptaion
    mParameters.set("luma-adaptation", "3");

    mParameters.set(CameraParameters::KEY_PICTURE_FORMAT,
                    CameraParameters::PIXEL_FORMAT_JPEG);

    //Set Lensshading
    mParameters.set(CameraParameters::KEY_LENSSHADE,
                    CameraParameters::LENSSHADE_ENABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_LENSSHADE_MODES,
                    mLensShadeValues);

    //Set ISO Mode
    mParameters.set(CameraParameters::KEY_ISO_MODE,
                    CameraParameters::ISO_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_ISO_MODES,
                    mIsoValues);

    //Set MCE
    mParameters.set(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT,
                    CameraParameters::MCE_ENABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_MEM_COLOR_ENHANCE_MODES,
                    mMceValues);
    //Set HFR
    if (cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_HFR)) {
        mParameters.set(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE,
                    CameraParameters::VIDEO_HFR_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_HFR_SIZES,
                    mHfrSizeValues.string());
        mParameters.set(CameraParameters::KEY_SUPPORTED_VIDEO_HIGH_FRAME_RATE_MODES,
                    mHfrValues);
	LOGE(" HFR supported");
    } else{
        mParameters.set(CameraParameters::KEY_SUPPORTED_HFR_SIZES,"");
	LOGE(" HFR not supported");
    }

    //Set Histogram
    mParameters.set(CameraParameters::KEY_HISTOGRAM,
                    CameraParameters::HISTOGRAM_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_HISTOGRAM_MODES,
                    mHistogramValues);

    //Set SkinTone Enhancement
    mParameters.set(CameraParameters::KEY_SKIN_TONE_ENHANCEMENT,
                    CameraParameters::SKIN_TONE_ENHANCEMENT_DISABLE);
    mParameters.set("skinToneEnhancement", "0");
    mParameters.set(CameraParameters::KEY_SUPPORTED_SKIN_TONE_ENHANCEMENT_MODES,
                    mSkinToneEnhancementValues);

    //Set Scene Mode
    mParameters.set(CameraParameters::KEY_SCENE_MODE,
                    CameraParameters::SCENE_MODE_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
                    mSceneModeValues);

    //Set Streaming Textures
    mParameters.set("strtextures", "OFF");

    //Set Denoise
    mParameters.set(CameraParameters::KEY_DENOISE,
                    CameraParameters::DENOISE_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_DENOISE,
                        denoise_value);
    //Set Touch AF/AEC
    mParameters.set(CameraParameters::KEY_TOUCH_AF_AEC,
                    CameraParameters::TOUCH_AF_AEC_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_TOUCH_AF_AEC,
                    mTouchAfAecValues);
    mParameters.set("touchAfAec-dx","100");
    mParameters.set("touchAfAec-dy","100");
    mParameters.set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS, "1");
    mParameters.set(CameraParameters::KEY_MAX_NUM_METERING_AREAS, "1");

    //Set Scene Detection
    mParameters.set(CameraParameters::KEY_SCENE_DETECT,
                   CameraParameters::SCENE_DETECT_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_DETECT,
                    mSceneDetectValues);

    //Set Selectable Zone AF
    mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF,
                    CameraParameters::SELECTABLE_ZONE_AF_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SELECTABLE_ZONE_AF,
                    mSelectableZoneAfValues);

    //Set Face Detection
    mParameters.set(CameraParameters::KEY_FACE_DETECTION,
                    CameraParameters::FACE_DETECTION_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_FACE_DETECTION,
                    mFaceDetectionValues);

    //Set Red Eye Reduction
    mParameters.set(CameraParameters::KEY_REDEYE_REDUCTION,
                    CameraParameters::REDEYE_REDUCTION_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_REDEYE_REDUCTION,
                    mRedeyeReductionValues);

    //Set ZSL
    mParameters.set(CameraParameters::KEY_ZSL,
                    CameraParameters::ZSL_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_ZSL_MODES,
                    mZslValues);

    //Set Focal length, horizontal and vertical view angles
    float focalLength = 0.0f;
    float horizontalViewAngle = 0.0f;
    float verticalViewAngle = 0.0f;
    cam_config_get_parm(mCameraId, MM_CAMERA_PARM_FOCAL_LENGTH,
            (void *)&focalLength);
    mParameters.setFloat(CameraParameters::KEY_FOCAL_LENGTH,
                    focalLength);
    cam_config_get_parm(mCameraId, MM_CAMERA_PARM_HORIZONTAL_VIEW_ANGLE,
            (void *)&horizontalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,
                    horizontalViewAngle);
    cam_config_get_parm(mCameraId, MM_CAMERA_PARM_VERTICAL_VIEW_ANGLE,
            (void *)&verticalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,
                    verticalViewAngle);

    //Set Exposure Compensation
    mParameters.set(
            CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION,
            EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR);
    mParameters.set(
            CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION,
            EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR);
    mParameters.set(
            CameraParameters::KEY_EXPOSURE_COMPENSATION,
            EXPOSURE_COMPENSATION_DEFAULT_NUMERATOR);
    mParameters.setFloat(
            CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP,
            EXPOSURE_COMPENSATION_STEP);

    mParameters.set("num-snaps-per-shutter", 1);

   // if(mIs3DModeOn)
   //     mParameters.set("3d-frame-format", "left-right");

    if (setParameters(mParameters) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
    //mUseOverlay = useOverlay();

    mInitialized = true;
    strTexturesOn = false;

    LOGI("%s: X", __func__);
    return;
}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported.
 */

int QCameraHardwareInterface::setParameters(const char *parms)
{
    CameraParameters param;
    String8 str = String8(parms);
    param.unflatten(str);
    status_t ret = setParameters(param);
	if(ret == NO_ERROR)
		return 0;
	else
		return -1;
}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported. */
status_t QCameraHardwareInterface::setParameters(const CameraParameters& params)
{
    status_t ret = NO_ERROR;

    LOGI("%s: E", __func__);
//    Mutex::Autolock l(&mLock);
    status_t rc, final_rc = NO_ERROR;

    if ((rc = setCameraMode(params)))                   final_rc = rc;
    if ((rc = setPreviewSize(params)))                  final_rc = rc;
    if ((rc = setRecordSize(params)))                   final_rc = rc;
    if ((rc = setPictureSize(params)))                  final_rc = rc;
    //  if ((rc = setJpegThumbnailSize(params)))        final_rc = rc;
    if ((rc = setJpegQuality(params)))                  final_rc = rc;
    if ((rc = setEffect(params)))                       final_rc = rc;
    if ((rc = setGpsLocation(params)))                  final_rc = rc;
    if ((rc = setRotation(params)))                     final_rc = rc;
    if ((rc = setZoom(params)))                         final_rc = rc;
    if ((rc = setOrientation(params)))                  final_rc = rc;
    if ((rc = setLensshadeValue(params)))               final_rc = rc;
    if ((rc = setMCEValue(params)))                     final_rc = rc;
    if ((rc = setPictureFormat(params)))                final_rc = rc;
    if ((rc = setSharpness(params)))                    final_rc = rc;
    if ((rc = setSaturation(params)))                   final_rc = rc;
    if ((rc = setTouchAfAec(params)))                   final_rc = rc;
    if ((rc = setSceneMode(params)))                    final_rc = rc;
    if ((rc = setContrast(params)))                     final_rc = rc;
    if ((rc = setSceneDetect(params)))                  final_rc = rc;
    if ((rc = setFaceDetect(params)))                   final_rc = rc;
    if ((rc = setStrTextures(params)))                  final_rc = rc;
    if ((rc = setPreviewFormat(params)))                final_rc = rc;
    if ((rc = setSkinToneEnhancement(params)))          final_rc = rc;
    if ((rc = setWaveletDenoise(params)))               final_rc = rc;
    if ((rc = setAntibanding(params)))                  final_rc = rc;
    //    if ((rc = setOverlayFormats(params)))         final_rc = rc;
    if ((rc = setRedeyeReduction(params)))              final_rc = rc;
    //    if ((rc = setDenoise(params)))                final_rc = rc;
    //    if ((rc = setPreviewFpsRange(params)))        final_rc = rc;
    if((rc = setRecordingHint(params)))                 final_rc = rc;
    if ((rc = setNumOfSnapshot(params)))                final_rc = rc;

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if((value != NOT_FOUND) && (value == CAMERA_BESTSHOT_OFF)) {
        if ((rc = setPreviewFrameRateMode(params)))     final_rc = rc;
        /* Fps mode has to be set before fps*/
        if ((rc = setPreviewFrameRate(params)))         final_rc = rc;
        if ((rc = setAutoExposure(params)))             final_rc = rc;
        if ((rc = setExposureCompensation(params)))     final_rc = rc;
        if ((rc = setWhiteBalance(params)))             final_rc = rc;
        if ((rc = setFlash(params)))                    final_rc = rc;
        if ((rc = setFocusMode(params)))                final_rc = rc;
        if ((rc = setBrightness(params)))               final_rc = rc;
        if ((rc = setISOValue(params)))                 final_rc = rc;
    }
    //selectableZoneAF needs to be invoked after continuous AF
    if ((rc = setSelectableZoneAf(params)))             final_rc = rc;   //@Guru : Need support from Lower level
    // setHighFrameRate needs to be done at end, as there can
    // be a preview restart, and need to use the updated parameters
    if ((rc = setHighFrameRate(params)))  final_rc = rc;

    final_rc=NO_ERROR;
   LOGI("%s: X", __func__);
   return final_rc;
}

/** Retrieve the camera parameters.  The buffer returned by the camera HAL
	must be returned back to it with put_parameters, if put_parameters
	is not NULL.
 */
int QCameraHardwareInterface::getParameters(char **parms)
{
    char* rc = NULL;
    String8 str;
    CameraParameters param = getParameters();
    str = param.flatten( );
    rc = (char *)malloc(sizeof(char)*(str.length()+1));
    strncpy(rc, str.string(), str.length());
	rc[str.length()] = 0;
	*parms = rc;
    return 0;
}

/** The camera HAL uses its own memory to pass us the parameters when we
	call get_parameters.  Use this function to return the memory back to
	the camera HAL, if put_parameters is not NULL.  If put_parameters
	is NULL, then you have to use free() to release the memory.
*/
void QCameraHardwareInterface::putParameters(char *rc)
{
    free(rc);
}

CameraParameters QCameraHardwareInterface::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t QCameraHardwareInterface::runFaceDetection()
{
    bool ret = true;

    const char *str = mParameters.get(CameraParameters::KEY_FACE_DETECTION);
    if (str != NULL) {
        int value = attr_lookup(facedetection,
                sizeof(facedetection) / sizeof(str_map), str);
#if 0
        mMetaDataWaitLock.lock();
        if (value == true) {
            if(mMetaDataHeap != NULL)
                mMetaDataHeap.clear();

            mMetaDataHeap =
                new AshmemPool((sizeof(int)*(MAX_ROI*4+1)),
                        1,
                        (sizeof(int)*(MAX_ROI*4+1)),
                        "metadata");
            if (!mMetaDataHeap->initialized()) {
                LOGE("Meta Data Heap allocation failed ");
                mMetaDataHeap.clear();
                LOGE("runFaceDetection X: error initializing mMetaDataHeap");
                mMetaDataWaitLock.unlock();
                return UNKNOWN_ERROR;
            }
            mSendMetaData = true;
        } else {
            if(mMetaDataHeap != NULL)
                mMetaDataHeap.clear();
        }
        mMetaDataWaitLock.unlock();
#endif
        cam_ctrl_dimension_t dim;
        cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
        preview_parm_config (&dim, mParameters);
        ret = cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
        ret = native_set_parms(MM_CAMERA_PARM_FD, sizeof(int8_t), (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    LOGE("Invalid Face Detection value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setSharpness(const CameraParameters& params)
{
    bool ret = false;
    int rc = MM_CAMERA_OK;
    LOGE("%s",__func__);
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_SHARPNESS);
    if(!rc) {
        LOGE("%s:CONTRAST not supported", __func__);
        return NO_ERROR;
    }
    int sharpness = params.getInt(CameraParameters::KEY_SHARPNESS);
    if((sharpness < CAMERA_MIN_SHARPNESS
            || sharpness > CAMERA_MAX_SHARPNESS))
        return UNKNOWN_ERROR;

    LOGV("setting sharpness %d", sharpness);
    mParameters.set(CameraParameters::KEY_SHARPNESS, sharpness);
    ret = native_set_parms(MM_CAMERA_PARM_SHARPNESS, sizeof(sharpness),
                               (void *)&sharpness);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QCameraHardwareInterface::setSaturation(const CameraParameters& params)
{
    bool ret = false;
    int rc = MM_CAMERA_OK;
    LOGE("%s",__func__);
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_SATURATION);
    if(!rc) {
        LOGE("%s:MM_CAMERA_PARM_SATURATION not supported", __func__);
        return NO_ERROR;
    }
    int result;
    int saturation = params.getInt(CameraParameters::KEY_SATURATION);

    if((saturation < CAMERA_MIN_SATURATION)
        || (saturation > CAMERA_MAX_SATURATION))
    return UNKNOWN_ERROR;

    LOGV("Setting saturation %d", saturation);
    mParameters.set(CameraParameters::KEY_SATURATION, saturation);
    ret = native_set_parms(MM_CAMERA_PARM_SATURATION, sizeof(saturation),
        (void *)&saturation, (int *)&result);
    if(result != MM_CAMERA_OK)
        LOGI("Saturation Value: %d is not set as the selected value is not supported", saturation);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QCameraHardwareInterface::setContrast(const CameraParameters& params)
{
   LOGE("%s E", __func__ );
   int rc = MM_CAMERA_OK;
   rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_CONTRAST);
   if(!rc) {
        LOGE("%s:CONTRAST not supported", __func__);
        return NO_ERROR;
    }
   const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
   LOGE("Contrast : %s",str);
   int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);
   if(value == CAMERA_BESTSHOT_OFF) {
        int contrast = params.getInt(CameraParameters::KEY_CONTRAST);
        if((contrast < CAMERA_MIN_CONTRAST)
                || (contrast > CAMERA_MAX_CONTRAST))
        {
            LOGE("Contrast Value not matching");
            return UNKNOWN_ERROR;
        }
        LOGV("setting contrast %d", contrast);
        mParameters.set(CameraParameters::KEY_CONTRAST, contrast);
        LOGE("Calling Contrast set on Lower layer");
        bool ret = native_set_parms(MM_CAMERA_PARM_CONTRAST, sizeof(contrast),
                                   (void *)&contrast);
        LOGE("Lower layer returned %d", ret);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
          LOGI(" Contrast value will not be set " \
          "when the scenemode selected is %s", str);
          return NO_ERROR;
    }
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setSceneDetect(const CameraParameters& params)
{
    LOGE("%s",__func__);
    bool retParm;
    int rc = MM_CAMERA_OK;

    rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_ASD_ENABLE);
    if(!rc) {
        LOGE("%s:MM_CAMERA_PARM_ASD_ENABLE not supported", __func__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_SCENE_DETECT);
    LOGE("Scene Detect string : %s",str);
    if (str != NULL) {
        int32_t value = attr_lookup(scenedetect, sizeof(scenedetect) / sizeof(str_map), str);
        LOGE("Scenedetect Value : %d",value);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_SCENE_DETECT, str);

            retParm = native_set_parms(MM_CAMERA_PARM_ASD_ENABLE, sizeof(value),
                                       (void *)&value);

            return retParm ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
   return BAD_VALUE;
}

status_t QCameraHardwareInterface::setZoom(const CameraParameters& params)
{
    status_t rc = NO_ERROR;

    LOGE("%s: E",__func__);


    if( !( cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_ZOOM))) {
        LOGE("%s:MM_CAMERA_PARM_ZOOM not supported", __func__);
        return NO_ERROR;
    }
    // No matter how many different zoom values the driver can provide, HAL
    // provides applictations the same number of zoom levels. The maximum driver
    // zoom value depends on sensor output (VFE input) and preview size (VFE
    // output) because VFE can only crop and cannot upscale. If the preview size
    // is bigger, the maximum zoom ratio is smaller. However, we want the
    // zoom ratio of each zoom level is always the same whatever the preview
    // size is. Ex: zoom level 1 is always 1.2x, zoom level 2 is 1.44x, etc. So,
    // we need to have a fixed maximum zoom value and do read it from the
    // driver.
    static const int ZOOM_STEP = 1;
    int32_t zoom_level = params.getInt("zoom");
    if(zoom_level >= 0 && zoom_level <= mMaxZoom-1) {
        mParameters.set("zoom", zoom_level);
        int32_t zoom_value = ZOOM_STEP * zoom_level;
        bool ret = native_set_parms(MM_CAMERA_PARM_ZOOM,
            sizeof(zoom_value), (void *)&zoom_value);
        if(ret) {
            mCurrentZoom=zoom_level;
        }
        rc = ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
        rc = BAD_VALUE;
    }
    LOGE("%s X",__func__);
    return rc;

}

status_t  QCameraHardwareInterface::setISOValue(const CameraParameters& params) {

    status_t rc = NO_ERROR;
    LOGE("%s",__func__);

    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_ISO);
    if(!rc) {
        LOGE("%s:MM_CAMERA_PARM_ISO not supported", __func__);
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_ISO_MODE);
    LOGE("ISO string : %s",str);
    int8_t temp_hjr;
    if (str != NULL) {
        int value = (camera_iso_mode_type)attr_lookup(
          iso, sizeof(iso) / sizeof(str_map), str);
        LOGE("ISO Value : %d",value);
        if (value != NOT_FOUND) {
            camera_iso_mode_type temp = (camera_iso_mode_type) value;
            if (value == CAMERA_ISO_DEBLUR) {
               temp_hjr = true;
               native_set_parms(MM_CAMERA_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
               mHJR = value;
            }
            else {
               if (mHJR == CAMERA_ISO_DEBLUR) {
                   temp_hjr = false;
                   native_set_parms(MM_CAMERA_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
                   mHJR = value;
               }
            }

            mParameters.set(CameraParameters::KEY_ISO_MODE, str);
            native_set_parms(MM_CAMERA_PARM_ISO, sizeof(camera_iso_mode_type), (void *)&temp);
            return NO_ERROR;
        }
    }
    return BAD_VALUE;
}


status_t QCameraHardwareInterface::setFocusMode(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_FOCUS_MODE);
    LOGE("%s",__func__);
    if (str != NULL) {

      LOGE("Focus mdoe %s",str);
        int32_t value = attr_lookup(focus_modes,
                                    sizeof(focus_modes) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FOCUS_MODE, str);

#if 0
            if(mHasAutoFocusSupport && (updateFocusDistances(str) != NO_ERROR)) {
                LOGE("%s: updateFocusDistances failed for %s", __FUNCTION__, str);
                return UNKNOWN_ERROR;
            }
#endif

            if(mHasAutoFocusSupport){
                int cafSupport = FALSE;
                if(!strcmp(str, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) ||
                   !strcmp(str, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE)){
                    cafSupport = TRUE;
                }
                LOGE("Continuous Auto Focus %d", cafSupport);
                bool ret = native_set_parms(MM_CAMERA_PARM_CONTINUOUS_AF, sizeof(cafSupport),
                                       (void *)&cafSupport);
            }

            return NO_ERROR;
        }
        LOGE("%s:Could not look up str value",__func__);
    }
    LOGE("Invalid focus mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setSceneMode(const CameraParameters& params)
{
    status_t rc = NO_ERROR;
    LOGE("%s",__func__);

    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_BESTSHOT_MODE);
    if(!rc) {
        LOGE("%s:Parameter Scenemode is not supported for this sensor", __func__);
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    LOGE("Scene Mode string : %s",str);

    if (str != NULL) {
        int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);
        LOGE("Setting Scenemode value = %d",value );
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_SCENE_MODE, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_BESTSHOT_MODE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid scenemode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setSelectableZoneAf(const CameraParameters& params)
{
    LOGE("%s",__func__);
    if(mHasAutoFocusSupport) {
        const char *str = params.get(CameraParameters::KEY_SELECTABLE_ZONE_AF);
        if (str != NULL) {
            int32_t value = attr_lookup(selectable_zone_af, sizeof(selectable_zone_af) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF, str);
                bool ret = native_set_parms(MM_CAMERA_PARM_FOCUS_RECT, sizeof(value),
                        (void *)&value);
                return ret ? NO_ERROR : UNKNOWN_ERROR;
            }
        }
        LOGE("Invalid selectable zone af value: %s", (str == NULL) ? "NULL" : str);

    }
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setEffect(const CameraParameters& params)
{
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    const char *str = params.get(CameraParameters::KEY_EFFECT);
    int result;
    if (str != NULL) {
        LOGE("Setting effect %s",str);
        int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
           rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_EFFECT);
           if(!rc) {
               LOGE("Camera Effect - %s mode is not supported for this sensor",str);
               return NO_ERROR;
           }else {
               mParameters.set(CameraParameters::KEY_EFFECT, str);
               LOGE("Setting effect to lower HAL : %d",value);
               bool ret = native_set_parms(MM_CAMERA_PARM_EFFECT, sizeof(value),
                                           (void *)&value,(int *)&result);
                if(result != MM_CAMERA_OK) {
                    LOGI("Camera Effect: %s is not set as the selected value is not supported ", str);
                }
               return ret ? NO_ERROR : UNKNOWN_ERROR;
          }
        }
    }
    LOGE("Invalid effect value: %s", (str == NULL) ? "NULL" : str);
    LOGE("setEffect X");
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setBrightness(const CameraParameters& params) {

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_BRIGHTNESS);
   if(!rc) {
       LOGE("MM_CAMERA_PARM_BRIGHTNESS mode is not supported for this sensor");
       return NO_ERROR;
   }
   int brightness = params.getInt("luma-adaptation");
   if (mBrightness !=  brightness) {
       LOGV(" new brightness value : %d ", brightness);
       mBrightness =  brightness;
       mParameters.set("luma-adaptation", brightness);
       bool ret = native_set_parms(MM_CAMERA_PARM_BRIGHTNESS, sizeof(mBrightness),
                                   (void *)&mBrightness);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
   }

    return NO_ERROR;
}

status_t QCameraHardwareInterface::setAutoExposure(const CameraParameters& params)
{

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_EXPOSURE);
   if(!rc) {
       LOGE("MM_CAMERA_PARM_EXPOSURE mode is not supported for this sensor");
       return NO_ERROR;
   }
   const char *str = params.get(CameraParameters::KEY_AUTO_EXPOSURE);
    if (str != NULL) {
        int32_t value = attr_lookup(autoexposure, sizeof(autoexposure) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_EXPOSURE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid auto exposure value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setExposureCompensation(
        const CameraParameters & params){
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_EXPOSURE_COMPENSATION);
    if(!rc) {
       LOGE("MM_CAMERA_PARM_EXPOSURE_COMPENSATION mode is not supported for this sensor");
       return NO_ERROR;
    }
    int numerator = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    if(EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR <= numerator &&
            numerator <= EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR){
        int16_t  numerator16 = (int16_t)(numerator & 0x0000ffff);
        uint16_t denominator16 = EXPOSURE_COMPENSATION_DENOMINATOR;
        uint32_t  value = 0;
        value = numerator16 << 16 | denominator16;

        mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
                            numerator);
       bool ret = native_set_parms(MM_CAMERA_PARM_EXPOSURE_COMPENSATION,
                                    sizeof(value), (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    LOGE("Invalid Exposure Compensation");
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setWhiteBalance(const CameraParameters& params)
{

     LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_WHITE_BALANCE);
    if(!rc) {
       LOGE("MM_CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
       return NO_ERROR;
    }
     int result;

    const char *str = params.get(CameraParameters::KEY_WHITE_BALANCE);
    if (str != NULL) {
        int32_t value = attr_lookup(whitebalance, sizeof(whitebalance) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_WHITE_BALANCE, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_WHITE_BALANCE, sizeof(value),
                                       (void *)&value, (int *)&result);
            if(result != MM_CAMERA_OK) {
                LOGI("WhiteBalance Value: %s is not set as the selected value is not supported ", str);
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid whitebalance value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}
status_t QCameraHardwareInterface::setAntibanding(const CameraParameters& params)
{
    int result;

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_ANTIBANDING);
    if(!rc) {
       LOGE("ANTIBANDING mode is not supported for this sensor");
       return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_ANTIBANDING);
    if (str != NULL) {
        int value = (camera_antibanding_type)attr_lookup(
          antibanding, sizeof(antibanding) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            camera_antibanding_type temp = (camera_antibanding_type) value;
            LOGE("Antibanding Value : %d",value);
            mParameters.set(CameraParameters::KEY_ANTIBANDING, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_ANTIBANDING,
                       sizeof(camera_antibanding_type), (void *)&value ,(int *)&result);
            if(result != MM_CAMERA_OK) {
                LOGI("AntiBanding Value: %s is not supported for the given BestShot Mode", str);
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid antibanding value: %s", (str == NULL) ? "NULL" : str);

    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setPreviewFrameRate(const CameraParameters& params)
{
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_FPS);
    if(!rc) {
       LOGE("MM_CAMERA_PARM_FPS is not supported for this sensor");
       return NO_ERROR;
    }
    uint16_t previousFps = (uint16_t)mParameters.getPreviewFrameRate();
    uint16_t fps = (uint16_t)params.getPreviewFrameRate();
    LOGV("requested preview frame rate  is %u", fps);

    if(mInitialized && (fps == previousFps)){
        LOGV("No change is FPS Value %d",fps );
        return NO_ERROR;
    }

    if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
        mParameters.setPreviewFrameRate(fps);
        bool ret = native_set_parms(MM_CAMERA_PARM_FPS,
                sizeof(fps), (void *)&fps);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }

    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setPreviewFrameRateMode(const CameraParameters& params) {

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_FPS);
    if(!rc) {
       LOGE(" CAMERA FPS mode is not supported for this sensor");
       return NO_ERROR;
    }
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_FPS_MODE);
    if(!rc) {
       LOGE("CAMERA FPS MODE mode is not supported for this sensor");
       return NO_ERROR;
    }

    const char *previousMode = mParameters.getPreviewFrameRateMode();
    const char *str = params.getPreviewFrameRateMode();
    if (NULL == previousMode) {
        LOGE("Preview Frame Rate Mode is NULL\n");
        return NO_ERROR;
    }
    if (NULL == str) {
        LOGE("Preview Frame Rate Mode is NULL\n");
        return NO_ERROR;
    }
    if( mInitialized && !strcmp(previousMode, str)) {
        LOGE("frame rate mode same as previous mode %s", previousMode);
        return NO_ERROR;
    }
    int32_t frameRateMode = attr_lookup(frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map),str);
    if(frameRateMode != NOT_FOUND) {
        LOGV("setPreviewFrameRateMode: %s ", str);
        mParameters.setPreviewFrameRateMode(str);
        bool ret = native_set_parms(MM_CAMERA_PARM_FPS_MODE, sizeof(frameRateMode), (void *)&frameRateMode);
        if(!ret) return ret;
        //set the fps value when chaging modes
        int16_t fps = (uint16_t)params.getPreviewFrameRate();
        if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
            mParameters.setPreviewFrameRate(fps);
            ret = native_set_parms(MM_CAMERA_PARM_FPS,
                                        sizeof(fps), (void *)&fps);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        LOGE("Invalid preview frame rate value: %d", fps);
        return BAD_VALUE;
    }
    LOGE("Invalid preview frame rate mode value: %s", (str == NULL) ? "NULL" : str);

    return BAD_VALUE;
}


status_t QCameraHardwareInterface::setTouchAfAec(const CameraParameters& params)
{
    LOGE("%s",__func__);
    if(mHasAutoFocusSupport){
        int xAec, yAec, xAf, yAf;
        int cx, cy;
        int width, height;
        params.getMeteringAreaCenter(&cx, &cy);
        getPreviewSize(&width, &height);

        // @Punit
        // The coords sent from upper layer is in range (-1000, -1000) to (1000, 1000)
        // So, they are transformed to range (0, 0) to (previewWidth, previewHeight)
        cx = cx + 1000;
        cy = cy + 1000;
        cx = cx * (width / 2000.0f);
        cy = cy * (height / 2000.0f);

        //Negative values are invalid and does not update anything
        LOGE("Touch Area Center (cx, cy) = (%d, %d)", cx, cy);

        //Currently using same values for AF and AEC
        xAec = cx; yAec = cy;
        xAf = cx; yAf = cy;

        const char *str = params.get(CameraParameters::KEY_TOUCH_AF_AEC);
        if (str != NULL) {
            int value = attr_lookup(touchafaec,
                    sizeof(touchafaec) / sizeof(str_map), str);
            if (value != NOT_FOUND) {

                //Dx,Dy will be same as defined in res/layout/camera.xml
                //passed down to HAL in a key.value pair.
                int FOCUS_RECTANGLE_DX = params.getInt("touchAfAec-dx");
                int FOCUS_RECTANGLE_DY = params.getInt("touchAfAec-dy");
                mParameters.set(CameraParameters::KEY_TOUCH_AF_AEC, str);
                mParameters.setTouchIndexAec(xAec, yAec);
                mParameters.setTouchIndexAf(xAf, yAf);

                cam_set_aec_roi_t aec_roi_value;
                roi_info_t af_roi_value;

                memset(&af_roi_value, 0, sizeof(roi_info_t));

                //If touch AF/AEC is enabled and touch event has occured then
                //call the ioctl with valid values.
                if (value == true
                        && (xAec >= 0 && yAec >= 0)
                        && (xAf >= 0 && yAf >= 0)) {
                    //Set Touch AEC params (Pass the center co-ordinate)
                    aec_roi_value.aec_roi_enable = AEC_ROI_ON;
                    aec_roi_value.aec_roi_type = AEC_ROI_BY_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.x = xAec;
                    aec_roi_value.aec_roi_position.coordinate.y = yAec;

                    //Set Touch AF params (Pass the top left co-ordinate)
                    af_roi_value.num_roi = 1;
                    if ((xAf-50) < 0)
                        af_roi_value.roi[0].x = 1;
                    else
                        af_roi_value.roi[0].x = xAf - (FOCUS_RECTANGLE_DX/2);

                    if ((yAf-50) < 0)
                        af_roi_value.roi[0].y = 1;
                    else
                        af_roi_value.roi[0].y = yAf - (FOCUS_RECTANGLE_DY/2);

                    af_roi_value.roi[0].dx = FOCUS_RECTANGLE_DX;
                    af_roi_value.roi[0].dy = FOCUS_RECTANGLE_DY;
                    af_roi_value.is_multiwindow = mMultiTouch;
                    native_set_parms(MM_CAMERA_PARM_AEC_ROI, sizeof(cam_set_aec_roi_t), (void *)&aec_roi_value);
                    native_set_parms(MM_CAMERA_PARM_AF_ROI, sizeof(roi_info_t), (void*)&af_roi_value);
                }
                else if(value == false) {
                    //Set Touch AEC params
                    aec_roi_value.aec_roi_enable = AEC_ROI_OFF;
                    aec_roi_value.aec_roi_type = AEC_ROI_BY_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.x = DONT_CARE_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.y = DONT_CARE_COORDINATE;

                    //Set Touch AF params
                    af_roi_value.num_roi = 0;
                    native_set_parms(MM_CAMERA_PARM_AEC_ROI, sizeof(cam_set_aec_roi_t), (void *)&aec_roi_value);
                    native_set_parms(MM_CAMERA_PARM_AF_ROI, sizeof(roi_info_t), (void*)&af_roi_value);
                }
                //@Punit: If the values are negative, we dont send anything to the lower layer
            }
            return NO_ERROR;
        }
        LOGE("Invalid Touch AF/AEC value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setSkinToneEnhancement(const CameraParameters& params) {
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_SCE_FACTOR);
    if(!rc) {
       LOGE("SkinToneEnhancement is not supported for this sensor");
       return NO_ERROR;
    }
     int skinToneValue = params.getInt("skinToneEnhancement");
     if (mSkinToneEnhancement != skinToneValue) {
          LOGV(" new skinTone correction value : %d ", skinToneValue);
          mSkinToneEnhancement = skinToneValue;
          mParameters.set("skinToneEnhancement", skinToneValue);
          bool ret = native_set_parms(MM_CAMERA_PARM_SCE_FACTOR, sizeof(mSkinToneEnhancement),
                        (void *)&mSkinToneEnhancement);
          return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setWaveletDenoise(const CameraParameters& params) {
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_WAVELET_DENOISE);
    if(rc != MM_CAMERA_PARM_SUPPORT_SET) {
        LOGE("Wavelet Denoise is not supported for this sensor");
        /* TO DO */
//        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_DENOISE);
    if (str != NULL) {
        int value = attr_lookup(denoise,
                sizeof(denoise) / sizeof(str_map), str);
        if ((value != NOT_FOUND) &&  (mDenoiseValue != value)) {
            mDenoiseValue =  value;
            mParameters.set(CameraParameters::KEY_DENOISE, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_WAVELET_DENOISE, sizeof(value),
                    (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        return NO_ERROR;
    }
    LOGE("Invalid Denoise value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setRecordSize(const CameraParameters& params)
{
    const char *recordSize = NULL;
    recordSize = params.get("record-size");
    if(!recordSize) {
        mParameters.set("record-size", "");
        //If application didn't set this parameter string, use the values from
        //getPreviewSize() as video dimensions.
        LOGV("No Record Size requested, use the preview dimensions");
        videoWidth = previewWidth;
        videoHeight = previewHeight;
    } else {
        //Extract the record witdh and height that application requested.
        LOGI("%s: requested record size %s", __FUNCTION__, recordSize);
        if(!parse_size(recordSize, videoWidth, videoHeight)) {
            mParameters.set("record-size" , recordSize);
            //VFE output1 shouldn't be greater than VFE output2.
            if( (previewWidth > videoWidth) || (previewHeight > videoHeight)) {
                //Set preview sizes as record sizes.
                LOGI("Preview size %dx%d is greater than record size %dx%d,\
                   resetting preview size to record size",previewWidth,\
                     previewHeight, videoWidth, videoHeight);
                previewWidth = videoWidth;
                previewHeight = videoHeight;
                mParameters.setPreviewSize(previewWidth, previewHeight);
            }

            if(mIs3DModeOn == true) {
                /* As preview and video frames are same in 3D mode,
                 * preview size should be same as video size. This
                 * cahnge is needed to take of video resolutions
                 * like 720P and 1080p where the application can
                 * request different preview sizes like 768x432
                 */
                previewWidth = videoWidth;
                previewHeight = videoHeight;
                mParameters.setPreviewSize(previewWidth, previewHeight);
            }
        } else {
            mParameters.set("record-size", "");
            LOGE("initPreview X: failed to parse parameter record-size (%s)", recordSize);
            return BAD_VALUE;
        }
    }
    LOGI("%s: preview dimensions: %dx%d", __FUNCTION__, previewWidth, previewHeight);
    LOGI("%s: video dimensions: %dx%d", __FUNCTION__, videoWidth, videoHeight);
    mDimension.display_width = previewWidth;
    mDimension.display_height= previewHeight;
    mDimension.orig_video_width = videoWidth;
    mDimension.orig_video_height = videoHeight;
    mDimension.video_width = videoWidth;
    mDimension.video_height = videoHeight;

    return NO_ERROR;
}

status_t QCameraHardwareInterface::setCameraMode(const CameraParameters& params) {
    int32_t value = params.getInt(CameraParameters::KEY_CAMERA_MODE);
    mParameters.set(CameraParameters::KEY_CAMERA_MODE,value);

    LOGI("ZSL is enabled  %d", value);
    if (value == 1) {
        myMode = (camera_mode_t)(myMode | CAMERA_ZSL_MODE);
    } else {
        myMode = (camera_mode_t)(myMode & ~CAMERA_ZSL_MODE);
    }
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setPreviewSize(const CameraParameters& params)
{
    int width, height;
    params.getPreviewSize(&width, &height);
    LOGE("################requested preview size %d x %d", width, height);

    // Validate the preview size
    for (size_t i = 0; i <  mPreviewSizeCount; ++i) {
        if (width ==  mPreviewSizes[i].width
           && height ==  mPreviewSizes[i].height) {
            mParameters.setPreviewSize(width, height);
            previewWidth = width;
            previewHeight = height;
            mDimension.display_width = width;
            mDimension.display_height= height;
            return NO_ERROR;
        }
    }
    LOGE("Invalid preview size requested: %dx%d", width, height);
    return BAD_VALUE;
}
status_t QCameraHardwareInterface::setPreviewFpsRange(const CameraParameters& params)
{
    int minFps,maxFps;

    params.getPreviewFpsRange(&minFps,&maxFps);
    LOGE("FPS Range Values: %dx%d", minFps, maxFps);

    for(size_t i=0;i<FPS_RANGES_SUPPORTED_COUNT;i++)
    {
        if(minFps==FpsRangesSupported[i].minFPS && maxFps == FpsRangesSupported[i].maxFPS){
            LOGV("i=%d : minFps = %d, maxFps = %d ",i,FpsRangesSupported[i].minFPS,FpsRangesSupported[i].maxFPS );
            mParameters.setPreviewFpsRange(minFps,maxFps);
            return NO_ERROR;
        }
    }

    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setJpegThumbnailSize(const CameraParameters& params){
    int width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int height = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    LOGE("requested jpeg thumbnail size %d x %d", width, height);

    // Validate the picture size
    for (unsigned int i = 0; i < JPEG_THUMBNAIL_SIZE_COUNT; ++i) {
       if (width == jpeg_thumbnail_sizes[i].width
         && height == jpeg_thumbnail_sizes[i].height) {
           mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, width);
           mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, height);
           return NO_ERROR;
       }
    }
    return BAD_VALUE;
}
status_t QCameraHardwareInterface::setPictureSize(const CameraParameters& params)
{
    int width, height;
    LOGE("QualcommCameraHardware::setPictureSize E");
    params.getPictureSize(&width, &height);
    LOGE("requested picture size %d x %d", width, height);

    // Validate the picture size
    for (int i = 0; i < mSupportedPictureSizesCount; ++i) {
        if (width == mPictureSizesPtr[i].width
          && height == mPictureSizesPtr[i].height) {
            mParameters.setPictureSize(width, height);
            mDimension.picture_width = width;
            mDimension.picture_height = height;
            return NO_ERROR;
        }
    }
    /* Dimension not among the ones in the list. Check if
     * its a valid dimension, if it is, then configure the
     * camera accordingly. else reject it.
     */
    if( isValidDimension(width, height) ) {
        mParameters.setPictureSize(width, height);
        mDimension.picture_width = width;
        mDimension.picture_height = height;
        return NO_ERROR;
    } else
        LOGE("Invalid picture size requested: %dx%d", width, height);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setJpegRotation(void) {
    int rotation = mParameters.getInt("rotation");
    return mm_jpeg_encoder_setRotation(rotation);
}

int QCameraHardwareInterface::getJpegRotation(void) {
    int rotation = mParameters.getInt("rotation");
    return rotation;
}

status_t QCameraHardwareInterface::setJpegQuality(const CameraParameters& params) {
    status_t rc = NO_ERROR;
    int quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
    LOGE("setJpegQuality E");
    if (quality >= 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg quality=%d", quality);
        rc = BAD_VALUE;
    }

    quality = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (quality >= 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg thumbnail quality=%d", quality);
        rc = BAD_VALUE;
    }
    LOGE("setJpegQuality X");
    return rc;
}

status_t QCameraHardwareInterface::
setNumOfSnapshot(const CameraParameters& params) {
    status_t rc = NO_ERROR;

    int num_of_snapshot = getNumOfSnapshots();

    if (num_of_snapshot <= 0) {
        num_of_snapshot = 1;
    }
    LOGI("number of snapshots = %d", num_of_snapshot);
    mParameters.set("num-snaps-per-shutter", num_of_snapshot);

    bool result = native_set_parms(MM_CAMERA_PARM_SNAPSHOT_BURST_NUM,
                                   sizeof(int),
                                   (void *)&num_of_snapshot);
    if(!result)
        LOGI("%s:Failure setting number of snapshots!!!", __func__);
    return rc;
}

status_t QCameraHardwareInterface::setPreviewFormat(const CameraParameters& params) {
    const char *str = params.getPreviewFormat();
    int32_t previewFormat = attr_lookup(preview_formats, sizeof(preview_formats) / sizeof(str_map), str);
    if(previewFormat != NOT_FOUND) {
        preview_format_info_t format_info;
        int num = sizeof(preview_format_info_list)/sizeof(preview_format_info_t);
        int i;
        for (i = 0; i < num; i++) {
          if (preview_format_info_list[i].Hal_format == previewFormat) {
            mPreviewFormatInfo = preview_format_info_list[i];
            break;
          }
        }
        if (i == num) {
          mPreviewFormatInfo.mm_cam_format = CAMERA_YUV_420_NV21;
          mPreviewFormatInfo.padding = CAMERA_PAD_TO_WORD;
          return BAD_VALUE;
        }
        bool ret = native_set_parms(MM_CAMERA_PARM_PREVIEW_FORMAT, sizeof(cam_format_t),
                                   (void *)&format_info.mm_cam_format);
        mParameters.set(CameraParameters::KEY_PREVIEW_FORMAT, str);
        mPreviewFormat = previewFormat;
        LOGE("Setting preview format to %d",mPreviewFormat);
        return NO_ERROR;
    } else if ( strTexturesOn ) {
      mPreviewFormatInfo.mm_cam_format = CAMERA_YUV_420_NV21;
      mPreviewFormatInfo.padding = CAMERA_PAD_TO_4K;
    } else {
      mPreviewFormatInfo.mm_cam_format = CAMERA_YUV_420_NV21;
      mPreviewFormatInfo.padding = CAMERA_PAD_TO_WORD;
    }
    LOGE("Invalid preview format value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setStrTextures(const CameraParameters& params) {
    const char *str = params.get("strtextures");
    if(str != NULL) {
        LOGV("strtextures = %s", str);
        mParameters.set("strtextures", str);
        if(!strncmp(str, "on", 2) || !strncmp(str, "ON", 2)) {
            LOGI("Resetting mUseOverlay to false");
            strTexturesOn = true;
            mUseOverlay = false;
        } else if (!strncmp(str, "off", 3) || !strncmp(str, "OFF", 3)) {
            strTexturesOn = false;
            mUseOverlay = true;
        }
    }
    return NO_ERROR;
}
status_t QCameraHardwareInterface::setFlash(const CameraParameters& params)
{
    LOGI("%s: E",__func__);
    int rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_LED_MODE);
    if(!rc) {
        LOGE("%s:LED FLASH not supported", __func__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_FLASH_MODE);
    if (str != NULL) {
        int32_t value = attr_lookup(flash, sizeof(flash) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FLASH_MODE, str);
            bool ret = native_set_parms(MM_CAMERA_PARM_LED_MODE,
                                       sizeof(value), (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid flash mode value: %s", (str == NULL) ? "NULL" : str);

    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setOverlayFormats(const CameraParameters& params)
{
    mParameters.set("overlay-format", HAL_PIXEL_FORMAT_YCbCr_420_SP);
    if(mIs3DModeOn == true) {
       int ovFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP|HAL_3D_IN_SIDE_BY_SIDE_L_R|HAL_3D_OUT_SIDE_BY_SIDE;
        mParameters.set("overlay-format", ovFormat);
    }
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setMCEValue(const CameraParameters& params)
{
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = cam_config_is_parm_supported(mCameraId,MM_CAMERA_PARM_MCE);
   if(!rc) {
       LOGE("MM_CAMERA_PARM_MCE mode is not supported for this sensor");
       return NO_ERROR;
   }
   const char *str = params.get(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT);
    if (str != NULL) {
        int value = attr_lookup(mce, sizeof(mce) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int temp = (int8_t)value;
            LOGI("%s: setting MCE value of %s", __FUNCTION__, str);
            mParameters.set(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT, str);

            native_set_parms(MM_CAMERA_PARM_MCE, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid MCE value: %s", (str == NULL) ? "NULL" : str);

    return NO_ERROR;
}

status_t QCameraHardwareInterface::setHighFrameRate(const CameraParameters& params)
{

    bool mCameraRunning;

    LOGE(": Entering SetHFR");
    int rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_HFR);
    if(!rc) {
        LOGE("%s: MM_CAMERA_PARM_HFR not supported", __func__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
    if (str != NULL) {
        int value = attr_lookup(hfr, sizeof(hfr) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int32_t temp = (int32_t)value;
            LOGE("%s: setting HFR value of %s(%d)", __FUNCTION__, str, temp);
            //Check for change in HFR value
            const char *oldHfr = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
            if(strcmp(oldHfr, str)){
                LOGE("%s: old HFR: %s, new HFR %s", __FUNCTION__, oldHfr, str);
                mParameters.set(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE, str);
//              mHFRMode = true;
		mCameraRunning=isPreviewRunning();
                if(mCameraRunning == true) {
//                    mHFRThreadWaitLock.lock();
//                    pthread_attr_t pattr;
//                    pthread_attr_init(&pattr);
//                    pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
//                    mHFRThreadRunning = !pthread_create(&mHFRThread,
//                                      &pattr,
//                                      hfr_thread,
//                                      (void*)NULL);
//                    mHFRThreadWaitLock.unlock();
 		    stopPreview();
                    native_set_parms(MM_CAMERA_PARM_HFR, sizeof(int32_t), (void *)&temp);
                    startPreview();
                    return NO_ERROR;
                }
            }
            native_set_parms(MM_CAMERA_PARM_HFR, sizeof(int32_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid HFR value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setLensshadeValue(const CameraParameters& params)
{

    int rc = cam_config_is_parm_supported(mCameraId, MM_CAMERA_PARM_ROLLOFF);
    if(!rc) {
        LOGE("%s:LENS SHADING not supported", __func__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_LENSSHADE);
    if (str != NULL) {
        int value = attr_lookup(lensshade,
                                    sizeof(lensshade) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int8_t temp = (int8_t)value;
            mParameters.set(CameraParameters::KEY_LENSSHADE, str);
            native_set_parms(MM_CAMERA_PARM_ROLLOFF, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid lensShade value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setFaceDetect(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_FACE_DETECTION);
    LOGE("setFaceDetect: %s", str);
    if (str != NULL) {
        int value = attr_lookup(facedetection,
                sizeof(facedetection) / sizeof(str_map), str);
        mFaceDetectOn = value;
        LOGE("%s Face detection value = %d",__func__, value);
        cam_ctrl_dimension_t dim;
        cam_config_get_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
        preview_parm_config (&dim, mParameters);
        cam_config_set_parm(mCameraId, MM_CAMERA_PARM_DIMENSION,&dim);
        native_set_parms(MM_CAMERA_PARM_FD, sizeof(int8_t), (void *)&value);
        mParameters.set(CameraParameters::KEY_FACE_DETECTION, str);
        return NO_ERROR;
    }
    LOGE("Invalid Face Detection value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}
status_t QCameraHardwareInterface::setFaceDetection(const char *str)
{
    if(supportsFaceDetection() == false){
        LOGE("Face detection is not enabled");
        return NO_ERROR;
    }
    if (str != NULL) {
        int value = attr_lookup(facedetection,
                                    sizeof(facedetection) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mMetaDataWaitLock.lock();
            mFaceDetectOn = value;
            mMetaDataWaitLock.unlock();
            mParameters.set(CameraParameters::KEY_FACE_DETECTION, str);
            return NO_ERROR;
        }
    }
    LOGE("Invalid Face Detection value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setRedeyeReduction(const CameraParameters& params)
{
    if(supportsRedEyeReduction() == false) {
        LOGE("Parameter Redeye Reduction is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_REDEYE_REDUCTION);
    if (str != NULL) {
        int value = attr_lookup(redeye_reduction, sizeof(redeye_reduction) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int8_t temp = (int8_t)value;
            LOGI("%s: setting Redeye Reduction value of %s", __FUNCTION__, str);
            mParameters.set(CameraParameters::KEY_REDEYE_REDUCTION, str);

            native_set_parms(MM_CAMERA_PARM_REDEYE_REDUCTION, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid Redeye Reduction value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setGpsLocation(const CameraParameters& params)
{
    const char *method = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    if (method) {
        mParameters.set(CameraParameters::KEY_GPS_PROCESSING_METHOD, method);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    }

    const char *latitude = params.get(CameraParameters::KEY_GPS_LATITUDE);
    if (latitude) {
        LOGE("latitude %s",latitude);
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE, latitude);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_LATITUDE);
    }

    const char *latitudeRef = params.get(CameraParameters::KEY_GPS_LATITUDE_REF);
    if (latitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE_REF, latitudeRef);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_LATITUDE_REF);
    }

    const char *longitude = params.get(CameraParameters::KEY_GPS_LONGITUDE);
    if (longitude) {
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE, longitude);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_LONGITUDE);
    }

    const char *longitudeRef = params.get(CameraParameters::KEY_GPS_LONGITUDE_REF);
    if (longitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE_REF, longitudeRef);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_LONGITUDE_REF);
    }

    const char *altitudeRef = params.get(CameraParameters::KEY_GPS_ALTITUDE_REF);
    if (altitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE_REF, altitudeRef);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_ALTITUDE_REF);
    }

    const char *altitude = params.get(CameraParameters::KEY_GPS_ALTITUDE);
    if (altitude) {
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE, altitude);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_ALTITUDE);
    }

    const char *status = params.get(CameraParameters::KEY_GPS_STATUS);
    if (status) {
        mParameters.set(CameraParameters::KEY_GPS_STATUS, status);
    }

    const char *dateTime = params.get(CameraParameters::KEY_EXIF_DATETIME);
    if (dateTime) {
        mParameters.set(CameraParameters::KEY_EXIF_DATETIME, dateTime);
    }else {
         mParameters.remove(CameraParameters::KEY_EXIF_DATETIME);
    }

    const char *timestamp = params.get(CameraParameters::KEY_GPS_TIMESTAMP);
    if (timestamp) {
        mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, timestamp);
    }else {
         mParameters.remove(CameraParameters::KEY_GPS_TIMESTAMP);
    }
    LOGE("setGpsLocation X");
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setRotation(const CameraParameters& params)
{
    status_t rc = NO_ERROR;
    int rotation = params.getInt(CameraParameters::KEY_ROTATION);
    if (rotation != NOT_FOUND) {
        if (rotation == 0 || rotation == 90 || rotation == 180
            || rotation == 270) {
          mParameters.set(CameraParameters::KEY_ROTATION, rotation);
          mRotation = rotation;
        } else {
            LOGE("Invalid rotation value: %d", rotation);
            rc = BAD_VALUE;
        }
    }
    LOGE("setRotation");
    return rc;
}

status_t QCameraHardwareInterface::setDenoise(const CameraParameters& params)
{
#if 0
    if(!mCfgControl.mm_camera_is_supported(MM_CAMERA_PARM_WAVELET_DENOISE)) {
        LOGE("Wavelet Denoise is not supported for this sensor");
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_DENOISE);
    if (str != NULL) {
        int value = attr_lookup(denoise,
        sizeof(denoise) / sizeof(str_map), str);
        if ((value != NOT_FOUND) &&  (mDenoiseValue != value)) {
        mDenoiseValue =  value;
        mParameters.set(CameraParameters::KEY_DENOISE, str);
        bool ret = native_set_parms(MM_CAMERA_PARM_WAVELET_DENOISE, sizeof(value),
                                               (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        return NO_ERROR;
    }
    LOGE("Invalid Denoise value: %s", (str == NULL) ? "NULL" : str);
#endif
    return BAD_VALUE;
}

status_t QCameraHardwareInterface::setOrientation(const CameraParameters& params)
{
    const char *str = params.get("orientation");

    if (str != NULL) {
        if (strcmp(str, "portrait") == 0 || strcmp(str, "landscape") == 0) {
            // Camera service needs this to decide if the preview frames and raw
            // pictures should be rotated.
            mParameters.set("orientation", str);
        } else {
            LOGE("Invalid orientation value: %s", str);
            return BAD_VALUE;
        }
    }
    return NO_ERROR;
}

status_t QCameraHardwareInterface::setPictureFormat(const CameraParameters& params)
{
    const char * str = params.get(CameraParameters::KEY_PICTURE_FORMAT);

    if(str != NULL){
        int32_t value = attr_lookup(picture_formats,
                                    sizeof(picture_formats) / sizeof(str_map), str);
        if(value != NOT_FOUND){
            mParameters.set(CameraParameters::KEY_PICTURE_FORMAT, str);
        } else {
            LOGE("Invalid Picture Format value: %s", str);
            return BAD_VALUE;
        }
    }
    return NO_ERROR;
}


status_t QCameraHardwareInterface::setRecordingHint(const CameraParameters& params)
{

  const char * str = params.get(CameraParameters::KEY_RECORDING_HINT);

  if(str != NULL){
      int32_t value = attr_lookup(recording_Hints,
                                  sizeof(recording_Hints) / sizeof(str_map), str);
      if(value != NOT_FOUND){

        native_set_parms(MM_CAMERA_PARM_RECORDING_HINT, sizeof(value),
                                               (void *)&value);
        native_set_parms(MM_CAMERA_PARM_CAF_ENABLE, sizeof(value),
                                               (void *)&value);
        mParameters.set(CameraParameters::KEY_RECORDING_HINT, str);
      } else {
          LOGE("Invalid Picture Format value: %s", str);
          return BAD_VALUE;
      }
  }
  return NO_ERROR;
}

isp3a_af_mode_t QCameraHardwareInterface::getAutoFocusMode(
  const CameraParameters& params)
{

  isp3a_af_mode_t afMode = AF_MODE_MAX;
  const char * focusMode = params.get(CameraParameters::KEY_FOCUS_MODE);

  if (focusMode ) {
    afMode = (isp3a_af_mode_t)attr_lookup(focus_modes,
      sizeof(focus_modes) / sizeof(str_map),
      params.get(CameraParameters::KEY_FOCUS_MODE));
  }
  return afMode;
}

void QCameraHardwareInterface::getPictureSize(int *picture_width,
                                              int *picture_height) const
{
    mParameters.getPictureSize(picture_width, picture_height);
}

void QCameraHardwareInterface::getPreviewSize(int *preview_width,
                                              int *preview_height) const
{

    mParameters.getPreviewSize(preview_width, preview_height);
}

cam_format_t QCameraHardwareInterface::getPreviewFormat() const
{
  cam_format_t foramt = CAMERA_YUV_420_NV21;
    const char *str = mParameters.getPreviewFormat();
    int32_t value = attr_lookup(preview_formats,
                                sizeof(preview_formats)/sizeof(str_map),
                                str);

    if(value != NOT_FOUND) {
        int num = sizeof(preview_format_info_list)/sizeof(preview_format_info_t);
        int i;
        for (i = 0; i < num; i++) {
          if (preview_format_info_list[i].Hal_format == value) {
            foramt = preview_format_info_list[i].mm_cam_format;
            break;
          }
        }
    }

    return foramt;
}

cam_pad_format_t QCameraHardwareInterface::getPreviewPadding() const
{
  return mPreviewFormatInfo.padding;
}

int QCameraHardwareInterface::getJpegQuality() const
{
    return mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
}

int QCameraHardwareInterface::getNumOfSnapshots(void) const
{
    char prop[PROPERTY_VALUE_MAX];
    memset(prop, 0, sizeof(prop));
    property_get("persist.camera.snapshot.number", prop, "0");
    LOGI("%s: prop enable/disable = %d", __func__, atoi(prop));
    if (atoi(prop)) {
        LOGE("%s: Reading maximum no of snapshots = %d"
             "from properties", __func__, atoi(prop));
        return atoi(prop);
    } else {
        return mParameters.getInt("num-snaps-per-shutter");
    }

}

int QCameraHardwareInterface::
getThumbSizesFromAspectRatio(uint32_t aspect_ratio,
                             int *picture_width,
                             int *picture_height)
{
    for(unsigned int i = 0; i < THUMBNAIL_SIZE_COUNT; i++ ){
        if(thumbnail_sizes[i].aspect_ratio == aspect_ratio)
        {
            *picture_width = thumbnail_sizes[i].width;
            *picture_height = thumbnail_sizes[i].height;
            return NO_ERROR;
        }
    }

    return BAD_VALUE;
}

bool QCameraHardwareInterface::isRawSnapshot()
{
  const char *format = mParameters.getPictureFormat();
    if( format!= NULL &&
       !strcmp(format, CameraParameters::PIXEL_FORMAT_RAW)){
        return true;
    }
    else{
        return false;
    }
}

status_t QCameraHardwareInterface::setPreviewSizeTable(void)
{
    status_t ret = NO_ERROR;
    mm_camera_dimension_t dim;
    struct camera_size_type* preview_size_table;
    int preview_table_size;
    int i = 0;

    /* Initialize table with default values */
    preview_size_table = default_preview_sizes;
    preview_table_size = sizeof(default_preview_sizes)/
        sizeof(default_preview_sizes[0]);

    /* Get maximum preview size supported by sensor*/
    memset(&dim, 0, sizeof(mm_camera_dimension_t));
    ret = cam_config_get_parm(mCameraId,
                              MM_CAMERA_PARM_MAX_PREVIEW_SIZE, &dim);
    if (ret != NO_ERROR) {
        LOGE("%s: Failure getting Max Preview Size supported by camera",
             __func__);
        goto end;
    }

    LOGD("%s: Max Preview Sizes Supported: %d X %d", __func__,
         dim.width, dim.height);

    for (i = 0; i < preview_table_size; i++) {
        if ((preview_size_table->width <= dim.width) &&
            (preview_size_table->height <= dim.height)) {
            LOGD("%s: Camera Preview Size Table "
                 "Max width: %d height %d table_size: %d",
                 __func__, preview_size_table->width,
                 preview_size_table->height, preview_table_size - i);
            break;
        }
        preview_size_table++;
    }

end:
    /* Save the table in global member*/
    mPreviewSizes = preview_size_table;
    mPreviewSizeCount = preview_table_size - i;

    return ret;
}

status_t QCameraHardwareInterface::setPictureSizeTable(void)
{
    status_t ret = NO_ERROR;
    mm_camera_dimension_t dim;
    struct camera_size_type* picture_size_table;
    int picture_table_size;
    int i = 0, count = 0;

    /* Initialize table with default values */
    picture_table_size = sizeof(default_picture_sizes)/
        sizeof(default_preview_sizes[0]);
    picture_size_table = default_picture_sizes;
    mPictureSizes =
        ( struct camera_size_type *)malloc(picture_table_size *
                                           sizeof(struct camera_size_type));
    if (mPictureSizes == NULL) {
        LOGE("%s: Failre allocating memory to store picture size table",__func__);
        goto end;
    }

    /* Get maximum picture size supported by sensor*/
    memset(&dim, 0, sizeof(mm_camera_dimension_t));
    ret = cam_config_get_parm(mCameraId,
                              MM_CAMERA_PARM_MAX_PICTURE_SIZE, &dim);
    if (ret != NO_ERROR) {
        LOGE("%s: Failure getting Max Picture Size supported by camera",
             __func__);
        ret = NO_MEMORY;
        free(mPictureSizes);
        mPictureSizes = NULL;
        goto end;
    }

    LOGD("%s: Max Picture Sizes Supported: %d X %d", __func__,
         dim.width, dim.height);

    for (i = 0; i < picture_table_size; i++) {
        /* We'll store those dimensions whose width AND height
           are less than or equal to maximum supported */
        if ((picture_size_table->width <= dim.width) &&
            (picture_size_table->height <= dim.height)) {
            LOGD("%s: Camera Picture Size Table "
                 "Max width: %d height %d table_size: %d",
                 __func__, picture_size_table->width,
                 picture_size_table->height, count+1);
            mPictureSizes[count].height = picture_size_table->height;
            mPictureSizes[count].width = picture_size_table->width;
            count++;
        }
        picture_size_table++;
    }
    mPictureSizeCount = count;

end:
     /* In case of error, we use default picture sizes */
     if (ret != NO_ERROR) {
        mPictureSizes = default_picture_sizes;
        mPictureSizeCount = picture_table_size;
    }
    return ret;
}

void QCameraHardwareInterface::freePictureTable(void)
{
    /* If we couldn't allocate memory to store picture table
       we use the picture table pointer to point to default
       picture table array. In that case we cannot free it.*/
    if ((mPictureSizes != default_picture_sizes) && mPictureSizes) {
        free(mPictureSizes);
    }
}

status_t QCameraHardwareInterface::setHistogram(int histogram_en)
{
    LOGE("setHistogram: E");
    if(mStatsOn == histogram_en) {
        return NO_ERROR;
    }

    mSendData = histogram_en;
    mStatsOn = histogram_en;
    mCurrentHisto = -1;
    mStatSize = sizeof(uint32_t)* HISTOGRAM_STATS_SIZE;

    if (histogram_en == QCAMERA_PARM_ENABLE) {
        /*Currently the Ashmem is multiplying the buffer size with total number
        of buffers and page aligning. This causes a crash in JNI as each buffer
        individually expected to be page aligned  */
        int page_size_minus_1 = getpagesize() - 1;
        int statSize = sizeof (camera_preview_histogram_info );
        int32_t mAlignedStatSize = ((statSize + page_size_minus_1) & (~page_size_minus_1));
#if 0
        mStatHeap =
        new AshmemPool(mAlignedStatSize, 3, statSize, "stat");
        if (!mStatHeap->initialized()) {
            LOGE("Stat Heap X failed ");
            mStatHeap.clear();
            mStatHeap = NULL;
            return UNKNOWN_ERROR;
        }
#endif
        for(int cnt = 0; cnt<3; cnt++) {
                mStatsMapped[cnt]=mGetMemory(-1, mStatSize, 1, mCallbackCookie);
                if(mStatsMapped[cnt] == NULL) {
                    LOGE("Failed to get camera memory for stats heap index: %d", cnt);
                    return(-1);
                } else {
                   LOGE("Received following info for stats mapped data:%p,handle:%p, size:%d,release:%p",
                   mStatsMapped[cnt]->data ,mStatsMapped[cnt]->handle, mStatsMapped[cnt]->size, mStatsMapped[cnt]->release);
                }
        }
    }
    LOGV("Setting histogram = %d", histogram_en);
    native_set_parms(MM_CAMERA_PARM_HISTOGRAM, sizeof(int), &histogram_en);
    if(histogram_en == QCAMERA_PARM_DISABLE)
    {
        //release memory
        for(int i=0; i<3; i++){
            if(mStatsMapped[i] != NULL) {
                mStatsMapped[i]->release(mStatsMapped[i]);
            }
        }
    }
    return NO_ERROR;
}

/* mode: lookback mode -
   0: look back based on timestamp
   1: based on frame count.
   value: number of miliseconds or frame count*/
status_t QCameraHardwareInterface::setZSLLookBack(int mode, int value)
{
    if (value < 0) {
        LOGE("%s: Undefined look back value!!!", __func__);
        return BAD_VALUE;
    }

    if (mode >= ZSL_LOOK_BACK_MODE_UNDEFINED) {
        LOGE("%s: Undefined look back mode!!!", __func__);
        return BAD_VALUE;
    }
    mZslLookBackMode = mode;
    mZslLookBackValue = value;

    return NO_ERROR;
}

void QCameraHardwareInterface::getZSLLookBack(int *mode, int *value)
{
    char prop[PROPERTY_VALUE_MAX];
    LOGV("%s: BEGIN", __func__);

    memset(prop, 0, sizeof(prop));
    property_get("persist.camera.zsl.prop.enable", prop, "0");
    /* If we set this property, we'll read zsl look back values from set
       properties. Otherwise it should be the value passed by User. */
    if (atoi(prop)) {
        LOGI("%s: Reading look-back mode from properties", __func__);
        memset(prop, 0, sizeof(prop));
        property_get("persist.camera.zsl.lb_mode", prop, "0");
        mZslLookBackMode = atoi(prop);

        memset(prop, 0, sizeof(prop));
        property_get("persist.camera.zsl.lb_value", prop, "0");
        mZslLookBackValue = atoi(prop);
    }

    if (mZslLookBackMode >= ZSL_LOOK_BACK_MODE_UNDEFINED)
    {
        LOGE("%s: Undefined look-back value. Resetting to default!", __func__);
        mZslLookBackMode = ZSL_LOOK_BACK_MODE_COUNT;
    }

    *mode = mZslLookBackMode;
    *value = mZslLookBackValue;

    LOGI("%s: ZSL Lookback mode: %d value: %d", __func__, *mode, *value);
}

void QCameraHardwareInterface::setZSLEmptyQueueFlag(bool value)
{
    LOGI("%s: Setting ZSL Empty_Queue Flag to %d", __func__, value);
    mZslEmptyQueueFlag = value;
}

void QCameraHardwareInterface::getZSLEmptyQueueFlag(bool *flag)
{
    char value[PROPERTY_VALUE_MAX];
    LOGV("%s: BEGIN", __func__);

    memset(value, 0, sizeof(value));
    property_get("persist.camera.zsl.prop.enable", value, "0");
    /* If we set this property, we'll read zsl look back values from set
       properties. Otherwise it should be the value passed by User */
    if (atoi(value)) {
        LOGI("%s: Reading empty_queue flag from properties", __func__);
        memset(value, 0, sizeof(value));
        property_get("persist.camera.zsl.empty_queue", value, "0");
        mZslEmptyQueueFlag = (bool)atoi(value);
    }

    *flag = mZslEmptyQueueFlag;

    LOGI("%s: ZSL Empty Queue Flag is set to %d", __func__, mZslEmptyQueueFlag);
}
}; /*namespace android */
