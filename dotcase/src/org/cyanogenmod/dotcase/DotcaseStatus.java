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

import android.app.INotificationManager;
import android.content.Context;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.service.notification.StatusBarNotification;
import android.util.Log;

import java.util.List;
import java.util.Vector;

public class DotcaseStatus {

    private static final String TAG = "Dotcase";

    private boolean mRunning = true;
    private boolean mPocketed = false;
    private boolean mResetTimer = false;

    private boolean mRinging = false;
    private int mRingCounter = 0;
    private String mCallerNumber = "";
    private String mCallerName = "";
    private int mCallerTicker = 0;
    private boolean mAlarmClock = false;

    private boolean mStayOnTop = false;

    private List<Notification> mNotifications = new Vector<Notification>();

    synchronized boolean isRunning() {
        return mRunning;
    }

    synchronized void startRunning() {
        mRunning = true;
    }

    synchronized void stopRunning() {
        mRunning = false;
    }

    synchronized boolean isPocketed() {
        return mPocketed;
    }

    synchronized void setPocketed(boolean val) {
        mPocketed = val;
    }

    synchronized void resetTimer() {
        mResetTimer = true;
    }

    synchronized boolean isResetTimer() {
        boolean ret = mResetTimer;
        mResetTimer = false;
        return ret;
    }

    synchronized boolean isOnTop() {
        return mStayOnTop;
    }

    synchronized void setOnTop(boolean val) {
        mStayOnTop = val;
    }

    synchronized int ringCounter() {
        return mRingCounter;
    }

    synchronized void resetRingCounter() {
        mRingCounter = 0;
    }

    synchronized void incrementRingCounter() {
        mRingCounter++;
    }

    synchronized void startRinging(String number, String name) {
        mCallerName = name;
        startRinging(number);
    }

    synchronized void startRinging(String number) {
        mRinging = true;
        mResetTimer = true;
        mRingCounter = 0;
        mCallerNumber = number;
        mCallerTicker = -6;
    }

    synchronized void stopRinging() {
        mRinging = false;
        mCallerNumber = "";
        mCallerName = "";
    }

    synchronized int callerTicker() {
        if (mCallerTicker <= 0) {
            return 0;
        } else {
            return mCallerTicker;
        }
    }

    synchronized void incrementCallerTicker() {
        mCallerTicker++;
        if (mCallerTicker >= mCallerName.length()) {
            mCallerTicker = -3;
        }
    }

    synchronized void startAlarm() {
        mAlarmClock = true;
        mRingCounter = 0;
        mResetTimer = true;
    }

    synchronized void stopAlarm() {
        mAlarmClock = false;
    }

    synchronized boolean isRinging() {
        return mRinging;
    }

    synchronized boolean isAlarm() {
        return mAlarmClock;
    }

    synchronized boolean hasNotifications() {
        return mNotifications.isEmpty();
    }

    synchronized String getCallerName() {
        return mCallerName;
    }

    synchronized String getCallerNumber() {
        return mCallerNumber;
    }

    synchronized List<Notification> getNotifications() {
        return mNotifications;
    }

    synchronized void checkNotifications(Context context) {
        StatusBarNotification[] statusNotes = null;
        mNotifications.clear();

        try {
            INotificationManager notificationManager = INotificationManager.Stub.asInterface(
                    ServiceManager.getService(Context.NOTIFICATION_SERVICE));
            statusNotes = notificationManager.getActiveNotifications(context.getPackageName());
            for (StatusBarNotification statusNote : statusNotes) {
                Notification notification = DotcaseConstants.notificationMap.get(
                        statusNote.getPackageName());
                if (notification != null && !mNotifications.contains(notification)) {
                    mNotifications.add(notification);
                }
            }
        } catch (NullPointerException e) {
            Log.e(TAG, "Error retrieving notifications", e);
            mNotifications.clear();
        } catch (RemoteException e) {
            Log.e(TAG, "Error retrieving notifications", e);
            mNotifications.clear();
        }

        try {
            if (mNotifications.size() > 6) {
                mNotifications.subList(5, mNotifications.size()).clear();
                mNotifications.add(Notification.DOTS);
            }
        } catch (IndexOutOfBoundsException e) {
            // This should never happen...
            Log.e(TAG, "Error sublisting notifications, clearing to be safe", e);
            mNotifications.clear();
        }
    }
}
