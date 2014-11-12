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

#define CAMERA_PARAMETERS_EXTRA_C \
const char CameraParameters::KEY_CAPTURE_MODE[] = "capture-mode"; \
const char CameraParameters::KEY_CONTIBURST_TYPE[] = "contiburst-type"; \
const char CameraParameters::KEY_OIS_SUPPORT[] = "ois_support"; \
const char CameraParameters::KEY_OIS_MODE[] = "ois_mode"; \
const char CameraParameters::KEY_ZSL[] = "zsl"; \
const char CameraParameters::KEY_CAMERA_MODE[] = "camera-mode"; \
void CameraParameters::getBrightnessLumaTargetSet(int *magic, int *sauce) const{} \
void CameraParameters::getRawSize(int *magic, int *sauce) const{}

#define CAMERA_PARAMETERS_EXTRA_H \
    static const char KEY_CAPTURE_MODE[]; \
    static const char KEY_CONTIBURST_TYPE[]; \
    static const char KEY_OIS_SUPPORT[]; \
    static const char KEY_OIS_MODE[]; \
    static const char KEY_ZSL[]; \
    static const char KEY_CAMERA_MODE[]; \
    void getRawSize(int *magic, int *sauce) const; \
    void getBrightnessLumaTargetSet(int *magic, int *sauce) const;
