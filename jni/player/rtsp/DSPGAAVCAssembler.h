/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAAVCAssembler.h
 * Description      : Implemented based on android AAVCAssembler.h
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


 #ifndef DSPG_A_AVC_ASSEMBLER_H_

#define DSPG_A_AVC_ASSEMBLER_H_

#include "DSPGARTPAssembler.h"

#include <utils/List.h>
#include <utils/RefBase.h>

namespace android {

struct ABuffer;
struct AMessage;

struct DSPGAAVCAssembler : public DSPGARTPAssembler {
    DSPGAAVCAssembler(const sp<AMessage> &notify);

protected:
    virtual ~DSPGAAVCAssembler();

    virtual AssemblyStatus assembleMore(const sp<DSPGARTPSource> &source);
    virtual void onByeReceived();
    virtual void packetLost();

private:
    sp<AMessage> mNotifyMsg;

    uint32_t mAccessUnitRTPTime;
    bool mNextExpectedSeqNoValid;
    uint32_t mNextExpectedSeqNo;
    bool mAccessUnitDamaged;
    List<sp<ABuffer> > mNALUnits;

    AssemblyStatus addNALUnit(const sp<DSPGARTPSource> &source);
    void addSingleNALUnit(const sp<ABuffer> &buffer);
    AssemblyStatus addFragmentedNALUnit(List<sp<ABuffer> > *queue);
    bool addSingleTimeAggregationPacket(const sp<ABuffer> &buffer);

    void submitAccessUnit();

    DISALLOW_EVIL_CONSTRUCTORS(DSPGAAVCAssembler);
};

}  // namespace android

#endif  // DSPG_A_AVC_ASSEMBLER_H_
