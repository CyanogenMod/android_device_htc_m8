/*
 * Copyright (C) 2008-2014 The Android Open Source Project
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

#ifndef ANDROID_SENSOR_BASE_H
#define ANDROID_SENSOR_BASE_H

#include <errno.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>


/*****************************************************************************/

struct sensors_event_t;

#define NS_PER_SEC 1000000000LL
#define NS_PER_US 1000

class SensorBase {
protected:
    const char* dev_name;
    const char* data_name;
    int         dev_fd;
    int         data_fd;

    static int64_t getTimestamp();


    static int64_t timevalToNano(timeval const& t) {
        return t.tv_sec*NS_PER_SEC + t.tv_usec*NS_PER_US;
    }

    int open_device();
    int close_device();
    int write_int(char const *path, int value);
    int write_sys_attribute(
    char const *path, char const *value, int bytes);

public:
            SensorBase(
                    const char* dev_name,
                    const char* data_name);

    virtual ~SensorBase();

    virtual int readEvents(sensors_event_t* data, int count) = 0;
    virtual bool hasPendingEvents() const;
    virtual int getFd() const;
    virtual int getPollTime() { return -1; }
    virtual int setDelay(int32_t handle, int64_t ns);
    virtual int64_t getDelay(int32_t handle);

    // When this function is called, increments the reference counter.
    virtual int setEnable(int32_t handle, int enabled) = 0;
    // It returns the number of reference.
    virtual int getEnable(int32_t handle) = 0;
    virtual int batch(int handle, int flags, int64_t period_ns, int64_t timeout) = 0;
    virtual int flush(int handle) = 0;
};

/*****************************************************************************/

#endif  // ANDROID_SENSOR_BASE_H
