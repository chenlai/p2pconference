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

#ifndef A_RTP_STREAMER_H_

#define A_RTP_STREAMER_H_

#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/foundation/AHandlerReflector.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/foundation/base64.h>
#include <media/stagefright/MediaWriter.h>
#include <media/stagefright/MediaBuffer.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#define LOG_TO_FILES    0

//namespace android {


//struct ABuffer;
//struct MediaBuffer;

typedef void (*psdpcb)(const char *, const char *, const char *,  void *);


struct ARTPStreamer : public android::MediaWriter {
    ARTPStreamer();
    ARTPStreamer(int port, const char *pDestnIP);
    ARTPStreamer(int port, const char *pDestnIP,  void * cbData, psdpcb sdpcb);
    ARTPStreamer(int remote_port, int vRTPSocket, int vRTCPSocket, const char *pDestnIP, void * cbData, psdpcb sdpcb);
    ARTPStreamer(int remote_port, int rtpSocket, int rtcpSocket, const char *pDestnIP);

    virtual android::status_t addSource(const android::sp<android::MediaSource> &source);
    virtual bool reachedEOS();
    virtual android::status_t start(android::MetaData *params);
    virtual android::status_t stop();
    virtual android::status_t pause();

    virtual void onMessageReceived(const android::sp<android::AMessage> &msg);

    static uint64_t GetNowNTP();
    int32_t getFrameCounter();
    int32_t getEncoderBitRate();
    int32_t getIFrameInterval();
    int32_t getPFrameCounter();

protected:
    virtual ~ARTPStreamer();

private:
    enum {
        kWhatStart  = 'strt',
        kWhatStop   = 'stop',
        kWhatRead   = 'read',
        kWhatSendSR = 'sr  ',
    };

    enum {
        kFlagStarted  = 1,
        kFlagEOS      = 2,
    };

    psdpcb  m_sdpcb;
    void    *m_SDPCbData;

    android::Mutex mLock;
    android::Condition mCondition;
    uint32_t mFlags;
    int32_t mCounter;
    int32_t mLastCounter;
    int32_t mCurrentBitRate;
    int32_t mLastBitRate;
    struct timeval mIFrameTime;
    long mLastIFrameTimeSeconds;
    long mLastIFrameTimeUSeconds;
    int32_t mLastPFrameCounter;
    int32_t mPFrameCounter; 

    int mFd;
    int mFrameCounter;
#if LOG_TO_FILES
    int mRTPFd;
    int mRTCPFd;
    int mAMRFd;
#endif

    android::sp<android::MediaSource> mSource;
    android::sp<android::ALooper> mLooper;
    android::sp<android::AHandlerReflector<ARTPStreamer> > mReflector;

    int mRTPSocket;
    int mRTCPSocket;
    struct sockaddr_in mRTPAddr;
    struct sockaddr_in mRTCPAddr;

    android::AString mProfileLevel;
    android::AString mSeqParamSet;
    android::AString mPicParamSet;

    uint32_t mSourceID;
    uint32_t mSeqNo;
    uint32_t mRTPTimeBase;
    uint32_t mNumRTPSent;
    uint32_t mNumRTPOctetsSent;
    uint32_t mLastRTPTime;
    uint64_t mLastNTPTime;

    int32_t mNumSRsSent;

    enum {
        INVALID,
        H264,
        H263,
        AMR_NB,
        AMR_WB,
    } mMode;

    void init(int port, const char *pDestnIP);
    void onRead(const android::sp<android::AMessage> &msg);
    void onSendSR(const android::sp<android::AMessage> &msg);

    void addSR(const android::sp<android::ABuffer> &buffer);
    void addSDES(const android::sp<android::ABuffer> &buffer);

    void makeH264SPropParamSets(android::MediaBuffer *buffer);
    void dumpSessionDesc();

    void sendBye();
    void sendAVCData(android::MediaBuffer *mediaBuf);
    void sendH263Data(android::MediaBuffer *mediaBuf);
    void sendAMRData(android::MediaBuffer *mediaBuf);

    void send(const android::sp<android::ABuffer> &buffer, bool isRTCP);
    void send(const android::sp<android::ABuffer> &buffer, const struct sockaddr *sockAddr, int socketId);

    bool IsIFrame(const android::sp<android::ABuffer> &buffer);
    android::status_t getNextNALUnit(const uint8_t **_data, size_t *_size,const uint8_t **nalStart, size_t *nalSize,bool startCodeFollows);
    android::sp<android::ABuffer> FindNAL(const uint8_t *data, size_t size, unsigned nalType,size_t *stopOffset);
    unsigned parseUE(android::ABitReader *br);

    DISALLOW_EVIL_CONSTRUCTORS(ARTPStreamer);
};

//} //namespace android

#endif  // A_RTP_WRITER_H_
