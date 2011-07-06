
/*
** Copyright 2008, Google Inc.
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
#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>
#include "QualcommCameraHardware.h"

#include <utils/Errors.h>
#include <utils/threads.h>
#include <binder/MemoryHeapPmem.h>
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

#define LIKELY(exp)   __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)
#define CAMERA_HAL_UNUSED(expr) do { (void)(expr); } while (0)

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


#include <camera.h>
#include <cam_fifo.h>
#include <liveshot.h>
#include <mm-still/jpeg/jpege.h>
#include <jpeg_encoder.h>

#define DUMP_LIVESHOT_JPEG_FILE 0

#define DEFAULT_PICTURE_WIDTH  640
#define DEFAULT_PICTURE_HEIGHT 480
#define THUMBNAIL_BUFFER_SIZE (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT * 3/2)
#define MAX_ZOOM_LEVEL 5
#define NOT_FOUND -1
// Number of video buffers held by kernal (initially 1,2 &3)
#define ACTIVE_VIDEO_BUFFERS 3
#define ACTIVE_PREVIEW_BUFFERS 3
#define ACTIVE_ZSL_BUFFERS 3
#define APP_ORIENTATION 90

#if DLOPEN_LIBMMCAMERA
#include <dlfcn.h>

void *libmmcamera;
void* (*LINK_cam_conf)(void *data);
void* (*LINK_cam_frame)(void *data);
bool  (*LINK_jpeg_encoder_init)();
void  (*LINK_jpeg_encoder_join)();
bool  (*LINK_jpeg_encoder_encode)(const cam_ctrl_dimension_t *dimen,
                                  const uint8_t *thumbnailbuf, int thumbnailfd,
                                  const uint8_t *snapshotbuf, int snapshotfd,
                                  common_crop_t *scaling_parms, exif_tags_info_t *exif_data,
                                  int exif_table_numEntries, int jpegPadding, const int32_t cbcroffset);
void (*LINK_camframe_terminate)(void);
//for 720p
// Function pointer , called by camframe when a video frame is available.
void (**LINK_camframe_video_callback)(struct msm_frame * frame);
// Function to add a frame to free Q
void (*LINK_camframe_add_frame)(cam_frame_type_t type,struct msm_frame *frame);

void (*LINK_camframe_release_all_frames)(cam_frame_type_t type);

int8_t (*LINK_jpeg_encoder_setMainImageQuality)(uint32_t quality);
int8_t (*LINK_jpeg_encoder_setThumbnailQuality)(uint32_t quality);
int8_t (*LINK_jpeg_encoder_setRotation)(uint32_t rotation);
int8_t (*LINK_jpeg_encoder_get_buffer_offset)(uint32_t width, uint32_t height,
                                               uint32_t* p_y_offset,
                                                uint32_t* p_cbcr_offset,
                                                 uint32_t* p_buf_size);
int8_t (*LINK_jpeg_encoder_setLocation)(const camera_position_type *location);
void (*LINK_jpeg_encoder_set_3D_info)(cam_3d_frame_format_t format);
const struct camera_size_type *(*LINK_default_sensor_get_snapshot_sizes)(int *len);
int (*LINK_launch_cam_conf_thread)(void);
int (*LINK_release_cam_conf_thread)(void);
mm_camera_status_t (*LINK_mm_camera_init)(mm_camera_config *, mm_camera_notify*, mm_camera_ops*,uint8_t);
mm_camera_status_t (*LINK_mm_camera_deinit)();
mm_camera_status_t (*LINK_mm_camera_destroy)();
mm_camera_status_t (*LINK_mm_camera_exec)();
mm_camera_status_t (*LINK_mm_camera_get_camera_info) (camera_info_t* p_cam_info, int* p_num_cameras);

int8_t (*LINK_zoom_crop_upscale)(uint32_t width, uint32_t height,
    uint32_t cropped_width, uint32_t cropped_height, uint8_t *img_buf);

// callbacks
void  (**LINK_mmcamera_shutter_callback)(common_crop_t *crop);
void  (**LINK_cancel_liveshot)(void);
int8_t  (*LINK_set_liveshot_params)(uint32_t a_width, uint32_t a_height, exif_tags_info_t *a_exif_data,
                         int a_exif_numEntries, uint8_t* a_out_buffer, uint32_t a_outbuffer_size);
#else
#define LINK_cam_conf cam_conf
#define LINK_cam_frame cam_frame
#define LINK_jpeg_encoder_init jpeg_encoder_init
#define LINK_jpeg_encoder_join jpeg_encoder_join
#define LINK_jpeg_encoder_encode jpeg_encoder_encode
#define LINK_camframe_terminate camframe_terminate
#define LINK_jpeg_encoder_setMainImageQuality jpeg_encoder_setMainImageQuality
#define LINK_jpeg_encoder_setThumbnailQuality jpeg_encoder_setThumbnailQuality
#define LINK_jpeg_encoder_setRotation jpeg_encoder_setRotation
#define LINK_jpeg_encoder_get_buffer_offset jpeg_encoder_get_buffer_offset
#define LINK_jpeg_encoder_setLocation jpeg_encoder_setLocation
#define LINK_jpeg_encoder_set_3D_info jpeg_encoder_set_3D_info
#define LINK_default_sensor_get_snapshot_sizes default_sensor_get_snapshot_sizes
#define LINK_launch_cam_conf_thread launch_cam_conf_thread
#define LINK_release_cam_conf_thread release_cam_conf_thread
#define LINK_zoom_crop_upscale zoom_crop_upscale
#define LINK_mm_camera_init mm_camera_config_init
#define LINK_mm_camera_deinit mm_camera_config_deinit
#define LINK_mm_camera_destroy mm_camera_config_destroy
#define LINK_mm_camera_exec mm_camera_exec
#define LINK_camframe_add_frame camframe_add_frame
#define LINK_camframe_release_all_frames camframe_release_all_frames
#define LINK_mm_camera_get_camera_info mm_camera_get_camera_info

extern void (*mmcamera_camframe_callback)(struct msm_frame *frame);
extern void (*mmcamera_camstats_callback)(camstats_type stype, camera_preview_histogram_info* histinfo);
extern void (*mmcamera_jpegfragment_callback)(uint8_t *buff_ptr,
                                      uint32_t buff_size);
extern void (*mmcamera_jpeg_callback)(jpeg_event_t status);
extern void (*mmcamera_shutter_callback)(common_crop_t *crop);
extern void (*mmcamera_liveshot_callback)(liveshot_status status, uint32_t jpeg_size);
#define LINK_set_liveshot_params set_liveshot_params
#endif

} // extern "C"

#ifndef HAVE_CAMERA_SIZE_TYPE
struct camera_size_type {
    int width;
    int height;
};
#endif

typedef struct crop_info_struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} zoom_crop_info;

union zoomimage
{
    char d[sizeof(struct mdp_blit_req_list) + sizeof(struct mdp_blit_req) * 1];
    struct mdp_blit_req_list list;
} zoomImage;

//Default to VGA
#define DEFAULT_PREVIEW_WIDTH 640
#define DEFAULT_PREVIEW_HEIGHT 480

//Default FPS
#define MINIMUM_FPS 5
#define MAXIMUM_FPS 31
#define DEFAULT_FPS MAXIMUM_FPS

/*
 * Modifying preview size requires modification
 * in bitmasks for boardproperties
 */
static uint32_t  PREVIEW_SIZE_COUNT;
static uint32_t  HFR_SIZE_COUNT;

board_property boardProperties[] = {
        {TARGET_MSM7625, 0x00000fff, false, false, false},
        {TARGET_MSM7625A, 0x00000fff, false, false, false},
        {TARGET_MSM7627, 0x000006ff, false, false, false},
        {TARGET_MSM7627A, 0x000006ff, false, false, false},
        {TARGET_MSM7630, 0x00000fff, true, true, false},
        {TARGET_MSM8660, 0x00001fff, true, true, false},
        {TARGET_QSD8250, 0x00000fff, false, false, false}
};

//static const camera_size_type* picture_sizes;
//static int PICTURE_SIZE_COUNT;
/*       TODO
 * Ideally this should be a populated by lower layers.
 * But currently this is no API to do that at lower layer.
 * Hence populating with default sizes for now. This needs
 * to be changed once the API is supported.
 */
//sorted on column basis
static struct camera_size_type zsl_picture_sizes[] = {
  { 1024, 768}, // 1MP XGA
  { 800, 600}, //SVGA
  { 800, 480}, // WVGA
  { 640, 480}, // VGA
  { 352, 288}, //CIF
  { 320, 240}, // QVGA
  { 176, 144} // QCIF
};
static camera_size_type* picture_sizes;
static camera_size_type* preview_sizes;
static camera_size_type* hfr_sizes;
static unsigned int PICTURE_SIZE_COUNT;
static const camera_size_type * picture_sizes_ptr;
static int supportedPictureSizesCount;
static liveshotState liveshot_state = LIVESHOT_DONE;

#ifdef Q12
#undef Q12
#endif

#define Q12 4096

static const target_map targetList [] = {
    { "msm7625", TARGET_MSM7625 },
    { "msm7625a", TARGET_MSM7625A },
    { "msm7627", TARGET_MSM7627 },
    { "msm7627a", TARGET_MSM7627A },
    { "qsd8250", TARGET_QSD8250 },
    { "msm7630", TARGET_MSM7630 },
    { "msm8660", TARGET_MSM8660 }

};
static targetType mCurrentTarget = TARGET_MAX;

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
#define THUMBNAIL_SIZE_COUNT (sizeof(thumbnail_sizes)/sizeof(thumbnail_size_type))
#define DEFAULT_THUMBNAIL_SETTING 4
#define THUMBNAIL_WIDTH_STR "512"
#define THUMBNAIL_HEIGHT_STR "384"
#define THUMBNAIL_SMALL_HEIGHT 144
static camera_size_type jpeg_thumbnail_sizes[]  = {
    { 512, 288 },
    { 480, 288 },
    { 432, 288 },
    { 512, 384 },
    { 352, 288 },
    {0,0}
};
//supported preview fps ranges should be added to this array in the form (minFps,maxFps)
static  android::FPSRange FpsRangesSupported[] = {{MINIMUM_FPS*1000,MAXIMUM_FPS*1000}};

#define FPS_RANGES_SUPPORTED_COUNT (sizeof(FpsRangesSupported)/sizeof(FpsRangesSupported[0]))

#define JPEG_THUMBNAIL_SIZE_COUNT (sizeof(jpeg_thumbnail_sizes)/sizeof(camera_size_type))
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

// round to the next power of two
static inline unsigned clp2(unsigned x)
{
    x = x - 1;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >>16);
    return x + 1;
}

static int exif_table_numEntries = 0;
#define MAX_EXIF_TABLE_ENTRIES 11
exif_tags_info_t exif_data[MAX_EXIF_TABLE_ENTRIES];
static zoom_crop_info zoomCropInfo;
static void *mLastQueuedFrame = NULL;
#define RECORD_BUFFERS 9
#define RECORD_BUFFERS_8x50 8
static int kRecordBufferCount;
/* controls whether VPE is avialable for the target
 * under consideration.
 * 1: VPE support is available
 * 0: VPE support is not available (default)
 */
static bool mVpeEnabled;
static cam_frame_start_parms camframeParams;

static int HAL_numOfCameras;
static camera_info_t HAL_cameraInfo[MSM_MAX_CAMERA_SENSORS];
static int HAL_currentCameraId;
static int HAL_currentCameraMode;
static mm_camera_config mCfgControl;

static int HAL_currentSnapshotMode;
#define CAMERA_SNAPSHOT_NONZSL 0x04
#define CAMERA_SNAPSHOT_ZSL 0x08

namespace android {

static const int PICTURE_FORMAT_JPEG = 1;
static const int PICTURE_FORMAT_RAW = 2;

// from aeecamera.h
static const str_map whitebalance[] = {
    { CameraParameters::WHITE_BALANCE_AUTO,            CAMERA_WB_AUTO },
    { CameraParameters::WHITE_BALANCE_INCANDESCENT,    CAMERA_WB_INCANDESCENT },
    { CameraParameters::WHITE_BALANCE_FLUORESCENT,     CAMERA_WB_FLUORESCENT },
    { CameraParameters::WHITE_BALANCE_DAYLIGHT,        CAMERA_WB_DAYLIGHT },
    { CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT, CAMERA_WB_CLOUDY_DAYLIGHT }
};

// from camera_effect_t. This list must match aeecamera.h
static const str_map effects[] = {
    { CameraParameters::EFFECT_NONE,       CAMERA_EFFECT_OFF },
    { CameraParameters::EFFECT_MONO,       CAMERA_EFFECT_MONO },
    { CameraParameters::EFFECT_NEGATIVE,   CAMERA_EFFECT_NEGATIVE },
    { CameraParameters::EFFECT_SOLARIZE,   CAMERA_EFFECT_SOLARIZE },
    { CameraParameters::EFFECT_SEPIA,      CAMERA_EFFECT_SEPIA },
    { CameraParameters::EFFECT_POSTERIZE,  CAMERA_EFFECT_POSTERIZE },
    { CameraParameters::EFFECT_WHITEBOARD, CAMERA_EFFECT_WHITEBOARD },
    { CameraParameters::EFFECT_BLACKBOARD, CAMERA_EFFECT_BLACKBOARD },
    { CameraParameters::EFFECT_AQUA,       CAMERA_EFFECT_AQUA }
};

// from qcamera/common/camera.h
static const str_map autoexposure[] = {
    { CameraParameters::AUTO_EXPOSURE_FRAME_AVG,  CAMERA_AEC_FRAME_AVERAGE },
    { CameraParameters::AUTO_EXPOSURE_CENTER_WEIGHTED, CAMERA_AEC_CENTER_WEIGHTED },
    { CameraParameters::AUTO_EXPOSURE_SPOT_METERING, CAMERA_AEC_SPOT_METERING }
};

// from qcamera/common/camera.h
static const str_map antibanding[] = {
    { CameraParameters::ANTIBANDING_OFF,  CAMERA_ANTIBANDING_OFF },
    { CameraParameters::ANTIBANDING_50HZ, CAMERA_ANTIBANDING_50HZ },
    { CameraParameters::ANTIBANDING_60HZ, CAMERA_ANTIBANDING_60HZ },
    { CameraParameters::ANTIBANDING_AUTO, CAMERA_ANTIBANDING_AUTO }
};

/* Mapping from MCC to antibanding type */
struct country_map {
    uint32_t country_code;
    camera_antibanding_type type;
};

#if 0 //not using this function. keeping this as this came from Google.
static struct country_map country_numeric[] = {
    { 202, CAMERA_ANTIBANDING_50HZ }, // Greece
    { 204, CAMERA_ANTIBANDING_50HZ }, // Netherlands
    { 206, CAMERA_ANTIBANDING_50HZ }, // Belgium
    { 208, CAMERA_ANTIBANDING_50HZ }, // France
    { 212, CAMERA_ANTIBANDING_50HZ }, // Monaco
    { 213, CAMERA_ANTIBANDING_50HZ }, // Andorra
    { 214, CAMERA_ANTIBANDING_50HZ }, // Spain
    { 216, CAMERA_ANTIBANDING_50HZ }, // Hungary
    { 219, CAMERA_ANTIBANDING_50HZ }, // Croatia
    { 220, CAMERA_ANTIBANDING_50HZ }, // Serbia
    { 222, CAMERA_ANTIBANDING_50HZ }, // Italy
    { 226, CAMERA_ANTIBANDING_50HZ }, // Romania
    { 228, CAMERA_ANTIBANDING_50HZ }, // Switzerland
    { 230, CAMERA_ANTIBANDING_50HZ }, // Czech Republic
    { 231, CAMERA_ANTIBANDING_50HZ }, // Slovakia
    { 232, CAMERA_ANTIBANDING_50HZ }, // Austria
    { 234, CAMERA_ANTIBANDING_50HZ }, // United Kingdom
    { 235, CAMERA_ANTIBANDING_50HZ }, // United Kingdom
    { 238, CAMERA_ANTIBANDING_50HZ }, // Denmark
    { 240, CAMERA_ANTIBANDING_50HZ }, // Sweden
    { 242, CAMERA_ANTIBANDING_50HZ }, // Norway
    { 244, CAMERA_ANTIBANDING_50HZ }, // Finland
    { 246, CAMERA_ANTIBANDING_50HZ }, // Lithuania
    { 247, CAMERA_ANTIBANDING_50HZ }, // Latvia
    { 248, CAMERA_ANTIBANDING_50HZ }, // Estonia
    { 250, CAMERA_ANTIBANDING_50HZ }, // Russian Federation
    { 255, CAMERA_ANTIBANDING_50HZ }, // Ukraine
    { 257, CAMERA_ANTIBANDING_50HZ }, // Belarus
    { 259, CAMERA_ANTIBANDING_50HZ }, // Moldova
    { 260, CAMERA_ANTIBANDING_50HZ }, // Poland
    { 262, CAMERA_ANTIBANDING_50HZ }, // Germany
    { 266, CAMERA_ANTIBANDING_50HZ }, // Gibraltar
    { 268, CAMERA_ANTIBANDING_50HZ }, // Portugal
    { 270, CAMERA_ANTIBANDING_50HZ }, // Luxembourg
    { 272, CAMERA_ANTIBANDING_50HZ }, // Ireland
    { 274, CAMERA_ANTIBANDING_50HZ }, // Iceland
    { 276, CAMERA_ANTIBANDING_50HZ }, // Albania
    { 278, CAMERA_ANTIBANDING_50HZ }, // Malta
    { 280, CAMERA_ANTIBANDING_50HZ }, // Cyprus
    { 282, CAMERA_ANTIBANDING_50HZ }, // Georgia
    { 283, CAMERA_ANTIBANDING_50HZ }, // Armenia
    { 284, CAMERA_ANTIBANDING_50HZ }, // Bulgaria
    { 286, CAMERA_ANTIBANDING_50HZ }, // Turkey
    { 288, CAMERA_ANTIBANDING_50HZ }, // Faroe Islands
    { 290, CAMERA_ANTIBANDING_50HZ }, // Greenland
    { 293, CAMERA_ANTIBANDING_50HZ }, // Slovenia
    { 294, CAMERA_ANTIBANDING_50HZ }, // Macedonia
    { 295, CAMERA_ANTIBANDING_50HZ }, // Liechtenstein
    { 297, CAMERA_ANTIBANDING_50HZ }, // Montenegro
    { 302, CAMERA_ANTIBANDING_60HZ }, // Canada
    { 310, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 311, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 312, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 313, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 314, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 315, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 316, CAMERA_ANTIBANDING_60HZ }, // United States of America
    { 330, CAMERA_ANTIBANDING_60HZ }, // Puerto Rico
    { 334, CAMERA_ANTIBANDING_60HZ }, // Mexico
    { 338, CAMERA_ANTIBANDING_50HZ }, // Jamaica
    { 340, CAMERA_ANTIBANDING_50HZ }, // Martinique
    { 342, CAMERA_ANTIBANDING_50HZ }, // Barbados
    { 346, CAMERA_ANTIBANDING_60HZ }, // Cayman Islands
    { 350, CAMERA_ANTIBANDING_60HZ }, // Bermuda
    { 352, CAMERA_ANTIBANDING_50HZ }, // Grenada
    { 354, CAMERA_ANTIBANDING_60HZ }, // Montserrat
    { 362, CAMERA_ANTIBANDING_50HZ }, // Netherlands Antilles
    { 363, CAMERA_ANTIBANDING_60HZ }, // Aruba
    { 364, CAMERA_ANTIBANDING_60HZ }, // Bahamas
    { 365, CAMERA_ANTIBANDING_60HZ }, // Anguilla
    { 366, CAMERA_ANTIBANDING_50HZ }, // Dominica
    { 368, CAMERA_ANTIBANDING_60HZ }, // Cuba
    { 370, CAMERA_ANTIBANDING_60HZ }, // Dominican Republic
    { 372, CAMERA_ANTIBANDING_60HZ }, // Haiti
    { 401, CAMERA_ANTIBANDING_50HZ }, // Kazakhstan
    { 402, CAMERA_ANTIBANDING_50HZ }, // Bhutan
    { 404, CAMERA_ANTIBANDING_50HZ }, // India
    { 405, CAMERA_ANTIBANDING_50HZ }, // India
    { 410, CAMERA_ANTIBANDING_50HZ }, // Pakistan
    { 413, CAMERA_ANTIBANDING_50HZ }, // Sri Lanka
    { 414, CAMERA_ANTIBANDING_50HZ }, // Myanmar
    { 415, CAMERA_ANTIBANDING_50HZ }, // Lebanon
    { 416, CAMERA_ANTIBANDING_50HZ }, // Jordan
    { 417, CAMERA_ANTIBANDING_50HZ }, // Syria
    { 418, CAMERA_ANTIBANDING_50HZ }, // Iraq
    { 419, CAMERA_ANTIBANDING_50HZ }, // Kuwait
    { 420, CAMERA_ANTIBANDING_60HZ }, // Saudi Arabia
    { 421, CAMERA_ANTIBANDING_50HZ }, // Yemen
    { 422, CAMERA_ANTIBANDING_50HZ }, // Oman
    { 424, CAMERA_ANTIBANDING_50HZ }, // United Arab Emirates
    { 425, CAMERA_ANTIBANDING_50HZ }, // Israel
    { 426, CAMERA_ANTIBANDING_50HZ }, // Bahrain
    { 427, CAMERA_ANTIBANDING_50HZ }, // Qatar
    { 428, CAMERA_ANTIBANDING_50HZ }, // Mongolia
    { 429, CAMERA_ANTIBANDING_50HZ }, // Nepal
    { 430, CAMERA_ANTIBANDING_50HZ }, // United Arab Emirates
    { 431, CAMERA_ANTIBANDING_50HZ }, // United Arab Emirates
    { 432, CAMERA_ANTIBANDING_50HZ }, // Iran
    { 434, CAMERA_ANTIBANDING_50HZ }, // Uzbekistan
    { 436, CAMERA_ANTIBANDING_50HZ }, // Tajikistan
    { 437, CAMERA_ANTIBANDING_50HZ }, // Kyrgyz Rep
    { 438, CAMERA_ANTIBANDING_50HZ }, // Turkmenistan
    { 440, CAMERA_ANTIBANDING_60HZ }, // Japan
    { 441, CAMERA_ANTIBANDING_60HZ }, // Japan
    { 452, CAMERA_ANTIBANDING_50HZ }, // Vietnam
    { 454, CAMERA_ANTIBANDING_50HZ }, // Hong Kong
    { 455, CAMERA_ANTIBANDING_50HZ }, // Macao
    { 456, CAMERA_ANTIBANDING_50HZ }, // Cambodia
    { 457, CAMERA_ANTIBANDING_50HZ }, // Laos
    { 460, CAMERA_ANTIBANDING_50HZ }, // China
    { 466, CAMERA_ANTIBANDING_60HZ }, // Taiwan
    { 470, CAMERA_ANTIBANDING_50HZ }, // Bangladesh
    { 472, CAMERA_ANTIBANDING_50HZ }, // Maldives
    { 502, CAMERA_ANTIBANDING_50HZ }, // Malaysia
    { 505, CAMERA_ANTIBANDING_50HZ }, // Australia
    { 510, CAMERA_ANTIBANDING_50HZ }, // Indonesia
    { 514, CAMERA_ANTIBANDING_50HZ }, // East Timor
    { 515, CAMERA_ANTIBANDING_60HZ }, // Philippines
    { 520, CAMERA_ANTIBANDING_50HZ }, // Thailand
    { 525, CAMERA_ANTIBANDING_50HZ }, // Singapore
    { 530, CAMERA_ANTIBANDING_50HZ }, // New Zealand
    { 535, CAMERA_ANTIBANDING_60HZ }, // Guam
    { 536, CAMERA_ANTIBANDING_50HZ }, // Nauru
    { 537, CAMERA_ANTIBANDING_50HZ }, // Papua New Guinea
    { 539, CAMERA_ANTIBANDING_50HZ }, // Tonga
    { 541, CAMERA_ANTIBANDING_50HZ }, // Vanuatu
    { 542, CAMERA_ANTIBANDING_50HZ }, // Fiji
    { 544, CAMERA_ANTIBANDING_60HZ }, // American Samoa
    { 545, CAMERA_ANTIBANDING_50HZ }, // Kiribati
    { 546, CAMERA_ANTIBANDING_50HZ }, // New Caledonia
    { 548, CAMERA_ANTIBANDING_50HZ }, // Cook Islands
    { 602, CAMERA_ANTIBANDING_50HZ }, // Egypt
    { 603, CAMERA_ANTIBANDING_50HZ }, // Algeria
    { 604, CAMERA_ANTIBANDING_50HZ }, // Morocco
    { 605, CAMERA_ANTIBANDING_50HZ }, // Tunisia
    { 606, CAMERA_ANTIBANDING_50HZ }, // Libya
    { 607, CAMERA_ANTIBANDING_50HZ }, // Gambia
    { 608, CAMERA_ANTIBANDING_50HZ }, // Senegal
    { 609, CAMERA_ANTIBANDING_50HZ }, // Mauritania
    { 610, CAMERA_ANTIBANDING_50HZ }, // Mali
    { 611, CAMERA_ANTIBANDING_50HZ }, // Guinea
    { 613, CAMERA_ANTIBANDING_50HZ }, // Burkina Faso
    { 614, CAMERA_ANTIBANDING_50HZ }, // Niger
    { 616, CAMERA_ANTIBANDING_50HZ }, // Benin
    { 617, CAMERA_ANTIBANDING_50HZ }, // Mauritius
    { 618, CAMERA_ANTIBANDING_50HZ }, // Liberia
    { 619, CAMERA_ANTIBANDING_50HZ }, // Sierra Leone
    { 620, CAMERA_ANTIBANDING_50HZ }, // Ghana
    { 621, CAMERA_ANTIBANDING_50HZ }, // Nigeria
    { 622, CAMERA_ANTIBANDING_50HZ }, // Chad
    { 623, CAMERA_ANTIBANDING_50HZ }, // Central African Republic
    { 624, CAMERA_ANTIBANDING_50HZ }, // Cameroon
    { 625, CAMERA_ANTIBANDING_50HZ }, // Cape Verde
    { 627, CAMERA_ANTIBANDING_50HZ }, // Equatorial Guinea
    { 631, CAMERA_ANTIBANDING_50HZ }, // Angola
    { 633, CAMERA_ANTIBANDING_50HZ }, // Seychelles
    { 634, CAMERA_ANTIBANDING_50HZ }, // Sudan
    { 636, CAMERA_ANTIBANDING_50HZ }, // Ethiopia
    { 637, CAMERA_ANTIBANDING_50HZ }, // Somalia
    { 638, CAMERA_ANTIBANDING_50HZ }, // Djibouti
    { 639, CAMERA_ANTIBANDING_50HZ }, // Kenya
    { 640, CAMERA_ANTIBANDING_50HZ }, // Tanzania
    { 641, CAMERA_ANTIBANDING_50HZ }, // Uganda
    { 642, CAMERA_ANTIBANDING_50HZ }, // Burundi
    { 643, CAMERA_ANTIBANDING_50HZ }, // Mozambique
    { 645, CAMERA_ANTIBANDING_50HZ }, // Zambia
    { 646, CAMERA_ANTIBANDING_50HZ }, // Madagascar
    { 647, CAMERA_ANTIBANDING_50HZ }, // France
    { 648, CAMERA_ANTIBANDING_50HZ }, // Zimbabwe
    { 649, CAMERA_ANTIBANDING_50HZ }, // Namibia
    { 650, CAMERA_ANTIBANDING_50HZ }, // Malawi
    { 651, CAMERA_ANTIBANDING_50HZ }, // Lesotho
    { 652, CAMERA_ANTIBANDING_50HZ }, // Botswana
    { 653, CAMERA_ANTIBANDING_50HZ }, // Swaziland
    { 654, CAMERA_ANTIBANDING_50HZ }, // Comoros
    { 655, CAMERA_ANTIBANDING_50HZ }, // South Africa
    { 657, CAMERA_ANTIBANDING_50HZ }, // Eritrea
    { 702, CAMERA_ANTIBANDING_60HZ }, // Belize
    { 704, CAMERA_ANTIBANDING_60HZ }, // Guatemala
    { 706, CAMERA_ANTIBANDING_60HZ }, // El Salvador
    { 708, CAMERA_ANTIBANDING_60HZ }, // Honduras
    { 710, CAMERA_ANTIBANDING_60HZ }, // Nicaragua
    { 712, CAMERA_ANTIBANDING_60HZ }, // Costa Rica
    { 714, CAMERA_ANTIBANDING_60HZ }, // Panama
    { 722, CAMERA_ANTIBANDING_50HZ }, // Argentina
    { 724, CAMERA_ANTIBANDING_60HZ }, // Brazil
    { 730, CAMERA_ANTIBANDING_50HZ }, // Chile
    { 732, CAMERA_ANTIBANDING_60HZ }, // Colombia
    { 734, CAMERA_ANTIBANDING_60HZ }, // Venezuela
    { 736, CAMERA_ANTIBANDING_50HZ }, // Bolivia
    { 738, CAMERA_ANTIBANDING_60HZ }, // Guyana
    { 740, CAMERA_ANTIBANDING_60HZ }, // Ecuador
    { 742, CAMERA_ANTIBANDING_50HZ }, // French Guiana
    { 744, CAMERA_ANTIBANDING_50HZ }, // Paraguay
    { 746, CAMERA_ANTIBANDING_60HZ }, // Suriname
    { 748, CAMERA_ANTIBANDING_50HZ }, // Uruguay
    { 750, CAMERA_ANTIBANDING_50HZ }, // Falkland Islands
};
#define country_number (sizeof(country_numeric) / sizeof(country_map))
/* Look up pre-sorted antibanding_type table by current MCC. */
static camera_antibanding_type camera_get_location(void) {
    char value[PROP_VALUE_MAX];
    char country_value[PROP_VALUE_MAX];
    uint32_t country_code;
    memset(value, 0x00, sizeof(value));
    memset(country_value, 0x00, sizeof(country_value));
    if (!__system_property_get("gsm.operator.numeric", value)) {
        return CAMERA_ANTIBANDING_60HZ;
    }
    memcpy(country_value, value, 3);
    country_code = atoi(country_value);
    LOGD("value:%s, country value:%s, country code:%d\n",
            value, country_value, country_code);
    int left = 0;
    int right = country_number - 1;
    while (left <= right) {
        int index = (left + right) >> 1;
        if (country_numeric[index].country_code == country_code)
            return country_numeric[index].type;
        else if (country_numeric[index].country_code > country_code)
            right = index - 1;
        else
            left = index + 1;
    }
    return CAMERA_ANTIBANDING_60HZ;
}
#endif

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

// from camera.h, led_mode_t
static const str_map flash[] = {
    { CameraParameters::FLASH_MODE_OFF,  LED_MODE_OFF },
    { CameraParameters::FLASH_MODE_AUTO, LED_MODE_AUTO },
    { CameraParameters::FLASH_MODE_ON, LED_MODE_ON },
    { CameraParameters::FLASH_MODE_TORCH, LED_MODE_TORCH}
};

// from mm-camera/common/camera.h.
static const str_map iso[] = {
    { CameraParameters::ISO_AUTO,  CAMERA_ISO_AUTO},
    { CameraParameters::ISO_HJR,   CAMERA_ISO_DEBLUR},
    { CameraParameters::ISO_100,   CAMERA_ISO_100},
    { CameraParameters::ISO_200,   CAMERA_ISO_200},
    { CameraParameters::ISO_400,   CAMERA_ISO_400},
    { CameraParameters::ISO_800,   CAMERA_ISO_800 },
    { CameraParameters::ISO_1600,  CAMERA_ISO_1600 }
};


#define DONT_CARE 0
static const str_map focus_modes[] = {
    { CameraParameters::FOCUS_MODE_AUTO,     AF_MODE_AUTO},
    { CameraParameters::FOCUS_MODE_INFINITY, DONT_CARE },
    { CameraParameters::FOCUS_MODE_NORMAL,   AF_MODE_NORMAL },
    { CameraParameters::FOCUS_MODE_MACRO,    AF_MODE_MACRO },
    { CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO, DONT_CARE }
};

static const str_map lensshade[] = {
    { CameraParameters::LENSSHADE_ENABLE, TRUE },
    { CameraParameters::LENSSHADE_DISABLE, FALSE }
};

static const str_map hfr[] = {
    { CameraParameters::VIDEO_HFR_OFF, CAMERA_HFR_MODE_OFF },
    { CameraParameters::VIDEO_HFR_2X, CAMERA_HFR_MODE_60FPS },
    { CameraParameters::VIDEO_HFR_3X, CAMERA_HFR_MODE_90FPS },
    { CameraParameters::VIDEO_HFR_4X, CAMERA_HFR_MODE_120FPS },
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

static const str_map selectable_zone_af[] = {
    { CameraParameters::SELECTABLE_ZONE_AF_AUTO,  AUTO },
    { CameraParameters::SELECTABLE_ZONE_AF_SPOT_METERING, SPOT },
    { CameraParameters::SELECTABLE_ZONE_AF_CENTER_WEIGHTED, CENTER_WEIGHTED },
    { CameraParameters::SELECTABLE_ZONE_AF_FRAME_AVERAGE, AVERAGE }
};

static const str_map facedetection[] = {
    { CameraParameters::FACE_DETECTION_OFF, FALSE },
    { CameraParameters::FACE_DETECTION_ON, TRUE }
};

#define DONT_CARE_COORDINATE -1
static const str_map touchafaec[] = {
    { CameraParameters::TOUCH_AF_AEC_OFF, FALSE },
    { CameraParameters::TOUCH_AF_AEC_ON, TRUE }
};

static const str_map redeye_reduction[] = {
    { CameraParameters::REDEYE_REDUCTION_ENABLE, TRUE },
    { CameraParameters::REDEYE_REDUCTION_DISABLE, FALSE }
};


/*
 * Values based on aec.c
 */

#define CAMERA_HISTOGRAM_ENABLE 1
#define CAMERA_HISTOGRAM_DISABLE 0
#define HISTOGRAM_STATS_SIZE 257

/*
 * Values based on aec.c
 */
#define EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR 12
#define EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR -12
#define EXPOSURE_COMPENSATION_DEFAULT_NUMERATOR 0
#define EXPOSURE_COMPENSATION_DENOMINATOR 6
#define EXPOSURE_COMPENSATION_STEP ((float (1))/EXPOSURE_COMPENSATION_DENOMINATOR)

static const str_map picture_formats[] = {
        {CameraParameters::PIXEL_FORMAT_JPEG, PICTURE_FORMAT_JPEG},
        {CameraParameters::PIXEL_FORMAT_RAW, PICTURE_FORMAT_RAW}
};

static const str_map frame_rate_modes[] = {
        {CameraParameters::KEY_PREVIEW_FRAME_RATE_AUTO_MODE, FPS_MODE_AUTO},
        {CameraParameters::KEY_PREVIEW_FRAME_RATE_FIXED_MODE, FPS_MODE_FIXED}
};

static int mPreviewFormat;
static const str_map preview_formats[] = {
        {CameraParameters::PIXEL_FORMAT_YUV420SP,   CAMERA_YUV_420_NV21},
        {CameraParameters::PIXEL_FORMAT_YUV420SP_ADRENO, CAMERA_YUV_420_NV21_ADRENO}
};

static bool parameter_string_initialized = false;
static String8 preview_size_values;
static String8 hfr_size_values;
static String8 picture_size_values;
static String8 fps_ranges_supported_values;
static String8 jpeg_thumbnail_size_values;
static String8 antibanding_values;
static String8 effect_values;
static String8 autoexposure_values;
static String8 whitebalance_values;
static String8 flash_values;
static String8 focus_mode_values;
static String8 iso_values;
static String8 lensshade_values;
static String8 mce_values;
static String8 histogram_values;
static String8 skinToneEnhancement_values;
static String8 touchafaec_values;
static String8 picture_format_values;
static String8 scenemode_values;
static String8 denoise_values;
static String8 zoom_ratio_values;
static String8 preview_frame_rate_values;
static String8 frame_rate_mode_values;
static String8 scenedetect_values;
static String8 preview_format_values;
static String8 selectable_zone_af_values;
static String8 facedetection_values;
static String8 hfr_values;
static String8 redeye_reduction_values;

mm_camera_notify mCamNotify;
mm_camera_ops mCamOps;
static mm_camera_buffer_t mEncodeOutputBuffer[MAX_SNAPSHOT_BUFFERS];
static encode_params_t mImageEncodeParms;
static capture_params_t mImageCaptureParms;
static raw_capture_params_t mRawCaptureParms;
static zsl_capture_params_t mZslCaptureParms;
static zsl_params_t mZslParms;
static String8 create_sizes_str(const camera_size_type *sizes, int len) {
    String8 str;
    char buffer[32];

    if (len > 0) {
        sprintf(buffer, "%dx%d", sizes[0].width, sizes[0].height);
        str.append(buffer);
    }
    for (int i = 1; i < len; i++) {
        sprintf(buffer, ",%dx%d", sizes[i].width, sizes[i].height);
        str.append(buffer);
    }
    return str;
}

static String8 create_fps_str(const android:: FPSRange* fps, int len) {
    String8 str;
    char buffer[32];

    if (len > 0) {
        sprintf(buffer, "(%d,%d)", fps[0].minFPS, fps[0].maxFPS);
        str.append(buffer);
    }
    for (int i = 1; i < len; i++) {
        sprintf(buffer, ",(%d,%d)", fps[i].minFPS, fps[i].maxFPS);
        str.append(buffer);
    }
    return str;
}

static String8 create_values_str(const str_map *values, int len) {
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

extern "C" {
//------------------------------------------------------------------------
//   : 720p busyQ funcitons
//   --------------------------------------------------------------------
static struct fifo_queue g_busy_frame_queue =
    {0, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (char *)"video_busy_q"};
};
/*===========================================================================
 * FUNCTION      cam_frame_wait_video
 *
 * DESCRIPTION    this function waits a video in the busy queue
 * ===========================================================================*/

static void cam_frame_wait_video (void)
{
    LOGV("cam_frame_wait_video E ");
    if ((g_busy_frame_queue.num_of_frames) <=0){
        pthread_cond_wait(&(g_busy_frame_queue.wait), &(g_busy_frame_queue.mut));
    }
    LOGV("cam_frame_wait_video X");
    return;
}

/*===========================================================================
 * FUNCTION      cam_frame_flush_video
 *
 * DESCRIPTION    this function deletes all the buffers in  busy queue
 * ===========================================================================*/
void cam_frame_flush_video (void)
{
    LOGV("cam_frame_flush_video: in n = %d\n", g_busy_frame_queue.num_of_frames);
    pthread_mutex_lock(&(g_busy_frame_queue.mut));

    while (g_busy_frame_queue.front)
    {
       //dequeue from the busy queue
       struct fifo_node *node  = dequeue (&g_busy_frame_queue);
       if(node)
           free(node);

       LOGV("cam_frame_flush_video: node \n");
    }
    pthread_mutex_unlock(&(g_busy_frame_queue.mut));
    LOGV("cam_frame_flush_video: out n = %d\n", g_busy_frame_queue.num_of_frames);
    return ;
}
/*===========================================================================
 * FUNCTION      cam_frame_get_video
 *
 * DESCRIPTION    this function returns a video frame from the head
 * ===========================================================================*/
static struct msm_frame * cam_frame_get_video()
{
    struct msm_frame *p = NULL;
    LOGV("cam_frame_get_video... in\n");
    LOGV("cam_frame_get_video... got lock\n");
    if (g_busy_frame_queue.front)
    {
        //dequeue
       struct fifo_node *node  = dequeue (&g_busy_frame_queue);
       if (node)
       {
           p = (struct msm_frame *)node->f;
           free (node);
       }
       LOGV("cam_frame_get_video... out = %x\n", p->buffer);
    }
    return p;
}

/*===========================================================================
 * FUNCTION      cam_frame_post_video
 *
 * DESCRIPTION    this function add a busy video frame to the busy queue tails
 * ===========================================================================*/
static void cam_frame_post_video (struct msm_frame *p)
{
    if (!p)
    {
        LOGE("post video , buffer is null");
        return;
    }
    LOGV("cam_frame_post_video... in = %x\n", (unsigned int)(p->buffer));
    pthread_mutex_lock(&(g_busy_frame_queue.mut));
    LOGV("post_video got lock. q count before enQ %d", g_busy_frame_queue.num_of_frames);
    //enqueue to busy queue
    struct fifo_node *node = (struct fifo_node *)malloc (sizeof (struct fifo_node));
    if (node)
    {
        LOGV(" post video , enqueing in busy queue");
        node->f = p;
        node->next = NULL;
        enqueue (&g_busy_frame_queue, node);
        LOGV("post_video got lock. q count after enQ %d", g_busy_frame_queue.num_of_frames);
    }
    else
    {
        LOGE("cam_frame_post_video error... out of memory\n");
    }

    pthread_mutex_unlock(&(g_busy_frame_queue.mut));
    pthread_cond_signal(&(g_busy_frame_queue.wait));

    LOGV("cam_frame_post_video... out = %x\n", p->buffer);

    return;
}

QualcommCameraHardware::FrameQueue::FrameQueue(){
    mInitialized = false;
}

QualcommCameraHardware::FrameQueue::~FrameQueue(){
    flush();
}

void QualcommCameraHardware::FrameQueue::init(){
    Mutex::Autolock l(&mQueueLock);
    mInitialized = true;
    mQueueWait.signal();
}

void QualcommCameraHardware::FrameQueue::deinit(){
    Mutex::Autolock l(&mQueueLock);
    mInitialized = false;
    mQueueWait.signal();
}

bool QualcommCameraHardware::FrameQueue::isInitialized(){
   Mutex::Autolock l(&mQueueLock);
   return mInitialized;
}

bool QualcommCameraHardware::FrameQueue::add(
                struct msm_frame * element){
    Mutex::Autolock l(&mQueueLock);
    if(mInitialized == false)
        return false;

    mContainer.add(element);
    mQueueWait.signal();
    return true;
}

struct msm_frame * QualcommCameraHardware::FrameQueue::get(){

    struct msm_frame *frame;
    mQueueLock.lock();
    while(mInitialized && mContainer.isEmpty()){
        mQueueWait.wait(mQueueLock);
    }

    if(!mInitialized){
        mQueueLock.unlock();
        return NULL;
    }

    frame = mContainer.itemAt(0);
    mContainer.removeAt(0);
    mQueueLock.unlock();
    return frame;
}

void QualcommCameraHardware::FrameQueue::flush(){
    Mutex::Autolock l(&mQueueLock);
    mContainer.clear();

}


void QualcommCameraHardware::storeTargetType(void) {
    char mDeviceName[PROPERTY_VALUE_MAX];
    property_get("ro.product.device",mDeviceName," ");
    mCurrentTarget = TARGET_MAX;
    for( int i = 0; i < TARGET_MAX ; i++) {
        if( !strncmp(mDeviceName, targetList[i].targetStr, 7)) {
            mCurrentTarget = targetList[i].targetEnum;
            if(mCurrentTarget == TARGET_MSM7625) {
                if(!strncmp(mDeviceName, "msm7625a" , 8))
                    mCurrentTarget = TARGET_MSM7625A;
            }
            if(mCurrentTarget == TARGET_MSM7627) {
                if(!strncmp(mDeviceName, "msm7627a" , 8))
                    mCurrentTarget = TARGET_MSM7627A;
            }
            break;
        }
    }
    LOGV(" Storing the current target type as %d ", mCurrentTarget );
    return;
}

void *openCamera(void *data) {
    LOGV(" openCamera : E");
    if (!libmmcamera) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        return false;
    }

    *(void **)&LINK_mm_camera_init =
        ::dlsym(libmmcamera, "mm_camera_init");

    *(void **)&LINK_mm_camera_exec =
        ::dlsym(libmmcamera, "mm_camera_exec");

    *(void **)&LINK_mm_camera_deinit =
        ::dlsym(libmmcamera, "mm_camera_deinit");


    if (MM_CAMERA_SUCCESS != LINK_mm_camera_init(&mCfgControl, &mCamNotify, &mCamOps, 0)) {
        LOGE("startCamera: mm_camera_init failed:");
        return FALSE;
    }

    uint8_t camera_id8 = (uint8_t)HAL_currentCameraId;
    if (MM_CAMERA_SUCCESS != mCfgControl.mm_camera_set_parm(CAMERA_PARM_CAMERA_ID, &camera_id8)) {
        LOGE("setting camera id failed");
        LINK_mm_camera_deinit();
        return FALSE;
    }

    camera_mode_t mode = (camera_mode_t)HAL_currentCameraMode;
    if (MM_CAMERA_SUCCESS != mCfgControl.mm_camera_set_parm(CAMERA_PARM_MODE, &mode)) {
        LOGE("startCamera: CAMERA_PARM_MODE failed:");
        LINK_mm_camera_deinit();
        return FALSE;
    }

    if (MM_CAMERA_SUCCESS != LINK_mm_camera_exec()) {
        LOGE("startCamera: mm_camera_exec failed:");
        return FALSE;
    }

    if (CAMERA_MODE_3D == mode) {
        camera_3d_frame_t snapshotFrame;
        snapshotFrame.frame_type = CAM_SNAPSHOT_FRAME;
        if(MM_CAMERA_SUCCESS !=
            mCfgControl.mm_camera_get_parm(CAMERA_PARM_3D_FRAME_FORMAT,
                (void *)&snapshotFrame)){
            LOGE("%s: get 3D format failed", __func__);
            LINK_mm_camera_deinit();
            return FALSE;
        }
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->mSnapshot3DFormat = snapshotFrame.format;
            LOGI("%s: 3d format  snapshot %d", __func__, obj->mSnapshot3DFormat);
        }
    }

    LOGV(" openCamera : X");
    return NULL;
}
//-------------------------------------------------------------------------------------
static Mutex singleton_lock;
static bool singleton_releasing;
static nsecs_t singleton_releasing_start_time;
static const nsecs_t SINGLETON_RELEASING_WAIT_TIME = seconds_to_nanoseconds(5);
static const nsecs_t SINGLETON_RELEASING_RECHECK_TIMEOUT = seconds_to_nanoseconds(1);
static Condition singleton_wait;

static void receive_camframe_callback(struct msm_frame *frame);
static void receive_liveshot_callback(liveshot_status status, uint32_t jpeg_size);
static void receive_camstats_callback(camstats_type stype, camera_preview_histogram_info* histinfo);
static void receive_camframe_video_callback(struct msm_frame *frame); // 720p
static int8_t receive_event_callback(mm_camera_event* event);
static void receive_shutter_callback(common_crop_t *crop);
static void receive_camframe_error_callback(camera_error_type err);
static int fb_fd = -1;
static int32_t mMaxZoom = 0;
static bool zoomSupported = false;
static int dstOffset = 0;

static int16_t * zoomRatios;


/* When using MDP zoom, double the preview buffers. The usage of these
 * buffers is as follows:
 * 1. As all the buffers comes under a single FD, and at initial registration,
 * this FD will be passed to surface flinger, surface flinger can have access
 * to all the buffers when needed.
 * 2. Only "kPreviewBufferCount" buffers (SrcSet) will be registered with the
 * camera driver to receive preview frames. The remaining buffers (DstSet),
 * will be used at HAL and by surface flinger only when crop information
 * is present in the frame.
 * 3. When there is no crop information, there will be no call to MDP zoom,
 * and the buffers in SrcSet will be passed to surface flinger to display.
 * 4. With crop information present, MDP zoom will be called, and the final
 * data will be placed in a buffer from DstSet, and this buffer will be given
 * to surface flinger to display.
 */
#define NUM_MORE_BUFS 2

QualcommCameraHardware::QualcommCameraHardware()
    : mParameters(),
      mCameraRunning(false),
      mPreviewInitialized(false),
      mPreviewThreadRunning(false),
      mHFRThreadRunning(false),
      mFrameThreadRunning(false),
      mVideoThreadRunning(false),
      mSnapshotThreadRunning(false),
      mJpegThreadRunning(false),
      mSmoothzoomThreadRunning(false),
      mSmoothzoomThreadExit(false),
      mInSnapshotMode(false),
      mEncodePending(false),
      mSnapshotFormat(0),
      mFirstFrame(true),
      mReleasedRecordingFrame(false),
      mPreviewFrameSize(0),
      mRawSize(0),
      mCbCrOffsetRaw(0),
      mAutoFocusThreadRunning(false),
      mInitialized(false),
      mBrightness(0),
      mSkinToneEnhancement(0),
      mHJR(0),
      mInPreviewCallback(false),
      mUseOverlay(0),
      mIs3DModeOn(0),
      mOverlay(0),
      mMsgEnabled(0),
      mNotifyCallback(0),
      mDataCallback(0),
      mDataCallbackTimestamp(0),
      mCallbackCookie(0),
      mDebugFps(0),
      mSnapshotDone(0),
      maxSnapshotWidth(0),
      maxSnapshotHeight(0),
      mHasAutoFocusSupport(0),
      mDisEnabled(0),
      mRotation(0),
      mResetOverlayCrop(false),
      mThumbnailWidth(0),
      mThumbnailHeight(0),
      strTexturesOn(false),
      mPictureWidth(0),
      mPictureHeight(0),
      mPostviewWidth(0),
      mPostviewHeight(0),
      mZslEnable(0),
      mZslFlashEnable(false),
      mZslPanorama(false),
      mSnapshotCancel(false),
      mHFRMode(false),
      mActualPictWidth(0),
      mActualPictHeight(0),
      mDenoiseValue(0),
      mPreviewStopping(false),
      mInHFRThread(false)
{
    LOGI("QualcommCameraHardware constructor E");
    mMMCameraDLRef = MMCameraDL::getInstance();
    libmmcamera = mMMCameraDLRef->pointer();
    char value[PROPERTY_VALUE_MAX];

    if(HAL_currentSnapshotMode == CAMERA_SNAPSHOT_ZSL) {
        LOGI("%s: this is ZSL mode", __FUNCTION__);
        mZslEnable = true;
    }

    storeTargetType();

    if(HAL_currentCameraMode == CAMERA_SUPPORT_MODE_3D){
        mIs3DModeOn = true;
    }
    /* TODO: Will remove this command line interface at end */
    property_get("persist.camera.hal.3dmode", value, "0");
    int mode = atoi(value);
    if( mode  == 1) {
        mIs3DModeOn = true;
        HAL_currentCameraMode = CAMERA_MODE_3D;
    }

    if( (pthread_create(&mDeviceOpenThread, NULL, openCamera, NULL)) != 0) {
        LOGE(" openCamera thread creation failed ");
    }
    memset(&mDimension, 0, sizeof(mDimension));
    memset(&mCrop, 0, sizeof(mCrop));
    memset(&zoomCropInfo, 0, sizeof(zoom_crop_info));
    property_get("persist.debug.sf.showfps", value, "0");
    mDebugFps = atoi(value);
    if( mCurrentTarget == TARGET_MSM7630 || mCurrentTarget == TARGET_MSM8660 ) {
        kPreviewBufferCountActual = kPreviewBufferCount;
        kRecordBufferCount = RECORD_BUFFERS;
        recordframes = new msm_frame[kRecordBufferCount];
        record_buffers_tracking_flag = new bool[kRecordBufferCount];
    }
    else {
        kPreviewBufferCountActual = kPreviewBufferCount + NUM_MORE_BUFS;
        if( mCurrentTarget == TARGET_QSD8250 ) {
            kRecordBufferCount = RECORD_BUFFERS_8x50;
            recordframes = new msm_frame[kRecordBufferCount];
            record_buffers_tracking_flag = new bool[kRecordBufferCount];
        }
    }

    switch(mCurrentTarget){
        case TARGET_MSM7627:
        case TARGET_MSM7627A:
            jpegPadding = 0; // to be checked.
            break;
        case TARGET_QSD8250:
        case TARGET_MSM7630:
        case TARGET_MSM8660:
            jpegPadding = 0;
            break;
        default:
            jpegPadding = 0;
            break;
    }
    // Initialize with default format values. The format values can be
    // overriden when application requests.
    mDimension.prev_format     = CAMERA_YUV_420_NV21;
    mPreviewFormat             = CAMERA_YUV_420_NV21;
    mDimension.enc_format      = CAMERA_YUV_420_NV21;
    if((mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660))
        mDimension.enc_format  = CAMERA_YUV_420_NV12;

    mDimension.main_img_format = CAMERA_YUV_420_NV21;
    mDimension.thumb_format    = CAMERA_YUV_420_NV21;

    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660) ){
        /* DIS is disabled all the time in VPE support targets.
         * No provision for the user to control this.
         */
        mDisEnabled = 0;
        /* Get the DIS value from properties, to check whether
         * DIS is disabled or not. If the property is not found
         * default to DIS disabled.*/
        property_get("persist.camera.hal.dis", value, "0");
        mDisEnabled = atoi(value);
        mVpeEnabled = 1;
    }

    if(mIs3DModeOn) {
        mDisEnabled = 0;
    }

    LOGV("constructor EX");
}

void QualcommCameraHardware::hasAutoFocusSupport(){
    if( !mCamOps.mm_camera_is_supported(CAMERA_OPS_FOCUS)){
        LOGI("AutoFocus is not supported");
        mHasAutoFocusSupport = false;
    }else {
        mHasAutoFocusSupport = true;
    }
    if(mZslEnable)
        mHasAutoFocusSupport = false;
}

//filter Picture sizes based on max width and height
void QualcommCameraHardware::filterPictureSizes(){
    unsigned int i;
    if(PICTURE_SIZE_COUNT <= 0)
        return;
    maxSnapshotWidth = picture_sizes[0].width;
    maxSnapshotHeight = picture_sizes[0].height;
   // Iterate through all the width and height to find the max value
    for(i =0; i<PICTURE_SIZE_COUNT;i++){
        if(((maxSnapshotWidth < picture_sizes[i].width) &&
            (maxSnapshotHeight <= picture_sizes[i].height))){
            maxSnapshotWidth = picture_sizes[i].width;
            maxSnapshotHeight = picture_sizes[i].height;
        }
    }
    if(mZslEnable){
        // due to lack of PMEM we restrict to lower resolution
        picture_sizes_ptr = zsl_picture_sizes;
        supportedPictureSizesCount = 7;
    }else{
    picture_sizes_ptr = picture_sizes;
    supportedPictureSizesCount = PICTURE_SIZE_COUNT;
    }
}

bool QualcommCameraHardware::supportsSceneDetection() {
   unsigned int prop = 0;
   for(prop=0; prop<sizeof(boardProperties)/sizeof(board_property); prop++) {
       if((mCurrentTarget == boardProperties[prop].target)
          && boardProperties[prop].hasSceneDetect == true) {
           return true;
           break;
       }
   }
   return false;
}

bool QualcommCameraHardware::supportsSelectableZoneAf() {
   unsigned int prop = 0;
   for(prop=0; prop<sizeof(boardProperties)/sizeof(board_property); prop++) {
       if((mCurrentTarget == boardProperties[prop].target)
          && boardProperties[prop].hasSelectableZoneAf == true) {
           return true;
           break;
       }
   }
   return false;
}

bool QualcommCameraHardware::supportsFaceDetection() {
   unsigned int prop = 0;
   for(prop=0; prop<sizeof(boardProperties)/sizeof(board_property); prop++) {
       if((mCurrentTarget == boardProperties[prop].target)
          && boardProperties[prop].hasFaceDetect == true) {
           return true;
           break;
       }
   }
   return false;
}

void QualcommCameraHardware::initDefaultParameters()
{
    LOGI("initDefaultParameters E");
    mDimension.picture_width = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;
    mDimension.ui_thumbnail_width =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].width;
    mDimension.ui_thumbnail_height =
            thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;
    bool ret = native_set_parms(CAMERA_PARM_DIMENSION,
               sizeof(cam_ctrl_dimension_t),(void *) &mDimension);
    if(ret != true) {
        LOGE("CAMERA_PARM_DIMENSION failed!!!");
        return;
    }
    hasAutoFocusSupport();
    //Disable DIS for Web Camera
    if( !mCfgControl.mm_camera_is_supported(CAMERA_PARM_VIDEO_DIS)){
        LOGV("DISABLE DIS");
        mDisEnabled = 0;
    }else {
        LOGV("Enable DIS");
    }
    // Initialize constant parameter strings. This will happen only once in the
    // lifetime of the mediaserver process.
    if (!parameter_string_initialized) {
        antibanding_values = create_values_str(
            antibanding, sizeof(antibanding) / sizeof(str_map));
        effect_values = create_values_str(
            effects, sizeof(effects) / sizeof(str_map));
        autoexposure_values = create_values_str(
            autoexposure, sizeof(autoexposure) / sizeof(str_map));
        whitebalance_values = create_values_str(
            whitebalance, sizeof(whitebalance) / sizeof(str_map));
        //filter picture sizes
        filterPictureSizes();
        picture_size_values = create_sizes_str(
                picture_sizes_ptr, supportedPictureSizesCount);
        preview_size_values = create_sizes_str(
                preview_sizes,  PREVIEW_SIZE_COUNT);
        hfr_size_values = create_sizes_str(
                hfr_sizes, HFR_SIZE_COUNT);

        fps_ranges_supported_values = create_fps_str(
            FpsRangesSupported,FPS_RANGES_SUPPORTED_COUNT );
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
            fps_ranges_supported_values);
        mParameters.setPreviewFpsRange(MINIMUM_FPS*1000,MAXIMUM_FPS*1000);

        flash_values = create_values_str(
            flash, sizeof(flash) / sizeof(str_map));
        if(mHasAutoFocusSupport){
            focus_mode_values = create_values_str(
                    focus_modes, sizeof(focus_modes) / sizeof(str_map));
        }
        iso_values = create_values_str(
            iso,sizeof(iso)/sizeof(str_map));
        lensshade_values = create_values_str(
            lensshade,sizeof(lensshade)/sizeof(str_map));
        mce_values = create_values_str(
            mce,sizeof(mce)/sizeof(str_map));
        hfr_values = create_values_str(
            hfr,sizeof(hfr)/sizeof(str_map));
        //Currently Enabling Histogram for 8x60
        if(mCurrentTarget == TARGET_MSM8660) {
            histogram_values = create_values_str(
                histogram,sizeof(histogram)/sizeof(str_map));
        }
        //Currently Enabling Skin Tone Enhancement for 8x60 and 7630
        if((mCurrentTarget == TARGET_MSM8660)||(mCurrentTarget == TARGET_MSM7630)) {
            skinToneEnhancement_values = create_values_str(
                skinToneEnhancement,sizeof(skinToneEnhancement)/sizeof(str_map));
        }
        if(mHasAutoFocusSupport){
            touchafaec_values = create_values_str(
                touchafaec,sizeof(touchafaec)/sizeof(str_map));
        }

        picture_format_values = create_values_str(
            picture_formats, sizeof(picture_formats)/sizeof(str_map));

        if(mCurrentTarget == TARGET_MSM8660 ||
          (mCurrentTarget == TARGET_MSM7625A ||
           mCurrentTarget == TARGET_MSM7627A)) {
            denoise_values = create_values_str(
                denoise, sizeof(denoise) / sizeof(str_map));
        }
       if( mCfgControl.mm_camera_query_parms(CAMERA_PARM_ZOOM_RATIO, (void **)&zoomRatios, (uint32_t *) &mMaxZoom) == MM_CAMERA_SUCCESS)
       {
            zoomSupported = true;
            if( mMaxZoom >0) {
                LOGE("Maximum zoom value is %d", mMaxZoom);
                if(zoomRatios != NULL) {
                    zoom_ratio_values =  create_str(zoomRatios, mMaxZoom);
                } else {
                    LOGE("Failed to get zoomratios ..");
                }
           } else {
               zoomSupported = false;
           }
       } else {
            zoomSupported = false;
            LOGE("Failed to get maximum zoom value...setting max "
                    "zoom to zero");
            mMaxZoom = 0;
        }
        preview_frame_rate_values = create_values_range_str(
            MINIMUM_FPS, MAXIMUM_FPS);

        scenemode_values = create_values_str(
            scenemode, sizeof(scenemode) / sizeof(str_map));

        if(supportsSceneDetection()) {
            scenedetect_values = create_values_str(
                scenedetect, sizeof(scenedetect) / sizeof(str_map));
        }

        if(mHasAutoFocusSupport && supportsSelectableZoneAf()){
            selectable_zone_af_values = create_values_str(
                selectable_zone_af, sizeof(selectable_zone_af) / sizeof(str_map));
        }

        if(mHasAutoFocusSupport && supportsFaceDetection()) {
            facedetection_values = create_values_str(
                facedetection, sizeof(facedetection) / sizeof(str_map));
        }

        redeye_reduction_values = create_values_str(
            redeye_reduction, sizeof(redeye_reduction) / sizeof(str_map));

        parameter_string_initialized = true;
    }

    mParameters.setPreviewSize(DEFAULT_PREVIEW_WIDTH, DEFAULT_PREVIEW_HEIGHT);
    mDimension.display_width = DEFAULT_PREVIEW_WIDTH;
    mDimension.display_height = DEFAULT_PREVIEW_HEIGHT;

    mParameters.setPreviewFrameRate(DEFAULT_FPS);
    if( mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS)){
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            preview_frame_rate_values.string());
     } else {
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            DEFAULT_FPS);
     }
    mParameters.setPreviewFrameRateMode("frame-rate-auto");
    mParameters.setPreviewFormat("yuv420sp"); // informative
    mParameters.set("overlay-format", HAL_PIXEL_FORMAT_YCbCr_420_SP);

    mParameters.setPictureSize(DEFAULT_PICTURE_WIDTH, DEFAULT_PICTURE_HEIGHT);
    mParameters.setPictureFormat("jpeg"); // informative

    mParameters.set(CameraParameters::KEY_JPEG_QUALITY, "85"); // max quality
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

    if(zoomSupported){
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
        LOGV("max zoom is %d", mMaxZoom-1);
        /* mMaxZoom value that the query interface returns is the size
         * of zoom table. So the actual max zoom value will be one
         * less than that value.
         */
        mParameters.set("max-zoom",mMaxZoom-1);
        mParameters.set(CameraParameters::KEY_ZOOM_RATIOS,
                            zoom_ratio_values);
    } else {
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");
    }
    /* Enable zoom support for video application if VPE enabled */
    if(zoomSupported && mVpeEnabled) {
        mParameters.set("video-zoom-support", "true");
    } else {
        mParameters.set("video-zoom-support", "false");
    }

    mParameters.set(CameraParameters::KEY_ANTIBANDING,
                    CameraParameters::ANTIBANDING_OFF);
    mParameters.set(CameraParameters::KEY_EFFECT,
                    CameraParameters::EFFECT_NONE);
    mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE,
                    CameraParameters::AUTO_EXPOSURE_FRAME_AVG);
    mParameters.set(CameraParameters::KEY_WHITE_BALANCE,
                    CameraParameters::WHITE_BALANCE_AUTO);
    if( (mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_QSD8250)
        && (mCurrentTarget != TARGET_MSM8660)) {
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
                    "yuv420sp");
    }
    else {
        preview_format_values = create_values_str(
            preview_formats, sizeof(preview_formats) / sizeof(str_map));
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
                preview_format_values.string());
    }

    frame_rate_mode_values = create_values_str(
            frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map));
 if( mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS_MODE)){
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATE_MODES,
                    frame_rate_mode_values.string());
    }

    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                    preview_size_values.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    picture_size_values.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
                    antibanding_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_EFFECTS, effect_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_AUTO_EXPOSURE, autoexposure_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
                    whitebalance_values);

    if(mHasAutoFocusSupport){
       mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                    focus_mode_values);
       mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                    CameraParameters::FOCUS_MODE_AUTO);
    } else {
       mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                   CameraParameters::FOCUS_MODE_INFINITY);
       mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                   CameraParameters::FOCUS_MODE_INFINITY);
    }

    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
                    picture_format_values);

    if(mCfgControl.mm_camera_is_supported(CAMERA_PARM_LED_MODE)) {
        mParameters.set(CameraParameters::KEY_FLASH_MODE,
                        CameraParameters::FLASH_MODE_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
                        flash_values);
    }

    mParameters.set(CameraParameters::KEY_MAX_SHARPNESS,
            CAMERA_MAX_SHARPNESS);
    mParameters.set(CameraParameters::KEY_MAX_CONTRAST,
            CAMERA_MAX_CONTRAST);
    mParameters.set(CameraParameters::KEY_MAX_SATURATION,
            CAMERA_MAX_SATURATION);

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

    mParameters.set("luma-adaptation", "3");
    mParameters.set("skinToneEnhancement", "0");
    mParameters.set("zoom-supported", "true");
    mParameters.set("zoom", 0);
    mParameters.set(CameraParameters::KEY_PICTURE_FORMAT,
                    CameraParameters::PIXEL_FORMAT_JPEG);

    mParameters.set(CameraParameters::KEY_SHARPNESS,
                    CAMERA_DEF_SHARPNESS);
    mParameters.set(CameraParameters::KEY_CONTRAST,
                    CAMERA_DEF_CONTRAST);
    mParameters.set(CameraParameters::KEY_SATURATION,
                    CAMERA_DEF_SATURATION);

    mParameters.set(CameraParameters::KEY_ISO_MODE,
                    CameraParameters::ISO_AUTO);
    mParameters.set(CameraParameters::KEY_LENSSHADE,
                    CameraParameters::LENSSHADE_ENABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_ISO_MODES,
                    iso_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_LENSSHADE_MODES,
                    lensshade_values);
    mParameters.set(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT,
                    CameraParameters::MCE_ENABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_MEM_COLOR_ENHANCE_MODES,
                    mce_values);
    if(mCfgControl.mm_camera_is_supported(CAMERA_PARM_HFR)) {
        mParameters.set(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE,
                    CameraParameters::VIDEO_HFR_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_HFR_SIZES,
                    hfr_size_values.string());
        mParameters.set(CameraParameters::KEY_SUPPORTED_VIDEO_HIGH_FRAME_RATE_MODES,
                    hfr_values);
    } else
        mParameters.set(CameraParameters::KEY_SUPPORTED_HFR_SIZES,"");

    mParameters.set(CameraParameters::KEY_HISTOGRAM,
                    CameraParameters::HISTOGRAM_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_HISTOGRAM_MODES,
                    histogram_values);
    mParameters.set(CameraParameters::KEY_SKIN_TONE_ENHANCEMENT,
                    CameraParameters::SKIN_TONE_ENHANCEMENT_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SKIN_TONE_ENHANCEMENT_MODES,
                    skinToneEnhancement_values);
    mParameters.set(CameraParameters::KEY_SCENE_MODE,
                    CameraParameters::SCENE_MODE_AUTO);
    mParameters.set("strtextures", "OFF");

    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
                    scenemode_values);
    mParameters.set(CameraParameters::KEY_DENOISE,
                    CameraParameters::DENOISE_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_DENOISE,
                    denoise_values);
    mParameters.set(CameraParameters::KEY_TOUCH_AF_AEC,
                    CameraParameters::TOUCH_AF_AEC_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_TOUCH_AF_AEC,
                    touchafaec_values);
    mParameters.setTouchIndexAec(-1, -1);
    mParameters.setTouchIndexAf(-1, -1);
    mParameters.set("touchAfAec-dx","100");
    mParameters.set("touchAfAec-dy","100");
    mParameters.set(CameraParameters::KEY_SCENE_DETECT,
                    CameraParameters::SCENE_DETECT_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_DETECT,
                    scenedetect_values);
    mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF,
                    CameraParameters::SELECTABLE_ZONE_AF_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SELECTABLE_ZONE_AF,
                    selectable_zone_af_values);
    mParameters.set(CameraParameters::KEY_FACE_DETECTION,
                    CameraParameters::FACE_DETECTION_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_FACE_DETECTION,
                    facedetection_values);
    mParameters.set(CameraParameters::KEY_REDEYE_REDUCTION,
                    CameraParameters::REDEYE_REDUCTION_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_REDEYE_REDUCTION,
                    redeye_reduction_values);

    float focalLength = 0.0f;
    float horizontalViewAngle = 0.0f;
    float verticalViewAngle = 0.0f;

    mCfgControl.mm_camera_get_parm(CAMERA_PARM_FOCAL_LENGTH,
            (void *)&focalLength);
    mParameters.setFloat(CameraParameters::KEY_FOCAL_LENGTH,
                    focalLength);
    mCfgControl.mm_camera_get_parm(CAMERA_PARM_HORIZONTAL_VIEW_ANGLE,
            (void *)&horizontalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,
                    horizontalViewAngle);
    mCfgControl.mm_camera_get_parm(CAMERA_PARM_VERTICAL_VIEW_ANGLE,
            (void *)&verticalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,
                    verticalViewAngle);
    numCapture = 1;
    if(mZslEnable) {
        int maxSnapshot = MAX_SNAPSHOT_BUFFERS - 2;
        char value[5];
        property_get("persist.camera.hal.capture", value, "1");
        numCapture = atoi(value);
        if(numCapture > maxSnapshot)
            numCapture = maxSnapshot;
        else if(numCapture < 1)
            numCapture = 1;
        mParameters.set("capture-burst-captures-values", maxSnapshot);
        mParameters.set("capture-burst-interval-supported", "false");
    }
    mParameters.set("num-snaps-per-shutter", numCapture);
    LOGI("%s: setting num-snaps-per-shutter to %d", __FUNCTION__, numCapture);
    if(mIs3DModeOn)
        mParameters.set("3d-frame-format", "left-right");

    if (setParameters(mParameters) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
    mUseOverlay = useOverlay();

    /* Initialize the camframe_timeout_flag*/
    Mutex::Autolock l(&mCamframeTimeoutLock);
    camframe_timeout_flag = FALSE;
    mPostviewHeap = NULL;
    mDisplayHeap = NULL;
    mLastPreviewFrameHeap = NULL;

    mInitialized = true;
    strTexturesOn = false;

    LOGI("initDefaultParameters X");
}


#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

bool QualcommCameraHardware::startCamera()
{
    LOGV("startCamera E");
    if( mCurrentTarget == TARGET_MAX ) {
        LOGE(" Unable to determine the target type. Camera will not work ");
        return false;
    }
#if DLOPEN_LIBMMCAMERA

    LOGV("loading liboemcamera at %p", libmmcamera);
    if (!libmmcamera) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        return false;
    }

    *(void **)&LINK_cam_frame =
        ::dlsym(libmmcamera, "cam_frame");
    *(void **)&LINK_camframe_terminate =
        ::dlsym(libmmcamera, "camframe_terminate");

    *(void **)&LINK_jpeg_encoder_init =
        ::dlsym(libmmcamera, "jpeg_encoder_init");

    *(void **)&LINK_jpeg_encoder_encode =
        ::dlsym(libmmcamera, "jpeg_encoder_encode");

    *(void **)&LINK_jpeg_encoder_join =
        ::dlsym(libmmcamera, "jpeg_encoder_join");

    mCamNotify.preview_frame_cb = &receive_camframe_callback;

    mCamNotify.camstats_cb = &receive_camstats_callback;

    mCamNotify.on_event =  &receive_event_callback;

    mCamNotify.on_error_event = &receive_camframe_error_callback;

    // 720 p new recording functions
    mCamNotify.video_frame_cb = &receive_camframe_video_callback;
     // 720 p new recording functions

    *(void **)&LINK_camframe_add_frame = ::dlsym(libmmcamera, "camframe_add_frame");

    *(void **)&LINK_camframe_release_all_frames = ::dlsym(libmmcamera, "camframe_release_all_frames");

    *(void **)&LINK_mmcamera_shutter_callback =
        ::dlsym(libmmcamera, "mmcamera_shutter_callback");

    *LINK_mmcamera_shutter_callback = receive_shutter_callback;

    *(void**)&LINK_jpeg_encoder_setMainImageQuality =
        ::dlsym(libmmcamera, "jpeg_encoder_setMainImageQuality");

    *(void**)&LINK_jpeg_encoder_setThumbnailQuality =
        ::dlsym(libmmcamera, "jpeg_encoder_setThumbnailQuality");

    *(void**)&LINK_jpeg_encoder_setRotation =
        ::dlsym(libmmcamera, "jpeg_encoder_setRotation");

    *(void**)&LINK_jpeg_encoder_get_buffer_offset =
        ::dlsym(libmmcamera, "jpeg_encoder_get_buffer_offset");

    *(void**)&LINK_jpeg_encoder_set_3D_info =
        ::dlsym(libmmcamera, "jpeg_encoder_set_3D_info");

/* Disabling until support is available.
    *(void**)&LINK_jpeg_encoder_setLocation =
        ::dlsym(libmmcamera, "jpeg_encoder_setLocation");
*/
    *(void **)&LINK_cam_conf =
        ::dlsym(libmmcamera, "cam_conf");

/* Disabling until support is available.
    *(void **)&LINK_default_sensor_get_snapshot_sizes =
        ::dlsym(libmmcamera, "default_sensor_get_snapshot_sizes");
*/
    *(void **)&LINK_launch_cam_conf_thread =
        ::dlsym(libmmcamera, "launch_cam_conf_thread");

    *(void **)&LINK_release_cam_conf_thread =
        ::dlsym(libmmcamera, "release_cam_conf_thread");

    mCamNotify.on_liveshot_event = &receive_liveshot_callback;

    *(void **)&LINK_cancel_liveshot =
        ::dlsym(libmmcamera, "cancel_liveshot");

    *(void **)&LINK_set_liveshot_params =
        ::dlsym(libmmcamera, "set_liveshot_params");

    *(void **)&LINK_mm_camera_destroy =
        ::dlsym(libmmcamera, "mm_camera_destroy");


/* Disabling until support is available.
    *(void **)&LINK_zoom_crop_upscale =
        ::dlsym(libmmcamera, "zoom_crop_upscale");
*/

#else
    mCamNotify.preview_frame_cb = &receive_camframe_callback;
    mCamNotify.camstats_cb = &receive_camstats_callback;
    mCamNotify.on_event =  &receive_event_callback;

    mmcamera_shutter_callback = receive_shutter_callback;
     mCamNotify.on_liveshot_event = &receive_liveshot_callback;
     mCamNotify.video_frame_cb = &receive_camframe_video_callback;

#endif // DLOPEN_LIBMMCAMERA

    if((mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_MSM8660)){
        fb_fd = open("/dev/graphics/fb0", O_RDWR);
        if (fb_fd < 0) {
            LOGE("startCamera: fb0 open failed: %s!", strerror(errno));
            return FALSE;
        }
    }
    if (pthread_join(mDeviceOpenThread, NULL) != 0) {
         LOGE("openCamera thread exit failed");
         return false;
    }

    mCfgControl.mm_camera_query_parms(CAMERA_PARM_PICT_SIZE, (void **)&picture_sizes, &PICTURE_SIZE_COUNT);
    if ((picture_sizes == NULL) || (!PICTURE_SIZE_COUNT)) {
        LOGE("startCamera X: could not get snapshot sizes");
        return false;
    }
     LOGV("startCamera picture_sizes %p PICTURE_SIZE_COUNT %d", picture_sizes, PICTURE_SIZE_COUNT);
    mCfgControl.mm_camera_query_parms(CAMERA_PARM_PREVIEW_SIZE, (void **)&preview_sizes, &PREVIEW_SIZE_COUNT);
    if ((preview_sizes == NULL) || (!PREVIEW_SIZE_COUNT)) {
        LOGE("startCamera X: could not get preview sizes");
        return false;
    }
    LOGV("startCamera preview_sizes %p previewSizeCount %d", preview_sizes, PREVIEW_SIZE_COUNT);

    mCfgControl.mm_camera_query_parms(CAMERA_PARM_HFR_SIZE, (void **)&hfr_sizes, &HFR_SIZE_COUNT);
    if ((hfr_sizes == NULL) || (!HFR_SIZE_COUNT)) {
        LOGE("startCamera X: could not get hfr sizes");
        return false;
    }
    LOGV("startCamera hfr_sizes %p hfrSizeCount %d", hfr_sizes, HFR_SIZE_COUNT);


    LOGV("startCamera X");
    return true;
}

status_t QualcommCameraHardware::dump(int fd,
                                      const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    // Dump internal primitives.
    result.append("QualcommCameraHardware::dump");
    snprintf(buffer, 255, "mMsgEnabled (%d)\n", mMsgEnabled);
    result.append(buffer);
    int width, height;
    mParameters.getPreviewSize(&width, &height);
    snprintf(buffer, 255, "preview width(%d) x height (%d)\n", width, height);
    result.append(buffer);
    mParameters.getPictureSize(&width, &height);
    snprintf(buffer, 255, "raw width(%d) x height (%d)\n", width, height);
    result.append(buffer);
    snprintf(buffer, 255,
             "preview frame size(%d), raw size (%d), jpeg size (%d) "
             "and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize,
             mJpegSize, mJpegMaxSize);
    result.append(buffer);
    write(fd, result.string(), result.size());

    // Dump internal objects.
    if (mPreviewHeap != 0) {
        mPreviewHeap->dump(fd, args);
    }
    if (mRawHeap != 0) {
        mRawHeap->dump(fd, args);
    }
    if (mJpegHeap != 0) {
        mJpegHeap->dump(fd, args);
    }
    mParameters.dump(fd, args);
    return NO_ERROR;
}

/* Issue ioctl calls related to starting Camera Operations*/
bool static native_start_ops(mm_camera_ops_type_t  type, void* value)
{
    if(mCamOps.mm_camera_start(type, value,NULL) != MM_CAMERA_SUCCESS) {
        LOGE("native_start_ops: type %d error %s",
            type,strerror(errno));
        return false;
    }
    return true;
}

/* Issue ioctl calls related to stopping Camera Operations*/
bool static native_stop_ops(mm_camera_ops_type_t  type, void* value)
{
     if(mCamOps.mm_camera_stop(type, value,NULL) != MM_CAMERA_SUCCESS) {
        LOGE("native_stop_ops: type %d error %s",
            type,strerror(errno));
        return false;
    }
    return true;
}
/*==========================================================================*/

static int recordingState = 0;

#define GPS_PROCESSING_METHOD_SIZE  101
#define FOCAL_LENGTH_DECIMAL_PRECISON 100

static const char ExifAsciiPrefix[] = { 0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0 };
#define EXIF_ASCII_PREFIX_SIZE (sizeof(ExifAsciiPrefix))

static rat_t latitude[3];
static rat_t longitude[3];
static char lonref[2];
static char latref[2];
static rat_t altitude;
static rat_t gpsTimestamp[3];
static char gpsDatestamp[20];
static char dateTime[20];
static rat_t focalLength;
static char gpsProcessingMethod[EXIF_ASCII_PREFIX_SIZE + GPS_PROCESSING_METHOD_SIZE];



static void addExifTag(exif_tag_id_t tagid, exif_tag_type_t type,
                        uint32_t count, uint8_t copy, void *data) {

    if(exif_table_numEntries == MAX_EXIF_TABLE_ENTRIES) {
        LOGE("Number of entries exceeded limit");
        return;
    }

    int index = exif_table_numEntries;
    exif_data[index].tag_id = tagid;
	exif_data[index].tag_entry.type = type;
	exif_data[index].tag_entry.count = count;
	exif_data[index].tag_entry.copy = copy;
    if((type == EXIF_RATIONAL) && (count > 1))
        exif_data[index].tag_entry.data._rats = (rat_t *)data;
    if((type == EXIF_RATIONAL) && (count == 1))
        exif_data[index].tag_entry.data._rat = *(rat_t *)data;
    else if(type == EXIF_ASCII)
        exif_data[index].tag_entry.data._ascii = (char *)data;
    else if(type == EXIF_BYTE)
        exif_data[index].tag_entry.data._byte = *(uint8_t *)data;

    // Increase number of entries
    exif_table_numEntries++;
}

static void parseLatLong(const char *latlonString, int *pDegrees,
                           int *pMinutes, int *pSeconds ) {

    double value = atof(latlonString);
    value = fabs(value);
    int degrees = (int) value;

    double remainder = value - degrees;
    int minutes = (int) (remainder * 60);
    int seconds = (int) (((remainder * 60) - minutes) * 60 * 1000);

    *pDegrees = degrees;
    *pMinutes = minutes;
    *pSeconds = seconds;
}

static void setLatLon(exif_tag_id_t tag, const char *latlonString) {

    int degrees, minutes, seconds;

    parseLatLong(latlonString, &degrees, &minutes, &seconds);

    rat_t value[3] = { {degrees, 1},
                       {minutes, 1},
                       {seconds, 1000} };

    if(tag == EXIFTAGID_GPS_LATITUDE) {
        memcpy(latitude, value, sizeof(latitude));
        addExifTag(EXIFTAGID_GPS_LATITUDE, EXIF_RATIONAL, 3,
                    1, (void *)latitude);
    } else {
        memcpy(longitude, value, sizeof(longitude));
        addExifTag(EXIFTAGID_GPS_LONGITUDE, EXIF_RATIONAL, 3,
                    1, (void *)longitude);
    }
}

void QualcommCameraHardware::setGpsParameters() {
    const char *str = NULL;

    str = mParameters.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);

    if(str!=NULL ){
       memcpy(gpsProcessingMethod, ExifAsciiPrefix, EXIF_ASCII_PREFIX_SIZE);
       strncpy(gpsProcessingMethod + EXIF_ASCII_PREFIX_SIZE, str,
           GPS_PROCESSING_METHOD_SIZE-1);
       gpsProcessingMethod[EXIF_ASCII_PREFIX_SIZE + GPS_PROCESSING_METHOD_SIZE-1] = '\0';
       addExifTag(EXIFTAGID_GPS_PROCESSINGMETHOD, EXIF_ASCII,
           EXIF_ASCII_PREFIX_SIZE + strlen(gpsProcessingMethod + EXIF_ASCII_PREFIX_SIZE) + 1,
           1, (void *)gpsProcessingMethod);
    }

    str = NULL;

    //Set Latitude
    str = mParameters.get(CameraParameters::KEY_GPS_LATITUDE);
    if(str != NULL) {
        setLatLon(EXIFTAGID_GPS_LATITUDE, str);
        //set Latitude Ref
        float latitudeValue = mParameters.getFloat(CameraParameters::KEY_GPS_LATITUDE);
        latref[0] = 'N';
        if(latitudeValue < 0 ){
            latref[0] = 'S';
        }
        latref[1] = '\0';
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE_REF, latref);
        addExifTag(EXIFTAGID_GPS_LATITUDE_REF, EXIF_ASCII, 2,
                                1, (void *)latref);
    }

    //set Longitude
    str = NULL;
    str = mParameters.get(CameraParameters::KEY_GPS_LONGITUDE);
    if(str != NULL) {
        setLatLon(EXIFTAGID_GPS_LONGITUDE, str);
        //set Longitude Ref
        float longitudeValue = mParameters.getFloat(CameraParameters::KEY_GPS_LONGITUDE);
        lonref[0] = 'E';
        if(longitudeValue < 0){
            lonref[0] = 'W';
        }
        lonref[1] = '\0';
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE_REF, lonref);
        addExifTag(EXIFTAGID_GPS_LONGITUDE_REF, EXIF_ASCII, 2,
                                1, (void *)lonref);
    }

    //set Altitude
    str = NULL;
    str = mParameters.get(CameraParameters::KEY_GPS_ALTITUDE);
    if(str != NULL) {
        double value = atof(str);
        int ref = 0;
        if(value < 0){
            ref = 1;
            value = -value;
        }
        uint32_t value_meter = value * 1000;
        rat_t alt_value = {value_meter, 1000};
        memcpy(&altitude, &alt_value, sizeof(altitude));
        addExifTag(EXIFTAGID_GPS_ALTITUDE, EXIF_RATIONAL, 1,
                    1, (void *)&altitude);
        //set AltitudeRef
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE_REF, ref);
        addExifTag(EXIFTAGID_GPS_ALTITUDE_REF, EXIF_BYTE, 1,
                    1, (void *)&ref);
    }

    //set Gps TimeStamp
    str = NULL;
    str = mParameters.get(CameraParameters::KEY_GPS_TIMESTAMP);
    if(str != NULL) {

      long value = atol(str);
      time_t unixTime;
      struct tm *UTCTimestamp;

      unixTime = (time_t)value;
      UTCTimestamp = gmtime(&unixTime);

      strftime(gpsDatestamp, sizeof(gpsDatestamp), "%Y:%m:%d", UTCTimestamp);
      addExifTag(EXIFTAGID_GPS_DATESTAMP, EXIF_ASCII,
                          strlen(gpsDatestamp)+1 , 1, (void *)&gpsDatestamp);

      rat_t time_value[3] = { {UTCTimestamp->tm_hour, 1},
                              {UTCTimestamp->tm_min, 1},
                              {UTCTimestamp->tm_sec, 1} };


      memcpy(&gpsTimestamp, &time_value, sizeof(gpsTimestamp));
      addExifTag(EXIFTAGID_GPS_TIMESTAMP, EXIF_RATIONAL,
                  3, 1, (void *)&gpsTimestamp);
    }

}


bool QualcommCameraHardware::initZslParameter(void)
    {  LOGV("%s: E", __FUNCTION__);
       mParameters.getPictureSize(&mPictureWidth, &mPictureHeight);
       LOGV("initZslParamter E: picture size=%dx%d", mPictureWidth, mPictureHeight);
       if (updatePictureDimension(mParameters, mPictureWidth, mPictureHeight)) {
         mDimension.picture_width = mPictureWidth;
         mDimension.picture_height = mPictureHeight;
       }

       /* use the default thumbnail sizes */
        mZslParms.picture_width = mPictureWidth;
        mZslParms.picture_height = mPictureHeight;
        mZslParms.preview_width =  mDimension.display_width;
        mZslParms.preview_height = mDimension.display_height;
        mZslParms.useExternalBuffers = TRUE;
          /* fill main image size, thumbnail size, postview size into capture_params_t*/
        memset(&mZslCaptureParms, 0, sizeof(zsl_capture_params_t));
        mZslCaptureParms.thumbnail_height = mPostviewHeight;
        mZslCaptureParms.thumbnail_width = mPostviewWidth;
        LOGV("Number of snapshot to capture: %d",numCapture);
        mZslCaptureParms.num_captures = numCapture;

        return true;
    }


bool QualcommCameraHardware::initImageEncodeParameters(int size)
{
    LOGV("%s: E", __FUNCTION__);
    memset(&mImageEncodeParms, 0, sizeof(encode_params_t));
    int jpeg_quality = mParameters.getInt("jpeg-quality");
    bool ret;
    if (jpeg_quality >= 0) {
        LOGV("initJpegParameters, current jpeg main img quality =%d",
             jpeg_quality);
        //Application can pass quality of zero
        //when there is no back sensor connected.
        //as jpeg quality of zero is not accepted at
        //camera stack, pass default value.
        if(jpeg_quality == 0) jpeg_quality = 85;
        mImageEncodeParms.quality = jpeg_quality;
        ret = native_set_parms(CAMERA_PARM_JPEG_MAINIMG_QUALITY, sizeof(int), &jpeg_quality);
        if(!ret){
          LOGE("initJpegParametersX: failed to set main image quality");
          return false;
        }
    }

    int thumbnail_quality = mParameters.getInt("jpeg-thumbnail-quality");
    if (thumbnail_quality >= 0) {
        //Application can pass quality of zero
        //when there is no back sensor connected.
        //as quality of zero is not accepted at
        //camera stack, pass default value.
        if(thumbnail_quality == 0) thumbnail_quality = 85;
        LOGV("initJpegParameters, current jpeg thumbnail quality =%d",
             thumbnail_quality);
        /* TODO: check with mm-camera? */
        mImageEncodeParms.quality = thumbnail_quality;
        ret = native_set_parms(CAMERA_PARM_JPEG_THUMB_QUALITY, sizeof(int), &thumbnail_quality);
        if(!ret){
          LOGE("initJpegParameters X: failed to set thumbnail quality");
          return false;
        }
    }

    int rotation = mParameters.getInt("rotation");
    if (mIs3DModeOn)
        rotation = 0;
    if (rotation >= 0) {
        LOGV("initJpegParameters, rotation = %d", rotation);
        mImageEncodeParms.rotation = rotation;
    }

    jpeg_set_location();

    //set TimeStamp
    const char *str = mParameters.get(CameraParameters::KEY_EXIF_DATETIME);
    if(str != NULL) {
      strncpy(dateTime, str, 19);
      dateTime[19] = '\0';
      addExifTag(EXIFTAGID_EXIF_DATE_TIME_ORIGINAL, EXIF_ASCII,
                  20, 1, (void *)dateTime);
    }

    int focalLengthValue = (int) (mParameters.getFloat(
                CameraParameters::KEY_FOCAL_LENGTH) * FOCAL_LENGTH_DECIMAL_PRECISON);
    rat_t focalLengthRational = {focalLengthValue, FOCAL_LENGTH_DECIMAL_PRECISON};
    memcpy(&focalLength, &focalLengthRational, sizeof(focalLengthRational));
    addExifTag(EXIFTAGID_FOCAL_LENGTH, EXIF_RATIONAL, 1,
                1, (void *)&focalLength);

    if (mUseJpegDownScaling) {
      LOGV("initImageEncodeParameters: update main image", __func__);
      mImageEncodeParms.output_picture_width = mActualPictWidth;
      mImageEncodeParms.output_picture_height = mActualPictHeight;
    }
    mImageEncodeParms.cbcr_offset = mCbCrOffsetRaw;
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO)
        mImageEncodeParms.cbcr_offset = mCbCrOffsetRaw;
    /* TODO: check this */
    mImageEncodeParms.y_offset = 0;
    for(int i = 0; i < size; i++){
        memset(&mEncodeOutputBuffer[i], 0, sizeof(mm_camera_buffer_t));
        mEncodeOutputBuffer[i].ptr = (uint8_t *)mJpegHeap->mHeap->base() + (i * mJpegHeap->mBufferSize);
        mEncodeOutputBuffer[i].filled_size = mJpegMaxSize;
        mEncodeOutputBuffer[i].size = mJpegMaxSize;
        mEncodeOutputBuffer[i].fd = mJpegHeap->mHeap->getHeapID();
        mEncodeOutputBuffer[i].offset = 0;
    }
    mImageEncodeParms.p_output_buffer = mEncodeOutputBuffer;
    mImageEncodeParms.exif_data = exif_data;
    mImageEncodeParms.exif_numEntries = exif_table_numEntries;

    mImageEncodeParms.format3d = mIs3DModeOn;
    return true;
}

bool QualcommCameraHardware::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value)
{
    if(mCfgControl.mm_camera_set_parm(type,value) != MM_CAMERA_SUCCESS) {
        LOGE("native_set_parms failed: type %d length %d error %s",
            type, length, strerror(errno));
        return false;
    }
    return true;

}
bool QualcommCameraHardware::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value, int *result)
{
    mm_camera_status_t status;
    status = mCfgControl.mm_camera_set_parm(type,value);
    LOGV("native_set_parms status = %d", status);
    if( status == MM_CAMERA_SUCCESS || status == MM_CAMERA_ERR_INVALID_OPERATION){
        *result = status ;
        return true;
    }
    LOGE("%s: type %d length %d error %s, status %d", __FUNCTION__,
                                       type, length, strerror(errno), status);
   *result = status;
    return false;
}

void QualcommCameraHardware::jpeg_set_location()
{
    bool encode_location = true;
    camera_position_type pt;

#define PARSE_LOCATION(what,type,fmt,desc) do {                                \
        pt.what = 0;                                                           \
        const char *what##_str = mParameters.get("gps-"#what);                 \
        LOGV("GPS PARM %s --> [%s]", "gps-"#what, what##_str);                 \
        if (what##_str) {                                                      \
            type what = 0;                                                     \
            if (sscanf(what##_str, fmt, &what) == 1)                           \
                pt.what = what;                                                \
            else {                                                             \
                LOGE("GPS " #what " %s could not"                              \
                     " be parsed as a " #desc, what##_str);                    \
                encode_location = false;                                       \
            }                                                                  \
        }                                                                      \
        else {                                                                 \
            LOGV("GPS " #what " not specified: "                               \
                 "defaulting to zero in EXIF header.");                        \
            encode_location = false;                                           \
       }                                                                       \
    } while(0)

    PARSE_LOCATION(timestamp, long, "%ld", "long");
    if (!pt.timestamp) pt.timestamp = time(NULL);
    PARSE_LOCATION(altitude, short, "%hd", "short");
    PARSE_LOCATION(latitude, double, "%lf", "double float");
    PARSE_LOCATION(longitude, double, "%lf", "double float");

#undef PARSE_LOCATION

    if (encode_location) {
        LOGD("setting image location ALT %d LAT %lf LON %lf",
             pt.altitude, pt.latitude, pt.longitude);

        setGpsParameters();
        /* Disabling until support is available.
        if (!LINK_jpeg_encoder_setLocation(&pt)) {
            LOGE("jpeg_set_location: LINK_jpeg_encoder_setLocation failed.");
        }
        */
    }
    else LOGV("not setting image location");
}

static bool register_buf(int size,
                         int frame_size,
                         int cbcr_offset,
                         int yoffset,
                         int pmempreviewfd,
                         uint32_t offset,
                         uint8_t *buf,
                         int pmem_type,
                         bool vfe_can_write,
                         bool register_buffer)
{
    struct msm_pmem_info pmemBuf;
    CAMERA_HAL_UNUSED(frame_size);

    pmemBuf.type     = pmem_type;
    pmemBuf.fd       = pmempreviewfd;
    pmemBuf.offset   = offset;
    pmemBuf.len      = size;
    pmemBuf.vaddr    = buf;
    pmemBuf.y_off    = yoffset;
    pmemBuf.cbcr_off = cbcr_offset;

    pmemBuf.active   = vfe_can_write;

    LOGV("register_buf:  reg = %d buffer = %p",
         !register_buffer, buf);
    if(native_start_ops(register_buffer ? CAMERA_OPS_REGISTER_BUFFER :
        CAMERA_OPS_UNREGISTER_BUFFER ,(void *)&pmemBuf) < 0) {
         LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM  error %s",
               strerror(errno));
         return false;
         }

    return true;

}

void QualcommCameraHardware::runFrameThread(void *data)
{
    LOGV("runFrameThread E");
    int CbCrOffset = PAD_TO_WORD(previewWidth * previewHeight);

    if(libmmcamera)
    {
        LINK_cam_frame(data);
    }
    //waiting for preview thread to complete before clearing of the buffers
    mPreviewThreadWaitLock.lock();
    while (mPreviewThreadRunning) {
        LOGI("runframethread: waiting for preview  thread to complete.");
        mPreviewThreadWait.wait(mPreviewThreadWaitLock);
        LOGI("initPreview: old preview thread completed.");
    }
    mPreviewThreadWaitLock.unlock();

    mPreviewBusyQueue.flush();
    /* Flush the Free Q */
    LINK_camframe_release_all_frames(CAM_PREVIEW_FRAME);

    if(mIs3DModeOn != true)
    {
        if(mInHFRThread == false)
        {
            mPreviewHeap.clear();
        }
        else
        {
            //unregister preview buffers. we are not deallocating here.
            for (int cnt = 0; cnt < kPreviewBufferCountActual; ++cnt) {
                register_buf(mPreviewFrameSize,
                         mPreviewFrameSize,
                         CbCrOffset,
                         0,
                         mPreviewHeap->mHeap->getHeapID(),
                         mPreviewHeap->mAlignedBufferSize * cnt,
                         (uint8_t *)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt,
                         MSM_PMEM_PREVIEW,
                         false,
                         false );
            }
        }
    }
    if(( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)){
        if(mHFRMode != true) {
            mRecordHeap.clear();
            mRecordHeap = NULL;
        }else{
            LOGI("%s: unregister record buffers with camera driver", __FUNCTION__);
            register_record_buffers(false);
        }
    }

    mFrameThreadWaitLock.lock();
    mFrameThreadRunning = false;
    mFrameThreadWait.signal();
    mFrameThreadWaitLock.unlock();

    LOGV("runFrameThread X");
}


void QualcommCameraHardware::runPreviewThread(void *data)
{
    static int hfr_count = 0;
    msm_frame* frame = NULL;
    CAMERA_HAL_UNUSED(data);
    while((frame = mPreviewBusyQueue.get()) != NULL) {
        if (UNLIKELY(mDebugFps)) {
            debugShowPreviewFPS();
        }
        mCallbackLock.lock();
        int msgEnabled = mMsgEnabled;
        data_callback pcb = mDataCallback;
        void *pdata = mCallbackCookie;
        data_callback_timestamp rcb = mDataCallbackTimestamp;
        void *rdata = mCallbackCookie;
        data_callback mcb = mDataCallback;
        void *mdata = mCallbackCookie;
        mCallbackLock.unlock();

        // signal smooth zoom thread , that a new preview frame is available
        mSmoothzoomThreadWaitLock.lock();
        if(mSmoothzoomThreadRunning) {
        //LOGV("smooth thread in progress , got a previe frame");
            mSmoothzoomThreadWait.signal();
        }
        mSmoothzoomThreadWaitLock.unlock();

        // Find the offset within the heap of the current buffer.
        ssize_t offset_addr =
            (ssize_t)frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();
        ssize_t offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
        common_crop_t *crop = (common_crop_t *) (frame->cropinfo);
    #ifdef DUMP_PREVIEW_FRAMES
        static int frameCnt = 0;
        int written;
                if (frameCnt >= 0 && frameCnt <= 10 ) {
                    char buf[128];
                    sprintf(buf, "/data/%d_preview.yuv", frameCnt);
                    int file_fd = open(buf, O_RDWR | O_CREAT, 0777);
                    LOGV("dumping preview frame %d", frameCnt);
                    if (file_fd < 0) {
                        LOGE("cannot open file\n");
                    }
                    else
                    {
                        LOGV("dumping data");
                        written = write(file_fd, (uint8_t *)frame->buffer,
                            mPreviewFrameSize );
                        if(written < 0)
                          LOGE("error in data write");
                    }
                    close(file_fd);
              }
              frameCnt++;
    #endif
        mInPreviewCallback = true;
        if(mUseOverlay) {
            mOverlayLock.lock();
            if(mOverlay != NULL) {
                mOverlay->setFd(mPreviewHeap->mHeap->getHeapID());
                if (crop->in1_w != 0 && crop->in1_h != 0) {
                    zoomCropInfo.x = (crop->out1_w - crop->in1_w + 1) / 2 - 1;
                    zoomCropInfo.y = (crop->out1_h - crop->in1_h + 1) / 2 - 1;
                    zoomCropInfo.w = crop->in1_w;
                    zoomCropInfo.h = crop->in1_h;
                    /* There can be scenarios where the in1_wXin1_h and
                     * out1_wXout1_h are same. In those cases, reset the
                     * x and y to zero instead of negative for proper zooming
                     */
                    if(zoomCropInfo.x < 0) zoomCropInfo.x = 0;
                    if(zoomCropInfo.y < 0) zoomCropInfo.y = 0;
                    mOverlay->setCrop(zoomCropInfo.x, zoomCropInfo.y,
                        zoomCropInfo.w, zoomCropInfo.h);
                    /* Set mResetOverlayCrop to true, so that when there is
                     * no crop information, setCrop will be called
                     * with zero crop values.
                     */
                    mResetOverlayCrop = true;

                } else {
                    // Reset zoomCropInfo variables. This will ensure that
                    // stale values wont be used for postview
                    zoomCropInfo.w = crop->in1_w;
                    zoomCropInfo.h = crop->in1_h;
                    /* This reset is required, if not, overlay driver continues
                     * to use the old crop information for these preview
                     * frames which is not the correct behavior. To avoid
                     * multiple calls, reset once.
                     */
                    if(mResetOverlayCrop == true){
                        mOverlay->setCrop(0, 0,previewWidth, previewHeight);
                        mResetOverlayCrop = false;
                    }
                }
                mOverlay->queueBuffer((void *)offset_addr);
                /* To overcome a timing case where we could be having the overlay refer to deallocated
                   mDisplayHeap(and showing corruption), the mDisplayHeap is not deallocated untill the
                   first preview frame is queued to the overlay in 8660. Also adding the condition
                   to check if snapshot is currently in progress ensures that the resources being
                   used by the snapshot thread are not incorrectly deallocated by preview thread*/
                if ((mCurrentTarget == TARGET_MSM8660)&&(mFirstFrame == true)&&(!mSnapshotThreadRunning)) {
                    LOGD(" receivePreviewFrame : first frame queued, display heap being deallocated");
                    mLastPreviewFrameHeap.clear();
                    if(!mZslEnable){
                        mDisplayHeap.clear();
                        mPostviewHeap.clear();
                    }
                    mFirstFrame = false;
                }
                mLastQueuedFrame = (void *)frame->buffer;
            }
            mOverlayLock.unlock();
        } else {
            if (crop->in1_w != 0 && crop->in1_h != 0) {
                dstOffset = (dstOffset + 1) % NUM_MORE_BUFS;
                offset = kPreviewBufferCount + dstOffset;
                ssize_t dstOffset_addr = offset * mPreviewHeap->mAlignedBufferSize;
                if( !native_zoom_image(mPreviewHeap->mHeap->getHeapID(),
                    offset_addr, dstOffset_addr, crop,previewWidth,previewHeight)) {
                    LOGE(" Error while doing MDP zoom ");
                    offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
                }
            }
            if (mCurrentTarget == TARGET_MSM7627  ||
               (mCurrentTarget == TARGET_MSM7625A ||
                mCurrentTarget == TARGET_MSM7627A)) {
                mLastQueuedFrame = (void *)mPreviewHeap->mBuffers[offset]->pointer();
            }
        }
        if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME))
        {
           const char *str = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
           if(str != NULL)
           {
               hfr_count++;
               if(!strcmp(str, CameraParameters::VIDEO_HFR_OFF)) {
                   pcb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap->mBuffers[offset],
                    pdata);
               } else if (!strcmp(str, CameraParameters::VIDEO_HFR_2X)) {
                 hfr_count %= 2;
               } else if (!strcmp(str, CameraParameters::VIDEO_HFR_3X)) {
                 hfr_count %= 3;
               } else if (!strcmp(str, CameraParameters::VIDEO_HFR_4X)) {
                 hfr_count %= 4;
               }
               if(hfr_count == 0)
                   pcb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap->mBuffers[offset],
                    pdata);
           } else
               pcb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap->mBuffers[offset],
               pdata);
        }

        // If output  is NOT enabled (targets otherthan 7x30 , 8x50 and 8x60 currently..)
        if( (mCurrentTarget != TARGET_MSM7630 ) &&  (mCurrentTarget != TARGET_QSD8250) && (mCurrentTarget != TARGET_MSM8660)) {
            if(rcb != NULL && (msgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
                rcb(systemTime(), CAMERA_MSG_VIDEO_FRAME, mPreviewHeap->mBuffers[offset], rdata);
                Mutex::Autolock rLock(&mRecordFrameLock);
                if (mReleasedRecordingFrame != true) {
                    LOGV("block waiting for frame release");
                    mRecordWait.wait(mRecordFrameLock);
                    LOGV("frame released, continuing");
                }
                mReleasedRecordingFrame = false;
            }
        }

        if ( mCurrentTarget == TARGET_MSM8660 ) {
            mMetaDataWaitLock.lock();
            if (mFaceDetectOn == true && mSendMetaData == true) {
                mSendMetaData = false;
                fd_roi_t *fd = (fd_roi_t *)(frame->roi_info.info);
                int faces_detected = fd->rect_num;
                int max_faces_detected = MAX_ROI * 4;
                int array[max_faces_detected + 1];

                array[0] = faces_detected * 4;
                for (int i = 1, j = 0;j < MAX_ROI; j++, i = i + 4) {
                    if (j < faces_detected) {
                        array[i]   = fd->faces[j].x;
                        array[i+1] = fd->faces[j].y;
                        array[i+2] = fd->faces[j].dx;
                        array[i+3] = fd->faces[j].dx;
                    } else {
                        array[i]   = -1;
                        array[i+1] = -1;
                        array[i+2] = -1;
                        array[i+3] = -1;
                    }
                }
                if(mMetaDataHeap != NULL){
                    LOGV("runPreviewThread mMetaDataHEap is non-NULL");
                    memcpy((uint32_t *)mMetaDataHeap->mHeap->base(), (uint32_t *)array, (sizeof(int)*(MAX_ROI*4+1)));
                    mMetaDataWaitLock.unlock();

                    if  (mcb != NULL && (msgEnabled & CAMERA_MSG_META_DATA)) {
                        mcb(CAMERA_MSG_META_DATA, mMetaDataHeap->mBuffers[0], mdata);
                    }
                } else {
                    mMetaDataWaitLock.unlock();
                    LOGE("runPreviewThread mMetaDataHeap is NULL");
                }
            } else {
                mMetaDataWaitLock.unlock();
            }
        }
        LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);
    }
    mPreviewThreadWaitLock.lock();
    mPreviewThreadRunning = false;
    mPreviewThreadWait.signal();
    mPreviewThreadWaitLock.unlock();
}

void *preview_thread(void *user)
{
    LOGI("preview_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runPreviewThread(user);
    }
    else LOGE("not starting preview thread: the object went away!");
    LOGI("preview_thread X");
    return NULL;
}

void *hfr_thread(void *user)
{
    LOGI("hfr_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runHFRThread(user);
    }
    else LOGE("not starting hfr thread: the object went away!");
    LOGI("hfr_thread X");
    return NULL;
}

void QualcommCameraHardware::runHFRThread(void *data)
{
    LOGD("runHFRThread E");
    mInHFRThread = true;
    CAMERA_HAL_UNUSED(data);
    LOGI("%s: stopping Preview", __FUNCTION__);
    stopPreviewInternal();
    LOGI("%s: setting parameters", __FUNCTION__);
    setParameters(mParameters);
    LOGI("%s: starting Preview", __FUNCTION__);
    startPreviewInternal();
    mHFRMode = false;
    mInHFRThread = false;
}

void QualcommCameraHardware::runVideoThread(void *data)
{
    LOGD("runVideoThread E");
    msm_frame* vframe = NULL;
    CAMERA_HAL_UNUSED(data);

    while(true) {
        pthread_mutex_lock(&(g_busy_frame_queue.mut));

        // Exit the thread , in case of stop recording..
        mVideoThreadWaitLock.lock();
        if(mVideoThreadExit){
            LOGV("Exiting video thread..");
            mVideoThreadWaitLock.unlock();
            pthread_mutex_unlock(&(g_busy_frame_queue.mut));
            break;
        }
        mVideoThreadWaitLock.unlock();

        LOGV("in video_thread : wait for video frame ");
        // check if any frames are available in busyQ and give callback to
        // services/video encoder
        cam_frame_wait_video();
        LOGV("video_thread, wait over..");

        // Exit the thread , in case of stop recording..
        mVideoThreadWaitLock.lock();
        if(mVideoThreadExit){
            LOGV("Exiting video thread..");
            mVideoThreadWaitLock.unlock();
            pthread_mutex_unlock(&(g_busy_frame_queue.mut));
            break;
        }
        mVideoThreadWaitLock.unlock();

        // Get the video frame to be encoded
        vframe = cam_frame_get_video ();
        pthread_mutex_unlock(&(g_busy_frame_queue.mut));
        LOGV("in video_thread : got video frame ");

        if (UNLIKELY(mDebugFps)) {
            debugShowVideoFPS();
        }

        if(vframe != NULL) {
            // Find the offset within the heap of the current buffer.
            LOGV("Got video frame :  buffer %d base %d ", vframe->buffer, mRecordHeap->mHeap->base());
            ssize_t offset =
                (ssize_t)vframe->buffer - (ssize_t)mRecordHeap->mHeap->base();
            LOGV("offset = %d , alignsize = %d , offset later = %d", offset, mRecordHeap->mAlignedBufferSize, (offset / mRecordHeap->mAlignedBufferSize));

            offset /= mRecordHeap->mAlignedBufferSize;

            //set the track flag to true for this video buffer
            record_buffers_tracking_flag[offset] = true;

            /* Extract the timestamp of this frame */
            nsecs_t timeStamp = nsecs_t(vframe->ts.tv_sec)*1000000000LL + vframe->ts.tv_nsec;

            // dump frames for test purpose
#ifdef DUMP_VIDEO_FRAMES
            static int frameCnt = 0;
            if (frameCnt >= 11 && frameCnt <= 13 ) {
                char buf[128];
                sprintf(buf, "/data/%d_v.yuv", frameCnt);
                int file_fd = open(buf, O_RDWR | O_CREAT, 0777);
                LOGV("dumping video frame %d", frameCnt);
                if (file_fd < 0) {
                    LOGE("cannot open file\n");
                }
                else
                {
                    write(file_fd, (const void *)vframe->buffer,
                        vframe->cbcr_off * 3 / 2);
                }
                close(file_fd);
          }
          frameCnt++;
#endif
          if(mIs3DModeOn && mUseOverlay && (mOverlay != NULL)) {
              mOverlayLock.lock();
              mOverlay->setFd(mRecordHeap->mHeap->getHeapID());
              /* VPE will be taking care of zoom, so no need to
               * use overlay's setCrop interface for zoom
               * functionality.
               */
              /* get the offset of current video buffer for rendering */
              ssize_t offset_addr = (ssize_t)vframe->buffer -
                                      (ssize_t)mRecordHeap->mHeap->base();
              mOverlay->queueBuffer((void *)offset_addr);
              /* To overcome a timing case where we could be having the overlay refer to deallocated
                 mDisplayHeap(and showing corruption), the mDisplayHeap is not deallocated untill the
                 first preview frame is queued to the overlay in 8660 */
              if ((mCurrentTarget == TARGET_MSM8660)&&(mFirstFrame == true)) {
                  LOGD(" receivePreviewFrame : first frame queued, display heap being deallocated");
                  mThumbnailHeap.clear();
                  mDisplayHeap.clear();
                  mFirstFrame = false;
                  mPostviewHeap.clear();
              }
              mLastQueuedFrame = (void *)vframe->buffer;
              mOverlayLock.unlock();
          }

            // Enable IF block to give frames to encoder , ELSE block for just simulation
#if 1
            LOGV("in video_thread : got video frame, before if check giving frame to services/encoder");
            mCallbackLock.lock();
            int msgEnabled = mMsgEnabled;
            data_callback_timestamp rcb = mDataCallbackTimestamp;
            void *rdata = mCallbackCookie;
            mCallbackLock.unlock();

            /* When 3D mode is ON, the video thread will be ON even in preview
             * mode. We need to distinguish when recording is started. So, when
             * 3D mode is ON, check for the recordingState (which will be set
             * with start recording and reset in stop recording), before
             * calling rcb.
             */
            if(!mIs3DModeOn) {
                if(rcb != NULL && (msgEnabled & CAMERA_MSG_VIDEO_FRAME) ) {
                    LOGV("in video_thread : got video frame, giving frame to services/encoder");
                    rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[offset], rdata);
                }
            } else {
                mCallbackLock.lock();
                msgEnabled = mMsgEnabled;
                data_callback pcb = mDataCallback;
                void *pdata = mCallbackCookie;
                mCallbackLock.unlock();
                if (pcb != NULL) {
                    LOGE("pcb is not null");
                    static int count = 0;
                    //if(msgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
                    if (!count) {
                        LOGE("Giving first frame to app");
                        pcb(CAMERA_MSG_PREVIEW_FRAME, mRecordHeap->mBuffers[offset],
                                pdata);
                        count++;
                    }
                }
                if(recordingState == 1) {
                    if(rcb != NULL && (msgEnabled & CAMERA_MSG_VIDEO_FRAME) ) {
                        LOGV("in video_thread 3D mode : got video frame, giving frame to services/encoder");
                        rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[offset], rdata);
                    }
                } else {
                    /* When in preview mode, put the video buffer back into
                     * free Q, for next availability.
                     */
                    LOGV("in video_thread 3D mode : got video frame, putting frame to Free Q");
                    record_buffers_tracking_flag[offset] = false;
                    LINK_camframe_add_frame(CAM_VIDEO_FRAME,vframe);
                }
            }
#else
            // 720p output2  : simulate release frame here:
            LOGE("in video_thread simulation , releasing the video frame");
            LINK_camframe_add_frame(CAM_VIDEO_FRAME,vframe);
#endif

        } else LOGE("in video_thread get frame returned null");


    } // end of while loop

    mVideoThreadWaitLock.lock();
    mVideoThreadRunning = false;
    mVideoThreadWait.signal();
    mVideoThreadWaitLock.unlock();

    LOGV("runVideoThread X");
}

void *video_thread(void *user)
{
    LOGV("video_thread E");
    CAMERA_HAL_UNUSED(user);

    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runVideoThread(user);
    }
    else LOGE("not starting video thread: the object went away!");
    LOGV("video_thread X");
    return NULL;
}

void *frame_thread(void *user)
{
    LOGD("frame_thread E");
    CAMERA_HAL_UNUSED(user);
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runFrameThread(user);
    }
    else LOGW("not starting frame thread: the object went away!");
    LOGD("frame_thread X");
    return NULL;
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

bool QualcommCameraHardware::initPreview()
{
    const char * pmem_region;

    LOGV("initPreview E: preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight );

    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {
        mDimension.video_width = CEILING16(videoWidth);
        /* Backup the video dimensions, as video dimensions in mDimension
         * will be modified when DIS is supported. Need the actual values
         * to pass ap part of VPE config
         */
        videoWidth = mDimension.video_width;
        mDimension.video_height = videoHeight;
        LOGI("initPreview : preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight);
    }

    // See comments in deinitPreview() for why we have to wait for the frame
    // thread here, and why we can't use pthread_join().
    mFrameThreadWaitLock.lock();
    while (mFrameThreadRunning) {
        LOGI("initPreview: waiting for old frame thread to complete.");
        mFrameThreadWait.wait(mFrameThreadWaitLock);
        LOGI("initPreview: old frame thread completed.");
    }
    mFrameThreadWaitLock.unlock();

    mInSnapshotModeWaitLock.lock();
    while (mInSnapshotMode) {
        LOGI("initPreview: waiting for snapshot mode to complete.");
        mInSnapshotModeWait.wait(mInSnapshotModeWaitLock);
        LOGI("initPreview: snapshot mode completed.");
    }
    mInSnapshotModeWaitLock.unlock();

    pmem_region = "/dev/pmem_adsp";

    int cnt = 0;
    mPreviewFrameSize = previewWidth * previewHeight * 3/2;
    int CbCrOffset = PAD_TO_WORD(previewWidth * previewHeight);

    //Pass the yuv formats, display dimensions,
    //so that vfe will be initialized accordingly.
    mDimension.display_luma_width = previewWidth;
    mDimension.display_luma_height = previewHeight;
    mDimension.display_chroma_width = previewWidth;
    mDimension.display_chroma_height = previewHeight;
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        mPreviewFrameSize = PAD_TO_4K(CEILING32(previewWidth) * CEILING32(previewHeight)) +
                                     2 * (CEILING32(previewWidth/2) * CEILING32(previewHeight/2));
        CbCrOffset = PAD_TO_4K(CEILING32(previewWidth) * CEILING32(previewHeight));
        mDimension.prev_format = CAMERA_YUV_420_NV21_ADRENO;
        mDimension.display_luma_width = CEILING32(previewWidth);
        mDimension.display_luma_height = CEILING32(previewHeight);
        mDimension.display_chroma_width = 2 * CEILING32(previewWidth/2);
        //Chroma Height is not needed as of now. Just sending with other dimensions.
        mDimension.display_chroma_height = CEILING32(previewHeight/2);
    }
    LOGV("mDimension.prev_format = %d", mDimension.prev_format);
    LOGV("mDimension.display_luma_width = %d", mDimension.display_luma_width);
    LOGV("mDimension.display_luma_height = %d", mDimension.display_luma_height);
    LOGV("mDimension.display_chroma_width = %d", mDimension.display_chroma_width);
    LOGV("mDimension.display_chroma_height = %d", mDimension.display_chroma_height);

    dstOffset = 0;
  //Pass the original video width and height and get the required width
    //and height for record buffer allocation
    mDimension.orig_video_width = videoWidth;
    mDimension.orig_video_height = videoHeight;
    if(mZslEnable){
        //Limitation of ZSL  where the thumbnail and display dimensions should be the same
        mDimension.ui_thumbnail_width = mDimension.display_width;
        mDimension.ui_thumbnail_height = mDimension.display_height;
        mParameters.getPictureSize(&mPictureWidth, &mPictureHeight);
        if (updatePictureDimension(mParameters, mPictureWidth,
          mPictureHeight)) {
          mDimension.picture_width = mPictureWidth;
          mDimension.picture_height = mPictureHeight;
        }
    }
    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parms(CAMERA_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);
    if(mIs3DModeOn != true) {
      if(mInHFRThread == false)
      {
        mPreviewHeap = new PmemPool(pmem_region,
                                MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                MSM_PMEM_PREVIEW, //MSM_PMEM_OUTPUT2,
                                mPreviewFrameSize,
                                kPreviewBufferCountActual,
                                mPreviewFrameSize,
                                CbCrOffset,
                                0,
                                "preview");

        if (!mPreviewHeap->initialized()) {
          mPreviewHeap.clear();
          LOGE("initPreview X: could not initialize Camera preview heap.");
          return false;
        }
      }
      else
      {
          for (int cnt = 0; cnt < kPreviewBufferCountActual; ++cnt) {
              bool status;
              int active = (cnt < ACTIVE_PREVIEW_BUFFERS);
              status = register_buf(mPreviewFrameSize,
                       mPreviewFrameSize,
                       CbCrOffset,
                       0,
                       mPreviewHeap->mHeap->getHeapID(),
                       mPreviewHeap->mAlignedBufferSize * cnt,
                       (uint8_t *)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt,
                       MSM_PMEM_PREVIEW,
                       active,
                       true);
              if(status == false){
                  LOGE("Registring Preview Buffers failed for HFR mode");
                  return false;
              }
          }
      }
      //set DIS value to get the updated video width and height to calculate
      //the required record buffer size
      if(mVpeEnabled) {
        bool status = setDIS();
        if(status) {
          LOGE("Failed to set DIS");
          return false;
        }
      }
    }

    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {

        // Allocate video buffers after allocating preview buffers.
        bool status = initRecord();
        if(status != true) {
            LOGE("Failed to allocate video bufers");
            return false;
        }
    }

    if (ret) {
        if(mIs3DModeOn != true) {
            for (cnt = 0; cnt < kPreviewBufferCount; cnt++) {
                frames[cnt].fd = mPreviewHeap->mHeap->getHeapID();
                frames[cnt].buffer =
                    (uint32_t)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt;
                frames[cnt].y_off = 0;
                frames[cnt].cbcr_off = CbCrOffset;
                frames[cnt].path = OUTPUT_TYPE_P; // MSM_FRAME_ENC;
            }

            mPreviewBusyQueue.init();
            LINK_camframe_release_all_frames(CAM_PREVIEW_FRAME);
            for(int i=ACTIVE_PREVIEW_BUFFERS ;i <kPreviewBufferCount; i++)
                LINK_camframe_add_frame(CAM_PREVIEW_FRAME,&frames[i]);

            mPreviewThreadWaitLock.lock();
            pthread_attr_t pattr;
            pthread_attr_init(&pattr);
            pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);

            mPreviewThreadRunning = !pthread_create(&mPreviewThread,
                                      &pattr,
                                      preview_thread,
                                      (void*)NULL);
            ret = mPreviewThreadRunning;
            mPreviewThreadWaitLock.unlock();

            if(ret == false)
                return ret;
        }


        mFrameThreadWaitLock.lock();
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        camframeParams.cammode = CAMERA_MODE_2D;

        if (mIs3DModeOn) {
            camframeParams.cammode = CAMERA_MODE_3D;
        } else {
            camframeParams.cammode = CAMERA_MODE_2D;
        }

        mFrameThreadRunning = !pthread_create(&mFrameThread,
                                              &attr,
                                              frame_thread,
                                              &camframeParams);
        ret = mFrameThreadRunning;
        mFrameThreadWaitLock.unlock();
    }
    mFirstFrame = true;

    LOGV("initPreview X: %d", ret);
    return ret;
}

void QualcommCameraHardware::deinitPreview(void)
{
    LOGI("deinitPreview E");

    mPreviewBusyQueue.deinit();

    // When we call deinitPreview(), we signal to the frame thread that it
    // needs to exit, but we DO NOT WAIT for it to complete here.  The problem
    // is that deinitPreview is sometimes called from the frame-thread's
    // callback, when the refcount on the Camera client reaches zero.  If we
    // called pthread_join(), we would deadlock.  So, we just call
    // LINK_camframe_terminate() in deinitPreview(), which makes sure that
    // after the preview callback returns, the camframe thread will exit.  We
    // could call pthread_join() in initPreview() to join the last frame
    // thread.  However, we would also have to call pthread_join() in release
    // as well, shortly before we destroy the object; this would cause the same
    // deadlock, since release(), like deinitPreview(), may also be called from
    // the frame-thread's callback.  This we have to make the frame thread
    // detached, and use a separate mechanism to wait for it to complete.

    LINK_camframe_terminate();
    LOGI("deinitPreview X");
}

bool QualcommCameraHardware::initRawSnapshot()
{
    LOGV("initRawSnapshot E");
    const char * pmem_region;

    //get width and height from Dimension Object
    bool ret = native_set_parms(CAMERA_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);


    if(!ret){
        LOGE("initRawSnapshot X: failed to set dimension");
        return false;
    }
    int rawSnapshotSize = mDimension.raw_picture_height *
                           mDimension.raw_picture_width;

    LOGV("raw_snapshot_buffer_size = %d, raw_picture_height = %d, "\
         "raw_picture_width = %d",
          rawSnapshotSize, mDimension.raw_picture_height,
          mDimension.raw_picture_width);

    if (mRawSnapShotPmemHeap != NULL) {
        LOGV("initRawSnapshot: clearing old mRawSnapShotPmemHeap.");
        mRawSnapShotPmemHeap.clear();
    }
    if(mCurrentTarget == TARGET_MSM8660)
       pmem_region = "/dev/pmem_smipool";
    else
       pmem_region = "/dev/pmem_adsp";

    //Pmem based pool for Camera Driver
    mRawSnapShotPmemHeap = new PmemPool(pmem_region,
                                    MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                    MSM_PMEM_RAW_MAINIMG,
                                    rawSnapshotSize,
                                    1,
                                    rawSnapshotSize,
                                    0,
                                    0,
                                    "raw pmem snapshot camera");

    if (!mRawSnapShotPmemHeap->initialized()) {
        mRawSnapShotPmemHeap.clear();
        LOGE("initRawSnapshot X: error initializing mRawSnapshotHeap");
        return false;
    }

    mRawCaptureParms.num_captures = numCapture;
    mRawCaptureParms.raw_picture_width = mDimension.raw_picture_width;
    mRawCaptureParms.raw_picture_height = mDimension.raw_picture_height;

    LOGV("initRawSnapshot X");
    return true;

}
bool QualcommCameraHardware::initZslBuffers(bool initJpegHeap){
    LOGE("Init ZSL buffers E");
    const char * pmem_region;
    int postViewBufferSize;

    mPostviewWidth = mDimension.display_width;
    mPostviewHeight =  mDimension.display_height;

    //postview buffer initialization
    postViewBufferSize  = mPostviewWidth * mPostviewHeight * 3 / 2;
    int CbCrOffsetPostview = PAD_TO_WORD(mPostviewWidth * mPostviewHeight);
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        postViewBufferSize  = PAD_TO_4K(CEILING32(mPostviewWidth) * CEILING32(mPostviewHeight)) +
                                  2 * (CEILING32(mPostviewWidth/2) * CEILING32(mPostviewHeight/2));
        int CbCrOffsetPostview = PAD_TO_4K(CEILING32(mPostviewWidth) * CEILING32(mPostviewHeight));
    }

    //Snapshot buffer initialization
    mRawSize = mPictureWidth * mPictureHeight * 3 / 2;
    mCbCrOffsetRaw = PAD_TO_WORD(mPictureWidth * mPictureHeight);
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        mRawSize = PAD_TO_4K(CEILING32(mPictureWidth) * CEILING32(mPictureHeight)) +
                            2 * (CEILING32(mPictureWidth/2) * CEILING32(mPictureHeight/2));
        mCbCrOffsetRaw = PAD_TO_4K(CEILING32(mPictureWidth) * CEILING32(mPictureHeight));
    }

    //Jpeg buffer initialization
    if( mCurrentTarget == TARGET_MSM7627 ||
       (mCurrentTarget == TARGET_MSM7625A ||
        mCurrentTarget == TARGET_MSM7627A))
        mJpegMaxSize = CEILING16(mPictureWidth) * CEILING16(mPictureHeight) * 3 / 2;
    else {
        mJpegMaxSize = mPictureWidth * mPictureHeight * 3 / 2;
        if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
            mJpegMaxSize =
               PAD_TO_4K(CEILING32(mPictureWidth) * CEILING32(mPictureHeight)) +
                    2 * (CEILING32(mPictureWidth/2) * CEILING32(mPictureHeight/2));
        }
    }

    cam_buf_info_t buf_info;
    int yOffset = 0;
    buf_info.resolution.width = mPictureWidth;
    buf_info.resolution.height = mPictureHeight;
    if(mPreviewFormat != CAMERA_YUV_420_NV21_ADRENO) {
        mCfgControl.mm_camera_get_parm(CAMERA_PARM_BUFFER_INFO, (void *)&buf_info);
        mRawSize = buf_info.size;
        mJpegMaxSize = mRawSize;
        mCbCrOffsetRaw = buf_info.cbcr_offset;
        yOffset = buf_info.yoffset;
    }

    LOGV("initZslBuffer: initializing mRawHeap.");
    if(mCurrentTarget == TARGET_MSM8660)
       pmem_region = "/dev/pmem_smipool";
    else
       pmem_region = "/dev/pmem_adsp";
    //Main Raw Image
    mRawHeap =
        new PmemPool(pmem_region,
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_MAINIMG,
                     mJpegMaxSize,
                     MAX_SNAPSHOT_BUFFERS,
                     mRawSize,
                     mCbCrOffsetRaw,
                     yOffset,
                     "snapshot camera");

    if (!mRawHeap->initialized()) {
       LOGE("initZslBuffer X failed ");
       mRawHeap.clear();
       LOGE("initRaw X: error initializing mRawHeap");
       return false;
    }


    // Jpeg
    if (initJpegHeap) {
        LOGV("initZslRaw: initializing mJpegHeap.");
        mJpegHeap =
            new AshmemPool(mJpegMaxSize,
                           (MAX_SNAPSHOT_BUFFERS - 2),  // It is the max number of snapshot supported.
                           0, // we do not know how big the picture will be
                           "jpeg");

        if (!mJpegHeap->initialized()) {
            mJpegHeap.clear();
            mRawHeap.clear();
            LOGE("initZslRaw X failed: error initializing mJpegHeap.");
            return false;
        }
    }

    //PostView
    pmem_region = "/dev/pmem_adsp";
    mPostviewHeap =
            new PmemPool(pmem_region,
                         MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                         MSM_PMEM_THUMBNAIL,
                         postViewBufferSize,
                         MAX_SNAPSHOT_BUFFERS,
                         postViewBufferSize,
                         CbCrOffsetPostview,
                         0,
                         "thumbnail");


    if (!mPostviewHeap->initialized()) {
        mPostviewHeap.clear();
        mJpegHeap.clear();
        mRawHeap.clear();
        LOGE("initZslBuffer X failed: error initializing mPostviewHeap.");
        return false;
    }

    /* frame all the exif and encode information into encode_params_t */
    initImageEncodeParameters(MAX_SNAPSHOT_BUFFERS);

    LOGV("initZslRaw X");
    return true;
}

bool QualcommCameraHardware::deinitZslBuffers()
{   LOGE("deinitZslBuffers E");
    if(mZslEnable) {

         if (mJpegHeap != NULL) {
            LOGV("initRaw: clearing old mJpegHeap.");
            mJpegHeap.clear();
          }
        if (mRawHeap != NULL) {
            LOGV("initRaw: clearing old mRawHeap.");
            mRawHeap.clear();
        }
        if (mPostviewHeap != NULL) {
            LOGV("initRaw: clearing old mPostviewHeap.");
            mPostviewHeap.clear();
        }
        if (mDisplayHeap != NULL) {
            LOGV("deinitZslBuffers: clearing old mDisplayHeap.");
            mDisplayHeap.clear();
        }
       if (mLastPreviewFrameHeap != NULL) {
            LOGV("deinitZslBuffers: clearing old mLastPreviewFrameHeap.");
            mLastPreviewFrameHeap.clear();
        }
    }
    LOGE("deinitZslBuffers X");
    return true;
}
bool QualcommCameraHardware::initRaw(bool initJpegHeap)
{
    const char * pmem_region;
    int postViewBufferSize;
    uint32_t pictureAspectRatio;
    uint32_t i;

    mParameters.getPictureSize(&mPictureWidth, &mPictureHeight);
    if (updatePictureDimension(mParameters, mPictureWidth, mPictureHeight)) {
        mDimension.picture_width = mPictureWidth;
        mDimension.picture_height = mPictureHeight;
    }
    LOGV("initRaw E: picture size=%dx%d", mPictureWidth, mPictureHeight);
    int w_scale_factor = (mIs3DModeOn && mSnapshot3DFormat == SIDE_BY_SIDE_FULL) ? 2 : 1;

    /* use the default thumbnail sizes */
    mThumbnailHeight = thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;
    mThumbnailWidth = (mThumbnailHeight * mPictureWidth)/ mPictureHeight;
    /* see if we can get better thumbnail sizes (not mandatory?) */
    pictureAspectRatio = (uint32_t)((mPictureWidth * Q12) / mPictureHeight);
    for(i = 0; i < THUMBNAIL_SIZE_COUNT; i++ ){
        if(thumbnail_sizes[i].aspect_ratio == pictureAspectRatio)
        {
            mThumbnailWidth = thumbnail_sizes[i].width;
            mThumbnailHeight = thumbnail_sizes[i].height;
            break;
        }
    }

    /* calculate postView size */
    mPostviewWidth = mThumbnailWidth;
    mPostviewHeight = mThumbnailHeight;
    /* Try to keep the postview dimensions near to preview for better
     * performance and userexperience. If the postview and preview dimensions
     * are same, then we can try to use the same overlay of preview for
     * postview also. If not, we need to reset the overlay for postview.
     * we will be getting the same dimensions for preview and postview
     * in most of the cases. The only exception is for applications
     * which won't use optimalPreviewSize based on picture size.
    */
    if((mPictureHeight >= previewHeight) &&
       (mCurrentTarget != TARGET_MSM7627 &&
        mCurrentTarget != TARGET_MSM7627A &&
        mCurrentTarget != TARGET_MSM7625A) && !mIs3DModeOn) {
        mPostviewHeight = previewHeight;
        mPostviewWidth = (previewHeight * mPictureWidth) / mPictureHeight;
    }else if(mPictureHeight < mThumbnailHeight){
        mPostviewHeight = THUMBNAIL_SMALL_HEIGHT;
        mPostviewWidth = (THUMBNAIL_SMALL_HEIGHT * mPictureWidth)/ mPictureHeight;
        mThumbnailWidth = mPostviewWidth;
        mThumbnailHeight = mPostviewHeight;
    }

    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
        mDimension.main_img_format = CAMERA_YUV_420_NV21_ADRENO;
        mDimension.thumb_format = CAMERA_YUV_420_NV21_ADRENO;
    }

    mDimension.ui_thumbnail_width = mPostviewWidth;
    mDimension.ui_thumbnail_height = mPostviewHeight;

    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parms(CAMERA_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);

    if(!ret) {
        LOGE("initRaw X: failed to set dimension");
        return false;
    }

    if (mJpegHeap != NULL) {
        LOGV("initRaw: clearing old mJpegHeap.");
        mJpegHeap.clear();
    }

    //postview buffer initialization
    postViewBufferSize  = mPostviewWidth * w_scale_factor * mPostviewHeight * 3 / 2;
    int CbCrOffsetPostview = PAD_TO_WORD(mPostviewWidth * w_scale_factor * mPostviewHeight);

    //Snapshot buffer initialization
    mRawSize = mPictureWidth * w_scale_factor * mPictureHeight * 3 / 2;
    mCbCrOffsetRaw = PAD_TO_WORD(mPictureWidth * w_scale_factor * mPictureHeight);
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        mRawSize = PAD_TO_4K(CEILING32(mPictureWidth * w_scale_factor) * CEILING32(mPictureHeight)) +
                            2 * (CEILING32(mPictureWidth * w_scale_factor/2) * CEILING32(mPictureHeight/2));
        mCbCrOffsetRaw = PAD_TO_4K(CEILING32(mPictureWidth * w_scale_factor) * CEILING32(mPictureHeight));
    }

    //Jpeg buffer initialization
    if( mCurrentTarget == TARGET_MSM7627 ||
       (mCurrentTarget == TARGET_MSM7625A ||
        mCurrentTarget == TARGET_MSM7627A))
        mJpegMaxSize = CEILING16(mPictureWidth * w_scale_factor) * CEILING16(mPictureHeight) * 3 / 2;
    else {
        mJpegMaxSize = mPictureWidth * w_scale_factor * mPictureHeight * 3 / 2;
        if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
            mJpegMaxSize =
               PAD_TO_4K(CEILING32(mPictureWidth * w_scale_factor) * CEILING32(mPictureHeight)) +
                    2 * (CEILING32(mPictureWidth * w_scale_factor/2) * CEILING32(mPictureHeight/2));
        }
    }

    int rotation = mParameters.getInt("rotation");
    if (mIs3DModeOn)
        rotation = 0;
    ret = native_set_parms(CAMERA_PARM_JPEG_ROTATION, sizeof(int), &rotation);
    if(!ret){
        LOGE("setting camera id failed");
        return false;
    }
    cam_buf_info_t buf_info;
    int yOffset = 0;
    if(mIs3DModeOn == false)
    {
        buf_info.resolution.width = mPictureWidth * w_scale_factor;
        buf_info.resolution.height = mPictureHeight;
        mCfgControl.mm_camera_get_parm(CAMERA_PARM_BUFFER_INFO, (void *)&buf_info);
        mRawSize = buf_info.size;
        mJpegMaxSize = mRawSize;
        mCbCrOffsetRaw = buf_info.cbcr_offset;
        yOffset = buf_info.yoffset;
    }

    LOGE("rawsize = %d cbcr offset =%d", mRawSize, mCbCrOffsetRaw);

    LOGV("initRaw: initializing mRawHeap.");
    if(mCurrentTarget == TARGET_MSM8660)
       pmem_region = "/dev/pmem_smipool";
    else
       pmem_region = "/dev/pmem_adsp";
    //Main Raw Image
    mRawHeap =
        new PmemPool(pmem_region,
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_MAINIMG,
                     mJpegMaxSize,
                     kRawBufferCount,
                     mRawSize,
                     mCbCrOffsetRaw,
                     yOffset,
                     "snapshot camera");

    if (!mRawHeap->initialized()) {
       LOGE("initRaw X failed ");
       mRawHeap.clear();
       LOGE("initRaw X: error initializing mRawHeap");
       return false;
    }

    //This is kind of workaround for the GPU limitation, as it can't
    //output in line to correct NV21 adreno formula for some snapshot
    //sizes (like 3264x2448). This change of cbcr offset will ensure that
    //chroma plane always starts at the beginning of a row.
    if(mPreviewFormat != CAMERA_YUV_420_NV21_ADRENO)
        mCbCrOffsetRaw = CEILING32(mPictureWidth * w_scale_factor) * CEILING32(mPictureHeight);

    // Jpeg
    if (initJpegHeap) {
        LOGV("initRaw: initializing mJpegHeap.");
        mJpegHeap =
            new AshmemPool(mJpegMaxSize,
                           kJpegBufferCount,
                           0, // we do not know how big the picture will be
                           "jpeg");

        if (!mJpegHeap->initialized()) {
            mJpegHeap.clear();
            mRawHeap.clear();
            LOGE("initRaw X failed: error initializing mJpegHeap.");
            return false;
        }
    }

    //PostView
    pmem_region = "/dev/pmem_adsp";

    mPostviewHeap =
            new PmemPool(pmem_region,
                         MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                         MSM_PMEM_THUMBNAIL,
                         postViewBufferSize,
                         1,
                         postViewBufferSize,
                         CbCrOffsetPostview,
                         0,
                         "thumbnail");

    if (!mPostviewHeap->initialized()) {
        mPostviewHeap.clear();
        mJpegHeap.clear();
        mRawHeap.clear();
        LOGE("initRaw X failed: error initializing mPostviewHeap.");
        return false;
    }

    /* frame all the exif and encode information into encode_params_t */

    initImageEncodeParameters(1);
    /* fill main image size, thumbnail size, postview size into capture_params_t*/
    memset(&mImageCaptureParms, 0, sizeof(capture_params_t));
    mImageCaptureParms.num_captures = 1;
    mImageCaptureParms.picture_width = mPictureWidth;
    mImageCaptureParms.picture_height = mPictureHeight;
    mImageCaptureParms.postview_width = mPostviewWidth;
    mImageCaptureParms.postview_height = mPostviewHeight;

    int width = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int height = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    if((width != 0) && (height != 0)) {
        mImageCaptureParms.thumbnail_width = mThumbnailWidth;
        mImageCaptureParms.thumbnail_height = mThumbnailHeight;
    } else {
        mImageCaptureParms.thumbnail_width = 0;
        mImageCaptureParms.thumbnail_height = 0;
    }

    LOGI("%s: picture size=%dx%d",__FUNCTION__,
        mImageCaptureParms.picture_width, mImageCaptureParms.picture_height);
    LOGI("%s: postview size=%dx%d",__FUNCTION__,
        mImageCaptureParms.postview_width, mImageCaptureParms.postview_height);
    LOGI("%s: thumbnail size=%dx%d",__FUNCTION__,
        mImageCaptureParms.thumbnail_width, mImageCaptureParms.thumbnail_height);

    LOGV("initRaw X");
    return true;
}


void QualcommCameraHardware::deinitRawSnapshot()
{
    LOGV("deinitRawSnapshot E");
    mRawSnapShotPmemHeap.clear();
    LOGV("deinitRawSnapshot X");
}

void QualcommCameraHardware::deinitRaw()
{
    LOGV("deinitRaw E");

    mJpegHeap.clear();
    mRawHeap.clear();
    if(mCurrentTarget != TARGET_MSM8660){
       mPostviewHeap.clear();
       mDisplayHeap.clear();
    }

    LOGV("deinitRaw X");
}

void QualcommCameraHardware::release()
{
    LOGI("release E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock checkLock(&singleton_lock);
        if(singleton_releasing){
            LOGE("ERROR: multiple release!");
            return;
        }
    }
    LOGI("release: mCameraRunning = %d", mCameraRunning);
    if (mCameraRunning) {
        if(mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
            mRecordFrameLock.lock();
            mReleasedRecordingFrame = true;
            mRecordWait.signal();
            mRecordFrameLock.unlock();
        }
        stopPreviewInternal();
        LOGI("release: stopPreviewInternal done.");
    }

    mm_camera_ops_type_t current_ops_type = (mSnapshotFormat
            == PICTURE_FORMAT_JPEG) ? CAMERA_OPS_CAPTURE_AND_ENCODE
            : CAMERA_OPS_RAW_CAPTURE;
    mCamOps.mm_camera_deinit(current_ops_type, NULL, NULL);

    //Signal the snapshot thread
    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = false;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();

    // Wait for snapshot thread to complete before clearing the
    // resources.
    mSnapshotThreadWaitLock.lock();
    while (mSnapshotThreadRunning) {
        LOGV("release: waiting for old snapshot thread to complete.");
        mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
        LOGV("release: old snapshot thread completed.");
    }
    mSnapshotThreadWaitLock.unlock();

    {
        Mutex::Autolock l (&mRawPictureHeapLock);
        deinitRaw();
    }

    deinitRawSnapshot();
    LOGI("release: clearing resources done.");
    if(mCurrentTarget == TARGET_MSM8660) {
       LOGV("release : Clearing the mThumbnailHeap and mDisplayHeap");
       mLastPreviewFrameHeap.clear();
       mLastPreviewFrameHeap = NULL;
       mThumbnailHeap.clear();
       mThumbnailHeap = NULL;
       mPostviewHeap.clear();
       mPostviewHeap = NULL;
       mDisplayHeap.clear();
       mDisplayHeap = NULL;
    }
    LINK_mm_camera_deinit();
    if(fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
    singleton_lock.lock();
    singleton_releasing = true;
    singleton_releasing_start_time = systemTime();
    singleton_lock.unlock();

    LOGI("release X: mCameraRunning = %d, mFrameThreadRunning = %d", mCameraRunning, mFrameThreadRunning);
    LOGI("mVideoThreadRunning = %d, mSnapshotThreadRunning = %d, mJpegThreadRunning = %d", mVideoThreadRunning, mSnapshotThreadRunning, mJpegThreadRunning);
    LOGI("camframe_timeout_flag = %d, mAutoFocusThreadRunning = %d", camframe_timeout_flag, mAutoFocusThreadRunning);
}

QualcommCameraHardware::~QualcommCameraHardware()
{
    LOGI("~QualcommCameraHardware E");
    libmmcamera = NULL;
    mMMCameraDLRef.clear();

    singleton_lock.lock();
    if( mCurrentTarget == TARGET_MSM7630 || mCurrentTarget == TARGET_QSD8250 || mCurrentTarget == TARGET_MSM8660 ) {
        delete [] recordframes;
        recordframes = NULL;
        delete [] record_buffers_tracking_flag;
        record_buffers_tracking_flag = NULL;
    }
    singleton.clear();
    singleton_releasing = false;
    singleton_releasing_start_time = 0;
    singleton_wait.signal();
    singleton_lock.unlock();
    LOGI("~QualcommCameraHardware X");
}

sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
{
    LOGV("getRawHeap");
    return mDisplayHeap != NULL ? mDisplayHeap->mHeap : NULL;
}

sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
{
    LOGV("getPreviewHeap");
    if(mIs3DModeOn != true)
        return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
    else
        return mRecordHeap != NULL ? mRecordHeap->mHeap : NULL;
}


status_t QualcommCameraHardware::startPreviewInternal()
{
   LOGV("in startPreviewInternal : E");
   mPreviewStopping = false;
   if(mZslEnable && !mZslPanorama){
       LOGE("start zsl Preview called");
       mCamOps.mm_camera_start(CAMERA_OPS_ZSL_STREAMING_CB,NULL, NULL);
       if (mCurrentTarget == TARGET_MSM8660) {
           if(mLastPreviewFrameHeap != NULL)
	       mLastPreviewFrameHeap.clear();
	}
    }
    if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }
    if(mZslEnable){
         //call init
         LOGI("ZSL Enable called");
         uint8_t is_zsl = 1;
          mm_camera_status_t status;
          if(MM_CAMERA_SUCCESS != mCfgControl.mm_camera_set_parm(CAMERA_PARM_ZSL_ENABLE,
                     (void *)&is_zsl)){
              LOGE("ZSL Enable failed");
          return UNKNOWN_ERROR;
          }
    }

    if (!mPreviewInitialized) {
        mLastQueuedFrame = NULL;
        mPreviewInitialized = initPreview();
        if (!mPreviewInitialized) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            mPreviewBusyQueue.deinit();
            return UNKNOWN_ERROR;
        }
    }

    /* For 3D mode, start the video output, as this need to be
     * used for display also.
     */
    if(mIs3DModeOn) {
        startRecordingInternal();
        if(!mVideoThreadRunning) {
            LOGE("startPreview X startRecording failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }
    }

    {
        Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
        if(( mCurrentTarget != TARGET_MSM7630 ) &&
                (mCurrentTarget != TARGET_QSD8250) && (mCurrentTarget != TARGET_MSM8660))
            mCameraRunning = native_start_ops(CAMERA_OPS_STREAMING_PREVIEW, NULL);
        else {
            if(!mZslEnable){
                LOGE("Calling CAMERA_OPS_STREAMING_VIDEO");
                mCameraRunning = native_start_ops(CAMERA_OPS_STREAMING_VIDEO, NULL);
	    }else {
                initZslParameter();
                 mCameraRunning = false;
                 if (MM_CAMERA_SUCCESS == mCamOps.mm_camera_init(CAMERA_OPS_STREAMING_ZSL,
                        (void *)&mZslParms, NULL)) {
                        //register buffers for ZSL
                        bool status = initZslBuffers(true);
                        if(status != true) {
                             LOGE("Failed to allocate ZSL buffers");
                             return false;
                        }
                        if(MM_CAMERA_SUCCESS == mCamOps.mm_camera_start(CAMERA_OPS_STREAMING_ZSL,NULL, NULL)){
                            mCameraRunning = true;
                        }
                }
                if(mCameraRunning == false)
                    LOGE("Starting  ZSL CAMERA_OPS_STREAMING_ZSL failed!!!");
            }
        }
    }

    if(!mCameraRunning) {
        deinitPreview();
        if(mZslEnable){
            //deinit
            LOGI("ZSL DISABLE called");
           uint8_t is_zsl = 0;
            mm_camera_status_t status;
            if( MM_CAMERA_SUCCESS != mCfgControl.mm_camera_set_parm(CAMERA_PARM_ZSL_ENABLE,
                     (void *)&is_zsl)){
                LOGE("ZSL_Disable failed!!");
                return UNKNOWN_ERROR;
            }
        }
        /* Flush the Busy Q */
        cam_frame_flush_video();
        /* Need to flush the free Qs as these are initalized in initPreview.*/
        LINK_camframe_release_all_frames(CAM_VIDEO_FRAME);
        LINK_camframe_release_all_frames(CAM_PREVIEW_FRAME);
        mPreviewInitialized = false;
        mOverlayLock.lock();
        mOverlay = NULL;
        mOverlayLock.unlock();
        LOGE("startPreview X: native_start_ops: CAMERA_OPS_STREAMING_PREVIEW ioctl failed!");
        return UNKNOWN_ERROR;
    }

    //Reset the Gps Information
    exif_table_numEntries = 0;

    LOGV("startPreviewInternal X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::startPreview()
{
    LOGV("startPreview E");
    Mutex::Autolock l(&mLock);
    return startPreviewInternal();
}

void QualcommCameraHardware::stopPreviewInternal()
{
    LOGI("stopPreviewInternal E: %d", mCameraRunning);
    mPreviewStopping = true;
    if (mCameraRunning) {
        /* For 3D mode, we need to exit the video thread.*/
        if(mIs3DModeOn) {
            recordingState = 0;
            mVideoThreadWaitLock.lock();
            LOGI("%s: 3D mode, exit video thread", __FUNCTION__);
            mVideoThreadExit = 1;
            mVideoThreadWaitLock.unlock();

            pthread_mutex_lock(&(g_busy_frame_queue.mut));
            pthread_cond_signal(&(g_busy_frame_queue.wait));
            pthread_mutex_unlock(&(g_busy_frame_queue.mut));
        }

        // Cancel auto focus.
        {
            if (mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
                cancelAutoFocusInternal();
            }
        }

        // make mSmoothzoomThreadExit true
        mSmoothzoomThreadLock.lock();
        mSmoothzoomThreadExit = true;
        mSmoothzoomThreadLock.unlock();
        // singal smooth zoom thread , so that it can exit gracefully
        mSmoothzoomThreadWaitLock.lock();
        if(mSmoothzoomThreadRunning)
            mSmoothzoomThreadWait.signal();

        mSmoothzoomThreadWaitLock.unlock();

        Mutex::Autolock l(&mCamframeTimeoutLock);
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(!camframe_timeout_flag) {
                if (( mCurrentTarget != TARGET_MSM7630 ) &&
                        (mCurrentTarget != TARGET_QSD8250) && (mCurrentTarget != TARGET_MSM8660))
                         mCameraRunning = !native_stop_ops(CAMERA_OPS_STREAMING_PREVIEW, NULL);
                else{
                    if(!mZslEnable){
                        mCameraRunning = !native_stop_ops(CAMERA_OPS_STREAMING_VIDEO, NULL);
                    }else {
                        mCameraRunning = true;
                        if(MM_CAMERA_SUCCESS == mCamOps.mm_camera_stop(CAMERA_OPS_STREAMING_ZSL,NULL, NULL)){
                            deinitZslBuffers();
                            if (MM_CAMERA_SUCCESS == mCamOps.mm_camera_deinit(CAMERA_OPS_STREAMING_ZSL,
                                    (void *)&mZslParms, NULL)) {
                                mCameraRunning = false;
                            }
                        }
                        if(mCameraRunning ==true)
                            LOGE("Starting  ZSL CAMERA_OPS_STREAMING_ZSL failed!!!");
                    }
                }
            } else {
                /* This means that the camframetimeout was issued.
                 * But we did not issue native_stop_preview(), so we
                 * need to update mCameraRunning to indicate that
                 * Camera is no longer running. */
                mCameraRunning = 0;
            }
        }
    }
    /* in 3D mode, wait for the video thread before clearing resources.*/
    if(mIs3DModeOn) {
        mVideoThreadWaitLock.lock();
        while (mVideoThreadRunning) {
            LOGI("%s: waiting for video thread to complete.", __FUNCTION__);
            mVideoThreadWait.wait(mVideoThreadWaitLock);
            LOGI("%s : video thread completed.", __FUNCTION__);
        }
        mVideoThreadWaitLock.unlock();
    }

    if (!mCameraRunning) {
        if(mPreviewInitialized) {
            deinitPreview();
            if( ( mCurrentTarget == TARGET_MSM7630 ) ||
                (mCurrentTarget == TARGET_QSD8250) ||
                (mCurrentTarget == TARGET_MSM8660)) {
                mVideoThreadWaitLock.lock();
                LOGV("in stopPreviewInternal: making mVideoThreadExit 1");
                mVideoThreadExit = 1;
                mVideoThreadWaitLock.unlock();
                //720p : signal the video thread , and check in video thread
                //if stop is called, if so exit video thread.
                pthread_mutex_lock(&(g_busy_frame_queue.mut));
                pthread_cond_signal(&(g_busy_frame_queue.wait));
                pthread_mutex_unlock(&(g_busy_frame_queue.mut));
                /* Flush the Busy Q */
                cam_frame_flush_video();
                /* Flush the Free Q */
                LINK_camframe_release_all_frames(CAM_VIDEO_FRAME);
            }
            mPreviewInitialized = false;
        }
    }
    else LOGI("stopPreviewInternal: Preview is stopped already");

    LOGI("stopPreviewInternal X: %d", mCameraRunning);
}

void QualcommCameraHardware::stopPreview()
{
    LOGV("stopPreview: E");
    Mutex::Autolock l(&mLock);
    {
        if (mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME))
            return;
    }
    stopPreviewInternal();
    LOGV("stopPreview: X");
}

void QualcommCameraHardware::runAutoFocus()
{
    bool status = true;
    void *libhandle = NULL;
    isp3a_af_mode_t afMode;

    mAutoFocusThreadLock.lock();
    // Skip autofocus if focus mode is infinity.
    const char * focusMode = mParameters.get(CameraParameters::KEY_FOCUS_MODE);
    if ((mParameters.get(CameraParameters::KEY_FOCUS_MODE) == 0)
           || (strcmp(focusMode, CameraParameters::FOCUS_MODE_INFINITY) == 0)
           || (strcmp(focusMode, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) == 0)) {
        goto done;
    }

    if(!libmmcamera){
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }

    afMode = (isp3a_af_mode_t)attr_lookup(focus_modes,
                                sizeof(focus_modes) / sizeof(str_map),
                                mParameters.get(CameraParameters::KEY_FOCUS_MODE));

    /* This will block until either AF completes or is cancelled. */
    LOGV("af start (mode %d)", afMode);
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(mCameraRunning){
                LOGV("Start AF");
                status =  native_start_ops(CAMERA_OPS_FOCUS ,(void *)&afMode);
            }else{
                LOGV("As Camera preview is not running, AF not issued");
                status = false;
            }
        }
        mAfLock.unlock();
    }
    else{
        //AF Cancel would have acquired the lock,
        //so, no need to perform any AF
        LOGV("As Cancel auto focus is in progress, auto focus request "
                "is ignored");
        status = FALSE;
    }

    {
        Mutex::Autolock pl(&mParametersLock);
        if(mHasAutoFocusSupport && (updateFocusDistances(focusMode) != NO_ERROR)) {
            LOGE("%s: updateFocusDistances failed for %s", __FUNCTION__, focusMode);
        }
    }

    LOGV("af done: %d", (int)status);

done:
    mAutoFocusThreadRunning = false;
    mAutoFocusThreadLock.unlock();

    mCallbackLock.lock();
    bool autoFocusEnabled = mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS);
    notify_callback cb = mNotifyCallback;
    void *data = mCallbackCookie;
    mCallbackLock.unlock();
    if (autoFocusEnabled)
        cb(CAMERA_MSG_FOCUS, status, 0, data);

}

status_t QualcommCameraHardware::cancelAutoFocusInternal()
{
    LOGV("cancelAutoFocusInternal E");
    bool afRunning = true;

    if(!mHasAutoFocusSupport){
        LOGV("cancelAutoFocusInternal X");
        return NO_ERROR;
    }

    status_t rc = NO_ERROR;
    status_t err;

    do {
      err = mAfLock.tryLock();
      if(err == NO_ERROR) {
          //Got Lock, means either AF hasn't started or
          // AF is done. So no need to cancel it, just change the state
          LOGV("Auto Focus is not in progress, Cancel Auto Focus is ignored");
          mAfLock.unlock();

          mAutoFocusThreadLock.lock();
          afRunning = mAutoFocusThreadRunning;
          mAutoFocusThreadLock.unlock();
          if(afRunning) {
            usleep( 5000 );
          }
      }
    } while ( err == NO_ERROR && afRunning );
    if(afRunning) {
        //AF is in Progess, So cancel it
        LOGV("Lock busy...cancel AF");
        rc = native_stop_ops(CAMERA_OPS_FOCUS, NULL) ?
          NO_ERROR : UNKNOWN_ERROR;

        /*now just wait for auto focus thread to be finished*/
        mAutoFocusThreadLock.lock();
        mAutoFocusThreadLock.unlock();
    }
    LOGV("cancelAutoFocusInternal X: %d", rc);
    return rc;
}

void *auto_focus_thread(void *user)
{
    LOGV("auto_focus_thread E");
    CAMERA_HAL_UNUSED(user);
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runAutoFocus();
    }
    else LOGW("not starting autofocus: the object went away!");
    LOGV("auto_focus_thread X");
    return NULL;
}

status_t QualcommCameraHardware::autoFocus()
{
    LOGV("autoFocus E");
    Mutex::Autolock l(&mLock);

    if(!mHasAutoFocusSupport){
       /*
        * If autofocus is not supported HAL defaults
        * focus mode to infinity and supported mode to
        * infinity also. In this mode and fixed mode app
        * should not call auto focus.
        */
        LOGE("Auto Focus not supported");
        LOGV("autoFocus X");
        return INVALID_OPERATION;
    }
    {
        mAutoFocusThreadLock.lock();
        if (!mAutoFocusThreadRunning) {

            // Create a detached thread here so that we don't have to wait
            // for it when we cancel AF.
            pthread_t thr;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            mAutoFocusThreadRunning =
                !pthread_create(&thr, &attr,
                                auto_focus_thread, NULL);
            if (!mAutoFocusThreadRunning) {
                LOGE("failed to start autofocus thread");
                mAutoFocusThreadLock.unlock();
                return UNKNOWN_ERROR;
            }
        }
        mAutoFocusThreadLock.unlock();
    }

    LOGV("autoFocus X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::cancelAutoFocus()
{
    LOGV("cancelAutoFocus E");
    Mutex::Autolock l(&mLock);

    int rc = NO_ERROR;
    if (mCameraRunning && mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
        rc = cancelAutoFocusInternal();
    }

    LOGV("cancelAutoFocus X");
    return rc;
}

void QualcommCameraHardware::runSnapshotThread(void *data)
{
    bool ret = true;
    CAMERA_HAL_UNUSED(data);
    LOGI("runSnapshotThread E");

    if(!libmmcamera){
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
    }
    mSnapshotCancelLock.lock();
    if(mSnapshotCancel == true) {
        mSnapshotCancel = false;
        mSnapshotCancelLock.unlock();
        LOGI("%s: cancelpicture has been called..so abort taking snapshot", __FUNCTION__);
        deinitRaw();
        mInSnapshotModeWaitLock.lock();
        mInSnapshotMode = false;
        mInSnapshotModeWait.signal();
        mInSnapshotModeWaitLock.unlock();
        mSnapshotThreadWaitLock.lock();
        mSnapshotThreadRunning = false;
        mSnapshotThreadWait.signal();
        mSnapshotThreadWaitLock.unlock();
        return;
    }
    mSnapshotCancelLock.unlock();

    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = true;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();
    mm_camera_ops_type_t current_ops_type = (mSnapshotFormat == PICTURE_FORMAT_JPEG) ?
                                             CAMERA_OPS_CAPTURE_AND_ENCODE :
                                              CAMERA_OPS_RAW_CAPTURE;
    if(strTexturesOn == true) {
        current_ops_type = CAMERA_OPS_CAPTURE;
        mCamOps.mm_camera_start(current_ops_type,(void *)&mImageCaptureParms,
                         NULL);
    } else if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if(!mZslEnable || mZslFlashEnable){
            mCamOps.mm_camera_start(current_ops_type,(void *)&mImageCaptureParms,
                 (void *)&mImageEncodeParms);
            }else{
                notifyShutter(NULL,TRUE);
                initZslParameter();
                LOGE("snapshot mZslCapture.thumbnail %d %d %d",mZslCaptureParms.thumbnail_width,
                                     mZslCaptureParms.thumbnail_height,mZslCaptureParms.num_captures);
                mCamOps.mm_camera_start(current_ops_type,(void *)&mZslCaptureParms,
                      (void *)&mImageEncodeParms);
           }
        mJpegThreadWaitLock.lock();
        while (mJpegThreadRunning) {
            LOGV("%s: waiting for jpeg callback.", __FUNCTION__);
            mJpegThreadWait.wait(mJpegThreadWaitLock);
            LOGV("%s: jpeg callback received.", __FUNCTION__);
        }
        mJpegThreadWaitLock.unlock();

        //cleanup
       if(!mZslEnable || mZslFlashEnable)
            deinitRaw();
    }else if(mSnapshotFormat == PICTURE_FORMAT_RAW){
        notifyShutter(NULL,TRUE);
        mCamOps.mm_camera_start(current_ops_type,(void *)&mRawCaptureParms,
                                 NULL);
        mJpegThreadWaitLock.lock();
        while (mJpegThreadRunning) {
            LOGV("%s: waiting for jpeg callback.", __FUNCTION__);
            mJpegThreadWait.wait(mJpegThreadWaitLock);
            LOGV("%s: jpeg callback received.", __FUNCTION__);
        }
        mJpegThreadWaitLock.unlock();
    }

    if(!mZslEnable || mZslFlashEnable)
        mCamOps.mm_camera_deinit(current_ops_type, NULL, NULL);
    mZslFlashEnable  = false;
    mSnapshotThreadWaitLock.lock();
    mSnapshotThreadRunning = false;
    mSnapshotThreadWait.signal();
    mSnapshotThreadWaitLock.unlock();
    LOGI("runSnapshotThread X");
}

void *snapshot_thread(void *user)
{
    LOGD("snapshot_thread E");
    CAMERA_HAL_UNUSED(user);
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runSnapshotThread(user);
    }
    else LOGW("not starting snapshot thread: the object went away!");
    LOGD("snapshot_thread X");
    return NULL;
}

status_t QualcommCameraHardware::takePicture()
{
    LOGE("takePicture(%d)", mMsgEnabled);
    Mutex::Autolock l(&mLock);

    if(strTexturesOn == true){
        mEncodePendingWaitLock.lock();
        while(mEncodePending) {
            LOGE("takePicture: Frame given to application, waiting for encode call");
            mEncodePendingWait.wait(mEncodePendingWaitLock);
            LOGE("takePicture: Encode of the application data is done");
        }
        mEncodePendingWaitLock.unlock();
    }

    // Wait for old snapshot thread to complete.
    mSnapshotThreadWaitLock.lock();
    while (mSnapshotThreadRunning) {
        LOGV("takePicture: waiting for old snapshot thread to complete.");
        mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
        LOGV("takePicture: old snapshot thread completed.");
    }
    // if flash is enabled then run snapshot as normal mode and not zsl mode.
    // App should expect only 1 callback as multi snapshot in normal mode is not supported
    mZslFlashEnable = false;
    if(mZslEnable){
        int is_flash_needed = 0;
        mm_camera_status_t status;
        status = mCfgControl.mm_camera_get_parm(CAMERA_PARM_QUERY_FALSH4SNAP,
                      (void *)&is_flash_needed);
        if(is_flash_needed) {
            mZslFlashEnable = true;
        }
    }

    if(mParameters.getPictureFormat() != 0 &&
            !strcmp(mParameters.getPictureFormat(),
                    CameraParameters::PIXEL_FORMAT_RAW)){
        mSnapshotFormat = PICTURE_FORMAT_RAW;
      {
       // HACK: Raw ZSL capture is not supported yet
        mZslFlashEnable = true;
      }
    }
    else
        mSnapshotFormat = PICTURE_FORMAT_JPEG;
    if(!mZslEnable || mZslFlashEnable){
        if((mSnapshotFormat == PICTURE_FORMAT_JPEG)){
            if(!native_start_ops(CAMERA_OPS_PREPARE_SNAPSHOT, NULL)) {
                mSnapshotThreadWaitLock.unlock();
                LOGE("PREPARE SNAPSHOT: CAMERA_OPS_PREPARE_SNAPSHOT ioctl Failed");
                return UNKNOWN_ERROR;
            }
        }
    }

    if(mCurrentTarget == TARGET_MSM8660) {
       /* Store the last frame queued for preview. This
        * shall be used as postview */
        if (!(storePreviewFrameForPostview()))
        return UNKNOWN_ERROR;
    }
    if(!mZslEnable || mZslFlashEnable)
        stopPreviewInternal();
    else if(mZslEnable && !mZslPanorama) {
        /* Dont stop preview if ZSL Panorama is enabled for
         * Continuous viewfinder support*/
        LOGE("Calling stop preview");
        mCamOps.mm_camera_stop(CAMERA_OPS_ZSL_STREAMING_CB,NULL, NULL);
    }

    mm_camera_ops_type_t current_ops_type = (mSnapshotFormat == PICTURE_FORMAT_JPEG) ?
                                             CAMERA_OPS_CAPTURE_AND_ENCODE :
                                              CAMERA_OPS_RAW_CAPTURE;
    if(strTexturesOn == true)
        current_ops_type = CAMERA_OPS_CAPTURE;

    if( !mZslEnable || mZslFlashEnable)
	    mCamOps.mm_camera_init(current_ops_type, NULL, NULL);

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if(!mZslEnable || mZslFlashEnable)
        {
            if (!initRaw(mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE))) {
                LOGE("initRaw failed.  Not taking picture.");
                mSnapshotThreadWaitLock.unlock();
                return UNKNOWN_ERROR;
            }
        }
    } else if(mSnapshotFormat == PICTURE_FORMAT_RAW ){
        if(!initRawSnapshot()){
            LOGE("initRawSnapshot failed. Not taking picture.");
            mSnapshotThreadWaitLock.unlock();
            return UNKNOWN_ERROR;
        }
    }

    mShutterLock.lock();
    mShutterPending = true;
    mShutterLock.unlock();

    mSnapshotCancelLock.lock();
    mSnapshotCancel = false;
    mSnapshotCancelLock.unlock();

    numJpegReceived = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    mSnapshotThreadRunning = !pthread_create(&mSnapshotThread,
                                             &attr,
                                             snapshot_thread,
                                             NULL);
    mSnapshotThreadWaitLock.unlock();

    mInSnapshotModeWaitLock.lock();
    mInSnapshotMode = true;
    mInSnapshotModeWaitLock.unlock();

    setOverlayFormats(mParameters);

    LOGE("takePicture: X");
    return mSnapshotThreadRunning ? NO_ERROR : UNKNOWN_ERROR;
}

void QualcommCameraHardware::set_liveshot_exifinfo()
{
    setGpsParameters();
    //set TimeStamp
    const char *str = mParameters.get(CameraParameters::KEY_EXIF_DATETIME);
    if(str != NULL) {
        strncpy(dateTime, str, 19);
        dateTime[19] = '\0';
        addExifTag(EXIFTAGID_EXIF_DATE_TIME_ORIGINAL, EXIF_ASCII,
                   20, 1, (void *)dateTime);
    }
}

status_t QualcommCameraHardware::takeLiveSnapshot()
{
    LOGV("takeLiveSnapshot: E ");
    Mutex::Autolock l(&mLock);

    if(liveshot_state == LIVESHOT_IN_PROGRESS || !recordingState) {
        return NO_ERROR;
    }

    if( (mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_MSM8660)) {
        LOGI("LiveSnapshot not supported on this target");
        liveshot_state = LIVESHOT_STOPPED;
        return NO_ERROR;
    }

    liveshot_state = LIVESHOT_IN_PROGRESS;

    if (!initLiveSnapshot(videoWidth, videoHeight)) {
        LOGE("takeLiveSnapshot: Jpeg Heap Memory allocation failed.  Not taking Live Snapshot.");
        liveshot_state = LIVESHOT_STOPPED;
        return UNKNOWN_ERROR;
    }

    uint32_t maxjpegsize = videoWidth * videoHeight *1.5;
    set_liveshot_exifinfo();
    if(!LINK_set_liveshot_params(videoWidth, videoHeight,
                                exif_data, exif_table_numEntries,
                                (uint8_t *)mJpegHeap->mHeap->base(), maxjpegsize)) {
        LOGE("Link_set_liveshot_params failed.");
        mJpegHeap.clear();
        return NO_ERROR;
    }

      if(!native_start_ops(CAMERA_OPS_LIVESHOT, NULL)) {
        LOGE("start_liveshot ioctl failed");
        liveshot_state = LIVESHOT_STOPPED;
        mJpegHeap.clear();
        return UNKNOWN_ERROR;
    }

    LOGV("takeLiveSnapshot: X");
    return NO_ERROR;
}

bool QualcommCameraHardware::initLiveSnapshot(int videowidth, int videoheight)
{
    LOGV("initLiveSnapshot E");

    if (mJpegHeap != NULL) {
        LOGV("initLiveSnapshot: clearing old mJpegHeap.");
        mJpegHeap.clear();
    }

    mJpegMaxSize = videowidth * videoheight * 1.5;

    LOGV("initLiveSnapshot: initializing mJpegHeap.");
    mJpegHeap =
        new AshmemPool(mJpegMaxSize,
                       kJpegBufferCount,
                       0, // we do not know how big the picture will be
                       "jpeg");

    if (!mJpegHeap->initialized()) {
        mJpegHeap.clear();
        LOGE("initLiveSnapshot X failed: error initializing mJpegHeap.");
        return false;
    }

    LOGV("initLiveSnapshot X");
    return true;
}


status_t QualcommCameraHardware::cancelPicture()
{
    status_t rc;
    LOGI("cancelPicture: E");

    mSnapshotCancelLock.lock();
    LOGI("%s: setting mSnapshotCancel to true", __FUNCTION__);
    mSnapshotCancel = true;
    mSnapshotCancelLock.unlock();

    if (mCurrentTarget == TARGET_MSM7627 ||
       (mCurrentTarget == TARGET_MSM7625A ||
        mCurrentTarget == TARGET_MSM7627A)) {
        mSnapshotDone = TRUE;
        mSnapshotThreadWaitLock.lock();
        while (mSnapshotThreadRunning) {
            LOGV("cancelPicture: waiting for snapshot thread to complete.");
            mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
            LOGV("cancelPicture: snapshot thread completed.");
        }
        mSnapshotThreadWaitLock.unlock();
    }
    rc = native_stop_ops(CAMERA_OPS_CAPTURE, NULL) ? NO_ERROR : UNKNOWN_ERROR;
    mSnapshotDone = FALSE;
    LOGI("cancelPicture: X: %d", rc);
    return rc;
}

status_t QualcommCameraHardware::setParameters(const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);
    Mutex::Autolock pl(&mParametersLock);
    status_t rc, final_rc = NO_ERROR;

    if ((rc = setPreviewSize(params)))  final_rc = rc;
    if ((rc = setRecordSize(params)))  final_rc = rc;
    if ((rc = setPictureSize(params)))  final_rc = rc;
    if ((rc = setJpegThumbnailSize(params))) final_rc = rc;
    if ((rc = setJpegQuality(params)))  final_rc = rc;
    if ((rc = setEffect(params)))       final_rc = rc;
    if ((rc = setGpsLocation(params)))  final_rc = rc;
    if ((rc = setRotation(params)))     final_rc = rc;
    if ((rc = setZoom(params)))         final_rc = rc;
    if ((rc = setOrientation(params)))  final_rc = rc;
    if ((rc = setLensshadeValue(params)))  final_rc = rc;
    if ((rc = setMCEValue(params)))  final_rc = rc;
    if ((rc = setPictureFormat(params))) final_rc = rc;
    if ((rc = setSharpness(params)))    final_rc = rc;
    if ((rc = setSaturation(params)))   final_rc = rc;
    if ((rc = setTouchAfAec(params)))   final_rc = rc;
    if ((rc = setSceneMode(params)))    final_rc = rc;
    if ((rc = setContrast(params)))     final_rc = rc;
    if ((rc = setSceneDetect(params)))  final_rc = rc;
    if ((rc = setStrTextures(params)))   final_rc = rc;
    if ((rc = setPreviewFormat(params)))   final_rc = rc;
    if ((rc = setSkinToneEnhancement(params)))   final_rc = rc;
    if ((rc = setAntibanding(params)))  final_rc = rc;
    if ((rc = setOverlayFormats(params)))  final_rc = rc;
    if ((rc = setRedeyeReduction(params)))  final_rc = rc;
    if ((rc = setDenoise(params)))  final_rc = rc;
    if ((rc = setPreviewFpsRange(params)))  final_rc = rc;
    if ((rc = setZslParam(params)))  final_rc = rc;
    if ((rc = setSnapshotCount(params)))  final_rc = rc;
    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if((value != NOT_FOUND) && (value == CAMERA_BESTSHOT_OFF)) {
        if ((rc = setPreviewFrameRate(params))) final_rc = rc;
        if ((rc = setPreviewFrameRateMode(params))) final_rc = rc;
        if ((rc = setAutoExposure(params))) final_rc = rc;
        if ((rc = setExposureCompensation(params))) final_rc = rc;
        if ((rc = setWhiteBalance(params))) final_rc = rc;
        if ((rc = setFlash(params)))        final_rc = rc;
        if ((rc = setFocusMode(params)))    final_rc = rc;
        if ((rc = setBrightness(params)))   final_rc = rc;
        if ((rc = setISOValue(params)))  final_rc = rc;
    }
    //selectableZoneAF needs to be invoked after continuous AF
    if ((rc = setSelectableZoneAf(params)))   final_rc = rc;
    // setHighFrameRate needs to be done at end, as there can
    // be a preview restart, and need to use the updated parameters
    if ((rc = setHighFrameRate(params)))  final_rc = rc;
    LOGV("setParameters: X");
    return final_rc;
}

CameraParameters QualcommCameraHardware::getParameters() const
{
    LOGV("getParameters: EX");
    return mParameters;
}
status_t QualcommCameraHardware::setHistogramOn()
{
    LOGV("setHistogramOn: EX");

    mStatsWaitLock.lock();
    mSendData = true;
    if(mStatsOn == CAMERA_HISTOGRAM_ENABLE) {
        mStatsWaitLock.unlock();
        return NO_ERROR;
     }

    if (mStatHeap != NULL) {
        LOGV("setHistogram on: clearing old mStatHeap.");
        mStatHeap.clear();
    }

    mStatSize = sizeof(uint32_t)* HISTOGRAM_STATS_SIZE;
    mCurrent = -1;
    /*Currently the Ashmem is multiplying the buffer size with total number
    of buffers and page aligning. This causes a crash in JNI as each buffer
    individually expected to be page aligned  */
    int page_size_minus_1 = getpagesize() - 1;
    int32_t mAlignedStatSize = ((mStatSize + page_size_minus_1) & (~page_size_minus_1));

    mStatHeap =
            new AshmemPool(mAlignedStatSize,
                           3,
                           mStatSize,
                           "stat");
      if (!mStatHeap->initialized()) {
          LOGE("Stat Heap X failed ");
          mStatHeap.clear();
          LOGE("setHistogramOn X: error initializing mStatHeap");
          mStatsWaitLock.unlock();
          return UNKNOWN_ERROR;
      }
    mStatsOn = CAMERA_HISTOGRAM_ENABLE;

    mStatsWaitLock.unlock();
    mCfgControl.mm_camera_set_parm(CAMERA_PARM_HISTOGRAM, &mStatsOn);
    return NO_ERROR;

}

status_t QualcommCameraHardware::setHistogramOff()
{
    LOGV("setHistogramOff: EX");
    mStatsWaitLock.lock();
    if(mStatsOn == CAMERA_HISTOGRAM_DISABLE) {
    mStatsWaitLock.unlock();
        return NO_ERROR;
     }
    mStatsOn = CAMERA_HISTOGRAM_DISABLE;
    mStatsWaitLock.unlock();

    mCfgControl.mm_camera_set_parm(CAMERA_PARM_HISTOGRAM, &mStatsOn);

    mStatsWaitLock.lock();
    mStatHeap.clear();
    mStatsWaitLock.unlock();

    return NO_ERROR;
}


status_t QualcommCameraHardware::runFaceDetection()
{
    bool ret = true;

    const char *str = mParameters.get(CameraParameters::KEY_FACE_DETECTION);
    if (str != NULL) {
        int value = attr_lookup(facedetection,
                sizeof(facedetection) / sizeof(str_map), str);

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
        ret = native_set_parms(CAMERA_PARM_FD, sizeof(int8_t), (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    LOGE("Invalid Face Detection value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

void* smoothzoom_thread(void* user)
{
    // call runsmoothzoomthread
    LOGV("smoothzoom_thread E");
    CAMERA_HAL_UNUSED(user);

    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runSmoothzoomThread(user);
    }
    else LOGE("not starting smooth zoom thread: the object went away!");
    LOGV("Smoothzoom_thread X");
    return NULL;
}

status_t QualcommCameraHardware::sendCommand(int32_t command, int32_t arg1,
                                             int32_t arg2)
{
    LOGV("sendCommand: EX");
    CAMERA_HAL_UNUSED(arg1);
    CAMERA_HAL_UNUSED(arg2);
    Mutex::Autolock l(&mLock);

    switch(command)  {

      case CAMERA_CMD_HISTOGRAM_ON:
                                   LOGV("histogram set to on");
                                   return setHistogramOn();
      case CAMERA_CMD_HISTOGRAM_OFF:
                                   LOGV("histogram set to off");
                                   return setHistogramOff();
      case CAMERA_CMD_HISTOGRAM_SEND_DATA:
                                   mStatsWaitLock.lock();
                                   if(mStatsOn == CAMERA_HISTOGRAM_ENABLE)
                                       mSendData = true;
                                   mStatsWaitLock.unlock();
                                   return NO_ERROR;
      case CAMERA_CMD_FACE_DETECTION_ON:
                                   if(supportsFaceDetection() == false){
                                        LOGI("face detection support is not available");
                                        return NO_ERROR;
                                   }

                                   setFaceDetection("on");
                                   return runFaceDetection();
      case CAMERA_CMD_FACE_DETECTION_OFF:
                                   if(supportsFaceDetection() == false){
                                        LOGI("face detection support is not available");
                                        return NO_ERROR;
                                   }
                                   setFaceDetection("off");
                                   return runFaceDetection();
      case CAMERA_CMD_SEND_META_DATA:
                                   mMetaDataWaitLock.lock();
                                   if(mFaceDetectOn == true) {
                                       mSendMetaData = true;
                                   }
                                   mMetaDataWaitLock.unlock();
                                   return NO_ERROR;
      case CAMERA_CMD_START_SMOOTH_ZOOM :
             LOGV("HAL sendcmd start smooth zoom %d %d", arg1 , arg2);
             mTargetSmoothZoom = arg1;
             if(!mPreviewStopping) {
                 // create smooth zoom thread
                 mSmoothzoomThreadLock.lock();
                 mSmoothzoomThreadExit = false;
                 pthread_attr_t attr;
                 pthread_attr_init(&attr);
                 pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                 pthread_create(&mSmoothzoomThread,
                                    &attr,
                                    smoothzoom_thread,
                                    NULL);
                 mSmoothzoomThreadLock.unlock();
             } else
                 LOGV(" Not creating smooth zoom thread "
                      " since preview is stopping ");
             mTargetSmoothZoom = arg1;
             return NO_ERROR;

      case CAMERA_CMD_STOP_SMOOTH_ZOOM :
             mSmoothzoomThreadLock.lock();
             mSmoothzoomThreadExit = true;
             mSmoothzoomThreadLock.unlock();
             LOGV("HAL sendcmd stop smooth zoom");
             return NO_ERROR;
   }
   return BAD_VALUE;
}

void QualcommCameraHardware::runSmoothzoomThread(void * data) {

    LOGV("runSmoothzoomThread: Current zoom %d - "
          "Target %d", mParameters.getInt("zoom"), mTargetSmoothZoom);
    int current_zoom = mParameters.getInt("zoom");
    int step = (current_zoom > mTargetSmoothZoom)? -1: 1;

    if(current_zoom == mTargetSmoothZoom) {
        LOGV("Smoothzoom target zoom value is same as "
             "current zoom value, return...");
        if(!mPreviewStopping)
            mNotifyCallback(CAMERA_MSG_ZOOM,
                current_zoom, 1, mCallbackCookie);
        else
            LOGV("Not issuing callback since preview is stopping");
        return;
    }

    CameraParameters p = getParameters();

    mSmoothzoomThreadWaitLock.lock();
    mSmoothzoomThreadRunning = true;
    mSmoothzoomThreadWaitLock.unlock();

    int i = current_zoom;
    while(1) {  // Thread loop
        mSmoothzoomThreadLock.lock();
        if(mSmoothzoomThreadExit) {
            LOGV("Exiting smoothzoom thread, as stop smoothzoom called");
            mSmoothzoomThreadLock.unlock();
            break;
        }
        mSmoothzoomThreadLock.unlock();

        if((i < 0) || (i > mMaxZoom)) {
            LOGE(" ERROR : beyond supported zoom values, break..");
            break;
        }
        // update zoom
        p.set("zoom", i);
        setZoom(p);
        if(!mPreviewStopping) {
            // give call back to zoom listener in app
            mNotifyCallback(CAMERA_MSG_ZOOM, i, (mTargetSmoothZoom-i == 0)?1:0,
                    mCallbackCookie);
        } else {
            LOGV("Preview is stopping. Breaking out of smooth zoom loop");
            break;
        }
        if(i == mTargetSmoothZoom)
            break;

        i+=step;

        /* wait on singal, which will be signalled on
         * receiving next preview frame */
        mSmoothzoomThreadWaitLock.lock();
        //LOGV("Smoothzoom thread: waiting for preview frame.");
        mSmoothzoomThreadWait.wait(mSmoothzoomThreadWaitLock);
        //LOGV("Smoothzoom thread: wait over for preview frame.");
        mSmoothzoomThreadWaitLock.unlock();
    } // while loop over, exiting thread

    mSmoothzoomThreadWaitLock.lock();
    mSmoothzoomThreadRunning = false;
    mSmoothzoomThreadWaitLock.unlock();
    LOGV("Exiting Smooth Zoom Thread");
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId, int mode)
{
    int i;
    LOGI("openCameraHardware: call createInstance");
    for(i = 0; i < HAL_numOfCameras; i++) {
        if(HAL_cameraInfo[i].camera_id == cameraId) {
            LOGI("openCameraHardware:Valid camera ID %d", cameraId);
            LOGI("openCameraHardware:camera mode %d", mode);
            parameter_string_initialized = false;
            HAL_currentCameraId = cameraId;
            HAL_currentCameraMode = CAMERA_MODE_2D;
            /* The least significant two bits of mode parameter indicates the sensor mode
               of 2D or 3D. The next two bits indicates the snapshot mode of
               ZSL or NONZSL
               */
            int sensorModeMask = 0x03 & mode;
            if(sensorModeMask & HAL_cameraInfo[i].modes_supported){
                HAL_currentCameraMode = sensorModeMask;
            }else{
                LOGE("openCameraHardware:Invalid camera mode (%d) requested", mode);
                return NULL;
            }
            HAL_currentSnapshotMode = CAMERA_SNAPSHOT_NONZSL;
            //Remove values set by app other than  supported values
            mode = mode & HAL_cameraInfo[cameraId].modes_supported;
            if((mode & CAMERA_SNAPSHOT_ZSL) == CAMERA_SNAPSHOT_ZSL)
                HAL_currentSnapshotMode = CAMERA_SNAPSHOT_ZSL;
            LOGI("%s: HAL_currentSnapshotMode = %d", __FUNCTION__, HAL_currentSnapshotMode);

            return QualcommCameraHardware::createInstance();
        }
    }
    LOGE("openCameraHardware:Invalid camera ID %d", cameraId);
    return NULL;
}

wp<QualcommCameraHardware> QualcommCameraHardware::singleton;

// If the hardware already exists, return a strong pointer to the current
// object. If not, create a new hardware object, put it in the singleton,
// and return it.
sp<CameraHardwareInterface> QualcommCameraHardware::createInstance()
{
    LOGI("createInstance: E");

    singleton_lock.lock();

    // Wait until the previous release is done.
    while (singleton_releasing) {
        if((singleton_releasing_start_time != 0) &&
                (systemTime() - singleton_releasing_start_time) > SINGLETON_RELEASING_WAIT_TIME){
            LOGV("in createinstance system time is %lld %lld %lld ",
                    systemTime(), singleton_releasing_start_time, SINGLETON_RELEASING_WAIT_TIME);
            singleton_lock.unlock();
            LOGE("Previous singleton is busy and time out exceeded. Returning null");
            return NULL;
        }
        LOGI("Wait for previous release.");
        singleton_wait.waitRelative(singleton_lock, SINGLETON_RELEASING_RECHECK_TIMEOUT);
        LOGI("out of Wait for previous release.");
    }

    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            LOGD("createInstance: X return existing hardware=%p", &(*hardware));
            singleton_lock.unlock();
            return hardware;
        }
    }

    {
        struct stat st;
        int rc = stat("/dev/oncrpc", &st);
        if (rc < 0) {
            LOGD("createInstance: X failed to create hardware: %s", strerror(errno));
            singleton_lock.unlock();
            return NULL;
        }
    }

    QualcommCameraHardware *cam = new QualcommCameraHardware();
    sp<QualcommCameraHardware> hardware(cam);
    singleton = hardware;

    LOGI("createInstance: created hardware=%p", &(*hardware));
    if (!cam->startCamera()) {
        LOGE("%s: startCamera failed!", __FUNCTION__);
        singleton_lock.unlock();
        return NULL;
    }

    cam->initDefaultParameters();
    singleton_lock.unlock();
    LOGI("createInstance: X");
    return hardware;
}

// For internal use only, hence the strong pointer to the derived type.
sp<QualcommCameraHardware> QualcommCameraHardware::getInstance()
{
    sp<CameraHardwareInterface> hardware = singleton.promote();
    if (hardware != 0) {
        //    LOGV("getInstance: X old instance of hardware");
        return sp<QualcommCameraHardware>(static_cast<QualcommCameraHardware*>(hardware.get()));
    } else {
        LOGV("getInstance: X new instance of hardware");
        return sp<QualcommCameraHardware>();
    }
}
void QualcommCameraHardware::receiveRecordingFrame(struct msm_frame *frame)
{
    LOGV("receiveRecordingFrame E");
    // post busy frame
    if (frame)
    {
        cam_frame_post_video (frame);
    }
    else LOGE("in  receiveRecordingFrame frame is NULL");
    LOGV("receiveRecordingFrame X");
}


bool QualcommCameraHardware::native_zoom_image(int fd, int srcOffset, int dstOffSet, common_crop_t *crop,int framewidth,int frameheight)
{
    int result = 0;
    struct mdp_blit_req *e;

    /* Initialize yuv structure */
    zoomImage.list.count = 1;

    e = &zoomImage.list.req[0];

    e->src.width = framewidth;
    e->src.height = frameheight;
    e->src.format = MDP_Y_CBCR_H2V2;
    e->src.offset = srcOffset;
    e->src.memory_id = fd;

    e->dst.width = framewidth;
    e->dst.height = frameheight;
    e->dst.format = MDP_Y_CBCR_H2V2;
    e->dst.offset = dstOffSet;
    e->dst.memory_id = fd;

    e->transp_mask = 0xffffffff;
    e->flags = 0;
    e->alpha = 0xff;
    if (crop->in1_w != 0 && crop->in1_h != 0) {
        e->src_rect.x = (crop->out1_w - crop->in1_w + 1) / 2 - 1;
        e->src_rect.y = (crop->out1_h - crop->in1_h + 1) / 2 - 1;
        e->src_rect.w = crop->in1_w;
        e->src_rect.h = crop->in1_h;
    } else {
        e->src_rect.x = 0;
        e->src_rect.y = 0;
        e->src_rect.w = framewidth;
        e->src_rect.h = frameheight;
    }
    //LOGV(" native_zoom : SRC_RECT : x,y = %d,%d \t w,h = %d, %d",
    //        e->src_rect.x, e->src_rect.y, e->src_rect.w, e->src_rect.h);

    e->dst_rect.x = 0;
    e->dst_rect.y = 0;
    e->dst_rect.w = framewidth;
    e->dst_rect.h = frameheight;

    result = ioctl(fb_fd, MSMFB_BLIT, &zoomImage.list);
    if (result < 0) {
        LOGE("MSM_FBIOBLT failed! line=%d\n", __LINE__);
        return FALSE;
    }
    return TRUE;
}

void QualcommCameraHardware::debugShowPreviewFPS() const
{
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (diff > ms2ns(250)) {
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        LOGI("Preview Frames Per Second: %.4f", mFps);
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}

void QualcommCameraHardware::debugShowVideoFPS() const
{
    static int mFrameCount;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mLastFpsTime;
    if (diff > ms2ns(250)) {
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        LOGI("Video Frames Per Second: %.4f", mFps);
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
    }
}

void QualcommCameraHardware::receiveLiveSnapshot(uint32_t jpeg_size)
{
    LOGV("receiveLiveSnapshot E");

#ifdef DUMP_LIVESHOT_JPEG_FILE
    int file_fd = open("/data/LiveSnapshot.jpg", O_RDWR | O_CREAT, 0777);
    LOGV("dumping live shot image in /data/LiveSnapshot.jpg");
    if (file_fd < 0) {
        LOGE("cannot open file\n");
    }
    else
    {
        write(file_fd, (uint8_t *)mJpegHeap->mHeap->base(),jpeg_size);
    }
    close(file_fd);
#endif

#if 1
    Mutex::Autolock cbLock(&mCallbackLock);
    if (mDataCallback && (mMsgEnabled & MEDIA_RECORDER_MSG_COMPRESSED_IMAGE)) {
        sp<MemoryBase> buffer = new
            MemoryBase(mJpegHeap->mHeap,
                       0,
                       jpeg_size);
        mDataCallback(MEDIA_RECORDER_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
        buffer = NULL;
    }
    else LOGV("JPEG callback was cancelled--not delivering image.");

    //Reset the Gps Information & relieve memory
    exif_table_numEntries = 0;
    mJpegHeap.clear();

    liveshot_state = LIVESHOT_DONE;

    LOGV("receiveLiveSnapshot X");
#endif
}
void QualcommCameraHardware::receivePreviewFrame(struct msm_frame *frame)
{
//    LOGV("receivePreviewFrame E");
    if (!mCameraRunning) {
        LOGE("ignoring preview callback--camera has been stopped");
        LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);
        return;
    }

    if(mPreviewBusyQueue.add(frame) == false)
        LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);


//  LOGV("receivePreviewFrame X");
}
void QualcommCameraHardware::receiveCameraStats(camstats_type stype, camera_preview_histogram_info* histinfo)
{
  //  LOGV("receiveCameraStats E");
    CAMERA_HAL_UNUSED(stype);

    if (!mCameraRunning) {
        LOGE("ignoring stats callback--camera has been stopped");
        return;
    }

    mOverlayLock.lock();
    if(mOverlay == NULL) {
       mOverlayLock.unlock();
       return;
    }
    mOverlayLock.unlock();
    mCallbackLock.lock();
    int msgEnabled = mMsgEnabled;
    data_callback scb = mDataCallback;
    void *sdata = mCallbackCookie;
    mCallbackLock.unlock();
    mStatsWaitLock.lock();
    if(mStatsOn == CAMERA_HISTOGRAM_DISABLE) {
      mStatsWaitLock.unlock();
      return;
    }
    if(!mSendData) {
        mStatsWaitLock.unlock();
     } else {
        mSendData = false;
        mCurrent = (mCurrent+1)%3;
    // The first element of the array will contain the maximum hist value provided by driver.
        *(uint32_t *)((unsigned int)mStatHeap->mHeap->base()+ (mStatHeap->mBufferSize * mCurrent)) = histinfo->max_value;
        memcpy((uint32_t *)((unsigned int)mStatHeap->mHeap->base()+ (mStatHeap->mBufferSize * mCurrent)+ sizeof(int32_t)), (uint32_t *)histinfo->buffer,(sizeof(int32_t) * 256));

        mStatsWaitLock.unlock();

        if (scb != NULL && (msgEnabled & CAMERA_MSG_STATS_DATA))
            scb(CAMERA_MSG_STATS_DATA, mStatHeap->mBuffers[mCurrent],
                sdata);
     }
  //  LOGV("receiveCameraStats X");
}


bool QualcommCameraHardware::initRecord()
{
    const char *pmem_region;
    int CbCrOffset;
    int recordBufferSize;

    LOGV("initREcord E");
    if(mZslEnable){
       LOGV("initRecord X.. Not intializing Record buffers in ZSL mode");
       return true;
    }

    if(mCurrentTarget == TARGET_MSM8660)
        pmem_region = "/dev/pmem_smipool";
    else
        pmem_region = "/dev/pmem_adsp";

    LOGI("initRecord: mDimension.video_width = %d mDimension.video_height = %d",
             mDimension.video_width, mDimension.video_height);
    // for 8x60 the Encoder expects the CbCr offset should be aligned to 2K.
    if(mCurrentTarget == TARGET_MSM8660) {
        CbCrOffset = PAD_TO_2K(mDimension.video_width  * mDimension.video_height);
        recordBufferSize = CbCrOffset + PAD_TO_2K((mDimension.video_width * mDimension.video_height)/2);
    } else {
        CbCrOffset = PAD_TO_WORD(mDimension.video_width  * mDimension.video_height);
        recordBufferSize = (mDimension.video_width  * mDimension.video_height *3)/2;
    }

    /* Buffersize and frameSize will be different when DIS is ON.
     * We need to pass the actual framesize with video heap, as the same
     * is used at camera MIO when negotiating with encoder.
     */
    mRecordFrameSize = recordBufferSize;
    bool dis_disable = 0;
    const char *str = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
    if((str != NULL) && (strcmp(str, CameraParameters::VIDEO_HFR_OFF))) {
        LOGI("%s: HFR is ON, DIS has to be OFF", __FUNCTION__);
        dis_disable = 1;
    }
    if((mVpeEnabled && mDisEnabled && (!dis_disable))|| mIs3DModeOn){
        mRecordFrameSize = videoWidth * videoHeight * 3 / 2;
        if(mCurrentTarget == TARGET_MSM8660){
            mRecordFrameSize = PAD_TO_2K(videoWidth * videoHeight)
                                + PAD_TO_2K((videoWidth * videoHeight)/2);
        }
    }
    LOGV("mRecordFrameSize = %d", mRecordFrameSize);
    if(mRecordHeap == NULL) {
        mRecordHeap = new PmemPool(pmem_region,
                               MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                MSM_PMEM_VIDEO,
                                recordBufferSize,
                                kRecordBufferCount,
                                mRecordFrameSize,
                                CbCrOffset,
                                0,
                                "record");
        if (!mRecordHeap->initialized()) {
            mRecordHeap.clear();
            mRecordHeap = NULL;
            LOGE("initRecord X: could not initialize record heap.");
            return false;
        }
    } else {
        if(mHFRMode == true) {
            LOGI("%s: register record buffers with camera driver", __FUNCTION__);
            register_record_buffers(true);
            mHFRMode = false;
        }
    }

    for (int cnt = 0; cnt < kRecordBufferCount; cnt++) {
        recordframes[cnt].fd = mRecordHeap->mHeap->getHeapID();
        recordframes[cnt].buffer =
            (uint32_t)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
        recordframes[cnt].y_off = 0;
        recordframes[cnt].cbcr_off = CbCrOffset;
        recordframes[cnt].path = OUTPUT_TYPE_V;
        record_buffers_tracking_flag[cnt] = false;
        LOGV ("initRecord :  record heap , video buffers  buffer=%lu fd=%d y_off=%d cbcr_off=%d \n",
          (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, recordframes[cnt].y_off,
          recordframes[cnt].cbcr_off);
    }

    // initial setup : buffers 1,2,3 with kernel , 4 with camframe , 5,6,7,8 in free Q
    // flush the busy Q
    cam_frame_flush_video();

    mVideoThreadWaitLock.lock();
    while (mVideoThreadRunning) {
        LOGV("initRecord: waiting for old video thread to complete.");
        mVideoThreadWait.wait(mVideoThreadWaitLock);
        LOGV("initRecord : old video thread completed.");
    }
    mVideoThreadWaitLock.unlock();

    // flush free queue and add 5,6,7,8 buffers.
    LINK_camframe_release_all_frames(CAM_VIDEO_FRAME);
    if(mVpeEnabled) {
        //If VPE is enabled, the VPE buffer shouldn't be added to Free Q initally.
        for(int i=ACTIVE_VIDEO_BUFFERS;i <kRecordBufferCount-1; i++)
            LINK_camframe_add_frame(CAM_VIDEO_FRAME,&recordframes[i]);
    } else {
        for(int i=ACTIVE_VIDEO_BUFFERS;i <kRecordBufferCount; i++)
            LINK_camframe_add_frame(CAM_VIDEO_FRAME,&recordframes[i]);
    }
    LOGV("initREcord X");

    return true;
}


status_t QualcommCameraHardware::setDIS() {
    LOGV("setDIS E");

    video_dis_param_ctrl_t disCtrl;
    bool ret = true;
    LOGV("mDisEnabled = %d", mDisEnabled);

    int video_frame_cbcroffset;
    video_frame_cbcroffset = PAD_TO_WORD(videoWidth * videoHeight);
    if(mCurrentTarget == TARGET_MSM8660)
        video_frame_cbcroffset = PAD_TO_2K(videoWidth * videoHeight);

    disCtrl.dis_enable = mDisEnabled;
    const char *str = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
    if((str != NULL) && (strcmp(str, CameraParameters::VIDEO_HFR_OFF))) {
        LOGI("%s: HFR is ON, setting DIS as OFF", __FUNCTION__);
        disCtrl.dis_enable = 0;
    }
    disCtrl.video_rec_width = videoWidth;
    disCtrl.video_rec_height = videoHeight;
    disCtrl.output_cbcr_offset = video_frame_cbcroffset;

    ret = native_set_parms( CAMERA_PARM_VIDEO_DIS,
                       sizeof(disCtrl), &disCtrl);

    LOGV("setDIS X (%d)", ret);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setVpeParameters()
{
    LOGV("setVpeParameters E");

    video_rotation_param_ctrl_t rotCtrl;
    bool ret = true;
    LOGV("videoWidth = %d, videoHeight = %d", videoWidth, videoHeight);
    rotCtrl.rotation = (mRotation == 0) ? ROT_NONE :
                       ((mRotation == 90) ? ROT_CLOCKWISE_90 :
                  ((mRotation == 180) ? ROT_CLOCKWISE_180 : ROT_CLOCKWISE_270));

    if( ((videoWidth == 1280 && videoHeight == 720) || (videoWidth == 800 && videoHeight == 480))
        && (mRotation == 90 || mRotation == 270) ){
        /* Due to a limitation at video core to support heights greater than 720, adding this check.
         * This is a temporary hack, need to be removed once video core support is available
         */
        LOGI("video resolution (%dx%d) with rotation (%d) is not supported, setting rotation to NONE",
            videoWidth, videoHeight, mRotation);
        rotCtrl.rotation = ROT_NONE;
    }
    LOGV("rotCtrl.rotation = %d", rotCtrl.rotation);

    ret = native_set_parms(CAMERA_PARM_VIDEO_ROT,
                           sizeof(rotCtrl), &rotCtrl);

    LOGV("setVpeParameters X (%d)", ret);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::startRecording()
{
    LOGV("startRecording E");
    int ret;
    Mutex::Autolock l(&mLock);

    if(mZslEnable){
        LOGE("Recording not supported in ZSL mode");
        return UNKNOWN_ERROR;
    }

    if( (ret=startPreviewInternal())== NO_ERROR) {
        /* this variable state will be used in 3D mode.
         * recordingState = 1 : start giving frames for encoding.
         * recordingState = 0 : stop giving frames for encoding.
         */
        recordingState = 1;
        return startRecordingInternal();
    }

    return ret;
}

status_t QualcommCameraHardware::startRecordingInternal()
{
    LOGI("%s: E", __FUNCTION__);
    mReleasedRecordingFrame = false;

    /* In 3D mode, the video thread has to be started as part
     * of preview itself, because video buffers and video callback
     * need to be used for both display and encoding.
     * startRecordingInternal() will be called as part of startPreview().
     * This check is needed to support both 3D and non-3D mode.
     */
    if(mVideoThreadRunning) {
        LOGI("Video Thread is in progress");
        return NO_ERROR;
    }

    if(mVpeEnabled){
        LOGI("startRecording: VPE enabled, setting vpe parameters");
        bool status = setVpeParameters();
        if(status) {
            LOGE("Failed to set VPE parameters");
            return status;
        }
    }
    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660))  {
        // Remove the left out frames in busy Q and them in free Q.
        // this should be done before starting video_thread so that,
        // frames in previous recording are flushed out.
        LOGV("frames in busy Q = %d", g_busy_frame_queue.num_of_frames);
        while((g_busy_frame_queue.num_of_frames) >0){
            msm_frame* vframe = cam_frame_get_video ();
            LINK_camframe_add_frame(CAM_VIDEO_FRAME,vframe);
        }
        LOGV("frames in busy Q = %d after deQueing", g_busy_frame_queue.num_of_frames);

        //Clear the dangling buffers and put them in free queue
        for(int cnt = 0; cnt < kRecordBufferCount; cnt++) {
            if(record_buffers_tracking_flag[cnt] == true) {
                LOGI("Dangling buffer: offset = %d, buffer = %d", cnt, (unsigned int)recordframes[cnt].buffer);
                LINK_camframe_add_frame(CAM_VIDEO_FRAME,&recordframes[cnt]);
                record_buffers_tracking_flag[cnt] = false;
            }
        }

        LOGE(" in startREcording : calling start_recording");
        if(!mIs3DModeOn)
            native_start_ops(CAMERA_OPS_VIDEO_RECORDING, NULL);

        // Start video thread and wait for busy frames to be encoded, this thread
        // should be closed in stopRecording
        mVideoThreadWaitLock.lock();
        mVideoThreadExit = 0;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        mVideoThreadRunning = !pthread_create(&mVideoThread,
                                              &attr,
                                              video_thread,
                                              NULL);
        mVideoThreadWaitLock.unlock();
        // Remove the left out frames in busy Q and them in free Q.
    }
    LOGV("%s: E", __FUNCTION__);
    return NO_ERROR;
}

void QualcommCameraHardware::stopRecording()
{
    LOGV("stopRecording: E");
    Mutex::Autolock l(&mLock);
    {
        mRecordFrameLock.lock();
        mReleasedRecordingFrame = true;
        mRecordWait.signal();
        mRecordFrameLock.unlock();

        if(mDataCallback && !(mCurrentTarget == TARGET_QSD8250) &&
                         (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
            LOGV("stopRecording: X, preview still in progress");
            return;
        }
    }
    // If output2 enabled, exit video thread, invoke stop recording ioctl
    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660))  {
        /* when 3D mode is ON, don't exit the video thread, as
         * we need to support the preview mode. Just set the recordingState
         * to zero, so that there won't be any rcb callbacks. video thread
         * will be terminated as part of stop preview.
         */
        if(mIs3DModeOn) {
            LOGV("%s: 3D mode on, so don't exit video thread", __FUNCTION__);
            recordingState = 0;
            return;
        }

        mVideoThreadWaitLock.lock();
        mVideoThreadExit = 1;
        mVideoThreadWaitLock.unlock();
        native_stop_ops(CAMERA_OPS_VIDEO_RECORDING, NULL);

        pthread_mutex_lock(&(g_busy_frame_queue.mut));
        pthread_cond_signal(&(g_busy_frame_queue.wait));
        pthread_mutex_unlock(&(g_busy_frame_queue.mut));
    }
    else  // for other targets where output2 is not enabled
        stopPreviewInternal();

    if (mJpegHeap != NULL) {
        LOGV("stopRecording: clearing old mJpegHeap.");
        mJpegHeap.clear();
    }
    recordingState = 0; // recording not started
    LOGV("stopRecording: X");
}

void QualcommCameraHardware::releaseRecordingFrame(
       const sp<IMemory>& mem __attribute__((unused)))
{
    LOGV("releaseRecordingFrame E");
    Mutex::Autolock rLock(&mRecordFrameLock);
    mReleasedRecordingFrame = true;
    mRecordWait.signal();

    // Ff 7x30 : add the frame to the free camframe queue
    if( (mCurrentTarget == TARGET_MSM7630 )  || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {
        ssize_t offset;
        size_t size;
        sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);
        msm_frame* releaseframe = NULL;
        LOGV(" in release recording frame :  heap base %d offset %d buffer %d ", heap->base(), offset, heap->base() + offset );
        int cnt;
        for (cnt = 0; cnt < kRecordBufferCount; cnt++) {
            if((unsigned int)recordframes[cnt].buffer == ((unsigned int)heap->base()+ offset)){
                LOGV("in release recording frame found match , releasing buffer %d", (unsigned int)recordframes[cnt].buffer);
                releaseframe = &recordframes[cnt];
                break;
            }
        }
        if(cnt < kRecordBufferCount) {
            // do this only if frame thread is running
            mFrameThreadWaitLock.lock();
            if(mFrameThreadRunning ) {
                //Reset the track flag for this frame buffer
                record_buffers_tracking_flag[cnt] = false;
                LINK_camframe_add_frame(CAM_VIDEO_FRAME,releaseframe);
            }

            mFrameThreadWaitLock.unlock();
        } else {
            LOGE("in release recordingframe XXXXX error , buffer not found");
            for (int i=0; i< kRecordBufferCount; i++) {
                 LOGE(" recordframes[%d].buffer = %d", i, (unsigned int)recordframes[i].buffer);
            }
        }
    }

    LOGV("releaseRecordingFrame X");
}

bool QualcommCameraHardware::recordingEnabled()
{
    return mCameraRunning && mDataCallbackTimestamp && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME);
}

void QualcommCameraHardware::notifyShutter(common_crop_t *crop,bool mPlayShutterSoundOnly)
{
    mShutterLock.lock();
    image_rect_type size;

    if(mPlayShutterSoundOnly) {
        /* At this point, invoke Notify Callback to play shutter sound only.
         * We want to call notify callback again when we have the
         * yuv picture ready. This is to reduce blanking at the time
         * of displaying postview frame. Using ext2 to indicate whether
         * to play shutter sound only or register the postview buffers.
         */
        mNotifyCallback(CAMERA_MSG_SHUTTER, 0, mPlayShutterSoundOnly,
                            mCallbackCookie);
        mShutterLock.unlock();
        return;
    }

    if (mShutterPending && mNotifyCallback && (mMsgEnabled & CAMERA_MSG_SHUTTER)) {
        mDisplayHeap = mPostviewHeap;
        if (crop != NULL && (crop->in1_w != 0 && crop->in1_h != 0)) {
            size.width = crop->in1_w;
            size.height = crop->in1_h;
        }
        else {
            size.width = mPostviewWidth;
            size.height = mPostviewHeight;
        }
        if(strTexturesOn == true) {
            mDisplayHeap = mRawHeap;
            size.width = mPictureWidth;
            size.height = mPictureHeight;
        }
        /* Now, invoke Notify Callback to unregister preview buffer
         * and register postview buffer with surface flinger. Set ext2
         * as 0 to indicate not to play shutter sound.
         */
        mNotifyCallback(CAMERA_MSG_SHUTTER, (int32_t)&size, 0,
                        mCallbackCookie);
        mShutterPending = false;
    }
    mShutterLock.unlock();
}

static void receive_shutter_callback(common_crop_t *crop)
{
    LOGV("receive_shutter_callback: E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        /* Just play shutter sound at this time */
        obj->notifyShutter(NULL,TRUE);
    }
    LOGV("receive_shutter_callback: X");
}

// Crop the picture in place.
static void crop_yuv420(uint32_t width, uint32_t height,
                 uint32_t cropped_width, uint32_t cropped_height,
                 uint8_t *image, const char *name)
{
    uint32_t i;
    uint32_t x, y;
    uint8_t* chroma_src, *chroma_dst;
    int yOffsetSrc, yOffsetDst, CbCrOffsetSrc, CbCrOffsetDst;
    int mSrcSize, mDstSize;

    //check if all fields needed eg. size and also how to set y offset. If condition for 7x27
    //and need to check if needed for 7x30.

    LINK_jpeg_encoder_get_buffer_offset(width, height, (uint32_t *)&yOffsetSrc,
                                       (uint32_t *)&CbCrOffsetSrc, (uint32_t *)&mSrcSize);

    LINK_jpeg_encoder_get_buffer_offset(cropped_width, cropped_height, (uint32_t *)&yOffsetDst,
                                       (uint32_t *)&CbCrOffsetDst, (uint32_t *)&mDstSize);

    // Calculate the start position of the cropped area.
    x = (width - cropped_width) / 2;
    y = (height - cropped_height) / 2;
    x &= ~1;
    y &= ~1;

    if((mCurrentTarget == TARGET_MSM7627)
       || (mCurrentTarget == TARGET_MSM7625A)
       || (mCurrentTarget == TARGET_MSM7627A)
       || (mCurrentTarget == TARGET_MSM7630)
       || (mCurrentTarget == TARGET_MSM8660)) {
        if (!strcmp("snapshot camera", name)) {
            chroma_src = image + CbCrOffsetSrc;
            chroma_dst = image + CbCrOffsetDst;
        } else {
            chroma_src = image + width * height;
            chroma_dst = image + cropped_width * cropped_height;
            yOffsetSrc = 0;
            yOffsetDst = 0;
            CbCrOffsetSrc = width * height;
            CbCrOffsetDst = cropped_width * cropped_height;
        }
    } else {
       chroma_src = image + CbCrOffsetSrc;
       chroma_dst = image + CbCrOffsetDst;
    }

    int32_t bufDst = yOffsetDst;
    int32_t bufSrc = yOffsetSrc + (width * y) + x;

    if( bufDst > bufSrc ){
        LOGV("crop yuv Y destination position follows source position");
        /*
         * If buffer destination follows buffer source, memcpy
         * of lines will lead to overwriting subsequent lines. In order
         * to prevent this, reverse copying of lines is performed
         * for the set of lines where destination follows source and
         * forward copying of lines is performed for lines where source
         * follows destination. To calculate the position to switch,
         * the initial difference between source and destination is taken
         * and divided by difference between width and cropped width. For
         * every line copied the difference between source destination
         * drops by width - cropped width
         */
        //calculating inversion
        int position = ( bufDst - bufSrc ) / (width - cropped_width);
        // Copy luma component.
        for(i=position+1; i < cropped_height; i++){
            memmove(image + yOffsetDst + i * cropped_width,
                    image + yOffsetSrc + width * (y + i) + x,
                    cropped_width);
        }
        for(int j=position; j>=0; j--){
            memmove(image + yOffsetDst + j * cropped_width,
                    image + yOffsetSrc + width * (y + j) + x,
                    cropped_width);
        }
    } else {
        // Copy luma component.
        for(i = 0; i < cropped_height; i++)
            memcpy(image + yOffsetDst + i * cropped_width,
                   image + yOffsetSrc + width * (y + i) + x,
                   cropped_width);
    }

    // Copy chroma components.
    cropped_height /= 2;
    y /= 2;

    bufDst = CbCrOffsetDst;
    bufSrc = CbCrOffsetSrc + (width * y) + x;

    if( bufDst > bufSrc ) {
        LOGV("crop yuv Chroma destination position follows source position");
        /*
         * Similar to y
         */
        int position = ( bufDst - bufSrc ) / (width - cropped_width);
        for(i=position+1; i < cropped_height; i++){
            memmove(chroma_dst + i * cropped_width,
                    chroma_src + width * (y + i) + x,
                    cropped_width);
        }
        for(int j=position; j >=0; j--){
            memmove(chroma_dst + j * cropped_width,
                    chroma_src + width * (y + j) + x,
                    cropped_width);
        }
    } else {
        for(i = 0; i < cropped_height; i++)
            memcpy(chroma_dst + i * cropped_width,
                   chroma_src + width * (y + i) + x,
                   cropped_width);
    }
}

void QualcommCameraHardware::receiveRawPicture(status_t status,struct msm_frame *postviewframe, struct msm_frame *mainframe)
{
    LOGE("%s: E", __FUNCTION__);
    void *cropp;
    common_crop_t *crop = NULL;
    ssize_t offset_addr ;
    ssize_t offset;
    mSnapshotThreadWaitLock.lock();
    if(mSnapshotThreadRunning == false) {
        LOGE("%s called in wrong state, ignore", __FUNCTION__);
        return;
    }
    mSnapshotThreadWaitLock.unlock();

    if(status != NO_ERROR){
        LOGE("%s: Failed to get Snapshot Image", __FUNCTION__);
        if(mDataCallback &&
            (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
            /* get picture failed. Give jpeg callback with NULL data
             * to the application to restore to preview mode
             */
            LOGE("get picture failed, giving jpeg callback with NULL data");
            mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, NULL, mCallbackCookie);
        }
        mShutterLock.lock();
        mShutterPending = false;
        mShutterLock.unlock();
        mJpegThreadWaitLock.lock();
        mJpegThreadRunning = false;
        mJpegThreadWait.signal();
        mJpegThreadWaitLock.unlock();
        mInSnapshotModeWaitLock.lock();
        mInSnapshotMode = false;
        mInSnapshotModeWait.signal();
        mInSnapshotModeWaitLock.unlock();
        return;
    }
    /* call notifyShutter to config surface and overlay
     * for postview rendering.
     * Its necessary to issue another notifyShutter here with
     * mPlayShutterSoundOnly as FALSE, since that is when the
     * preview buffers are unregistered with the surface flinger.
     * That is necessary otherwise the preview memory wont be
     * deallocated.
     */
    cropp =postviewframe->cropinfo;
    if((mCurrentTarget == TARGET_MSM7627 ||
       (mCurrentTarget == TARGET_MSM7627A ||
        mCurrentTarget == TARGET_MSM7625A)) &&
        cropp != NULL) {
        crop = (common_crop_t *) cropp;
    }
    notifyShutter(crop,FALSE);

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG) {
        // Find the offset within the heap of the current buffer.
        offset_addr = (ssize_t)postviewframe->buffer - (ssize_t)mPostviewHeap->mHeap->base();
        offset = offset_addr / mPostviewHeap->mAlignedBufferSize;
        if(mUseOverlay && !mZslPanorama) {
            mOverlayLock.lock();
            if(mOverlay != NULL) {
                mOverlay->setFd(mPostviewHeap->mHeap->getHeapID());
                if(cropp != NULL){
                    crop = (common_crop_t *)cropp;
                    if (crop->in1_w != 0 && crop->in1_h != 0) {
                        int x = (crop->out1_w - crop->in1_w + 1) / 2 - 1;
                        int y = (crop->out1_h - crop->in1_h + 1) / 2 - 1;
                        int w = crop->in1_w;
                        int h = crop->in1_h;
                        if(x < 0) x = 0;
                        if(y < 0) y = 0;

                        mIs3DModeOn ?
                            mOverlay->setCrop(x, y, 2 * w, h) :
                            mOverlay->setCrop(x, y, w, h);

                        mResetOverlayCrop = true;
                    }else {
                        mIs3DModeOn ?
                            mOverlay->setCrop(0, 0, 
                                      2 * mPostviewWidth, mPostviewHeight) :
                            mOverlay->setCrop(0, 0, 
                                      mPostviewWidth, mPostviewHeight);
                    }
                }
                mOverlay->queueBuffer((void *)offset_addr);
            }
            mOverlayLock.unlock();
        }
        if(mCurrentTarget == TARGET_MSM7627 ||
          (mCurrentTarget == TARGET_MSM7625A ||
           mCurrentTarget == TARGET_MSM7627A)) {
            /* Give the postview buffer to upper layers */
            if (crop->in1_w != 0 && crop->in1_h != 0) {
                crop_yuv420(crop->out1_w, crop->out1_h, crop->in1_w, crop->in1_h,
                                          (uint8_t *)mPostviewHeap->mHeap->base(), mPostviewHeap->mName);
            }
            if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE))
                mDataCallback(CAMERA_MSG_RAW_IMAGE, mPostviewHeap->mBuffers[0],
                                mCallbackCookie);
        }
        else {
            /* Give the main Image as raw to upper layers */
            if(!mZslEnable) {
                offset_addr = (ssize_t)mainframe->buffer - (ssize_t)mRawHeap->mHeap->base();
                offset = offset_addr / mRawHeap->mAlignedBufferSize;
                if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE))
                    mDataCallback(CAMERA_MSG_RAW_IMAGE, mRawHeap->mBuffers[offset],
                            mCallbackCookie);
            }
            if(strTexturesOn == true) {
                LOGI("Raw Data given to app for processing...will wait for jpeg encode call");
                mEncodePendingWaitLock.lock();
                mEncodePending = true;
                mEncodePendingWaitLock.unlock();
                mJpegThreadWaitLock.lock();
                mJpegThreadRunning = false;
                mJpegThreadWait.signal();
                mJpegThreadWaitLock.unlock();
            }
        }
    }else {
        if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE))
                mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, mRawSnapShotPmemHeap->mBuffers[0],
                           mCallbackCookie);
        mJpegThreadWaitLock.lock();
        mJpegThreadRunning = false;
        mJpegThreadWait.signal();
        mJpegThreadWaitLock.unlock();
        //cleanup
        deinitRawSnapshot();
    }

    /* can start preview at this stage? early preview? */
    mInSnapshotModeWaitLock.lock();
    mInSnapshotMode = false;
    mInSnapshotModeWait.signal();
    mInSnapshotModeWaitLock.unlock();

    LOGV("%s: X", __FUNCTION__);
}


void QualcommCameraHardware::receiveJpegPicture(status_t status, mm_camera_buffer_t *encoded_buffer)
{
    Mutex::Autolock cbLock(&mCallbackLock);
    numJpegReceived++;
    uint32_t offset ;
    int32_t index = -1;
    int32_t buffer_size = 0;
    if(encoded_buffer && status == NO_ERROR) {
      buffer_size = encoded_buffer->filled_size;
      LOGV("receiveJpegPicture: E image (%d uint8_ts out of %d)",
        buffer_size, mJpegHeap->mBufferSize);

      offset = (uint32_t)encoded_buffer->ptr - (uint32_t)mJpegHeap->mHeap->base();
      LOGE("address of Jpeg %d encoded buf %u Jpeg Heap base %u", offset,
         (uint32_t)encoded_buffer->ptr, (uint32_t)mJpegHeap->mHeap->base());

      index = offset/ mJpegHeap->mBufferSize;
      if(buffer_size && (buffer_size <= mJpegHeap->mBufferSize)){
          mJpegHeap->mFrameSize = buffer_size;
      }
    }
    if((index < 0) || (index >= (MAX_SNAPSHOT_BUFFERS-2))){
        LOGE("Jpeg index is not valid or fails. ");
        if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
          mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, NULL, mCallbackCookie);
        }
        mJpegThreadWaitLock.lock();
        mJpegThreadRunning = false;
        mJpegThreadWait.signal();
        mJpegThreadWaitLock.unlock();
    } else {
      LOGV("receiveJpegPicture: Index of Jpeg is %d",index);

      if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
          // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
          // that the JPEG image's size will probably change from one snapshot
          // to the next, so we cannot reuse the MemoryBase object.
          sp<MemoryBase> buffer = new
              MemoryBase(mJpegHeap->mHeap,
                         index * mJpegHeap->mBufferSize +
                         0,
                         buffer_size);
          if(status == NO_ERROR)
              mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
          buffer = NULL;
      } else {
        LOGV("JPEG callback was cancelled--not delivering image.");
      }
      if(numJpegReceived == numCapture){
          mJpegThreadWaitLock.lock();
          mJpegThreadRunning = false;
          mJpegThreadWait.signal();
          mJpegThreadWaitLock.unlock();
      }
    }

    LOGV("receiveJpegPicture: X callback done.");
}

bool QualcommCameraHardware::previewEnabled()
{
    /* If overlay is used the message CAMERA_MSG_PREVIEW_FRAME would
     * be disabled at CameraService layer. Hence previewEnabled would
     * return FALSE even though preview is running. Hence check for
     * mOverlay not being NULL to ensure that previewEnabled returns
     * accurate information.
     */
    if(mZslEnable)
        return false;

    return mCameraRunning && mDataCallback &&
           ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mOverlay != NULL));
}
status_t QualcommCameraHardware::setRecordSize(const CameraParameters& params)
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
            if( (mCurrentTarget != TARGET_MSM7630)
                && (mCurrentTarget != TARGET_QSD8250)
                 && (mCurrentTarget != TARGET_MSM8660) ) {
                //For Single VFE output targets, use record dimensions as preview dimensions.
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
    return NO_ERROR;
}

status_t QualcommCameraHardware::setPreviewSize(const CameraParameters& params)
{
    int width, height;
    params.getPreviewSize(&width, &height);
    LOGV("requested preview size %d x %d", width, height);

    // Validate the preview size
    for (size_t i = 0; i <  PREVIEW_SIZE_COUNT; ++i) {
        if (width ==  preview_sizes[i].width
           && height ==  preview_sizes[i].height) {
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
status_t QualcommCameraHardware::setPreviewFpsRange(const CameraParameters& params)
{
    int minFps,maxFps;
    params.getPreviewFpsRange(&minFps,&maxFps);
    LOGE("FPS Range Values: %dx%d", minFps, maxFps);

    for(size_t i=0;i<FPS_RANGES_SUPPORTED_COUNT;i++)
    {
        if(minFps==FpsRangesSupported[i].minFPS && maxFps == FpsRangesSupported[i].maxFPS){
            mParameters.setPreviewFpsRange(minFps,maxFps);
            return NO_ERROR;
        }
    }
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRate(const CameraParameters& params)
{
    if( !mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS)){
         LOGI("Set fps is not supported for this sensor");
        return NO_ERROR;
    }
    uint16_t previousFps = (uint16_t)mParameters.getPreviewFrameRate();
    uint16_t fps = (uint16_t)params.getPreviewFrameRate();
    LOGV("requested preview frame rate  is %u", fps);

    if(mInitialized && (fps == previousFps)){
        LOGV("fps same as previous fps");
        return NO_ERROR;
    }

    if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
        mParameters.setPreviewFrameRate(fps);
        bool ret = native_set_parms(CAMERA_PARM_FPS,
                sizeof(fps), (void *)&fps);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRateMode(const CameraParameters& params) {
    if( !mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS_MODE) &&  !mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS)){
         LOGI("set fps mode is not supported for this sensor");
        return NO_ERROR;
    }

    const char *previousMode = mParameters.getPreviewFrameRateMode();
    const char *str = params.getPreviewFrameRateMode();
    if( mInitialized && !strcmp(previousMode, str)) {
        LOGV("frame rate mode same as previous mode %s", previousMode);
        return NO_ERROR;
    }
    int32_t frameRateMode = attr_lookup(frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map),str);
    if(frameRateMode != NOT_FOUND) {
        LOGV("setPreviewFrameRateMode: %s ", str);
        mParameters.setPreviewFrameRateMode(str);
        bool ret = native_set_parms(CAMERA_PARM_FPS_MODE, sizeof(frameRateMode), (void *)&frameRateMode);
        if(!ret) return ret;
        //set the fps value when chaging modes
        int16_t fps = (uint16_t)params.getPreviewFrameRate();
        if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
            mParameters.setPreviewFrameRate(fps);
            ret = native_set_parms(CAMERA_PARM_FPS,
                                        sizeof(fps), (void *)&fps);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        LOGE("Invalid preview frame rate value: %d", fps);
        return BAD_VALUE;
    }
    LOGE("Invalid preview frame rate mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setJpegThumbnailSize(const CameraParameters& params){
    int width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int height = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    LOGV("requested jpeg thumbnail size %d x %d", width, height);

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

bool QualcommCameraHardware::updatePictureDimension(const CameraParameters& params, int& width, int& height)
{
    bool retval = false;
    int previewWidth, previewHeight;
    params.getPreviewSize(&previewWidth, &previewHeight);
    LOGV("updatePictureDimension: %dx%d <- %dx%d", width, height,
      previewWidth, previewHeight);
    if ((width < previewWidth) && (height < previewHeight)) {
        mUseJpegDownScaling = true;
        mActualPictWidth = width;
        width = previewWidth;
        mActualPictHeight = height;
        height = previewHeight;
        retval = true;
    } else
        mUseJpegDownScaling = false;
    return retval;
}

status_t QualcommCameraHardware::setPictureSize(const CameraParameters& params)
{
    int width, height;
    params.getPictureSize(&width, &height);
    LOGV("requested picture size %d x %d", width, height);

    // Validate the picture size
    for (int i = 0; i < supportedPictureSizesCount; ++i) {
        if (width == picture_sizes_ptr[i].width
          && height == picture_sizes_ptr[i].height) {
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

status_t QualcommCameraHardware::setJpegQuality(const CameraParameters& params) {
    status_t rc = NO_ERROR;
    int quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
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
    return rc;
}

status_t QualcommCameraHardware::setEffect(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_EFFECT);
    int result;

    if (str != NULL) {
        int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
           if( !mCfgControl.mm_camera_is_parm_supported(CAMERA_PARM_EFFECT, (void *) &value)){
               LOGE("Camera Effect - %s mode is not supported for this sensor",str);
               return NO_ERROR;
           }else {
               mParameters.set(CameraParameters::KEY_EFFECT, str);
               bool ret = native_set_parms(CAMERA_PARM_EFFECT, sizeof(value),
                                           (void *)&value,(int *)&result);
                if(result == MM_CAMERA_ERR_INVALID_OPERATION) {
                    LOGI("Camera Effect: %s is not set as the selected value is not supported ", str);
                }
               return ret ? NO_ERROR : UNKNOWN_ERROR;
          }
        }
    }
    LOGE("Invalid effect value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setExposureCompensation(
        const CameraParameters & params){
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_EXPOSURE_COMPENSATION)) {
        LOGI("Exposure Compensation is not supported for this sensor");
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
       bool ret = native_set_parms(CAMERA_PARM_EXPOSURE_COMPENSATION,
                                    sizeof(value), (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    LOGE("Invalid Exposure Compensation");
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setAutoExposure(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_EXPOSURE)) {
        LOGI("Auto Exposure not supported for this sensor");
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_AUTO_EXPOSURE);
    if (str != NULL) {
        int32_t value = attr_lookup(autoexposure, sizeof(autoexposure) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE, str);
            bool ret = native_set_parms(CAMERA_PARM_EXPOSURE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid auto exposure value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setSharpness(const CameraParameters& params)
{
     if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_SHARPNESS)) {
        LOGI("Sharpness not supported for this sensor");
        return NO_ERROR;
    }
    int sharpness = params.getInt(CameraParameters::KEY_SHARPNESS);
    if((sharpness < CAMERA_MIN_SHARPNESS
            || sharpness > CAMERA_MAX_SHARPNESS))
        return UNKNOWN_ERROR;

    LOGV("setting sharpness %d", sharpness);
    mParameters.set(CameraParameters::KEY_SHARPNESS, sharpness);
    bool ret = native_set_parms(CAMERA_PARM_SHARPNESS, sizeof(sharpness),
                               (void *)&sharpness);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setContrast(const CameraParameters& params)
{
     if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_CONTRAST)) {
        LOGI("Contrast not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if(value == CAMERA_BESTSHOT_OFF) {
        int contrast = params.getInt(CameraParameters::KEY_CONTRAST);
        if((contrast < CAMERA_MIN_CONTRAST)
                || (contrast > CAMERA_MAX_CONTRAST))
            return UNKNOWN_ERROR;

        LOGV("setting contrast %d", contrast);
        mParameters.set(CameraParameters::KEY_CONTRAST, contrast);
        bool ret = native_set_parms(CAMERA_PARM_CONTRAST, sizeof(contrast),
                                   (void *)&contrast);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
          LOGI(" Contrast value will not be set " \
          "when the scenemode selected is %s", str);
    return NO_ERROR;
    }
}

status_t QualcommCameraHardware::setSaturation(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_SATURATION)) {
        LOGI("Saturation not supported for this sensor");
        return NO_ERROR;
    }
    int result;
    int saturation = params.getInt(CameraParameters::KEY_SATURATION);

    if((saturation < CAMERA_MIN_SATURATION)
        || (saturation > CAMERA_MAX_SATURATION))
    return UNKNOWN_ERROR;

    LOGV("Setting saturation %d", saturation);
    mParameters.set(CameraParameters::KEY_SATURATION, saturation);
    bool ret = native_set_parms(CAMERA_PARM_SATURATION, sizeof(saturation),
		(void *)&saturation, (int *)&result);
    if(result == MM_CAMERA_ERR_INVALID_OPERATION)
        LOGI("Saturation Value: %d is not set as the selected value is not supported", saturation);

    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setPreviewFormat(const CameraParameters& params) {
    const char *str = params.getPreviewFormat();
    int32_t previewFormat = attr_lookup(preview_formats, sizeof(preview_formats) / sizeof(str_map), str);
    if(previewFormat != NOT_FOUND) {
        mParameters.set(CameraParameters::KEY_PREVIEW_FORMAT, str);
        mPreviewFormat = previewFormat;
        bool ret = native_set_parms(CAMERA_PARM_PREVIEW_FORMAT, sizeof(previewFormat),
                                   (void *)&previewFormat);
        return NO_ERROR;
    }
    LOGE("Invalid preview format value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setStrTextures(const CameraParameters& params) {
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
            if((mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660))
                mUseOverlay = true;
        }
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setBrightness(const CameraParameters& params) {
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_BRIGHTNESS)) {
        LOGI("Set Brightness not supported for this sensor");
        return NO_ERROR;
    }
    int brightness = params.getInt("luma-adaptation");
    if (mBrightness !=  brightness) {
        LOGV(" new brightness value : %d ", brightness);
        mBrightness =  brightness;
        mParameters.set("luma-adaptation", brightness);
    bool ret = native_set_parms(CAMERA_PARM_BRIGHTNESS, sizeof(mBrightness),
                                   (void *)&mBrightness);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setSkinToneEnhancement(const CameraParameters& params) {
     if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_SCE_FACTOR)) {
        LOGI("SkinToneEnhancement not supported for this sensor");
        return NO_ERROR;
     }
     int skinToneValue = params.getInt("skinToneEnhancement");
     if (mSkinToneEnhancement != skinToneValue) {
          LOGV(" new skinTone correction value : %d ", skinToneValue);
          mSkinToneEnhancement = skinToneValue;
          mParameters.set("skinToneEnhancement", skinToneValue);
          bool ret = native_set_parms(CAMERA_PARM_SCE_FACTOR, sizeof(mSkinToneEnhancement),
                        (void *)&mSkinToneEnhancement);
          return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setWhiteBalance(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_WHITE_BALANCE)) {
        LOGI("WhiteBalance not supported for this sensor");
        return NO_ERROR;
    }

    int result;

    const char *str = params.get(CameraParameters::KEY_WHITE_BALANCE);
    if (str != NULL) {
        int32_t value = attr_lookup(whitebalance, sizeof(whitebalance) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_WHITE_BALANCE, str);
            bool ret = native_set_parms(CAMERA_PARM_WHITE_BALANCE, sizeof(value),
                                       (void *)&value, (int *)&result);
            if(result == MM_CAMERA_ERR_INVALID_OPERATION) {
                LOGI("WhiteBalance Value: %s is not set as the selected value is not supported ", str);
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
        LOGE("Invalid whitebalance value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
}

status_t QualcommCameraHardware::setFlash(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_LED_MODE)) {
        LOGI("%s: flash not supported", __FUNCTION__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_FLASH_MODE);
    if (str != NULL) {
        int32_t value = attr_lookup(flash, sizeof(flash) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FLASH_MODE, str);
            bool ret = native_set_parms(CAMERA_PARM_LED_MODE,
                                       sizeof(value), (void *)&value);
            if(mZslEnable && (value != LED_MODE_OFF)){
                    mParameters.set("num-snaps-per-shutter", "1");
                    numCapture = 1;
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid flash mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setOverlayFormats(const CameraParameters& params)
{
    int ovFormat;
    if(mIs3DModeOn) {
        ovFormat = HAL_3D_IN_SIDE_BY_SIDE_L_R|HAL_3D_OUT_SIDE_BY_SIDE;
        mInSnapshotMode ?
            ovFormat |= HAL_PIXEL_FORMAT_YCbCr_420_SP:
            ovFormat |= HAL_PIXEL_FORMAT_YCrCb_420_SP;
    }
    else {
        ovFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }

    mParameters.set("overlay-format", ovFormat);

    return NO_ERROR;
}

status_t QualcommCameraHardware::setAntibanding(const CameraParameters& params)
{   int result;
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_ANTIBANDING)) {
        LOGI("Parameter AntiBanding is not supported for this sensor");
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_ANTIBANDING);
    if (str != NULL) {
        int value = (camera_antibanding_type)attr_lookup(
          antibanding, sizeof(antibanding) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            camera_antibanding_type temp = (camera_antibanding_type) value;
            mParameters.set(CameraParameters::KEY_ANTIBANDING, str);
            bool ret = native_set_parms(CAMERA_PARM_ANTIBANDING,
                       sizeof(camera_antibanding_type), (void *)&temp ,(int *)&result);
            if(result == MM_CAMERA_ERR_INVALID_OPERATION) {
                LOGI("AntiBanding Value: %s is not supported for the given BestShot Mode", str);
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid antibanding value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setMCEValue(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_MCE)) {
        LOGI("Parameter MCE is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT);
    if (str != NULL) {
        int value = attr_lookup(mce, sizeof(mce) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int8_t temp = (int8_t)value;
            LOGI("%s: setting MCE value of %s", __FUNCTION__, str);
            mParameters.set(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT, str);

            native_set_parms(CAMERA_PARM_MCE, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid MCE value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setHighFrameRate(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_HFR)) {
        LOGI("Parameter HFR is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
    if (str != NULL) {
        int value = attr_lookup(hfr, sizeof(hfr) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int32_t temp = (int32_t)value;
            LOGI("%s: setting HFR value of %s(%d)", __FUNCTION__, str, temp);
            //Check for change in HFR value
            const char *oldHfr = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
            if(strcmp(oldHfr, str)){
                LOGI("%s: old HFR: %s, new HFR %s", __FUNCTION__, oldHfr, str);
                mParameters.set(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE, str);
                mHFRMode = true;
                if(mCameraRunning == true) {
                    mHFRThreadWaitLock.lock();
                    pthread_attr_t pattr;
                    pthread_attr_init(&pattr);
                    pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
                    mHFRThreadRunning = !pthread_create(&mHFRThread,
                                      &pattr,
                                      hfr_thread,
                                      (void*)NULL);
                    mHFRThreadWaitLock.unlock();
                    return NO_ERROR;
                }
            }
            native_set_parms(CAMERA_PARM_HFR, sizeof(int32_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid HFR value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setLensshadeValue(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_ROLLOFF)) {
        LOGI("Parameter Rolloff is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_LENSSHADE);
    if (str != NULL) {
        int value = attr_lookup(lensshade,
                                    sizeof(lensshade) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int8_t temp = (int8_t)value;
            mParameters.set(CameraParameters::KEY_LENSSHADE, str);

            native_set_parms(CAMERA_PARM_ROLLOFF, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid lensShade value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setSelectableZoneAf(const CameraParameters& params)
{
    if(mHasAutoFocusSupport && supportsSelectableZoneAf()) {
        const char *str = params.get(CameraParameters::KEY_SELECTABLE_ZONE_AF);
        if (str != NULL) {
            int32_t value = attr_lookup(selectable_zone_af, sizeof(selectable_zone_af) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF, str);
                bool ret = native_set_parms(CAMERA_PARM_FOCUS_RECT, sizeof(value),
                        (void *)&value);
                return ret ? NO_ERROR : UNKNOWN_ERROR;
            }
        }
        LOGE("Invalid selectable zone af value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setTouchAfAec(const CameraParameters& params)
{
    if(mHasAutoFocusSupport){
        int xAec, yAec, xAf, yAf;

        params.getTouchIndexAec(&xAec, &yAec);
        params.getTouchIndexAf(&xAf, &yAf);
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
                }
                else {
                    //Set Touch AEC params
                    aec_roi_value.aec_roi_enable = AEC_ROI_OFF;
                    aec_roi_value.aec_roi_type = AEC_ROI_BY_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.x = DONT_CARE_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.y = DONT_CARE_COORDINATE;

                    //Set Touch AF params
                    af_roi_value.num_roi = 0;
                }
                native_set_parms(CAMERA_PARM_AEC_ROI, sizeof(cam_set_aec_roi_t), (void *)&aec_roi_value);
                native_set_parms(CAMERA_PARM_AF_ROI, sizeof(roi_info_t), (void*)&af_roi_value);
            }
            return NO_ERROR;
        }
        LOGE("Invalid Touch AF/AEC value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setFaceDetection(const char *str)
{
    if(supportsFaceDetection() == false){
        LOGI("Face detection is not enabled");
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

status_t QualcommCameraHardware::setRedeyeReduction(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_REDEYE_REDUCTION)) {
        LOGI("Parameter Redeye Reduction is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_REDEYE_REDUCTION);
    if (str != NULL) {
        int value = attr_lookup(redeye_reduction, sizeof(redeye_reduction) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            int8_t temp = (int8_t)value;
            LOGI("%s: setting Redeye Reduction value of %s", __FUNCTION__, str);
            mParameters.set(CameraParameters::KEY_REDEYE_REDUCTION, str);

            native_set_parms(CAMERA_PARM_REDEYE_REDUCTION, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid Redeye Reduction value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t  QualcommCameraHardware::setISOValue(const CameraParameters& params) {
    int8_t temp_hjr;
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_ISO)) {
            LOGE("Parameter ISO Value is not supported for this sensor");
            return NO_ERROR;
        }
    const char *str = params.get(CameraParameters::KEY_ISO_MODE);
    if (str != NULL) {
        int value = (camera_iso_mode_type)attr_lookup(
          iso, sizeof(iso) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            camera_iso_mode_type temp = (camera_iso_mode_type) value;
            if (value == CAMERA_ISO_DEBLUR) {
               temp_hjr = true;
               native_set_parms(CAMERA_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
               mHJR = value;
            }
            else {
               if (mHJR == CAMERA_ISO_DEBLUR) {
                   temp_hjr = false;
                   native_set_parms(CAMERA_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
                   mHJR = value;
               }
            }

            mParameters.set(CameraParameters::KEY_ISO_MODE, str);
            native_set_parms(CAMERA_PARM_ISO, sizeof(camera_iso_mode_type), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid Iso value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setSceneDetect(const CameraParameters& params)
{

    bool retParm1, retParm2;
    if (supportsSceneDetection()) {
        if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_BL_DETECTION) && !mCfgControl.mm_camera_is_supported(CAMERA_PARM_SNOW_DETECTION)) {
            LOGE("Parameter Auto Scene Detection is not supported for this sensor");
            return NO_ERROR;
        }
        const char *str = params.get(CameraParameters::KEY_SCENE_DETECT);
        if (str != NULL) {
            int32_t value = attr_lookup(scenedetect, sizeof(scenedetect) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                mParameters.set(CameraParameters::KEY_SCENE_DETECT, str);

                retParm1 = native_set_parms(CAMERA_PARM_BL_DETECTION, sizeof(value),
                                           (void *)&value);

                retParm2 = native_set_parms(CAMERA_PARM_SNOW_DETECTION, sizeof(value),
                                           (void *)&value);

                //All Auto Scene detection modes should be all ON or all OFF.
                if(retParm1 == false || retParm2 == false) {
                    value = !value;
                    retParm1 = native_set_parms(CAMERA_PARM_BL_DETECTION, sizeof(value),
                                               (void *)&value);

                    retParm2 = native_set_parms(CAMERA_PARM_SNOW_DETECTION, sizeof(value),
                                               (void *)&value);
                }
                return (retParm1 && retParm2) ? NO_ERROR : UNKNOWN_ERROR;
            }
        }
    LOGE("Invalid auto scene detection value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setSceneMode(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_BESTSHOT_MODE)) {
        LOGE("Parameter Scenemode is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);

    if (str != NULL) {
        int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_SCENE_MODE, str);
            bool ret = native_set_parms(CAMERA_PARM_BESTSHOT_MODE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid scenemode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}
status_t QualcommCameraHardware::setGpsLocation(const CameraParameters& params)
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

    return NO_ERROR;
}

status_t QualcommCameraHardware::setRotation(const CameraParameters& params)
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
    return rc;
}

status_t QualcommCameraHardware::setZoom(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_ZOOM)) {
        LOGE("Parameter setZoom is not supported for this sensor");
        return NO_ERROR;
    }
    status_t rc = NO_ERROR;
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
        bool ret = native_set_parms(CAMERA_PARM_ZOOM,
            sizeof(zoom_value), (void *)&zoom_value);
        rc = ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
        rc = BAD_VALUE;
    }

    return rc;
}

status_t QualcommCameraHardware::setDenoise(const CameraParameters& params)
{
    if(!mCfgControl.mm_camera_is_supported(CAMERA_PARM_WAVELET_DENOISE)) {
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
        bool ret = native_set_parms(CAMERA_PARM_WAVELET_DENOISE, sizeof(value),
                                               (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        return NO_ERROR;
    }
    LOGE("Invalid Denoise value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setZslParam(const CameraParameters& params)
{
    if(!mZslEnable) {
        LOGV("Zsl is not enabled");
        return NO_ERROR;
    }
    /* This ensures that restart of Preview doesnt happen when taking
     * Snapshot for continuous viewfinder */
    const char *str = params.get("continuous-temporal-bracketing");
    if(str !=NULL) {
        if(!strncmp(str, "enable", 8))
            mZslPanorama = true;
        else
            mZslPanorama = false;
        return NO_ERROR;
    }
    mZslPanorama = false;
    return NO_ERROR;
}

status_t QualcommCameraHardware::setSnapshotCount(const CameraParameters& params)
{
    if(!mZslEnable){
        numCapture = 1;
    } else {
        /* ZSL case */
        const char *str = params.get("num-snaps-per-shutter");
        if (str != NULL) {
            char snapshotCount[5];
            int value = atoi(str);
            if(value > MAX_SNAPSHOT_BUFFERS -2)
                value = MAX_SNAPSHOT_BUFFERS -2;
            else if(value < 1)
                value = 1;
            sprintf(snapshotCount,"%d",value);
            numCapture = value;
            mParameters.set("num-snaps-per-shutter", snapshotCount);
        }
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::updateFocusDistances(const char *focusmode)
{
    LOGV("%s: IN", __FUNCTION__);
    focus_distances_info_t focusDistances;
    if( mCfgControl.mm_camera_get_parm(CAMERA_PARM_FOCUS_DISTANCES,
        (void *)&focusDistances) == MM_CAMERA_SUCCESS) {
        String8 str;
        char buffer[32];
        sprintf(buffer, "%f", focusDistances.focus_distance[0]);
        str.append(buffer);
        sprintf(buffer, ",%f", focusDistances.focus_distance[1]);
        str.append(buffer);
        if(strcmp(focusmode, CameraParameters::FOCUS_MODE_INFINITY) == 0)
            sprintf(buffer, ",%s", "Infinity");
        else
            sprintf(buffer, ",%f", focusDistances.focus_distance[2]);
        str.append(buffer);
        LOGI("%s: setting KEY_FOCUS_DISTANCES as %s", __FUNCTION__, str.string());
        mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES, str.string());
        return NO_ERROR;
    }
    LOGE("%s: get CAMERA_PARM_FOCUS_DISTANCES failed!!!", __FUNCTION__);
    return UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setFocusMode(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_FOCUS_MODE);
    if (str != NULL) {
        int32_t value = attr_lookup(focus_modes,
                                    sizeof(focus_modes) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FOCUS_MODE, str);

            if(mHasAutoFocusSupport && (updateFocusDistances(str) != NO_ERROR)) {
                LOGE("%s: updateFocusDistances failed for %s", __FUNCTION__, str);
                return UNKNOWN_ERROR;
            }

            if(mHasAutoFocusSupport){
                int cafSupport = FALSE;
                if(!strcmp(str, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO)){
                    cafSupport = TRUE;
                }
                LOGV("Continuous Auto Focus %d", cafSupport);
                native_set_parms(CAMERA_PARM_CONTINUOUS_AF, sizeof(int8_t), (void *)&cafSupport);
            }
            // Focus step is reset to infinity when preview is started. We do
            // not need to do anything now.
            return NO_ERROR;
        }
    }
    LOGE("Invalid focus mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setOrientation(const CameraParameters& params)
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

status_t QualcommCameraHardware::setPictureFormat(const CameraParameters& params)
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

QualcommCameraHardware::MMCameraDL::MMCameraDL(){
    LOGV("MMCameraDL: E");
    libmmcamera = NULL;
#if DLOPEN_LIBMMCAMERA
    libmmcamera = ::dlopen("liboemcamera.so", RTLD_NOW);
#endif
    LOGV("Open MM camera DL libeomcamera loaded at %p ", libmmcamera);
    LOGV("MMCameraDL: X");
}

void * QualcommCameraHardware::MMCameraDL::pointer(){
    return libmmcamera;
}

QualcommCameraHardware::MMCameraDL::~MMCameraDL(){
    LOGV("~MMCameraDL: E");
    LINK_mm_camera_destroy();
    if (libmmcamera != NULL) {
        ::dlclose(libmmcamera);
        LOGV("closed MM Camera DL ");
    }
    libmmcamera = NULL;
    LOGV("~MMCameraDL: X");
}

wp<QualcommCameraHardware::MMCameraDL> QualcommCameraHardware::MMCameraDL::instance;
Mutex QualcommCameraHardware::MMCameraDL::singletonLock;


sp<QualcommCameraHardware::MMCameraDL> QualcommCameraHardware::MMCameraDL::getInstance(){
    Mutex::Autolock instanceLock(singletonLock);
    sp<MMCameraDL> mmCamera = instance.promote();
    if(mmCamera == NULL){
        mmCamera = new MMCameraDL();
        instance = mmCamera;
    }
    return mmCamera;
}

QualcommCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                         int frame_size,
                                         const char *name) :
    mBufferSize(buffer_size),
    mNumBuffers(num_buffers),
    mFrameSize(frame_size),
    mBuffers(NULL), mName(name)
{
    int page_size_minus_1 = getpagesize() - 1;
    mAlignedBufferSize = (buffer_size + page_size_minus_1) & (~page_size_minus_1);
}

void QualcommCameraHardware::MemPool::completeInitialization()
{
    // If we do not know how big the frame will be, we wait to allocate
    // the buffers describing the individual frames until we do know their
    // size.

    if (mFrameSize > 0) {
        mBuffers = new sp<MemoryBase>[mNumBuffers];
        for (int i = 0; i < mNumBuffers; i++) {
            mBuffers[i] = new
                MemoryBase(mHeap,
                           i * mAlignedBufferSize,
                           mFrameSize);
        }
    }
}

QualcommCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                               int frame_size,
                                               const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    name)
{
    LOGV("constructing MemPool %s backed by ashmem: "
         "%d frames @ %d uint8_ts, "
         "buffer size %d",
         mName,
         num_buffers, frame_size, buffer_size);

    int page_mask = getpagesize() - 1;
    int ashmem_size = buffer_size * num_buffers;
    ashmem_size += page_mask;
    ashmem_size &= ~page_mask;

    mHeap = new MemoryHeapBase(ashmem_size);

    completeInitialization();
}

bool QualcommCameraHardware::register_record_buffers(bool register_buffer) {
    LOGI("%s: (%d) E", __FUNCTION__, register_buffer);
    struct msm_pmem_info pmemBuf;

    for (int cnt = 0; cnt < kRecordBufferCount; ++cnt) {
        pmemBuf.type     = MSM_PMEM_VIDEO;
        pmemBuf.fd       = mRecordHeap->mHeap->getHeapID();
        pmemBuf.offset   = mRecordHeap->mAlignedBufferSize * cnt;
        pmemBuf.len      = mRecordHeap->mBufferSize;
        pmemBuf.vaddr    = (uint8_t *)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
        pmemBuf.y_off    = 0;
        pmemBuf.cbcr_off = recordframes[0].cbcr_off;
        if(register_buffer == true) {
            pmemBuf.active   = (cnt<ACTIVE_VIDEO_BUFFERS);
            if( (mVpeEnabled) && (cnt == kRecordBufferCount-1)) {
                pmemBuf.type = MSM_PMEM_VIDEO_VPE;
                pmemBuf.active = 1;
            }
        } else {
            pmemBuf.active   = false;
        }

        LOGV("register_buf:  reg = %d buffer = %p", !register_buffer,
          (void *)pmemBuf.vaddr);
        if(native_start_ops(register_buffer ? CAMERA_OPS_REGISTER_BUFFER :
                CAMERA_OPS_UNREGISTER_BUFFER ,(void *)&pmemBuf) < 0) {
            LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM  error %s",
                strerror(errno));
            return false;
        }
    }
    return true;
}

static bool register_buf(int size,
                         int frame_size,
                         int cbcr_offset,
                         int yoffset,
                         int pmempreviewfd,
                         uint32_t offset,
                         uint8_t *buf,
                         int pmem_type,
                         bool vfe_can_write,
                         bool register_buffer = true);

QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                           int flags,
                                           int pmem_type,
                                           int buffer_size, int num_buffers,
                                           int frame_size, int cbcr_offset,
                                           int yOffset, const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    name),
    mPmemType(pmem_type),
    mCbCrOffset(cbcr_offset),
    myOffset(yOffset)
{
    LOGI("constructing MemPool %s backed by pmem pool %s: "
         "%d frames @ %d bytes, buffer size %d",
         mName,
         pmem_pool, num_buffers, frame_size,
         buffer_size);

    mMMCameraDLRef = QualcommCameraHardware::MMCameraDL::getInstance();


    // Make a new mmap'ed heap that can be shared across processes.
    // mAlignedBufferSize is already in 4k aligned. (do we need total size necessary to be in power of 2??)
    mAlignedSize = mAlignedBufferSize * num_buffers;

    sp<MemoryHeapBase> masterHeap =
        new MemoryHeapBase(pmem_pool, mAlignedSize, flags);

    if (masterHeap->getHeapID() < 0) {
        LOGE("failed to construct master heap for pmem pool %s", pmem_pool);
        masterHeap.clear();
        return;
    }

    sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, flags);
    if (pmemHeap->getHeapID() >= 0) {
        pmemHeap->slap();
        masterHeap.clear();
        mHeap = pmemHeap;
        pmemHeap.clear();

        mFd = mHeap->getHeapID();
        if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
            LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                 pmem_pool,
                 ::strerror(errno), errno);
            mHeap.clear();
            return;
        }

        LOGV("pmem pool %s ioctl(fd = %d, PMEM_GET_SIZE) is %ld",
             pmem_pool,
             mFd,
             mSize.len);
        LOGD("mBufferSize=%d, mAlignedBufferSize=%d\n", mBufferSize, mAlignedBufferSize);
        // Unregister preview buffers with the camera drivers.  Allow the VFE to write
        // to all preview buffers except for the last one.
        // Only Register the preview, snapshot and thumbnail buffers with the kernel.
        if( (strcmp("postview", mName) != 0) ){
            int num_buf = num_buffers;
            if(!strcmp("preview", mName)) num_buf = kPreviewBufferCount;
            LOGD("num_buffers = %d", num_buf);
            for (int cnt = 0; cnt < num_buf; ++cnt) {
                int active = 1;
                if(pmem_type == MSM_PMEM_VIDEO){
                     active = (cnt<ACTIVE_VIDEO_BUFFERS);
                     //When VPE is enabled, set the last record
                     //buffer as active and pmem type as PMEM_VIDEO_VPE
                     //as this is a requirement from VPE operation.
                     //No need to set this pmem type to VIDEO_VPE while unregistering,
                     //because as per camera stack design: "the VPE AXI is also configured
                     //when VFE is configured for VIDEO, which is as part of preview
                     //initialization/start. So during this VPE AXI config camera stack
                     //will lookup the PMEM_VIDEO_VPE buffer and give it as o/p of VPE and
                     //change it's type to PMEM_VIDEO".
                     if( (mVpeEnabled) && (cnt == kRecordBufferCount-1)) {
                         active = 1;
                         pmem_type = MSM_PMEM_VIDEO_VPE;
                     }
                     LOGV(" pmempool creating video buffers : active %d ", active);
                }
                else if (pmem_type == MSM_PMEM_PREVIEW){
                    active = (cnt < ACTIVE_PREVIEW_BUFFERS);
                }
                else if ((pmem_type == MSM_PMEM_MAINIMG)
                     || (pmem_type == MSM_PMEM_THUMBNAIL)){
                    active = (cnt < ACTIVE_ZSL_BUFFERS);
                }
                register_buf(mBufferSize,
                         mFrameSize, mCbCrOffset, myOffset,
                         mHeap->getHeapID(),
                         mAlignedBufferSize * cnt,
                         (uint8_t *)mHeap->base() + mAlignedBufferSize * cnt,
                         pmem_type,
                         active);
            }
        }

        completeInitialization();
    }
    else LOGE("pmem pool %s error: could not create master heap!",
              pmem_pool);
    LOGI("%s: (%s) X ", __FUNCTION__, mName);
}

QualcommCameraHardware::PmemPool::~PmemPool()
{
    LOGI("%s: %s E", __FUNCTION__, mName);
    if (mHeap != NULL) {
        // Unregister preview buffers with the camera drivers.
        //  Only Unregister the preview, snapshot and thumbnail
        //  buffers with the kernel.
        if( (strcmp("postview", mName) != 0) ){
            int num_buffers = mNumBuffers;
            if(!strcmp("preview", mName)) num_buffers = kPreviewBufferCount;
            for (int cnt = 0; cnt < num_buffers; ++cnt) {
                register_buf(mBufferSize,
                         mFrameSize,
                         mCbCrOffset,
                         myOffset,
                         mHeap->getHeapID(),
                         mAlignedBufferSize * cnt,
                         (uint8_t *)mHeap->base() + mAlignedBufferSize * cnt,
                         mPmemType,
                         false,
                         false /* unregister */);
            }
        }
    }
    mMMCameraDLRef.clear();
    LOGI("%s: %s X", __FUNCTION__, mName);
}

QualcommCameraHardware::MemPool::~MemPool()
{
    LOGV("destroying MemPool %s", mName);
    if (mFrameSize > 0)
        delete [] mBuffers;
    mHeap.clear();
    LOGV("destroying MemPool %s completed", mName);
}


status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    CAMERA_HAL_UNUSED(args);
    snprintf(buffer, 255, "QualcommCameraHardware::AshmemPool::dump\n");
    result.append(buffer);
    if (mName) {
        snprintf(buffer, 255, "mem pool name (%s)\n", mName);
        result.append(buffer);
    }
    if (mHeap != 0) {
        snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                 mHeap->getBase(), mHeap->getSize(),
                 mHeap->getFlags(), mHeap->getDevice());
        result.append(buffer);
    }
    snprintf(buffer, 255,
             "buffer size (%d), number of buffers (%d), frame size(%d)",
             mBufferSize, mNumBuffers, mFrameSize);
    result.append(buffer);
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

static void receive_camframe_callback(struct msm_frame *frame)
{
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receivePreviewFrame(frame);
    }
}

static void receive_camstats_callback(camstats_type stype, camera_preview_histogram_info* histinfo)
{
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveCameraStats(stype,histinfo);
    }
}

static void receive_liveshot_callback(liveshot_status status, uint32_t jpeg_size)
{
    if(status == LIVESHOT_SUCCESS) {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveLiveSnapshot(jpeg_size);
        }
    }
    else
        LOGE("Liveshot not succesful");
}


static int8_t receive_event_callback(mm_camera_event* event)
{
    LOGV("%s: E", __FUNCTION__);
    if(event == NULL) {
        LOGE("%s: event is NULL!", __FUNCTION__);
        return FALSE;
    }
    switch(event->event_type) {
        case SNAPSHOT_DONE:
        {
            /* postview buffer is received */
            sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
            if (obj != 0) {

                obj->receiveRawPicture(NO_ERROR, event->event_data.yuv_frames[0],event->event_data.yuv_frames[1]);
            }
        }
        break;
        case SNAPSHOT_FAILED:
        {
            /* postview buffer is received */
            sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
            if (obj != 0) {

                obj->receiveRawPicture(UNKNOWN_ERROR, NULL,NULL);
            }
        }
        break;
        case JPEG_ENC_DONE:
        {
            sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
            if (obj != 0) {
                obj->receiveJpegPicture(NO_ERROR, event->event_data.encoded_frame);
            }
        }
        break;
        case JPEG_ENC_FAILED:
        {
            sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
            if (obj != 0) {
                obj->receiveJpegPicture(UNKNOWN_ERROR, 0);
            }
        }
        break;
        default:
            LOGE("%s: ignore default case", __FUNCTION__);
    }
    return TRUE;
    LOGV("%s: X", __FUNCTION__);
}
// 720p : video frame calbback from camframe
static void receive_camframe_video_callback(struct msm_frame *frame)
{
    LOGV("receive_camframe_video_callback E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
			obj->receiveRecordingFrame(frame);
		 }
    LOGV("receive_camframe_video_callback X");
}

void QualcommCameraHardware::setCallbacks(notify_callback notify_cb,
                             data_callback data_cb,
                             data_callback_timestamp data_cb_timestamp,
                             void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCallback = notify_cb;
    mDataCallback = data_cb;
    mDataCallbackTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void QualcommCameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void QualcommCameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool QualcommCameraHardware::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

bool QualcommCameraHardware::useOverlay(void)
{
    if((mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660)) {
        /* 7x30 and 8x60 supports Overlay */
        mUseOverlay = TRUE;
    } else
        mUseOverlay = FALSE;

    LOGV(" Using Overlay : %s ", mUseOverlay ? "YES" : "NO" );
    return mUseOverlay;
}

status_t QualcommCameraHardware::setOverlay(const sp<Overlay> &Overlay)
{
    if( Overlay != NULL) {
        LOGV(" Valid overlay object ");
        mOverlayLock.lock();
        mOverlay = Overlay;
        mOverlayLock.unlock();
    } else {
        LOGV(" Overlay object NULL. returning ");
        mOverlayLock.lock();
        mOverlay = NULL;
        mOverlayLock.unlock();
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

void QualcommCameraHardware::receive_camframe_error_timeout(void) {
    LOGI("receive_camframe_error_timeout: E");
    Mutex::Autolock l(&mCamframeTimeoutLock);
    LOGE(" Camframe timed out. Not receiving any frames from camera driver ");
    camframe_timeout_flag = TRUE;
    mNotifyCallback(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0,
                    mCallbackCookie);
    LOGI("receive_camframe_error_timeout: X");
}

static void receive_camframe_error_callback(camera_error_type err) {
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        if ((err == CAMERA_ERROR_TIMEOUT) ||
            (err == CAMERA_ERROR_ESD)) {
            /* Handling different error types is dependent on the requirement.
             * Do the same action by default
             */
            obj->receive_camframe_error_timeout();
        }
    }
}

bool QualcommCameraHardware::storePreviewFrameForPostview(void) {
    LOGV("storePreviewFrameForPostview : E ");

    /* Since there is restriction on the maximum overlay dimensions
     * that can be created, we use the last preview frame as postview
     * for 7x30. */
    LOGV("Copying the preview buffer to postview buffer %d  ",
         mPreviewFrameSize);
    if(mLastPreviewFrameHeap == NULL) {
        int CbCrOffset = PAD_TO_WORD(mPreviewFrameSize * 2/3);
        mLastPreviewFrameHeap =
           new PmemPool("/dev/pmem_adsp",
           MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
           MSM_PMEM_PREVIEW, //MSM_PMEM_OUTPUT2,
           mPreviewFrameSize,
           1,
           mPreviewFrameSize,
           CbCrOffset,
           0,
           "postview");

           if (!mLastPreviewFrameHeap->initialized()) {
               mLastPreviewFrameHeap.clear();
               LOGE(" Failed to initialize Postview Heap");
               return false;
            }
    }

    if( mLastPreviewFrameHeap != NULL && mLastQueuedFrame != NULL) {
        memcpy(mLastPreviewFrameHeap->mHeap->base(),
               (uint8_t *)mLastQueuedFrame, mPreviewFrameSize );

        if(mUseOverlay && !mZslPanorama) {
            mOverlayLock.lock();
            if(mOverlay != NULL){
                mOverlay->setFd(mLastPreviewFrameHeap->mHeap->getHeapID());
                if( zoomCropInfo.w !=0 && zoomCropInfo.h !=0) {
                    LOGE("zoomCropInfo non-zero, setting crop ");
                    LOGE("setCrop with %dx%d and %dx%d", zoomCropInfo.x, zoomCropInfo.y, zoomCropInfo.w, zoomCropInfo.h);
                    mOverlay->setCrop(zoomCropInfo.x, zoomCropInfo.y,
                               zoomCropInfo.w, zoomCropInfo.h);
                }
                LOGV("Queueing Postview with last frame till the snapshot is done ");
                mOverlay->queueBuffer((void *)0);
            }
            mOverlayLock.unlock();
        }
    } else
        LOGE("Failed to store Preview frame. No Postview ");
    LOGV("storePreviewFrameForPostview : X ");
    return true;
}

bool QualcommCameraHardware::isValidDimension(int width, int height) {
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
status_t QualcommCameraHardware::getBufferInfo(sp<IMemory>& Frame, size_t *alignedSize) {
    status_t ret;
    LOGV(" getBufferInfo : E ");
    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660) )
    {
	if( mRecordHeap != NULL){
		LOGV(" Setting valid buffer information ");
		Frame = mRecordHeap->mBuffers[0];
		if( alignedSize != NULL) {
			*alignedSize = mRecordHeap->mAlignedBufferSize;
			LOGV(" HAL : alignedSize = %d ", *alignedSize);
			ret = NO_ERROR;
		} else {
	        	LOGE(" HAL : alignedSize is NULL. Cannot update alignedSize ");
	        	ret = UNKNOWN_ERROR;
		}
        } else {
		LOGE(" RecordHeap is null. Buffer information wont be updated ");
		Frame = NULL;
		ret = UNKNOWN_ERROR;
	}
    } else {
	if(mPreviewHeap != NULL) {
		LOGV(" Setting valid buffer information ");
		Frame = mPreviewHeap->mBuffers[0];
		if( alignedSize != NULL) {
			*alignedSize = mPreviewHeap->mAlignedBufferSize;
		        LOGV(" HAL : alignedSize = %d ", *alignedSize);
		        ret = NO_ERROR;
	        } else {
		        LOGE(" HAL : alignedSize is NULL. Cannot update alignedSize ");
		        ret = UNKNOWN_ERROR;
	        }
	} else {
	        LOGE(" PreviewHeap is null. Buffer information wont be updated ");
	        Frame = NULL;
	        ret = UNKNOWN_ERROR;
	}
    }
    LOGV(" getBufferInfo : X ");
    return ret;
}

void QualcommCameraHardware::encodeData() {
    LOGV("encodeData: E");

    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        mJpegThreadWaitLock.lock();
            mJpegThreadRunning = true;
            mJpegThreadWaitLock.unlock();
            mm_camera_ops_type_t current_ops_type = CAMERA_OPS_ENCODE;
            mCamOps.mm_camera_start(current_ops_type,(void *)&mImageCaptureParms,
                                     (void *)&mImageEncodeParms);
            //Wait until jpeg encoding is done and clear the resources.
            mJpegThreadWaitLock.lock();
            while (mJpegThreadRunning) {
                LOGV("encodeData: waiting for jpeg thread to complete.");
                mJpegThreadWait.wait(mJpegThreadWaitLock);
                LOGV("encodeData: jpeg thread completed.");
            }
            mJpegThreadWaitLock.unlock();
    }
    else LOGV("encodeData: JPEG callback is NULL, not encoding image.");

    mCamOps.mm_camera_deinit(CAMERA_OPS_CAPTURE, NULL, NULL);
    //clear the resources
    deinitRaw();
    //Encoding is done.
    mEncodePendingWaitLock.lock();
    mEncodePending = false;
    mEncodePendingWait.signal();
    mEncodePendingWaitLock.unlock();

    LOGV("encodeData: X");
}

void QualcommCameraHardware::getCameraInfo()
{
    LOGI("getCameraInfo: IN");
    mm_camera_status_t status;

#if DLOPEN_LIBMMCAMERA
    void *libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGI("getCameraInfo: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
    }
    *(void **)&LINK_mm_camera_get_camera_info =
        ::dlsym(libhandle, "mm_camera_get_camera_info");
#endif
    storeTargetType();
    status = LINK_mm_camera_get_camera_info(HAL_cameraInfo, &HAL_numOfCameras);
    LOGI("getCameraInfo: numOfCameras = %d", HAL_numOfCameras);
    for(int i = 0; i < HAL_numOfCameras; i++) {
        if((HAL_cameraInfo[i].position == BACK_CAMERA )&&
            mCurrentTarget == TARGET_MSM8660){
            HAL_cameraInfo[i].modes_supported |= CAMERA_ZSL_MODE;
        } else{
            HAL_cameraInfo[i].modes_supported |= CAMERA_NONZSL_MODE;
        }
        LOGI("Camera sensor %d info:", i);
        LOGI("camera_id: %d", HAL_cameraInfo[i].camera_id);
        LOGI("modes_supported: %x", HAL_cameraInfo[i].modes_supported);
        LOGI("position: %d", HAL_cameraInfo[i].position);
        LOGI("sensor_mount_angle: %d", HAL_cameraInfo[i].sensor_mount_angle);
    }

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("getCameraInfo: dlclose(libqcamera)");
    }
#endif
    LOGI("getCameraInfo: OUT");
}

extern "C" int HAL_isIn3DMode()
{
    return HAL_currentCameraMode == CAMERA_MODE_3D;
}

extern "C" int HAL_getNumberOfCameras()
{
    QualcommCameraHardware::getCameraInfo();
    return HAL_numOfCameras;
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
    int i;
    char mDeviceName[PROPERTY_VALUE_MAX];
    if(cameraInfo == NULL) {
        LOGE("cameraInfo is NULL");
        return;
    }

    property_get("ro.product.device",mDeviceName," ");

    for(i = 0; i < HAL_numOfCameras; i++) {
        if(i == cameraId) {
            LOGI("Found a matching camera info for ID %d", cameraId);
            cameraInfo->facing = (HAL_cameraInfo[i].position == BACK_CAMERA)?
                                   CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
            // App Orientation not needed for 7x27 , sensor mount angle 0 is
            // enough.
            if(cameraInfo->facing == CAMERA_FACING_FRONT)
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else if( !strncmp(mDeviceName, "msm7625a", 8))
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else if( !strncmp(mDeviceName, "msm7627a", 8))
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else if( !strncmp(mDeviceName, "msm7627", 7))
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else if( !strncmp(mDeviceName, "msm8660", 7))
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else
                cameraInfo->orientation = ((APP_ORIENTATION - HAL_cameraInfo[i].sensor_mount_angle) + 360)%360;

            LOGI("%s: orientation = %d", __FUNCTION__, cameraInfo->orientation);
            cameraInfo->mode = 0;
            if(HAL_cameraInfo[i].modes_supported & CAMERA_MODE_2D)
                cameraInfo->mode |= CAMERA_SUPPORT_MODE_2D;
            if(HAL_cameraInfo[i].modes_supported & CAMERA_MODE_3D)
                cameraInfo->mode |= CAMERA_SUPPORT_MODE_3D;
            if((HAL_cameraInfo[i].position == BACK_CAMERA )&&
                !strncmp(mDeviceName, "msm8660", 7)){
                cameraInfo->mode |= CAMERA_ZSL_MODE;
            } else{
                cameraInfo->mode |= CAMERA_NONZSL_MODE;
            }

            LOGI("%s: modes supported = %d", __FUNCTION__, cameraInfo->mode);

            return;
        }
    }
    LOGE("Unable to find matching camera info for ID %d", cameraId);
}

}; // namespace android

