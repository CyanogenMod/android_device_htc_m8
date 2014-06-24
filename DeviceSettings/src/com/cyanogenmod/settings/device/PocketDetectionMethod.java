package com.cyanogenmod.settings.device;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;

public class PocketDetectionMethod implements OnPreferenceChangeListener {

    private static final String FILE = "/sys/android_touch/pocket_detect";

    private static final String METHOD_NONE = "0";
    private static final String METHOD_DARK = "1";
    private static final String METHOD_NO_DARK = "2";

    public static boolean isSupported() {
        return Utils.fileExists(FILE);
    }

    private static void setSysFsForMethod(String method)
    {
        if (method.equals(METHOD_NONE))
        {
             Utils.writeValue(FILE, "0\n");
        } else
        if (method.equals(METHOD_DARK))
        {
             Utils.writeValue(FILE, "1\n");
        } else
        if (method.equals(METHOD_NO_DARK))
        {
             Utils.writeValue(FILE, "2\n");
        }
    }

    /**
     * Restore WakeMethod setting from SharedPreferences. (Write to kernel.)
     * @param context       The context to read the SharedPreferences from
     */
    public static void restore(Context context) {
        if (!isSupported()) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        String method = sharedPrefs.getString(SensorsFragmentActivity.KEY_POCKETDETECTION_METHOD, "1");
        setSysFsForMethod(method);
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        setSysFsForMethod((String)newValue);
        return true;
    }

}
