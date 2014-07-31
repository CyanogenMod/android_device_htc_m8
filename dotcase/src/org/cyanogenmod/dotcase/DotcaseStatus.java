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

    private boolean running = true;
    private boolean pocketed = false;
    private boolean resetTimer = false;

    private boolean ringing = false;
    private int ringCounter = 0;
    private String phoneNumber = "";
    private boolean torchStatus = false;
    private boolean alarmClock = false;

    private boolean stayOnTop = false;

    private List<Notification> notifications = new Vector<Notification>();

    synchronized boolean isRunning() {
        return running;
    }

    synchronized void startRunning() {
        running = true;
    }

    synchronized void stopRunning() {
        running = false;
    }

    synchronized boolean isPocketed() {
        return pocketed;
    }

    synchronized void setPocketed(boolean val) {
        pocketed = val;
    }

    synchronized void resetTimer() {
        resetTimer = true;
    }

    synchronized boolean isResetTimer() {
        boolean ret = resetTimer;
        resetTimer = false;
        return ret;
    }

    synchronized boolean isOnTop() {
        return stayOnTop;
    }

    synchronized void setOnTop(boolean val) {
        stayOnTop = val;
    }

    synchronized int ringCounter() {
        return ringCounter;
    }

    synchronized void resetRingCounter() {
        ringCounter = 0;
    }

    synchronized void incrementRingCounter() {
        ringCounter++;
    }

    synchronized void startRinging(String number) {
        ringing = true;
        resetTimer = true;
        ringCounter = 0;
        phoneNumber = number;
    }

    synchronized void stopRinging() {
        ringing = false;
        phoneNumber = "";
    }

    synchronized void startAlarm() {
        alarmClock = true;
        ringCounter = 0;
        resetTimer = true;
    }

    synchronized void stopAlarm() {
        alarmClock = false;
    }

    synchronized boolean isRinging() {
        return ringing;
    }

    synchronized boolean isAlarm() {
        return alarmClock;
    }

    synchronized boolean isTorch() {
        return torchStatus;
    }

    synchronized void setTorch(boolean val) {
        torchStatus = val;
    }

    synchronized boolean hasNotifications() {
        return notifications.isEmpty();
    }

    synchronized String getPhoneNumber() {
        return phoneNumber;
    }

    synchronized List<Notification> getNotifications() {
        return notifications;
    }

    synchronized void checkNotifications(Context context) {
        StatusBarNotification[] statusNotes = null;
        notifications.clear();

        try {
            INotificationManager notificationManager = INotificationManager.Stub.asInterface(
                    ServiceManager.getService(Context.NOTIFICATION_SERVICE));
            statusNotes = notificationManager.getActiveNotifications(context.getPackageName());
            for (StatusBarNotification statusNote : statusNotes) {
                Notification notification = DotcaseConstants.notificationMap.get(
                        statusNote.getPackageName());
                if (notification != null && !notifications.contains(notification)) {
                    notifications.add(notification);
                }
            }
        } catch (NullPointerException e) {
            Log.e(TAG, "Error retrieving notifications", e);
            notifications.clear();
        } catch (RemoteException e) {
            Log.e(TAG, "Error retrieving notifications", e);
            notifications.clear();
        }

        try {
            if (notifications.size() > 6) {
                notifications.subList(5, notifications.size()).clear();
                notifications.add(Notification.DOTS);
            }
        } catch (IndexOutOfBoundsException e) {
            // This should never happen...
            Log.e(TAG, "Error sublisting notifications, clearing to be safe", e);
            notifications.clear();
        }
    }
}
