/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGARTPConnection.h
 * Description      : Implemented based on android ARTPConnection.h
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

#ifndef DSPG_A_RTP_CONNECTION_H_

#define DSPG_A_RTP_CONNECTION_H_

#include <media/stagefright/foundation/AHandler.h>
#include <utils/List.h>

namespace android {

struct ABuffer;
struct DSPGARTPSource;
struct DSPGASessionDescription;

struct DSPGARTPConnection : public AHandler {
    enum Flags {
        kFakeTimestamps      = 1,
        kRegularlyRequestFIR = 2,
// DSPG Code Change Start
        kStarted             = 4,
// DSPG Code Change End
    };

    DSPGARTPConnection(uint32_t flags = 0);

// DSPG Code Change Start
    status_t start();
#ifdef ENABLE_PROFILING
    status_t decrementFrameCounter();
#endif
// DSPG Code Change End
    void addStream(
            int rtpSocket, int rtcpSocket,
            const sp<DSPGASessionDescription> &sessionDesc, size_t index,
            const sp<AMessage> &notify,
            bool injected);

    void removeStream(int rtpSocket, int rtcpSocket);

    void injectPacket(int index, const sp<ABuffer> &buffer);

    // Creates a pair of UDP datagram sockets bound to adjacent ports
    // (the rtpSocket is bound to an even port, the rtcpSocket to the
    // next higher port).
    static void MakePortPair(
            int *rtpSocket, int *rtcpSocket, unsigned *rtpPort);

    void fakeTimestamps();

protected:
    virtual ~DSPGARTPConnection();
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kWhatAddStream,
        kWhatRemoveStream,
        kWhatPollStreams,
        kWhatInjectPacket,
        kWhatFakeTimestamps,
    };

    static const int64_t kSelectTimeoutUs;

    uint32_t mFlags;

    uint32_t mLastrtpTime;
    uint32_t mFrameCounter;

    struct StreamInfo;
    List<StreamInfo> mStreams;

    bool mPollEventPending;
    int64_t mLastReceiverReportTimeUs;

    void onAddStream(const sp<AMessage> &msg);
    void onRemoveStream(const sp<AMessage> &msg);
    void onPollStreams();
    void onInjectPacket(const sp<AMessage> &msg);
    void onSendReceiverReports();
    void onFakeTimestamps();

    status_t receive(StreamInfo *info, bool receiveRTP);

    status_t parseRTP(StreamInfo *info, const sp<ABuffer> &buffer);
    status_t parseRTCP(StreamInfo *info, const sp<ABuffer> &buffer);
    status_t parseSR(StreamInfo *info, const uint8_t *data, size_t size);
    status_t parseBYE(StreamInfo *info, const uint8_t *data, size_t size);

    sp<DSPGARTPSource> findSource(StreamInfo *info, uint32_t id);

    void postPollEvent();

    DISALLOW_EVIL_CONSTRUCTORS(DSPGARTPConnection);
};

}  // namespace android

#endif  // DSPG_A_RTP_CONNECTION_H_
