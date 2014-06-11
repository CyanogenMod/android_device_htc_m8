/*
 * Copyright (C) 2014, The CyanogenMod Project
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

//#define LOG_NDEBUG 0

#define LOG_TAG "wcnss_htc"

#define SUCCESS 0
#define FAILED -1

#define CALIBRATION_PATH "/proc/calibration"

#include <cutils/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int wcnss_init_qmi(void)
{
    /* empty */
    return SUCCESS;
}

int wcnss_qmi_get_wlan_address(unsigned char *pBdAddr)
{
    int fd, ret;
    char buf[30];

    fd = open(CALIBRATION_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("Failure opening calibration path: %d\n", errno);
        return FAILED;
    }

    ret = read(fd, buf, 30);
    if (ret < 0) {
        ALOGE("Failure to read calibration data: %d\n", errno);
        close(fd);
        return FAILED;
    }

    sscanf(buf, "macaddr=%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            &pBdAddr[0],
            &pBdAddr[1],
            &pBdAddr[2],
            &pBdAddr[3],
            &pBdAddr[4],
            &pBdAddr[5]);

    ALOGI("Found MAC address: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
            pBdAddr[0],
            pBdAddr[1],
            pBdAddr[2],
            pBdAddr[3],
            pBdAddr[4],
            pBdAddr[5]);

    close(fd);

    return SUCCESS;
}

void wcnss_qmi_deinit(void)
{
    /* empty */
}
