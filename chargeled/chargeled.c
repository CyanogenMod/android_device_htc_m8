/*
 * Copyright (C) 2014 The CyanogenMod Project
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

#define LOG_TAG "[CHRG_LED]"
#define KLOG_LEVEL 6

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <cutils/klog.h>
#include <cutils/uevent.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define POWER_SUPPLY_SUBSYSTEM "SUBSYSTEM=power_supply"

#define BATTERY_STATUS_FILE "/sys/class/power_supply/battery/status"
#define AMBER_LED "/sys/class/leds/amber/brightness"
#define GREEN_LED "/sys/class/leds/green/brightness"

#define LED_OFF "0"
#define LED_ON "1"

#define STR_BUF_SIZE 128
#define UEVENT_BUF_SIZE 64*1024
#define UEVENT_MSG_LEN 1024
static int uevent_fd;

/* Matches frameworks/native/include/batteryservice/BatteryService.h */
enum {
    BATTERY_STATUS_UNKNOWN = 1,
    BATTERY_STATUS_CHARGING = 2,
    BATTERY_STATUS_DISCHARGING = 3,
    BATTERY_STATUS_NOT_CHARGING = 4,
    BATTERY_STATUS_FULL = 5,
};

struct sysfs_string_enum_map {
    char* str;
    int val;
} battery_status_map[] = {
    { "Unknown", BATTERY_STATUS_UNKNOWN },
    { "Charging", BATTERY_STATUS_CHARGING },
    { "Discharging", BATTERY_STATUS_DISCHARGING },
    { "Not charging", BATTERY_STATUS_NOT_CHARGING },
    { "Full", BATTERY_STATUS_FULL },
    { NULL, 0 },
};

static int map_sysfs_string(const char* str) {
    int i;
    int ret = -1;

    for (i = 0; battery_status_map[i].str; i++) {
        if (!strcmp(str, battery_status_map[i].str))
            ret = battery_status_map[i].val;
    }

    if (ret == -1)
        KLOG_ERROR(LOG_TAG, "%s: unknown charging status '%s'\n",
            __func__, str);

    return ret;
}

static int get_charging_status() {
    char batt_stat_str[STR_BUF_SIZE];
    int ret = -1;
    size_t ln;

    FILE *bstat;
    bstat = fopen(BATTERY_STATUS_FILE, "r");
    if (bstat) {
        if (fgets(batt_stat_str, STR_BUF_SIZE, bstat) == NULL) {
            KLOG_ERROR(LOG_TAG, "%s: failed to read %s; errno=%s\n",
                __func__, BATTERY_STATUS_FILE, strerror(errno));
            return -(errno);
        }

        if (!strcmp(batt_stat_str, "")) {
            KLOG_ERROR(LOG_TAG, "%s: empty battery status file %s\n",
                __func__, BATTERY_STATUS_FILE);
            return 0;
        }

        ln = strlen(batt_stat_str) - 1;
        if (batt_stat_str[ln] == '\n')
            batt_stat_str[ln] = '\0';

        fclose(bstat);
        ret = map_sysfs_string(batt_stat_str);
    } else {
        KLOG_ERROR(LOG_TAG, "%s: could not open %s; errno=%s\n",
            __func__, BATTERY_STATUS_FILE, strerror(errno));
        return -(errno);
    }

    return ret;
}

static void update_led(int charge_status) {
    FILE *aled, *gled;
    aled = fopen(AMBER_LED, "w");
    if (!aled) {
        KLOG_ERROR(LOG_TAG, "%s: could not open amber LED: %s\n",
            __func__, AMBER_LED);
        return;
    } else {
        gled = fopen(GREEN_LED, "w");
        if (!gled) {
            fclose(aled);
            KLOG_ERROR(LOG_TAG, "%s: could not open green LED: %s\n",
                __func__, GREEN_LED);
            return;
        }
    }

    KLOG_INFO(LOG_TAG, "%s: setting charging status '%d'\n",
            __func__, charge_status);

    switch (charge_status) {
        case BATTERY_STATUS_CHARGING:
            fputs(LED_ON, aled);
            fputs(LED_OFF, gled);
            break;
        case BATTERY_STATUS_FULL:
            fputs(LED_OFF, aled);
            fputs(LED_ON, gled);
            break;
        default:
            fputs(LED_OFF, aled);
            fputs(LED_OFF, gled);
            break;
    }

    fclose(aled);
    fclose(gled);
}

static void chargeled_update() {
    static int last_charge_status = BATTERY_STATUS_UNKNOWN;
    int charge_status;

    charge_status = get_charging_status();
    if (charge_status <= 0 || charge_status == last_charge_status)
        return;

    last_charge_status = charge_status;
    update_led(charge_status);
}

static int uevent_init() {
    uevent_fd = uevent_open_socket(UEVENT_BUF_SIZE, true);

    if (uevent_fd >= 0) {
        fcntl(uevent_fd, F_SETFL, O_NONBLOCK);
    } else {
        KLOG_ERROR(LOG_TAG, "%s: uevent_open_socket failed; errno=%s\n",
            __func__, strerror(errno));
        return errno;
    }

    return 0;
}

static void uevent_event() {
    char msg[UEVENT_MSG_LEN+2];
    char *cp;
    int n;

    n = uevent_kernel_multicast_recv(uevent_fd, msg, UEVENT_MSG_LEN);
    if (n <= 0)
        return;
    if (n >= UEVENT_MSG_LEN)   /* overflow -- discard */
        return;

    msg[n] = '\0';
    msg[n+1] = '\0';
    cp = msg;

    while (*cp) {
        if (!strcmp(cp, POWER_SUPPLY_SUBSYSTEM)) {
            chargeled_update();
            break;
        }

        /* advance to after the next \0 */
        while (*cp++)
            ;
    }
}

static int chargeled_mainloop() {
    struct epoll_event ev;
    int epollfd;
    int maxevents = 1;
    struct epoll_event events[maxevents];
    int nevents;
    int n;

    epollfd = epoll_create(1);
    if (epollfd == -1) {
        KLOG_ERROR(LOG_TAG, "%s: epoll_create failed; errno=%s\n",
            __func__, strerror(errno));
        return errno;
    }

    ev.events = EPOLLIN | EPOLLWAKEUP;
    ev.data.ptr = (void *)uevent_event;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, uevent_fd, &ev) == -1) {
        KLOG_ERROR(LOG_TAG,
            "%s: epoll_ctl for uevent_fd failed; errno=%s\n",
            __func__, strerror(errno));
        return errno;
    }

    /* If we've gotten this far, perform an LED update before the loop */
    chargeled_update();

    while (1) {
        nevents = epoll_wait(epollfd, events, maxevents, -1);

        if (nevents == -1) {
            if (errno == EINTR)
                continue;
            KLOG_ERROR(LOG_TAG, "%s: epoll_wait failed\n", __func__);
            break;
        }

        for (n = 0; n < nevents; ++n) {
            if (events[n].data.ptr)
                (*(void (*)())events[n].data.ptr)();
        }
    }

    return errno;
}

int main() {
    int ret;

    klog_set_level(KLOG_LEVEL);

    ret = uevent_init();
    if (ret == 0)
        ret = chargeled_mainloop();

    return ret;
}
