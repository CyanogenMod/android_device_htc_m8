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

#define LOG_NDEBUG 0
#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHAL"
#include <utils/Log.h>

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

#include "QCameraHAL.h"
#include "mm_jpeg_encoder.h"


#include <camera.h>
#include <cam_fifo.h>
#include <liveshot.h>
#include <mm-still/jpeg/jpege.h>

#define DUMP_LIVESHOT_JPEG_FILE 0

#define DEFAULT_PICTURE_WIDTH  1280 //640
#define DEFAULT_PICTURE_HEIGHT 960 //480
#define THUMBNAIL_BUFFER_SIZE (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT * 3/2)
#define MAX_ZOOM_LEVEL 5

// Number of video buffers held by kernal (initially 1,2 &3)
#define ACTIVE_VIDEO_BUFFERS 3
#define ACTIVE_PREVIEW_BUFFERS 3
#define ACTIVE_ZSL_BUFFERS 3
#define APP_ORIENTATION 90

#if DLOPEN_LIBMMCAMERA
//#include <dlfcn.h>
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
#define DEFAULT_PREVIEW_WIDTH 800
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
        //TODO: figure out the correct values for these
        {TARGET_MSM8960, 0x00001fff, true, true, false},
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
//static camera_size_type* picture_sizes;
//static camera_size_type* preview_sizes;
//static camera_size_type* hfr_sizes;

//static struct camera_size_type default_picture_sizes[] = {
static camera_size_type picture_sizes[] = {
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

//static struct camera_size_type default_preview_sizes[] = {
static camera_size_type preview_sizes[] = {
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

static camera_size_type hfr_sizes[] = {
  { 800, 480}, // WVGA
  { 640, 480} // VGA
};


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
    { "msm8660", TARGET_MSM8660 },
    { "msm8960", TARGET_MSM8960 }

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
#define DEFAULT_THUMBNAIL_SETTING 2
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
#define RECORD_BUFFERS 4
#define RECORD_BUFFERS_8x50 8
static int kRecordBufferCount;
/* controls whether VPE is avialable for the target
 * under consideration.
 * 1: VPE support is available
 * 0: VPE support is not available (default)
 */
static bool mVpeEnabled;
//static cam_frame_start_parms camframeParams;

int HAL_numOfCameras;
camera_info_t HAL_cameraInfo[MSM_MAX_CAMERA_SENSORS];
mm_camera_t * HAL_camerahandle[MSM_MAX_CAMERA_SENSORS];
int HAL_currentCameraId;
int HAL_currentCameraMode;

//static mm_camera_config mCfgControl;

static int HAL_currentSnapshotMode;
#define CAMERA_SNAPSHOT_NONZSL 0x04
#define CAMERA_SNAPSHOT_ZSL 0x08

namespace android {

static const int PICTURE_FORMAT_JPEG = 1;
static const int PICTURE_FORMAT_RAW = 2;

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

// from camera.h, led_mode_t
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

static const str_map facedetection[] = {
    { CameraParameters::FACE_DETECTION_OFF, FALSE },
    { CameraParameters::FACE_DETECTION_ON, TRUE }
};

#define DONT_CARE_COORDINATE -1
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



static const str_map picture_formats[] = {
        {CameraParameters::PIXEL_FORMAT_JPEG, PICTURE_FORMAT_JPEG},
        {CameraParameters::PIXEL_FORMAT_RAW, PICTURE_FORMAT_RAW}
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
static String8 flash_values;
static String8 lensshade_values;
static String8 mce_values;
static String8 histogram_values;
static String8 skinToneEnhancement_values;
static String8 picture_format_values;
static String8 denoise_values;
static String8 zoom_ratio_values;
static String8 preview_frame_rate_values;
static String8 preview_format_values;
static String8 facedetection_values;
static String8 hfr_values;
static String8 redeye_reduction_values;


//mm_camera_notify mCamNotify;
//mm_camera_ops mCamOps;
static mm_camera_buffer_t mEncodeOutputBuffer[MAX_SNAPSHOT_BUFFERS];
static encode_params_t mImageEncodeParms;

extern "C" int HAL_isIn3DMode()
{
    return false;
}


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

String8 QualcommCameraHardware::create_values_str(const str_map *values, int len) {
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
       LOGV("cam_frame_get_video... out = %lx\n", p->buffer);
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

    LOGV("cam_frame_post_video... out = %lx\n", p->buffer);

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
    int32_t result;

    mm_camera_t* current_camera = HAL_camerahandle[HAL_currentCameraId];
    result=current_camera->ops->open(current_camera, MM_CAMERA_OP_MODE_NOTUSED);
    LOGE("Cam open returned %d",result);
    if(result!=MM_CAMERA_OK) {
        LOGE("startCamera: mm_camera_ops_open failed: handle used : 0x%p",current_camera);
        return FALSE;
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

static void do_receive_camframe_callback(mm_camera_ch_data_buf_t* packed_frame, void* HAL_obj);
static void receive_liveshot_callback(liveshot_status status, uint32_t jpeg_size);
static void receive_camstats_callback(camstats_type stype, camera_preview_histogram_info* histinfo);
static void receive_camframe_video_callback(mm_camera_ch_data_buf_t* packed_frame, void* HAL_obj);
//static int8_t receive_event_callback(mm_camera_event* event);
static void receive_shutter_callback(common_crop_t *crop);
static void receive_camframe_error_callback(camera_error_type err);
static int fb_fd = -1;
//static int32_t mMaxZoom = 0;
//static bool zoomSupported = false;
static int dstOffset = 0;

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
#define PREVIEW_BUFFER_COUNT 4
#define VIDEO_BUFFER_COUNT 4
QualcommCameraHardware::QualcommCameraHardware()
    : mStopRecording(false),
      mParameters(),
      mCameraRunning(false),
      mPreviewInitialized(false),
      mPreviewThreadRunning(false),
      mHFRThreadRunning(false),
      mFrameThreadRunning(false),
      mVideoThreadRunning(false),
      mSmoothzoomThreadExit(false),
    mSmoothzoomThreadRunning(false),
    mSnapshotThreadRunning(false),
    mJpegThreadRunning(false),
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
      mDenoiseValue(0),
      mZslEnable(0),
      mZslFlashEnable(false),
      mSnapshotCancel(false),
      mHFRMode(false),
      mActualPictWidth(0),
      mActualPictHeight(0),
    mRecordEnable(false)
{
    LOGI("QualcommCameraHardware constructor E");
    char value[PROPERTY_VALUE_MAX];   

    mZslEnable = false;
    storeTargetType();    
    mIs3DModeOn = false;
    HAL_currentCameraMode = CAMERA_MODE_2D;
   
    if( (pthread_create(&mDeviceOpenThread, NULL, openCamera, NULL)) != 0) {
        LOGE(" openCamera thread creation failed ");
    }
    memset(&mDimension, 0, sizeof(mDimension));
    memset(&mCrop, 0, sizeof(mCrop));
    memset(&zoomCropInfo, 0, sizeof(zoom_crop_info));
   
    property_get("persist.debug.sf.showfps", value, "0");
    mDebugFps = atoi(value);
   
    kPreviewBufferCountActual = kPreviewBufferCount;
    kRecordBufferCount = RECORD_BUFFERS;
    recordframes = new msm_frame[kRecordBufferCount];
    record_buffers_tracking_flag = new bool[kRecordBufferCount];
    jpegPadding = 0;
    
    mDimension.prev_format     = CAMERA_YUV_420_NV21;
    mPreviewFormat             = CAMERA_YUV_420_NV21;
    mDimension.enc_format      = CAMERA_YUV_420_NV21;
    if((mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660) || (mCurrentTarget == TARGET_MSM8960))
        mDimension.enc_format  = CAMERA_YUV_420_NV12;

    mDimension.main_img_format = CAMERA_YUV_420_NV21;
    mDimension.thumb_format    = CAMERA_YUV_420_NV21;

    if( (mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660) || (mCurrentTarget == TARGET_MSM8960) ){
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
    LOGE("%s",__func__);
    if(MM_CAMERA_OK==HAL_camerahandle[HAL_currentCameraId]->ops->is_op_supported (HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_OPS_FOCUS )) {
        LOGE("AutoFocus is not supported");
        mHasAutoFocusSupport = false;
    }
    else {
        LOGE("AutoFocus is supported");
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
#if 1
    mDimension.picture_width = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;
    
    if( MM_CAMERA_OK != HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(
        HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_DIMENSION, 
        &mDimension)) {
        LOGE("CAMERA_PARM_DIMENSION failed!!!");
        return;
    }
   
    hasAutoFocusSupport();
    //Disable DIS for Web Camera
    #if 0
if( !mCfgControl.mm_camera_is_supported(CAMERA_PARM_VIDEO_DIS)){
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
    if (!parameter_string_initialized) {
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

        picture_format_values = create_values_str(
            picture_formats, sizeof(picture_formats)/sizeof(str_map));

        if(mCurrentTarget == TARGET_MSM8660 ||
          (mCurrentTarget == TARGET_MSM7625A ||
           mCurrentTarget == TARGET_MSM7627A)) {
            denoise_values = create_values_str(
                denoise, sizeof(denoise) / sizeof(str_map));
        }
  /*mansoor
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
            //zoom_ratio_values=0;
            zoomSupported = false;
            LOGE("Failed to get maximum zoom value...setting max "
                    "zoom to zero");
            mMaxZoom = 0;
        }*/
        preview_frame_rate_values = create_values_range_str(
            MINIMUM_FPS, MAXIMUM_FPS);

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
    /*mansoor
	if( mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS)){
        mParameters.set(
            CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
            preview_frame_rate_values.string());
     } else*/ {
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
/*#if 0
    if(zoomSupported){
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
        LOGV("max zoom is %d", mMaxZoom-1);
        /* mMaxZoom value that the query interface returns is the size
         * of zoom table. So the actual max zoom value will be one
         * less than that value.
         * /
        mParameters.set("max-zoom",mMaxZoom-1);
        mParameters.set(CameraParameters::KEY_ZOOM_RATIOS,
                            zoom_ratio_values);
    } else
#endif
        {
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");
    }*/

    /* Enable zoom support for video application if VPE enabled */
   /* if(zoomSupported && mVpeEnabled) {
        mParameters.set("video-zoom-support", "true");
    } else*/ {
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

    /*frame_rate_mode_values = create_values_str(
            frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map));*/
/* if( mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS_MODE)){
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATE_MODES,
                    frame_rate_mode_values.string());
    }mansoor */

    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                    preview_size_values.string());
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    picture_size_values.string());

    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
                    picture_format_values);

/*mansoor
    if(mCfgControl.mm_camera_is_supported(CAMERA_PARM_LED_MODE)) {
        mParameters.set(CameraParameters::KEY_FLASH_MODE,
                        CameraParameters::FLASH_MODE_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,
                        flash_values);
    }
*/
    mParameters.set(CameraParameters::KEY_MAX_SHARPNESS,
            CAMERA_MAX_SHARPNESS);
    mParameters.set(CameraParameters::KEY_MAX_CONTRAST,
            CAMERA_MAX_CONTRAST);
    mParameters.set(CameraParameters::KEY_MAX_SATURATION,
            CAMERA_MAX_SATURATION);

    mParameters.set("luma-adaptation", "3");
    mParameters.set("skinToneEnhancement", "0");
    mParameters.set("zoom-supported", "false");
    mParameters.set("zoom", 0);
    mParameters.set(CameraParameters::KEY_PICTURE_FORMAT,
                    CameraParameters::PIXEL_FORMAT_JPEG);

    mParameters.set(CameraParameters::KEY_SHARPNESS,
                    CAMERA_DEF_SHARPNESS);
    mParameters.set(CameraParameters::KEY_CONTRAST,
                    CAMERA_DEF_CONTRAST);
    mParameters.set(CameraParameters::KEY_SATURATION,
                    CAMERA_DEF_SATURATION);

   
    mParameters.set(CameraParameters::KEY_LENSSHADE,
                    CameraParameters::LENSSHADE_ENABLE);
     mParameters.set(CameraParameters::KEY_ISO_MODE,
                    CameraParameters::ISO_AUTO);
    //mParameters.set(CameraParameters::KEY_SUPPORTED_ISO_MODES,
    //                iso_values);
    mParameters.set(CameraParameters::KEY_SUPPORTED_LENSSHADE_MODES,
                    lensshade_values);
    mParameters.set(CameraParameters::KEY_MEMORY_COLOR_ENHANCEMENT,
                    CameraParameters::MCE_ENABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_MEM_COLOR_ENHANCE_MODES,
                    mce_values);
  /*  if(mCfgControl.mm_camera_is_supported(CAMERA_PARM_HFR)) {
        mParameters.set(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE,
                    CameraParameters::VIDEO_HFR_OFF);
        mParameters.set(CameraParameters::KEY_SUPPORTED_HFR_SIZES,
                    hfr_size_values.string());
        mParameters.set(CameraParameters::KEY_SUPPORTED_VIDEO_HIGH_FRAME_RATE_MODES,
                    hfr_values);
    } else mansoor*/
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

    //mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
    //                scenemode_values);
    mParameters.set(CameraParameters::KEY_DENOISE,
                    CameraParameters::DENOISE_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_DENOISE,
                    denoise_values);
    mParameters.set(CameraParameters::KEY_TOUCH_AF_AEC,
                    CameraParameters::TOUCH_AF_AEC_OFF);
    /*mParameters.set(CameraParameters::KEY_SUPPORTED_TOUCH_AF_AEC,
                    touchafaec_values);*/
    mParameters.setTouchIndexAec(-1, -1);
    mParameters.setTouchIndexAf(-1, -1);
    mParameters.set("touchAfAec-dx","100");
    mParameters.set("touchAfAec-dy","100");
    //mParameters.set(CameraParameters::KEY_SCENE_DETECT,
    //               CameraParameters::SCENE_DETECT_OFF);
    //mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_DETECT,
    //                scenedetect_values);
    /*mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF,
                    CameraParameters::SELECTABLE_ZONE_AF_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SELECTABLE_ZONE_AF,
                    selectable_zone_af_values);*/
    mParameters.set(CameraParameters::KEY_FACE_DETECTION,
                    CameraParameters::FACE_DETECTION_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_FACE_DETECTION,
                    facedetection_values);
    mParameters.set(CameraParameters::KEY_REDEYE_REDUCTION,
                    CameraParameters::REDEYE_REDUCTION_DISABLE);
    mParameters.set(CameraParameters::KEY_SUPPORTED_REDEYE_REDUCTION,
                    redeye_reduction_values);
    
    //Added Function to init parameters in QCameraHWI_Parm.cpp
    initDefaultParam();

    float focalLength = 0.0f;
    float horizontalViewAngle = 0.0f;
    float verticalViewAngle = 0.0f;


//   mCfgControl.mm_camera_get_parm(CAMERA_PARM_FOCAL_LENGTH,
//            (void *)&focalLength);
    mParameters.setFloat(CameraParameters::KEY_FOCAL_LENGTH,
                    focalLength);
//    mCfgControl.mm_camera_get_parm(CAMERA_PARM_HORIZONTAL_VIEW_ANGLE,
//            (void *)&horizontalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,
                    horizontalViewAngle);
//    mCfgControl.mm_camera_get_parm(CAMERA_PARM_VERTICAL_VIEW_ANGLE,
//            (void *)&verticalViewAngle);
    mParameters.setFloat(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,
                    verticalViewAngle);

  /*  if(mZslEnable == true) {
        LOGI("%s: setting num-snaps-per-shutter to %d", __FUNCTION__, MAX_SNAPSHOT_BUFFERS-2);
        mParameters.set("num-snaps-per-shutter", MAX_SNAPSHOT_BUFFERS-2);
    } else mansoor*/ {
        LOGI("%s: setting num-snaps-per-shutter to %d", __FUNCTION__, 1);
        mParameters.set("num-snaps-per-shutter", "1");
    }
   // if(mIs3DModeOn)
   //     mParameters.set("3d-frame-format", "left-right");

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
#endif
    LOGI("initDefaultParameters X");
}


#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

bool QualcommCameraHardware::startCamera()
{
    LOGV("startCamera E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if( mCurrentTarget == TARGET_MAX ) {
        LOGE(" Unable to determine the target type. Camera will not work ");
        return false;
    }
//#if DLOPEN_LIBMMCAMERA
//
//    LOGV("loading liboemcamera at %p", libmmcamera);
//    if (!libmmcamera) {
//        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
//        return false;
//    }

//    *(void **)&LINK_cam_frame =
//        ::dlsym(libmmcamera, "cam_frame");
//    *(void **)&LINK_camframe_terminate =
//        ::dlsym(libmmcamera, "camframe_terminate");

//    *(void **)&LINK_jpeg_encoder_init =
//        ::dlsym(libmmcamera, "jpeg_encoder_init");

//    *(void **)&LINK_jpeg_encoder_encode =
//        ::dlsym(libmmcamera, "jpeg_encoder_encode");

//    *(void **)&LINK_jpeg_encoder_join =
//        ::dlsym(libmmcamera, "jpeg_encoder_join");

//    mCamNotify.preview_frame_cb = &receive_camframe_callback;

//    mCamNotify.camstats_cb = &receive_camstats_callback;

//    mCamNotify.on_event =  &receive_event_callback;

//    mCamNotify.on_error_event = &receive_camframe_error_callback;

//    // 720 p new recording functions
//    mCamNotify.video_frame_cb = &receive_camframe_video_callback;
//     // 720 p new recording functions

//    *(void **)&LINK_camframe_add_frame = ::dlsym(libmmcamera, "camframe_add_frame");

//    *(void **)&LINK_camframe_release_all_frames = ::dlsym(libmmcamera, "camframe_release_all_frames");

//    *(void **)&LINK_mmcamera_shutter_callback =
//        ::dlsym(libmmcamera, "mmcamera_shutter_callback");

//    *LINK_mmcamera_shutter_callback = receive_shutter_callback;

//    *(void**)&LINK_jpeg_encoder_setMainImageQuality =
//        ::dlsym(libmmcamera, "jpeg_encoder_setMainImageQuality");

//    *(void**)&LINK_jpeg_encoder_setThumbnailQuality =
//        ::dlsym(libmmcamera, "jpeg_encoder_setThumbnailQuality");

//    *(void**)&LINK_jpeg_encoder_setRotation =
//        ::dlsym(libmmcamera, "jpeg_encoder_setRotation");

//    *(void**)&LINK_jpeg_encoder_get_buffer_offset =
//        ::dlsym(libmmcamera, "jpeg_encoder_get_buffer_offset");

//    *(void**)&LINK_jpeg_encoder_set_3D_info =
//        ::dlsym(libmmcamera, "jpeg_encoder_set_3D_info");

/* Disabling until support is available.
    *(void**)&LINK_jpeg_encoder_setLocation =
        ::dlsym(libmmcamera, "jpeg_encoder_setLocation");
*/
//    *(void **)&LINK_cam_conf =
//        ::dlsym(libmmcamera, "cam_conf");

/* Disabling until support is available.
    *(void **)&LINK_default_sensor_get_snapshot_sizes =
        ::dlsym(libmmcamera, "default_sensor_get_snapshot_sizes");
*/
//    *(void **)&LINK_launch_cam_conf_thread =
//        ::dlsym(libmmcamera, "launch_cam_conf_thread");

//    *(void **)&LINK_release_cam_conf_thread =
//        ::dlsym(libmmcamera, "release_cam_conf_thread");

//    mCamNotify.on_liveshot_event = &receive_liveshot_callback;

//    *(void **)&LINK_cancel_liveshot =
//        ::dlsym(libmmcamera, "cancel_liveshot");

//    *(void **)&LINK_set_liveshot_params =
//        ::dlsym(libmmcamera, "set_liveshot_params");

//    *(void **)&LINK_mm_camera_destroy =
//        ::dlsym(libmmcamera, "mm_camera_destroy");


/* Disabling until support is available.
    *(void **)&LINK_zoom_crop_upscale =
        ::dlsym(libmmcamera, "zoom_crop_upscale");
*/

//#else
//    mCamNotify.preview_frame_cb = &receive_camframe_callback;
//    mCamNotify.camstats_cb = &receive_camstats_callback;
//    mCamNotify.on_event =  &receive_event_callback;

//    mmcamera_shutter_callback = receive_shutter_callback;
//     mCamNotify.on_liveshot_event = &receive_liveshot_callback;
//     mCamNotify.video_frame_cb = &receive_camframe_video_callback;

//#endif // DLOPEN_LIBMMCAMERA

    //BUFFERS ARE NOT ALLOCATED UNTIL PREVIEW SO WE HAVE NONE TO REGISTER.
    // Do we OR these are seperate calles to each MM_CAMERA_CH_VIDEO,MM_CAMERA_CH_SNAPSHOT
#if 0
       

    if((mCurrentTarget != TARGET_MSM7630) && (mCurrentTarget != TARGET_MSM8660)&& (mCurrentTarget != TARGET_MSM8960)){
        fb_fd = open("/dev/graphics/fb0", O_RDWR);
        if (fb_fd < 0) {
            LOGE("startCamera: fb0 open failed: %s!", strerror(errno));
            return FALSE;
        }
    }
#endif
    if (pthread_join(mDeviceOpenThread, NULL) != 0) {
         LOGE("openCamera thread exit failed");
         return false;
    }

#if 0 
//We are going to hardcode these
    //mCfgControl.mm_camera_query_parms(CAMERA_PARM_PICT_SIZE, (void **)&picture_sizes, &PICTURE_SIZE_COUNT);
    mm_camera_cfg_get_parm(HAL_camerahandle[HAL_currentCameraId], CAMERA_PARM_PICT_SIZE, (void*)& picture_sizes);
    if ((picture_sizes == NULL) /*|| (!PICTURE_SIZE_COUNT)*/) {
        LOGE("startCamera X: could not get snapshot sizes");
        return false;
    }
     //LOGV("startCamera picture_sizes %p PICTURE_SIZE_COUNT %d", picture_sizes, PICTURE_SIZE_COUNT);
    //mCfgControl.mm_camera_query_parms(CAMERA_PARM_PREVIEW_SIZE, (void **)&preview_sizes, &PREVIEW_SIZE_COUNT);
    mm_camera_cfg_get_parm(HAL_camerahandle[HAL_currentCameraId], CAMERA_PARM_PREVIEW_SIZE, (void*)& preview_sizes);
    if ((preview_sizes == NULL) /*|| (!PREVIEW_SIZE_COUNT)*/) {
        LOGE("startCamera X: could not get preview sizes");
        return false;
    }
    //LOGV("startCamera preview_sizes %p previewSizeCount %d", preview_sizes, PREVIEW_SIZE_COUNT);

    //mCfgControl.mm_camera_query_parms(CAMERA_PARM_HFR_SIZE, (void **)&hfr_sizes, &HFR_SIZE_COUNT);
    mm_camera_cfg_get_parm(HAL_camerahandle[HAL_currentCameraId], CAMERA_PARM_HFR_SIZE, (void*)& hfr_sizes);
    if ((hfr_sizes == NULL) /*|| (!HFR_SIZE_COUNT)*/) {
        LOGE("startCamera X: could not get hfr sizes");
        return false;
    }
    //LOGV("startCamera hfr_sizes %p hfrSizeCount %d", hfr_sizes, HFR_SIZE_COUNT);

#endif

#if 0
//static struct camera_size_type default_picture_sizes[] = {
picture_sizes[] = {
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
PICTURE_SIZE_COUNT=15;

//static struct camera_size_type default_preview_sizes[] = {
preview_sizes[] = {
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
PREVIEW_SIZE_COUNT=13;
 
hfr_sizes[] = {
  { 800, 480}, // WVGA
  { 640, 480} // VGA
};
HFR_SIZE_COUNT=2;
#endif
PREVIEW_SIZE_COUNT=13;
HFR_SIZE_COUNT=2;
PICTURE_SIZE_COUNT=15;
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
bool QualcommCameraHardware::native_start_ops(mm_camera_ops_type_t  type, void* arg_val)
{
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action (HAL_camerahandle[HAL_currentCameraId],1,type,arg_val )) {
        LOGE("native_start_ops: type %d error %s", type,strerror(errno));
        return false;
    }
#if 0
    if(mCamOps.mm_camera_start(type, value,NULL) != MM_CAMERA_SUCCESS) {
        LOGE("native_start_ops: type %d error %s",
            type,strerror(errno));
        return false;
    }
 
#endif 
    return true;
}

/* Issue ioctl calls related to stopping Camera Operations*/
bool static native_stop_ops(mm_camera_ops_type_t  type, void* arg_val)
{
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action(HAL_camerahandle[HAL_currentCameraId],FALSE,type,arg_val )) {
        LOGE("native_stop_ops: type %d error %s", type,strerror(errno));
        return false;
    }
#if 0
    if(mCamOps.mm_camera_stop(type, value,NULL) != MM_CAMERA_SUCCESS) {
        LOGE("native_stop_ops: type %d error %s",
            type,strerror(errno));
        return false;
    }
#endif
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
    {  
#if 0
        LOGV("%s: E", __FUNCTION__);
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
#endif 
        return true;
    }


bool QualcommCameraHardware::initImageEncodeParameters(int size)
{

    LOGV("%s: E", __FUNCTION__);
#if 0
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
#endif 
    return true;
}

bool QualcommCameraHardware::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value)
{
    LOGE("%s : type : %d Value : %d",__func__,length,*((int *)value));
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(HAL_camerahandle[HAL_currentCameraId],type,value )) {
        LOGE("native_set_parms failed: type %d length %d error %s",
            type, length, strerror(errno));
        return false;
    }

    return true;

}
bool QualcommCameraHardware::native_set_parms(
    mm_camera_parm_type_t type, uint16_t length, void *value, int *result)
{

    *result=HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(HAL_camerahandle[HAL_currentCameraId],type,value );
    if(MM_CAMERA_OK!=*result) {
        LOGE("native_set_parms failed: type %d length %d error %s",
            type, length, strerror(errno));
        return false;
    }
    return true;
#if 0
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
#endif 
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
#if 0
void QualcommCameraHardware::runFrameThread(void *data)
{
    LOGV("runFrameThread E");

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
        mPreviewHeap.clear();
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
            //debugShowPreviewFPS();
        }
        #if 0
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
    #endif
        // Find the offset within the heap of the current buffer.
        ssize_t offset_addr =
            (ssize_t)frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();
        ssize_t offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
        common_crop_t *crop = (common_crop_t *) (frame->cropinfo);
    #if 0 
        //DUMP_PREVIEW_FRAMES
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
//        if(mUseOverlay) {
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
#if 0
        }

         else {
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
#endif
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
#if 0
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
#endif
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
        //LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);
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
            #endif 
#if 0
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
    CAMERA_HAL_UNUSED(data);
    LOGI("%s: stopping Preview", __FUNCTION__);
    stopPreviewInternal();
    LOGI("%s: setting parameters", __FUNCTION__);
    setParameters(mParameters);
    LOGI("%s: starting Preview", __FUNCTION__);
    startPreviewInternal();
    mHFRMode = false;
}
#endif
void QualcommCameraHardware::runVideoThread(void *data)
{
    LOGE("runVideoThread E");
    msm_frame* vframe = NULL;
    CAMERA_HAL_UNUSED(data);
#if 0
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
#endif

    //**********************************Video Thread *****************************/
	/*while((vframe = mRecordBusyQueue.get()) != NULL) {
		LOGE("Video Thread is waiting for a frame");
	}*/
	while(!mStopRecording) {
	vframe = mRecordBusyQueue.get();
	LOGE("Got Buffer %p from Queue",vframe);
	if(vframe != NULL) {
		// Find the offset within the heap of the current buffer.
		//LOGE("Got video frame :  buffer %d base %d ", vframe->buffer, mRecordHeap->mHeap->base());
		/*ssize_t offset =
			(ssize_t)vframe->buffer - (ssize_t)mRecordHeap->mHeap->base();
		LOGE("offset = %d , alignsize = %d , offset later = %d", offset, mRecordHeap->mAlignedBufferSize, (offset / mRecordHeap->mAlignedBufferSize));

		offset /= mRecordHeap->mAlignedBufferSize;

		//set the track flag to true for this video buffer
		record_buffers_tracking_flag[offset] = true;*/

		/* Extract the timestamp of this frame */
		nsecs_t timeStamp = nsecs_t(vframe->ts.tv_sec)*1000000000LL + vframe->ts.tv_nsec;


		// dump frames for test purpose
#if 1 		//#ifdef DUMP_VIDEO_FRAMES
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
			/*mOverlayLock.lock();
			if(mOverlay != NULL) {
				mOverlay->setFd(mRecordHeap->mHeap->getHeapID());
				ssize_t offset_addr = (ssize_t)vframe->buffer -
								  (ssize_t)mRecordHeap->mHeap->base();
				mOverlay->queueBuffer((void *)offset_addr);
			}
			mOverlayLock.unlock();*/

			LOGE("in video_thread : got video frame, before if check giving frame to services/encoder");
            mCallbackLock.lock();
            int msgEnabled = mMsgEnabled;
            data_callback_timestamp rcb = mDataCallbackTimestamp;
            void *rdata = mCallbackCookie;
            mCallbackLock.unlock();

			LOGE("in video_thread : got video frame, giving frame to services/encoder");
            //rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[offset], rdata);
            //rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME,vframe, rdata);
            free(vframe);
		}	//if(vframe != NULL)
	}//End of while
	/***********************************************************************************/
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
#if 0
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
#endif
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
int g_mPreviewFrameSize;
static uint32_t g_record_frame_len;

struct msm_frame previewframes[MM_CAMERA_MAX_NUM_FRAMES];
struct msm_frame videorecordframes[MM_CAMERA_MAX_NUM_FRAMES];
struct msm_frame snapshotframe;
struct msm_frame thumbnailframe;
static int g_dbg_snapshot_main_size;
static int g_dbg_snapshot_thumb_size;

static pthread_mutex_t g_s_mutex;
static int g_status = 0;
static pthread_cond_t g_s_cond_v;

static void mm_app_snapshot_done()
{
  pthread_mutex_lock(&g_s_mutex);
  g_status = TRUE;
  pthread_cond_signal(&g_s_cond_v);
  pthread_mutex_unlock(&g_s_mutex);
}

static void mm_app_snapshot_wait()
{
	pthread_mutex_lock(&g_s_mutex);
	if(FALSE == g_status) pthread_cond_wait(&g_s_cond_v, &g_s_mutex);
	pthread_mutex_unlock(&g_s_mutex);
    g_status = FALSE;
}

static int mm_app_prepare_buf2 (cam_format_t fmt_type, camera_mode_t mode, 
                                int num_preview_buf, int display_width,int display_height)
{
	/* now we hard code format */
	int i, rc = MM_CAMERA_OK;
    uint32_t frame_len, y_off, cbcr_off;
	mm_camera_reg_buf_t reg_buf;
    
	LOGE("%s: BEGIN\n", __func__);
	
    memset(previewframes,  0,  sizeof(struct msm_frame)*MM_CAMERA_MAX_NUM_FRAMES);
	
	frame_len = mm_camera_get_msm_frame_len(fmt_type, mode, display_width, display_height, &y_off, &cbcr_off, MM_CAMERA_PAD_WORD);
	
	for(i = 0; i < num_preview_buf; i++) {
		previewframes[i].buffer = (unsigned long) mm_camera_do_mmap(
			frame_len, &previewframes[i].fd );
		if (!previewframes[i].buffer) {
			LOGE("%s:no mem for video buf index %d\n", __func__, i);
				rc = -MM_CAMERA_E_NO_MEMORY;
				goto end;
		}
		previewframes[i].path = OUTPUT_TYPE_P;
        previewframes[i].y_off= y_off;
		previewframes[i].cbcr_off = cbcr_off;
	}
    memset(&reg_buf, 0, sizeof(reg_buf));
	reg_buf.ch_type = MM_CAMERA_CH_PREVIEW;
	reg_buf.preview.num = num_preview_buf;
	reg_buf.preview.frame = &previewframes[0];
	rc = HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
	if(rc != MM_CAMERA_OK) {
		LOGE("%s:reg preview buf err=%d\n", __func__, rc);
	}
end:
	LOGE("%s: END, rc=%d\n", __func__, rc);
	return rc;
}
static int mm_app_prepare_video_buf2 (cam_format_t fmt_type, camera_mode_t mode, 
                                int num_video_buf, int width,int height)
{
	/* now we hard code format */
	int i, rc = MM_CAMERA_OK;
    uint32_t frame_len, y_off, cbcr_off;
	mm_camera_reg_buf_t reg_buf;
    
	LOGE("%s: BEGIN\n", __func__);
	
    memset(videorecordframes,  0,  sizeof(struct msm_frame)*MM_CAMERA_MAX_NUM_FRAMES);
	frame_len = mm_camera_get_msm_frame_len(fmt_type, mode, width, height, &y_off, &cbcr_off, MM_CAMERA_PAD_2K);
	
	for(i = 0; i < num_video_buf; i++) {
		videorecordframes[i].buffer = (unsigned long) mm_camera_do_mmap(
			frame_len, &videorecordframes[i].fd );
		if (!videorecordframes[i].buffer) {
			LOGE("%s:no mem for video buf index %d\n", __func__, i);
				rc = -MM_CAMERA_E_NO_MEMORY;
				goto end;
		}
		videorecordframes[i].path = OUTPUT_TYPE_V;
        videorecordframes[i].y_off= y_off;
		videorecordframes[i].cbcr_off = cbcr_off; 
	}
    memset(&reg_buf, 0, sizeof(reg_buf));
	reg_buf.ch_type = MM_CAMERA_CH_VIDEO;
	reg_buf.video.video.num = num_video_buf;
	reg_buf.video.video.frame = &videorecordframes[0];
	rc = HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
	if(rc != MM_CAMERA_OK) {
		LOGE("%s:reg video buf err=%d\n", __func__, rc);
	}
end:
	LOGE("%s: END, rc=%d\n", __func__, rc);
	return rc;
}
static int mm_app_prepare_snapshot_buf2 (cam_format_t fmt_type, cam_format_t fmt_type_t, camera_mode_t mode, 
                                int main_width, int main_height, 
                                int display_width,int display_height)
{
	/* now we hard code format */
	int rc = MM_CAMERA_OK;
    uint32_t frame_len, y_off, cbcr_off;
	mm_camera_reg_buf_t reg_buf;
    
	LOGE("%s: BEGIN\n", __func__);

    memset(&snapshotframe,  0,  sizeof(struct msm_frame));
    memset(&thumbnailframe,  0,  sizeof(struct msm_frame));
	
	frame_len = mm_camera_get_msm_frame_len(fmt_type, mode, main_width, main_height, &y_off, &cbcr_off, MM_CAMERA_PAD_WORD);
    g_dbg_snapshot_main_size = frame_len;

    snapshotframe.buffer = (unsigned long) mm_camera_do_mmap(
        frame_len, &snapshotframe.fd );
    if (!snapshotframe.buffer) {
        LOGE("%s:no mem for snapshot buf\n", __func__);
            rc = -MM_CAMERA_E_NO_MEMORY;
            goto end;
    }
    snapshotframe.path = OUTPUT_TYPE_S;
    snapshotframe.y_off= y_off;
    snapshotframe.cbcr_off = cbcr_off;

	frame_len = mm_camera_get_msm_frame_len(fmt_type_t, mode, display_width, display_height, &y_off, &cbcr_off, MM_CAMERA_PAD_WORD);	
    g_dbg_snapshot_thumb_size = frame_len;
    thumbnailframe.buffer = (unsigned long) mm_camera_do_mmap(
        frame_len, &thumbnailframe.fd );
    if (!thumbnailframe.buffer) {
        LOGE("%s:no mem for thumbnail buf\n", __func__);
            rc = -MM_CAMERA_E_NO_MEMORY;
            goto end;
    }
    thumbnailframe.path = OUTPUT_TYPE_T;
    thumbnailframe.y_off= y_off;
    thumbnailframe.cbcr_off = cbcr_off;

    memset(&reg_buf, 0, sizeof(reg_buf));
	reg_buf.ch_type = MM_CAMERA_CH_SNAPSHOT;
	reg_buf.snapshot.main.num = 1;
	reg_buf.snapshot.main.frame = &snapshotframe;
	reg_buf.snapshot.thumbnail.num = 1;
	reg_buf.snapshot.thumbnail.frame = &thumbnailframe;
	rc = HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
	if(rc != MM_CAMERA_OK) {
		LOGE("%s:reg snapshot buf err=%d\n", __func__, rc);
	}
end:
    LOGE("%s: END, rc=%d\n", __func__, rc); 
    return rc;
}

static int mm_app_unprepare_snapshot_buf2()
{

	int rc = MM_CAMERA_OK;
	LOGV("%s: BEGIN\n", __func__);

    LOGD("Unpreparing Snapshot Buffer");
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->unprepare_buf(HAL_camerahandle[HAL_currentCameraId],
                                                                   MM_CAMERA_CH_SNAPSHOT);
	if(rc != MM_CAMERA_OK) {
		LOGE("%s:unreg snapshot buf err=%d\n", __func__, rc);
	}

//    LOGE("Unmapping snapshot frame");
//	(void)mm_do_munmap(snapshotframe.fd, (void *)snapshotframe.buffer, g_dbg_snapshot_main_size);
    
//    LOGE("Unmapping thumbnail frame");
//	(void)mm_do_munmap(thumbnailframe.fd, (void *)thumbnailframe.buffer, g_dbg_snapshot_thumb_size);
    

	// zero out the buf stuct 
	memset(&snapshotframe,  0,  sizeof(struct msm_frame));
    memset(&thumbnailframe,  0,  sizeof(struct msm_frame));
end:
	LOGV("%s: END, rc=%d\n", __func__, rc);

	return rc;
}

void QualcommCameraHardware::deinitSnapshotBuffer()
{
    LOGV("%s: Deinit snapshot Begin", __func__);

    LOGD("Unmap snapshot buffers");
    mm_app_unprepare_snapshot_buf2();

    mRawHeap.clear();
    mPostviewHeap.clear();

    /* unreg buf notify*/
    LOGD("Unregister buf notification");
    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(
            HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_CH_SNAPSHOT,
            NULL, NULL)
            ) {
        LOGE("takePicture: Failure setting snapshot callback");
    }

    LOGD("Release snapshot channel");
    HAL_camerahandle[HAL_currentCameraId]->ops->ch_release(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_SNAPSHOT);

    LOGV("%s: Deinit snapshot End",__func__);
}

bool QualcommCameraHardware::initPreview()
{
    const char * pmem_region;
    mm_camera_reg_buf_t reg_buf;
    mm_camera_ch_image_fmt_parm_t fmt;
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    //previewWidth=videoWidth=640;
    //previewHeight=videoHeight=480;
    LOGE("initPreview E 1: preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight );
    //previewWidth=videoWidth=640;
    //previewHeight=videoHeight=480;
    videoWidth=previewWidth;
    videoHeight=previewHeight;

    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || 
        (mCurrentTarget == TARGET_MSM8660) || (mCurrentTarget == TARGET_MSM8960)) {
        mDimension.video_width = CEILING16(videoWidth);
        /* Backup the video dimensions, as video dimensions in mDimension
         * will be modified when DIS is supported. Need the actual values
         * to pass ap part of VPE config
         */
        videoWidth = mDimension.video_width;
        mDimension.video_height = videoHeight;
        mDimension.picture_width = DEFAULT_PICTURE_WIDTH;
        mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;
        //mDimension.previewHeight=previewHeight;
        //mDimension.previewWidth=previewWidth;
        LOGE("initPreview 2: preview size=%dx%d videosize = %d x %d", previewWidth, previewHeight, videoWidth, videoHeight);
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
    g_mPreviewFrameSize=mPreviewFrameSize;
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
    LOGE("mDimension.prev_format = %d", mDimension.prev_format);
    LOGE("mDimension.display_luma_width = %d", mDimension.display_luma_width);
    LOGE("mDimension.display_luma_height = %d", mDimension.display_luma_height);
    LOGE("mDimension.display_chroma_width = %d", mDimension.display_chroma_width);
    LOGE("mDimension.display_chroma_height = %d", mDimension.display_chroma_height);

    dstOffset = 0;
  //Pass the original video width and height and get the required width
    //and height for record buffer allocation
    mDimension.orig_video_width = videoWidth;
    mDimension.orig_video_height = videoHeight;
#if 0
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
#endif
    // mDimension will be filled with thumbnail_width, thumbnail_height,
    // orig_picture_dx, and orig_picture_dy after this function call. We need to
    // keep it for jpeg_encoder_encode.
    bool ret=true;
#if 1
 /*mnsr*/
    LOGE("initPreview 3:######## preview size=%dx%d videosize = %d x %d previewFormat= %d ", previewWidth, previewHeight, videoWidth, videoHeight,mPreviewFormat);
    if( MM_CAMERA_OK != HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(
        HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_DIMENSION, 
        &mDimension)) {
        LOGE("INITRAW: Set DIMENSION failed");
        return UNKNOWN_ERROR;
    }

#endif
    /* here we set both preview and video */

    memset(&fmt, 0, sizeof(fmt));
    fmt.ch_type = MM_CAMERA_CH_VIDEO;
    fmt.video.video.fmt = CAMERA_YUV_420_NV21;
    fmt.video.video.dim.width = previewWidth;
    fmt.video.video.dim.height =previewHeight;

    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt)){
        LOGE("startPreviewInternal:set_param: VIDEO: IMG_FMT:");
        return UNKNOWN_ERROR;
    }

    memset(&fmt, 0, sizeof(mm_camera_ch_image_fmt_parm_t));
    fmt.ch_type = MM_CAMERA_CH_PREVIEW;
    fmt.def.fmt = CAMERA_YUV_420_NV21;
    fmt.def.dim.width = previewWidth;
    fmt.def.dim.height = previewHeight;

    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_CH_IMAGE_FMT, &fmt)){
        LOGE("startPreviewInternal:set_param: PREVIEW: IMG_FMT:");
        return UNKNOWN_ERROR;
    }





    if(mIs3DModeOn != true) {
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

 
  if(mIs3DModeOn != true) {
      for(int ii = 0; ii < 4; ii++) {
            mPreviewHeapx[ii] = new PmemPool(pmem_region,
                                    MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                    MSM_PMEM_PREVIEW, //MSM_PMEM_OUTPUT2,
                                    mPreviewFrameSize,
                                    1,
                                    mPreviewFrameSize,
                                    CbCrOffset,
                                    0,
                                    "preview");
    
            if (!mPreviewHeapx[ii]->initialized()) {
                mPreviewHeapx[ii].clear();
                LOGE("initPreview X: could not initialize Camera preview heap.");
                return false;
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
    }
 
    //if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)(mCurrentTarget == TARGET_MSM8960)) {

        // Allocate video buffers after allocating preview buffers.
        bool status = initRecord();
        if(status != true) {
            LOGE("Failed to allocate video bufers");
            return false;
        }
    //}

    if (ret) {
        if(mIs3DModeOn != true) {
            for (cnt = 0; cnt < kPreviewBufferCount; cnt++) {
                frames[cnt].fd = mPreviewHeapx[cnt]->mHeap->getHeapID();//mPreviewHeap->mHeap->getHeapID();
                LOGE("#############frame fd:%d",frames[cnt].fd);

                frames[cnt].buffer = (uint32_t)mPreviewHeapx[cnt]->mHeap->base();// + mPreviewHeap->mAlignedBufferSize * cnt;
                    //(uint32_t)mPreviewHeapx[cnt]->mHeap->base();
                LOGE("##########buffer: 0x%lx",frames[cnt].buffer);
                frames[cnt].y_off = 0;
                frames[cnt].cbcr_off = CbCrOffset;
                frames[cnt].path = OUTPUT_TYPE_P; // MSM_FRAME_ENC;
            }

            mPreviewBusyQueue.init();
        //    LINK_camframe_release_all_frames(CAM_PREVIEW_FRAME);
            reg_buf.ch_type = MM_CAMERA_CH_PREVIEW;
            reg_buf.preview.num = PREVIEW_BUFFER_COUNT;//kPreviewBufferCount;
            reg_buf.preview.frame = &frames[0];
          //  reg_buf.preview.frame_offset[0]
         //   for(int i=ACTIVE_PREVIEW_BUFFERS ;i <kPreviewBufferCount; i++)
          //  {
               
                //LINK_camframe_add_frame(CAM_PREVIEW_FRAME,&frames[i]);
           // }
         #if 0
            if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf))
            {
                LOGE("InitPreview: prepare_buf failed: ");
                return FALSE;
            }
         #endif
            mm_app_prepare_buf2(CAMERA_YUV_420_NV21, CAMERA_MODE_2D,PREVIEW_BUFFER_COUNT,previewWidth,previewHeight);
            if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_PREVIEW,do_receive_camframe_callback,&obj)) {
                LOGE("InitPreview: mm_camera_register_buf_notify failed: ");
                return FALSE;
            }

            if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action(HAL_camerahandle[HAL_currentCameraId],1,MM_CAMERA_OPS_PREVIEW,0)) {
                LOGE("InitPreview: Stream on failed: ");
                return FALSE;
            }

            return true;
           /* mPreviewThreadWaitLock.lock();
            pthread_attr_t pattr;
            pthread_attr_init(&pattr);
            pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);

            mPreviewThreadRunning = !pthread_create(&mPreviewThread,
                                      &pattr,
                                      preview_thread,
                                      (void*)NULL);
            ret = mPreviewThreadRunning;
            mPreviewThreadWaitLock.unlock();
*/
            if(ret == false)
                return ret;
        }

#if 0
        mFrameThreadWaitLock.lock();
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#if 0
        camframeParams.cammode = CAMERA_MODE_2D;

        if (mIs3DModeOn) {
            camframeParams.cammode = CAMERA_MODE_3D;
        } else {
            camframeParams.cammode = CAMERA_MODE_2D;
        }
#endif
        mFrameThreadRunning = !pthread_create(&mFrameThread,
                                              &attr,
                                              frame_thread,
                                              /*&camframeParams*/(void*)NULL);
        ret = mFrameThreadRunning;
        mFrameThreadWaitLock.unlock();
#endif    
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

    
    LOGI("deinitPreview X");
}

bool QualcommCameraHardware::initRawSnapshot()
{
    LOGV("initRawSnapshot E");
#if 0
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
#endif
    LOGV("initRawSnapshot X");
    return true;

}
bool QualcommCameraHardware::initZslBuffers(bool initJpegHeap){
    LOGE("Init ZSL buffers E");
#if 0
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
#endif
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

/*define few global debug var*/
bool QualcommCameraHardware::initRaw(bool initJpegHeap)
{
    uint32_t frame_len, y_off, cbcr_off;
    mm_camera_reg_buf_t reg_buf;
    bool rc = true;
    
    LOGV("%s: BEGIN\n", __func__);

    mDimension.picture_width =1280;
    mDimension.picture_height = 960;
    mDimension.ui_thumbnail_width = previewWidth/*320*/;
    mDimension.ui_thumbnail_height = previewHeight/*240*/;
    LOGD("Set snapshot dimension");
    if( MM_CAMERA_OK != HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(
        HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_DIMENSION, 
        &mDimension)) {
        LOGE("INITRAW: Set DIMENSION failed");
        return UNKNOWN_ERROR;
    }

    /* Set format */
    LOGD("Set snapshot format");
    mm_camera_ch_image_fmt_parm_t snapshot_img_fmt;
    memset((mm_camera_ch_image_fmt_parm_t *)&snapshot_img_fmt, 0, sizeof(snapshot_img_fmt));
    snapshot_img_fmt.ch_type = MM_CAMERA_CH_SNAPSHOT;
    snapshot_img_fmt.snapshot.main.fmt = mDimension.main_img_format;
    snapshot_img_fmt.snapshot.main.dim.width = mDimension.picture_width;
    snapshot_img_fmt.snapshot.main.dim.height = mDimension.picture_height;

    snapshot_img_fmt.snapshot.thumbnail.fmt = mDimension.thumb_format;
    snapshot_img_fmt.snapshot.thumbnail.dim.width = mDimension.ui_thumbnail_width;
    snapshot_img_fmt.snapshot.thumbnail.dim.height = mDimension.ui_thumbnail_height;

    if( MM_CAMERA_OK != HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(
        HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_PARM_CH_IMAGE_FMT, 
        &snapshot_img_fmt)) {
        LOGE("INITRAW: Set Snapshot Image Format failed");
        return UNKNOWN_ERROR;
    }

    memset(&snapshotframe,  0,  sizeof(struct msm_frame));
    memset(&thumbnailframe,  0,  sizeof(struct msm_frame));
	
	frame_len = mm_camera_get_msm_frame_len(mDimension.main_img_format, CAMERA_MODE_2D, 
                                            mDimension.picture_width, mDimension.picture_width,
                                            &y_off, &cbcr_off,MM_CAMERA_PAD_WORD);
    g_dbg_snapshot_main_size = frame_len;

    //Main Raw Image
    mRawHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_MAINIMG,
                     frame_len,
                     1,
                     frame_len,
                     cbcr_off,
                     y_off,
                     "snapshot camera");

    if (!mRawHeap->initialized()) {
       LOGE("initRaw X failed ");
       mRawHeap.clear();
       LOGE("initRaw X: error initializing mRawHeap");
       rc = false;
       goto end;
    }

    snapshotframe.fd = mRawHeap->mHeap->getHeapID();
    snapshotframe.buffer = (uint32_t)mRawHeap->mHeap->base();

//    snapshotframe.buffer = (unsigned long) mm_camera_do_mmap(
//        frame_len, &snapshotframe.fd );
//    if (!snapshotframe.buffer) {
//       LOGE("%s:no mem for snapshot buf\n", __func__);
//            rc = -MM_CAMERA_E_NO_MEMORY;
//            goto end;
//    }

    snapshotframe.path = OUTPUT_TYPE_S;
    snapshotframe.y_off= y_off;
    snapshotframe.cbcr_off = cbcr_off;


//    frame_len = mm_camera_get_msm_frame_len(fmt_type_t, mode, display_width, display_height, &y_off, &cbcr_off);	
      frame_len = mm_camera_get_msm_frame_len(mDimension.thumb_format, CAMERA_MODE_2D, 
                                              mDimension.ui_thumbnail_width,mDimension.ui_thumbnail_height, 
                                              &y_off, &cbcr_off,MM_CAMERA_PAD_WORD);	
      g_dbg_snapshot_thumb_size = frame_len;

     //Postview Image
     mPostviewHeap =
        new PmemPool("/dev/pmem_adsp",
                     MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                     MSM_PMEM_THUMBNAIL,
                     frame_len,
                     1,
                     frame_len,
                     cbcr_off,
                     y_off,
                     "thumbnail");

    if (!mPostviewHeap->initialized()) {
       LOGE("initRaw X failed ");
       mRawHeap.clear();
       mPostviewHeap.clear();
       LOGE("initRaw X: error initializing mRawHeap");
       rc = false;
       goto end;
    }

//    thumbnailframe.buffer = (unsigned long) mm_camera_do_mmap(
//        frame_len, &thumbnailframe.fd );
//    if (!thumbnailframe.buffer) {
//       LOGE("%s:no mem for thumbnail buf\n", __func__);
//            rc = -MM_CAMERA_E_NO_MEMORY;
//            goto end;
//    }
    thumbnailframe.path = OUTPUT_TYPE_T;
    thumbnailframe.y_off= y_off;
    thumbnailframe.cbcr_off = cbcr_off;
    thumbnailframe.fd = mPostviewHeap->mHeap->getHeapID();
    thumbnailframe.buffer = (uint32_t)mPostviewHeap->mHeap->base();

    memset(&reg_buf, 0, sizeof(reg_buf));
	reg_buf.ch_type = MM_CAMERA_CH_SNAPSHOT;
	reg_buf.snapshot.main.num = 1;
	reg_buf.snapshot.main.frame = &snapshotframe;
	reg_buf.snapshot.thumbnail.num = 1;
	reg_buf.snapshot.thumbnail.frame = &thumbnailframe;
	if( MM_CAMERA_OK != 
        HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(
            HAL_camerahandle[HAL_currentCameraId],&reg_buf)){
		LOGE("%s:reg snapshot buf failed\n", __func__);
        rc = false;
        goto end;
	}
end:
	LOGV("%s: END, rc=%d\n", __func__, rc);
	return rc;
}


void QualcommCameraHardware::deinitRawSnapshot()
{
    LOGV("deinitRawSnapshot E");
    //mRawSnapShotPmemHeap.clear();
    LOGV("deinitRawSnapshot X");
}

void QualcommCameraHardware::deinitRaw()
{
    LOGV("deinitRaw E");

    LOGD("Unmap snapshot buffers");
    mm_app_unprepare_snapshot_buf2();

    mRawHeap.clear();
    mPostviewHeap.clear();

    /* unreg buf notify*/
    LOGD("Unregister buf notification");
    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(
            HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_CH_SNAPSHOT,
            NULL, NULL)
            ) {
        LOGE("takePicture: Failure setting snapshot callback");
    }

    LOGD("Release snapshot channel");
    HAL_camerahandle[HAL_currentCameraId]->ops->ch_release(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_SNAPSHOT);
   
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

#if 0
    mm_camera_ops_type_t current_ops_type = (mSnapshotFormat
            == PICTURE_FORMAT_JPEG) ? CAMERA_OPS_CAPTURE_AND_ENCODE
            : CAMERA_OPS_RAW_CAPTURE;
    mCamOps.mm_camera_deinit(current_ops_type, NULL, NULL);
#endif
    //Signal the snapshot thread
 //   mJpegThreadWaitLock.lock();
 //   mJpegThreadRunning = false;
 //   mJpegThreadWait.signal();
 //   mJpegThreadWaitLock.unlock();

    // Wait for snapshot thread to complete before clearing the
    // resources.
 //   mSnapshotThreadWaitLock.lock();
 //   while (mSnapshotThreadRunning) {
 //       LOGV("release: waiting for old snapshot thread to complete.");
 //       mSnapshotThreadWait.wait(mSnapshotThreadWaitLock);
 //       LOGV("release: old snapshot thread completed.");
 //   }
 //   mSnapshotThreadWaitLock.unlock();
#if 0
    {
        Mutex::Autolock l (&mRawPictureHeapLock);
        deinitRaw();
    }

    deinitRawSnapshot();
#endif
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
//    LINK_mm_camera_deinit();
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
    //libmmcamera = NULL;
    mMMCameraDLRef.clear();
    int result;
    LOGE("################POS1");
    singleton_lock.lock();
    LOGE("################POS2");
    if( mCurrentTarget == TARGET_MSM7630 || mCurrentTarget == TARGET_QSD8250 || mCurrentTarget == TARGET_MSM8660 ) {
        delete [] recordframes;
        recordframes = NULL;
        delete [] record_buffers_tracking_flag;
        record_buffers_tracking_flag = NULL;
    }
    LOGE("################POS3");
    mm_camera_t* current_camera = HAL_camerahandle[HAL_currentCameraId];
    current_camera->ops->close(current_camera);
    LOGE("################POS4");
    singleton.clear();
    LOGE("################POS5");
    singleton_releasing = false;
    singleton_releasing_start_time = 0;
    singleton_wait.signal();
    LOGE("################POS6");
    singleton_lock.unlock();
    LOGE("################POS7");
    LOGI("~QualcommCameraHardware X");
}

sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
{
    LOGV("getRawHeap");
    return mDisplayHeap != NULL ? mDisplayHeap->mHeap : NULL;
}

sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
{
    LOGE("##################getPreviewHeap");
    if(mIs3DModeOn != true)
        return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
    else
        return mRecordHeap != NULL ? mRecordHeap->mHeap : NULL;
}


status_t QualcommCameraHardware::startPreviewInternal()
{
   /* if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }*/

    mm_camera_op_mode_type_t op_mode=MM_CAMERA_OP_MODE_VIDEO;
    LOGV("in startPreviewInternal : E");
    if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }
    LOGD("Setting Preview Op Mode");
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_OP_MODE,&op_mode)) {
        LOGE("startPreviewInternal:setopmode failed:");
        return UNKNOWN_ERROR;
    }
    LOGD("Acquire Preview channel");
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->ch_acquire(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_PREVIEW)){
        LOGE("startPreviewInternal:ch_aquire: MM_CAMERA_CH_PREVIEW failed:");
        return UNKNOWN_ERROR;
    }
    LOGD("Acquire video channel");
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->ch_acquire(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_VIDEO)){
        LOGE("startPreviewInternal:ch_aquire: MM_CAMERA_CH_VIDEO failed:");
        return UNKNOWN_ERROR;
    }

    if (!mPreviewInitialized) {
        mLastQueuedFrame = NULL;
        LOGD("Init Preview");
        mPreviewInitialized = initPreview();
        if (!mPreviewInitialized) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            mPreviewBusyQueue.deinit();
            return UNKNOWN_ERROR;
        }
    }

    //   Mutex::Autolock cameraRunningLock(&mCameraRunningLock);

    mCameraRunning = true;
    LOGD("startPreviewInternal X: mCameraRunning: %d", mCameraRunning);
    return NO_ERROR;
}

status_t QualcommCameraHardware::startPreview()
{
    LOGV("startPreview E");
    Mutex::Autolock l(&mLock);
    return startPreviewInternal();
}
int mm_do_munmap(int pmem_fd, void *addr, size_t size)
{
  int rc;

  size = (size + 4095) & (~4095);

  LOGD("munmapped size = %d, virt_addr = 0x%p\n",
    size, addr);

  rc = (munmap(addr, size));

  close(pmem_fd);

  LOGD("do_mmap: pmem munmap fd %d ptr %p len %u rc %d\n", pmem_fd, addr,
    size, rc);

  return rc;
}

void QualcommCameraHardware::stopPreviewInternal()
{
    LOGV("stopPreviewInternal E mCameraRunning : %d", mCameraRunning);

    if(!mCameraRunning) {
        LOGV("Preview Already stopped.");
        return;
    }
    LOGD("Stop OPS PREVIEW");
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action(HAL_camerahandle[HAL_currentCameraId],FALSE,MM_CAMERA_OPS_PREVIEW,0)) {
        LOGE("%s: ###############Stream off failed: ",__func__);
    //    return ;
    }
    else
    {
        LOGE("%s: ###############Stream off passed: ",__func__);
    }
    int rc;

    LOGD("Unprepare Preview Buf");
    rc= HAL_camerahandle[HAL_currentCameraId]->cfg->unprepare_buf(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_PREVIEW);
    if(rc != MM_CAMERA_OK) {
        LOGE("%s:Ureg preview buf err=%d\n", __func__, rc);
    }
    
    LOGD("Unmap Preview frames");
    for(int i=0;i<4;i++)
    {
        mm_do_munmap(previewframes[i].fd,&previewframes[i].buffer,mPreviewFrameSize);
    }

    LOGD("Unprepare Video Buf");
    rc= HAL_camerahandle[HAL_currentCameraId]->cfg->unprepare_buf(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_VIDEO);
    if(rc != MM_CAMERA_OK) {
        LOGE("%s:Ureg video buf err=%d\n", __func__, rc);
    }


    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(
            HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_CH_PREVIEW,
            NULL, NULL)
            ) {
        LOGE("%s: Failure unregistering preview callback", __func__);
    }

    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(
            HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_CH_VIDEO,
            NULL, NULL)
            ) {
        LOGE("%s: Failure unregistering video callback", __func__);
    }

    LOGD("Releasing Preview/Video channels");
    HAL_camerahandle[HAL_currentCameraId]->ops->ch_release(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_VIDEO);
    HAL_camerahandle[HAL_currentCameraId]->ops->ch_release(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_PREVIEW);

    mCameraRunning = false;
    mPreviewInitialized = false;
    LOGV("stopPreviewInternal X: mCameraRunning: %d mPreviewInitialized: %d", mCameraRunning, mPreviewInitialized);
}

void QualcommCameraHardware::stopPreview()
{
    LOGV("stopPreview: E");
    Mutex::Autolock l(&mLock);
    stopPreviewInternal();
    LOGV("stopPreview: X");
}

status_t QualcommCameraHardware::cancelAutoFocusInternal()
{


    LOGV("cancelAutoFocusInternal E");
    status_t rc = NO_ERROR;
#if 0
    if(!mHasAutoFocusSupport){
        LOGV("cancelAutoFocusInternal X");
        return NO_ERROR;
    }

    
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
             rc = native_stop_ops(CAMERA_OPS_FOCUS, NULL) ?
                NO_ERROR :
                UNKNOWN_ERROR;
    }



    LOGV("cancelAutoFocusInternal X: %d", rc);    
#endif        
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

static int mm_app_dump_snapshot_frame(struct msm_frame *frame, uint32_t len, int is_main, int loop)
{
	char bufp[128];
	int file_fdp;
	int rc = 0;

	if(is_main) {
		sprintf(bufp, "/data/bs%d.yuv", loop);
	} else {
		sprintf(bufp, "/data/bt%d.yuv", loop);
	}

	file_fdp = open(bufp, O_RDWR | O_CREAT, 0777);

	if (file_fdp < 0) {
		rc = -1;
		goto end;
	}
	write(file_fdp,
		(const void *)frame->buffer, len);
	close(file_fdp);
end:
	return rc;
}

void snapshot_jpeg_cb(jpeg_event_t event){
    LOGE("in cb");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveCompleteJpegPicture(event);
    }
    else
        LOGW("receive jepeg cb obj null");

    mm_jpeg_encoder_join();

    //cleanup
    obj->deinitSnapshotBuffer();

}

void snapshot_jpeg_fragment_cb(uint8_t *ptr, uint32_t size){
    LOGE("in framgment cb");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveJpegFragment(ptr,size);
    }
    else
        LOGW("receive jepeg fragment cb obj null");
}


static void receive_snapshot_frame_cb(mm_camera_ch_data_buf_t *recvd_frame, void *user)
{
    static int loop = 0;
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();

    LOGV("Receive snapshot frame callback");
    
    //mm_app_dump_snapshot_frame(recvd_frame->snapshot.main.frame, g_dbg_snapshot_main_size, TRUE, loop);
	//mm_app_dump_snapshot_frame(recvd_frame->snapshot.thumbnail.frame, g_dbg_snapshot_thumb_size, FALSE, loop++);

   
    LOGD("Calling receiveRawPicture");
    if (obj != 0) {
        obj->receiveRawPicture(NO_ERROR, recvd_frame->snapshot.thumbnail.frame, recvd_frame->snapshot.main.frame);
    }
    else
        LOGW("receive_snapshot_frame_cb: Unable to find QualcommCameraHardware obj");
    
    LOGD("Calling buf_done");
    HAL_camerahandle[HAL_currentCameraId]->evt->buf_done(HAL_camerahandle[HAL_currentCameraId],recvd_frame);
    mm_app_snapshot_done();

    LOGV("mm_app_snapshot_done called");

}

static int jpeg_offset = 0;
void QualcommCameraHardware::receiveJpegFragment(uint8_t *ptr, uint32_t size){

  memcpy(mJpegHeap->mHeap->base()+jpeg_offset, ptr, size);
  jpeg_offset += size;
}



void QualcommCameraHardware::receiveCompleteJpegPicture(jpeg_event_t event){

  //give callback to app

    char bufp[128];
	int file_fdp;
	
    LOGV("%s: received full JPEG Pix", __func__);

#if 0
	sprintf(bufp, "/data/bikjpeg.jpeg");

	file_fdp = open(bufp, O_RDWR | O_CREAT, 0777);

	if (file_fdp >= 0) {
		write(file_fdp, (const void *)mJpegHeap->mHeap->base(), jpeg_offset);
	    close(file_fdp);
    }
#endif

  LOGD("Calling upperlayer callback");
  if (mDataCallback && (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE)) {
        // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
        // that the JPEG image's size will probably change from one snapshot
        // to the next, so we cannot reuse the MemoryBase object.
        sp<MemoryBase> buffer = new
            MemoryBase(mJpegHeap->mHeap,
                       0,
                       jpeg_offset);

        mDataCallback(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
        buffer = NULL;

    } else {
      LOGV("JPEG callback was cancelled--not delivering image.");
    }

    //reset jpeg_offset
    jpeg_offset = 0;

    if(mJpegHeap != 0) {
        mJpegHeap.clear();
    }
   
    LOGV("Leaving receiveCompleteJpegPicture");
}


void QualcommCameraHardware::receiveRawPicture(status_t status,struct msm_frame *postviewframe, struct msm_frame *mainframe){


    LOGD("Inside receiveRawPicture");

    cam_ctrl_dimension_t dimension;
    dimension.orig_picture_dx = mDimension.picture_width;
    dimension.orig_picture_dy = mDimension.picture_height;
    dimension.thumbnail_width = mDimension.ui_thumbnail_width;
    dimension.thumbnail_height = mDimension.ui_thumbnail_height;

    jpeg_offset = 0;

    LOGD("Allocating memory to store jpeg image.");
    mJpegHeap =
            new AshmemPool(g_dbg_snapshot_main_size,
                           1,
                           0, // we do not know how big the picture will be
                           "jpeg");

        if (!mJpegHeap->initialized()) {
            mJpegHeap.clear();
            LOGE("initRaw X failed: error initializing mJpegHeap.");
            return;
        }

    common_crop_t crop;
    crop.in2_w = 0;
    crop.in2_h =0;
    crop.out2_w = 0;
    crop.out2_h =0;

    crop.in1_w = 0;
    crop.in1_h =0;
    crop.out1_w = 0;
    crop.out1_h =0;

    LOGD("Setting callbacks, initializing encoder and start encoding.");
    set_callbacks(snapshot_jpeg_fragment_cb, snapshot_jpeg_cb);
    mm_jpeg_encoder_init();
    mm_jpeg_encoder_encode((const cam_ctrl_dimension_t *)&dimension, (uint8_t *)postviewframe->buffer,  postviewframe->fd,
            (uint8_t *)mainframe->buffer, mainframe->fd, &crop, NULL, 0, -1, NULL, NULL);

    mJpegThreadRunning = true;

    /* Display postview image*/
    LOGV("Displaying Postview Image");
    if(mUseOverlay) {
            mOverlayLock.lock();
            if(mOverlay != NULL) {
                mOverlay->setFd(postviewframe->fd);
                //mOverlay->setCrop(0, 0, previewWidth, previewWidth);
                }
                LOGV(" Queueing Postview for display ");
                mOverlay->queueBuffer((void *)0);
            
            mOverlayLock.unlock();
        }

     // send upperlayer callback
    // if (mDataCallback && (mMsgEnabled & CAMERA_MSG_RAW_IMAGE)){

    //     mDataCallback(CAMERA_MSG_RAW_IMAGE, ???,
    //                            mCallbackCookie);
     //}
     LOGV("Leaving receiveRawPicture");
}


status_t QualcommCameraHardware::takePicture()
{
    LOGV("takePicture(%d)", mMsgEnabled);


    LOGD("Stope preview");
    stopPreviewInternal();

/*    if( MM_CAMERA_OK != 
        HAL_camerahandle[HAL_currentCameraId]->ops->action(
            HAL_camerahandle[HAL_currentCameraId], 
            FALSE, MM_CAMERA_OPS_PREVIEW, 0)) {
        LOGE("Stop Preview Failed");
        return UNKNOWN_ERROR;
    }
*/

    /* Acquire Snapshot channel */
    LOGD("Acquire snapshot channel");
    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->ops->ch_acquire(
            HAL_camerahandle[HAL_currentCameraId],
            MM_CAMERA_CH_SNAPSHOT)) {
        LOGE("CHANNEL ACQUIRE: Acquire MM_CAMERA_CH_SNAPSHOT channel failed");
        return UNKNOWN_ERROR;
    }

     /* Set camera op mode to MM_CAMERA_OP_MODE_CAPTURE */
    LOGD("Setting OP_MODE_CAPTURE");
    mm_camera_op_mode_type_t op_mode = MM_CAMERA_OP_MODE_CAPTURE;
    if( MM_CAMERA_OK != 
        HAL_camerahandle[HAL_currentCameraId]->cfg->set_parm(
            HAL_camerahandle[HAL_currentCameraId], 
            MM_CAMERA_PARM_OP_MODE, &op_mode)) {
        LOGE("SET MODE: MM_CAMERA_OP_MODE_CAPTURE failed");
        return UNKNOWN_ERROR;
    }

    /* Set dimension, format and allocate memory for snapshot main image/thumbnail*/
    LOGD("Set dimension/format and allocate memory");
    if(initRaw(1) != true){
        LOGE("Failure initialzing snapshot buffers");
        return UNKNOWN_ERROR;
    }


    LOGD("Set snapshot callback");
    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(
            HAL_camerahandle[HAL_currentCameraId], MM_CAMERA_CH_SNAPSHOT,
            receive_snapshot_frame_cb, NULL)
            ) {
        LOGE("takePicture: Failure setting snapshot callback");
        return UNKNOWN_ERROR;
    }

    /* Prepare snapshot*/
    LOGD("Prepare snapshot");
    if( MM_CAMERA_OK !=
        HAL_camerahandle[HAL_currentCameraId]->ops->action(
            HAL_camerahandle[HAL_currentCameraId], TRUE, MM_CAMERA_OPS_PREPARE_SNAPSHOT,
            0)
            ) {
        LOGE("takePicture: Failure preparing snapshot");
        return UNKNOWN_ERROR;
    }


    LOGD("Call MM_CAMERA_OPS_SNAPSHOT");
    if( MM_CAMERA_OK != 
        HAL_camerahandle[HAL_currentCameraId]->ops->action(
        HAL_camerahandle[HAL_currentCameraId], TRUE, MM_CAMERA_OPS_SNAPSHOT,
        0)
        ) {
           LOGE("Failure taking snapshot");
           return UNKNOWN_ERROR;
    }

    /* Wait for snapshot frame callback to return*/
    LOGD("Waiting for Snapshot notifcation done");
    mm_app_snapshot_wait();

    LOGD("Disabling Snapshot");
    if( MM_CAMERA_OK != 
        HAL_camerahandle[HAL_currentCameraId]->ops->action(
        HAL_camerahandle[HAL_currentCameraId], FALSE, MM_CAMERA_OPS_SNAPSHOT,
        0)
        ) {
           LOGE("runSnapshotThread: Failure taking snapshot");
    }

    LOGV("takePicture: X");
       
    return NO_ERROR;
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
#if 0
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
#endif
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
    status_t rc = UNKNOWN_ERROR;
    LOGI("cancelPicture: E");
#if 0
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
    rc = native_stop_ops(CAMERA_OPS_SNAPSHOT, NULL) ? NO_ERROR : UNKNOWN_ERROR;
    mSnapshotDone = FALSE; 
#endif     
    LOGI("cancelPicture: X: %d", rc);
    return rc;
}


status_t QualcommCameraHardware::setHistogramOn()
{
    LOGV("setHistogramOn: EX");
#if 0
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
#endif             
    return NO_ERROR;

}

status_t QualcommCameraHardware::setHistogramOff()
{
    LOGV("setHistogramOff: EX");
#if 0
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
#endif
    return NO_ERROR;
}


status_t QualcommCameraHardware::runFaceDetection()
{
    bool ret = true;
#if 0
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
#endif             
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

void  QualcommCameraHardware::runSmoothzoomThread(void * data)
{
    LOGV("runSmoothZoom thread current zoom %d - target %d",  mParameters.getInt("zoom"), mTargetSmoothZoom);
    int current_zoom = mParameters.getInt("zoom");
    int step = (current_zoom > mTargetSmoothZoom)?-1:1;

    if( current_zoom == mTargetSmoothZoom )
    {
        LOGV("Smoothzoom target zoom value is same as current zoom value, return...");
        mNotifyCallback(CAMERA_MSG_ZOOM, current_zoom, 1, mCallbackCookie);
        return;
    }

    CameraParameters p = getParameters();

    mSmoothzoomThreadWaitLock.lock();
    mSmoothzoomThreadRunning = true;
    mSmoothzoomThreadWaitLock.unlock();

    int i = current_zoom;
    while(1){  // Thread loop
        mSmoothzoomThreadLock.lock();
        if(mSmoothzoomThreadExit) {
            LOGV("Exiting smoothzoom thread, as stop smoothzoom called.");
            mNotifyCallback(CAMERA_MSG_ZOOM, i, 1, mCallbackCookie);
            mSmoothzoomThreadLock.unlock();
            break;
        }
        mSmoothzoomThreadLock.unlock();

        /*if(i < 0 ||  i > mMaxZoom){
            LOGE(" ERROR : beyond supported zoom values, break..");
            break;
        }*/
        // update zoom
        p.set("zoom", i);
        LOGE("Calling Set Zoom from Thread");
        setZoom(p);

        // give call back to zoom listener in app
        mNotifyCallback(CAMERA_MSG_ZOOM, i, (mTargetSmoothZoom-i == 0)?1:0,
                    mCallbackCookie);
        if(i==mTargetSmoothZoom)break;
        i+=step;

        // wait on a singal , which will be signalled on receiving next preview frame
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
            if(mode & CAMERA_SNAPSHOT_ZSL)
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
    LOGI("createInstance Got lock");

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
            LOGI("createInstance: X return existing hardware=%p", &(*hardware));
            singleton_lock.unlock();
            return hardware;
        }
    }

    LOGI("createInstance: Calling constructor ");

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
void QualcommCameraHardware::receiveRecordingFrame(mm_camera_ch_data_buf_t*frame)
{
   LOGV("receiveRecordingFrame E");
   #if 0
    // post busy frame
    if (frame)
    {
        cam_frame_post_video (frame);
    }
    else 
        LOGE("in  receiveRecordingFrame frame is NULL");
#endif
    mCallbackLock.lock();
    int msgEnabled = mMsgEnabled;
    LOGE("Guru: Calling Encoder");
    data_callback_timestamp rcb = mDataCallbackTimestamp;
    void *rdata = mCallbackCookie;
    mCallbackLock.unlock();
    
    nsecs_t timeStamp = nsecs_t(frame->video.video.frame->ts.tv_sec)*1000000000LL + frame->video.video.frame->ts.tv_nsec;

    LOGE("Video Callback : got video frame, giving frame to services/encoder Timestamp:  %ld",timeStamp);  
    LOGE("receiveRecordingFrame frame idx = %d\n", frame->video.video.idx);
    if(mRecordEnable == true) {
        //FreeQueueCount++;
        //mRecordFreeQueue.add(frame);
        LOGE("### Call to services/encoder ###");
        rcb(timeStamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap->mBuffers[frame->video.video.idx], rdata);
    }

    HAL_camerahandle[HAL_currentCameraId]->evt->buf_done(HAL_camerahandle[HAL_currentCameraId],frame);
	/*********************************************************************************/
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
        LOGE("###########ignoring preview callback--camera has been stopped");
        //LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);
        return;
    }

    if(mPreviewBusyQueue.add(frame) == false)
    {
        LOGE("###########Failed to add to busy queue");
        //LINK_camframe_add_frame(CAM_PREVIEW_FRAME,frame);
    }
   

//  LOGV("receivePreviewFrame X");
}
#if 0
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
#endif


bool QualcommCameraHardware::initRecord()
{
    const char *pmem_region;
    //int CbCrOffset;
   int recordBufferSize;
    mm_camera_reg_buf_t reg_buf;
    LOGV("initREcord E");
    int rc = 0;
	uint32_t y_off,cbcr_off,frame_len;
	uint32_t offset[VIDEO_BUFFER_COUNT]; 
	
#if 0
    rc = mm_app_prepare_video_buf2 (mDimension.enc_format, CAMERA_MODE_2D, 
                                VIDEO_BUFFER_COUNT, mDimension.video_width,mDimension.video_height);
#else
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
     if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->evt->register_buf_notify(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_CH_VIDEO,receive_camframe_video_callback,&obj)) {
                LOGE("InitRecord: mm_camera_register_buf_notify failed: ");
                return FALSE;
    		}

     frame_len = mm_camera_get_msm_frame_len(mDimension.enc_format, CAMERA_MODE_2D, 
                                             mDimension.video_width,mDimension.video_height, &y_off, &cbcr_off, MM_CAMERA_PAD_2K);
      g_record_frame_len = frame_len;
      pmem_region = "/dev/pmem_adsp";

    LOGE("initRecord: mDimension.video_width = %d mDimension.video_height = %d",
             mDimension.video_width, mDimension.video_height);
    LOGE("initRecord: frame_len = %d, cbcr_off =  %d", frame_len, cbcr_off);
         
    // for 8x60 the Encoder expects the CbCr offset should be aligned to 2K.
    /*if(mCurrentTarget == TARGET_MSM8660) {
        CbCrOffset = PAD_TO_2K(mDimension.video_width  * mDimension.video_height);
        recordBufferSize = CbCrOffset + PAD_TO_2K((mDimension.video_width * mDimension.video_height)/2);
    } else*/ {
        //CbCrOffset = (mDimension.video_width  * mDimension.video_height);
        recordBufferSize = (mDimension.video_width  * mDimension.video_height *3)/2;
    }

    /* Buffersize and frameSize will be different when DIS is ON.
     * We need to pass the actual framesize with video heap, as the same
     * is used at camera MIO when negotiating with encoder.
     */
    mRecordFrameSize = recordBufferSize;
    //LOGE("mansoor: RecordBufferSize:%d",mRecordFrameSize);
/*/
	bool dis_disable = 0;
    const char *str = mParameters.get(CameraParameters::KEY_VIDEO_HIGH_FRAME_RATE);
    if((str != NULL) && (strcmp(str, CameraParameters::VIDEO_HFR_OFF))) {
        LOGE("%s: HFR is ON, DIS has to be OFF", __FUNCTION__);
        dis_disable = 1;
    }
*/
    /*if((mVpeEnabled && mDisEnabled && (!dis_disable))|| mIs3DModeOn){
        mRecordFrameSize = videoWidth * videoHeight * 3 / 2;
        if(mCurrentTarget == TARGET_MSM8660){
            mRecordFrameSize = PAD_TO_2K(videoWidth * videoHeight)
                                + PAD_TO_2K((videoWidth * videoHeight)/2);
        }
    }*/
    //LOGE("mRecordFrameSize = %d", mRecordFrameSize);
    if(mRecordHeap == NULL) {
        LOGE("mansoor:CallingPmemPool in initRecord");
        mRecordHeap = new PmemPool(pmem_region,
                               MemoryHeapBase::READ_ONLY | MemoryHeapBase::NO_CACHING,
                                MSM_PMEM_VIDEO,
                                frame_len,
                                VIDEO_BUFFER_COUNT,
                                frame_len,
                                cbcr_off,
                                0,
                                "record");
        if (!mRecordHeap->initialized()) {
            mRecordHeap.clear();
            mRecordHeap = NULL;
            LOGE("initRecord X: could not initialize record heap.");
            return false;
        }
    } else {
        /*if(mHFRMode == true) {
            LOGI("%s: register record buffers with camera driver", __FUNCTION__);
            register_record_buffers(true);
            mHFRMode = false;
        }*/
    }

    for (int cnt = 0; cnt < VIDEO_BUFFER_COUNT; cnt++) {
        recordframes[cnt].fd = mRecordHeap->mHeap->getHeapID();
        recordframes[cnt].buffer =
            (uint32_t)mRecordHeap->mHeap->base() + mRecordHeap->mAlignedBufferSize * cnt;
        recordframes[cnt].y_off = y_off;
        recordframes[cnt].cbcr_off = cbcr_off;
        recordframes[cnt].path = OUTPUT_TYPE_V;
        record_buffers_tracking_flag[cnt] = false;
		offset[cnt] =  mRecordHeap->mAlignedBufferSize * cnt;
        LOGE ("initRecord :  record heap , video buffers  buffer=%lu fd=%d y_off=%d cbcr_off=%d \n",
          (unsigned long)recordframes[cnt].buffer, recordframes[cnt].fd, recordframes[cnt].y_off,
          recordframes[cnt].cbcr_off);
    }

	#if 0
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
#endif
    // flush free queue and add 5,6,7,8 buffers.
    //LINK_camframe_release_all_frames(CAM_VIDEO_FRAME);
   /* if(mVpeEnabled) {
        //If VPE is enabled, the VPE buffer shouldn't be added to Free Q initally.
        for(int i=ACTIVE_VIDEO_BUFFERS;i <kRecordBufferCount-1; i++)
            LINK_camframe_add_frame(CAM_VIDEO_FRAME,&recordframes[i]);
    } else */
/*            reg_buf.ch_type = MM_CAMERA_CH_PREVIEW;
            reg_buf.preview.num = kPreviewBufferCount;
            reg_buf.preview.frame = &frames[0];
            for(int i=ACTIVE_PREVIEW_BUFFERS ;i <kPreviewBufferCount; i++)
            {
                HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
                //LINK_camframe_add_frame(CAM_PREVIEW_FRAME,&frames[i]);
            }
*/
    memset(&reg_buf, 0, sizeof(reg_buf));
    reg_buf.ch_type = MM_CAMERA_CH_VIDEO;
    reg_buf.video.video.num = VIDEO_BUFFER_COUNT;//kRecordBufferCount;
	reg_buf.video.video.frame_offset = offset;
    reg_buf.video.video.frame = &recordframes[0];
    LOGE("reg video buf type =%d, offset[1] =%d, buffer[1] =%lx", reg_buf.ch_type, offset[1], recordframes[1].buffer);
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->prepare_buf(HAL_camerahandle[HAL_currentCameraId],&reg_buf);
	if(rc != MM_CAMERA_OK) {
		LOGE("%s:reg video buf err=%d\n", __func__, rc);
	}
#endif
// mzhu         
    LOGV("initREcord X");

    return true;
}


status_t QualcommCameraHardware::setDIS() {
    LOGV("setDIS E");

    video_dis_param_ctrl_t disCtrl;
    bool ret = true;
#if 0
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
#endif
    LOGV("setDIS X (%d)", ret);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::setVpeParameters()
{
    LOGV("setVpeParameters E");

    video_rotation_param_ctrl_t rotCtrl;
    bool ret = true;
#if 0
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
#endif
    LOGV("setVpeParameters X (%d)", ret);
    return ret ? NO_ERROR : UNKNOWN_ERROR;
}

status_t QualcommCameraHardware::startRecording()
{
    LOGE("startRecording E");
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
        mRecordEnable = true;
        return startRecordingInternal();
    }

    return ret;
}

status_t QualcommCameraHardware::startRecordingInternal()
{
    LOGI("%s: E", __FUNCTION__);
    mReleasedRecordingFrame = false;
#if 0
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
#endif      
   /******************************Guru : Video Recording*******************************************/
	/*	mRecordBusyQueue.init();

		mVideoThreadWaitLock.lock();
        mVideoThreadExit = 0;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        mVideoThreadRunning = !pthread_create(&mVideoThread,
                                              &attr,
                                              video_thread,
                                              NULL);
        mVideoThreadWaitLock.unlock();*/
        if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action(HAL_camerahandle[HAL_currentCameraId],1,MM_CAMERA_OPS_VIDEO,0)) {
                LOGE("InitRecord: Stream on failed: ");
                return FALSE;
            }
		LOGE(" Video Thread is Started");
    /****************************************************************************************************/     
    LOGV("%s: E", __FUNCTION__);
    return NO_ERROR;
}

void QualcommCameraHardware::stopRecording()
{
    LOGV("stopRecording: E");
	mStopRecording = true;
    mRecordEnable = false;
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action(HAL_camerahandle[HAL_currentCameraId],0,MM_CAMERA_OPS_VIDEO,0)) {
                LOGE("stopRecording: Stream on failed: ");
    }
#if 0
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
#endif
    LOGV("stopRecording: X");
}

void QualcommCameraHardware::releaseRecordingFrame(
       const sp<IMemory>& mem __attribute__((unused)))
{
    LOGV("releaseRecordingFrame E");
#if 0
    ssize_t offset;
    size_t size;
    mm_camera_ch_data_buf_t *frame;

    //vector<int>::iterator i;
    int i;
    //sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);
    //for (i = mRecordFreeQueue.begin();i<mRecordFreeQueue.end();i++  ) {
    //for (frame = mRecordFreeQueue.front();frame==mRecordFreeQueue.back();i++  ) {
    for (int i = 0;i<FreeQueueCount;i++  ) {
        frame = mRecordFreeQueue.itemAt(i);
        sp<IMemory> org_buffer = mRecordHeap->mBuffers[frame->video.video.idx];
        if(org_buffer ==  mem) {
            mRecordFreeQueue.removeAt(i);
            FreeQueueCount--;
            //HAL_camerahandle[HAL_currentCameraId]->evt->buf_done(HAL_camerahandle[HAL_currentCameraId],frame);
            LOGE("This is the buffer to release");
            break;
        }
    }

#endif
#if 0
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
#endif
    LOGV("releaseRecordingFrame X");
}
#if 0
#endif
bool QualcommCameraHardware::recordingEnabled()
{
    LOGE("Guru : recordingEnabled ");
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
#if 0 /* Ignoring for now */
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
#endif
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
    LOGE("################requested preview size %d x %d", width, height);

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
#if 0
    params.getPreviewFpsRange(&minFps,&maxFps);
    LOGE("FPS Range Values: %dx%d", minFps, maxFps);

    for(size_t i=0;i<FPS_RANGES_SUPPORTED_COUNT;i++)
    {
        if(minFps==FpsRangesSupported[i].minFPS && maxFps == FpsRangesSupported[i].maxFPS){
            mParameters.setPreviewFpsRange(minFps,maxFps);
            return NO_ERROR;
        }
    }
#endif     
    return BAD_VALUE;
}

/*
status_t QualcommCameraHardware::setPreviewFrameRate(const CameraParameters& params)
{
#if 0
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
#endif     
    return BAD_VALUE;
}
*/
/*
status_t QualcommCameraHardware::setPreviewFrameRateMode(const CameraParameters& params) {
#if 0
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
#endif     
    return BAD_VALUE;
}
*/
status_t QualcommCameraHardware::setJpegThumbnailSize(const CameraParameters& params){
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
    LOGE("QualcommCameraHardware::setPictureSize E");
    params.getPictureSize(&width, &height);
    LOGE("requested picture size %d x %d", width, height);

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
    LOGE("setJpegQuality X");
    return rc;
}

status_t QualcommCameraHardware::setPreviewFormat(const CameraParameters& params) {
    const char *str = params.getPreviewFormat();
    int32_t previewFormat = attr_lookup(preview_formats, sizeof(preview_formats) / sizeof(str_map), str);
    if(previewFormat != NOT_FOUND) {
        mParameters.set(CameraParameters::KEY_PREVIEW_FORMAT, str);
        mPreviewFormat = previewFormat;
        LOGE("Setting preview format to %d",mPreviewFormat);        
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

/*
status_t QualcommCameraHardware::setSkinToneEnhancement(const CameraParameters& params) {
#if 0
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
#endif
    return NO_ERROR;
}*/

status_t QualcommCameraHardware::setFlash(const CameraParameters& params)
{
#if 0
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
            return ret ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
    LOGE("Invalid flash mode value: %s", (str == NULL) ? "NULL" : str);
#endif
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setOverlayFormats(const CameraParameters& params)
{
    mParameters.set("overlay-format", HAL_PIXEL_FORMAT_YCbCr_420_SP);
    if(mIs3DModeOn == true) {
        int ovFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP|HAL_3D_IN_SIDE_BY_SIDE_L_R|HAL_3D_OUT_SIDE_BY_SIDE;
        mParameters.set("overlay-format", ovFormat);
    }
    return NO_ERROR;
}

/*status_t QualcommCameraHardware::setAntibanding(const CameraParameters& params)
{   int result;
#if 0
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
#endif
    return BAD_VALUE;
}*/

status_t QualcommCameraHardware::setMCEValue(const CameraParameters& params)
{
#if 0
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
#endif
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setHighFrameRate(const CameraParameters& params)
{
#if 0
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
#endif
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setLensshadeValue(const CameraParameters& params)
{
#if 0
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
#endif
    return BAD_VALUE;
}

/*
status_t QualcommCameraHardware::setTouchAfAec(const CameraParameters& params)
{
#if 0
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
#endif
    return NO_ERROR;
}
*/
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
#if 0
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
#endif
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
    LOGE("setGpsLocation X");
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
    LOGE("setRotation");
    return rc;
}

status_t QualcommCameraHardware::setDenoise(const CameraParameters& params)
{
#if 0
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
#endif
    return BAD_VALUE;
}
status_t QualcommCameraHardware::updateFocusDistances(const char *focusmode)
{
    LOGV("%s: IN", __FUNCTION__);
    focus_distances_info_t focusDistances;

    status_t rc = NO_ERROR;

    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->get_parm(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_FOCUS_DISTANCES,(void *)&focusDistances);
    if(rc != MM_CAMERA_OK) {
        LOGE("%s:Parameter MM_CAMERA_PARM_FOCUS_DISTANCES is not supported for this sensor", __func__);
        return NO_ERROR;
    }
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
    
#if 0
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
#endif
    LOGE("%s: get CAMERA_PARM_FOCUS_DISTANCES failed!!!", __FUNCTION__);
    return UNKNOWN_ERROR;
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

void * QualcommCameraHardware::MMCameraDL::pointer(){
    return libmmcamera;
}


wp<QualcommCameraHardware::MMCameraDL> QualcommCameraHardware::MMCameraDL::instance;
Mutex QualcommCameraHardware::MMCameraDL::singletonLock;

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
        /*TODO replace with newer functions*/
        /*if(native_start_ops(register_buffer ? CAMERA_OPS_REGISTER_BUFFER :
                CAMERA_OPS_UNREGISTER_BUFFER ,(void *)&pmemBuf) < 0) {
            LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM  error %s",
                strerror(errno));
            return false;
        }*/
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

    //mMMCameraDLRef = QualcommCameraHardware::MMCameraDL::getInstance();


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
 /*               register_buf(mBufferSize,
                         mFrameSize, mCbCrOffset, myOffset,
                         mHeap->getHeapID(),
                         mAlignedBufferSize * cnt,
                         (uint8_t *)mHeap->base() + mAlignedBufferSize * cnt,
                         pmem_type,
                         active);
  */          }
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
    /*TODO*/
    /*if(native_start_ops(register_buffer ? CAMERA_OPS_REGISTER_BUFFER :
        CAMERA_OPS_UNREGISTER_BUFFER ,(void *)&pmemBuf) < 0) {
         LOGE("register_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM  error %s",
               strerror(errno));
         return false;
         }*/

    return true;

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
static int mm_app_dump_video_frame(struct msm_frame *frame, uint32_t len, int is_preview, int water_mark, int num_run)
{
	static int p_cnt = 0;
	static int v_cnt = 0;
	char bufp[128];
	int file_fdp;
	int rc = 0;

	if(is_preview) {
		p_cnt++;
		if( 100 >= p_cnt) 
			sprintf(bufp, "/data/dump/mans%d_%d.yuv", num_run, p_cnt/100);
		else 
			return 0;
	} else {
		v_cnt++;
		if(0 == (v_cnt % 100) && water_mark >= v_cnt) 
			sprintf(bufp, "/data/v%d_%d.yuv", num_run, v_cnt/100);
		else 
			return 0;
	}

	file_fdp = open(bufp, O_RDWR | O_CREAT, 0777);

	if (file_fdp < 0) {
		LOGE("cannot open file %s\n", bufp);
		rc = -1;
		goto end;
	}
	LOGE("%s:dump frame to '%s'\n", __func__, bufp);
	write(file_fdp,
		(const void *)frame->buffer, len);
	close(file_fdp);
end:
	return rc;
}
void static do_receive_camframe_callback(mm_camera_ch_data_buf_t* packed_frame, void* HAL_obj)
{   
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    obj->receive_camframe_callback(packed_frame,HAL_obj);
}
static int frms_cnt;
void QualcommCameraHardware::receive_camframe_callback(mm_camera_ch_data_buf_t* packed_frame, void* HAL_obj)
//old signature (struct msm_frame *frame)
{
  
  //   LOGE("####mansoor: Got frame ID = %d!!", packed_frame->def.idx);
     mCallbackLock.lock();
     int msgEnabled = mMsgEnabled;
     data_callback pcb = mDataCallback;
     void *pdata = mCallbackCookie;
     data_callback_timestamp rcb = mDataCallbackTimestamp;
     void *rdata = mCallbackCookie;
     data_callback mcb = mDataCallback;
     void *mdata = mCallbackCookie;
     mCallbackLock.unlock();

     //packed_frame->def.frame;
 //    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
  //   if (obj != 0) {
  //       obj->receivePreviewFrame(packed_frame->def.frame);
  //   }

#if 1
    mOverlayLock.lock();
    if(mOverlay != NULL) {
  //      uint8_t * tmp = (uint8_t *)(mPreviewHeap->mHeap->base() + packed_frame->def.idx*mPreviewFrameSize);
   //     memcpy(tmp, (void *)packed_frame->def.frame->buffer, mPreviewFrameSize); 
    //    LOGE("##############Calling setFd with FD:%d",mPreviewHeap->mHeap->getHeapID());
        //mOverlay->setFd(mPreviewHeapx[packed_frame->def.idx]->mHeap->getHeapID());
        mOverlay->setFd(previewframes[packed_frame->def.idx].fd);

    //    ssize_t offset_addr = 
        //    (ssize_t)packed_frame->def.frame->buffer - (ssize_t)mPreviewHeap->mHeap->base();
      //   LOGE("####mansoor: Got frame ID = %d buffer add: 0x%x!!", packed_frame->def.idx,packed_frame->def.frame->buffer);
        //ssize_t offset = offset_addr / mPreviewHeap->mAlignedBufferSize;
        //if(!frms_cnt) {
        //    mOverlay->setFd(mPreviewHeap->mHeap->getHeapID());
         //  frms_cnt++;
       // }
#if 0
        common_crop_t *crop = (common_crop_t *) (packed_frame->def.frame->cropinfo);
        if(crop) {
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
        }     
#endif             
        mOverlay->queueBuffer((void *)0/*offset_addr*/);
        
    }
    mOverlayLock.unlock();
    if (pcb != NULL && (msgEnabled & CAMERA_MSG_PREVIEW_FRAME)){
        LOGE("#######Calling upper layer callback");
        //pcb(CAMERA_MSG_PREVIEW_FRAME, (void*)packed_frame->def.frame->buffer /*mPreviewHeap->mBuffers[offset]*/,pdata);
    }
   // mm_app_dump_video_frame(packed_frame->def.frame,g_mPreviewFrameSize, TRUE, 1000, 0);
    if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->evt->buf_done(HAL_camerahandle[HAL_currentCameraId],packed_frame))
    {
        LOGE("###############BUF DONE FAILED");
    }         
#endif             
   // LOGE("####mansoor/Bikas:receive_camframe_callback X");
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

#if 0
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
#endif         
// 720p : video frame calbback from camframe
static void receive_camframe_video_callback(mm_camera_ch_data_buf_t* packed_frame, void* HAL_obj)
{
    LOGE("receive_camframe_video_callback E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
			obj->receiveRecordingFrame(packed_frame);
	 }
    LOGE("receive_camframe_video_callback X");
}

void QualcommCameraHardware::setCallbacks(notify_callback notify_cb,
                             data_callback data_cb,
                             data_callback_timestamp data_cb_timestamp,
                             void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCallback = notify_cb;
    mDataCallback = data_cb;
    LOGE("Encoder Callback Registred");
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
    if((mCurrentTarget == TARGET_MSM7630) || (mCurrentTarget == TARGET_MSM8660)|| (mCurrentTarget == TARGET_MSM8960) ) {
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
        LOGE(" Valid overlay object ");
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

        if(mUseOverlay) {
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
    if( ( mCurrentTarget == TARGET_MSM7630 ) || (mCurrentTarget == TARGET_QSD8250) || (mCurrentTarget == TARGET_MSM8660)|| (mCurrentTarget == TARGET_MSM8960) )
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
#if 0
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
#endif
    LOGV("encodeData: X");
}

void QualcommCameraHardware::getCameraInfo()
{
    LOGI("getCameraInfo: IN");
    uint8_t num_camera;
    mm_camera_t * handle_base;
    //mm_camera_status_t status;

//#if DLOPEN_LIBMMCAMERA
//    void *libhandle = ::dlopen("liboemcamera.so", RTLD_NOW);
//    LOGI("getCameraInfo: loading libqcamera at %p", libhandle);
//    if (!libhandle) {
//        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
//    }
//    *(void **)&LINK_mm_camera_get_camera_info =
//        ::dlsym(libhandle, "mm_camera_get_camera_info");
//#endif

//    status = LINK_mm_camera_get_camera_info(HAL_cameraInfo, &HAL_numOfCameras);
    handle_base= mm_camera_query(&num_camera);
    LOGE("Handle base =%p",handle_base);
    HAL_numOfCameras=num_camera;
    
    LOGI("getCameraInfo: numOfCameras = %d", HAL_numOfCameras);
    for(int i = 0; i < HAL_numOfCameras; i++) {
	LOGE("Handle [%d]=0x%x",i,(uint32_t)(handle_base+i));
        HAL_camerahandle[i]=handle_base + i; 
        HAL_cameraInfo[i]=HAL_camerahandle[i]->camera_info;
        LOGI("Camera sensor %d info:", i);
        LOGI("camera_id: %d", HAL_cameraInfo[i].camera_id);
        LOGI("modes_supported: %x", HAL_cameraInfo[i].modes_supported);
        LOGI("position: %d", HAL_cameraInfo[i].position);
        LOGI("sensor_mount_angle: %d", HAL_cameraInfo[i].sensor_mount_angle);
    }
    storeTargetType();

//#if DLOPEN_LIBMMCAMERA
//    if (libhandle) {
//        ::dlclose(libhandle);
//        LOGV("getCameraInfo: dlclose(libqcamera)");
//    }
//#endif
    LOGI("getCameraInfo: OUT");
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
            else if( !strncmp(mDeviceName, "msm8960", 7))
                cameraInfo->orientation = HAL_cameraInfo[i].sensor_mount_angle;
            else
                cameraInfo->orientation = ((APP_ORIENTATION - HAL_cameraInfo[i].sensor_mount_angle) + 360)%360;

            LOGI("%s: orientation = %d", __FUNCTION__, cameraInfo->orientation);
            cameraInfo->mode = 0;
            if(HAL_cameraInfo[i].modes_supported & CAMERA_MODE_2D)
                cameraInfo->mode |= CAMERA_SUPPORT_MODE_2D;
            if(HAL_cameraInfo[i].modes_supported & CAMERA_MODE_3D)
                cameraInfo->mode |= CAMERA_SUPPORT_MODE_3D;

            LOGI("%s: modes supported = %d", __FUNCTION__, cameraInfo->mode);

            return;
        }
    }
    LOGE("Unable to find matching camera info for ID %d", cameraId);
}

}; // namespace android

