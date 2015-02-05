/*
 * Copyright (C) 2015 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.internal.telephony;

import static com.android.internal.telephony.RILConstants.*;

import android.content.Context;
//import android.os.AsyncResult;
import android.os.Parcel;

/**
 * Custom Qualcomm RIL for HTC M8
 *
 * {@hide}
 */
public class HtcM8RIL extends RIL implements CommandsInterface {
    static final String LOG_TAG = "HtcM8RIL";

    static final int RIL_UNSOL_VOICE_SPEECH_CODEC = 3065;

/*
    Unused ATM. See comments below.
    protected RegistrantList mVoiceSpeechCodecInfoRegistransts = new RegistrantList();
*/

    public HtcM8RIL(Context context, int preferredNetworkType,
            int cdmaSubscription, Integer instanceId) {
        this(context, preferredNetworkType, cdmaSubscription);
    }

    public HtcM8RIL(Context context, int networkMode, int cdmaSubscription) {
        super(context, networkMode, cdmaSubscription);
    }

    @Override
    protected void
    processUnsolicited (Parcel p) {
        Object ret;
        int dataPosition = p.dataPosition(); // save off position within the Parcel
        int response = p.readInt();

        switch(response) {
            case RIL_UNSOL_VOICE_SPEECH_CODEC: ret = responseInts(p); break;
            case RIL_UNSOL_RINGBACK_TONE: ret = responseInts(p); break;
            default:
                // Rewind the Parcel
                p.setDataPosition(dataPosition);
                // Forward responses that we are not overriding to the super class
                super.processUnsolicited(p);
                return;
        }
/*
    Simply handling this response might be enough to get shit working, this may be
    unecessary.

        switch(response) {
            case RIL_UNSOL_VOICE_SPEECH_CODEC: {
                if (RILJ_LOGD) unsljLogRet(response, ret);

                if (mVoiceSpeechCodecInfoRegistransts != null) {
                    mVoiceSpeechCodecInfoRegistransts.notifyRegistrants(
                            new AsyncResult(null, ret, null));
                }
                break;
            }
        }
*/
    }

/*
    This would require a change to the base class...appears unecessary as well

    @Override
    static String
    responseToString(int request)
    {
        switch(request) {
            case RIL_UNSOL_VOICE_SPEECH_CODEC: return "RIL_UNSOL_VOICE_SPEECH_CODEC";
            default: return super.responseToString(request);
        }
    }
*/

/*
    This may not be necessary either, (its from BaseCommands). Its here to mimic
    RIL_UNSOL_VOICE_RADIO_TECH_CHANGED but requires some hax in other classes as well,
    so attempting without it for now.

    @Override
    public void registerForVoiceSpeechCodecInfo(Handler h, int what, Object obj) {
        Registrant r = new Registrant (h, what, obj);
        mVoiceSpeechCodecInfoRegistransts.add(r);
    }

    @Override
    public void unregisterForVoiceSpeechCodecInfo(Handler h) {
        mVoiceSpeechCodecInfoRegistransts.remove(h);
    }
*/
}
