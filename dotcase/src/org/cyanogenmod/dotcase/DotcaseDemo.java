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

import org.cyanogenmod.dotcase.DotcaseConstants.Notification;

import android.app.Activity;
//import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
//import android.hardware.Sensor;
//import android.hardware.SensorEvent;
//import android.hardware.SensorEventListener;
//import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.PowerManager;
//import android.os.RemoteException;
//import android.os.ServiceManager;
import android.os.SystemClock;
import android.util.Log;
//import android.view.GestureDetector;
//import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

//import com.android.internal.telephony.ITelephony;

//import java.lang.Math;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Vector;

public class DotcaseDemo extends Activity
{
    private static final String TAG = "Dotcase";

    private final IntentFilter mFilter = new IntentFilter();
    private PowerManager mPowerManager;
    private static Context mContext;
    private List<Notification> artificialNotifications = new Vector<Notification>();

    static DotcaseStatus sStatus = new DotcaseStatus();

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        mContext = this;

        mFilter.addAction(DotcaseConstants.ACTION_KILL_ACTIVITY);

        getWindow().addFlags(
                    WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON|
                    WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED|
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        final DrawView drawView = new DrawView(mContext);
        setContentView(drawView);

        mPowerManager = (PowerManager) mContext.getSystemService(Context.POWER_SERVICE);

        for (Map.Entry<String, Notification> entry : DotcaseConstants.notificationMap.entrySet()) {
            artificialNotifications.add(entry.getValue());
        }

        sStatus.stopRunning();
        new Thread(new Service()).start();
    }

    class Service implements Runnable {
        @Override
        public void run() {
            sStatus.setNotifications(artificialNotifications);
            if (!sStatus.isRunning()) {
                sStatus.startRunning();
                while (sStatus.isRunning()) {
                    Intent batteryIntent = mContext.getApplicationContext().registerReceiver(null,
                                             new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                    int timeout;

                    if(batteryIntent.getIntExtra("plugged", -1) > 0) {
                        timeout = 40;
                    } else {
                        timeout = 20;
                    }

                    for (int i = 0; i <= timeout; i++) {
                        if (sStatus.isResetTimer() || sStatus.isRinging() || sStatus.isAlarm()) {
                            i = 0;
                        }

                        if (!sStatus.isRunning()) {
                            return;
                        }

                        try {
                            Thread.sleep(500);
                        } catch (IllegalArgumentException e) {
                            // This isn't going to happen
                        } catch (InterruptedException e) {
                            Log.i(TAG, "Sleep interrupted", e);
                        }

                        Intent intent = new Intent();
                        intent.setAction(DotcaseConstants.ACTION_REDRAW);
                        mContext.sendBroadcast(intent);
                    }
                    mPowerManager.goToSleep(SystemClock.uptimeMillis());
                }
            }
        }
    }
}
