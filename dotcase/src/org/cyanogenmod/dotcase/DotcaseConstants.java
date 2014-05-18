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

import android.graphics.Paint;

public class DotcaseConstants {
    static String ACTION_KILL_ACTIVITY = "org.cyanogenmod.dotcase.KILL_ACTIVITY";
    static String ACTION_REDRAW = "org.cyanogenmod.dotcase.REDRAW";

    static int[][] handsetSprite = {
            {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
            {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
            {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
            {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0},
            {0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0}};

    static int[][] ringerSprite = {
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

    static int[][] lightningSprite = {
            {-1, -1, -1, -1,  0,  0, -1, -1},
            {-1, -1, -1,  0,  7,  0, -1, -1},
            {-1, -1,  0,  7,  7,  0, -1, -1},
            {-1,  0,  7,  7,  7,  0, -1, -1},
            { 0,  7,  7,  7,  7,  0,  0,  0},
            { 0,  0,  0,  7,  7,  7,  7,  0},
            {-1, -1,  0,  7,  7,  7,  0, -1},
            {-1, -1,  0,  7,  7,  0, -1, -1},
            {-1, -1,  0,  7,  0, -1, -1, -1},
            {-1, -1,  0,  0, -1, -1, -1, -1}};

    static int[][] clockSprite = {
            { 0, 12, 12, 12,  0,  0,  0,  0,  0, 12, 12, 12,  0},
            {12, 12, 12,  0,  0,  0,  0,  0,  0,  0, 12, 12, 12},
            {12, 12,  0,  0,  0,  0,  0,  0,  0,  0,  0, 12, 12},
            {12,  0,  0,  0, 12, 12, 12, 12, 12,  0,  0,  0, 12},
            { 0,  0,  0, 12,  0,  0,  0,  0,  0, 12,  0,  0,  0},
            { 0,  0, 12,  0,  0,  0, 12,  0,  0,  0, 12,  0,  0},
            { 0,  0, 12,  0,  0,  0, 12,  0,  0,  0, 12,  0,  0},
            { 0,  0, 12,  0,  0,  0, 12, 12, 12,  0, 12,  0,  0},
            { 0,  0, 12,  0,  0,  0,  0,  0,  0,  0, 12,  0,  0},
            { 0,  0, 12,  0,  0,  0,  0,  0,  0,  0, 12,  0,  0},
            { 0,  0,  0, 12,  0,  0,  0,  0,  0, 12,  0,  0,  0},
            { 0,  0,  0,  0, 12, 12, 12, 12, 12,  0,  0,  0,  0},
            { 0,  0,  0, 12,  0,  0,  0,  0,  0, 12,  0,  0,  0}};

    static int[][] snoozeArray = {
            {12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 12},
            {12,  0,  0,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0,  0,  0, 12,  0, 12,  0,  0},
            {12, 12, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0,  0, 12,  0,  0, 12, 12, 12},
            { 0,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0, 12,  0,  0,  0, 12,  0,  0},
            {12, 12, 12,  0, 12,  0, 12,  0, 12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 21,  0, 12, 12, 12}};

    static int[][] sleepArray = {
            {12, 12, 12,  0, 12,  0,  0,  0, 12, 12, 12, 0, 12, 12, 12,  0, 12, 12, 12},
            {12,  0,  0,  0, 12,  0,  0,  0, 12,  0,  0, 0, 12,  0,  0,  0, 12,  0, 12},
            {12, 12, 12,  0, 12,  0,  0,  0, 12, 12, 12, 0, 12, 12, 12,  0, 12, 12, 12},
            { 0,  0, 12,  0, 12,  0,  0,  0, 12,  0,  0, 0, 12,  0,  0,  0, 12,  0,  0},
            {12, 12, 12,  0, 12, 12, 12,  0, 12, 12, 12, 0, 12, 12, 12,  0, 12,  0,  0}};

    static int[][] hangoutsSprite = {
            {0, 0, 0, 0, 0, 0, 0},
            {0, 3, 3, 3, 3, 3, 0},
            {3, 3, 1, 3, 1, 3, 3},
            {3, 3, 1, 3, 1, 3, 3},
            {0, 3, 3, 3, 3, 3, 0},
            {0, 0, 0, 3, 3, 0, 0},
            {0, 0, 0, 3, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0}};

    static int[][] mmsSprite = {
            {0, 0, 0, 0, 0, 0, 0},
            {3, 3, 3, 3, 3, 3, 3},
            {3, 3, 3, 3, 3, 3, 3},
            {3, 3, 3, 3, 3, 3, 3},
            {3, 3, 3, 3, 3, 3, 3},
            {3, 3, 3, 3, 3, 3, 3},
            {0, 3, 3, 0, 0, 0, 0},
            {0, 3, 0, 0, 0, 0, 0}};

    static int[][] twitterSprite = {
            {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 9, 9, 0, 0, 0},
            {0, 0, 9, 9, 9, 9, 0},
            {0, 0, 9, 9, 9, 9, 0},
            {0, 0, 9, 9, 0, 0, 0},
            {0, 0, 9, 9, 9, 9, 0},
            {0, 0, 0, 9, 9, 9, 0},
            {0, 0, 0, 0, 0, 0, 0}};

    static int[][] voicemailSprite = { // this icon is shit
            {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0},
            {0, 7, 7, 0, 7, 7, 0},
            {7, 0, 0, 7, 0, 0, 7},
            {7, 0, 0, 7, 0, 0, 7},
            {0, 7, 7, 7, 7, 7, 0},
            {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0}};

    static int[][] missedCallSprite = {
            {0, 0, 0, 0, 0, 0, 0},
            {0, 1, 0, 0, 0, 1, 0},
            {0, 0, 1, 0, 1, 0, 0},
            {0, 0, 0, 1, 0, 0, 0},
            {0, 7, 7, 7, 7, 7, 0},
            {7, 7, 7, 7, 7, 7, 7},
            {7, 7, 0, 0, 0, 7, 7},
            {0, 0, 0, 0, 0, 0, 0}};

    static int[][] gmailSprite = {
            {0, 0, 0, 0, 0, 0, 0},
            {2, 1, 1, 1, 1, 1, 2},
            {2, 2, 1, 1, 1, 2, 2},
            {2, 1, 2, 1, 2, 1, 2},
            {2, 1, 1, 2, 1, 1, 2},
            {2, 1, 1, 1, 1, 1, 2},
            {2, 1, 1, 1, 1, 1, 2},
            {0, 0, 0, 0, 0, 0, 0}};

    static int[][] timeColon = {
            {9},
            {0},
            {0},
            {9}};

    static int[][] smallTimeColon = {
            {9},
            {0},
            {9}};

    static int[][] amSprite = {
            {9, 9, 9, 0, 9, 9, 0, 9, 9},
            {9, 0, 9, 0, 9, 0, 9, 0, 9},
            {9, 9, 9, 0, 9, 0, 9, 0, 9},
            {9, 0, 9, 0, 9, 0, 0, 0, 9},
            {9, 0, 9, 0, 9, 0, 0, 0, 9}};

    static int[][] pmSprite = {
            {9, 9, 9, 0, 9, 9, 0, 9, 9},
            {9, 0, 9, 0, 9, 0, 9, 0, 9},
            {9, 9, 9, 0, 9, 0, 9, 0, 9},
            {9, 0, 0, 0, 9, 0, 0, 0, 9},
            {9, 0, 0, 0, 9, 0, 0, 0, 9}};

    static int[][] getSprite(char c) {
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

    static int[][] getSmallSprite(char c) {
        int[][] sprite;
        switch (c) {
            case '0': sprite = new int[][]
                    {{ 9,  9,  9},
                            { 9, -1,  9},
                            { 9, -1,  9},
                            { 9, -1,  9},
                            { 9,  9,  9}};
                break;
            case '1': sprite = new int[][]
                    {{ 9,  9, -1},
                            {-1,  9, -1},
                            {-1,  9, -1},
                            {-1,  9, -1},
                            { 9,  9,  9}};
                break;
            case '2': sprite = new int[][]
                    {{ 9,  9,  9},
                            {-1, -1,  9},
                            { 9,  9,  9},
                            { 9, -1, -1},
                            { 9,  9,  9}};
                break;
            case '3': sprite = new int[][]
                    {{ 9,  9,  9},
                            {-1, -1,  9},
                            { 9,  9,  9},
                            {-1, -1,  9},
                            { 9,  9,  9}};
                break;
            case '4': sprite = new int[][]
                    {{ 9, -1,  9},
                            { 9, -1,  9},
                            { 9,  9,  9},
                            {-1, -1,  9},
                            {-1, -1,  9}};
                break;
            case '5': sprite = new int[][]
                    {{ 9,  9,  9},
                            { 9, -1, -1},
                            { 9,  9,  9},
                            {-1, -1,  9},
                            { 9,  9,  9}};
                break;
            case '6': sprite = new int[][]
                    {{ 9,  9,  9},
                            { 9, -1, -1},
                            { 9,  9,  9},
                            { 9, -1,  9},
                            { 9,  9,  9}};
                break;
            case '7': sprite = new int[][]
                    {{ 9,  9,  9},
                            {-1, -1,  9},
                            {-1, -1,  9},
                            {-1, -1,  9},
                            {-1, -1,  9}};
                break;
            case '8': sprite = new int[][]
                    {{ 9,  9,  9},
                            { 9, -1,  9},
                            { 9,  9,  9},
                            { 9, -1,  9},
                            { 9,  9,  9}};
                break;
            case '9': sprite = new int[][]
                    {{ 9,  9,  9},
                            { 9, -1,  9},
                            { 9,  9,  9},
                            {-1, -1,  9},
                            { 9,  9,  9}};
                break;
            default: sprite = new int[][]
                    {{-1, -1, -1},
                            {-1, -1, -1},
                            {-1, -1, -1},
                            {-1, -1, -1},
                            {-1, -1, -1}};
                break;
        }

        return sprite;
    }

    static Paint getPaintFromNumber(int color) {
        Paint paint = new Paint();
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
            case 12: // faded yellow
                paint.setARGB(255, 255, 255, 153);
                break;
            default: // black
                paint.setARGB(255, 0, 0, 0);
                break;
        }

        return paint;
    }
}
