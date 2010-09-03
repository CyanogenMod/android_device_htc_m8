/*
** Copyright 2008, Google Inc.
** Copyright (c) 2009-2010 Code Aurora Forum. All rights reserved.
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

#include "linux/msm_mdp.h"
#include <linux/fb.h>

#define LIKELY(exp)   __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)

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

#include <media/msm_camera.h>

#include <camera.h>
#include <camframe.h>
#include <liveshot.h>
#include <mm-still/jpeg/jpege.h>
#include <jpeg_encoder.h>

#define DUMP_LIVESHOT_JPEG_FILE 0

#define DEFAULT_PICTURE_WIDTH  1024
#define DEFAULT_PICTURE_HEIGHT 768
#define THUMBNAIL_BUFFER_SIZE (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT * 3/2)
#define MAX_ZOOM_LEVEL 5
#define NOT_FOUND -1
// Number of video buffers held by kernal (initially 1,2 &3)
#define ACTIVE_VIDEO_BUFFERS 3

#define PAD_TO_2K(x) (((x)+2047)& ~2047)

#if DLOPEN_LIBMMCAMERA
#include <dlfcn.h>

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
// Function to add a video buffer to free Q
void (*LINK_camframe_free_video)(struct msm_frame *frame);
// Function pointer , called by camframe when a video frame is available.
void (**LINK_camframe_video_callback)(struct msm_frame * frame);
// To flush free Q in cam frame.
void (*LINK_cam_frame_flush_free_video)(void);

int8_t (*LINK_jpeg_encoder_setMainImageQuality)(uint32_t quality);
int8_t (*LINK_jpeg_encoder_setThumbnailQuality)(uint32_t quality);
int8_t (*LINK_jpeg_encoder_setRotation)(uint32_t rotation);
int8_t (*LINK_jpeg_encoder_get_buffer_offset)(uint32_t width, uint32_t height,
                                               uint32_t* p_y_offset,
                                                uint32_t* p_cbcr_offset,
                                                 uint32_t* p_buf_size);
int8_t (*LINK_jpeg_encoder_setLocation)(const camera_position_type *location);
const struct camera_size_type *(*LINK_default_sensor_get_snapshot_sizes)(int *len);
int (*LINK_launch_cam_conf_thread)(void);
int (*LINK_release_cam_conf_thread)(void);
int8_t (*LINK_zoom_crop_upscale)(uint32_t width, uint32_t height,
    uint32_t cropped_width, uint32_t cropped_height, uint8_t *img_buf);

// callbacks
void  (**LINK_mmcamera_camframe_callback)(struct msm_frame *frame);
void  (**LINK_mmcamera_jpegfragment_callback)(uint8_t *buff_ptr,
                                              uint32_t buff_size);
void  (**LINK_mmcamera_jpeg_callback)(jpeg_event_t status);
void  (**LINK_mmcamera_shutter_callback)(common_crop_t *crop);
void  (**LINK_camframe_timeout_callback)(void);
void  (**LINK_mmcamera_liveshot_callback)(liveshot_status status, uint32_t jpeg_size);
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
#define LINK_default_sensor_get_snapshot_sizes default_sensor_get_snapshot_sizes
#define LINK_launch_cam_conf_thread launch_cam_conf_thread
#define LINK_release_cam_conf_thread release_cam_conf_thread
#define LINK_zoom_crop_upscale zoom_crop_upscale
extern void (*mmcamera_camframe_callback)(struct msm_frame *frame);
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
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} zoom_crop_info;

union zoomimage
{
    char d[sizeof(struct mdp_blit_req_list) + sizeof(struct mdp_blit_req) * 1];
    struct mdp_blit_req_list list;
} zoomImage;

//Default to WVGA
#define DEFAULT_PREVIEW_WIDTH 800
#define DEFAULT_PREVIEW_HEIGHT 480

//Default FPS
#define MINIMUM_FPS 5
#define MAXIMUM_FPS 30
#define DEFAULT_FPS MAXIMUM_FPS

/*
 * Modifying preview size requires modification
 * in bitmasks for boardproperties
 */

static const camera_size_type preview_sizes[] = {
    { 1920, 1088 }, //1080p
    { 1280, 720 }, // 720P, reserved
    { 800, 480 }, // WVGA
    { 768, 432 },
    { 720, 480 },
    { 640, 480 }, // VGA
    { 576, 432 },
    { 480, 320 }, // HVGA
    { 384, 288 },
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 240, 160 }, // SQVGA
    { 176, 144 }, // QCIF
};
#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(camera_size_type))

static camera_size_type supportedPreviewSizes[PREVIEW_SIZE_COUNT];
static unsigned int previewSizeCount;

board_property boardProperties[] = {
        {TARGET_MSM7625, 0x00000fff, false},
        {TARGET_MSM7627, 0x000006ff, false},
        {TARGET_MSM7630, 0x00000fff, true},
        {TARGET_MSM8660, 0x00001fff, true},
        {TARGET_QSD8250, 0x00000fff, true}
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
static const camera_size_type picture_sizes[] = {
    { 4000, 3000 }, // 12MP
    { 3200, 2400 }, // 8MP
    { 2592, 1944 }, // 5MP
    { 2048, 1536 }, // 3MP QXGA
    { 1920, 1080 }, //HD1080
    { 1600, 1200 }, // 2MP UXGA
    { 1280, 768 }, //WXGA
    { 1280, 720 }, //HD720
    { 1024, 768}, // 1MP XGA
    { 800, 600 }, //SVGA
    { 800, 480 }, // WVGA
    { 640, 480 }, // VGA
    { 352, 288 }, //CIF
    { 320, 240 }, // QVGA
    { 176, 144 } // QCIF
};
static int PICTURE_SIZE_COUNT = sizeof(picture_sizes)/sizeof(camera_size_type);
static const camera_size_type * picture_sizes_ptr;
static int supportedPictureSizesCount;
static liveshotState liveshot_state = LIVESHOT_DONE;

#ifdef Q12
#undef Q12
#endif

#define Q12 4096

static const target_map targetList [] = {
    { "msm7625", TARGET_MSM7625 },
    { "msm7627", TARGET_MSM7627 },
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
    { 6144, 432, 288 }, //1.5
    { 5461, 512, 384 }, //1.333333
    { 5006, 352, 288 }, //1.222222
};
#define THUMBNAIL_SIZE_COUNT (sizeof(thumbnail_sizes)/sizeof(thumbnail_size_type))
#define DEFAULT_THUMBNAIL_SETTING 2
#define THUMBNAIL_WIDTH_STR "512"
#define THUMBNAIL_HEIGHT_STR "384"
#define THUMBNAIL_SMALL_HEIGHT 144

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
#define MAX_EXIF_TABLE_ENTRIES 8
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
};

static const str_map scenedetect[] = {
    { CameraParameters::SCENE_DETECT_OFF, FALSE  },
    { CameraParameters::SCENE_DETECT_ON, TRUE },
};

#define country_number (sizeof(country_numeric) / sizeof(country_map))

/* Look up pre-sorted antibanding_type table by current MCC. */
static camera_antibanding_type camera_get_location(void) {
    char value[PROP_VALUE_MAX];
    char country_value[PROP_VALUE_MAX];
    uint32_t country_code, count;
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
    { CameraParameters::FOCUS_MODE_MACRO,    AF_MODE_MACRO }
};

static const str_map lensshade[] = {
    { CameraParameters::LENSSHADE_ENABLE, TRUE },
    { CameraParameters::LENSSHADE_DISABLE, FALSE }
};

static const str_map continuous_af[] = {
    { CameraParameters::CONTINUOUS_AF_OFF, FALSE },
    { CameraParameters::CONTINUOUS_AF_ON, TRUE }
};

#define DONT_CARE_COORDINATE -1
static const str_map touchafaec[] = {
    { CameraParameters::TOUCH_AF_AEC_OFF, FALSE },
    { CameraParameters::TOUCH_AF_AEC_ON, TRUE }
};

struct SensorType {
    const char *name;
    int rawPictureWidth;
    int rawPictureHeight;
    bool hasAutoFocusSupport;
    int max_supported_snapshot_width;
    int max_supported_snapshot_height;
    int bitMask;
};


/*
 * Values based on aec.c
 */

#define EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR 12
#define EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR -12
#define EXPOSURE_COMPENSATION_DEFAULT_NUMERATOR 0
#define EXPOSURE_COMPENSATION_DENOMINATOR 6
#define EXPOSURE_COMPENSATION_STEP ((float (1))/EXPOSURE_COMPENSATION_DENOMINATOR)

static SensorType sensorTypes[] = {
        { "12mp", 5464, 3120, true, 4000, 3000,0x00001fff },
        { "12mp_sn12m0pz",4032, 3024, true,  4000, 3000,0x00000fff },
        { "5mp", 2608, 1960, true,  2592, 1944,0x00000fff },
        { "3mp", 2064, 1544, false, 2048, 1536,0x000007ff },
        { "2mp", 3200, 1200, false, 1600, 1200,0x000007ff } };


static SensorType * sensorType;

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
static String8 picture_size_values;
static String8 antibanding_values;
static String8 effect_values;
static String8 autoexposure_values;
static String8 whitebalance_values;
static String8 flash_values;
static String8 focus_mode_values;
static String8 iso_values;
static String8 lensshade_values;
static String8 touchafaec_values;
static String8 picture_format_values;
static String8 scenemode_values;
static String8 continuous_af_values;
static String8 zoom_ratio_values;
static String8 preview_frame_rate_values;
static String8 frame_rate_mode_values;
static String8 scenedetect_values;
static String8 preview_format_values;

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
    {0, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};
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

void QualcommCameraHardware::storeTargetType(void) {
    char mDeviceName[PROPERTY_VALUE_MAX];
    property_get("ro.product.device",mDeviceName," ");
    mCurrentTarget = TARGET_MAX;
    for( int i = 0; i < TARGET_MAX ; i++) {
        if( !strncmp(mDeviceName, targetList[i].targetStr, 7)) {
            mCurrentTarget = targetList[i].targetEnum;
            break;
        }
    }
    LOGV(" Storing the current target type as %d ", mCurrentTarget );
    return;
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
static void receive_camframe_video_callback(struct msm_frame *frame); // 720p
static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size);
static void receive_jpeg_callback(jpeg_event_t status);
static void receive_shutter_callback(common_crop_t *crop);
static void receive_camframetimeout_callback(void);
static int fb_fd = -1;
static int32_t mMaxZoom = 0;
static bool zoomSupported = false;
static bool native_get_maxzoom(int camfd, void *pZm);
static bool native_get_zoomratios(int camfd, void *pZr, int maxZoomLevel);

static int dstOffset = 0;

static int camerafd;
pthread_t w_thread;

void *opencamerafd(void *data) {
    camerafd = open(MSM_CAMERA_CONTROL, O_RDWR);
    return NULL;
}

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
      mFrameThreadRunning(false),
      mVideoThreadRunning(false),
      mSnapshotThreadRunning(false),
      mEncodePending(false),
      mJpegThreadRunning(false),
      mInSnapshotMode(false),
      mSnapshotFormat(0),
      mReleasedRecordingFrame(false),
      mPreviewFrameSize(0),
      mRawSize(0),
      mCbCrOffsetRaw(0),
      mCameraControlFd(-1),
      mAutoFocusThreadRunning(false),
      mAutoFocusFd(-1),
      mBrightness(0),
      mHJR(0),
      mInPreviewCallback(false),
      mUseOverlay(0),
      mOverlay(0),
      mMsgEnabled(0),
      mNotifyCallback(0),
      mDataCallback(0),
      mDataCallbackTimestamp(0),
      mCallbackCookie(0),
      mInitialized(false),
      mDebugFps(0),
      mSnapshotDone(0),
      mDisEnabled(0),
      mRotation(0),
      mResetOverlayCrop(false)
{
    LOGI("QualcommCameraHardware constructor E");
    // Start opening camera device in a separate thread/ Since this
    // initializes the sensor hardware, this can take a long time. So,
    // start the process here so it will be ready by the time it's
    // needed.
    if ((pthread_create(&w_thread, NULL, opencamerafd, NULL)) != 0) {
        LOGE("Camera open thread creation failed");
    }

    memset(&mDimension, 0, sizeof(mDimension));
    memset(&mCrop, 0, sizeof(mCrop));
    memset(&zoomCropInfo, 0, sizeof(zoom_crop_info));
    storeTargetType();
    char value[PROPERTY_VALUE_MAX];
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
            jpegPadding = 8;
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
        /* DIS is enabled all the time in VPE support targets.
         * No provision for the user to control this.
         */
        mDisEnabled = 1;
        /* Get the DIS value from properties, to check whether
         * DIS is disabled or not
         */
        property_get("persist.camera.hal.dis", value, "1");
        mDisEnabled = atoi(value);
        mVpeEnabled = 1;
    }

    LOGV("constructor EX");
}

void QualcommCameraHardware::filterPreviewSizes(){

    unsigned int boardMask = 0;
    unsigned int prop = 0;
    for(prop=0;prop<sizeof(boardProperties)/sizeof(board_property);prop++){
        if(mCurrentTarget == boardProperties[prop].target){
            boardMask = boardProperties[prop].previewSizeMask;
            break;
        }
    }

    int bitMask = boardMask & sensorType->bitMask;
    if(bitMask){
        unsigned int mask = 1<<(PREVIEW_SIZE_COUNT-1);
        previewSizeCount=0;
        unsigned int i = 0;
        while(mask){
            if(mask&bitMask)
                supportedPreviewSizes[previewSizeCount++] =
                        preview_sizes[i];
            i++;
            mask = mask >> 1;
        }
    }
}

//filter Picture sizes based on max width and height
void QualcommCameraHardware::filterPictureSizes(){
    int i;
    for(i=0;i<PICTURE_SIZE_COUNT;i++){
        if(((picture_sizes[i].width <=
                sensorType->max_supported_snapshot_width) &&
           (picture_sizes[i].height <=
                   sensorType->max_supported_snapshot_height))){
            picture_sizes_ptr = picture_sizes + i;
            supportedPictureSizesCount = PICTURE_SIZE_COUNT - i  ;
            return ;
        }
    }
}

bool QualcommCameraHardware::supportsSceneDetection() {
   int prop = 0;
   for(prop=0; prop<sizeof(boardProperties)/sizeof(board_property); prop++) {
       if((mCurrentTarget == boardProperties[prop].target)
          && boardProperties[prop].hasSceneDetect == true) {
           return true;
           break;
       }
   }
   return false;
}

void QualcommCameraHardware::initDefaultParameters()
{
    LOGI("initDefaultParameters E");

    findSensorType();
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

        //filter preview sizes
        filterPreviewSizes();
        preview_size_values = create_sizes_str(
            supportedPreviewSizes, previewSizeCount);
        //filter picture sizes
        filterPictureSizes();
        picture_size_values = create_sizes_str(
                picture_sizes_ptr, supportedPictureSizesCount);

        flash_values = create_values_str(
            flash, sizeof(flash) / sizeof(str_map));
        if(sensorType->hasAutoFocusSupport){
            focus_mode_values = create_values_str(
                    focus_modes, sizeof(focus_modes) / sizeof(str_map));
        }
        iso_values = create_values_str(
            iso,sizeof(iso)/sizeof(str_map));
        lensshade_values = create_values_str(
            lensshade,sizeof(lensshade)/sizeof(str_map));

        if(sensorType->hasAutoFocusSupport){
            touchafaec_values = create_values_str(
                touchafaec,sizeof(touchafaec)/sizeof(str_map));
        }

        picture_format_values = create_values_str(
            picture_formats, sizeof(picture_formats)/sizeof(str_map));

        if(sensorType->hasAutoFocusSupport){
            continuous_af_values = create_values_str(
                continuous_af, sizeof(continuous_af) / sizeof(str_map));
        }

        if(native_get_maxzoom(mCameraControlFd,
                (void *)&mMaxZoom) == true){
            LOGD("Maximum zoom value is %d", mMaxZoom);
            zoomSupported = true;
            if(mMaxZoom > 0){
                //if max zoom is available find the zoom ratios
                int16_t * zoomRatios = new int16_t[mMaxZoom+1];
                if(zoomRatios != NULL){
                    if(native_get_zoomratios(mCameraControlFd,
                            (void *)zoomRatios, mMaxZoom + 1) == true){
                        zoom_ratio_values =
                                create_str(zoomRatios, mMaxZoom + 1);
                    }else {
                        LOGE("Failed to get zoomratios...");
                    }
                    delete zoomRatios;
                } else {
                    LOGE("zoom ratios failed to acquire memory");
                }
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

        parameter_string_initialized = true;
    }

    mParameters.setPreviewSize(DEFAULT_PREVIEW_WIDTH, DEFAULT_PREVIEW_HEIGHT);
    mDimension.display_width = DEFAULT_PREVIEW_WIDTH;
    mDimension.display_height = DEFAULT_PREVIEW_HEIGHT;

    mParameters.setPreviewFrameRate(DEFAULT_FPS);
     if((strcmp(mSensorInfo.name, "vx6953")) &&
              (strcmp(mSensorInfo.name, "VX6953")) &&
              (strcmp(sensorType->name, "2mp"))){
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            preview_frame_rate_values.string());
    }
    mParameters.setPreviewFrameRateMode("frame-rate-auto");
    mParameters.setPreviewFormat("yuv420sp"); // informative

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

    if(zoomSupported){
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
        LOGV("max zoom is %d", mMaxZoom);
        mParameters.set("max-zoom",mMaxZoom);
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
    mParameters.set(CameraParameters::KEY_FOCUS_MODE,
                    CameraParameters::FOCUS_MODE_AUTO);
    if( (mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_QSD8250) ) {
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
    if((strcmp(mSensorInfo.name, "vx6953")) &&
        (strcmp(mSensorInfo.name, "VX6953")) &&
        (strcmp(sensorType->name, "2mp"))){
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
    mParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
                    focus_mode_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
                    picture_format_values);

    if (mSensorInfo.flash_enabled) {
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
    mParameters.set(CameraParameters::KEY_SCENE_MODE,
                    CameraParameters::SCENE_MODE_AUTO);
    mParameters.set("strtextures", "OFF");

    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
                    scenemode_values);
    mParameters.set(CameraParameters::KEY_CONTINUOUS_AF,
                    CameraParameters::CONTINUOUS_AF_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_CONTINUOUS_AF,
                    continuous_af_values);
    mParameters.set(CameraParameters::KEY_TOUCH_AF_AEC,
                    CameraParameters::TOUCH_AF_AEC_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_TOUCH_AF_AEC,
                    touchafaec_values);
    mParameters.setTouchIndexAec(-1, -1);
    mParameters.setTouchIndexAf(-1, -1);
    mParameters.set(CameraParameters::KEY_SCENE_DETECT,
                    CameraParameters::SCENE_DETECT_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_DETECT,
                    scenedetect_values);
    if (setParameters(mParameters) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }

    mUseOverlay = useOverlay();

    /* Initialize the camframe_timeout_flag*/
    Mutex::Autolock l(&mCamframeTimeoutLock);
    camframe_timeout_flag = FALSE;
    mPostViewHeap = NULL;

    mInitialized = true;

    LOGI("initDefaultParameters X");
}

void QualcommCameraHardware::findSensorType(){
    mDimension.picture_width = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
                    sizeof(cam_ctrl_dimension_t), &mDimension);
    if (ret) {
        unsigned int i;
        for (i = 0; i < sizeof(sensorTypes) / sizeof(SensorType); i++) {
            if (sensorTypes[i].rawPictureHeight
                    == mDimension.raw_picture_height) {
                sensorType = sensorTypes + i;
                return;
            }
        }
    }
    //default to 5 mp
    sensorType = sensorTypes;
    return;
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
    libmmcamera = ::dlopen("liboemcamera.so", RTLD_NOW);

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

    *(void **)&LINK_mmcamera_camframe_callback =
        ::dlsym(libmmcamera, "mmcamera_camframe_callback");

    *LINK_mmcamera_camframe_callback = receive_camframe_callback;

    *(void **)&LINK_mmcamera_jpegfragment_callback =
        ::dlsym(libmmcamera, "mmcamera_jpegfragment_callback");

    *LINK_mmcamera_jpegfragment_callback = receive_jpeg_fragment_callback;

    *(void **)&LINK_mmcamera_jpeg_callback =
        ::dlsym(libmmcamera, "mmcamera_jpeg_callback");

    *LINK_mmcamera_jpeg_callback = receive_jpeg_callback;

    *(void **)&LINK_camframe_timeout_callback =
        ::dlsym(libmmcamera, "camframe_timeout_callback");

    *LINK_camframe_timeout_callback = receive_camframetimeout_callback;

    // 720 p new recording functions
    *(void **)&LINK_cam_frame_flush_free_video = ::dlsym(libmmcamera, "cam_frame_flush_free_video");

    *(void **)&LINK_camframe_free_video = ::dlsym(libmmcamera, "cam_frame_add_free_video");

    *(void **)&LINK_camframe_video_callback = ::dlsym(libmmcamera, "mmcamera_camframe_videocallback");
        *LINK_camframe_video_callback = receive_camframe_video_callback;

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

    *(void **)&LINK_mmcamera_liveshot_callback =
        ::dlsym(libmmcamera, "mmcamera_liveshot_callback");

    *LINK_mmcamera_liveshot_callback = receive_liveshot_callback;

    *(void **)&LINK_cancel_liveshot =
        ::dlsym(libmmcamera, "cancel_liveshot");

    *(void **)&LINK_set_liveshot_params =
        ::dlsym(libmmcamera, "set_liveshot_params");

/* Disabling until support is available.
    *(void **)&LINK_zoom_crop_upscale =
        ::dlsym(libmmcamera, "zoom_crop_upscale");
*/

#else
    mmcamera_camframe_callback = receive_camframe_callback;
    mmcamera_jpegfragment_callback = receive_jpeg_fragment_callback;
    mmcamera_jpeg_callback = receive_jpeg_callback;
    mmcamera_shutter_callback = receive_shutter_callback;
    mmcamera_liveshot_callback = receive_liveshot_callback;
#endif // DLOPEN_LIBMMCAMERA

    /* The control thread is in libcamera itself. */
    if (pthread_join(w_thread, NULL) != 0) {
        LOGE("Camera open thread exit failed");
        return false;
    }
    mCameraControlFd = camerafd;

    if (mCameraControlFd < 0) {
        LOGE("startCamera X: %s open failed: %s!",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        return false;
    }

    if((mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_MSM8660)){
        fb_fd = open("/dev/graphics/fb0", O_RDWR);
        if (fb_fd < 0) {
            LOGE("startCamera: fb0 open failed: %s!", strerror(errno));
            return FALSE;
        }
    }

    /* This will block until the control thread is launched. After that, sensor
     * information becomes available.
     */

    if (LINK_launch_cam_conf_thread()) {
        LOGE("failed to launch the camera config thread");
        return false;
    }

    memset(&mSensorInfo, 0, sizeof(mSensorInfo));
    if (ioctl(mCameraControlFd,
              MSM_CAM_IOCTL_GET_SENSOR_INFO,
              &mSensorInfo) < 0)
        LOGW("%s: cannot retrieve sensor info!", __FUNCTION__);
    else
        LOGI("%s: camsensor name %s, flash %d", __FUNCTION__,
             mSensorInfo.name, mSensorInfo.flash_enabled);
/* Disabling until support is available.
    picture_sizes = LINK_default_sensor_get_snapshot_sizes(&PICTURE_SIZE_COUNT);
    if (!picture_sizes || !PICTURE_SIZE_COUNT) {
        LOGE("startCamera X: could not get snapshot sizes");
        return false;
    }
*/
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
    if(mRawSnapshotAshmemHeap != 0 ){
        mRawSnapshotAshmemHeap->dump(fd, args);
    }
    mParameters.dump(fd, args);
    return NO_ERROR;
}

static bool native_get_maxzoom(int camfd, void *pZm)
{
    LOGV("native_get_maxzoom E");

    struct msm_ctrl_cmd ctrlCmd;
    int32_t *pZoom = (int32_t *)pZm;

    ctrlCmd.type       = CAMERA_GET_PARM_MAXZOOM;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = sizeof(int32_t);
    ctrlCmd.value      = pZoom;
    ctrlCmd.resp_fd    = camfd;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_get_maxzoom: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    LOGD("ctrlCmd.value = %d", *(int32_t *)ctrlCmd.value);
    memcpy(pZoom, (int32_t *)ctrlCmd.value, sizeof(int32_t));

    LOGV("native_get_maxzoom X");
    return true;
}

static bool native_get_zoomratios(int camfd, void *pZr, int maxZoomSize)
{
    LOGV("native_get_zoomratios E");
    struct msm_ctrl_cmd ctrlCmd;
    int16_t *zoomRatios = (int16_t *)pZr;

    if(maxZoomSize <= 0)
        return false;

    ctrlCmd.type       = CAMERA_GET_PARM_ZOOMRATIOS;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = sizeof(int16_t)* (maxZoomSize);
    ctrlCmd.value      = zoomRatios;
    ctrlCmd.resp_fd    = camfd;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_get_zoomratios: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    LOGV("native_get_zoomratios X");
    return true;
}


static bool native_set_afmode(int camfd, isp3a_af_mode_t af_type)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_SET_PARM_AUTO_FOCUS;
    ctrlCmd.length = sizeof(af_type);
    ctrlCmd.value = &af_type;
    ctrlCmd.resp_fd = camfd; // FIXME: this will be put in by the kernel

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0)
        LOGE("native_set_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));

    LOGV("native_set_afmode: ctrlCmd.status == %d\n", ctrlCmd.status);
    return rc >= 0 && ctrlCmd.status == CAMERA_EXIT_CB_DONE;
}

static bool native_cancel_afmode(int camfd, int af_fd)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 0;
    ctrlCmd.type = CAMERA_AUTO_FOCUS_CANCEL;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = -1; // there's no response fd

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND_2, &ctrlCmd)) < 0)
    {
        LOGE("native_cancel_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_start_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_preview: MSM_CAM_IOCTL_CTRL_COMMAND fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_get_picture (int camfd, common_crop_t *crop)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = sizeof(common_crop_t);
    ctrlCmd.value      = crop;

    if(ioctl(camfd, MSM_CAM_IOCTL_GET_PICTURE, &ctrlCmd) < 0) {
        LOGE("native_get_picture: MSM_CAM_IOCTL_GET_PICTURE fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    LOGV("crop: in1_w %d", crop->in1_w);
    LOGV("crop: in1_h %d", crop->in1_h);
    LOGV("crop: out1_w %d", crop->out1_w);
    LOGV("crop: out1_h %d", crop->out1_h);

    LOGV("crop: in2_w %d", crop->in2_w);
    LOGV("crop: in2_h %d", crop->in2_h);
    LOGV("crop: out2_w %d", crop->out2_w);
    LOGV("crop: out2_h %d", crop->out2_h);

    LOGV("crop: update %d", crop->update_flag);

    return true;
}

static bool native_stop_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_STOP_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_stop_preview: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_prepare_snapshot(int camfd)
{
    int ioctlRetVal = true;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type       = CAMERA_PREPARE_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.value      = NULL;
    ctrlCmd.resp_fd = camfd;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_prepare_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    return true;
}

static bool native_start_snapshot(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

static bool native_start_liveshot(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_START_LIVESHOT;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;
    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_start_liveshot: ioctl failed. ioctl return value is %d ", ret);
        return false;
    }
    return true;
}

static bool native_start_raw_snapshot(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type = CAMERA_START_RAW_SNAPSHOT;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;

    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_start_raw_snapshot: ioctl failed. ioctl return value "\
             "is %d \n", ret);
        return false;
    }
    return true;
}


static bool native_stop_snapshot (int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 0;
    ctrlCmd.type       = CAMERA_STOP_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = -1;

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND_2, &ctrlCmd) < 0) {
        LOGE("native_stop_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}
/*===========================================================================
 * FUNCTION    - native_start_recording -
 *
 * DESCRIPTION:
 *==========================================================================*/
static bool native_start_recording(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type = CAMERA_START_RECORDING;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;

    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_start_recording: ioctl failed. ioctl return value "\
            "is %d \n", ret);
        return false;
    }
    LOGV("native_start_recording: ioctl good. ioctl return value is %d \n",ret);

  /* TODO: Check status of postprocessing if there is any,
   *       PP status should be in  ctrlCmd */

    return true;
}

/*===========================================================================
 * FUNCTION    - native_stop_recording -
 *
 * DESCRIPTION:
 *==========================================================================*/
static bool native_stop_recording(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;
    LOGV("in native_stop_recording ");
    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type = CAMERA_STOP_RECORDING;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;

    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_stop_video: ioctl failed. ioctl return value is %d \n",
        ret);
        return false;
    }
    LOGV("in native_stop_recording returned %d", ret);
    return true;
}
/*===========================================================================
 * FUNCTION    - native_start_video -
 *
 * DESCRIPTION:
 *==========================================================================*/
static bool native_start_video(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type = CAMERA_START_VIDEO;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;

    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_start_video: ioctl failed. ioctl return value is %d \n",
        ret);
        return false;
    }

  /* TODO: Check status of postprocessing if there is any,
   *       PP status should be in  ctrlCmd */

    return true;
}

/*===========================================================================
 * FUNCTION    - native_stop_video -
 *
 * DESCRIPTION:
 *==========================================================================*/
static bool native_stop_video(int camfd)
{
    int ret;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 1000;
    ctrlCmd.type = CAMERA_STOP_VIDEO;
    ctrlCmd.length = 0;
    ctrlCmd.value = NULL;
    ctrlCmd.resp_fd = camfd;

    if ((ret = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0) {
        LOGE("native_stop_video: ioctl failed. ioctl return value is %d \n",
        ret);
        return false;
    }

    return true;
}
/*==========================================================================*/

static cam_frame_start_parms frame_parms;
static int recordingState = 0;

static rat_t latitude[3];
static rat_t longitude[3];
static char lonref[2];
static char latref[2];
static rat_t altitude;
static rat_t gpsTimestamp[3];
static char dateTime[20];
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

    //Set Latitude
    str = mParameters.get(CameraParameters::KEY_GPS_LATITUDE);
    if(str != NULL) {
        setLatLon(EXIFTAGID_GPS_LATITUDE, str);
        //set Latitude Ref
        str = NULL;
        str = mParameters.get(CameraParameters::KEY_GPS_LATITUDE_REF);
        if(str != NULL) {
            strncpy(latref, str, 1);
            latref[1] = '\0';
            addExifTag(EXIFTAGID_GPS_LATITUDE_REF, EXIF_ASCII, 2,
                        1, (void *)latref);
        }
    }

    //set Longitude
    str = NULL;
    str = mParameters.get(CameraParameters::KEY_GPS_LONGITUDE);
    if(str != NULL) {
        setLatLon(EXIFTAGID_GPS_LONGITUDE, str);
        //set Longitude Ref
        str = NULL;
        str = mParameters.get(CameraParameters::KEY_GPS_LONGITUDE_REF);
        if(str != NULL) {
            strncpy(lonref, str, 1);
            lonref[1] = '\0';
            addExifTag(EXIFTAGID_GPS_LONGITUDE_REF, EXIF_ASCII, 2,
                        1, (void *)lonref);
	}
    }

    //set Altitude
    str = NULL;
    str = mParameters.get(CameraParameters::KEY_GPS_ALTITUDE);
    if(str != NULL) {
        int value = atoi(str);
        rat_t alt_value = {value, 1000};
        memcpy(&altitude, &alt_value, sizeof(altitude));
        addExifTag(EXIFTAGID_GPS_ALTITUDE, EXIF_RATIONAL, 1,
                    1, (void *)&altitude);
        //set AltitudeRef
        int ref = mParameters.getInt(CameraParameters::KEY_GPS_ALTITUDE_REF);
        if( !(ref < 0 || ref > 1) )
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

      rat_t time_value[3] = { {UTCTimestamp->tm_hour, 1},
                              {UTCTimestamp->tm_min, 1},
                              {UTCTimestamp->tm_sec, 1} };

      memcpy(&gpsTimestamp, &time_value, sizeof(gpsTimestamp));
      addExifTag(EXIFTAGID_GPS_TIMESTAMP, EXIF_RATIONAL,
                  3, 1, (void *)&gpsTimestamp);
    }

}

bool QualcommCameraHardware::native_jpeg_encode(void)
{
    int jpeg_quality = mParameters.getInt("jpeg-quality");
    if (jpeg_quality >= 0) {
        LOGV("native_jpeg_encode, current jpeg main img quality =%d",
             jpeg_quality);
        if(!LINK_jpeg_encoder_setMainImageQuality(jpeg_quality)) {
            LOGE("native_jpeg_encode set jpeg-quality failed");
            return false;
        }
    }

    int thumbnail_quality = mParameters.getInt("jpeg-thumbnail-quality");
    if (thumbnail_quality >= 0) {
        LOGV("native_jpeg_encode, current jpeg thumbnail quality =%d",
             thumbnail_quality);
        if(!LINK_jpeg_encoder_setThumbnailQuality(thumbnail_quality)) {
            LOGE("native_jpeg_encode set thumbnail-quality failed");
            return false;
        }
    }

    if( (mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_MSM7627) && (mCurrentTarget != TARGET_MSM8660) ) {
        int rotation = mParameters.getInt("rotation");
        if (rotation >= 0) {
            LOGV("native_jpeg_encode, rotation = %d", rotation);
            if(!LINK_jpeg_encoder_setRotation(rotation)) {
                LOGE("native_jpeg_encode set rotation failed");
                return false;
            }
        }
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

    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        // Pass the main image as thumbnail buffer, so that jpeg encoder will
        // generate thumbnail based on main image.
        // Set the input and output dimensions for thumbnail generation to main
        // image dimensions and required thumbanail size repectively, for the
        // encoder to do downscaling of the main image accordingly.
        mCrop.in1_w  = mDimension.orig_picture_dx;
        mCrop.in1_h  = mDimension.orig_picture_dy;
        mCrop.out1_w = mDimension.thumbnail_width;
        mCrop.out1_h = mDimension.thumbnail_height;
        mDimension.thumbnail_width = mDimension.orig_picture_dx;
        mDimension.thumbnail_height = mDimension.orig_picture_dy;
        if (!LINK_jpeg_encoder_encode(&mDimension,
                                      (uint8_t *)mRawHeap->mHeap->base(),
                                      mRawHeap->mHeap->getHeapID(),
                                      (uint8_t *)mRawHeap->mHeap->base(),
                                      mRawHeap->mHeap->getHeapID(),
                                      &mCrop, exif_data, exif_table_numEntries,
                                      jpegPadding/2, mCbCrOffsetRaw)) {
            LOGE("native_jpeg_encode: jpeg_encoder_encode failed.");
            return false;
        }
    } else {
    if (!LINK_jpeg_encoder_encode(&mDimension,
                                  (uint8_t *)mThumbnailHeap->mHeap->base(),
                                  mThumbnailHeap->mHeap->getHeapID(),
                                  (uint8_t *)mRawHeap->mHeap->base(),
                                  mRawHeap->mHeap->getHeapID(),
                                  &mCrop, exif_data, exif_table_numEntries,
                                      jpegPadding/2, -1)) {
        LOGE("native_jpeg_encode: jpeg_encoder_encode failed.");
        return false;
    }
    }
    return true;
}

bool QualcommCameraHardware::native_set_parm(
    cam_ctrl_type type, uint16_t length, void *value)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = (uint16_t)type;
    ctrlCmd.length     = length;
    // FIXME: this will be put in by the kernel
    ctrlCmd.resp_fd    = mCameraControlFd;
    ctrlCmd.value = value;

    LOGV("%s: fd %d, type %d, length %d", __FUNCTION__,
         mCameraControlFd, type, length);
    if (ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0 ||
                ctrlCmd.status != CAM_CTRL_SUCCESS) {
        LOGE("%s: error (%s): fd %d, type %d, length %d, status %d",
             __FUNCTION__, strerror(errno),
             mCameraControlFd, type, length, ctrlCmd.status);
        return false;
    }
    return true;
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

void QualcommCameraHardware::runFrameThread(void *data)
{
    LOGV("runFrameThread E");

    int cnt;

#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // frame thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    void *libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("FRAME: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
    }
    if (libhandle)
#endif
    {
        LINK_cam_frame(data);
    }

    mPreviewHeap.clear();
    if(( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660))
        mRecordHeap.clear();

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("FRAME: dlclose(libqcamera)");
    }
#endif

    mFrameThreadWaitLock.lock();
    mFrameThreadRunning = false;
    mFrameThreadWait.signal();
    mFrameThreadWaitLock.unlock();

    LOGV("runFrameThread X");
}

void QualcommCameraHardware::runVideoThread(void *data)
{
    LOGD("runVideoThread E");
    msm_frame* vframe = NULL;

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
            // Enable IF block to give frames to encoder , ELSE block for just simulation
#if 1
            LOGV("in video_thread : got video frame, before if check giving frame to services/encoder");
            mCallbackLock.lock();
            int msgEnabled = mMsgEnabled;
            data_callback_timestamp rcb = mDataCallbackTimestamp;
            void *rdata = mCallbackCookie;
            mCallbackLock.unlock();

            if(rcb != NULL && (msgEnabled & CAMERA_MSG_VIDEO_FRAME) ) {
                LOGV("in video_thread : got video frame, giving frame to services/encoder");
                rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[offset], rdata);
            }
#else
            // 720p output2  : simulate release frame here:
            LOGE("in video_thread simulation , releasing the video frame");
            LINK_camframe_free_video(vframe);
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
    // See comments in deinitPreview() for why we have to wait for the frame
    // thread here, and why we can't use pthread_join().
    const char * pmem_region;
    mParameters.getPreviewSize(&previewWidth, &previewHeight);

    //Get the Record sizes
    const char *recordSize = NULL;
    recordSize = mParameters.get("record-size");
    if(!recordSize) {
        //If application didn't set this parameter string, use the values from
        //getPreviewSize() as video dimensions.
        LOGI("No Record Size requested, use the preview dimensions");
        videoWidth = previewWidth;
        videoHeight = previewHeight;
    } else {
        //Extract the record witdh and height that application requested.
        if(!parse_size(recordSize, videoWidth, videoHeight)) {
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
        } else {
            LOGE("initPreview X: failed to parse parameter record-size (%s)", recordSize);
            return false;
        }
    }

    mDimension.display_width = previewWidth;
    mDimension.display_height= previewHeight;

    LOGV("initPreview E: preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight );

    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {
        mDimension.video_width = CEILING16(videoWidth);
        /* Backup the video dimensions, as video dimensions in mDimension
         * will be modified when DIS is supported. Need the actual values
         * to pass ap part of VPE config
         */
        videoWidth = mDimension.video_width;
        mDimension.video_height = videoHeight;
        LOGV("initPreview : preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, mDimension.video_width, mDimension.video_height );
    }


    mFrameThreadWaitLock.lock();
    while (mFrameThreadRunning) {
        LOGV("initPreview: waiting for old frame thread to complete.");
        mFrameThreadWait.wait(mFrameThreadWaitLock);
        LOGV("initPreview: old frame thread completed.");
    }
    mFrameThreadWaitLock.unlock();

    mInSnapshotModeWaitLock.lock();
    while (mInSnapshotMode) {
        LOGV("initPreview: waiting for snapshot mode to complete.");
        mInSnapshotModeWait.wait(mInSnapshotModeWaitLock);
        LOGV("initPreview: snapshot mode completed.");
    }
    mInSnapshotModeWaitLock.unlock();

     /*Temporary migrating the preview buffers to smi pool for 8x60 till the bug is resolved in the pmem_adsp pool*/
    if(mCurrentTarget == TARGET_MSM8660)
        pmem_region = "/dev/pmem_smipool";
    else
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
    mPreviewHeap = new PmemPool(pmem_region,
                                MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                mCameraControlFd,
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
    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660)
        || (mCurrentTarget == TARGET_MSM7627) ) {
        mPostViewHeap.clear();
	if(mPostViewHeap == NULL) {
        LOGV(" Allocating Postview heap ");
        /* mPostViewHeap should be declared only for 7630 target */
	    mPostViewHeap =
		new PmemPool("/dev/pmem_adsp",
			MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
			mCameraControlFd,
			MSM_PMEM_PREVIEW, //MSM_PMEM_OUTPUT2,
			mPreviewFrameSize,
			1,
			mPreviewFrameSize,
                        CbCrOffset,
                        0,
			"postview");

	    if (!mPostViewHeap->initialized()) {
		mPostViewHeap.clear();
		LOGE(" Failed to initialize Postview Heap");
		return false;
	    }
	}
    }

    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {

        // Allocate video buffers after allocating preview buffers.
        initRecord();
    }

    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);

    if (ret) {
        for (cnt = 0; cnt < kPreviewBufferCount; cnt++) {
            frames[cnt].fd = mPreviewHeap->mHeap->getHeapID();
            frames[cnt].buffer =
                (uint32_t)mPreviewHeap->mHeap->base() + mPreviewHeap->mAlignedBufferSize * cnt;
            frames[cnt].y_off = 0;
            frames[cnt].cbcr_off = CbCrOffset;
            frames[cnt].path = OUTPUT_TYPE_P; // MSM_FRAME_ENC;
        }

        mFrameThreadWaitLock.lock();
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        frame_parms.frame = frames[kPreviewBufferCount - 1];

        if( mCurrentTarget == TARGET_MSM7630 || mCurrentTarget == TARGET_QSD8250 || mCurrentTarget == TARGET_MSM8660)
            frame_parms.video_frame =  recordframes[kPreviewBufferCount - 1];
        else
            frame_parms.video_frame =  frames[kPreviewBufferCount - 1];

        LOGV ("initpreview before cam_frame thread carete , video frame  buffer=%lu fd=%d y_off=%d cbcr_off=%d \n",
          (unsigned long)frame_parms.video_frame.buffer, frame_parms.video_frame.fd, frame_parms.video_frame.y_off,
          frame_parms.video_frame.cbcr_off);
        mFrameThreadRunning = !pthread_create(&mFrameThread,
                                              &attr,
                                              frame_thread,
                                              (void*)&(frame_parms));
        ret = mFrameThreadRunning;
        mFrameThreadWaitLock.unlock();
    }

    LOGV("initPreview X: %d", ret);
    return ret;
}

void QualcommCameraHardware::deinitPreview(void)
{
    LOGI("deinitPreview E");

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
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
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
                                    mCameraControlFd,
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
    LOGV("initRawSnapshot X");
    return true;

}

bool QualcommCameraHardware::initRaw(bool initJpegHeap)
{
    int rawWidth, rawHeight;
    const char * pmem_region;

    mParameters.getPictureSize(&rawWidth, &rawHeight);
    LOGV("initRaw E: picture size=%dx%d", rawWidth, rawHeight);

    int thumbnailBufferSize;
    //Thumbnail height should be smaller than Picture height
    if (rawHeight > (int)thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height){
        mDimension.ui_thumbnail_width =
                thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].width;
        mDimension.ui_thumbnail_height =
                thumbnail_sizes[DEFAULT_THUMBNAIL_SETTING].height;
        uint32_t pictureAspectRatio = (uint32_t)((rawWidth * Q12) / rawHeight);
        uint32_t i;
        for(i = 0; i < THUMBNAIL_SIZE_COUNT; i++ )
        {
            if(thumbnail_sizes[i].aspect_ratio == pictureAspectRatio)
            {
                mDimension.ui_thumbnail_width = thumbnail_sizes[i].width;
                mDimension.ui_thumbnail_height = thumbnail_sizes[i].height;
                break;
            }
        }
    }
    else{
        mDimension.ui_thumbnail_height = THUMBNAIL_SMALL_HEIGHT;
        mDimension.ui_thumbnail_width =
                (THUMBNAIL_SMALL_HEIGHT * rawWidth)/ rawHeight;
    }

    LOGV("Thumbnail Size Width %d Height %d",
            mDimension.ui_thumbnail_width,
            mDimension.ui_thumbnail_height);

    thumbnailBufferSize = mDimension.ui_thumbnail_width *
                          mDimension.ui_thumbnail_height * 3 / 2;
    int CbCrOffsetThumb = PAD_TO_WORD(mDimension.ui_thumbnail_width *
                          mDimension.ui_thumbnail_height);
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
        mDimension.main_img_format = CAMERA_YUV_420_NV21_ADRENO;
        mDimension.thumb_format = CAMERA_YUV_420_NV21_ADRENO;
        thumbnailBufferSize = PAD_TO_4K(CEILING32(mDimension.ui_thumbnail_width) *
                              CEILING32(mDimension.ui_thumbnail_height)) +
                              2 * (CEILING32(mDimension.ui_thumbnail_width/2) *
                                CEILING32(mDimension.ui_thumbnail_height/2));
        CbCrOffsetThumb = PAD_TO_4K(CEILING32(mDimension.ui_thumbnail_width) *
                              CEILING32(mDimension.ui_thumbnail_height));
    }
    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret = native_set_parm(CAMERA_SET_PARM_DIMENSION,
                               sizeof(cam_ctrl_dimension_t), &mDimension);
    if(!ret) {
        LOGE("initRaw X: failed to set dimension");
        return false;
    }

    if (mJpegHeap != NULL) {
        LOGV("initRaw: clearing old mJpegHeap.");
        mJpegHeap.clear();
    }

    // Snapshot
    mRawSize = rawWidth * rawHeight * 3 / 2;
    mCbCrOffsetRaw = PAD_TO_WORD(rawWidth * rawHeight);
    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
        mRawSize = PAD_TO_4K(CEILING32(rawWidth) * CEILING32(rawHeight)) +
                            2 * (CEILING32(rawWidth/2) * CEILING32(rawHeight/2));
        mCbCrOffsetRaw = PAD_TO_4K(CEILING32(rawWidth) * CEILING32(rawHeight));
    }
    if( mCurrentTarget == TARGET_MSM7627 )
        mJpegMaxSize = CEILING16(rawWidth) * CEILING16(rawHeight) * 3 / 2;
    else {
        mJpegMaxSize = rawWidth * rawHeight * 3 / 2;
        if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
            mJpegMaxSize =
               PAD_TO_4K(CEILING32(rawWidth) * CEILING32(rawHeight)) +
                    2 * (CEILING32(rawWidth/2) * CEILING32(rawHeight/2));
        }
    }

    //For offline jpeg hw encoder, jpeg encoder will provide us the
    //required offsets and buffer size depending on the rotation.
    int yOffset = 0;
    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM7627) || (mCurrentTarget == TARGET_MSM8660)) {
        int rotation = mParameters.getInt("rotation");
        if (rotation >= 0) {
            LOGV("initRaw, jpeg_rotation = %d", rotation);
            if(!LINK_jpeg_encoder_setRotation(rotation)) {
                LOGE("native_jpeg_encode set rotation failed");
                return false;
            }
        }
        //Don't call the get_buffer_offset() for ADRENO, as the width and height
        //for Adreno format will be of CEILING32.
        if(mPreviewFormat != CAMERA_YUV_420_NV21_ADRENO) {
        LINK_jpeg_encoder_get_buffer_offset(rawWidth, rawHeight, (uint32_t *)&yOffset,
                                            (uint32_t *)&mCbCrOffsetRaw, (uint32_t *)&mRawSize);
        mJpegMaxSize = mRawSize;
    }
        LOGV("initRaw: yOffset = %d, mCbCrOffsetRaw = %d, mRawSize = %d",
                     yOffset, mCbCrOffsetRaw, mRawSize);
    }

    if(mCurrentTarget == TARGET_MSM8660)
        pmem_region = "/dev/pmem_smipool";
    else
        pmem_region = "/dev/pmem_adsp";


    LOGV("initRaw: initializing mRawHeap.");
    mRawHeap =
        new PmemPool(pmem_region,
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     mCameraControlFd,
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
        mCbCrOffsetRaw = CEILING32(rawWidth) * CEILING32(rawHeight);

    LOGV("do_mmap snapshot pbuf = %p, pmem_fd = %d",
         (uint8_t *)mRawHeap->mHeap->base(), mRawHeap->mHeap->getHeapID());

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

        // Thumbnails

        mThumbnailHeap =
            new PmemPool(pmem_region,
                         MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                         mCameraControlFd,
                         MSM_PMEM_THUMBNAIL,
                         thumbnailBufferSize,
                         1,
                         thumbnailBufferSize,
                         CbCrOffsetThumb,
                         0,
                         "thumbnail");

        if (!mThumbnailHeap->initialized()) {
            mThumbnailHeap.clear();
            mJpegHeap.clear();
            mRawHeap.clear();
            LOGE("initRaw X failed: error initializing mThumbnailHeap.");
            return false;
        }
    }

    LOGV("initRaw X");
    return true;
}


void QualcommCameraHardware::deinitRawSnapshot()
{
    LOGV("deinitRawSnapshot E");
    mRawSnapShotPmemHeap.clear();
    mRawSnapshotAshmemHeap.clear();
    LOGV("deinitRawSnapshot X");
}

void QualcommCameraHardware::deinitRaw()
{
    LOGV("deinitRaw E");

    mThumbnailHeap.clear();
    mJpegHeap.clear();
    mRawHeap.clear();
    mDisplayHeap.clear();

    LOGV("deinitRaw X");
}

void QualcommCameraHardware::release()
{
    LOGI("release E");
    Mutex::Autolock l(&mLock);

#if DLOPEN_LIBMMCAMERA
    if (libmmcamera == NULL) {
        LOGE("ERROR: multiple release!");
        return;
    }
#else
#warning "Cannot detect multiple release when not dlopen()ing libqcamera!"
#endif

    int cnt, rc;
    struct msm_ctrl_cmd ctrlCmd;
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

    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660)
        || (mCurrentTarget == TARGET_MSM7627) ) {
    mPostViewHeap.clear();
        mPostViewHeap = NULL;
    }
    LINK_jpeg_encoder_join();
    {
        Mutex::Autolock l (&mRawPictureHeapLock);
        deinitRaw();
    }
    //Signal the snapshot thread
    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = false;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();

    deinitRawSnapshot();
    LOGI("release: clearing resources done.");

    
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length = 0;
    ctrlCmd.type = (uint16_t)CAMERA_EXIT;
    ctrlCmd.resp_fd = mCameraControlFd; // FIXME: this will be put in by the kernel
    if (ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
          LOGE("ioctl CAMERA_EXIT fd %d error %s",
              mCameraControlFd, strerror(errno));

    LOGI("release: CAMERA_EXIT done.");
    LINK_release_cam_conf_thread();
    close(mCameraControlFd);
    mCameraControlFd = -1;
    if(fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
#if DLOPEN_LIBMMCAMERA
    if (libmmcamera) {
        ::dlclose(libmmcamera);
        LOGV("dlclose(libqcamera)");
        libmmcamera = NULL;
    }
#endif

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
    return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
}

status_t QualcommCameraHardware::startPreviewInternal()
{
    LOGV("in startPreviewInternal : E");
    if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }

    if (!mPreviewInitialized) {
        mLastQueuedFrame = NULL;
        mPreviewInitialized = initPreview();
        if (!mPreviewInitialized) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }
    }

    {
        Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
        if(( mCurrentTarget != TARGET_MSM7630 ) &&
                (mCurrentTarget != TARGET_QSD8250) && (mCurrentTarget != TARGET_MSM8660))
            mCameraRunning = native_start_preview(mCameraControlFd);
        else
            mCameraRunning = native_start_video(mCameraControlFd);
    }

    if(!mCameraRunning) {
        deinitPreview();
        mPreviewInitialized = false;
        mOverlay = NULL;
        LOGE("startPreview X: native_start_preview failed!");
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
    if (mCameraRunning) {
        // Cancel auto focus.
        {
            if (mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS)) {
                cancelAutoFocusInternal();
            }
        }

        Mutex::Autolock l(&mCamframeTimeoutLock);
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(!camframe_timeout_flag) {
                if (( mCurrentTarget != TARGET_MSM7630 ) &&
                        (mCurrentTarget != TARGET_QSD8250) && (mCurrentTarget != TARGET_MSM8660))
                    mCameraRunning = !native_stop_preview(mCameraControlFd);
                else
                    mCameraRunning = !native_stop_video(mCameraControlFd);
            } else {
                /* This means that the camframetimeout was issued.
                 * But we did not issue native_stop_preview(), so we
                 * need to update mCameraRunning to indicate that
                 * Camera is no longer running. */
                mCameraRunning = 0;
            }
        }

	if (!mCameraRunning && mPreviewInitialized) {
	    deinitPreview();
	    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)) {
		mVideoThreadWaitLock.lock();
		LOGV("in stopPreviewInternal: making mVideoThreadExit 1");
		mVideoThreadExit = 1;
		mVideoThreadWaitLock.unlock();
		//  720p : signal the video thread , and check in video thread if stop is called, if so exit video thread.
		pthread_mutex_lock(&(g_busy_frame_queue.mut));
		pthread_cond_signal(&(g_busy_frame_queue.wait));
		pthread_mutex_unlock(&(g_busy_frame_queue.mut));
                /* Flush the Busy Q */
                cam_frame_flush_video();
                /* Flush the Free Q */
                LINK_cam_frame_flush_free_video();
	    }
	    mPreviewInitialized = false;
	}
	else LOGE("stopPreviewInternal: failed to stop preview");
    }
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
    if ((mParameters.get(CameraParameters::KEY_FOCUS_MODE) == 0)
           || (strcmp(mParameters.get(CameraParameters::KEY_FOCUS_MODE),
               CameraParameters::FOCUS_MODE_INFINITY) == 0)) {
        goto done;
    }

    mAutoFocusFd = open(MSM_CAMERA_CONTROL, O_RDWR);
    if (mAutoFocusFd < 0) {
        LOGE("autofocus: cannot open %s: %s",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }

#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // AF thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("AF: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        close(mAutoFocusFd);
        mAutoFocusFd = -1;
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }
#endif

    afMode = (isp3a_af_mode_t)attr_lookup(focus_modes,
                                sizeof(focus_modes) / sizeof(str_map),
                                mParameters.get(CameraParameters::KEY_FOCUS_MODE));

    /* This will block until either AF completes or is cancelled. */
    LOGV("af start (fd %d mode %d)", mAutoFocusFd, afMode);
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(mCameraRunning){
                LOGV("Start AF");
                status = native_set_afmode(mAutoFocusFd, afMode);
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

    LOGV("af done: %d", (int)status);
    close(mAutoFocusFd);
    mAutoFocusFd = -1;

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

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("AF: dlclose(libqcamera)");
    }
#endif
}

status_t QualcommCameraHardware::cancelAutoFocusInternal()
{
    LOGV("cancelAutoFocusInternal E");

    if(!sensorType->hasAutoFocusSupport){
        LOGV("cancelAutoFocusInternal X");
        return NO_ERROR;
    }

#if 0
    if (mAutoFocusFd < 0) {
        LOGV("cancelAutoFocusInternal X: not in progress");
        return NO_ERROR;
    }
#endif

    status_t rc = NO_ERROR;
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        //Got Lock, means either AF hasn't started or
        // AF is done. So no need to cancel it, just change the state
        LOGV("As Auto Focus is not in progress, Cancel Auto Focus "
                "is ignored");
        mAfLock.unlock();
    }
    else {
        //AF is in Progess, So cancel it
        LOGV("Lock busy...cancel AF");
        rc = native_cancel_afmode(mCameraControlFd, mAutoFocusFd) ?
                NO_ERROR :
                UNKNOWN_ERROR;
    }



    LOGV("cancelAutoFocusInternal X: %d", rc);
    return rc;
}

void *auto_focus_thread(void *user)
{
    LOGV("auto_focus_thread E");
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

    if(!sensorType->hasAutoFocusSupport){
        bool status = false;
        mCallbackLock.lock();
        bool autoFocusEnabled = mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS);
        notify_callback cb = mNotifyCallback;
        void *data = mCallbackCookie;
        mCallbackLock.unlock();
        if (autoFocusEnabled)
            cb(CAMERA_MSG_FOCUS, status, 0, data);
        LOGV("autoFocus X");
        return NO_ERROR;
    }

    if (mCameraControlFd < 0) {
        LOGE("not starting autofocus: main control fd %d", mCameraControlFd);
        return UNKNOWN_ERROR;
    }

    {
        mAutoFocusThreadLock.lock();
        if (!mAutoFocusThreadRunning) {
            if (native_prepare_snapshot(mCameraControlFd) == FALSE) {
               LOGE("native_prepare_snapshot failed!\n");
               mAutoFocusThreadLock.unlock();
               return UNKNOWN_ERROR;
            }

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
    LOGV("runSnapshotThread E");
#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // Snapshot thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    void *libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
    LOGV("SNAPSHOT: loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
    }
#endif

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if (native_start_snapshot(mCameraControlFd))
            receiveRawPicture();
        else
            LOGE("main: native_start_snapshot failed!");
    } else if(mSnapshotFormat == PICTURE_FORMAT_RAW){
        if(native_start_raw_snapshot(mCameraControlFd)){
           receiveRawSnapshot();
        } else {
           LOGE("main: native_start_raw_snapshot failed!");
        }
    }
    mInSnapshotModeWaitLock.lock();
    mInSnapshotMode = false;
    mInSnapshotModeWait.signal();
    mInSnapshotModeWaitLock.unlock();

    mSnapshotFormat = 0;
    if(mPreviewFormat != CAMERA_YUV_420_NV21_ADRENO ) {
        mJpegThreadWaitLock.lock();
        while (mJpegThreadRunning) {
            LOGV("runSnapshotThread: waiting for jpeg thread to complete.");
            mJpegThreadWait.wait(mJpegThreadWaitLock);
            LOGV("runSnapshotThread: jpeg thread completed.");
        }
        mJpegThreadWaitLock.unlock();
        //clear the resources
#if DLOPEN_LIBMMCAMERA
        if(libhandle)
#endif
        {
            LINK_jpeg_encoder_join();
        }
        deinitRaw();
    }

    mSnapshotThreadWaitLock.lock();
    mSnapshotThreadRunning = false;
    mSnapshotThreadWait.signal();
    mSnapshotThreadWaitLock.unlock();
#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("SNAPSHOT: dlclose(libqcamera)");
    }
#endif

    LOGV("runSnapshotThread X");
}

void *snapshot_thread(void *user)
{
    LOGD("snapshot_thread E");
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
    LOGV("takePicture(%d)", mMsgEnabled);
    Mutex::Autolock l(&mLock);

    if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO){
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

    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660)
        || (mCurrentTarget == TARGET_MSM7627) ) {
    /* Store the last frame queued for preview. This
     * shall be used as postview */
    storePreviewFrameForPostview();
    }

    //mSnapshotFormat is protected by mSnapshotThreadWaitLock
    if(mParameters.getPictureFormat() != 0 &&
            !strcmp(mParameters.getPictureFormat(),
                    CameraParameters::PIXEL_FORMAT_RAW))
        mSnapshotFormat = PICTURE_FORMAT_RAW;
    else
        mSnapshotFormat = PICTURE_FORMAT_JPEG;

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if(!native_prepare_snapshot(mCameraControlFd)) {
            mSnapshotThreadWaitLock.unlock();
            return UNKNOWN_ERROR;
        }
    }

    stopPreviewInternal();

    if(mSnapshotFormat == PICTURE_FORMAT_JPEG){
        if (!initRaw(mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE))) {
            LOGE("initRaw failed.  Not taking picture.");
            mSnapshotThreadWaitLock.unlock();
            return UNKNOWN_ERROR;
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

    LOGV("takePicture: X");
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

    if(!native_start_liveshot(mCameraControlFd)) {
        LOGE("native_start_liveshot failed");
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
    LOGV("cancelPicture: E");
    if (mCurrentTarget == TARGET_MSM7627) {
        mSnapshotDone = TRUE;
        mSnapshotThreadWaitLock.lock();
        while (mSnapshotThreadRunning) {
            LOGV("cancelPicture: waiting for snapshot thread to complete.");
            mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
            LOGV("cancelPicture: snapshot thread completed.");
        }
        mSnapshotThreadWaitLock.unlock();
    }
    rc = native_stop_snapshot(mCameraControlFd) ? NO_ERROR : UNKNOWN_ERROR;
    mSnapshotDone = FALSE;
    LOGV("cancelPicture: X: %d", rc);
    return rc;
}

status_t QualcommCameraHardware::setParameters(const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);
    status_t rc, final_rc = NO_ERROR;

    if ((rc = setPreviewSize(params)))  final_rc = rc;
    if ((rc = setPictureSize(params)))  final_rc = rc;
    if ((rc = setJpegQuality(params)))  final_rc = rc;
    if ((rc = setEffect(params)))       final_rc = rc;
    if ((rc = setGpsLocation(params)))  final_rc = rc;
    if ((rc = setRotation(params)))     final_rc = rc;
    if ((rc = setZoom(params)))         final_rc = rc;
    if ((rc = setOrientation(params)))  final_rc = rc;
    if ((rc = setLensshadeValue(params)))  final_rc = rc;
    if ((rc = setPictureFormat(params))) final_rc = rc;
    if ((rc = setSharpness(params)))    final_rc = rc;
    if ((rc = setSaturation(params)))   final_rc = rc;
    if ((rc = setContinuousAf(params)))  final_rc = rc;
    if ((rc = setTouchAfAec(params)))   final_rc = rc;
    if ((rc = setSceneMode(params)))  final_rc = rc;
    if ((rc = setContrast(params)))     final_rc = rc;
    if ((rc = setRecordSize(params)))  final_rc = rc;
    if ((rc = setSceneDetect(params)))  final_rc = rc;
    if ((rc = setStrTextures(params)))   final_rc = rc;
    if ((rc = setPreviewFormat(params)))   final_rc = rc;

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if((value != NOT_FOUND) && (value == CAMERA_BESTSHOT_OFF)) {
        if ((rc = setPreviewFrameRate(params))) final_rc = rc;
        if ((rc = setPreviewFrameRateMode(params))) final_rc = rc;
        if ((rc = setAntibanding(params)))  final_rc = rc;
        if ((rc = setAutoExposure(params))) final_rc = rc;
        if ((rc = setExposureCompensation(params))) final_rc = rc;
        if ((rc = setWhiteBalance(params))) final_rc = rc;
        if ((rc = setFlash(params)))        final_rc = rc;
        if ((rc = setFocusMode(params)))    final_rc = rc;
        if ((rc = setBrightness(params)))   final_rc = rc;
        if ((rc = setISOValue(params)))  final_rc = rc;
    }
    LOGV("setParameters: X");
    return final_rc;
}

CameraParameters QualcommCameraHardware::getParameters() const
{
    LOGV("getParameters: EX");
    return mParameters;
}

status_t QualcommCameraHardware::sendCommand(int32_t command, int32_t arg1,
                                             int32_t arg2)
{
    LOGV("sendCommand: EX");
    return BAD_VALUE;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    LOGI("openCameraHardware: call createInstance");
    return QualcommCameraHardware::createInstance();
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


bool QualcommCameraHardware::native_zoom_image(int fd, int srcOffset, int dstOffSet, common_crop_t *crop)
{
    int result = 0;
    struct mdp_blit_req *e;
    struct timeval td1, td2;

    /* Initialize yuv structure */
    zoomImage.list.count = 1;

    e = &zoomImage.list.req[0];

    e->src.width = previewWidth;
    e->src.height = previewHeight;
    e->src.format = MDP_Y_CBCR_H2V2;
    e->src.offset = srcOffset;
    e->src.memory_id = fd;

    e->dst.width = previewWidth;
    e->dst.height = previewHeight;
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
        e->src_rect.w = previewWidth;
        e->src_rect.h = previewHeight;
    }
    //LOGV(" native_zoom : SRC_RECT : x,y = %d,%d \t w,h = %d, %d",
    //        e->src_rect.x, e->src_rect.y, e->src_rect.w, e->src_rect.h);

    e->dst_rect.x = 0;
    e->dst_rect.y = 0;
    e->dst_rect.w = previewWidth;
    e->dst_rect.h = previewHeight;

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
}

void QualcommCameraHardware::receivePreviewFrame(struct msm_frame *frame)
{
//    LOGV("receivePreviewFrame E");

    if (!mCameraRunning) {
        LOGE("ignoring preview callback--camera has been stopped");
        return;
    }

    if (UNLIKELY(mDebugFps)) {
        debugShowPreviewFPS();
    }

    mCallbackLock.lock();
    int msgEnabled = mMsgEnabled;
    data_callback pcb = mDataCallback;
    void *pdata = mCallbackCookie;
    data_callback_timestamp rcb = mDataCallbackTimestamp;
    void *rdata = mCallbackCookie;
    mCallbackLock.unlock();

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
        if(mOverlay != NULL) {
            mOverlayLock.lock();
            mOverlay->setFd(mPreviewHeap->mHeap->getHeapID());
            if (crop->in1_w != 0 && crop->in1_h != 0) {
                zoomCropInfo.x = (crop->out1_w - crop->in1_w + 1) / 2 - 1;
                zoomCropInfo.y = (crop->out1_h - crop->in1_h + 1) / 2 - 1;
                zoomCropInfo.w = crop->in1_w;
                zoomCropInfo.h = crop->in1_h;
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
            mLastQueuedFrame = (void *)frame->buffer;
            mOverlayLock.unlock();
        }
    } else {
        if (crop->in1_w != 0 && crop->in1_h != 0) {
            dstOffset = (dstOffset + 1) % NUM_MORE_BUFS;
            offset = kPreviewBufferCount + dstOffset;
            ssize_t dstOffset_addr = offset * mPreviewHeap->mAlignedBufferSize;
            if( !native_zoom_image(mPreviewHeap->mHeap->getHeapID(),
                offset_addr, dstOffset_addr, crop)) {
                LOGE(" Error while doing MDP zoom ");
                offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
            }
        }
        if (mCurrentTarget == TARGET_MSM7627) {
            mLastQueuedFrame = (void *)mPreviewHeap->mBuffers[offset]->pointer();
        }
    }
    if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME))
        pcb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap->mBuffers[offset],
            pdata);

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
    mInPreviewCallback = false;

//    LOGV("receivePreviewFrame X");
}


bool QualcommCameraHardware::initRecord()
{
    const char *pmem_region;
    int CbCrOffset;
    int recordBufferSize;

    LOGV("initREcord E");

    /* when DIS is ON, video dimensions should be increased,
     * as a requirement from VFE-VPE operations. This logic will be
     * moved down to camera stack coming forward.
     */
    if(mVpeEnabled && mDisEnabled) {
        /* As 5MP sensor, in QTR mode can't output frame of size greater than 1280,
         * don't request for bigger buffer
         */
        if(((mDimension.video_width == 1280 && mDimension.video_height == 720)
          && !strcmp(sensorType->name, "5mp")) || (mDimension.video_width == 1920 && mDimension.video_height == 1088)) {
            LOGI("video resolution (%dx%d) is not supported for sensor (%s) when DIS is ON",
                   mDimension.video_width, mDimension.video_height, sensorType->name);
        } else {
            int width = mDimension.video_width *1.1;
            mDimension.video_width = CEILING32(width);
            int height = mDimension.video_height *1.1;
            mDimension.video_height = CEILING32(height);
        }
    }

    if(mCurrentTarget == TARGET_MSM8660)
        pmem_region = "/dev/pmem_smipool";
    else
        pmem_region = "/dev/pmem_adsp";

    // for 8x60 the Encoder expects the CbCr offset should be aligned to 2K.
    if(mCurrentTarget == TARGET_MSM8660) {
        CbCrOffset = PAD_TO_2K(mDimension.video_width  * mDimension.video_height);
        recordBufferSize = CbCrOffset + PAD_TO_2K((mDimension.video_width * mDimension.video_height)/2);
    } else {
        CbCrOffset = PAD_TO_WORD(mDimension.video_width  * mDimension.video_height);
        recordBufferSize = (mDimension.video_width  * mDimension.video_height *3)/2;
    }
    LOGV("initRecord: mDimension.video_width = %d mDimension.video_height = %d",
             mDimension.video_width, mDimension.video_height);

    /* Buffersize and frameSize will be different when DIS is ON.
     * We need to pass the actual framesize with video heap, as the same
     * is used at camera MIO when negotiating with encoder.
     */
    mRecordFrameSize = recordBufferSize;
    if(mVpeEnabled && mDisEnabled){
        mRecordFrameSize = videoWidth * videoHeight * 3 / 2;
        if(mCurrentTarget == TARGET_MSM8660){
            mRecordFrameSize = PAD_TO_2K(videoWidth * videoHeight)
                                + PAD_TO_2K((videoWidth * videoHeight)/2);
        }
    }
    LOGV("mRecordFrameSize = %d", mRecordFrameSize);

    mRecordHeap = new PmemPool(pmem_region,
                               MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                mCameraControlFd,
                                MSM_PMEM_VIDEO,
                                recordBufferSize,
                                kRecordBufferCount,
                                mRecordFrameSize,
                                CbCrOffset,
                                0,
                                "record");

    if (!mRecordHeap->initialized()) {
        mRecordHeap.clear();
        LOGE("initRecord X: could not initialize record heap.");
        return false;
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
    LINK_cam_frame_flush_free_video();
    if(mVpeEnabled) {
        //If VPE is enabled, the VPE buffer shouldn't be added to Free Q initally.
        for(int i=ACTIVE_VIDEO_BUFFERS+1;i <kRecordBufferCount-1; i++)
            LINK_camframe_free_video(&recordframes[i]);
    } else {
        for(int i=ACTIVE_VIDEO_BUFFERS+1;i <kRecordBufferCount; i++)
            LINK_camframe_free_video(&recordframes[i]);
    }
    LOGV("initREcord X");

    return true;
}

status_t QualcommCameraHardware::setVpeParameters()
{
    LOGV("setVpeParameters E");

    video_dis_param_ctrl_t disCtrl;
    video_rotation_param_ctrl_t rotCtrl;
    bool ret = true;
    LOGV("mDisEnabled = %d", mDisEnabled);
    LOGV("videoWidth = %d, videoHeight = %d", videoWidth, videoHeight);
    if(mDisEnabled) {
        /* As 5MP sensor, in QTR mode can't output frame of size greater than 1280,
         * don't set DIS to ON.
         */
        int video_frame_cbcroffset;
        video_frame_cbcroffset = PAD_TO_WORD(videoWidth * videoHeight);
        if(mCurrentTarget == TARGET_MSM8660)
            video_frame_cbcroffset = PAD_TO_2K(videoWidth * videoHeight);

        disCtrl.dis_enable = mDisEnabled;
        disCtrl.video_rec_width = videoWidth;
        disCtrl.video_rec_height = videoHeight;
        disCtrl.output_cbcr_offset = video_frame_cbcroffset;

        if(((videoWidth == 1280 && videoHeight == 720)
            && !strcmp(sensorType->name, "5mp"))||(videoWidth == 1920 && videoHeight == 1088)) {
            LOGI("video resolution (%dx%d) is not supported for sensor (%s) when DIS is ON",
                   mDimension.video_width, mDimension.video_height, sensorType->name);
            disCtrl.dis_enable = 0;
        }
        ret = native_set_parm(CAMERA_SET_VIDEO_DIS_PARAMS,
                           sizeof(disCtrl), &disCtrl);
    }

    rotCtrl.rotation = (mRotation == 0) ? ROT_NONE :
                       ((mRotation == 90) ? ROT_CLOCKWISE_90 :
                  ((mRotation == 180) ? ROT_CLOCKWISE_180 : ROT_CLOCKWISE_270));

    if( ((videoWidth == 1280 && videoHeight == 720) || (videoWidth == 800 && videoHeight == 480))
        && (mRotation == 90 || mRotation == 270) ){
        /* Due to a limitation at video core to support heights greater than 720, adding this check.
         * This is a temporary hack, need to be removed once video core support is available
         */
        LOGI("video resolution (%dx%d) with rotation (%d) is not supported",
            videoWidth, videoHeight, mRotation);
        rotCtrl.rotation = ROT_NONE;
    }
    LOGV("rotCtrl.rotation = %d", rotCtrl.rotation);

    ret = native_set_parm(CAMERA_SET_VIDEO_ROT_PARAMS,
                           sizeof(rotCtrl), &rotCtrl);

    LOGV("setVpeParameters X (%d)", ret);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::startRecording()
{
    LOGV("startRecording E");
    int ret;
    Mutex::Autolock l(&mLock);
    mReleasedRecordingFrame = false;
    if( (ret=startPreviewInternal())== NO_ERROR){
        if(mVpeEnabled){
            LOGI("startRecording: VPE enabled, setting vpe parameters");
            bool status = setVpeParameters();
            if(status) {
                LOGE("Failed to set VPE parameters");
                return status;
            }
        }
        if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660))  {
            LOGV(" in startREcording : calling native_start_recording");
            native_start_recording(mCameraControlFd);
            recordingState = 1;
            // Remove the left out frames in busy Q and them in free Q.
            // this should be done before starting video_thread so that,
            // frames in previous recording are flushed out.
            LOGV("frames in busy Q = %d", g_busy_frame_queue.num_of_frames);
            while((g_busy_frame_queue.num_of_frames) >0){
                msm_frame* vframe = cam_frame_get_video ();
                LINK_camframe_free_video(vframe);
            }
            LOGV("frames in busy Q = %d after deQueing", g_busy_frame_queue.num_of_frames);

            //Clear the dangling buffers and put them in free queue
            for(int cnt = 0; cnt < kRecordBufferCount; cnt++) {
                if(record_buffers_tracking_flag[cnt] == true) {
                    LOGI("Dangling buffer: offset = %d, buffer = %d", cnt, (unsigned int)recordframes[cnt].buffer);
                    LINK_camframe_free_video(&recordframes[cnt]);
                    record_buffers_tracking_flag[cnt] = false;
                }
            }

            // Start video thread and wait for busy frames to be encoded, this thread
            // should be closed in stopRecording
            mVideoThreadWaitLock.lock();
            mVideoThreadExit = 0;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            mVideoThreadRunning = pthread_create(&mVideoThread,
                                              &attr,
                                              video_thread,
                                              NULL);
            mVideoThreadWaitLock.unlock();
            // Remove the left out frames in busy Q and them in free Q.
        }
    }
    return ret;
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
        mVideoThreadWaitLock.lock();
        mVideoThreadExit = 1;
        mVideoThreadWaitLock.unlock();
        native_stop_recording(mCameraControlFd);

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
                LINK_camframe_free_video(releaseframe);
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

void QualcommCameraHardware::notifyShutter(common_crop_t *crop, bool mPlayShutterSoundOnly)
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
        LOGV("out2_w=%d, out2_h=%d, in2_w=%d, in2_h=%d",
             crop->out2_w, crop->out2_h, crop->in2_w, crop->in2_h);
        LOGV("out1_w=%d, out1_h=%d, in1_w=%d, in1_h=%d",
             crop->out1_w, crop->out1_h, crop->in1_w, crop->in1_h);

        // To workaround a bug in MDP which happens if either
        // dimension > 2048, we display the thumbnail instead.

        if (mCurrentTarget == TARGET_MSM7627)
            mDisplayHeap = mPostViewHeap;
        else
            mDisplayHeap = mRawHeap;

        if (crop->in1_w == 0 || crop->in1_h == 0) {
            // Full size
            size.width = mDimension.picture_width;
            size.height = mDimension.picture_height;
            if (size.width > 2048 || size.height > 2048) {
                size.width = mDimension.ui_thumbnail_width;
                size.height = mDimension.ui_thumbnail_height;
                mDisplayHeap = mThumbnailHeap;
            }
            else {
                if (mCurrentTarget == TARGET_MSM7627) {
                    size.width = previewWidth;
                    size.height = previewHeight;
                }
            }
        } else {
            // Cropped
            size.width = (crop->in2_w + jpegPadding) & ~1;
            size.height = (crop->in2_h + jpegPadding) & ~1;
            if (size.width > 2048 || size.height > 2048) {
                size.width = (crop->in1_w + jpegPadding) & ~1;
                size.height = (crop->in1_h + jpegPadding) & ~1;
                mDisplayHeap = mThumbnailHeap;
            }
            else {
                if (mCurrentTarget == TARGET_MSM7627) {
                    size.width = previewWidth;
                    size.height = previewHeight;
                }
            }
        }
        //For Adreno format, we need to pass the main image in all the cases.
        if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO)
            mDisplayHeap = mRawHeap;

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
        obj->notifyShutter(crop, TRUE);
    }
    LOGV("receive_shutter_callback: X");
}

// Crop the picture in place.
static void crop_yuv420(uint32_t width, uint32_t height,
                 uint32_t cropped_width, uint32_t cropped_height,
                 uint8_t *image, const char *name)
{
    int32_t i;
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
        for(i=position; i>=0; i--){
            memmove(image + yOffsetDst + i * cropped_width,
                    image + yOffsetSrc + width * (y + i) + x,
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
        for(i=position; i >=0; i--){
            memmove(chroma_dst + i * cropped_width,
                    chroma_src + width * (y + i) + x,
                    cropped_width);
        }
    } else {
        for(i = 0; i < cropped_height; i++)
            memcpy(chroma_dst + i * cropped_width,
                   chroma_src + width * (y + i) + x,
                   cropped_width);
    }
}

void QualcommCameraHardware::receiveRawSnapshot(){
    LOGV("receiveRawSnapshot E");

    Mutex::Autolock cbLock(&mCallbackLock);
    /* Issue notifyShutter with mPlayShutterSoundOnly as TRUE */
    notifyShutter(&mCrop, TRUE);

    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {

        if(native_get_picture(mCameraControlFd, &mCrop) == false) {
            LOGE("receiveRawSnapshot X: native_get_picture failed!");
            return;
        }
        /* Its necessary to issue another notifyShutter here with
         * mPlayShutterSoundOnly as FALSE, since that is when the
         * preview buffers are unregistered with the surface flinger.
         * That is necessary otherwise the preview memory wont be
         * deallocated.
         */
        notifyShutter(&mCrop, FALSE);

        //Create a Ashmem heap to copy data from PMem heap for application layer
        if(mRawSnapshotAshmemHeap != NULL){
            LOGV("receiveRawSnapshot: clearing old mRawSnapShotAshmemHeap.");
            mRawSnapshotAshmemHeap.clear();
        }
        mRawSnapshotAshmemHeap = new AshmemPool(
                                        mRawSnapShotPmemHeap->mBufferSize,
                                        mRawSnapShotPmemHeap->mNumBuffers,
                                        mRawSnapShotPmemHeap->mFrameSize,
                                        "raw ashmem snapshot camera"
                                        );

        if(!mRawSnapshotAshmemHeap->initialized()){
            LOGE("receiveRawSnapshot X: error initializing mRawSnapshotHeap");
            deinitRawSnapshot();
            return;
        }

        memcpy(mRawSnapshotAshmemHeap->mHeap->base(),
                mRawSnapShotPmemHeap->mHeap->base(),
                mRawSnapShotPmemHeap->mHeap->getSize());
       if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE))
           mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, mRawSnapshotAshmemHeap->mBuffers[0],
                mCallbackCookie);

    }

    //cleanup
    deinitRawSnapshot();

    LOGV("receiveRawSnapshot X");
}

void QualcommCameraHardware::receiveRawPicture()
{
    LOGV("receiveRawPicture: E");

    Mutex::Autolock cbLock(&mCallbackLock);
    if (mDataCallback && ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) || mSnapshotDone)) {
        if(native_get_picture(mCameraControlFd, &mCrop) == false) {
            LOGE("getPicture failed!");
            return;
        }
        mSnapshotDone = FALSE;
        mCrop.in1_w &= ~1;
        mCrop.in1_h &= ~1;
        mCrop.in2_w &= ~1;
        mCrop.in2_h &= ~1;


        // Crop the image if zoomed.
        if (mCrop.in2_w != 0 && mCrop.in2_h != 0 &&
                ((mCrop.in2_w + jpegPadding) < mCrop.out2_w) &&
                ((mCrop.in2_h + jpegPadding) < mCrop.out2_h) &&
                ((mCrop.in1_w + jpegPadding) < mCrop.out1_w)  &&
                ((mCrop.in1_h + jpegPadding) < mCrop.out1_h) ) {

            // By the time native_get_picture returns, picture is taken. Call
            // shutter callback if cam config thread has not done that.
            notifyShutter(&mCrop, FALSE);
            {
                Mutex::Autolock l(&mRawPictureHeapLock);
                if(mRawHeap != NULL)
                    crop_yuv420(mCrop.out2_w, mCrop.out2_h, (mCrop.in2_w + jpegPadding), (mCrop.in2_h + jpegPadding),
                            (uint8_t *)mRawHeap->mHeap->base(), mRawHeap->mName);
                if(mThumbnailHeap != NULL) {
                    crop_yuv420(mCrop.out1_w, mCrop.out1_h, (mCrop.in1_w + jpegPadding), (mCrop.in1_h + jpegPadding),
                            (uint8_t *)mThumbnailHeap->mHeap->base(), mThumbnailHeap->mName);
                }
            }

            // We do not need jpeg encoder to upscale the image. Set the new
            // dimension for encoder.
            mDimension.orig_picture_dx = mCrop.in2_w + jpegPadding;
            mDimension.orig_picture_dy = mCrop.in2_h + jpegPadding;
            mDimension.thumbnail_width = mCrop.in1_w + jpegPadding;
            mDimension.thumbnail_height = mCrop.in1_h + jpegPadding;
        }else {
            memset(&mCrop, 0 ,sizeof(mCrop));
            // By the time native_get_picture returns, picture is taken. Call
            // shutter callback if cam config thread has not done that.
            notifyShutter(&mCrop, FALSE);
        }

	if( mUseOverlay && (mOverlay != NULL) ) {
            mOverlay->setFd(mPostViewHeap->mHeap->getHeapID());
	    if( zoomCropInfo.w !=0 && zoomCropInfo.h !=0) {
		LOGD(" zoomCropInfo non-zero, setting crop ");
		mOverlay->setCrop(zoomCropInfo.x, zoomCropInfo.y,
			zoomCropInfo.w, zoomCropInfo.h);
	    }
	    LOGD(" Queueing Postview for display ");
	    mOverlay->queueBuffer((void *)0);
	}
   if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE))
       mDataCallback(CAMERA_MSG_RAW_IMAGE, mDisplayHeap->mBuffers[0],
                            mCallbackCookie);
       if(mPreviewFormat == CAMERA_YUV_420_NV21_ADRENO) {
           LOGE("Raw Data given to app for processing...will wait for jpeg encode call");
           mEncodePendingWaitLock.lock();
           mEncodePending = true;
           mEncodePendingWaitLock.unlock();
       }
    }
    else LOGV("Raw-picture callback was canceled--skipping.");

    if(mPreviewFormat != CAMERA_YUV_420_NV21_ADRENO) {
        if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
            mJpegSize = 0;
            mJpegThreadWaitLock.lock();
            if (LINK_jpeg_encoder_init()) {
                mJpegThreadRunning = true;
                mJpegThreadWaitLock.unlock();
                if(native_jpeg_encode()) {
                    LOGV("receiveRawPicture: X (success)");
                    return;
                }
                LOGE("jpeg encoding failed");
            }
            else {
                LOGE("receiveRawPicture X: jpeg_encoder_init failed.");
                mJpegThreadWaitLock.unlock();
            }
        }
        else LOGV("JPEG callback is NULL, not encoding image.");
        deinitRaw();
    }
    LOGV("receiveRawPicture: X");
}

void QualcommCameraHardware::receiveJpegPictureFragment(
    uint8_t *buff_ptr, uint32_t buff_size)
{
    LOGV("receiveJpegPictureFragment size %d", buff_size);
    uint32_t remaining = mJpegHeap->mHeap->virtualSize();
    remaining -= mJpegSize;
    uint8_t *base = (uint8_t *)mJpegHeap->mHeap->base();

    if (buff_size > remaining) {
        LOGE("receiveJpegPictureFragment: size %d exceeds what "
             "remains in JPEG heap (%d), truncating",
             buff_size,
             remaining);
        buff_size = remaining;
    }
    memcpy(base + mJpegSize, buff_ptr, buff_size);
    mJpegSize += buff_size;
}

void QualcommCameraHardware::receiveJpegPicture(void)
{
    LOGV("receiveJpegPicture: E image (%d uint8_ts out of %d)",
         mJpegSize, mJpegHeap->mBufferSize);
    Mutex::Autolock cbLock(&mCallbackLock);

    int index = 0, rc;

    if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
        // that the JPEG image's size will probably change from one snapshot
        // to the next, so we cannot reuse the MemoryBase object.
        sp<MemoryBase> buffer = new
            MemoryBase(mJpegHeap->mHeap,
                       index * mJpegHeap->mBufferSize +
                       0,
                       mJpegSize);
        mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
        buffer = NULL;
    }
    else LOGV("JPEG callback was cancelled--not delivering image.");

    mJpegThreadWaitLock.lock();
    mJpegThreadRunning = false;
    mJpegThreadWait.signal();
    mJpegThreadWaitLock.unlock();

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
    return mCameraRunning && mDataCallback &&
           ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mOverlay != NULL));
}
status_t QualcommCameraHardware::setRecordSize(const CameraParameters& params)
{
    int width, height;
    const char *str = params.get("record-size");
    if(str) {
        LOGV("Requested Record size %s", str);
        mParameters.set("record-size" , str);
    } else {
        //set the record-size value to null.
        //This is required as the application can request
        //to reset this value, so that it won't be carried
        //when switched to camera.
        mParameters.set("record-size", "");
    }
    return NO_ERROR;
}


status_t QualcommCameraHardware::setPreviewSize(const CameraParameters& params)
{
    int width, height;
    params.getPreviewSize(&width, &height);
    LOGV("requested preview size %d x %d", width, height);

    // Validate the preview size
    for (size_t i = 0; i < previewSizeCount; ++i) {
        if (width == supportedPreviewSizes[i].width
           && height == supportedPreviewSizes[i].height) {
            mParameters.setPreviewSize(width, height);
            mDimension.display_width = width;
            mDimension.display_height= height;
            return NO_ERROR;
        }
    }
    LOGE("Invalid preview size requested: %dx%d", width, height);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRate(const CameraParameters& params)
{
    //Temporary check for mipi sensor
    if((!strcmp(mSensorInfo.name, "vx6953")) ||
        (!strcmp(mSensorInfo.name, "VX6953")) ||
        (!strcmp(sensorType->name, "2mp"))){
        LOGI("set fps is not supported for this sensor");
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
        bool ret = native_set_parm(CAMERA_SET_PARM_FPS,
                sizeof(fps), (void *)&fps);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRateMode(const CameraParameters& params) {
    //Temporary check for mipi sensor
    if((!strcmp(mSensorInfo.name, "vx6953")) ||
        (!strcmp(mSensorInfo.name, "VX6953")) ||
        (!strcmp(sensorType->name, "2mp"))){
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
        bool ret = native_set_parm(CAMERA_SET_FPS_MODE, sizeof(frameRateMode), (void *)&frameRateMode);
        if(!ret) return ret;
        //set the fps value when chaging modes
        int16_t fps = (uint16_t)params.getPreviewFrameRate();
        if(MINIMUM_FPS <= fps && fps <=MAXIMUM_FPS){
            mParameters.setPreviewFrameRate(fps);
            ret = native_set_parm(CAMERA_SET_PARM_FPS,
                                        sizeof(fps), (void *)&fps);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
        LOGI("Invalid preview frame rate value: %d", fps);
        return BAD_VALUE;
    }
    LOGI("Invalid preview frame rate mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
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
    if (quality > 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg quality=%d", quality);
        rc = BAD_VALUE;
    }

    quality = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (quality > 0 && quality <= 100) {
        mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, quality);
    } else {
        LOGE("Invalid jpeg thumbnail quality=%d", quality);
        rc = BAD_VALUE;
    }
    return rc;
}

status_t QualcommCameraHardware::setEffect(const CameraParameters& params)
{

    const char *str_wb = mParameters.get(CameraParameters::KEY_WHITE_BALANCE);
    int32_t value_wb = attr_lookup(whitebalance, sizeof(whitebalance) / sizeof(str_map), str_wb);
    const char *str = params.get(CameraParameters::KEY_EFFECT);

    if (str != NULL) {
        int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            if(!strcmp(sensorType->name, "2mp") && (value != CAMERA_EFFECT_OFF)
               &&(value != CAMERA_EFFECT_MONO) && (value != CAMERA_EFFECT_NEGATIVE)
               &&(value != CAMERA_EFFECT_SOLARIZE) && (value != CAMERA_EFFECT_SEPIA)) {
                LOGE("Special effect parameter is not supported for this sensor");
                return NO_ERROR;
            }

           if(((value == CAMERA_EFFECT_MONO) || (value == CAMERA_EFFECT_NEGATIVE)
           || (value == CAMERA_EFFECT_AQUA) || (value == CAMERA_EFFECT_SEPIA))
               && (value_wb != CAMERA_WB_AUTO)) {
               LOGE("Color Effect value will not be set " \
               "when the whitebalance selected is %s", str_wb);
               return NO_ERROR;
           }
           else {
               mParameters.set(CameraParameters::KEY_EFFECT, str);
               bool ret = native_set_parm(CAMERA_SET_PARM_EFFECT, sizeof(value),
                                           (void *)&value);
               return ret ? NO_ERROR : UNKNOWN_ERROR;
          }
        }
    }
    LOGE("Invalid effect value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setExposureCompensation(
        const CameraParameters & params){

    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Exposure Compensation is not supported for this sensor");
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
        bool ret = native_set_parm(CAMERA_SET_PARM_EXPOSURE_COMPENSATION,
                                    sizeof(value), (void *)&value);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    }
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setAutoExposure(const CameraParameters& params)
{
    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Auto Exposure not supported for this sensor");
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_AUTO_EXPOSURE);
    if (str != NULL) {
        int32_t value = attr_lookup(autoexposure, sizeof(autoexposure) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE, str);
            bool ret = native_set_parm(CAMERA_SET_PARM_EXPOSURE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid auto exposure value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setSharpness(const CameraParameters& params)
{
    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Sharpness not supported for this sensor");
        return NO_ERROR;
    }
    int sharpness = params.getInt(CameraParameters::KEY_SHARPNESS);
    if((sharpness < CAMERA_MIN_SHARPNESS
            || sharpness > CAMERA_MAX_SHARPNESS))
        return UNKNOWN_ERROR;

    LOGV("setting sharpness %d", sharpness);
    mParameters.set(CameraParameters::KEY_SHARPNESS, sharpness);
    bool ret = native_set_parm(CAMERA_SET_PARM_SHARPNESS, sizeof(sharpness),
                               (void *)&sharpness);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setContrast(const CameraParameters& params)
{
    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Contrast not supported for this sensor");
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
        bool ret = native_set_parm(CAMERA_SET_PARM_CONTRAST, sizeof(contrast),
                                   (void *)&contrast);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
          LOGE(" Contrast value will not be set " \
          "when the scenemode selected is %s", str);
    return NO_ERROR;
    }
}

status_t QualcommCameraHardware::setSaturation(const CameraParameters& params)
{
    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Saturation not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_EFFECT);
    int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);

    if( (value != CAMERA_EFFECT_MONO) && (value != CAMERA_EFFECT_NEGATIVE)
	    && (value != CAMERA_EFFECT_AQUA) && (value != CAMERA_EFFECT_SEPIA)) {

	int saturation = params.getInt(CameraParameters::KEY_SATURATION);
	if((saturation < CAMERA_MIN_SATURATION)
		|| (saturation > CAMERA_MAX_SATURATION))
	    return UNKNOWN_ERROR;

	LOGV("Setting saturation %d", saturation);
	mParameters.set(CameraParameters::KEY_SATURATION, saturation);
	bool ret = native_set_parm(CAMERA_SET_PARM_SATURATION, sizeof(saturation),
		(void *)&saturation);
	return ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
	LOGE(" Saturation value will not be set " \
		"when the effect selected is %s", str);
	return NO_ERROR;
    }
}

status_t QualcommCameraHardware::setPreviewFormat(const CameraParameters& params) {
    const char *str = params.getPreviewFormat();
    int32_t previewFormat = attr_lookup(preview_formats, sizeof(preview_formats) / sizeof(str_map), str);
    if(previewFormat != NOT_FOUND) {
        mParameters.set(CameraParameters::KEY_PREVIEW_FORMAT, str);
        mPreviewFormat = previewFormat;
        return NO_ERROR;
    }
    LOGI("Invalid preview format value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setStrTextures(const CameraParameters& params) {
    const char *str = params.get("strtextures");
    if(str != NULL) {
        LOGI("strtextures = %s", str);
        mParameters.set("strtextures", str);
        if(mUseOverlay) {
            if(!strncmp(str, "on", 2) || !strncmp(str, "ON", 2)) {
                LOGI("Resetting mUseOverlay to false");
                mUseOverlay = false;
            }
        }
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setBrightness(const CameraParameters& params) {
        int brightness = params.getInt("luma-adaptation");
        if (mBrightness !=  brightness) {
            LOGV(" new brightness value : %d ", brightness);
            mBrightness =  brightness;

            bool ret = native_set_parm(CAMERA_SET_PARM_BRIGHTNESS, sizeof(mBrightness),
                                       (void *)&mBrightness);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        } else {
            return NO_ERROR;
        }
}

status_t QualcommCameraHardware::setWhiteBalance(const CameraParameters& params)
{

    const char *str_effect = mParameters.get(CameraParameters::KEY_EFFECT);
    int32_t value_effect = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str_effect);

    if( (value_effect != CAMERA_EFFECT_MONO) && (value_effect != CAMERA_EFFECT_NEGATIVE)
    && (value_effect != CAMERA_EFFECT_AQUA) && (value_effect != CAMERA_EFFECT_SEPIA)) {
        const char *str = params.get(CameraParameters::KEY_WHITE_BALANCE);

        if (str != NULL) {
            int32_t value = attr_lookup(whitebalance, sizeof(whitebalance) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                mParameters.set(CameraParameters::KEY_WHITE_BALANCE, str);
                bool ret = native_set_parm(CAMERA_SET_PARM_WB, sizeof(value),
                                           (void *)&value);
                return ret ? NO_ERROR : UNKNOWN_ERROR;
            }
        }
        LOGE("Invalid whitebalance value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    } else {
            LOGE("Whitebalance value will not be set " \
            "when the effect selected is %s", str_effect);
            return NO_ERROR;
    }
}

status_t QualcommCameraHardware::setFlash(const CameraParameters& params)
{
    if (!mSensorInfo.flash_enabled) {
        LOGV("%s: flash not supported", __FUNCTION__);
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_FLASH_MODE);
    if (str != NULL) {
        int32_t value = attr_lookup(flash, sizeof(flash) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FLASH_MODE, str);
            bool ret = native_set_parm(CAMERA_SET_PARM_LED_MODE,
                                       sizeof(value), (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid flash mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setAntibanding(const CameraParameters& params)
{
    if(!strcmp(sensorType->name, "2mp")) {
        LOGE("Parameter AntiBanding is not supported for this sensor");
        return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_ANTIBANDING);
    if (str != NULL) {
        int value = (camera_antibanding_type)attr_lookup(
          antibanding, sizeof(antibanding) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            camera_antibanding_type temp = (camera_antibanding_type) value;
            mParameters.set(CameraParameters::KEY_ANTIBANDING, str);
            bool ret;
            if (temp == CAMERA_ANTIBANDING_AUTO) {
                ret = native_set_parm(CAMERA_ENABLE_AFD,
                            0, NULL);
            } else {
                ret = native_set_parm(CAMERA_SET_PARM_ANTIBANDING,
                            sizeof(camera_antibanding_type), (void *)&temp);
            }
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid antibanding value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setLensshadeValue(const CameraParameters& params)
{
    if( (!strcmp(sensorType->name, "2mp")) ||
        (!strcmp(mSensorInfo.name, "vx6953")) ||
	(!strcmp(mSensorInfo.name, "VX6953")) ) {
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

            native_set_parm(CAMERA_SET_PARM_ROLLOFF, sizeof(int8_t), (void *)&temp);
            return NO_ERROR;
        }
    }
    LOGE("Invalid lensShade value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setContinuousAf(const CameraParameters& params)
{
    if(sensorType->hasAutoFocusSupport){
        const char *str = params.get(CameraParameters::KEY_CONTINUOUS_AF);
        if (str != NULL) {
            int value = attr_lookup(continuous_af,
                    sizeof(continuous_af) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                int8_t temp = (int8_t)value;
                mParameters.set(CameraParameters::KEY_CONTINUOUS_AF, str);

                native_set_parm(CAMERA_SET_CAF, sizeof(int8_t), (void *)&temp);
                return NO_ERROR;
            }
        }
        LOGE("Invalid continuous Af value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setTouchAfAec(const CameraParameters& params)
{
    if(sensorType->hasAutoFocusSupport){
        int xAec, yAec, xAf, yAf;

        params.getTouchIndexAec(&xAec, &yAec);
        params.getTouchIndexAf(&xAf, &yAf);
        const char *str = params.get(CameraParameters::KEY_TOUCH_AF_AEC);

        if (str != NULL) {
            int value = attr_lookup(touchafaec,
                    sizeof(touchafaec) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
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
                    //Set Touch AEC params 
                    aec_roi_value.aec_roi_enable = AEC_ROI_ON;
                    aec_roi_value.aec_roi_type = AEC_ROI_BY_COORDINATE;
                    aec_roi_value.aec_roi_position.coordinate.x = xAec;
                    aec_roi_value.aec_roi_position.coordinate.y = yAec;

                    //Set Touch AF params
                    af_roi_value.num_roi = 1;
                    af_roi_value.roi[0].x = xAf;
                    af_roi_value.roi[0].y = yAf;
                    af_roi_value.roi[0].dx = 100;
                    af_roi_value.roi[0].dy = 100;
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
                native_set_parm(CAMERA_SET_PARM_AEC_ROI, sizeof(cam_set_aec_roi_t), (void *)&aec_roi_value);
                native_set_parm(CAMERA_SET_PARM_AF_ROI, sizeof(roi_info_t), (void*)&af_roi_value);
            }
            return NO_ERROR;
        }
        LOGE("Invalid Touch AF/AEC value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t  QualcommCameraHardware::setISOValue(const CameraParameters& params) {
    int8_t temp_hjr;
    const char *str = params.get(CameraParameters::KEY_ISO_MODE);
    if (str != NULL) {
        int value = (camera_iso_mode_type)attr_lookup(
          iso, sizeof(iso) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            camera_iso_mode_type temp = (camera_iso_mode_type) value;
            if (value == CAMERA_ISO_DEBLUR) {
               temp_hjr = true;
               native_set_parm(CAMERA_SET_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
               mHJR = value;
            }
            else {
               if (mHJR == CAMERA_ISO_DEBLUR) {
                   temp_hjr = false;
                   native_set_parm(CAMERA_SET_PARM_HJR, sizeof(int8_t), (void*)&temp_hjr);
                   mHJR = value;
               }
            }

            mParameters.set(CameraParameters::KEY_ISO_MODE, str);
            native_set_parm(CAMERA_SET_PARM_ISO, sizeof(camera_iso_mode_type), (void *)&temp);
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
        const char *str = params.get(CameraParameters::KEY_SCENE_DETECT);
        if (str != NULL) {
            int32_t value = attr_lookup(scenedetect, sizeof(scenedetect) / sizeof(str_map), str);
            if (value != NOT_FOUND) {
                mParameters.set(CameraParameters::KEY_SCENE_DETECT, str);

                retParm1 = native_set_parm(CAMERA_SET_PARM_BL_DETECTION_ENABLE, sizeof(value),
                                           (void *)&value);

                retParm2 = native_set_parm(CAMERA_SET_PARM_SNOW_DETECTION_ENABLE, sizeof(value),
                                           (void *)&value);

                //All Auto Scene detection modes should be all ON or all OFF.
                if(retParm1 == false || retParm2 == false) {
                    value = !value;
                    retParm1 = native_set_parm(CAMERA_SET_PARM_BL_DETECTION_ENABLE, sizeof(value),
                                               (void *)&value);

                    retParm2 = native_set_parm(CAMERA_SET_PARM_SNOW_DETECTION_ENABLE, sizeof(value),
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

    if(!strcmp(sensorType->name, "2mp")) {
        LOGI("Parameter Scenemode is not supported for this sensor");
        return NO_ERROR;
    }

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);

    if (str != NULL) {
        int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_SCENE_MODE, str);
            bool ret = native_set_parm(CAMERA_SET_PARM_BESTSHOT_MODE, sizeof(value),
                                       (void *)&value);
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid scenemode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}
status_t QualcommCameraHardware::setGpsLocation(const CameraParameters& params)
{
    const char *latitude = params.get(CameraParameters::KEY_GPS_LATITUDE);
    if (latitude) {
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE, latitude);
    }

    const char *latitudeRef = params.get(CameraParameters::KEY_GPS_LATITUDE_REF);
    if (latitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_LATITUDE_REF, latitudeRef);
    }

    const char *longitude = params.get(CameraParameters::KEY_GPS_LONGITUDE);
    if (longitude) {
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE, longitude);
    }

    const char *longitudeRef = params.get(CameraParameters::KEY_GPS_LONGITUDE_REF);
    if (longitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_LONGITUDE_REF, longitudeRef);
    }

    const char *altitudeRef = params.get(CameraParameters::KEY_GPS_ALTITUDE_REF);
    if (altitudeRef) {
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE_REF, altitudeRef);
    }

    const char *altitude = params.get(CameraParameters::KEY_GPS_ALTITUDE);
    if (altitude) {
        mParameters.set(CameraParameters::KEY_GPS_ALTITUDE, altitude);
    }

    const char *status = params.get(CameraParameters::KEY_GPS_STATUS);
    if (status) {
        mParameters.set(CameraParameters::KEY_GPS_STATUS, status);
    }

    const char *dateTime = params.get(CameraParameters::KEY_EXIF_DATETIME);
    if (dateTime) {
        mParameters.set(CameraParameters::KEY_EXIF_DATETIME, dateTime);
    }

    const char *timestamp = params.get(CameraParameters::KEY_GPS_TIMESTAMP);
    if (timestamp) {
        mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, timestamp);
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

    LOGV("Set zoom=%d", zoom_level);
    if(zoom_level >= 0 && zoom_level <= mMaxZoom) {
        mParameters.set("zoom", zoom_level);
        int32_t zoom_value = ZOOM_STEP * zoom_level;
        bool ret = native_set_parm(CAMERA_SET_PARM_ZOOM,
            sizeof(zoom_value), (void *)&zoom_value);
        rc = ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
        rc = BAD_VALUE;
    }

    return rc;
}

status_t QualcommCameraHardware::setFocusMode(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_FOCUS_MODE);
    if (str != NULL) {
        int32_t value = attr_lookup(focus_modes,
                                    sizeof(focus_modes) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FOCUS_MODE, str);
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

static bool register_buf(int camfd,
                         int size,
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
                                           int camera_control_fd,
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
    myOffset(yOffset),
    mCameraControlFd(dup(camera_control_fd))
{
    LOGV("constructing MemPool %s backed by pmem pool %s: "
         "%d frames @ %d bytes, buffer size %d",
         mName,
         pmem_pool, num_buffers, frame_size,
         buffer_size);

    LOGV("%s: duplicating control fd %d --> %d",
         __FUNCTION__,
         camera_control_fd, mCameraControlFd);

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
                     active = (cnt < (num_buf-1));
                }
                register_buf(mCameraControlFd,
                         mBufferSize,
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
}

QualcommCameraHardware::PmemPool::~PmemPool()
{
    LOGV("%s: %s E", __FUNCTION__, mName);
    if (mHeap != NULL) {
        // Unregister preview buffers with the camera drivers.
        //  Only Unregister the preview, snapshot and thumbnail
        //  buffers with the kernel.
        if( (strcmp("postview", mName) != 0) ){
            int num_buffers = mNumBuffers;
            if(!strcmp("preview", mName)) num_buffers = kPreviewBufferCount;
            for (int cnt = 0; cnt < num_buffers; ++cnt) {
                register_buf(mCameraControlFd,
                         mBufferSize,
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
    LOGV("destroying PmemPool %s: closing control fd %d",
         mName,
         mCameraControlFd);
    close(mCameraControlFd);
    LOGV("%s: %s X", __FUNCTION__, mName);
}

QualcommCameraHardware::MemPool::~MemPool()
{
    LOGV("destroying MemPool %s", mName);
    if (mFrameSize > 0)
        delete [] mBuffers;
    mHeap.clear();
    LOGV("destroying MemPool %s completed", mName);
}

static bool register_buf(int camfd,
                         int size,
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

    pmemBuf.type     = pmem_type;
    pmemBuf.fd       = pmempreviewfd;
    pmemBuf.offset   = offset;
    pmemBuf.len      = size;
    pmemBuf.vaddr    = buf;
    pmemBuf.y_off    = yoffset;
    pmemBuf.cbcr_off = cbcr_offset;

    pmemBuf.active   = vfe_can_write;

    LOGV("register_buf: camfd = %d, reg = %d buffer = %p",
         camfd, !register_buffer, buf);
    if (ioctl(camfd,
              register_buffer ?
              MSM_CAM_IOCTL_REGISTER_PMEM :
              MSM_CAM_IOCTL_UNREGISTER_PMEM,
              &pmemBuf) < 0) {
        LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }
    return true;
}

status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
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


static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size)
{
    LOGV("receive_jpeg_fragment_callback E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveJpegPictureFragment(buff_ptr, buff_size);
    }
    LOGV("receive_jpeg_fragment_callback X");
}

static void receive_jpeg_callback(jpeg_event_t status)
{
    LOGV("receive_jpeg_callback E (completion status %d)", status);
    if (status == JPEG_EVENT_DONE) {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveJpegPicture();
        }
    }
    LOGV("receive_jpeg_callback X");
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
        mOverlay = NULL;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

void QualcommCameraHardware::receive_camframetimeout(void) {
    LOGV("receive_camframetimeout: E");
    Mutex::Autolock l(&mCamframeTimeoutLock);
    LOGE(" Camframe timed out. Not receiving any frames from camera driver ");
    camframe_timeout_flag = TRUE;
    mNotifyCallback(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0,
                    mCallbackCookie);
    LOGV("receive_camframetimeout: X");
}

static void receive_camframetimeout_callback(void) {
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receive_camframetimeout();
    }
}

void QualcommCameraHardware::storePreviewFrameForPostview(void) {
    LOGV(" storePreviewFrameForPostview : E ");

    /* Since there is restriction on the maximum overlay dimensions
     * that can be created, we use the last preview frame as postview
     * for 7x30. */
    LOGV(" Copying the preview buffer to postview buffer %d  ", 
         mPreviewFrameSize);
    if( mPostViewHeap != NULL && mLastQueuedFrame != NULL) {
	memcpy(mPostViewHeap->mHeap->base(),
		(uint8_t *)mLastQueuedFrame, mPreviewFrameSize );
    } else
        LOGE(" Failed to store Preview frame. No Postview ");

    LOGV(" storePreviewFrameForPostview : X ");
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
     && (width <= sensorType->max_supported_snapshot_width)
     && (height <= sensorType->max_supported_snapshot_height) )
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
        mJpegSize = 0;
        mJpegThreadWaitLock.lock();
        if (LINK_jpeg_encoder_init()) {
            mJpegThreadRunning = true;
            mJpegThreadWaitLock.unlock();
            if(native_jpeg_encode()) {
                LOGV("encodeData: X (success)");
                //Wait until jpeg encoding is done and call jpeg join
                //in this context. Also clear the resources.
                mJpegThreadWaitLock.lock();
                while (mJpegThreadRunning) {
                    LOGV("encodeData: waiting for jpeg thread to complete.");
                    mJpegThreadWait.wait(mJpegThreadWaitLock);
                    LOGV("encodeData: jpeg thread completed.");
                }
                mJpegThreadWaitLock.unlock();
                //Call jpeg join in this thread context
                LINK_jpeg_encoder_join();
            }
            LOGE("encodeData: jpeg encoding failed");
        }
        else {
            LOGE("encodeData X: jpeg_encoder_init failed.");
            mJpegThreadWaitLock.unlock();
        }
    }
    else LOGV("encodeData: JPEG callback is NULL, not encoding image.");
    //clear the resources
    deinitRaw();
    //Encoding is done.
    mEncodePendingWaitLock.lock();
    mEncodePending = false;
    mEncodePendingWait.signal();
    mEncodePendingWaitLock.unlock();

    LOGV("encodeData: X");
}

}; // namespace android

