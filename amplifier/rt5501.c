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

#define LOG_TAG "rt5501"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <system/audio.h>

#include <cutils/log.h>

#include "rt5501.h"

static struct rt5501_reg_data rt5501_playback_data[] = {
    { 0x00, 0xC0, },
    { 0x01, 0x1B, }, // gain -1dB
    { 0x02, 0x80, }, // noise gate on
    { 0x08, 0x37, }, // noise gate on
    { 0x07, 0x7F, }, // noise gate setting
    { 0x09, 0x02, }, // noise gate setting
    { 0x0A, 0x03, }, // noise gate setting
    { 0x0B, 0xD9, }, // noise gate setting
};

static struct rt5501_reg_data rt5501_playback_128_data[] = {
    { 0x00, 0xC0, },
    { 0x01, 0x20, }, // gain 4dB
    { 0x02, 0x80, }, // noise gate on
    { 0x08, 0x37, }, // noise gate on
    { 0x07, 0x7F, }, // noise gate setting
    { 0x09, 0x02, }, // noise gate setting
    { 0x0A, 0x03, }, // noise gate setting
    { 0x0B, 0xD9, }, // noise gate setting
};

static struct rt5501_reg_data rt5501_ring_data[] = {
    { 0x00, 0xC0, },
    { 0x01, 0x1C, }, // gain 0dB
    { 0x02, 0x81, }, // noise gate on
    { 0x08, 0x01, }, // noise gate on
    { 0x07, 0x7F, }, // noise gate setting
    { 0x09, 0x01, }, // noise gate setting
    { 0x0A, 0x00, }, // noise gate setting
    { 0x0B, 0xC7, }, // noise gate setting
};

static struct rt5501_reg_data rt5501_voice_data[] = {
    { 0x00, 0xC0, },
    { 0x01, 0x1C, }, // gain 0dB
    { 0x02, 0x00, }, // noise gate off
    { 0x07, 0x7F, }, // noise gate setting
    { 0x09, 0x01, }, // noise gate setting
    { 0x0A, 0x00, }, // noise gate setting
    { 0x0B, 0xC7, }, // noise gate setting
};

int rt5501_set_mode(audio_mode_t mode) {
    struct rt5501_comm_data amp_data;
    struct rt5501_config amp_config;
    int headsetohm = HEADSET_OM_UNDER_DETECT;
    int rt5501_fd;
    int ret = -1;

    /* Open the amplifier device */
    if ((rt5501_fd = open(RT5501_DEVICE, O_RDWR)) < 0) {
        ALOGE("error opening amplifier device %s", RT5501_DEVICE);
        return -1;
    }

    /* Get impedance of headset */
    if (ioctl(rt5501_fd, RT5501_QUERY_OM, &headsetohm) < 0)
        ALOGE("error querying headset impedance");

    switch(mode) {
        case AUDIO_MODE_NORMAL:
            /* For headsets with a impedance between 128ohm and 1000ohm */
            if (headsetohm >= HEADSET_128OM && headsetohm <= HEADSET_1KOM) {
                ALOGI("Mode: Playback 128");
                amp_config.reg_len = sizeof(rt5501_playback_128_data) / sizeof(struct rt5501_reg_data);
                memcpy(&amp_config.reg, rt5501_playback_128_data, sizeof(rt5501_playback_128_data));
            } else {
                ALOGI("Mode: Playback");
                amp_config.reg_len = sizeof(rt5501_playback_data) / sizeof(struct rt5501_reg_data);
                memcpy(&amp_config.reg, rt5501_playback_data, sizeof(rt5501_playback_data));
            }
            amp_data.out_mode = RT5501_MODE_PLAYBACK;
            amp_data.config = amp_config;
            break;
        case AUDIO_MODE_RINGTONE:
            ALOGI("Mode: Ring");
            amp_config.reg_len = sizeof(rt5501_ring_data) / sizeof(struct rt5501_reg_data);
            memcpy(&amp_config.reg, rt5501_ring_data, sizeof(rt5501_ring_data));
            amp_data.out_mode = RT5501_MODE_RING;
            amp_data.config = amp_config;
            break;
        case AUDIO_MODE_IN_CALL:
        case AUDIO_MODE_IN_COMMUNICATION:
            ALOGI("Mode: Voice");
            amp_config.reg_len = sizeof(rt5501_voice_data) / sizeof(struct rt5501_reg_data);
            memcpy(&amp_config.reg, rt5501_voice_data, sizeof(rt5501_voice_data));
            amp_data.out_mode = RT5501_MODE_VOICE;
            amp_data.config = amp_config;
            break;
        default:
            ALOGI("Mode: Default");
            amp_config.reg_len = sizeof(rt5501_playback_data) / sizeof(struct rt5501_reg_data);
            memcpy(&amp_config.reg, rt5501_playback_data, sizeof(rt5501_playback_data));
            amp_data.out_mode = RT5501_MODE_PLAYBACK;
            amp_data.config = amp_config;
            break;
    }

    /* Set the selected config */
    if ((ret = ioctl(rt5501_fd, RT5501_SET_CONFIG, &amp_data)) != 0) {
        ALOGE("ioctl %d failed. ret = %d", RT5501_SET_CONFIG, ret);
    }

    close(rt5501_fd);

    return ret;
}
