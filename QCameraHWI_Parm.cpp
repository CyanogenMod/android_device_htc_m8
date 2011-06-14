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

static int16_t * zoomRatios;
static bool zoomSupported = false;
static int32_t mMaxZoom = 0;

//@Guru - May have to remove these arrays once we moved all the set functions from QCamera_HAL.cpp
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

#if 0  //@Guru : need to enable once CAMERA_PARM_ZOOM_RATIO is supported
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
}
status_t QualcommCameraHardware::setParameters(const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);
    Mutex::Autolock pl(&mParametersLock);
    status_t rc, final_rc = NO_ERROR;
    
    LOGE("Final_rc1:%d",final_rc);
    
    if ((rc = setPreviewSize(params)))  final_rc = rc;
    if ((rc = setRecordSize(params)))  final_rc = rc;
//    if ((rc = setPictureSize(params)))  final_rc = rc;
//    if ((rc = setJpegThumbnailSize(params))) final_rc = rc;
//    if ((rc = setJpegQuality(params)))  final_rc = rc;
    if ((rc = setEffect(params)))       final_rc = rc;
//    if ((rc = setGpsLocation(params)))  final_rc = rc;
//    if ((rc = setRotation(params)))     final_rc = rc;
//    if ((rc = setZoom(params)))         final_rc = rc;  //@Guru : Need support to Query from Lower layer
     LOGE("setZoom rc:%d",final_rc);
//    if ((rc = setOrientation(params)))  final_rc = rc;
//    if ((rc = setLensshadeValue(params)))  final_rc = rc;
//    if ((rc = setMCEValue(params)))  final_rc = rc;
    if ((rc = setPictureFormat(params))) final_rc = rc;
    if ((rc = setSharpness(params)))    final_rc = rc;
    LOGE("Final_rc3:%d",final_rc);
//    if ((rc = setSaturation(params)))   final_rc = rc;
//    if ((rc = setTouchAfAec(params)))   final_rc = rc;
//      if ((rc = setSceneMode(params)))    final_rc = rc;
//      LOGE("RC setSceneMode:%d",final_rc);
      if ((rc = setContrast(params)))     final_rc = rc;
      LOGE("Final_rc4 for contast : %d",final_rc);
//      if ((rc = setSceneDetect(params)))  final_rc = rc;
//      LOGE("setSceneDetect rc:%d",final_rc);
//    if ((rc = setStrTextures(params)))   final_rc = rc;
    if ((rc = setPreviewFormat(params)))   final_rc = rc;
    LOGE("Final_rc4:%d",final_rc);
//    if ((rc = setSkinToneEnhancement(params)))   final_rc = rc;
//    if ((rc = setAntibanding(params)))  final_rc = rc;
//    if ((rc = setOverlayFormats(params)))  final_rc = rc;
//    if ((rc = setRedeyeReduction(params)))  final_rc = rc;
//    if ((rc = setDenoise(params)))  final_rc = rc;
//    if ((rc = setPreviewFpsRange(params)))  final_rc = rc;

    const char *str = params.get(CameraParameters::KEY_SCENE_MODE);
    int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);

    if((value != NOT_FOUND) && (value == CAMERA_BESTSHOT_OFF)) {
//        if ((rc = setPreviewFrameRate(params))) final_rc = rc;
//        if ((rc = setPreviewFrameRateMode(params))) final_rc = rc;
//        if ((rc = setAutoExposure(params))) final_rc = rc;
//        if ((rc = setExposureCompensation(params))) final_rc = rc;
//        if ((rc = setWhiteBalance(params))) final_rc = rc;
//        if ((rc = setFlash(params)))        final_rc = rc;
//        if ((rc = setFocusMode(params)))    final_rc = rc;  //@Guru : Need support from Lower level
//        if ((rc = setBrightness(params)))   final_rc = rc;
        if ((rc = setISOValue(params)))  final_rc = rc;
        LOGE("setISOValue rc:%d",final_rc);
    }
    //selectableZoneAF needs to be invoked after continuous AF
//    if ((rc = setSelectableZoneAf(params)))   final_rc = rc;   //@Guru : Need support from Lower level
//    LOGE("setSelectableZoneAf rc:%d",final_rc);
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
#if 0
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
#endif
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
    if (str != NULL) {
        int32_t value = attr_lookup(scenedetect, sizeof(scenedetect) / sizeof(str_map), str);
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
    int8_t temp_hjr;
    if (str != NULL) {
        int value = (camera_iso_mode_type)attr_lookup(
          iso, sizeof(iso) / sizeof(str_map), str);
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
                native_set_parms(MM_CAMERA_PARM_CONTINUOUS_AF, sizeof(int8_t), (void *)&cafSupport);
            }
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

    if (str != NULL) {
        int32_t value = attr_lookup(scenemode, sizeof(scenemode) / sizeof(str_map), str);
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
        int32_t value = attr_lookup(effects, sizeof(effects) / sizeof(str_map), str);
        if (value != NOT_FOUND) {
           rc = HAL_camerahandle[HAL_currentCameraId]->cfg->is_parm_supported(HAL_camerahandle[HAL_currentCameraId],MM_CAMERA_PARM_BESTSHOT_MODE);
           if(rc != MM_CAMERA_OK) {
               LOGE("Camera Effect - %s mode is not supported for this sensor",str);
               return NO_ERROR;
           }else {
               mParameters.set(CameraParameters::KEY_EFFECT, str);
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


} /*namespace android */
