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
import android.os.SystemClock;
import android.provider.Settings;
import android.support.v4.view.GestureDetectorCompat;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ViewGroup.LayoutParams;
import android.view.View;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.FileReader;

public class DotcaseActivity extends Activity
{
    private static final String TAG = "DotcaseActivity";
    private static final String COVER_NODE = "/sys/android_touch/cover";
    private final IntentFilter filter = new IntentFilter();
    private GestureDetectorCompat mDetector;
    private PowerManager manager;
    private Context mContext;
    private volatile boolean running = true;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        mContext = this;

        filter.addAction("org.cyanogenmod.dotcase.KILL_ACTIVITY");
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
        mDetector = new GestureDetectorCompat(mContext, new DotcaseGestureListener());
        running = true;
        new Thread(new Service()).start();
    }

    class Service implements Runnable {
        @Override
        public void run() {
            while (running) {
                Intent batteryIntent = mContext.getApplicationContext().registerReceiver(null,
                                             new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                int timeout;

                if(batteryIntent.getIntExtra("plugged", -1) > 0) {
                    timeout = 20;
                } else {
                    timeout = 10;
                }

                for (int i = 0; i <= timeout; i++) {
                    try {
                        BufferedReader br = new BufferedReader(new FileReader(COVER_NODE));
                        String value = br.readLine();
                        br.close();

                        if (value.equals("0")) {
                            finish();
                            overridePendingTransition(0, 0);
                            running = false;
                        }
                    } catch (Exception ex) {
                        Log.e(TAG, ex.toString());
                    }

                    try {
                        Thread.sleep(1000);
                    } catch (Exception ex) {
                        Log.e(TAG, ex.toString());
                    }

                    Intent intent = new Intent();
                    intent.setAction("org.cyanogenmod.dotcase.REDRAW");
                    mContext.sendBroadcast(intent);
                }
                manager.goToSleep(SystemClock.uptimeMillis());
            }
        }
    }

    @Override
    public void onDestroy() {
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
        public boolean onDoubleTap(MotionEvent event)
        {
            Log.d(TAG, "onDoubleTap event");
            manager.goToSleep(SystemClock.uptimeMillis());
            return true;
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals("org.cyanogenmod.dotcase.KILL_ACTIVITY")) {
                try {
                    context.getApplicationContext().unregisterReceiver(receiver);
                } catch (Exception ex) {
                    Log.e(TAG, ex.toString());
                }
                finish();
                overridePendingTransition(0, 0);
            }
        }
    };
}
