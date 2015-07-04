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

#define LOG_TAG "tfa9887"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <cutils/log.h>

#include <system/audio.h>
#include <tinyalsa/asoundlib.h>

#include "tfa9887.h"

#define UNUSED __attribute__((unused))

/* Module variables */

const struct mode_config_t right_mode_configs[TFA9887_MODE_MAX] = {
    {   /* Playback */
        .config = CONFIG_TFA9887,
        .preset = PRESET_PLAYBACK_R,
        .eq = EQ_PLAYBACK_R,
        .drc = DRC_PLAYBACK_R
    },
    {   /* Ring */
        .config = CONFIG_TFA9887,
        .preset = PRESET_RING_R,
        .eq = EQ_RING_R,
        .drc = DRC_RING_R
    },
    {   /* Voice */
        .config = CONFIG_TFA9887,
        .preset = PRESET_VOICE_R,
        .eq = EQ_VOICE_R,
        .drc = DRC_VOICE_R
    },
    {   /* VOIP */
        .config = CONFIG_TFA9887,
        .preset = PRESET_VOIP_R,
        .eq = EQ_VOIP_R,
        .drc = DRC_VOIP_R
    }
};

const struct mode_config_t left_mode_configs[TFA9887_MODE_MAX] = {
    {   /* Playback */
        .config = CONFIG_TFA9887,
        .preset = PRESET_PLAYBACK_L,
        .eq = EQ_PLAYBACK_L,
        .drc = DRC_PLAYBACK_L
    },
    {   /* Ring */
        .config = CONFIG_TFA9887,
        .preset = PRESET_RING_L,
        .eq = EQ_RING_L,
        .drc = DRC_RING_L
    },
    {   /* Voice */
        .config = CONFIG_TFA9887,
        .preset = PRESET_VOICE_L,
        .eq = EQ_VOICE_L,
        .drc = DRC_VOICE_L
    },
    {   /* VOIP */
        .config = CONFIG_TFA9887,
        .preset = PRESET_VOIP_L,
        .eq = EQ_VOIP_L,
        .drc = DRC_VOIP_L
    }
};

#define AMP_RIGHT 0
#define AMP_LEFT 1
#define AMP_MAX 2
static struct tfa9887_amp_t *amps = NULL;

/* Helper functions */

static int i2s_interface_en(bool enable)
{
    enum mixer_ctl_type type;
    struct mixer_ctl *ctl;
    struct mixer *mixer = mixer_open(0);

    if (mixer == NULL) {
        ALOGE("Error opening mixer 0");
        return -1;
    }

    ctl = mixer_get_ctl_by_name(mixer, I2S_MIXER_CTL);
    if (ctl == NULL) {
        mixer_close(mixer);
        ALOGE("%s: Could not find %s\n", __func__, I2S_MIXER_CTL);
        return -ENODEV;
    }

    type = mixer_ctl_get_type(ctl);
    if (type != MIXER_CTL_TYPE_BOOL) {
        ALOGE("%s: %s is not supported\n", __func__, I2S_MIXER_CTL);
        mixer_close(mixer);
        return -ENOTTY;
    }

    mixer_ctl_set_value(ctl, 0, enable);
    mixer_close(mixer);
    return 0;
}

void * write_dummy_data(void *param)
{
    struct tfa9887_amp_t *amp = (struct tfa9887_amp_t *) param;
    uint8_t *buffer;
    int size;
    struct pcm *pcm;
    struct pcm_config config = {
        .channels = 2,
        .rate = 48000,
        .period_size = 960,
        .period_count = 8,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = 960 / 4,
        .stop_threshold = INT_MAX,
        .avail_min = 960 / 4,
    };

    if (i2s_interface_en(true)) {
        ALOGE("%s: Failed to enable I2S interface\n", __func__);
        return NULL;
    }

    pcm = pcm_open(0, 0, PCM_OUT, &config);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("pcm_open failed: %s", pcm_get_error(pcm));
        if (pcm) {
            goto err_close_pcm;
        }
        goto err_disable_i2s;
    }

    size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));
    buffer = calloc(1, size);
    if (!buffer) {
        ALOGE("%s: failed to allocate buffer", __func__);
        goto err_close_pcm;
    }

    do {
        if (pcm_write(pcm, buffer, size)) {
            ALOGE("%s: pcm_write failed", __func__);
        }
        pthread_mutex_lock(&amp->mutex);
        amp->writing = true;
        pthread_cond_signal(&amp->cond);
        pthread_mutex_unlock(&amp->mutex);
    } while (amp->initializing);

err_free:
    free(buffer);
err_close_pcm:
    pcm_close(pcm);
err_disable_i2s:
    i2s_interface_en(false);
    return NULL;
}

static uint32_t get_mode(audio_mode_t mode)
{
    switch (mode) {
        case AUDIO_MODE_RINGTONE:
            return TFA9887_MODE_RING;
        case AUDIO_MODE_IN_CALL:
            return TFA9887_MODE_VOICE;
        case AUDIO_MODE_IN_COMMUNICATION:
            return TFA9887_MODE_VOIP;
        case AUDIO_MODE_NORMAL:
        default:
            return TFA9887_MODE_PLAYBACK;
    }
}

static int read_file(const char *file_name, uint8_t *buf, int sz, int seek)
{
    int ret;
    int fd;

    fd = open(file_name, O_RDONLY);
    if (fd < 0) {
        ret = errno;
        ALOGE("%s: unable to open file %s: %d", __func__, file_name, ret);
        return ret;
    }

    lseek(fd, seek, SEEK_SET);

    ret = read(fd, buf, sz);
    if (ret < 0) {
        ret = errno;
        ALOGE("%s: error reading from file %s: %d", __func__, file_name, ret);
    }

    close(fd);
    return ret;
}

static void bytes2data(const uint8_t bytes[], int num_bytes,
        uint32_t data[])
{
    int i; /* index for data */
    int k; /* index for bytes */
    uint32_t d;
    int num_data = num_bytes / 3;

    for (i = 0, k = 0; i < num_data; i++, k += 3) {
        d = (bytes[k] << 16) | (bytes[k + 1] << 8) | (bytes[k + 2]);
        /* sign bit was set*/
        if (bytes[k] & 0x80) {
            d = - ((1 << 24) - d);
        }
        data[i] = d;
    }
}

static void data2bytes(const int32_t data[], int num_data, uint8_t bytes[])
{
    int i; /* index for data */
    int k; /* index for bytes */
    uint32_t d;

    for (i = 0, k = 0; i < num_data; i++, k += 3) {
        if (data[i] >= 0) {
            d = MIN(data[i], (1 << 23) - 1);
        } else {
            d = (1 << 24) - MIN(-data[i], 1 << 23);
        }
        bytes[k] = (d >> 16) & 0xff;
        bytes[k + 1] = (d >> 8) & 0xff;
        bytes[k + 2] = d & 0xff;
    }
}

/* TFA9887 helper functions */

static int tfa9887_read_reg(struct tfa9887_amp_t *amp, uint8_t reg,
        uint16_t *val)
{
    int ret = 0;
    /* kernel uses unsigned int */
    unsigned int reg_val[2];
    uint8_t buf[2];

    if (!amp) {
        return -ENODEV;
    }

    reg_val[0] = 2;
    reg_val[1] = (unsigned int) &buf;
    /* unsure why the first byte is skipped */
    buf[0] = 0;
    buf[1] = reg;
    if ((ret = ioctl(amp->fd, TPA9887_WRITE_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_WRITE_CONFIG, ret);
        goto read_reg_err;
    }

    reg_val[0] = 2;
    reg_val[1] = (unsigned int) &buf;
    if ((ret = ioctl(amp->fd, TPA9887_READ_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_READ_CONFIG, ret);
        goto read_reg_err;
    }

    *val = ((buf[0] << 8) | buf[1]);

read_reg_err:
    return ret;
}

static int tfa9887_write_reg(struct tfa9887_amp_t *amp, uint8_t reg,
        uint16_t val)
{
    int ret = 0;
    /* kernel uses unsigned int */
    unsigned int reg_val[2];
    uint8_t buf[4];

    if (!amp) {
        return -ENODEV;
    }

    reg_val[0] = 4;
    reg_val[1] = (unsigned int) &buf;
    /* unsure why the first byte is skipped */
    buf[0] = 0;
    buf[1] = reg;
    buf[2] = (0xFF00 & val) >> 8;
    buf[3] = (0x00FF & val);
    if ((ret = ioctl(amp->fd, TPA9887_WRITE_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_WRITE_CONFIG, ret);
        goto write_reg_err;
    }

write_reg_err:
    return ret;
}

static int tfa9887_read(struct tfa9887_amp_t *amp, int addr, uint8_t *buf,
        int len)
{
    int ret = 0;
    uint8_t reg_buf[2];
    /* kernel uses unsigned int */
    unsigned int reg_val[2];
    uint8_t kernel_buf[len];

    if (!amp) {
        return -ENODEV;
    }

    reg_val[0] = 2;
    reg_val[1] = (unsigned int) &reg_buf;
    /* unsure why the first byte is skipped */
    reg_buf[0] = 0;
    reg_buf[1] = (0xFF & addr);
    if ((ret = ioctl(amp->fd, TPA9887_WRITE_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_WRITE_CONFIG, ret);
        goto read_err;
    }

    reg_val[0] = len;
    reg_val[1] = (unsigned int) &kernel_buf;
    if ((ret = ioctl(amp->fd, TPA9887_READ_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_READ_CONFIG, ret);
        goto read_err;
    }
    memcpy(buf, kernel_buf, len);

read_err:
    return ret;
}

static int tfa9887_write(struct tfa9887_amp_t *amp, int addr,
        const uint8_t *buf, int len)
{
    int ret = 0;
    /* kernel uses unsigned int */
    unsigned int reg_val[2];
    uint8_t ioctl_buf[len + 2];

    if (!amp) {
        return -ENODEV;
    }

    reg_val[0] = len + 2;
    reg_val[1] = (unsigned int) &ioctl_buf;
    /* unsure why the first byte is skipped */
    ioctl_buf[0] = 0;
    ioctl_buf[1] = (0xFF & addr);
    memcpy(ioctl_buf + 2, buf, len);

    if ((ret = ioctl(amp->fd, TPA9887_WRITE_CONFIG, &reg_val)) != 0) {
        ALOGE("ioctl %d failed, ret = %d", TPA9887_WRITE_CONFIG, ret);
        goto write_err;
    }

write_err:
    return ret;
}

static int tfa9887_read_mem(struct tfa9887_amp_t *amp, uint16_t start_offset,
        int num_words, uint32_t *p_values)
{
    uint16_t cf_ctrl; /* the value to sent to the CF_CONTROLS register */
    uint8_t bytes[MAX_I2C_LENGTH];
    int burst_size; /* number of words per burst size */
    int bytes_per_word = 3;
    int num_bytes;
    uint32_t *p;
    int error;

    if (!amp) {
        return -ENODEV;
    }

    /* first set DMEM and AIF, leaving other bits intact */
    error = tfa9887_read_reg(amp, TFA9887_CF_CONTROLS, &cf_ctrl);
    if (error != 0) {
        ALOGE("%s: error reading from register %d",
                __func__, TFA9887_CF_CONTROLS);
        goto read_mem_err;
    }
    cf_ctrl &= ~0x000E; /* clear AIF & DMEM */
    cf_ctrl |= (TFA9887_DMEM_XMEM << 1); /* set DMEM, leave AIF cleared for autoincrement */
    error = tfa9887_write_reg(amp, TFA9887_CF_CONTROLS, cf_ctrl);
    if (error != 0) {
        ALOGE("%s: error writing to register %d",
                __func__, TFA9887_CF_CONTROLS);
        goto read_mem_err;
    }
    error = tfa9887_write_reg(amp, TFA9887_CF_MAD, start_offset);
    if (error != 0) {
        ALOGE("%s: error writing to register %d", __func__, TFA9887_CF_MAD);
        goto read_mem_err;
    }
    num_bytes = num_words * bytes_per_word;
    p = p_values;
    for (; num_bytes > 0; ) {
        burst_size = ROUND_DOWN(MAX_I2C_LENGTH, bytes_per_word);
        if (num_bytes < burst_size) {
            burst_size = num_bytes;
        }
        error = tfa9887_read(amp, TFA9887_CF_MEM, bytes, burst_size);
        if (error != 0) {
            ALOGE("%s: error reading from %d", __func__, TFA9887_CF_MEM);
            goto read_mem_err;
        }
        bytes2data(bytes, burst_size,  p);
        num_bytes -= burst_size;
        p += burst_size / bytes_per_word;
    }

read_mem_err:
    return error;
}

/* Hardware functions */

static int tfa9887_load_patch(struct tfa9887_amp_t *amp, const char *file_name)
{
    int error;
    int size;
    int index = 0;
    int length;
    uint8_t bytes[MAX_PATCH_SIZE];
    uint8_t buffer[MAX_I2C_LENGTH];
    uint32_t value = 0;
    uint16_t status;

    if (!amp) {
        return -ENODEV;
    }

    length = read_file(file_name, bytes, MAX_PATCH_SIZE, PATCH_HEADER_LENGTH);
    if (length < 0) {
        ALOGE("Unable to read patch file");
        return -EIO;
    }

    error = tfa9887_read_reg(amp, TFA9887_STATUS, &status);
    if (error != 0) {
        if ((status & 0x0043) != 0x0043) {
            /* one of Vddd, PLL and clocks not ok */
            error = -1;
        }
    }
    ALOGI("tfa9887 status %u", status);
    error = tfa9887_read_mem(amp, 0x2210, 1, &value);
    ALOGI("tfa9887 version %x", value);
    while (index < length) {
        /* extract little endian length */
        size = bytes[index] + bytes[index+1] * 256;
        index += 2;
        if ( (index + size) > length) {
            /* outside the buffer, error in the input data */
            return -1;
        }
        memcpy(buffer, bytes + index, size);
        error = tfa9887_write(amp, buffer[0],
                &buffer[1], size - 1);
        ALOGV("%d %d", buffer[0], size - 1);
        if (error != 0) {
            ALOGE("error");
            break;
        }
        index += size;
    }
    return error;
}

static int tfa9887_load_data(struct tfa9887_amp_t *amp, int param_id,
        int module_id, uint8_t *data, int num_bytes)
{
    int error;
    uint16_t cf_ctrl = 0x0002; /* the value to be sent to the CF_CONTROLS register: cf_req=00000000, cf_int=0, cf_aif=0, cf_dmem=XMEM=01, cf_rst_dsp=0 */
    uint16_t cf_mad = 0x0001; /* memory address to be accessed (0 : Status, 1 : ID, 2 : parameters) */
    uint16_t cf_status; /* the contents of the CF_STATUS register */
    uint8_t mem[3];
    uint8_t id[3];
    uint32_t rpc_status = 0;
    int tries = 0;

    if (!amp) {
        return -ENODEV;
    }

    error = tfa9887_write_reg(amp, TFA9887_CF_CONTROLS, cf_ctrl);
    if (error == 0) {
        error = tfa9887_write_reg(amp, TFA9887_CF_MAD, cf_mad);
    }
    if (error == 0) {
        id[0] = 0;
        id[1] = module_id + 128;
        id[2] = param_id;
        error = tfa9887_write(amp, TFA9887_CF_MEM, id, 3);
    }

    error = tfa9887_write(amp, TFA9887_CF_MEM, data, num_bytes);
    if (error == 0) {
        cf_ctrl |= (1 << 8) | (1 << 4); /* set the cf_req1 and cf_int bit */
        error = tfa9887_write_reg(amp, TFA9887_CF_CONTROLS, cf_ctrl);
        do {
            error = tfa9887_read_reg(amp, TFA9887_CF_STATUS, &cf_status);
            tries++;
            usleep(1000);
            /* don't wait forever, DSP is pretty quick to respond (< 1ms) */
        } while ((error == 0) &&
                ((cf_status & 0x0100) == 0) &&
                (tries < 100));

        if (tries >= 100) {
            /* something wrong with communication with DSP */
            ALOGE("%s: Timed out waiting for status", __func__);
            error = -1;
        }
    }
    cf_ctrl = 0x0002;
    cf_mad = 0x0000;
    if (error == 0) {
        error = tfa9887_write_reg(amp, TFA9887_CF_CONTROLS, cf_ctrl);
    }
    if (error == 0) {
        error = tfa9887_write_reg(amp, TFA9887_CF_MAD, cf_mad);
    }
    tries = 0;
    if (error == 0) {
        /* wait for mem to come to stable state */
        do {
            error = tfa9887_read(amp, TFA9887_CF_MEM, mem, 3);
            rpc_status = (int)((mem[0] << 16) | (mem[1] << 8) | mem[2]);
            tries++;
            usleep(1000);
        } while (rpc_status != 0 && tries < 100);
    }
    if (error == 0) {
        if (rpc_status != STATUS_OK) {
            error = rpc_status + 100;
            ALOGE("RPC rpc_status: %d", error);
        }
    }
    return error;
}

static int tfa9887_load_dsp(struct tfa9887_amp_t *amp, const char *param_file)
{
    int param_id, module_id;
    uint8_t data[MAX_PARAM_SIZE];
    int num_bytes, error;
    char *suffix;

    suffix = strrchr(param_file, '.');
    if (suffix == NULL) {
        ALOGE("%s: Failed to determine parameter file type", __func__);
        return -EINVAL;
    } else if (strcmp(suffix, ".speaker") == 0) {
        param_id = PARAM_SET_LSMODEL;
        module_id = MODULE_SPEAKERBOOST;
    } else if (strcmp(suffix, ".config") == 0) {
        param_id = PARAM_SET_CONFIG;
        module_id = MODULE_SPEAKERBOOST;
    } else if (strcmp(suffix, ".preset") == 0) {
        param_id = PARAM_SET_PRESET;
        module_id = MODULE_SPEAKERBOOST;
    } else if (strcmp(suffix, ".drc") == 0) {
        param_id = PARAM_SET_DRC;
        module_id = MODULE_SPEAKERBOOST;
    } else {
        ALOGE("%s: Invalid DSP param file %s", __func__, param_file);
        return -EINVAL;
    }

    num_bytes = read_file(param_file, data, MAX_PARAM_SIZE, 0);
    if (num_bytes < 0) {
        error = num_bytes;
        ALOGE("%s: Failed to load file %s: %d", __func__, param_file, error);
        return -EIO;
    }

    return tfa9887_load_data(amp, param_id, module_id, data, num_bytes);
}

static int tfa9887_load_eq(struct tfa9887_amp_t *amp, const char *eq_file)
{
    uint8_t data[MAX_EQ_SIZE];
    const float disabled[5] = { 1.0, 0.0, 0.0, 0.0, 0.0 };
    float line[5];
    int32_t line_data[6];
    float max;
    FILE *f;
    int i, j;
    int idx, space, rc;

    memset(data, 0, MAX_EQ_SIZE);

    f = fopen(eq_file, "r");
    if (!f) {
        rc = errno;
        ALOGE("%s: Unable to open file %s: %d", __func__, eq_file, rc);
        return -EIO;
    }

    for (i = 0; i < MAX_EQ_LINES; i++) {
        rc = fscanf(f, "%d %f %f %f %f %f", &idx, &line[0], &line[1],
                &line[2], &line[3], &line[4]);
        if (rc != 6) {
            ALOGE("%s: %s has bad format: line must be 6 values\n",
                    __func__, eq_file);
            fclose(f);
            return -EINVAL;
        }

        if (idx != i + 1) {
            ALOGE("%s: %s has bad format: index mismatch\n",
                    __func__, eq_file);
            fclose(f);
            return -EINVAL;
        }

        if (!memcmp(disabled, line, 5)) {
            /* skip */
            continue;
        } else {
            max = (float) fabs(line[0]);
            /* Find the max */
            for (j = 1; j < 5; j++) {
                if (fabs(line[j]) > max) {
                    max = (float) fabs(line[j]);
                }
            }
            space = (int) ceil(log(max + pow(2.0, -23)) / log(2.0));
            if (space > 8) {
                fclose(f);
                ALOGE("%s: Invalid value encountered\n", __func__);
                return -EINVAL;
            }
            if (space < 0) {
                space = 0;
            }

            /* Pack line into bytes */
            line_data[0] = space;
            line_data[1] = (int32_t) (-line[4] * (1 << (23 - space)));
            line_data[2] = (int32_t) (-line[3] * (1 << (23 - space)));
            line_data[3] = (int32_t) (line[2] * (1 << (23 - space)));
            line_data[4] = (int32_t) (line[1] * (1 << (23 - space)));
            line_data[5] = (int32_t) (line[0] * (1 << (23 - space)));
            data2bytes(line_data, MAX_EQ_LINE_SIZE,
                    &data[i * MAX_EQ_LINE_SIZE * MAX_EQ_ITEM_SIZE]);
        }
    }

    fclose(f);

    return tfa9887_load_data(amp, PARAM_SET_EQ, MODULE_BIQUADFILTERBANK, data,
            MAX_EQ_SIZE);
}

static int htc_init9887(struct tfa9887_amp_t *amp)
{
    int error;
    uint16_t value;

    error = tfa9887_read_reg(amp, 0x7, &value);
    if (error != 0) {
        ALOGE("%s: right: unable to read 0x7", __func__);
        goto htc_err;
    }

    error = tfa9887_write_reg(amp, 0x7, 0x5);
    if (error != 0) {
        ALOGE("%s: right: unable to write 0x7", __func__);
        goto htc_err;
    }

    error = tfa9887_read_reg(amp, 0x9, &value);
    if (error != 0) {
        ALOGE("%s: right: unable to read 0x9", __func__);
        goto htc_err;
    }

    error = tfa9887_write_reg(amp, 0x9, value | 0x200);
    if (error != 0) {
        ALOGE("%s: right: unable to write 0x9", __func__);
        goto htc_err;
    }

    error = tfa9887_read_reg(amp, 0xa, &value);
    if (error != 0) {
        ALOGE("%s: right: unable to read 0xa", __func__);
        goto htc_err;
    }

    value = value & 0xf9fe;
    error = tfa9887_write_reg(amp, 0xa, value | 0x10);
    if (error != 0) {
        ALOGE("%s: right: unable to write 0xa", __func__);
        goto htc_err;
    }

    error = tfa9887_read_reg(amp, 0x7, &value);
    if (error != 0) {
        ALOGE("%s: left: unable to read 0x7", __func__);
        goto htc_err;
    }

    error = tfa9887_write_reg(amp, 0x7, value | 6);
    if (error != 0) {
        ALOGE("%s: left: unable to write 0x7", __func__);
        goto htc_err;
    }

    error = tfa9887_read_reg(amp, 0x9, &value);
    if (error != 0) {
        ALOGE("%s: left: unable to read 0x9", __func__);
        goto htc_err;
    }

    error = tfa9887_write_reg(amp, 0x9, value | 0x200);
    if (error != 0) {
        ALOGE("%s: left: unable to write 0x9", __func__);
        goto htc_err;
    }

    error = tfa9887_read_reg(amp, 0xa, &value);
    if (error != 0) {
        ALOGE("%s: left: unable to read 0xa", __func__);
        goto htc_err;
    }

    value = value & 0xf9fe;
    error = tfa9887_write_reg(amp, 0x9, value | 0x10);
    if (error != 0) {
        ALOGE("%s: left: unable to write 0x9", __func__);
        goto htc_err;
    }

htc_err:
    return error;
}

static int tfa9887_hw_power(struct tfa9887_amp_t *amp, bool on)
{
    int error;
    uint16_t value;

    if (amp->is_on == on) {
        ALOGV("Already powered on\n");
        return 0;
    }

    error = tfa9887_read_reg(amp, TFA9887_SYSTEM_CONTROL, &value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_SYSTEM_CONTROL");
        goto power_err;
    }

    // get powerdown bit
    value &= ~(TFA9887_SYSCTRL_POWERDOWN);
    if (!on) {
        value |= TFA9887_SYSCTRL_POWERDOWN;
    }

    error = tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, value);
    if (error != 0) {
        ALOGE("Unable to write TFA9887_SYSTEM_CONTROL");
    }

    if (!on) {
        usleep(1000);
    }

    amp->is_on = on;

power_err:
    return error;
}

static int tfa9887_set_volume(struct tfa9887_amp_t *amp, float volume)
{
    int error;
    uint16_t value;
    uint8_t volume_int;

    if (!amp) {
        return -ENODEV;
    }

    if (volume > 0.0) {
        return -1;
    }

    error = tfa9887_read_reg(amp, TFA9887_AUDIO_CONTROL, &value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_AUDIO_CONTROL");
        goto set_vol_err;
    }

    volume = -2.0 * volume;
    volume_int = (((uint8_t) volume) & 0xFF);

    value = ((value & 0x00FF) | (volume_int << 8));
    error = tfa9887_write_reg(amp, TFA9887_AUDIO_CONTROL, value);
    if (error != 0) {
        ALOGE("Unable to write to TFA9887_AUDIO_CONTROL");
        goto set_vol_err;
    }

set_vol_err:
    return error;
}

static int tfa9887_mute(struct tfa9887_amp_t *amp, uint32_t mute)
{
    int error;
    uint16_t aud_value, sys_value;

    error = tfa9887_read_reg(amp, TFA9887_AUDIO_CONTROL, &aud_value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_AUDIO_CONTROL");
        goto mute_err;
    }
    error = tfa9887_read_reg(amp, TFA9887_SYSTEM_CONTROL, &sys_value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_SYSTEM_CONTROL");
        goto mute_err;
    }

    switch (mute) {
        case TFA9887_MUTE_OFF:
            /* clear CTRL_MUTE, set ENBL_AMP, mute none */
            aud_value &= ~(TFA9887_AUDIOCTRL_MUTE);
            sys_value |= TFA9887_SYSCTRL_ENBL_AMP;
            break;
        case TFA9887_MUTE_DIGITAL:
            /* set CTRL_MUTE, set ENBL_AMP, mute ctrl */
            aud_value |= TFA9887_AUDIOCTRL_MUTE;
            sys_value |= TFA9887_SYSCTRL_ENBL_AMP;
            break;
        case TFA9887_MUTE_AMPLIFIER:
            /* clear CTRL_MUTE, clear ENBL_AMP, only mute amp */
            aud_value &= ~(TFA9887_AUDIOCTRL_MUTE);
            sys_value &= ~(TFA9887_SYSCTRL_ENBL_AMP);
            break;
        default:
            error = -1;
            ALOGW("Unknown mute type: %d", mute);
            goto mute_err;
    }

    error = tfa9887_write_reg(amp, TFA9887_AUDIO_CONTROL, aud_value);
    if (error != 0) {
        ALOGE("Unable to write TFA9887_AUDIO_CONTROL");
        goto mute_err;
    }
    error = tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, sys_value);
    if (error != 0) {
        ALOGE("Unable to write TFA9887_SYSTEM_CONTROL");
        goto mute_err;
    }

mute_err:
    return error;
}

static int tfa9887_select_input(struct tfa9887_amp_t *amp, int input)
{
    int error;
    uint16_t value;

    if (!amp) {
        return -ENODEV;
    }

    error = tfa9887_read_reg(amp, TFA9887_I2S_CONTROL, &value);
    if (error != 0) {
        goto select_amp_err;
    }

    // clear 2 bits
    value &= ~(0x3 << TFA9887_I2SCTRL_INPUT_SEL_SHIFT);

    switch (input) {
        case 1:
            value |= 0x40;
            break;
        case 2:
            value |= 0x80;
            break;
        default:
            ALOGW("Invalid input selected: %d",
                    input);
            error = -1;
            goto select_amp_err;
    }
    error = tfa9887_write_reg(amp, TFA9887_I2S_CONTROL, value);

select_amp_err:
    return error;
}

static int tfa9887_select_channel(struct tfa9887_amp_t *amp, int channels)
{
    int error;
    uint16_t value;

    if (!amp) {
        return -ENODEV;
    }

    error = tfa9887_read_reg(amp, TFA9887_I2S_CONTROL, &value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_I2S_CONTROL");
        goto select_channel_err;
    }

    // clear the 2 bits first
    value &= ~(0x3 << TFA9887_I2SCTRL_CHANSEL_SHIFT);

    switch (channels) {
        case 0:
            value |= 0x8;
            break;
        case 1:
            value |= 0x10;
            break;
        case 2:
            value |= 0x18;
            break;
        default:
            ALOGW("Too many channels requested: %d",
                    channels);
            error = -1;
            goto select_channel_err;
    }
    error = tfa9887_write_reg(amp, TFA9887_I2S_CONTROL, value);
    if (error != 0) {
        ALOGE("Unable to write to TFA9887_I2S_CONTROL");
        goto select_channel_err;
    }

select_channel_err:
    return error;
}

static int tfa9887_set_sample_rate(struct tfa9887_amp_t *amp, int sample_rate)
{
    int error;
    uint16_t value;

    if (!amp) {
        return -ENODEV;
    }

    error = tfa9887_read_reg(amp, TFA9887_I2S_CONTROL, &value);
    if (error == 0) {
        // clear the 4 bits first
        value &= (~(0xF << TFA9887_I2SCTRL_RATE_SHIFT));
        switch (sample_rate) {
            case 48000:
                value |= TFA9887_I2SCTRL_RATE_48000;
                break;
            case 44100:
                value |= TFA9887_I2SCTRL_RATE_44100;
                break;
            case 32000:
                value |= TFA9887_I2SCTRL_RATE_32000;
                break;
            case 24000:
                value |= TFA9887_I2SCTRL_RATE_24000;
                break;
            case 22050:
                value |= TFA9887_I2SCTRL_RATE_22050;
                break;
            case 16000:
                value |= TFA9887_I2SCTRL_RATE_16000;
                break;
            case 12000:
                value |= TFA9887_I2SCTRL_RATE_12000;
                break;
            case 11025:
                value |= TFA9887_I2SCTRL_RATE_11025;
                break;
            case 8000:
                value |= TFA9887_I2SCTRL_RATE_08000;
                break;
            default:
                ALOGE("Unsupported sample rate %d", sample_rate);
                error = -1;
                return error;
        }
        error = tfa9887_write_reg(amp, TFA9887_I2S_CONTROL, value);
    }

    return error;
}

static int tfa9887_wait_ready(struct tfa9887_amp_t *amp,
        uint32_t ready_bits, uint32_t ready_state)
{
    int error;
    uint16_t value;
    int tries;
    bool ready;

    if (!amp) {
        return -ENODEV;
    }

    tries = 0;
    do {
        error = tfa9887_read_reg(amp, TFA9887_STATUS, &value);
        ready = (error == 0 &&
                (value & ready_bits) == ready_state);
        ALOGV("Waiting for 0x%04x, current state: 0x%04x",
                ready_state, value);
        tries++;
        usleep(1000);
    } while (!ready && tries < 10);

    if (tries >= 10) {
        ALOGE("Timed out waiting for tfa9887 to become ready");
        error = -1;
    }

    return error;
}

static int tfa9887_set_configured(struct tfa9887_amp_t *amp)
{
    int error;
    uint16_t value;

    if (!amp) {
        return -ENODEV;
    }

    error = tfa9887_read_reg(amp, TFA9887_SYSTEM_CONTROL, &value);
    if (error != 0) {
        ALOGE("Unable to read from TFA9887_SYSTEM_CONTROL");
        goto set_conf_err;
    }
    value |= TFA9887_SYSCTRL_CONFIGURED;
    tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, value);
    if (error != 0) {
        ALOGE("Unable to write TFA9887_SYSTEM_CONTROL");
    }

set_conf_err:
    return error;
}

static int tfa9887_startup(struct tfa9887_amp_t *amp)
{
    int error;
    uint16_t value;
    uint16_t value2 = 0x12;

    if (!amp) {
        return -ENODEV;
    }

    tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, 0x2);

    tfa9887_read_reg(amp, 0x8, &value2);
    if ( value2 & 0x400 ) {
        tfa9887_write_reg(amp, 0x8, value2 & 0xFBFF);
        tfa9887_read_reg(amp, 0x8, &value2);
        tfa9887_write_reg(amp, 0x8, value2);
    }

    error = tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, 0x2);
    if (0 == error) {
        error = tfa9887_read_reg(amp, TFA9887_SYSTEM_CONTROL, &value);
    }
    if (0 == error) {
        /* DSP must be in control of the amplifier to avoid plops */
        value |= TFA9887_SYSCTRL_SEL_ENBL_AMP;
        error = tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, value);
    }

    /* some other registers must be set for optimal amplifier behaviour */
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_BAT_PROT, 0x13AB);
    }
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_AUDIO_CONTROL, 0x1F);
    }
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_SPKR_CALIBRATION, 0x3C4E);
    }
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_SYSTEM_CONTROL, 0x24D);
    }
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_PWM_CONTROL, 0x308);
    }
    if (0 == error) {
        error = tfa9887_write_reg(amp, TFA9887_CURRENTSENSE4, 0xE82);
    }

    return error;
}

static int tfa9887_hw_init(struct tfa9887_amp_t *amp, int sample_rate)
{
    int error;
    const char *patch_file, *speaker_file;
    int channel;
    uint32_t pll_lock_bits = (TFA9887_STATUS_CLKS | TFA9887_STATUS_PLLS);

    if (!amp) {
        return -ENODEV;
    }

    if (amp->is_right) {
        channel = 1;
        patch_file = PATCH_TFA9887;
        speaker_file = SPKR_R;
    } else {
        channel = 0;
        patch_file = PATCH_TFA9887;
        speaker_file = SPKR_L;
    }

    /* must wait until chip is ready otherwise no init happens */
    error = tfa9887_wait_ready(amp, TFA9887_STATUS_MTPB, 0);
    if (error != 0) {
        ALOGE("tfa9887 MTP still busy");
        goto priv_init_err;
    }

    /* do cold boot init */
    error = tfa9887_startup(amp);
    if (error != 0) {
        ALOGE("Unable to cold boot");
        goto priv_init_err;
    }
    error = htc_init9887(amp);
    if (error != 0) {
        ALOGE("Failed to execute HTC amp init");
        goto priv_init_err;
    }
    error = tfa9887_set_sample_rate(amp, sample_rate);
    if (error != 0) {
        ALOGE("Unable to set sample rate");
        goto priv_init_err;
    }
    error = tfa9887_select_channel(amp, channel);
    if (error != 0) {
        ALOGE("Unable to select channel");
        goto priv_init_err;
    }
    error = tfa9887_select_input(amp, 2);
    if (error != 0) {
        ALOGE("Unable to select input");
        goto priv_init_err;
    }
    error = tfa9887_set_volume(amp, 0.0);
    if (error != 0) {
        ALOGE("Unable to set volume");
        goto priv_init_err;
    }
    error = tfa9887_hw_power(amp, true);
    if (error != 0) {
        ALOGE("Unable to power up");
        goto priv_init_err;
    }

    /* wait for ready */
    error = tfa9887_wait_ready(amp, pll_lock_bits, pll_lock_bits);
    if (error != 0) {
        ALOGE("Failed to lock PLLs");
        goto priv_init_err;
    }

    /* load firmware */
    error = tfa9887_load_patch(amp, patch_file);
    if (error != 0) {
        ALOGE("Unable to load patch data");
        goto priv_init_err;
    }
    error = tfa9887_load_dsp(amp, speaker_file);
    if (error != 0) {
        ALOGE("Unable to load speaker data");
        goto priv_init_err;
    }

    ALOGI("%s: Initialized hardware", __func__);

priv_init_err:
    return error;
}

static int tfa9887_set_dsp_mode(struct tfa9887_amp_t *amp, uint32_t mode)
{
    int error;
    const struct mode_config_t *config;

    if (!amp) {
        return -ENODEV;
    }

    if (amp->is_right) {
        config = right_mode_configs;
        ALOGV("Setting right mode to %d", mode);
    } else {
        config = left_mode_configs;
        ALOGV("Setting left mode to %d", mode);
    }

    error = tfa9887_load_dsp(amp, config[mode].config);
    if (error != 0) {
        ALOGE("Unable to load config data");
        goto set_dsp_err;
    }
    error = tfa9887_load_dsp(amp, config[mode].preset);
    if (error != 0) {
        ALOGE("Unable to load preset data");
        goto set_dsp_err;
    }
    error = tfa9887_load_eq(amp, config[mode].eq);
    if (error != 0) {
        ALOGE("Unable to load EQ data");
        goto set_dsp_err;
    }

    error = tfa9887_load_dsp(amp, config[mode].drc);
    if (error != 0) {
        ALOGE("Unable to load DRC data");
        goto set_dsp_err;
    }

    error = tfa9887_set_configured(amp);
    if (error != 0) {
        ALOGE("Unable to set configured");
        goto set_dsp_err;
    }

    /* wait for ready */
    error = tfa9887_wait_ready(amp, TFA9887_STATUS_MTPB, 0);
    if (error != 0) {
        ALOGE("Failed to become ready");
        goto set_dsp_err;
    }

    ALOGI("%s: Set %s DSP mode to %d\n",
            __func__, amp->is_right ? "right" : "left", mode);

set_dsp_err:
    return error;
}

static int tfa9887_lock(struct tfa9887_amp_t *amp, bool lock)
{
    int rc;
    uint32_t reg_value[2];

    if (!amp) {
        return -ENODEV;
    }

    if (amp->fd < 0) {
        return -ENODEV;
    }

    reg_value[0] = 1;
    reg_value[1] = lock ? 1 : 0;
    rc = ioctl(amp->fd, TPA9887_KERNEL_LOCK, &reg_value);
    if (rc) {
        rc = -errno;
        ALOGE("%s: Failed to lock amplifier: %d\n",
                __func__, rc);
        return rc;
    }

    return rc;
}

static int tfa9887_enable_dsp(struct tfa9887_amp_t *amp, bool enable)
{
    int rc;
    uint32_t reg_value[2];

    if (!amp) {
        return -ENODEV;
    }

    if (amp->fd < 0) {
        return -ENODEV;
    }

    reg_value[0] = 1;
    reg_value[1] = enable ? 1 : 0;
    rc = ioctl(amp->fd, TPA9887_ENABLE_DSP, &reg_value);
    if (rc) {
        rc = -errno;
        ALOGE("%s: Failed to enable DSP mode: %d\n",
                __func__, rc);
        return rc;
    }

    ALOGI("%s: Set %s DSP enable to %d\n",
            __func__, amp->is_right ? "right" : "left", enable);

    return rc;
}

static int tfa9887_init(struct tfa9887_amp_t *amp, bool is_right)
{
    int rc;

    if (!amp) {
        return -ENODEV;
    }

    memset(amp, 0, sizeof(struct tfa9887_amp_t));

    amp->is_right = is_right;
    amp->is_on = false;
    amp->mode = TFA9887_MODE_PLAYBACK;
    amp->initializing = true;
    amp->writing = false;
    pthread_mutex_init(&amp->mutex, NULL);
    pthread_cond_init(&amp->cond, NULL);

    amp->fd = open(amp->is_right ? TFA9887_DEVICE : TFA9887L_DEVICE, O_RDWR);
    if (amp->fd < 0) {
        rc = -errno;
        ALOGE("%s: Failed to open %s amplifier device: %d\n",
                __func__, amp->is_right ? "right" : "left", rc);
        return rc;
    }

    return 0;
}

/* Public functions */

int tfa9887_open(void)
{
    int rc;
    int i;
    struct tfa9887_amp_t *amp = NULL;

    if (amps) {
        ALOGE("%s: TFA9887 already opened\n", __func__);
        return -EBUSY;
    }

    amps = calloc(AMP_MAX, sizeof(struct tfa9887_amp_t));
    if (!amps) {
        ALOGE("%s: Failed to allocate TFA9887 amplifier device memory", __func__);
        return -ENOMEM;
    }

    for (i = 0; i < AMP_MAX; i++) {
        amp = &amps[i];
        rc = tfa9887_init(amp, (i == AMP_RIGHT));
        if (rc) {
            /* Try next amp */
            continue;
        }
        /* Open I2S interface while DSP ops are occurring */
        pthread_create(&amp->write_thread, NULL, write_dummy_data, amp);
        pthread_mutex_lock(&amp->mutex);
        while (!amp->writing) {
            pthread_cond_wait(&amp->cond, &amp->mutex);
        }
        pthread_mutex_unlock(&amp->mutex);

        rc = tfa9887_hw_init(amp, TFA9887_DEFAULT_RATE);
        rc = tfa9887_set_dsp_mode(amp, amp->mode);
        if (rc == 0) {
            rc = tfa9887_enable_dsp(amp, true);
        }

        /* Shut down I2S interface */
        amp->initializing = false;
        pthread_join(amp->write_thread, NULL);
        /* Remember to power off, since we powered on in hw_init */
        tfa9887_hw_power(amp, false);
    }

    return 0;
}

int tfa9887_power(bool on)
{
    int i, rc;
    struct tfa9887_amp_t *amp = NULL;

    if (!amps) {
        ALOGE("%s: TFA9887 not open!\n", __func__);
    }

    for (i = 0; i < AMP_MAX; i++) {
        amp = &amps[i];
        rc = tfa9887_hw_power(amp, on);
        if (rc) {
            ALOGE("Unable to power on %s amp: %d\n",
                    amp->is_right? "right" : "left", rc);
        }
    }

    ALOGI("%s: Set amplifier power to %d\n", __func__, on);

    return 0;
}

int tfa9887_set_mode(audio_mode_t mode)
{
    int rc, i;
    uint32_t dsp_mode;
    struct tfa9887_amp_t *amp = NULL;

    if (!amps) {
        ALOGE("%s: TFA9887 not opened\n", __func__);
        return -ENODEV;
    }

    dsp_mode = get_mode(mode);

    for (i = 0; i < AMP_MAX; i++) {
        amp = &amps[i];
        if (dsp_mode == amp->mode) {
            ALOGV("No mode change needed, already mode %d", dsp_mode);
            continue;
        }
        rc = tfa9887_lock(amp, true);
        if (rc) {
            /* Try next amp */
            continue;
        }
        rc = tfa9887_mute(amp, TFA9887_MUTE_DIGITAL);
        rc = tfa9887_set_dsp_mode(amp, dsp_mode);
        if (rc == 0) {
            /* Only count DSP mode switches that were successful */
            amp->mode = dsp_mode;
        }
        rc = tfa9887_mute(amp, TFA9887_MUTE_OFF);
        rc = tfa9887_lock(amp, false);
    }

    ALOGV("%s: Set amplifier audio mode to %d\n", __func__, mode);

    return 0;
}

int tfa9887_close(void)
{
    int i;
    struct tfa9887_amp_t *amp = NULL;

    if (!amps) {
        ALOGE("%s: TFA9887 not open!\n", __func__);
    }

    for (i = 0; i < AMP_MAX; i++) {
        amp = &amps[i];
        tfa9887_hw_power(amp, false);
        close(amp->fd);
    }

    free(amps);

    return 0;
}
