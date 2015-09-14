/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAPacketSource.h
 * Description      : Implemented based on android APacketSource.h
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
 
#ifndef DSPG_A_PACKET_SOURCE_H_

#define DSPG_A_PACKET_SOURCE_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/MediaSource.h>
#include <utils/threads.h>
#include <utils/List.h>

namespace android {

struct ABuffer;
struct DSPGASessionDescription;

struct DSPGAPacketSource : public MediaSource {
    DSPGAPacketSource(const sp<DSPGASessionDescription> &sessionDesc, size_t index);

    status_t initCheck() const;

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

    void queueAccessUnit(const sp<ABuffer> &buffer);
    void signalEOS(status_t result);

    void flushQueue();

    int64_t getNormalPlayTimeUs();

    void setNormalPlayTimeMapping(
            uint32_t rtpTime, int64_t normalPlayTimeUs);

    int64_t getQueueDurationUs(bool *eos);

protected:
    virtual ~DSPGAPacketSource();

private:
    status_t mInitCheck;

    Mutex mLock;
    Condition mCondition;

    sp<MetaData> mFormat;
    List<sp<ABuffer> > mBuffers;
    status_t mEOSResult;

    bool mIsAVC;
    bool mScanForIDR;

    uint32_t mClockRate;
    int mFrameInCounter;
    int mFrameOutCounter;

    uint32_t mRTPTimeBase;
    int64_t mNormalPlayTimeBaseUs;

    int64_t mLastNormalPlayTimeUs;

    void updateNormalPlayTime_l(const sp<ABuffer> &buffer);

    DISALLOW_EVIL_CONSTRUCTORS(DSPGAPacketSource);
};


}  // namespace android

#endif  // A_PACKET_SOURCE_H_
