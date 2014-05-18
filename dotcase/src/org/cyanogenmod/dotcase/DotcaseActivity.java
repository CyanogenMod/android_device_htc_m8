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

public class DotcaseActivity extends Activity
{
    private static final String TAG = "DotcaseActivity";
    private final IntentFilter filter = new IntentFilter();
    private GestureDetectorCompat mDetector;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        filter.addAction("org.cyanogenmod.dotcase.KILL_ACTIVITY");
        this.getApplicationContext().registerReceiver(receiver, filter);

        getWindow().addFlags(
                    WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED|
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        final DrawView drawView = new DrawView(this);
        setContentView(drawView);

        mDetector = new GestureDetectorCompat(this, new DotcaseGestureListener());
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
            PowerManager manager = (PowerManager) getSystemService(Context.POWER_SERVICE);
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
