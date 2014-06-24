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

package com.cyanogenmod.settings.device;

import android.content.Context;
import android.content.res.Resources;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.TwoStatePreference;
import android.util.Log;

import com.cyanogenmod.settings.device.R;

public class SensorsFragmentActivity extends PreferenceFragment implements OnPreferenceChangeListener {

    private static final String PREF_ENABLED = "1";
    private static final String TAG = "DeviceSettings_Sensors";

    private static final String KEY_PROXIMITY_CALIBRATION = "proximity_calibration";
    private static final String FILE_PROXIMITY_KADC = "/sys/devices/virtual/optical_sensors/proximity/ps_kadc";
    public static final String KEY_POCKETDETECTION_METHOD = "pocketdetection_method";

    private static boolean sPocketDetection;
    private ListPreference mPocketDetectionMethod;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Resources res = getResources();
        sPocketDetection = res.getBoolean(R.bool.has_pocketdetection);

        addPreferencesFromResource(R.xml.sensors_preferences);

        final ListPreference proximityPref = (ListPreference)findPreference(KEY_PROXIMITY_CALIBRATION);
        proximityPref.setOnPreferenceChangeListener(this);

        if (sPocketDetection) {
            mPocketDetectionMethod = (ListPreference) findPreference(KEY_POCKETDETECTION_METHOD);
            mPocketDetectionMethod.setEnabled(PocketDetectionMethod.isSupported());
            mPocketDetectionMethod.setOnPreferenceChangeListener(new PocketDetectionMethod());
        }

    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
        String boxValue;
        String key = preference.getKey();
        Log.w(TAG, "key: " + key);
        return true;
    }

    public boolean onPreferenceChange(Preference preference, Object newValue) {
        Log.e(TAG, "New proximity calibration value: " + (String)newValue);
        Utils.writeValue(FILE_PROXIMITY_KADC, (String)newValue);
        return true;
    }

    public static boolean isSupported(String FILE) {
        return Utils.fileExists(FILE);
    }

    public static void restore(Context context) {
        if (!isSupported(FILE_PROXIMITY_KADC)) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        Utils.writeValue(FILE_PROXIMITY_KADC, sharedPrefs.getString(KEY_PROXIMITY_CALIBRATION, "0x0 0xFFFF3C2D"));
    }
}
