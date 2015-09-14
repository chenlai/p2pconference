/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGARTPSource.h
 * Description      : Implemented based on android ARTPSource.h
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

#ifndef DSPG_A_RTP_SOURCE_H_

#define DSPG_A_RTP_SOURCE_H_

#include <stdint.h>

#include <media/stagefright/foundation/ABase.h>
#include <utils/List.h>
#include <utils/RefBase.h>

namespace android {

struct ABuffer;
struct AMessage;
struct DSPGARTPAssembler;
struct DSPGASessionDescription;

struct DSPGARTPSource : public RefBase {
    DSPGARTPSource(
            uint32_t id,
            const sp<DSPGASessionDescription> &sessionDesc, size_t index,
            const sp<AMessage> &notify);

    void processRTPPacket(const sp<ABuffer> &buffer);
    void timeUpdate(uint32_t rtpTime, uint64_t ntpTime);
    void byeReceived();

    List<sp<ABuffer> > *queue() { return &mQueue; }

    void addReceiverReport(const sp<ABuffer> &buffer);
    void addFIR(const sp<ABuffer> &buffer);

    bool timeEstablished() const {
        return mNumTimes == 2;
    }
// DSPG Code Change Start
    int32_t getTimeScale() {
        return mTimeScale;
    }
// DSPG Code Change End

private:
    uint32_t mID;
    uint32_t mHighestSeqNumber;
// DSPG Code Change Start
    int32_t mTimeScale;
// DSPG Code Change End
    int32_t mNumBuffersReceived;

    List<sp<ABuffer> > mQueue;
    sp<DSPGARTPAssembler> mAssembler;

    size_t mNumTimes;
    uint64_t mNTPTime[2];
    uint32_t mRTPTime[2];

    uint64_t mLastNTPTime;
    int64_t mLastNTPTimeUpdateUs;

    bool mIssueFIRRequests;
    int64_t mLastFIRRequestUs;
    uint8_t mNextFIRSeqNo;

    uint64_t RTP2NTP(uint32_t rtpTime) const;

    bool queuePacket(const sp<ABuffer> &buffer);

    DISALLOW_EVIL_CONSTRUCTORS(DSPGARTPSource);
};

}  // namespace android

#endif  // DSPG_A_RTP_SOURCE_H_
