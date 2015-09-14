/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAMPEG4AudioAssembler.h
 * Description      : Implemented based on android AMPEG4AudioAssembler.h
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
 
#ifndef DSPG_A_MPEG4_AUDIO_ASSEMBLER_H_

#define DSPG_A_MPEG4_AUDIO_ASSEMBLER_H_

#include "DSPGARTPAssembler.h"

#include <utils/List.h>

#include <stdint.h>

namespace android {

struct AMessage;
struct AString;

struct DSPGAMPEG4AudioAssembler : public DSPGARTPAssembler {
    DSPGAMPEG4AudioAssembler(
            const sp<AMessage> &notify, const AString &params);

protected:
    virtual ~DSPGAMPEG4AudioAssembler();

    virtual AssemblyStatus assembleMore(const sp<DSPGARTPSource> &source);
    virtual void onByeReceived();
    virtual void packetLost();

private:
    sp<AMessage> mNotifyMsg;

    bool mMuxConfigPresent;
    unsigned mNumSubFrames;
    unsigned mFrameLengthType;
    bool mOtherDataPresent;
    unsigned mOtherDataLenBits;

    uint32_t mAccessUnitRTPTime;
    bool mNextExpectedSeqNoValid;
    uint32_t mNextExpectedSeqNo;
    bool mAccessUnitDamaged;
    List<sp<ABuffer> > mPackets;

    AssemblyStatus addPacket(const sp<DSPGARTPSource> &source);
    void submitAccessUnit();

    sp<ABuffer> removeLATMFraming(const sp<ABuffer> &buffer);

    DISALLOW_EVIL_CONSTRUCTORS(DSPGAMPEG4AudioAssembler);
};

}  // namespace android

#endif  // DSPG_A_MPEG4_AUDIO_ASSEMBLER_H_
