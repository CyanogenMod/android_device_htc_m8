/*
 * Copyright (C) 2013, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_amplifier"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <cutils/log.h>

#include <hardware/audio_amplifier.h>

#include "tfa9887.h"
#include "rt5501.h"

#define UNUSED __attribute__((unused))

typedef struct tfa9887_device {
    amplifier_device_t amp_dev;
    int current_devices;
    audio_mode_t current_mode;
} tfa9887_device_t;

static tfa9887_device_t *tfa9887_dev = NULL;

void amplifier_stream_start(struct audio_stream_out *stream, bool offload) {
    // Do nothing
}

void amplifier_stream_standby(struct audio_stream_out *stream) {
    // Do nothing
}

static int amp_set_mode(amplifier_device_t *device, audio_mode_t mode)
{
    int ret = 0;
    tfa9887_device_t *tfa9887 = (tfa9887_device_t *) device;

    tfa9887->current_mode = mode;

    if (tfa9887->current_devices & AUDIO_DEVICE_OUT_WIRED_HEADSET ||
            tfa9887->current_devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        /* Write config for headset amplifier */
        ret = rt5501_set_mode(mode);
    } else {
        /* Write config for speaker amplifier */
        ret = tfa9887_set_mode(mode);
    }

    return ret;
}

static int amp_set_devices(amplifier_device_t *device, int devices)
{
    tfa9887_device_t *tfa9887 = (tfa9887_device_t *) device;

    if (devices != 0) {
        if (tfa9887->current_devices != devices) {
            tfa9887->current_devices = devices;
            /* Set amplifier mode when device changes */
            amp_set_mode(device, tfa9887->current_mode);
        }
    }

    return 0;
}

static int amp_stream_start(UNUSED amplifier_device_t *device,
        UNUSED struct audio_stream_out *stream, UNUSED bool offload)
{
    return 0;
}

static int amp_stream_standby(UNUSED amplifier_device_t *device,
        UNUSED struct audio_stream_out *stream)
{
    return 0;
}

static int amp_dev_close(hw_device_t *device)
{
    tfa9887_device_t *tfa9887 = (tfa9887_device_t *) device;

    free(tfa9887);

    return 0;
}

static int amp_module_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    if (tfa9887_dev) {
        ALOGE("%s:%d: Unable to open second instance of TFA9887 amplifier\n",
                __func__, __LINE__);
        return -EBUSY;
    }

    tfa9887_dev = calloc(1, sizeof(tfa9887_device_t));
    if (!tfa9887_dev) {
        ALOGE("%s:%d: Unable to allocate memory for amplifier device\n",
                __func__, __LINE__);
        return -ENOMEM;
    }

    tfa9887_dev->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    tfa9887_dev->amp_dev.common.module = (hw_module_t *) module;
    tfa9887_dev->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    tfa9887_dev->amp_dev.common.close = amp_dev_close;

    tfa9887_dev->amp_dev.set_devices = amp_set_devices;
    tfa9887_dev->amp_dev.set_mode = amp_set_mode;
    tfa9887_dev->amp_dev.stream_start = amp_stream_start;
    tfa9887_dev->amp_dev.stream_standby = amp_stream_standby;

    tfa9887_dev->current_devices = AUDIO_DEVICE_NONE;
    tfa9887_dev->current_mode = AUDIO_MODE_NORMAL;

    *device = (hw_device_t *) tfa9887_dev;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = amp_module_open,
};

amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "M7 audio amplifier HAL",
        .author = "The CyanogenMod Open Source Project",
        .methods = &hal_module_methods,
    },
};
