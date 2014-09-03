/*
 * Copyright (C) 2013 The CyanogenMod Project
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

public class VibratorHW {

    private static String LEVEL_PATH = "/sys/devices/virtual/timed_output/vibrator/voltage_level";

    public static boolean isSupported() {
        File f = new File(LEVEL_PATH);
        return f.exists();
    }

    public static int getMaxIntensity()  {
        return 3199;
    }
    public static int getMinIntensity()  {
        return 1200;
    }
    public static int getWarningThreshold()  {
        return 3000;
    }
    public static int getCurIntensity()  {
        return Integer.parseInt(FileUtils.readOneLine(LEVEL_PATH));
    }
    public static int getDefaultIntensity()  {
        return 2700;
    }
    public static boolean setIntensity(int intensity)  {
        return FileUtils.writeLine(LEVEL_PATH, String.valueOf(intensity));
    }
}
