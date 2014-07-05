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
import android.text.format.DateFormat;
import android.view.View;

import java.util.Arrays;
import java.util.Calendar;
import java.util.Collections;

public class DrawView extends View {
    // 1920x1080 = 48 x 27 dots @ 40 pixels per dot
    private final Context mContext;
    private final IntentFilter filter = new IntentFilter();
    private int heartbeat = 0;

    public static int ringCounter;

    public DrawView(Context context) {
        super(context);
        Paint paint = new Paint();
        mContext = context;
        paint.setAntiAlias(true);
    }

    @Override
    public void onDraw(Canvas canvas) {
        if (Dotcase.alarm_clock) {
            drawAlarm(canvas);
        } else if (Dotcase.ringing) {
            drawNumber(canvas);
            drawRinger(canvas);
        } else {
            if (Dotcase.torchStatus) {
                dotcaseDrawSprite(DotcaseConstants.torchSprite, 19, 22, canvas);
            }
            drawTime(canvas);

            // Check notifications each cycle before displaying them
            if (heartbeat == 0) {
                Dotcase.checkNotifications();
            }

            if (Dotcase.gmail || Dotcase.hangouts || Dotcase.mms || Dotcase.missed_call
                              || Dotcase.twitter || Dotcase.voicemail) {
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
        }

        filter.addAction(DotcaseConstants.ACTION_REDRAW);
        mContext.getApplicationContext().registerReceiver(receiver, filter);
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
        int clockLength = DotcaseConstants.clockSprite.length;
        int clockElementLength = DotcaseConstants.clockSprite[0].length;
        int ringerLength = DotcaseConstants.ringerSprite.length;
        int ringerElementLength = DotcaseConstants.ringerSprite[0].length;
        timeObject time = getTimeObject();

        int[][] mClockSprite = new int[clockLength][clockElementLength];
        int[][] mRingerSprite = new int[ringerLength][ringerElementLength];

        for (int i = 0; i < ringerLength; i++) {
            for (int j = 0; j < ringerElementLength; j++) {
                if (DotcaseConstants.ringerSprite[i][j] > 0) {
                    mRingerSprite[i][j] =
                            DotcaseConstants.ringerSprite[i][j] == 3 - (ringCounter % 3)
                                    ? light : dark;
                }
            }
        }

        for (int i = 0; i < clockLength; i++) {
            for (int j = 0; j < clockElementLength; j++) {
                mClockSprite[i][j] = DotcaseConstants.clockSprite[i][j] > 0 ? light : 0;
            }
        }

        dotcaseDrawSprite(DotcaseConstants.getSmallSprite(
                time.timeString.charAt(0)), 0, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallSprite(
                time.timeString.charAt(1)), 4, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.smallTimeColon, 8, 1, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallSprite(
                time.timeString.charAt(2)), 11, 0, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSmallSprite(
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
            ringCounter = 0;
        } else {
            ringCounter++;
        }
    }

    private void drawNotifications(Canvas canvas) {
        int count = 0;
        int x = 1;
        int y = 30;
        if (Dotcase.missed_call) {
            dotcaseDrawSprite(DotcaseConstants.missedCallSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
            count++;
        }

        if (Dotcase.voicemail) {
            dotcaseDrawSprite(DotcaseConstants.voicemailSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
            count++;
        }

        if (Dotcase.gmail) {
            dotcaseDrawSprite(DotcaseConstants.gmailSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
            count++;
        }

        if (Dotcase.hangouts) {
            dotcaseDrawSprite(DotcaseConstants.hangoutsSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
            count++;
        }

        if (Dotcase.mms) {
            dotcaseDrawSprite(DotcaseConstants.mmsSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
            count++;
        }

        if (Dotcase.twitter) {
            dotcaseDrawSprite(DotcaseConstants.twitterSprite,
                    x + ((count % 3) * 9), y + ((count / 3) * 9), canvas);
        }
    }

    private void drawRinger(Canvas canvas) {
        int light, dark;
        int handsetLength = DotcaseConstants.handsetSprite.length;
        int handsetElementLength = DotcaseConstants.handsetSprite[0].length;
        int ringerLength = DotcaseConstants.ringerSprite.length;
        int ringerElementLength = DotcaseConstants.ringerSprite[0].length;

        int[][] mHandsetSprite = new int[handsetLength][handsetElementLength];
        int[][] mRingerSprite = new int[ringerLength][ringerElementLength];

        if (ringCounter /3 > 0) {
            light = 2;
            dark = 11;
        } else {
            light = 3;
            dark = 10;
        }

        for (int i = 0; i < ringerLength; i++) {
            for (int j = 0; j < ringerElementLength; j++) {
                if (DotcaseConstants.ringerSprite[i][j] > 0) {
                    mRingerSprite[i][j] =
                            DotcaseConstants.ringerSprite[i][j] == 3 - (ringCounter % 3)
                                    ? light : dark;
                }
            }
        }

        for (int i = 0; i < handsetLength; i++) {
            for (int j = 0; j < handsetElementLength; j++) {
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
            ringCounter = 0;
        } else {
            ringCounter++;
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
        dotcaseDrawSprite(DotcaseConstants.getSprite(time.timeString.charAt(0)),
                starter, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSprite(time.timeString.charAt(1)),
                starter + 5, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSprite(time.timeString.charAt(2)),
                starter + 12, 5, canvas);
        dotcaseDrawSprite(DotcaseConstants.getSprite(time.timeString.charAt(3)),
                starter + 17, 5, canvas);
    }

    private void dotcaseDrawPixel(int x, int y, Paint paint, Canvas canvas) {
        canvas.drawRect((x * DotcaseConstants.dotratio + 5),
                        (y * DotcaseConstants.dotratio + 5),
                        ((x + 1) * DotcaseConstants.dotratio -5),
                        ((y + 1) * DotcaseConstants.dotratio -5),
                        paint);
    }

    private void dotcaseDrawRect(int left, int top, int right,
                                 int bottom, int color, Canvas canvas) {
        for (int x=left; x < right; x++) {
            for (int y=top; y < bottom; y++) {
                dotcaseDrawPixel(x, y, DotcaseConstants.getPaintFromNumber(color), canvas);
            }
        }
    }

    private void dotcaseDrawSprite(int[][] sprite, int x, int y, Canvas canvas) {
        for (int i=0; i < sprite.length; i++) {
            for (int j=0; j < sprite[0].length; j++) {
                dotcaseDrawPixel(x + j, y + i,
                        DotcaseConstants.getPaintFromNumber(sprite[i][j]), canvas);
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

    private void drawNumber(Canvas canvas) {
        int[][] sprite;
        int x = 0, y = 5;
        if (Dotcase.ringing) {
            for (int i = 3; i < Dotcase.phoneNumber.length(); i++) {
                sprite = DotcaseConstants.getSmallSprite(Dotcase.phoneNumber.charAt(i));
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
