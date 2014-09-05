/*
 * Copyright (c) 2014 The CyanogenMod Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * Also add information on how to contact you by electronic and paper mail.
 *
 */

package org.cyanogenmod.dotcase;

import java.text.Normalizer;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.SystemClock;
import android.os.UEventObserver;
import android.provider.ContactsContract;
import android.provider.Settings;
import android.telephony.TelephonyManager;
import android.util.Log;

class CoverObserver extends UEventObserver {
    private static final String COVER_UEVENT_MATCH = "DEVPATH=/devices/virtual/switch/cover";

    private static final String TAG = "Dotcase";

    private final Context mContext;
    private final WakeLock mWakeLock;
    private final IntentFilter mFilter = new IntentFilter();
    private PowerManager mPowerManager;

    private int mOldBrightness = -1;
    private int mOldBrightnessMode = -1;
    private boolean mStoreOldBrightness = true;
    private int mSwitchState = 0;

    public CoverObserver(Context context) {
        mContext = context;
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "CoverObserver");
        mWakeLock.setReferenceCounted(false);
    }

    public synchronized final void init() {
        mFilter.addAction(Intent.ACTION_SCREEN_ON);
        mFilter.addAction(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        mFilter.addAction("com.android.deskclock.ALARM_ALERT");
        // add other alarm apps here

        mPowerManager = (PowerManager) mContext.getSystemService(Context.POWER_SERVICE);
        startObserving(COVER_UEVENT_MATCH);
    }

    @Override
    public void onUEvent(UEventObserver.UEvent event) {
        try {
            mSwitchState = Integer.parseInt(event.get("SWITCH_STATE"));
            boolean screenOn = mPowerManager.isScreenOn();
            Dotcase.sStatus.setOnTop(false);

            if (mSwitchState == 1) {
                if (screenOn) {
                    mPowerManager.goToSleep(SystemClock.uptimeMillis());
                }
            } else {
                killActivity();
                if (!screenOn) {
                    mPowerManager.wakeUp(SystemClock.uptimeMillis());
                }
            }

            mWakeLock.acquire();
            mHandler.sendMessageDelayed(mHandler.obtainMessage(mSwitchState), 0);
        } catch (NumberFormatException e) {
            Log.e(TAG, "Error parsing SWITCH_STATE event", e);
        }
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            if (msg.what == 1) {
                mContext.getApplicationContext().registerReceiver(receiver, mFilter);
            } else {
                try {
                    mContext.getApplicationContext().unregisterReceiver(receiver);
                } catch (IllegalArgumentException e) {
                    Log.e(TAG, "Failed to unregister receiver", e);
                }
            }
            mWakeLock.release();
        }
    };

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            // If the case is open, don't try to do any of this
            if (mSwitchState == 0) {
                return;
            }
            Intent i = new Intent();
            if (intent.getAction().equals(TelephonyManager.ACTION_PHONE_STATE_CHANGED)) {
                String state = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
                if (state.equals("RINGING")) {

                    String number = intent.getStringExtra(TelephonyManager.EXTRA_INCOMING_NUMBER);
                    Uri uri = Uri.withAppendedPath(ContactsContract.PhoneLookup.CONTENT_FILTER_URI,
                            Uri.encode(number));
                    Cursor cursor = context.getContentResolver().query(uri,
                            new String[] {ContactsContract.PhoneLookup.DISPLAY_NAME},
                            number, null, null);
                    String name;
                    if (cursor.moveToFirst()) {
                        name = cursor.getString(cursor.getColumnIndex(
                                ContactsContract.PhoneLookup.DISPLAY_NAME));
                    } else {
                        name = "";
                    }
                    cursor.close();

                    if (number.equalsIgnoreCase("restricted")) {
                        // If call is restricted, don't show a number
                        name = number;
                        number = "";
                    }

                    name = normalize(name);
                    name = name + "  "; // Add spaces so the scroll effect looks good

                    Dotcase.sStatus.startRinging(number, name);
                    Dotcase.sStatus.setOnTop(true);
                    new Thread(new ensureTopActivity()).start();

                } else {
                    Dotcase.sStatus.setOnTop(false);
                    Dotcase.sStatus.stopRinging();
                }
            } else if (intent.getAction().equals("com.android.deskclock.ALARM_ALERT")) {
                // add other alarm apps here
                Dotcase.sStatus.startAlarm();
                Dotcase.sStatus.setOnTop(true);
                new Thread(new ensureTopActivity()).start();
            } else if (intent.getAction().equals(Intent.ACTION_SCREEN_ON)) {
                crankUpBrightness();
                Dotcase.sStatus.resetTimer();
                intent.setAction(DotcaseConstants.ACTION_REDRAW);
                mContext.sendBroadcast(intent);
                i.setClassName("org.cyanogenmod.dotcase", "org.cyanogenmod.dotcase.Dotcase");
                i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                mContext.startActivity(i);
            }
        }
    };

    /**
     * Normalizes a string to lowercase without diacritics
     */
    private static String normalize(String str) {
        return Normalizer.normalize(str.toLowerCase(), Normalizer.Form.NFD)
                .replaceAll("\\p{InCombiningDiacriticalMarks}+", "")
                .replaceAll("æ", "ae")
                .replaceAll("ð", "d")
                .replaceAll("ø", "o")
                .replaceAll("þ", "th")
                .replaceAll("ß", "ss")
                .replaceAll("œ", "oe");
    }

    private void crankUpBrightness() {
        if (mStoreOldBrightness) {
            try {
                mOldBrightness = Settings.System.getInt(mContext.getContentResolver(),
                        Settings.System.SCREEN_BRIGHTNESS);
                mOldBrightnessMode = Settings.System.getInt(mContext.getContentResolver(),
                        Settings.System.SCREEN_BRIGHTNESS_MODE);
            } catch (Settings.SettingNotFoundException e) {
                Log.e(TAG, "Error retrieving brightness settings", e);
            }

            mStoreOldBrightness = false;
        }

        Settings.System.putInt(mContext.getContentResolver(),
                Settings.System.SCREEN_BRIGHTNESS_MODE,
                Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL);
        Settings.System.putInt(mContext.getContentResolver(),
                Settings.System.SCREEN_BRIGHTNESS, 255);
    }

    public void killActivity() {
        Dotcase.sStatus.stopRinging();
        Dotcase.sStatus.stopAlarm();
        Dotcase.sStatus.setOnTop(false);
        if (mOldBrightnessMode != -1 && mOldBrightness != -1 && !mStoreOldBrightness) {
            Settings.System.putInt(mContext.getContentResolver(),
                    Settings.System.SCREEN_BRIGHTNESS_MODE,
                    mOldBrightnessMode);
            Settings.System.putInt(mContext.getContentResolver(),
                    Settings.System.SCREEN_BRIGHTNESS,
                    mOldBrightness);
            mStoreOldBrightness = true;
        }

        Intent i = new Intent();
        i.setAction(DotcaseConstants.ACTION_KILL_ACTIVITY);
        mContext.sendBroadcast(i);
    }

    private class ensureTopActivity implements Runnable {
        Intent i = new Intent();

        @Override
        public void run() {
            while ((Dotcase.sStatus.isRinging() || Dotcase.sStatus.isAlarm())
                    && Dotcase.sStatus.isOnTop()) {
                ActivityManager am =
                        (ActivityManager) mContext.getSystemService(Activity.ACTIVITY_SERVICE);
                if (!am.getRunningTasks(1).get(0).topActivity.getPackageName().equals(
                        "org.cyanogenmod.dotcase")) {
                    i.setClassName("org.cyanogenmod.dotcase", "org.cyanogenmod.dotcase.Dotcase");
                    i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    mContext.startActivity(i);
                }
                try {
                    Thread.sleep(100);
                } catch (IllegalArgumentException e) {
                    // This isn't going to happen
                } catch (InterruptedException e) {
                    Log.i(TAG, "Sleep interrupted", e);
                }
            }
        }
    }
}
