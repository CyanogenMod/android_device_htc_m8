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

#define LOG_NDEBUG 0
#define LOG_NIDEBUG 0
#define LOG_TAG "QCameraHWI_Parm"
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

} // extern "C"

#include "QCameraHAL.h"


/*
 * Values based on aec.c
 */
#define EXPOSURE_COMPENSATION_MAXIMUM_NUMERATOR 12
#define EXPOSURE_COMPENSATION_MINIMUM_NUMERATOR -12
#define EXPOSURE_COMPENSATION_DEFAULT_NUMERATOR 0
#define EXPOSURE_COMPENSATION_DENOMINATOR 6
#define EXPOSURE_COMPENSATION_STEP ((float (1))/EXPOSURE_COMPENSATION_DENOMINATOR)

//Default FPS
#define MINIMUM_FPS 5
#define MAXIMUM_FPS 31
#define DEFAULT_FPS MAXIMUM_FPS

#define DONT_CARE_COORDINATE -1

extern int HAL_numOfCameras;
extern camera_info_t HAL_cameraInfo[MSM_MAX_CAMERA_SENSORS];
extern mm_camera_t * HAL_camerahandle[MSM_MAX_CAMERA_SENSORS];
extern int HAL_currentCameraId;
extern int HAL_currentCameraMode;

namespace android {

static String8 effect_values;
static String8 iso_values;
static String8 scenemode_values;
static String8 scenedetect_values;
static String8 focus_mode_values;
static String8 selectable_zone_af_values;
static String8 autoexposure_values;
static String8 whitebalance_values;
static String8 antibanding_values;
static String8 frame_rate_mode_values;
static String8 touchafaec_values;

static int16_t * zoomRatios;
static bool zoomSupported = false;
static int32_t mMaxZoom = 0;

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
    { CameraParameters::EFFECT_AQUA,       CAMERA_EFFECT_AQUA }
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

#define DONT_CARE 0
static const str_map focus_modes[] = {
    { CameraParameters::FOCUS_MODE_AUTO,     AF_MODE_AUTO},
    { CameraParameters::FOCUS_MODE_INFINITY, DONT_CARE },
    { CameraParameters::FOCUS_MODE_NORMAL,   AF_MODE_NORMAL },
    { CameraParameters::FOCUS_MODE_MACRO,    AF_MODE_MACRO },
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


void QualcommCameraHardware::initDefaultParam()
{
    effect_values = create_values_str(effects, sizeof(effects) / sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_EFFECTS, effect_values);

    antibanding_values = create_values_str(
            antibanding, sizeof(antibanding) / sizeof(str_map));

    iso_values = create_values_str(iso,sizeof(iso)/sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_ISO_MODES,iso_values);

    scenemode_values = create_values_str(scenemode, sizeof(scenemode) / sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,scenemode_values);

    if(supportsSceneDetection()) {
            scenedetect_values = create_values_str(scenedetect, sizeof(scenedetect) / sizeof(str_map));
    }
    mParameters.set(CameraParameters::KEY_SCENE_DETECT,
                    CameraParameters::SCENE_DETECT_OFF);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_DETECT,scenedetect_values);

    if(mHasAutoFocusSupport){
            focus_mode_values = create_values_str(
                    focus_modes, sizeof(focus_modes) / sizeof(str_map));
    }
    autoexposure_values = create_values_str(
            autoexposure, sizeof(autoexposure) / sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_AUTO_EXPOSURE, autoexposure_values);

    whitebalance_values = create_values_str(
            whitebalance, sizeof(whitebalance) / sizeof(str_map));
    mParameters.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,whitebalance_values);

    frame_rate_mode_values = create_values_str(
            frame_rate_modes, sizeof(frame_rate_modes) / sizeof(str_map));
    /* if( mCfgControl.mm_camera_is_supported(CAMERA_PARM_FPS_MODE)){
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATE_MODES,
                    frame_rate_mode_values.string());
    }mansoor */

    if(mHasAutoFocusSupport){
            touchafaec_values = create_values_str(
                touchafaec,sizeof(touchafaec)/sizeof(str_map));
    }

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

    if(mHasAutoFocusSupport && supportsSelectableZoneAf()){
            selectable_zone_af_values = create_values_str(
                selectable_zone_af, sizeof(selectable_zone_af) / sizeof(str_map));
    }
    mParameters.set(CameraParameters::KEY_SELECTABLE_ZONE_AF,
                    CameraParameters::SELECTABLE_ZONE_AF_AUTO);
    mParameters.set(CameraParameters::KEY_SUPPORTED_SELECTABLE_ZONE_AF,
                    selectable_zone_af_values);



    /*if(HAL_camerahandle[HAL_currentCameraId]->cfg->get_parm(HAL_camerahandle[HAL_currentCameraId,MM_CAMERA_PARM_ZOOM_RATIO, (void **)&zoomRatios, (uint32_t *) &mMaxZoom) == MM_CAMERA_OK)
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
     } else */{
            //zoom_ratio_values=0;
            zoomSupported = false;
            LOGE("Failed to get maximum zoom value...setting max "
                    "zoom to zero");
            mMaxZoom = 0;
     }

#if 0  //TODO: need to enable once CAMERA_PARM_ZOOM_RATIO is supported
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
    } else
#endif
    {
        mParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");
    }
    
    mParameters.set(CameraParameters::KEY_SUPPORTED_TOUCH_AF_AEC,
                    touchafaec_values);

    mParameters.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING,
                    antibanding_values);

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
}
status_t QualcommCameraHardware::setParameters(const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);
    Mutex::Autolock pl(&mParametersLock);
    status_t rc, final_rc = NO_ERROR;
    
    if ((rc = setPreviewSize(params)))  final_rc = rc;
    if ((rc = setRecordSize(params)))  final_rc = rc;
//    if ((rc = setPictureSize(params)))  final_rc = rc;
//    if ((rc = setJpegThumbnailSize(params))) final_rc = rc;
//    if ((rc = setJpegQuality(params)))  final_rc = rc;
    if ((rc = setEffect(params)))       final_rc = rc;
    LOGE("setEffect rc:%d",final_rc);
    final_rc = 0;
//    if ((rc = setGpsLocation(params)))  final_rc = rc;
//    if ((rc = setRotation(params)))     final_rc = rc;
//    if ((rc = setZoom(params)))         final_rc = rc;  //@TODO : Need support to Query from Lower layer
//    LOGE("setZoom rc:%d",final_rc);
//    if ((rc = setOrientation(params)))  final_rc = rc;
//    if ((rc = setLensshadeValue(params)))  final_rc = rc;
//    if ((rc = setMCEValue(params)))  final_rc = rc;
    if ((rc = setPictureFormat(params))) final_rc = rc;
    if ((rc = setSharpness(params)))    final_rc = rc;
    LOGE("setSharpness rc:%d",final_rc);
    final_rc = 0;
    if ((rc = setSaturation(params)))   final_rc = rc;
    LOGE("setSaturation rc:%d",final_rc);
    final_rc = 0;
    if ((rc = setTouchAfAec(params)))   final_rc = rc;
    LOGE("setTouchAfAec rc:%d",final_rc);
    final_rc = 0;
    if ((rc = setSceneMode(params)))    final_rc = rc;
    LOGE("RC setSceneMode:%d",final_rc);
    final_rc = 0;
    if ((rc = setContrast(params)))     final_rc = rc;
    LOGE("Final_rc4 for setContrast : %d",final_rc);
    final_rc = 0;
    if ((rc = setSceneDetect(params)))  final_rc = rc;
    LOGE("setSceneDetect rc:%d",final_rc);
    final_rc = 0;
//    if ((rc = setStrTextures(params)))   final_rc = rc;
    if ((rc = setPreviewFormat(params)))   final_rc = rc;
    LOGE("Final_rc4:%d",final_rc);
    if ((rc = setSkinToneEnhancement(params)))   final_rc = rc;
    LOGE("setSkinToneEnhancement:%d",final_rc);
    final_rc = 0;
    if ((rc = setAntibanding(params)))  final_rc = rc;
    LOGE("setAntibanding:%d",final_rc);
    final_rc = 0;
//    if ((rc = setOverlayFormats(params)))  final_rc = rc;
//    if ((rc = setRedeyeReduction(params)))  final_rc = rc;
//    if ((rc = setDenoise(params)))  final_rc = rc;
//    if ((rc = setPreviewFpsRange(params)))  final_rc = rc;

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if((value != NOT_FOUND) && (value == CAMERA_BESTSHOT_OFF)) {
        if ((rc = setPreviewFrameRate(params))) final_rc = rc;
        LOGE("setPreviewFrameRate rc:%d",final_rc);
        final_rc = 0;
        if ((rc = setPreviewFrameRateMode(params))) final_rc = rc;
        LOGE("setPreviewFrameRateMode rc:%d",final_rc);
        final_rc = 0;
        if ((rc = setAutoExposure(params))) final_rc = rc;	 
        LOGE("setAutoExposure rc:%d",final_rc);
        final_rc = 0;
//        if ((rc = setExposureCompensation(params))) final_rc = rc;  
        if ((rc = setWhiteBalance(params))) final_rc = rc;
        LOGE("setWhiteBalance rc:%d",final_rc);
        final_rc = 0;
//        if ((rc = setFlash(params)))        final_rc = rc;
        if ((rc = setFocusMode(params)))    final_rc = rc;  
        LOGE("setFocusMode rc:%d",final_rc);
        final_rc = 0;
        if ((rc = setBrightness(params)))   final_rc = rc;
        LOGE("setBrightness rc:%d",final_rc);
        final_rc = 0;
        if ((rc = setISOValue(params)))  final_rc = rc;
        LOGE("setISOValue rc:%d",final_rc);
        final_rc = 0;
    }
    //selectableZoneAF needs to be invoked after continuous AF
    if ((rc = setSelectableZoneAf(params)))   final_rc = rc;   //@Guru : Need support from Lower level
    LOGE("setSelectableZoneAf rc:%d",final_rc);
    final_rc = 0;
    // setHighFrameRate needs to be done at end, as there can
    // be a preview restart, and need to use the updated parameters
//    if ((rc = setHighFrameRate(params)))  final_rc = rc;
    LOGE("setParameters: X");
    return final_rc;
}

CameraParameters QualcommCameraHardware::getParameters() const
{
    LOGV("getParameters: EX");
    return mParameters;
}


status_t QualcommCameraHardware::setSharpness(const CameraParameters& params)
{
    bool ret = false;
    int rc = MM_CAMERA_OK;
    LOGE("%s",__func__);
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_SHARPNESS);
    if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setSaturation(const CameraParameters& params)
{
    bool ret = false;
    int rc = MM_CAMERA_OK;
    LOGE("%s",__func__);
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_SATURATION);
    if(rc != MM_CAMERA_OK) {
		LOGE("%s:CAMERA_PARM_SATURATION not supported", __func__);
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

status_t QualcommCameraHardware::setContrast(const CameraParameters& params)
{
   LOGE("%s E", __func__ );
   int rc = MM_CAMERA_OK;
   rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_CONTRAST);
   if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setSceneDetect(const CameraParameters& params)
{
	LOGE("%s",__func__);
    bool retParm1, retParm2;
    int rc = MM_CAMERA_OK;
    if (supportsSceneDetection()) {
        rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_BL_DETECTION);
       if(rc != MM_CAMERA_OK) {
    		LOGE("%s:MM_CAMERA_PARM_BL_DETECTION not supported", __func__);
            return NO_ERROR;
    	}
       rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_SNOW_DETECTION);
       if(rc != MM_CAMERA_OK) {
    		LOGE("%s:MM_CAMERA_PARM_SNOW_DETECTION not supported", __func__);
            return NO_ERROR;
    	}
    }
    const char *str = params.get(CameraParameters::KEY_SCENE_DETECT);
    LOGE("Scene Detect string : %s",str);
    if (str != NULL) {
        int32_t value = attr_lookup(scenedetect, sizeof(scenedetect) / sizeof(str_map), str);
        LOGE("Scenedetect Value : %d",value);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_SCENE_DETECT, str);
            
            retParm1 = native_set_parms(MM_CAMERA_PARM_BL_DETECTION, sizeof(value),
                                       (void *)&value);
            
            retParm2 = native_set_parms(MM_CAMERA_PARM_SNOW_DETECTION, sizeof(value),
                                       (void *)&value);
            
            //All Auto Scene detection modes should be all ON or all OFF.
            if(retParm1 == false || retParm2 == false) {
                value = !value;
                retParm1 = native_set_parms(MM_CAMERA_PARM_BL_DETECTION, sizeof(value),
                                           (void *)&value);
            
                retParm2 = native_set_parms(MM_CAMERA_PARM_SNOW_DETECTION, sizeof(value),
                                           (void *)&value);
            }
            return (retParm1 && retParm2) ? NO_ERROR : UNKNOWN_ERROR;
        }
    }
   return BAD_VALUE;
}

status_t QualcommCameraHardware::setZoom(const CameraParameters& params)
{
    status_t rc = NO_ERROR;

    LOGE("%s",__func__);

    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_ZOOM);
    if(rc != MM_CAMERA_OK) {
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
        rc = ret ? NO_ERROR : UNKNOWN_ERROR;
    } else {
        rc = BAD_VALUE;
    }
    LOGE("%s X",__func__);
    return rc;

}

status_t  QualcommCameraHardware::setISOValue(const CameraParameters& params) {

    status_t rc = NO_ERROR;
    LOGE("%s",__func__);

    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_ISO);
    if(rc != MM_CAMERA_OK) {
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


status_t QualcommCameraHardware::setFocusMode(const CameraParameters& params)
{
    const char *str = params.get(CameraParameters::KEY_FOCUS_MODE);
    LOGE("%s",__func__);
    if (str != NULL) {
        int32_t value = attr_lookup(focus_modes,
                                    sizeof(focus_modes) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
            mParameters.set(CameraParameters::KEY_FOCUS_MODE, str);

/*
            if(mHasAutoFocusSupport && (updateFocusDistances(str) != NO_ERROR)) {
                LOGE("%s: updateFocusDistances failed for %s", __FUNCTION__, str);
                return UNKNOWN_ERROR;
            }
            */
            /*
            if(mHasAutoFocusSupport){
                int cafSupport = FALSE;
                if(!strcmp(str, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO)){
                    cafSupport = TRUE;
                }
                LOGV("Continuous Auto Focus %d", cafSupport);
                native_set_parms(MM_CAMERA_PARM_CONTINUOUS_AF, sizeof(int8_t), (void *)&cafSupport);
            }
            */
            // Focus step is reset to infinity when preview is started. We do
            // not need to do anything now.
            return NO_ERROR;
        }
    }
    LOGE("Invalid focus mode value: %s", (str == NULL) ? "NULL" : str);
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setSceneMode(const CameraParameters& params)
{
    status_t rc = NO_ERROR;
    LOGE("%s",__func__);

    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_BESTSHOT_MODE);
    if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setSelectableZoneAf(const CameraParameters& params)
{
    LOGE("%s",__func__);
    if(mHasAutoFocusSupport && supportsSelectableZoneAf()) {
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

status_t QualcommCameraHardware::setEffect(const CameraParameters& params)
{
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    const char *str = params.get(CameraParameters::KEY_EFFECT);
    int result;
    if (str != NULL) {
        LOGE("Setting effect %s",str);
        int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
           rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_EFFECT);
           if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setBrightness(const CameraParameters& params) {

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_BRIGHTNESS);
   if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setAutoExposure(const CameraParameters& params)
{

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_EXPOSURE);
   if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setExposureCompensation(
        const CameraParameters & params){
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_EXPOSURE_COMPENSATION);
    if(rc != MM_CAMERA_OK) {
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

status_t QualcommCameraHardware::setWhiteBalance(const CameraParameters& params)
{

     LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_WHITE_BALANCE);
    if(rc != MM_CAMERA_OK) {
       LOGE("CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
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

void QualcommCameraHardware::runAutoFocus()
{
    LOGE("%s",__func__);
    bool status = true;
#ifndef FAKE_AUTO_FOCUS
    void *libhandle = NULL;
    isp3a_af_mode_t afMode;

    mAutoFocusThreadLock.lock();

    // Skip autofocus if focus mode is infinity.
    const char * focusMode = mParameters.get(CameraParameters::KEY_FOCUS_MODE);
    if ((mParameters.get(CameraParameters::KEY_FOCUS_MODE) == 0)
           || (strcmp(focusMode, CameraParameters::FOCUS_MODE_INFINITY) == 0)
           || (strcmp(focusMode, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO) == 0)) {
        LOGE("Error: Go to Done");
        goto done;
    }

    /*if(!libmmcamera){
        LOGE("FATAL ERROR: could not dlopen liboemcamera.so: %s", dlerror());
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }*/

    afMode = (isp3a_af_mode_t)attr_lookup(focus_modes,
                                sizeof(focus_modes) / sizeof(str_map),
                                mParameters.get(CameraParameters::KEY_FOCUS_MODE));

    /* This will block until either AF completes or is cancelled. */
    LOGV("af start (mode :  %d)", afMode);
    status_t err;
    err = mAfLock.tryLock();
    if(err == NO_ERROR) {
        {
            Mutex::Autolock cameraRunningLock(&mCameraRunningLock);
            if(mCameraRunning){
                LOGV("Start AF  : Call to Native");
                status =  native_start_ops(MM_CAMERA_OPS_FOCUS ,(void *)&afMode);
                LOGV("After native_start_ops for focus");
                /*if(MM_CAMERA_OK!=HAL_camerahandle[HAL_currentCameraId]->ops->action (HAL_camerahandle[HAL_currentCameraId],1,MM_CAMERA_OPS_FOCUS,&afMode )) {
                    LOGE("runAutoFocus: type %d error %s", MM_CAMERA_OPS_FOCUS,strerror(errno));
                    status = false;
                }*/
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
        /*
        if(mHasAutoFocusSupport && (updateFocusDistances(focusMode) != NO_ERROR)) {
            LOGE("%s: updateFocusDistances failed for %s", __FUNCTION__, focusMode);
        } 
        */ 
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
#else

    //yyan: 8960 bring up - fake callback till we really do auto focus
    //and ignore locks for now :
    //mCallbackLock.lock();
    bool autoFocusEnabled = mNotifyCallback && (mMsgEnabled & CAMERA_MSG_FOCUS);
    notify_callback cb = mNotifyCallback;
    void *data = mCallbackCookie;
    //mCallbackLock.unlock();
    if (autoFocusEnabled)
        cb(CAMERA_MSG_FOCUS, status, 0, data);

#endif

}

status_t QualcommCameraHardware::setAntibanding(const CameraParameters& params)
{   
    int result;

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_ANTIBANDING);
    if(rc != MM_CAMERA_OK) {
       LOGE("CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
       return NO_ERROR;
    }
    const char *str = params.get(CameraParameters::KEY_ANTIBANDING);
    LOGE("Antibanding String : %s1",str);
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

status_t QualcommCameraHardware::setPreviewFrameRate(const CameraParameters& params)
{
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_FPS);
    if(rc != MM_CAMERA_OK) {
       LOGE("CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
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
        bool ret = native_set_parms(MM_CAMERA_PARM_FPS,
                sizeof(fps), (void *)&fps);
        return ret ? NO_ERROR : UNKNOWN_ERROR;
    } 
  
    return BAD_VALUE;
}

status_t QualcommCameraHardware::setPreviewFrameRateMode(const CameraParameters& params) {

    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_FPS);
    if(rc != MM_CAMERA_OK) {
       LOGE("CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
       return NO_ERROR;
    }
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_FPS_MODE);
    if(rc != MM_CAMERA_OK) {
       LOGE("CAMERA_PARM_WHITE_BALANCE mode is not supported for this sensor");
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


status_t QualcommCameraHardware::setTouchAfAec(const CameraParameters& params)
{
    LOGE("%s",__func__);
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
                native_set_parms(MM_CAMERA_PARM_AEC_ROI, sizeof(cam_set_aec_roi_t), (void *)&aec_roi_value);
                native_set_parms(MM_CAMERA_PARM_AF_ROI, sizeof(roi_info_t), (void*)&af_roi_value);
            }
            return NO_ERROR;
        }
        LOGE("Invalid Touch AF/AEC value: %s", (str == NULL) ? "NULL" : str);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QualcommCameraHardware::setSkinToneEnhancement(const CameraParameters& params) {
    LOGE("%s",__func__);
    status_t rc = NO_ERROR;
    rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_SCE_FACTOR);
    if(rc != MM_CAMERA_OK) {
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

} /*namespace android */
