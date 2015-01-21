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

package org.cyanogenmod.hardware;

import java.io.File;
import org.cyanogenmod.hardware.util.FileUtils;

public class TapToWake {

    private static final String CONTROL_PATH = "/sys/devices/virtual/htc_sensorhub/sensor_hub/tap2wake";

    public static boolean isSupported() {
        File f = new File(CONTROL_PATH);
        return f.exists();
    }

    public static boolean isEnabled()  {
        return Integer.parseInt(FileUtils.readOneLine(CONTROL_PATH)) == 1;
    }

    public static boolean setEnabled(boolean state)  {
        return FileUtils.writeLine(CONTROL_PATH, (state ? "1" : "0"));
    }
}
