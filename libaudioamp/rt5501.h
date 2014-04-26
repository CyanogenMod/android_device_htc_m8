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

#define RT5501_DEVICE "/dev/rt5501"
#define RT5501_MAX_REG_DATA 15

struct rt5501_reg_data {
    unsigned char addr;
    unsigned char val;
};

struct rt5501_config {
    unsigned int reg_len;
    struct rt5501_reg_data reg[RT5501_MAX_REG_DATA];
};

struct rt5501_comm_data {
    unsigned int out_mode;
    struct rt5501_config config;
};

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

enum {
    RT5501_INIT = 0,
    RT5501_MUTE,
    RT5501_MAX_FUNC
};

enum RT5501_Mode {
    RT5501_MODE_OFF = RT5501_MAX_FUNC,
    RT5501_MODE_PLAYBACK,
    RT5501_MODE_PLAYBACK8OH,
    RT5501_MODE_PLAYBACK16OH,
    RT5501_MODE_PLAYBACK32OH,
    RT5501_MODE_PLAYBACK64OH,
    RT5501_MODE_PLAYBACK128OH,
    RT5501_MODE_PLAYBACK256OH,
    RT5501_MODE_PLAYBACK500OH,
    RT5501_MODE_PLAYBACK1KOH,
    RT5501_MODE_VOICE,
    RT5501_MODE_TTY,
    RT5501_MODE_FM,
    RT5501_MODE_RING,
    RT5501_MODE_MFG,
    RT5501_MODE_BEATS_8_64,
    RT5501_MODE_BEATS_128_500,
    RT5501_MODE_MONO,
    RT5501_MODE_MONO_BEATS,
    RT5501_MAX_MODE
};

enum HEADSET_OM {
    HEADSET_8OM = 0,
    HEADSET_16OM,
    HEADSET_32OM,
    HEADSET_64OM,
    HEADSET_128OM,
    HEADSET_256OM,
    HEADSET_500OM,
    HEADSET_1KOM,
    HEADSET_MONO,
    HEADSET_OM_UNDER_DETECT,
};

#define RT5501_IOCTL_MAGIC 'g'
#define RT5501_SET_CONFIG   _IOW(RT5501_IOCTL_MAGIC, 0x01,  unsigned)
#define RT5501_READ_CONFIG  _IOW(RT5501_IOCTL_MAGIC, 0x02, unsigned)
#define RT5501_SET_MODE        _IOW(RT5501_IOCTL_MAGIC, 0x03, unsigned)
#define RT5501_SET_PARAM       _IOW(RT5501_IOCTL_MAGIC, 0x04,  unsigned)
#define RT5501_WRITE_REG       _IOW(RT5501_IOCTL_MAGIC, 0x07,  unsigned)
#define RT5501_QUERY_OM       _IOW(RT5501_IOCTL_MAGIC, 0x08,  unsigned)

int rt5501_set_mode(audio_mode_t mode);
