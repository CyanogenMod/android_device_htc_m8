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

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.BatteryManager;
import android.util.Log;
import android.view.View;

import java.util.Calendar;

public class DrawView extends View {
    // 1080 wide (27 dots)
    // 1920 high (48 dots)
    // 40pixels per dot
    private static final String TAG = "Dotcase";
    private final Context mContext;
    private float dotratio = 40;
    private Paint paint = new Paint();
    private final IntentFilter filter = new IntentFilter();

    public DrawView(Context context) {
        super(context);
        mContext = context;
        paint.setAntiAlias(true);
    }

    @Override
    public void onDraw(Canvas canvas) {
        drawTime(canvas);
        drawBattery(canvas);

        filter.addAction("org.cyanogenmod.dotcase.REDRAW");
        mContext.getApplicationContext().registerReceiver(receiver, filter);
    }

    private void drawBattery(Canvas canvas) {
        Intent batteryIntent = mContext.getApplicationContext().registerReceiver(null,
                    new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int rawlevel = batteryIntent.getIntExtra("level", -1);
        double scale = batteryIntent.getIntExtra("scale", -1);
        int plugged = batteryIntent.getIntExtra("plugged", -1);
        double level = -1;
        if (rawlevel >= 0 && scale > 0) {
            level = rawlevel / scale;
        }

        paint.setARGB(255, 255, 255, 255);
        dotcaseDrawRect(1, 35, 25, 36, paint, canvas);   // top line
        dotcaseDrawRect(24, 35, 25, 39, paint, canvas);  // upper right line
        dotcaseDrawRect(25, 38, 26, 44, paint, canvas);  // nub right
        dotcaseDrawRect(24, 43, 25, 47, paint, canvas);  // lower right line
        dotcaseDrawRect(1, 46, 25, 47, paint, canvas);   // bottom line
        dotcaseDrawRect(1, 35, 2, 47, paint, canvas);    // left line

        // 4.34 percents per dot
        int fillDots = (int)Math.round((level*100)/4.34);

        if (level >= .50) {
            paint.setARGB(255, 0, 255, 0);
        } else if (level >= .25) {
            paint.setARGB(255, 255, 165, 0);
        } else {
            paint.setARGB(255, 255, 0, 0);
        }

        for (int i = 0; i < fillDots; i++) {
            dotcaseDrawRect(2 + i, 36, 3 + i, 46, paint, canvas);
        }

        if (plugged > 0) {

            paint.setARGB(255, 0, 0, 0);
            int[][] blackSprite = {
                               {0, 0, 0, 0, 1, 1, 0, 0},
                               {0, 0, 0, 1, 0, 1, 0, 0},
                               {0, 0, 1, 0, 0, 1, 0, 0},
                               {0, 1, 0, 0, 0, 1, 0, 0},
                               {1, 0, 0, 0, 0, 1, 1, 1},
                               {1, 1, 1, 0, 0, 0, 0, 1},
                               {0, 0, 1, 0, 0, 0, 1, 0},
                               {0, 0, 1, 0, 0, 1, 0, 0},
                               {0, 0, 1, 0, 1, 0, 0, 0},
                               {0, 0, 1, 1, 0, 0, 0, 0}};
            dotcaseDrawSprite(blackSprite, 9, 36, paint, canvas);

            paint.setARGB(255, 255, 255, 0);
            int[][] lightningSprite = {
                               {0, 0, 0, 1, 0, 0},
                               {0, 0, 1, 1, 0, 0},
                               {0, 1, 1, 1, 0, 0},
                               {1, 1, 1, 1, 0, 0},
                               {0, 0, 1, 1, 1, 1},
                               {0, 0, 1, 1, 1, 0},
                               {0, 0, 1, 1, 0, 0},
                               {0, 0, 1, 0, 0, 0}};
            dotcaseDrawSprite(lightningSprite, 10, 37, paint, canvas);
        }
    }

    private void drawTime(Canvas canvas) {
        int hour_of_day = Calendar.getInstance().get(Calendar.HOUR_OF_DAY);
        String hours = "  ";
        String minutes = ((Calendar.getInstance().get(Calendar.MINUTE) < 10) ?
                         "0" + Integer.toString(Calendar.getInstance().get(Calendar.MINUTE)) :
                         Integer.toString(Calendar.getInstance().get(Calendar.MINUTE)));

        if (hour_of_day > 12) {
            hour_of_day = hour_of_day - 12;
        } else if (hour_of_day == 0) {
            hour_of_day = 12;
        }

        if (hour_of_day < 10) {
            hours = " " + Integer.toString(hour_of_day);
        } else {
            hours = Integer.toString(hour_of_day);
        }

        String time = hours + minutes;

        int[][] sprite;
        int x, y;
        int starter;

        if (hour_of_day > 9) {
            starter = 3;
        } else {
            starter = 0;
        }

        paint.setARGB(255, 51, 181, 229);
        dotcaseDrawPixel(starter + 10, 9, paint, canvas);
        dotcaseDrawPixel(starter + 10, 12, paint, canvas);

        for (int i = 0; i < time.length(); i++) {
            sprite = getSprite(time.charAt(i));

            y = 5;

            if (i == 0) {
                x = starter + 0;
            } else if (i == 1) {
                x = starter + 5;
            } else if (i == 2) {
                x = starter + 12;
            } else {
                x = starter + 17;
            }

            dotcaseDrawSprite(sprite, x, y, paint, canvas);

        }
    }

    public int[][] getSprite(char c) {
        int[][] sprite;
        switch (c) {
            case '0': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            case '1': sprite = new int[][]
                               {{0, 0, 1, 0},
                                {0, 1, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 1, 1, 1}};
                      break;
            case '2': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 1, 0},
                                {0, 0, 1, 0},
                                {0, 1, 0, 0},
                                {0, 1, 0, 0},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 1, 1, 1}};
                      break;
            case '3': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 1, 1, 0},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            case '4': sprite = new int[][]
                               {{1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 1, 1, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1}};
                      break;
            case '5': sprite = new int[][]
                               {{1, 1, 1, 1},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 1, 1, 0},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            case '6': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 0, 0, 0},
                                {1, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            case '7': sprite = new int[][]
                               {{1, 1, 1, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1}};
                      break;
            case '8': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            case '9': sprite = new int[][]
                               {{0, 1, 1, 0},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {0, 0, 0, 1},
                                {1, 0, 0, 1},
                                {0, 1, 1, 0}};
                      break;
            default: sprite = new int[][]
                               {{0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0},
                                {0, 0, 0, 0}};
                     break;
        }

        return sprite;
    }

    private void dotcaseDrawPixel(int x, int y, Paint paint, Canvas canvas) {
        canvas.drawRect((float)(x * dotratio + 5),
                        (float)(y * dotratio + 5),
                        (float)((x + 1) * dotratio -5),
                        (float)((y + 1) * dotratio -5),
                        paint);
    }

    private void dotcaseDrawRect(int left, int top, int right, int bottom, Paint paint, Canvas canvas) {
        for (int x=left; x < right; x++) {
            for (int y=top; y < bottom; y++) {
                dotcaseDrawPixel(x, y, paint, canvas);
            }
        }
    }

    private void dotcaseDrawSprite(int[][] sprite, int x, int y, Paint paint, Canvas canvas) {
        for (int i=0; i < sprite.length; i++) {
            for (int j=0; j < sprite[0].length; j++) {
                if(sprite[i][j] == 1) {
                    dotcaseDrawPixel(x + j, y + i, paint, canvas);
                }
            }
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            postInvalidate();
        }
    };
}
