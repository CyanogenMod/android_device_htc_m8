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
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.PowerManager;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import com.android.internal.telephony.ITelephony;

import java.lang.Math;
import java.io.BufferedReader;
import java.io.FileReader;

public class Dotcase extends Activity
{
    private static final String TAG = "Dotcase";
    private static final String COVER_NODE = "/sys/android_touch/cover";
    private final IntentFilter filter = new IntentFilter();
    private GestureDetector mDetector;
    private PowerManager manager;
    private Context mContext;
    private volatile boolean running = true;

    public static final String ACTION_DONE_RINGING = "org.cyanogenmod.dotcase.DONE_RINGING";
    public static final String ACTION_KILL_ACTIVITY = "org.cyanogenmod.dotcase.KILL_ACTIVITY";
    public static final String ACTION_PHONE_RINGING = "org.cyanogenmod.dotcase.PHONE_RINGING";
    public static final String ACTION_REDRAW = "org.cyanogenmod.dotcase.REDRAW";

    public static final String NOTIFICATION = "org.cyanogenmod.dotcase.notification.START";
    public static final String NOTIFICATION_CANCEL = "org.cyanogenmod.dotcase.notification.CANCEL";

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        mContext = this;

        filter.addAction(Dotcase.ACTION_KILL_ACTIVITY);
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

        manager = (PowerManager) mContext.getSystemService(Context.POWER_SERVICE);
        mDetector = new GestureDetector(mContext, new DotcaseGestureListener());
        running = false;
        new Thread(new Service()).start();
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
                        } catch (Exception ex) {}

                        try {
                            Thread.sleep(500);
                        } catch (Exception ex) {}

                        Intent intent = new Intent();
                        intent.setAction(Dotcase.ACTION_REDRAW);
                        mContext.sendBroadcast(intent);
                    }
                    manager.goToSleep(SystemClock.uptimeMillis());
                }
            }
        }
    }

    @Override
    public void onDestroy() {
        running = false;
        super.onDestroy();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event){
        this.mDetector.onTouchEvent(event);
        return super.onTouchEvent(event);
    }

    class DotcaseGestureListener extends GestureDetector.SimpleOnGestureListener
    {
        @Override
        public boolean onDoubleTap(MotionEvent event) {
            manager.goToSleep(SystemClock.uptimeMillis());
            return true;
        }

        @Override
        public boolean onScroll(MotionEvent e1, MotionEvent e2, float distanceX, float distanceY) {
            if (Math.abs(distanceY) > 60) {
                try {
                    ITelephony telephonyService = ITelephony.Stub.asInterface(
                                        ServiceManager.checkService(Context.TELEPHONY_SERVICE));
                    if (distanceY < 60) {
                        telephonyService.endCall();
                    } else if (distanceY > 60) {
                        telephonyService.answerRingingCall();
                    }
                } catch (Exception ex) {}
            }
            return true;
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(Dotcase.ACTION_KILL_ACTIVITY)) {
                try {
                    context.getApplicationContext().unregisterReceiver(receiver);
                } catch (Exception ex) {}
                running = false;
                finish();
                overridePendingTransition(0, 0);
            }
        }
    };
}
