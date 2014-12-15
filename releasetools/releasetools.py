# Copyright (C) 2012 The Android Open Source Project
# Copyright (C) 2013 The CyanogenMod Project
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

""" Custom OTA commands for m8 devices """

import common
import re
import os

def FullOTA_InstallEnd(info):
  info.script.Mount("/system")
  info.script.AppendExtra('assert(run_program("/system/bin/makelinks.sh") == 0);')
  info.script.AppendExtra('ifelse(is_substring("0P6B20000", getprop("ro.boot.mid")), run_program("/sbin/sh", "-c", "busybox sed -i \'s/ro.com.google.clientidbase=android-google/ro.com.google.clientidbase=android-verizon/g\' /system/build.prop"));')
  info.script.Unmount("/system")

def FullOTA_Assertions(info):
  AddBootloaderAssertion(info, info.input_zip)

def IncrementalOTA_Assertions(info):
  AddBootloaderAssertion(info, info.target_zip)

def AddBootloaderAssertion(info, input_zip):
  android_info = input_zip.read("OTA/android-info.txt")
  m = re.search(r"require\s+version-bootloader\s*=\s*(\S+)", android_info)
  if m:
    bootloaders = m.group(1).split("|")
    if "*" not in bootloaders:
      info.script.AssertSomeBootloader(*bootloaders)
    info.metadata["pre-bootloader"] = m.group(1)
