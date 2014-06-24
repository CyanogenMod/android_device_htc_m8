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

public class TouchscreenFragmentActivity extends PreferenceFragment {

    private static final String PREF_ENABLED = "1";
    private static final String TAG = "DeviceSettings_Touchscreen";

    public static final String KEY_DT2WAKE_SWITCH = "dt2wake_switch";
    public static final String KEY_SWEEP2WAKE_SWITCH = "sweep2wake_switch";

    private TwoStatePreference mDT2WakeSwitch;
    private TwoStatePreference mSweep2WakeSwitch;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Resources res = getResources();

        addPreferencesFromResource(R.xml.touchscreen_preferences);

        if (Utils.fileExists("/sys/android_touch/doubletap2wake")) {
            mDT2WakeSwitch = (TwoStatePreference) findPreference(KEY_DT2WAKE_SWITCH);
            mDT2WakeSwitch.setEnabled(DT2WakeSwitch.isSupported());
            mDT2WakeSwitch.setOnPreferenceChangeListener(new DT2WakeSwitch());
        }
        if (Utils.fileExists("/sys/android_touch/sweep2wake")) {
            mSweep2WakeSwitch = (TwoStatePreference) findPreference(KEY_SWEEP2WAKE_SWITCH);
            mSweep2WakeSwitch.setEnabled(Sweep2WakeSwitch.isSupported());
            mSweep2WakeSwitch.setOnPreferenceChangeListener(new Sweep2WakeSwitch());
        }
    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
        String boxValue;
        String key = preference.getKey();
        Log.w(TAG, "key: " + key);
        return true;
    }

    public static boolean isSupported(String FILE) {
        return Utils.fileExists(FILE);
    }

}
