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

import android.app.INotificationManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.provider.Settings;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.os.UEventObserver;
import android.service.notification.StatusBarNotification;
import android.telephony.TelephonyManager;

import java.io.BufferedReader;
import java.io.FileReader;

class CoverObserver extends UEventObserver {
    private static final String TAG = "DotcaseCoverObserver";
    private static final String COVER_UEVENT_MATCH = "DEVPATH=/devices/virtual/switch/cover";

    private final Context mContext;
    private final WakeLock mWakeLock;
    private final IntentFilter filter = new IntentFilter();
    private PowerManager manager;

    private int oldBrightness = -1;
    private int oldBrightnessMode = -1;
    private boolean needStoreOldBrightness = true;

    public CoverObserver(Context context) {
        mContext = context;
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "CoverObserver");
        mWakeLock.setReferenceCounted(false);
    }

    public synchronized final void init() {
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(TelephonyManager.ACTION_PHONE_STATE_CHANGED);

        manager = (PowerManager) mContext.getSystemService(Context.POWER_SERVICE);
        startObserving(COVER_UEVENT_MATCH);
    }

    @Override
    public void onUEvent(UEventObserver.UEvent event) {
        try {
            int state = Integer.parseInt(event.get("SWITCH_STATE"));
            boolean screenOn = manager.isScreenOn();

            if (state == 1) {
                if (screenOn) {
                    manager.goToSleep(SystemClock.uptimeMillis());
                }
            } else {
                killActivity();
                if (!screenOn) {
                    manager.wakeUp(SystemClock.uptimeMillis());
                }
            }

            mWakeLock.acquire();
            mHandler.sendMessageDelayed(mHandler.obtainMessage(state), 0);
        } catch (Exception e) {}
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            if (msg.what == 1) {
                mContext.getApplicationContext().registerReceiver(receiver, filter);
            } else {
                try {
                    mContext.getApplicationContext().unregisterReceiver(receiver);
                } catch (Exception ex) {}
            }
            mWakeLock.release();
        }
    };

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            Intent i = new Intent();
            if (intent.getAction().equals(TelephonyManager.ACTION_PHONE_STATE_CHANGED)) {
                String state = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
                if (state.equals("RINGING")) {
                    intent.setAction(Dotcase.ACTION_PHONE_RINGING);
                    intent.putExtra("number", intent.getStringExtra(TelephonyManager.EXTRA_INCOMING_NUMBER));
                    mContext.sendBroadcast(intent);
                } else {
                    intent.setAction(Dotcase.ACTION_DONE_RINGING);
                    mContext.sendBroadcast(intent);
                }
            } else if (intent.getAction().equals(Intent.ACTION_SCREEN_ON)) {
                crankUpBrightness();
                checkNotifications();
                intent.setAction(Dotcase.ACTION_REDRAW);
                mContext.sendBroadcast(intent);
                i.setClassName("org.cyanogenmod.dotcase", "org.cyanogenmod.dotcase.Dotcase");
                i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                mContext.startActivity(i);
            }
        }
    };

    private void checkNotifications() {
        StatusBarNotification[] nots = null;
        try {
            INotificationManager mNoMan = INotificationManager.Stub.asInterface(
                     ServiceManager.getService(Context.NOTIFICATION_SERVICE));
            nots = mNoMan.getActiveNotifications(mContext.getPackageName());
        } catch (Exception ex) {}
        if (nots != null) {
            Intent intent = new Intent();
            boolean gmail = false;
            boolean hangouts = false;
            boolean twitter = false;
            boolean missed_call = false;
            for (int i = 0; i < nots.length; i++) {
                intent.setAction(Dotcase.NOTIFICATION);
                if (nots[i].getPackageName().equals("com.google.android.gm") && !gmail) {
                    gmail = true;
                    intent.putExtra("name", "gmail");
                    mContext.sendBroadcast(intent);
                } else if (nots[i].getPackageName().equals("com.google.android.talk") && !hangouts) {
                    hangouts = true;
                    intent.putExtra("name", "hangouts");
                    mContext.sendBroadcast(intent);
                } else if (nots[i].getPackageName().equals("com.twitter.android") && !twitter) {
                    twitter = true;
                    intent.putExtra("name", "twitter");
                    mContext.sendBroadcast(intent);
                } else if (nots[i].getPackageName().equals("com.android.phone") && !missed_call) {
                    missed_call = true;
                    intent.putExtra("name", "missed_call");
                    mContext.sendBroadcast(intent);
                }
            }

            intent.setAction(Dotcase.NOTIFICATION_CANCEL);

            if (!gmail) {
                intent.putExtra("name", "gmail");
                mContext.sendBroadcast(intent);
            }

            if (!hangouts) {
                intent.putExtra("name", "hangouts");
                mContext.sendBroadcast(intent);
            }

            if (!twitter) {
                intent.putExtra("name", "twitter");
                mContext.sendBroadcast(intent);
            }

            if (!missed_call) {
                intent.putExtra("name", "missed_call");
                mContext.sendBroadcast(intent);
            }

        }
    }

    private void crankUpBrightness() {
        if (needStoreOldBrightness) {
            try {
                oldBrightness = Settings.System.getInt(mContext.getContentResolver(),
                        Settings.System.SCREEN_BRIGHTNESS);
                oldBrightnessMode = Settings.System.getInt(mContext.getContentResolver(),
                        Settings.System.SCREEN_BRIGHTNESS_MODE);
            } catch (Exception ex) {}

            needStoreOldBrightness = false;
        }

        Settings.System.putInt(mContext.getContentResolver(),
                Settings.System.SCREEN_BRIGHTNESS_MODE,
                Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL);
        Settings.System.putInt(mContext.getContentResolver(),
                Settings.System.SCREEN_BRIGHTNESS, 255);
    }

    public void killActivity() {
        if (oldBrightnessMode != -1 && oldBrightness != -1 && !needStoreOldBrightness) {
            Settings.System.putInt(mContext.getContentResolver(),
                    Settings.System.SCREEN_BRIGHTNESS_MODE,
                    oldBrightnessMode);
            Settings.System.putInt(mContext.getContentResolver(),
                    Settings.System.SCREEN_BRIGHTNESS,
                    oldBrightness);
            needStoreOldBrightness = true;
        }

        try {
            Intent i = new Intent();
            i.setAction(Dotcase.ACTION_KILL_ACTIVITY);
            mContext.sendBroadcast(i);
        } catch (Exception ex) {}
    }
}
