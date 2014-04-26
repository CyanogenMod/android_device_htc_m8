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

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <system/audio.h>

//#define LOG_NDEBUG 0
#define LOG_TAG "libaudioamp"
#include <cutils/log.h>

#include "tfa9887.h"
#include "rt5501.h"
#include "audio_amplifier.h"

int mDevices = AUDIO_DEVICE_NONE;
audio_mode_t mMode = AUDIO_MODE_NORMAL;

int amplifier_open(void) {
    return 0;
}

void amplifier_set_devices(int devices) {
    if (devices != 0) {
        if (mDevices != devices) {
            mDevices = devices;
            /* Set amplifier mode when device changes */
            amplifier_set_mode(mMode);
        }
    }
}

int amplifier_set_mode(audio_mode_t mode) {
    int ret = 0;

    mMode = mode;

    if (mDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET || mDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        /* Write config for headset amplifier */
        ret = rt5501_set_mode(mode);
    } else {
        /* Write config for speaker amplifier */
        ret = tfa9887_set_mode(mode);
    }

    return ret;
}

int amplifier_close(void) {
    return 0;
}
