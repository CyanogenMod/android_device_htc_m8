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

import java.util.HashMap;
import java.util.Map;

public class DotcaseConstants {
    static final String ACTION_KILL_ACTIVITY = "org.cyanogenmod.dotcase.KILL_ACTIVITY";
    static final String ACTION_REDRAW = "org.cyanogenmod.dotcase.REDRAW";
    static final int DOT_RATIO = 40;

    /**
     * Notification types
     */
    enum Notification {
        DOTS,
        EMAIL,
        TWITTER,
        MISSED_CALL,
        MMS,
        VOICEMAIL,
        COUPLES,
        GMAIL,
        HANGOUTS,
        SNAPCHAT,
        FACEBOOK,
        FB_MESSENGER,
        KIK,
        GROUPME,
        GPLUS
    }

    /**
     * Paints
     */

    static final Paint pTrans = new Paint();
    static final Paint pBlack = new Paint();
    static final Paint pWhite = new Paint();
    static final Paint pRed = new Paint();
    static final Paint pGreen = new Paint();
    static final Paint pBlue = new Paint();
    static final Paint pOrange = new Paint();
    static final Paint pPurple = new Paint();
    static final Paint pYellow = new Paint();
    static final Paint pGrey = new Paint();
    static final Paint pCyan = new Paint();
    static final Paint pDGreen = new Paint();
    static final Paint pDRed = new Paint();
    static final Paint pFYellow = new Paint();
    static final Paint pTBlue = new Paint();
    static final Paint pFbBlue = new Paint();
    static final Paint pPink = new Paint();
    static final Paint pGmBlue = new Paint();

    static {
        pTrans.setARGB(0, 0, 0, 0);
        pBlack.setARGB(255, 0, 0, 0);
        pWhite.setARGB(255, 255, 255, 255);
        pRed.setARGB(255, 255, 0, 0);
        pGreen.setARGB(255, 0, 255, 0);
        pBlue.setARGB(255, 0, 0, 255);
        pOrange.setARGB(255, 255, 165, 0);
        pPurple.setARGB(255, 160, 32, 240);
        pYellow.setARGB(255, 255, 255, 0);
        pGrey.setARGB(255, 69, 69, 69);
        pCyan.setARGB(255, 51, 181, 229);
        pDGreen.setARGB(255, 0, 128, 0);
        pDRed.setARGB(255, 128, 0, 0);
        pFYellow.setARGB(255, 255, 255, 153);
        pTBlue.setARGB(255, 85, 172, 238);
        pFbBlue.setARGB(255, 59, 89, 152);
        pPink.setARGB(255, 255, 100, 200);
        pGmBlue.setARGB(255, 0, 175, 240);
    }

    static Paint getPaintFromNumber(int color) {
        switch (color) {
            case -1:
                return pTrans;
            case 0:
                return pBlack;
            case 1:
                return pWhite;
            case 2:
                return pRed;
            case 3:
                return pGreen;
            case 4:
                return pBlue;
            case 5:
                return pOrange;
            case 6:
                return pPurple;
            case 7:
                return pYellow;
            case 8:
                return pGrey;
            case 9:
                return pCyan;
            case 10:
                return pDGreen;
            case 11:
                return pDRed;
            case 12:
                return pFYellow;
            case 13:
                return pTBlue;
            case 14:
                return pFbBlue;
            case 15:
                return pPink;
            case 16:
                return pGmBlue;
            default:
                return pBlack;
        }
    }

    /**
     * Notification map
     */

    static final Map<String, Notification> notificationMap;
    static {
        notificationMap = new HashMap<String, Notification>();

        // Email apps
        notificationMap.put("com.android.email", Notification.EMAIL);
        notificationMap.put("com.google.android.email", Notification.EMAIL);
        notificationMap.put("com.mailboxapp", Notification.EMAIL);
        notificationMap.put("com.yahoo.mobile.client.android.mail", Notification.EMAIL);
        notificationMap.put("com.outlook.Z7", Notification.EMAIL);
        notificationMap.put("com.maildroid", Notification.EMAIL);
        notificationMap.put("com.maildroid.pro", Notification.EMAIL);
        notificationMap.put("com.fsck.k9", Notification.EMAIL);

        // Twitter apps
        notificationMap.put("com.twitter.android", Notification.TWITTER);
        notificationMap.put("com.dotsandlines.carbon", Notification.TWITTER);
        notificationMap.put("com.levelup.touiteur", Notification.TWITTER); // Plume
        notificationMap.put("com.klinker.android.twitter", Notification.TWITTER); // Talon
        notificationMap.put("com.jv.falcon.pro", Notification.TWITTER);
        notificationMap.put("com.handmark.tweetcaster", Notification.TWITTER);
        notificationMap.put("com.handmark.tweetcaster.premium", Notification.TWITTER);

        // Missed call apps
        notificationMap.put("com.android.phone", Notification.MISSED_CALL);

        // MMS apps
        notificationMap.put("com.android.mms", Notification.MMS);
        notificationMap.put("com.p1.chompsms", Notification.MMS);
        notificationMap.put("com.handcent.nextsms", Notification.MMS);
        notificationMap.put("com.klinker.android.evolve_sms", Notification.MMS);

        // Voicemail apps
        notificationMap.put("com.google.android.apps.googlevoice", Notification.VOICEMAIL);

        // Couples apps
        notificationMap.put("com.tenthbit.juliet", Notification.COUPLES); // Couple
        notificationMap.put("io.avocado.android", Notification.COUPLES);
        notificationMap.put("kr.co.vcnc.android.couple", Notification.COUPLES); // Between

        // Other apps
        notificationMap.put("com.google.android.apps.plus", Notification.GPLUS);
        notificationMap.put("com.google.android.gm", Notification.GMAIL);
        notificationMap.put("com.google.android.talk", Notification.HANGOUTS);
        notificationMap.put("com.snapchat.android", Notification.SNAPCHAT);
        notificationMap.put("com.facebook.katana", Notification.FACEBOOK);
        notificationMap.put("com.facebook.orca", Notification.FB_MESSENGER);
        notificationMap.put("kik.android", Notification.KIK);
        notificationMap.put("com.groupme.android", Notification.GROUPME);
    }

    /**
     * Notification sprites
     */

    static final int[][] dotsSprite = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 1, 0, 1, 0, 1, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0}};

    static final int[][] hangoutsSprite = {
        {0, 3, 3, 3, 3, 3, 0},
        {3, 3, 3, 3, 3, 3, 3},
        {3, 3, 1, 3, 1, 3, 3},
        {3, 3, 1, 3, 1, 3, 3},
        {3, 3, 3, 3, 3, 3, 3},
        {0, 3, 3, 3, 3, 3, 0},
        {0, 0, 0, 3, 3, 0, 0},
        {0, 0, 0, 3, 0, 0, 0}};

    static final int[][] gPlusSprite = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 2, 2, 2, 2, 2, 0},
        {2, 2, 2, 1, 2, 2, 2},
        {2, 2, 2, 1, 2, 2, 2},
        {2, 1, 1, 1, 1, 1, 2},
        {2, 2, 2, 1, 2, 2, 2},
        {2, 2, 2, 1, 2, 2, 2},
        {0, 2, 2, 2, 2, 2, 0}};

    static final int[][] mmsSprite = {
        {0, 0, 0, 0, 0, 0, 0},
        {3, 3, 3, 3, 3, 3, 3},
        {3, 3, 3, 3, 3, 3, 3},
        {3, 3, 3, 3, 3, 3, 3},
        {3, 3, 3, 3, 3, 3, 3},
        {3, 3, 3, 3, 3, 3, 3},
        {0, 3, 3, 0, 0, 0, 0},
        {0, 3, 0, 0, 0, 0, 0}};

    static final int[][] twitterSprite = {
        {0, 0,  0,  0,  0,  0, 0},
        {0, 0, 13, 13,  0,  0, 0},
        {0, 0, 13, 13, 13, 13, 0},
        {0, 0, 13, 13, 13, 13, 0},
        {0, 0, 13, 13,  0,  0, 0},
        {0, 0, 13, 13, 13, 13, 0},
        {0, 0,  0, 13, 13, 13, 0},
        {0, 0,  0,  0,  0,  0, 0}};

    static final int[][] voicemailSprite = { // this icon is shit
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 7, 7, 0, 7, 7, 0},
        {7, 0, 0, 7, 0, 0, 7},
        {7, 0, 0, 7, 0, 0, 7},
        {0, 7, 7, 7, 7, 7, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0}};

    static final int[][] missedCallSprite = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 1, 0, 0, 0, 1, 0},
        {0, 0, 1, 0, 1, 0, 0},
        {0, 0, 0, 1, 0, 0, 0},
        {0, 7, 7, 7, 7, 7, 0},
        {7, 7, 7, 7, 7, 7, 7},
        {7, 7, 0, 0, 0, 7, 7},
        {0, 0, 0, 0, 0, 0, 0}};

    static final int[][] emailSprite = {
        {0, 0, 1, 1, 1, 1, 0},
        {0, 1, 0, 0, 0, 0, 1},
        {1, 0, 0, 1, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1},
        {1, 0, 0, 1, 1, 1, 0},
        {0, 1, 0, 0, 0, 0, 0},
        {0, 0, 1, 1, 1, 1, 0}};

    static final int[][] gmailSprite = {
        {0, 0, 0, 0, 0, 0, 0},
        {2, 1, 1, 1, 1, 1, 2},
        {2, 2, 1, 1, 1, 2, 2},
        {2, 1, 2, 1, 2, 1, 2},
        {2, 1, 1, 2, 1, 1, 2},
        {2, 1, 1, 1, 1, 1, 2},
        {2, 1, 1, 1, 1, 1, 2},
        {0, 0, 0, 0, 0, 0, 0}};

    static final int[][] snapchatSprite = {
        {0, 0, 7, 7, 7, 0, 0},
        {0, 7, 7, 7, 7, 7, 0},
        {0, 7, 7, 7, 7, 7, 0},
        {7, 7, 7, 7, 7, 7, 7},
        {0, 7, 7, 7, 7, 7, 0},
        {0, 7, 7, 7, 7, 7, 0},
        {0, 7, 7, 7, 7, 7, 0},
        {0, 7, 0, 7, 0, 7, 0}};

    static final int[][] facebookSprite = {
        { 0, 14, 14, 14, 14, 14,  0},
        {14, 14, 14, 14, 14,  1, 14},
        {14, 14, 14, 14,  1, 14, 14},
        {14, 14, 14, 14,  1, 14, 14},
        {14, 14, 14,  1,  1,  1, 14},
        {14, 14, 14, 14,  1, 14, 14},
        {14, 14, 14, 14,  1, 14, 14},
        { 0, 14, 14, 14,  1, 14,  0}};

    static final int[][] couplesSprite = {
        { 0, 15,  0, 15,  0,  0,  0},
        {15, 15, 15, 15, 15,  0,  0},
        {15, 15, 15, 15, 15,  0,  0},
        { 0, 15, 15, 15,  2,  0,  0},
        { 0,  0, 15,  2,  2,  2,  0},
        { 0,  0,  2,  2,  2,  2,  2},
        { 0,  0,  2,  2,  2,  2,  2},
        { 0,  0,  0,  2,  0,  2,  0}};

    static final int[][] kikSprite = {
        {0, 3, 0, 0, 0, 0, 0, 0},
        {0, 3, 0, 0, 3, 0, 0, 0},
        {0, 3, 0, 3, 0, 0, 0, 0},
        {0, 3, 3, 0, 0, 4, 4, 0},
        {0, 3, 3, 0, 0, 4, 4, 0},
        {0, 3, 0, 3, 0, 0, 0, 0},
        {0, 3, 0, 0, 3, 0, 0, 0},
        {0, 3, 0, 0, 0, 3, 0, 0}};

    static final int[][] facebookMessengerSprite = {
        { 0, 14, 14, 14, 14, 14,  0},
        {14, 14, 14, 14, 14, 14, 14},
        {14, 14,  1, 14, 14, 14, 14},
        {14,  1, 14,  1, 14,  1, 14},
        {14, 14, 14, 14,  1, 14, 14},
        {14, 14, 14, 14, 14, 14, 14},
        { 0, 14, 14, 14, 14, 14,  0},
        { 0,  0, 14,  0,  0,  0,  0}};

    static final int[][] groupMeSprite = {
        {16, 16, 16, 16, 16, 16, 16},
        {16, 16,  1, 16,  1, 16, 16},
        {16,  1,  1,  1,  1,  1, 16},
        {16, 16,  1, 16,  1, 16, 16},
        {16,  1,  1,  1,  1,  1, 16},
        {16, 16,  1, 16,  1, 16, 16},
        {16, 16, 16, 16, 16, 16, 16},
        { 0,  0,  0, 16,  0,  0,  0}};

    static int[][] getNotificationSprite(Notification notification) {
        switch (notification) {
            case DOTS:
                return dotsSprite;
            case EMAIL:
                return emailSprite;
            case GMAIL:
                return gmailSprite;
            case HANGOUTS:
                return hangoutsSprite;
            case TWITTER:
                return twitterSprite;
            case MISSED_CALL:
                return missedCallSprite;
            case MMS:
                return mmsSprite;
            case VOICEMAIL:
                return voicemailSprite;
            case SNAPCHAT:
                return snapchatSprite;
            case FACEBOOK:
                return facebookSprite;
            case COUPLES:
                return couplesSprite;
            case KIK:
                return kikSprite;
            case FB_MESSENGER:
                return facebookMessengerSprite;
            case GROUPME:
                return groupMeSprite;
            case GPLUS:
                return gPlusSprite;
            default:
                return null;
        }
    }

    /**
     * Big number sprites
     */

    static final int[][] big0 = {
        {-1,  9,  9, -1},
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

    static final int[][] big1 = {
        {-1, -1,  9, -1},
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

    static final int[][] big2 = {
        {-1,  9,  9, -1},
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

    static final int[][] big3 = {
        {-1,  9,  9, -1},
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

    static final int[][] big4 = {
        { 9, -1, -1,  9},
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

    static final int[][] big5 = {
        { 9,  9,  9,  9},
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

    static final int[][] big6 = {
        {-1,  9,  9, -1},
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

    static final int[][] big7 = {
        { 9,  9,  9,  9},
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

    static final int[][] big8 = {
        {-1,  9,  9, -1},
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

    static final int[][] big9 = {
        {-1,  9,  9, -1},
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

    static final int[][] bigNull = {
        {-1, -1, -1, -1},
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

    static int[][] getNumSprite(char c) {
        switch (c) {
            case '0':
                return big0;
            case '1':
                return big1;
            case '2':
                return big2;
            case '3':
                return big3;
            case '4':
                return big4;
            case '5':
                return big5;
            case '6':
                return big6;
            case '7':
                return big7;
            case '8':
                return big8;
            case '9':
                return big9;
            default:
                return bigNull;
        }
    }

    /**
     * Small number sprites
     */

    static final int[][] small0 = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] small1 = {
        {-1,  9, -1},
        { 9,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1},
        { 9,  9,  9}};

    static final int[][] small2 = {
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9}};

    static final int[][] small3 = {
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9}};

    static final int[][] small4 = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        {-1, -1,  9},
        {-1, -1,  9}};

    static final int[][] small5 = {
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9}};

    static final int[][] small6 = {
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] small7 = {
        { 9,  9,  9},
        {-1, -1,  9},
        {-1,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1}};

    static final int[][] small8 = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] small9 = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9}};

    static final int[][] smallA = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallB = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9, -1},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] smallC = {
        {-1,  9,  9},
        { 9, -1, -1},
        { 9, -1, -1},
        { 9, -1, -1},
        {-1,  9,  9}};

    static final int[][] smallD = {
        { 9,  9, -1},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9, -1}};

    static final int[][] smallE = {
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9}};

    static final int[][] smallF = {
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9},
        { 9, -1, -1},
        { 9, -1, -1}};

    static final int[][] smallG = {
        {-1,  9,  9},
        { 9, -1, -1},
        { 9, -1,  9},
        { 9, -1,  9},
        {-1,  9,  9}};

    static final int[][] smallH = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallI = {
        { 9,  9,  9},
        {-1,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1},
        { 9,  9,  9}};

    static final int[][] smallJ = {
        {-1,  9,  9},
        {-1, -1,  9},
        {-1, -1,  9},
        { 9, -1,  9},
        {-1,  9, -1}};

    static final int[][] smallK = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9, -1},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallL = {
        { 9, -1, -1},
        { 9, -1, -1},
        { 9, -1, -1},
        { 9, -1, -1},
        { 9,  9,  9}};

    static final int[][] smallM = {
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallN = {
        { 9,  9, -1},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallO = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] smallP = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1, -1},
        { 9, -1, -1}};

    static final int[][] smallQ = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        {-1, -1,  9}};

    static final int[][] smallR = {
        { 9,  9,  9},
        { 9, -1,  9},
        { 9,  9, -1},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallS = {
        { 9,  9,  9},
        { 9, -1, -1},
        { 9,  9,  9},
        {-1, -1,  9},
        { 9,  9,  9}};

    static final int[][] smallT = {
        { 9,  9,  9},
        {-1,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1}};

    static final int[][] smallU = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9}};

    static final int[][] smallV = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        {-1,  9, -1}};

    static final int[][] smallW = {
        { 9, -1,  9},
        { 9, -1,  9},
        { 9, -1,  9},
        { 9,  9,  9},
        { 9, -1,  9}};

    static final int[][] smallX = {
        { 9, -1,  9},
        { 9, -1,  9},
        {-1,  9, -1},
        { 9, -1,  9},
        { 9, -1,  9}};

    static final int[][] smallY = {
        { 9, -1,  9},
        { 9, -1,  9},
        {-1,  9, -1},
        {-1,  9, -1},
        {-1,  9, -1}};

    static final int[][] smallZ = {
        { 9,  9,  9},
        {-1, -1,  9},
        {-1,  9, -1},
        { 9, -1, -1},
        { 9,  9,  9}};

    static final int[][] smallNull = {
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}};

    static int[][] getSmallCharSprite(char c) {
        switch (c) {
            case '0':
                return small0;
            case '1':
                return small1;
            case '2':
                return small2;
            case '3':
                return small3;
            case '4':
                return small4;
            case '5':
                return small5;
            case '6':
                return small6;
            case '7':
                return small7;
            case '8':
                return small8;
            case '9':
                return small9;
            case 'a':
                return smallA;
            case 'b':
                return smallB;
            case 'c':
                return smallC;
            case 'd':
                return smallD;
            case 'e':
                return smallE;
            case 'f':
                return smallF;
            case 'g':
                return smallG;
            case 'h':
                return smallH;
            case 'i':
                return smallI;
            case 'j':
                return smallJ;
            case 'k':
                return smallK;
            case 'l':
                return smallL;
            case 'm':
                return smallM;
            case 'n':
                return smallN;
            case 'o':
                return smallO;
            case 'p':
                return smallP;
            case 'q':
                return smallQ;
            case 'r':
                return smallR;
            case 's':
                return smallS;
            case 't':
                return smallT;
            case 'u':
                return smallU;
            case 'v':
                return smallV;
            case 'w':
                return smallW;
            case 'x':
                return smallX;
            case 'y':
                return smallY;
            case 'z':
                return smallZ;
            default:
                return smallNull;
        }
    }

    /**
     * Various sprites
     */

    static final int[][] handsetSprite = {
        {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
        {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
        {3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3},
        {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0},
        {0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0}};

    static final int[][] ringerSprite = {
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

    static final int[][] lightningSprite = {
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

    static final int[][] clockSprite = {
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

    static final int[][] snoozeArray = {
        {12, 12, 12, 0, 12, 12,  0, 0, 12, 12, 12, 0, 12, 12, 12, 0, 12, 12, 12, 0, 12, 12, 12},
        {12,  0,  0, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0,  0,  0, 12, 0, 12,  0,  0},
        {12, 12, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0,  0, 12,  0, 0, 12, 12, 12},
        { 0,  0, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0,  0, 0, 12,  0,  0},
        {12, 12, 12, 0, 12,  0, 12, 0, 12, 12, 12, 0, 12, 12, 12, 0, 12, 12, 12, 0, 12, 12, 12}};

    static final int[][] alarmCancelArray = {
        { 0, 12, 12, 0, 12, 12, 12, 0, 12, 12,  0, 0,  0, 12, 12, 0, 12, 12, 12, 0, 12,  0,  0},
        {12,  0,  0, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0,  0, 0, 12,  0,  0, 0, 12,  0,  0},
        {12,  0,  0, 0, 12, 12, 12, 0, 12,  0, 12, 0, 12,  0,  0, 0, 12, 12, 12, 0, 12,  0,  0},
        {12,  0,  0, 0, 12,  0, 12, 0, 12,  0, 12, 0, 12,  0,  0, 0, 12,  0,  0, 0, 12,  0,  0},
        { 0, 12, 12, 0, 12,  0, 12, 0, 12,  0, 12, 0,  0, 12, 12, 0, 12, 12, 12, 0, 12, 12, 12}};

    static final int[][] timeColon = {
        {9},
        {0},
        {0},
        {9}};

    static final int[][] smallTimeColon = {
        {9},
        {0},
        {9}};

    static final int[][] amSprite = {
        {9, 9, 9, 0, 9, 9, 0, 9, 9},
        {9, 0, 9, 0, 9, 0, 9, 0, 9},
        {9, 9, 9, 0, 9, 0, 9, 0, 9},
        {9, 0, 9, 0, 9, 0, 0, 0, 9},
        {9, 0, 9, 0, 9, 0, 0, 0, 9}};

    static final int[][] pmSprite = {
        {9, 9, 9, 0, 9, 9, 0, 9, 9},
        {9, 0, 9, 0, 9, 0, 9, 0, 9},
        {9, 9, 9, 0, 9, 0, 9, 0, 9},
        {9, 0, 0, 0, 9, 0, 0, 0, 9},
        {9, 0, 0, 0, 9, 0, 0, 0, 9}};

    static final int[][] batteryOutlineSprite = {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0}};
}
