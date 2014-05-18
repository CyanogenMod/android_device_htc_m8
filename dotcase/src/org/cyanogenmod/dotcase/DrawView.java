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
import android.graphics.Paint;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.view.View;

import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;

public class DrawView extends View {
    // 1920x1080 = 48 x 27 dots @ 40 pixels per dot
    private static final String TAG = "DotcaseDrawView";
    private final Context mContext;
    private float dotratio = 40;
    private Paint paint = new Paint();
    private final IntentFilter filter = new IntentFilter();
    private static boolean ringing = false;
    private static int ringCounter = 0;
    private static boolean ringerSwitcher = false;
    private static boolean gmail = false;
    private static boolean hangouts = false;
    private static boolean missed_call = false;
    private int heartbeat = 0;

    public DrawView(Context context) {
        super(context);
        mContext = context;
        paint.setAntiAlias(true);
    }

    @Override
    public void onDraw(Canvas canvas) {
        drawTime(canvas);
        if (!ringing) {
            if (gmail == true || hangouts == true || missed_call == true) {
                if (heartbeat < 3) {
                    drawNotifications(canvas);
                } else {
                    drawBattery(canvas);
                }
                heartbeat++;
                if (heartbeat > 5) {
                    heartbeat = 0;
                }
            } else {
                drawBattery(canvas);
                heartbeat = 0;
            }
        } else {
            drawRinger(canvas);
        }

        filter.addAction(Dotcase.ACTION_DONE_RINGING);
        filter.addAction(Dotcase.ACTION_PHONE_RINGING);
        filter.addAction(Dotcase.ACTION_REDRAW);
        filter.addAction(Dotcase.NOTIFICATION_HANGOUTS);
        filter.addAction(Dotcase.NOTIFICATION_HANGOUTS_CANCEL);
        filter.addAction(Dotcase.NOTIFICATION_GMAIL);
        filter.addAction(Dotcase.NOTIFICATION_GMAIL_CANCEL);
        filter.addAction(Dotcase.NOTIFICATION_MISSED_CALL);
        filter.addAction(Dotcase.NOTIFICATION_MISSED_CALL_CANCEL);
        mContext.getApplicationContext().registerReceiver(receiver, filter);
    }

    private void drawNotifications(Canvas canvas) {
        int count = 0;
        int x = 1;
        int y = 34;

        if (gmail) {
            drawGmail(canvas, x + (count * 9), y);
            count++;
        }

        if (hangouts) {
            drawHangouts(canvas, x + (count * 9), y);
            count++;
        }

        if (missed_call) {
            drawMissedCall(canvas, x + (count * 9), y);
            count++;
        }
    }

    private void drawRinger(Canvas canvas) {
        int light = 3;
        int dark = 10;

        int[][] handsetSprite = {
                                {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
                                {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
                                {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
                                {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0},
                                {0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0}};

        int[][] ringerSprite = {
                                {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
                                {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},
                                {0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0},
                                {0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0},
                                {0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0},
                                {0, 1, 1, 1, 0, 0, 2, 0, 0, 1, 1, 1, 0},
                                {1, 1, 1, 0, 0, 2, 2, 2, 0, 0, 1, 1, 1},
                                {0, 1, 0, 0, 2, 2, 2, 2, 2, 0, 0, 1, 0},
                                {0, 0, 0, 2, 2, 2, 0, 2, 2, 2, 0, 0, 0},
                                {0, 0, 2, 2, 2, 0, 0, 0, 2, 2, 2, 0, 0},
                                {0, 2, 2, 2, 0, 0, 3, 0, 0, 2, 2, 2, 0},
                                {2, 2, 2, 0, 0, 3, 3, 3, 0, 0, 2, 2, 2},
                                {0, 2, 0, 0, 3, 3, 3, 3, 3, 0, 0, 2, 0},
                                {0, 0, 0, 3, 3, 3, 0, 3, 3, 3, 0, 0, 0},
                                {0, 0, 3, 3, 3, 0, 0, 0, 3, 3, 3, 0, 0},
                                {0, 3, 3, 3, 0, 0, 0, 0, 0, 3, 3, 3, 0},
                                {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
                                {0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0}};

        if (ringerSwitcher) {
            Collections.reverse(Arrays.asList(ringerSprite));
            Collections.reverse(Arrays.asList(handsetSprite));
            light = 2;
            dark = 11;
            for (int i = 0; i < handsetSprite.length; i++) {
                for (int j = 0; j < handsetSprite[0].length; j++) {
                    handsetSprite[i][j] = handsetSprite[i][j] > 0 ? light : 0;
                }
            }
        }

        for (int i = 0; i < ringerSprite.length; i++) {
            for (int j = 0; j < ringerSprite[0].length; j++) {
                if (ringerSprite[i][j] > 0) {
                    ringerSprite[i][j] =
                          ringerSprite[i][j] == 3 - ringCounter ? light : dark;
                }
            }
        }

        dotcaseDrawSprite(handsetSprite, 6, 21, canvas);
        dotcaseDrawSprite(ringerSprite, 7, 28, canvas);

        ringCounter++;
        if (ringCounter > 2) {
            ringerSwitcher = ringerSwitcher ? false : true;
            ringCounter = 0;
        }

        return;
    }

    private void drawHangouts(Canvas canvas, int x, int y) {
        int[][] hangoutsSprite = {
                                  {0, 3, 3, 3, 3, 3, 0},
                                  {3, 3, 1, 3, 1, 3, 3},
                                  {3, 3, 1, 3, 1, 3, 3},
                                  {0, 3, 3, 3, 3, 3, 0},
                                  {0, 0, 0, 3, 3, 0, 0},
                                  {0, 0, 0, 3, 0, 0, 0}};

        dotcaseDrawSprite(hangoutsSprite, x, y, canvas);
    }

    private void drawMissedCall(Canvas canvas, int x, int y) {
        int[][] missedCallSprite = {
                                  {0, 1, 0, 0, 0, 1, 0},
                                  {0, 0, 1, 0, 1, 0, 0},
                                  {0, 0, 0, 1, 0, 0, 0},
                                  {0, 7, 7, 7, 7, 7, 0},
                                  {7, 7, 7, 7, 7, 7, 7},
                                  {7, 7, 0, 0, 0, 7, 7}};

        dotcaseDrawSprite(missedCallSprite, x, y, canvas);
    }

    private void drawGmail(Canvas canvas, int x, int y) {
        int[][] gmailSprite = {
                               {2, 1, 1, 1, 1, 1, 2},
                               {2, 2, 1, 1, 1, 2, 2},
                               {2, 1, 2, 1, 2, 1, 2},
                               {2, 1, 1, 2, 1, 1, 2},
                               {2, 1, 1, 1, 1, 1, 2},
                               {2, 1, 1, 1, 1, 1, 2}};

        dotcaseDrawSprite(gmailSprite, x, y, canvas);
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

        dotcaseDrawRect(1, 35, 25, 36, 1, canvas);   // top line
        dotcaseDrawRect(24, 35, 25, 39, 1, canvas);  // upper right line
        dotcaseDrawRect(25, 38, 26, 44, 1, canvas);  // nub right
        dotcaseDrawRect(24, 43, 25, 47, 1, canvas);  // lower right line
        dotcaseDrawRect(1, 46, 25, 47, 1, canvas);   // bottom line
        dotcaseDrawRect(1, 35, 2, 47, 1, canvas);    // left line

        // 4.34 percents per dot
        int fillDots = (int)Math.round((level*100)/4.34);
        int color;

        if (level >= .50) {
            color = 3;
        } else if (level >= .25) {
            color = 5;
        } else {
            color = 2;
        }

        for (int i = 0; i < fillDots; i++) {
            if (i == 22) {
                dotcaseDrawRect(2 + i, 39, 3 + i, 43, color, canvas);
            } else {
                dotcaseDrawRect(2 + i, 36, 3 + i, 46, color, canvas);
            }
        }

        if (plugged > 0) {
            int[][] blackSprite = {
                               {-1, -1, -1, -1,  0,  0, -1, -1},
                               {-1, -1, -1,  0, -1,  0, -1, -1},
                               {-1, -1,  0, -1, -1,  0, -1, -1},
                               {-1,  0, -1, -1, -1,  0, -1, -1},
                               { 0, -1, -1, -1, -1,  0,  0,  0},
                               { 0,  0,  0, -1, -1, -1, -1,  0},
                               {-1, -1,  0, -1, -1, -1,  0, -1},
                               {-1, -1,  0, -1, -1,  0, -1, -1},
                               {-1, -1,  0, -1,  0, -1, -1, -1},
                               {-1, -1,  0,  0, -1, -1, -1, -1}};
            dotcaseDrawSprite(blackSprite, 9, 36, canvas);

            int[][] lightningSprite = {
                               {-1, -1, -1,  7, -1, -1},
                               {-1, -1,  7,  7, -1, -1},
                               {-1,  7,  7,  7, -1, -1},
                               { 7,  7,  7,  7, -1, -1},
                               {-1, -1,  7,  7,  7,  7},
                               {-1, -1,  7,  7,  7, -1},
                               {-1, -1,  7,  7, -1, -1},
                               {-1, -1,  7, -1, -1, -1}};
            dotcaseDrawSprite(lightningSprite, 10, 37, canvas);
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

        dotcaseDrawPixel(starter + 10, 9, 9, canvas);
        dotcaseDrawPixel(starter + 10, 12, 9, canvas);

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

            dotcaseDrawSprite(sprite, x, y, canvas);

        }
    }

    public int[][] getSprite(char c) {
        int[][] sprite;
        switch (c) {
            case '0': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            case '1': sprite = new int[][]
                               {{-1, -1,  9, -1},
                                {-1,  9,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1,  9,  9,  9}};
                      break;
            case '2': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1,  9, -1},
                                {-1, -1,  9, -1},
                                {-1,  9, -1, -1},
                                {-1,  9, -1, -1},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9,  9,  9,  9}};
                      break;
            case '3': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1,  9,  9, -1},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            case '4': sprite = new int[][]
                               {{ 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9,  9,  9,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9}};
                      break;
            case '5': sprite = new int[][]
                               {{ 9,  9,  9,  9},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9,  9,  9, -1},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            case '6': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9, -1, -1, -1},
                                { 9,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            case '7': sprite = new int[][]
                               {{ 9,  9,  9,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9}};
                      break;
            case '8': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            case '9': sprite = new int[][]
                               {{-1,  9,  9, -1},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                {-1, -1, -1,  9},
                                { 9, -1, -1,  9},
                                {-1,  9,  9, -1}};
                      break;
            default: sprite = new int[][]
                               {{-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1},
                                {-1, -1, -1, -1}};
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

    private void dotcaseDrawPixel(int x, int y, int color, Canvas canvas) {
        dotcaseDrawPixel(x, y, getPaintFromNumber(color), canvas);
    }

    private void dotcaseDrawRect(int left, int top, int right, int bottom, int color, Canvas canvas) {
        for (int x=left; x < right; x++) {
            for (int y=top; y < bottom; y++) {
                dotcaseDrawPixel(x, y, getPaintFromNumber(color), canvas);
            }
        }
    }

    private void dotcaseDrawSprite(int[][] sprite, int x, int y, Canvas canvas) {
        for (int i=0; i < sprite.length; i++) {
            for (int j=0; j < sprite[0].length; j++) {
                dotcaseDrawPixel(x + j, y + i, getPaintFromNumber(sprite[i][j]), canvas);
            }
        }
    }

    private Paint getPaintFromNumber(int color) {
        switch (color) {
            case -1: // transparent
                     paint.setARGB(0, 0, 0, 0);
                     break;
            case 0:  // black
                     paint.setARGB(255, 0, 0, 0);
                     break;
            case 1:  // white
                     paint.setARGB(255, 255, 255, 255);
                     break;
            case 2:  // red
                     paint.setARGB(255, 255, 0, 0);
                     break;
            case 3:  // green
                     paint.setARGB(255, 0, 255, 0);
                     break;
            case 4:  // blue
                     paint.setARGB(255, 0, 0, 255);
                     break;
            case 5:  // orange
                     paint.setARGB(255, 255, 165, 0);
                     break;
            case 6:  // purple
                     paint.setARGB(255, 160, 32, 240);
                     break;
            case 7:  // yellow
                     paint.setARGB(255, 255, 255, 0);
                     break;
            case 8:  // grey
                     paint.setARGB(255, 69, 69, 69);
                     break;
            case 9:  // cyan
                     paint.setARGB(255, 51, 181, 229);
                     break;
            case 10: // dark green
                     paint.setARGB(255, 0, 128, 0);
                     break;
            case 11: // dark red
                     paint.setARGB(255, 128, 0, 0);
                     break;
            default: // black
                     paint.setARGB(255, 0, 0, 0);
                     break;
        }

        return paint;
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(Dotcase.ACTION_PHONE_RINGING)) {
                phoneRinging(TelephonyManager.EXTRA_INCOMING_NUMBER);
            } else if (intent.getAction().equals(Dotcase.ACTION_DONE_RINGING)) {
                ringing = false;
            } else if (intent.getAction().equals(Dotcase.ACTION_REDRAW)) {
                postInvalidate();
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_HANGOUTS)) {
                hangouts = true;
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_HANGOUTS_CANCEL)) {
                hangouts = false;
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_GMAIL)) {
                gmail = true;
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_GMAIL_CANCEL)) {
                gmail = false;
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_MISSED_CALL)) {
                missed_call = true;
            } else if (intent.getAction().equals(Dotcase.NOTIFICATION_MISSED_CALL_CANCEL)) {
                missed_call = false;
            }
        }
    };

    private void phoneRinging(String number) {
        ringing = true;
        ringCounter = 0;
        ringerSwitcher = false;
        Log.e(TAG, "Phone Number: " + number);
    }
}
