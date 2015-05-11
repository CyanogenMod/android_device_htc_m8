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

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.text.format.DateFormat;
import android.view.View;

import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;
import java.util.List;

public class DrawView extends View {
    // 1920x1080 = 48 x 27 dots @ 40 pixels per dot
    private final Context mContext;
    private final IntentFilter mFilter = new IntentFilter();
    private int mHeartbeat = 0;
    private Paint mPaint;

    public DrawView(Context context) {
        super(context);
        mContext = context;
        mPaint = new Paint();
        mPaint.setAntiAlias(true);
    }

    @Override
    public void onDraw(Canvas canvas) {
        if (Dotcase.sStatus.isAlarm()) {
            drawAlarm(canvas);
        } else if (Dotcase.sStatus.isRinging()) {
            drawName(canvas);
            drawNumber(canvas);
            drawRinger(canvas);
        } else {
            drawTime(canvas);

            // Check notifications each cycle before displaying them
            if (mHeartbeat == 0) {
                Dotcase.sStatus.checkNotifications(mContext);
            }

            if (!Dotcase.sStatus.hasNotifications()) {
                if (mHeartbeat < 3) {
                    drawNotifications(canvas);
                } else {
                    drawBattery(canvas);
                }

                mHeartbeat++;

                if (mHeartbeat > 5) {
                    mHeartbeat = 0;
                }
            } else {
                drawBattery(canvas);
                mHeartbeat = 0;
            }
        }

        mFilter.addAction(DotcaseConstants.ACTION_REDRAW);
        mContext.getApplicationContext().registerReceiver(receiver, mFilter);
    }

    private timeObject getTimeObject() {
        timeObject timeObj = new timeObject();
        timeObj.hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY);
        timeObj.min = Calendar.getInstance().get(Calendar.MINUTE);

        if (DateFormat.is24HourFormat(mContext)) {
            timeObj.is24Hour = true;
        } else {
            timeObj.is24Hour = false;
            if (timeObj.hour > 11) {
                if (timeObj.hour > 12) {
                    timeObj.hour = timeObj.hour - 12;
                }
                timeObj.am = false;
            } else {
                if (timeObj.hour == 0) {
                    timeObj.hour = 12;
                }
                timeObj.am = true;
            }
        }

        timeObj.timeString = (timeObj.hour < 10
                                   ? " " + Integer.toString(timeObj.hour)
                                   : Integer.toString(timeObj.hour))
                           + (timeObj.min < 10
                                   ? "0" + Integer.toString(timeObj.min)
                                   : Integer.toString(timeObj.min));
        return timeObj;
    }

    private void drawAlarm(Canvas canvas) {
        int light = 7, dark = 12;
        int clockHeight = DotcaseConstants.clockSprite.length;
        int clockWidth = DotcaseConstants.clockSprite[0].length;
        int ringerHeight = DotcaseConstants.ringerSprite.length;
        int ringerWidth = DotcaseConstants.ringerSprite[0].length;
        timeObject time = getTimeObject();

        int[][] mClockSprite = new int[clockHeight][clockWidth];
        int[][] mRingerSprite = new int[ringerHeight][ringerWidth];

        int ringCounter = Dotcase.sStatus.ringCounter();

        for (int i = 0; i < ringerHeight; i++) {
            for (int j = 0; j < ringerWidth; j++) {
                if (DotcaseConstants.ringerSprite[i][j] > 0) {
                    mRingerSprite[i][j] =
                            DotcaseConstants.ringerSprite[i][j] == 3 - (ringCounter % 3)
                                    ? light : dark;
                }
            }
        }

        for (int i = 0; i < clockHeight; i++) {
            for (int j = 0; j < clockWidth; j++) {
                mClockSprite[i][j] = DotcaseConstants.clockSprite[i][j] > 0 ? light : 0;
            }
        }

        dotcaseDrawSprite(DotcaseConstants.getSmallCharSprite(
                time.timeString.charAt(0)), 0, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallCharSprite(
                time.timeString.charAt(1)), 4, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.smallTimeColon, 8, 1, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallCharSprite(
                time.timeString.charAt(2)), 11, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallCharSprite(
                time.timeString.charAt(3)), 15, 0, canvas);
        dotcaseDrawSprite(mClockSprite, 7, 7, canvas);

        if (!time.is24Hour) {
            if (time.am) {
                dotcaseDrawSprite(DotcaseConstants.amSprite, 18, 0, canvas);
            } else {
                dotcaseDrawSprite(DotcaseConstants.pmSprite, 18, 0, canvas);
            }
        }

        if (ringCounter / 6 > 0) {
            dotcaseDrawSprite(DotcaseConstants.alarmCancelArray, 2, 21, canvas);
            Collections.reverse(Arrays.asList(mRingerSprite));
        } else {
            dotcaseDrawSprite(DotcaseConstants.snoozeArray, 2, 21, canvas);
        }

        dotcaseDrawSprite(mRingerSprite, 7, 28, canvas);

        if (ringCounter > 10) {
            Dotcase.sStatus.resetRingCounter();
        } else {
            Dotcase.sStatus.incrementRingCounter();
        }
    }

    private void drawNotifications(Canvas canvas) {
        int count = 0;
        int x = 1;
        int y = 30;

        List<Notification> notifications = Dotcase.sStatus.getNotifications();
        for (Notification notification : notifications) {
            int[][] sprite = DotcaseConstants.getNotificationSprite(notification);
            if (sprite != null) {
                dotcaseDrawSprite(sprite, x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
                count++;
            }
        }
    }

    private void drawRinger(Canvas canvas) {
        int light, dark;
        int handsetHeight = DotcaseConstants.handsetSprite.length;
        int handsetWidth = DotcaseConstants.handsetSprite[0].length;
        int ringerHeight = DotcaseConstants.ringerSprite.length;
        int ringerWidth = DotcaseConstants.ringerSprite[0].length;

        int[][] mHandsetSprite = new int[handsetHeight][handsetWidth];
        int[][] mRingerSprite = new int[ringerHeight][ringerWidth];

        int ringCounter = Dotcase.sStatus.ringCounter();

        if (ringCounter / 3 > 0) {
            light = 2;
            dark = 11;
        } else {
            light = 3;
            dark = 10;
        }

        for (int i = 0; i < ringerHeight; i++) {
            for (int j = 0; j < ringerWidth; j++) {
                if (DotcaseConstants.ringerSprite[i][j] > 0) {
                    mRingerSprite[i][j] =
                            DotcaseConstants.ringerSprite[i][j] == 3 - (ringCounter % 3)
                                    ? light : dark;
                }
            }
        }

        for (int i = 0; i < handsetHeight; i++) {
            for (int j = 0; j < handsetWidth; j++) {
                mHandsetSprite[i][j] = DotcaseConstants.handsetSprite[i][j] > 0 ? light : 0;
            }
        }

        if (ringCounter / 3 > 0) {
            Collections.reverse(Arrays.asList(mRingerSprite));
            Collections.reverse(Arrays.asList(mHandsetSprite));
        }

        dotcaseDrawSprite(mHandsetSprite, 6, 21, canvas);
        dotcaseDrawSprite(mRingerSprite, 7, 28, canvas);

        if (ringCounter > 4) {
            Dotcase.sStatus.resetRingCounter();
        } else {
            Dotcase.sStatus.incrementRingCounter();
        }
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

        dotcaseDrawSprite(DotcaseConstants.batteryOutlineSprite, 1, 35, canvas);

        // 4.34 percents per dot
        int fillDots = (int)Math.round((level * 100) / 4.34);
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
            dotcaseDrawSprite(DotcaseConstants.lightningSprite, 9, 36, canvas);
        }
    }

    private void drawTime(Canvas canvas) {
        timeObject time = getTimeObject();
        int starter;

        if (time.hour < 10) {
            starter = 0;
        } else {
            starter = 3;
        }

        if (!time.is24Hour) {
            if (time.am) {
                dotcaseDrawSprite(DotcaseConstants.amSprite, 3, 18, canvas);
            } else {
                dotcaseDrawSprite(DotcaseConstants.pmSprite, 3, 18, canvas);
            }
        }

        dotcaseDrawSprite(DotcaseConstants.timeColon, starter + 10, 5 + 4, canvas);
        dotcaseDrawSprite(DotcaseConstants.getNumSprite(time.timeString.charAt(0)),
                starter, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getNumSprite(time.timeString.charAt(1)),
                starter + 5, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getNumSprite(time.timeString.charAt(2)),
                starter + 12, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getNumSprite(time.timeString.charAt(3)),
                starter + 17, 5, canvas);
    }

    private void dotcaseDrawPixel(int x, int y, Paint paint, Canvas canvas) {
        canvas.drawRect((x * DotcaseConstants.DOT_RATIO + 5),
                        (y * DotcaseConstants.DOT_RATIO + 5),
                        ((x + 1) * DotcaseConstants.DOT_RATIO - 5),
                        ((y + 1) * DotcaseConstants.DOT_RATIO - 5),
                        paint);
    }

    private void dotcaseDrawRect(int left, int top, int right,
                                 int bottom, int color, Canvas canvas) {
        for (int x=left; x < right; x++) {
            for (int y=top; y < bottom; y++) {
                dotcaseDrawPixel(x, y, DotcaseConstants.getPaintFromNumber(mPaint, color), canvas);
            }
        }
    }

    private void dotcaseDrawSprite(int[][] sprite, int x, int y, Canvas canvas) {
        for (int i=0; i < sprite.length; i++) {
            for (int j=0; j < sprite[0].length; j++) {
                dotcaseDrawPixel(x + j, y + i,
                        DotcaseConstants.getPaintFromNumber(mPaint, sprite[i][j]), canvas);
            }
        }
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(DotcaseConstants.ACTION_REDRAW)) {
                postInvalidate();
            }
        }
    };

    private void drawName(Canvas canvas) {
        int[][] sprite;
        int x = 0, y = 2;
        if (Dotcase.sStatus.isRinging()) {
            int nameOffset = Dotcase.sStatus.callerTicker();

            String name = Dotcase.sStatus.getCallerName();
            String correctedName = "";

            // We can fit 7 characters, and the last two are spaces
            if (name.length() <= 9) {
                // Name is short enough to be drawn completely, cut off spaces at end
                correctedName = name.substring(0, name.length() - 2);
            } else if ((nameOffset + 7) > name.length()) {
                // Looping: end and beginning of the name are visible together
                int overflow = (nameOffset + 7) % name.length();
                correctedName = name.substring(nameOffset) + name.substring(0, overflow);
            } else if ((nameOffset + 7) <= name.length()) {
                // Draw a consecutive portion of the name
                correctedName = name.substring(nameOffset, nameOffset + 7);
            }

            for (int i = 0; i < correctedName.length(); i++) {
                sprite = DotcaseConstants.getSmallCharSprite(correctedName.charAt(i));
                dotcaseDrawSprite(sprite, x + i * 4, y, canvas);
            }

            Dotcase.sStatus.incrementCallerTicker();
        }
    }

    private void drawNumber(Canvas canvas) {
        int[][] sprite;
        int x = 0, y = 8;
        if (Dotcase.sStatus.isRinging()) {
            String number = Dotcase.sStatus.getCallerNumber();
            for (int i = 3; i < number.length() && i < 10; i++) {
                sprite = DotcaseConstants.getSmallCharSprite(number.charAt(i));
                dotcaseDrawSprite(sprite, x + (i - 3) * 4, y, canvas);
            }
        }
    }

    private class timeObject {
        String timeString;
        int hour;
        int min;
        boolean is24Hour;
        boolean am;
    }
}
