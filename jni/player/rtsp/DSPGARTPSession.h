/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGARTPSession.h
 * Description      : Implemented based on android ARTPSession.h
 *
 */
 
 /*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef DSPG_A_RTP_SESSION_H_

#define DSPG_A_RTP_SESSION_H_

#include <media/stagefright/foundation/AHandler.h>

namespace android {

struct DSPGAPacketSource;
struct DSPGARTPConnection;
struct DSPGASessionDescription;
struct MediaSource;

struct DSPGARTPSession : public AHandler {
    DSPGARTPSession();

    status_t setup(const sp<DSPGASessionDescription> &desc);
    status_t setup(const sp<DSPGASessionDescription> &desc, int aRTPSocket, int aRTCPSocket, int vRTPSocket, int vRTCPSocket);

    size_t countTracks();
    sp<MediaSource> trackAt(size_t index);
// DSPG Code Change Start
    status_t start();
    sp<DSPGAPacketSource> PtrackAt(size_t index);
    int forceStop() ;
// DSPG Code Change End

protected:
    virtual void onMessageReceived(const sp<AMessage> &msg);

    virtual ~DSPGARTPSession();

private:
    enum {
        kWhatAccessUnitComplete = 'accu'
    };

    struct TrackInfo {
        int mRTPSocket;
        int mRTCPSocket;

        sp<DSPGAPacketSource> mPacketSource;
    };

    bool  m_IsExternalSockets;
    status_t mInitCheck;
    sp<DSPGASessionDescription> mDesc;
    sp<DSPGARTPConnection> mRTPConn;

    Vector<TrackInfo> mTracks;

    bool validateMediaFormat(size_t index, unsigned *port) const;
    static int MakeUDPSocket(unsigned port);

    DISALLOW_EVIL_CONSTRUCTORS(DSPGARTPSession);
};

}  // namespace android

#endif  // A_RTP_SESSION_H_
