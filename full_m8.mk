# Copyright (C) 2011 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# This file is the build configuration for a full Android
# build for maguro hardware. This cleanly combines a set of
# device-specific aspects (drivers) with a device-agnostic
# product configuration (apps). Except for a few implementation
# details, it only fundamentally contains two inherit-product
# lines, full and maguro, hence its name.
#

# Extra Apps and files
PRODUCT_COPY_FILES += \
    vendor/htc/m8/Alert-SonarMerge.mp3:system/media/audio/notifications/Alert-SonarMerge.mp3 \
    vendor/htc/m8/CyanPing.ogg:system/media/audio/notifications/CyanPing.ogg \
    vendor/htc/m8/CyanMessage.ogg:system/media/audio/notifications/CyanMessage.ogg \
    vendor/htc/m8/apple_smsreceived.ogg:system/media/audio/notifications/apple_smsreceived.ogg \
    vendor/htc/m8/IphoneCellSoundMerge.mp3:system/media/audio/ringtones/IphoneCellSoundMerge.mp3 \
    vendor/aokp/prebuilt/common/app/NovaLauncher.apk:system/app/NovaLauncher.apk

# Inherit m8-specific vendor tree
$(call inherit-product-if-exists, vendor/htc/m8/m8-vendor.mk)

# Inherit from m8
$(call inherit-product, device/htc/m8/device.mk)

PRODUCT_NAME := full_m8
PRODUCT_DEVICE := m8
PRODUCT_BRAND := htc
PRODUCT_MANUFACTURER := htc
PRODUCT_MODEL := m8
