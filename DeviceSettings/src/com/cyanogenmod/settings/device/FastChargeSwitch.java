package com.cyanogenmod.settings.device;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;

public class FastChargeSwitch implements OnPreferenceChangeListener {

    private static final String FILE = "/sys/kernel/fast_charge/force_fast_charge";

    public static boolean isSupported() {
        return Utils.fileExists(FILE);
    }

    /**
     * Restore ButtonLightNotification setting from SharedPreferences. (Write to kernel.)
     * @param context       The context to read the SharedPreferences from
     */
    public static void restore(Context context) {
        if (!isSupported()) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        boolean enabled = sharedPrefs.getBoolean(MiscFragmentActivity.KEY_FASTCHARGE_SWITCH, false);
        if(enabled)
            Utils.writeValue(FILE, "1\n");
        else
            Utils.writeValue(FILE, "0\n");
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        Boolean enabled = (Boolean) newValue;
        if(enabled)
            Utils.writeValue(FILE, "1\n");
        else
            Utils.writeValue(FILE, "0\n");
        return true;
    }

}
