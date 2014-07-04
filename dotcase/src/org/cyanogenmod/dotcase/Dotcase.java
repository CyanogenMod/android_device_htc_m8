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

import android.app.Activity;
import android.app.INotificationManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.PowerManager;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.service.notification.StatusBarNotification;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import com.android.internal.telephony.ITelephony;
import com.android.internal.util.cm.TorchConstants;

import java.lang.Math;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;

public class Dotcase extends Activity implements SensorEventListener
{
    private static final String TAG = "Dotcase";

    private static final String COVER_NODE = "/sys/android_touch/cover";
    private final IntentFilter filter = new IntentFilter();
    private GestureDetector mDetector;
    private PowerManager powerManager;
    private SensorManager sensorManager;
    private static Context mContext;
    private static boolean running = true;
    private static boolean pocketed = false;

    public static boolean reset_timer = false;

    public static boolean ringing = false;
    public static int ringCounter = 0;
    public static String phoneNumber = "";
    public static boolean torchStatus = false;

    public static boolean gmail = false;
    public static boolean hangouts = false;
    public static boolean twitter = false;
    public static boolean missed_call = false;
    public static boolean mms = false;
    public static boolean voicemail = false;

    public static boolean alarm_clock = false;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        mContext = this;

        filter.addAction(DotcaseConstants.ACTION_KILL_ACTIVITY);
        filter.addAction(TorchConstants.ACTION_STATE_CHANGED);
        mContext.getApplicationContext().registerReceiver(receiver, filter);

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

        powerManager = (PowerManager) mContext.getSystemService(Context.POWER_SERVICE);
        sensorManager = (SensorManager) mContext.getSystemService(Context.SENSOR_SERVICE);
        mDetector = new GestureDetector(mContext, new DotcaseGestureListener());
        running = false;
        new Thread(new Service()).start();
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_PROXIMITY) {
            Log.d(TAG, "Proximity changed to " + event.values[0] + " max " + event.sensor.getMaximumRange());
            if (event.values[0] < event.sensor.getMaximumRange() && pocketed) {
                pocketed = false;
            } else if (!pocketed) {
                pocketed = true;
            }
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) { }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "Registering SensorEventListener");
        sensorManager.registerListener(this,
                sensorManager.getDefaultSensor(Sensor.TYPE_PROXIMITY),
                SensorManager.SENSOR_DELAY_NORMAL);
    }

    @Override
    protected void onPause() {
        super.onPause();
        try {
            Log.d(TAG, "Unregistering SensorEventListener");
            sensorManager.unregisterListener(this);
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Failed to unregister listener", e);
        }
    }

    class Service implements Runnable {
        @Override
        public void run() {
            if (!running) {
                running = true;
                while (running) {
                    Intent batteryIntent = mContext.getApplicationContext().registerReceiver(null,
                                             new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                    int timeout;

                    if(batteryIntent.getIntExtra("plugged", -1) > 0) {
                        timeout = 40;
                    } else {
                        timeout = 20;
                    }

                    for (int i = 0; i <= timeout; i++) {
                        if (reset_timer) {
                            i = 0;
                            reset_timer = false;
                        }

                        if (!running) {
                            return;
                        }

                        try {
                            BufferedReader br = new BufferedReader(new FileReader(COVER_NODE));
                            String value = br.readLine();
                            br.close();

                            if (value.equals("0")) {
                                running = false;
                                finish();
                                overridePendingTransition(0, 0);
                            }
                        } catch (IOException e) {
                            Log.e(TAG, "Error reading cover device", e);
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
                    powerManager.goToSleep(SystemClock.uptimeMillis());
                }
            }
        }
    }

    public static void checkNotifications() {
        StatusBarNotification[] nots = null;
        gmail = false;
        hangouts = false;
        twitter = false;
        missed_call = false;
        mms = false;
        voicemail = false;

        Log.d(TAG, "Checking notifications");

        try {
            INotificationManager mNoMan = INotificationManager.Stub.asInterface(
                    ServiceManager.getService(Context.NOTIFICATION_SERVICE));
            nots = mNoMan.getActiveNotifications(mContext.getPackageName());
            for (StatusBarNotification not : nots) {
                String pName = not.getPackageName();
                Log.d(TAG, "Found notification from " + pName);
                if (pName.equals("com.google.android.gm") && !gmail) {
                    gmail = true;
                } else if (pName.equals("com.google.android.talk") && !hangouts) {
                    hangouts = true;
                } else if (pName.equals("com.twitter.android") && !twitter) {
                    twitter = true;
                } else if (pName.equals("com.android.phone") && !missed_call) {
                    missed_call = true;
                } else if (pName.equals("com.android.mms") && !mms) {
                    mms = true;
                } else if (pName.equals("com.google.android.apps.googlevoice")
                           && !voicemail) {
                    voicemail = true;
                }
            }
        } catch (NullPointerException e) {
            Log.e(TAG, "Error retrieving notifications", e);
        } catch (RemoteException e) {
            Log.e(TAG, "Error retrieving notifications", e);
        }
    }

    @Override
    public void onDestroy() {
        running = false;
        super.onDestroy();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event){
        if (pocketed) {
            this.mDetector.onTouchEvent(event);
            return super.onTouchEvent(event);
        } else {
            Log.d(TAG, "Ignoring touch event due to proximity sensor");
            // Say that we handled this event so nobody else does
            return true;
        }
    }

    class DotcaseGestureListener extends GestureDetector.SimpleOnGestureListener
    {

        @Override
        public void onLongPress(MotionEvent event) {
            Log.d(TAG, "Long press, toggling torch");
            Intent i = new Intent(TorchConstants.ACTION_TOGGLE_STATE);
            mContext.sendBroadcast(i);
        }

        @Override
        public boolean onDoubleTap(MotionEvent event) {
            Log.d(TAG, "Double tap, going to sleep");
            powerManager.goToSleep(SystemClock.uptimeMillis());
            return true;
        }

        @Override
        public boolean onScroll(MotionEvent e1, MotionEvent e2, float distanceX, float distanceY) {
            if (Math.abs(distanceY) > 60) {
                Log.d(TAG, "Vertical scroll detected");
                if (ringing) {
                    CoverObserver.topActivityKeeper = false;
                    ITelephony telephonyService = ITelephony.Stub.asInterface(
                            ServiceManager.checkService(Context.TELEPHONY_SERVICE));
                    if (distanceY < 60) {
                        Log.d(TAG, "Ignoring call");
                        try {
                            telephonyService.endCall();
                        } catch (RemoteException e) {
                            Log.e(TAG, "Error ignoring call", e);
                        }
                    } else if (distanceY > 60) {
                        Log.d(TAG, "Answering call");
                        try {
                            telephonyService.answerRingingCall();
                        } catch (RemoteException e) {
                            Log.e(TAG, "Error answering call", e);
                        }
                    }
                } else if (alarm_clock) {
                    Intent i = new Intent();
                    if (distanceY < 60) {
                        Log.d(TAG, "Dismissing alarm");
                        i.setAction("com.android.deskclock.ALARM_DISMISS");
                        CoverObserver.topActivityKeeper = false;
                        mContext.sendBroadcast(i);
                        alarm_clock = false;
                    } else if (distanceY > 60) {
                        Log.d(TAG, "Snoozing alarm");
                        i.setAction("com.android.deskclock.ALARM_SNOOZE");
                        CoverObserver.topActivityKeeper = false;
                        mContext.sendBroadcast(i);
                        alarm_clock = false;
                    }
                }
            }
            return true;
        }

        @Override
        public boolean onSingleTapUp (MotionEvent e) {
            if (Dotcase.torchStatus) {
                if (e.getX() >19 * DotcaseConstants.dotratio
                        && e.getX() < 26 * DotcaseConstants.dotratio
                        && e.getY() > 22 * DotcaseConstants.dotratio
                        && e.getY() < 32 * DotcaseConstants.dotratio) {
                    Log.d(TAG, "Torch tapped, disabling torch");
                    Intent i = new Intent(TorchConstants.ACTION_TOGGLE_STATE);
                    mContext.sendBroadcast(i);
                }
            }
            reset_timer = true;
            return true;
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(DotcaseConstants.ACTION_KILL_ACTIVITY)) {
                try {
                    context.getApplicationContext().unregisterReceiver(receiver);
                } catch (IllegalArgumentException e) {
                    Log.e(TAG, "Failed to unregister receiver", e);
                }
                running = false;
                finish();
                overridePendingTransition(0, 0);
            } else if (intent.getAction().equals(TorchConstants.ACTION_STATE_CHANGED)) {
                torchStatus = intent.getIntExtra(TorchConstants.EXTRA_CURRENT_STATE, 0) != 0;
            }
        }
    };
}
