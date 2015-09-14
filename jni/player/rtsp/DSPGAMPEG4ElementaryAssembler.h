/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAMPEG4ElementaryAssembler.h
 * Description      : Implemented based on android AMPEG4ElementaryAssembler.h
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
 
 #ifndef DSPG_A_MPEG4_ELEM_ASSEMBLER_H_

#define DSPG_A_MPEG4_ELEM_ASSEMBLER_H_

#include "DSPGARTPAssembler.h"

#include <media/stagefright/foundation/AString.h>

#include <utils/List.h>
#include <utils/RefBase.h>

namespace android {

struct ABuffer;
struct AMessage;

struct DSPGAMPEG4ElementaryAssembler : public DSPGARTPAssembler {
    DSPGAMPEG4ElementaryAssembler(
            const sp<AMessage> &notify, const AString &desc,
            const AString &params);

protected:
    virtual ~DSPGAMPEG4ElementaryAssembler();

    virtual AssemblyStatus assembleMore(const sp<DSPGARTPSource> &source);
    virtual void onByeReceived();
    virtual void packetLost();

private:
    sp<AMessage> mNotifyMsg;
    bool mIsGeneric;
    AString mParams;

    unsigned mSizeLength;
    unsigned mIndexLength;
    unsigned mIndexDeltaLength;
    unsigned mCTSDeltaLength;
    unsigned mDTSDeltaLength;
    bool mRandomAccessIndication;
    unsigned mStreamStateIndication;
    unsigned mAuxiliaryDataSizeLength;
    bool mHasAUHeader;

    uint32_t mAccessUnitRTPTime;
    bool mNextExpectedSeqNoValid;
    uint32_t mNextExpectedSeqNo;
    bool mAccessUnitDamaged;
    List<sp<ABuffer> > mPackets;

    AssemblyStatus addPacket(const sp<DSPGARTPSource> &source);
    void submitAccessUnit();

    DISALLOW_EVIL_CONSTRUCTORS(DSPGAMPEG4ElementaryAssembler);
};

}  // namespace android

#endif  // DSPG_A_MPEG4_ELEM_ASSEMBLER_H_
