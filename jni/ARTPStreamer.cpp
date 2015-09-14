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

//#define LOG_NDEBUG 0
#define LOG_TAG "ARTPStreamer"
#include <utils/Log.h>

#include <sys/time.h>

#include "ARTPStreamer.h"

#include <fcntl.h>

#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <utils/ByteOrder.h>
#include <sys/time.h>

/* Don't change the values without changing it in MediaController.cpp */
#define PT_VIDEO      97
#define PT_VIDEO_STR  "97"

#define PT_AUDIO      96
#define PT_AUDIO_STR  "96"

#define INVALID_VALUE -1

using namespace android;

static const size_t kMaxPacketSize = 1400;  // maximum payload in UDP over IP
//static const size_t kMaxPacketSize = 10000;

static int UniformRand(int limit) {
    return ((double)rand() * limit) / RAND_MAX;
}

/* This constructor is for H264 video streaming, which provides SPS & PPS info to caller through the given callback pointer 
   All sockets needs to be created by the caller for using this constructor. */
ARTPStreamer::ARTPStreamer(int remote_port, int rtpSocket, int rtcpSocket, const char *pDestnIP, void * cbData, psdpcb sdpcb)
    : mFlags(0),
      mFd(-1),
      mLooper(new ALooper),
      mReflector(new AHandlerReflector<ARTPStreamer>(this)) {
    LOGI("ARTPStreamer_socket port:%d", remote_port);
    m_sdpcb = sdpcb;
    m_SDPCbData = cbData;

    mLooper->setName("dsps_rtp_writer_video");
    mLooper->registerHandler(mReflector);
    /* Creating a thread with priority equal to OMXCallbackDispatcher thread, otherwise 
       application thread will not be able to consume encoded data with the rate at which
       its actually generated */
    mLooper->start(false, false, PRIORITY_AUDIO);

	/* initialise socket ID's */
    mRTPSocket = rtpSocket;
    CHECK_GE(mRTPSocket, 0);
    mRTCPSocket = rtcpSocket;
    CHECK_GE(mRTCPSocket, 0);

    /* intialise RTP sockaddr_in structure */
    memset(mRTPAddr.sin_zero, 0, sizeof(mRTPAddr.sin_zero));
    mRTPAddr.sin_family = AF_INET;
    inet_aton(pDestnIP, &mRTPAddr.sin_addr);
    mRTPAddr.sin_port = htons(remote_port);
    CHECK_EQ(0, ntohs(mRTPAddr.sin_port) & 1);

    /* intialise RTCP sockaddr_in structure */
    mRTCPAddr = mRTPAddr;
    mRTCPAddr.sin_port = htons(remote_port+1);
}

/* This constructor can be used for video or audio (video other than H264 which does not expect SPS & PPS for playback).
   All sockets needs to be created by the caller for using this constructor. */
ARTPStreamer::ARTPStreamer(int remote_port, int rtpSocket, int rtcpSocket, const char *pDestnIP)
    : mFlags(0),
      mFd(-1),
      mLooper(new ALooper), m_sdpcb(NULL),
      mReflector(new AHandlerReflector<ARTPStreamer>(this)) {
    LOGI("ARTPStreamer_socket port:%d", remote_port);

    mLooper->setName("dspg_rtp_writer_audio");
    mLooper->registerHandler(mReflector);
    /* Creating a thread with priority equal to OMXCallbackDispatcher thread, otherwise 
       application thread will not be able to consume encoded data with the rate at which
       its actually generated */
    mLooper->start(false, false, PRIORITY_AUDIO);
    
	mRTPSocket = rtpSocket;
    CHECK_GE(mRTPSocket, 0);
    mRTCPSocket = rtcpSocket;
    CHECK_GE(mRTCPSocket, 0);

    memset(mRTPAddr.sin_zero, 0, sizeof(mRTPAddr.sin_zero));
    mRTPAddr.sin_family = AF_INET;
    inet_aton(pDestnIP, &mRTPAddr.sin_addr);
    mRTPAddr.sin_port = htons(remote_port);
    CHECK_EQ(0, ntohs(mRTPAddr.sin_port) & 1);

    //TBD: RTCP is also sent over the same socket, which has to be changed
    mRTCPAddr = mRTPAddr;
    mRTCPAddr.sin_port = htons(ntohs(mRTPAddr.sin_port) | 1);
}

/* This constructor is for H264 video streaming, which provides SPS & PPS info to caller through the given callback pointer 
   Socket creation is taken care in this module itself. */
ARTPStreamer::ARTPStreamer(int port, const char *pDestnIP, void * cbData, psdpcb sdpcb)
    : mFlags(0),
      mFd(-1),
      mLooper(new ALooper),
      mReflector(new AHandlerReflector<ARTPStreamer>(this)) {
    LOGI("ARTPStreamer_cb port:%d pDestnIP:%s", port, pDestnIP);
    m_sdpcb = sdpcb;
    m_SDPCbData = cbData;
    init (port, pDestnIP);
}

/* This constructor can be used for video or audio (video other than H264 which does not expect SPS & PPS for playback).
   Socket creation is taken care in this module itself. */
ARTPStreamer::ARTPStreamer(int port, const char *pDestnIP)
    : mFlags(0),
      mFd(-1),
      mLooper(new ALooper),
      m_sdpcb(NULL),
      mReflector(new AHandlerReflector<ARTPStreamer>(this)) {

    LOGI("ARTPStreamer port:%d pDestnIP:%s", port, pDestnIP);
    init (port, pDestnIP);
}

#ifdef ENABLE_PROFILING
static int64_t getNowms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)(tv.tv_usec/1000) + tv.tv_sec * 1000ll;
}
#endif

void ARTPStreamer::init(int port, const char *pDestnIP)
{
    LOGI("ARTPStreamer::init port:%d pDestnIP:%s", port, pDestnIP);        
    mLooper->setName("rtp writer");
    mLooper->registerHandler(mReflector);
    /* Creating a thread with priority equal to OMXCallbackDispatcher thread, otherwise 
       application thread will not be able to consume encoded data with the rate at which
       its actually generated */
    mLooper->start(false, false, PRIORITY_AUDIO);

    mRTCPSocket = 0;
    mRTPSocket = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK_GE(mRTPSocket, 0);

    memset(mRTPAddr.sin_zero, 0, sizeof(mRTPAddr.sin_zero));
    mRTPAddr.sin_family = AF_INET;
    //mRTPAddr.sin_addr.s_addr = INADDR_ANY;
    inet_aton(pDestnIP, &mRTPAddr.sin_addr);
    mRTPAddr.sin_port = htons(port);
    CHECK_EQ(0, ntohs(mRTPAddr.sin_port) & 1);

    mRTCPAddr = mRTPAddr;
    mRTCPAddr.sin_port = htons(ntohs(mRTPAddr.sin_port) | 1);
}

/* This is the default constructor provided by Android, which we are not using 
   since it has hard coded values for port & IP. */
ARTPStreamer::ARTPStreamer()
    : mFlags(0),
      mFd(0),
      mLooper(new ALooper),
      mCounter(-1),
      mLastCounter(-1),
      mCurrentBitRate(-1),
      mLastBitRate(-1),
      mLastIFrameTimeSeconds(-1),
      mLastIFrameTimeUSeconds(-1),
      mLastPFrameCounter(-1),
      mPFrameCounter(-1),
      mReflector(new AHandlerReflector<ARTPStreamer>(this)) {
    //CHECK_GE(fd, 0);

    LOGV("ARTPStreamer : CONSTRuctor");
    mLooper->setName("rtp streamer");
    mLooper->registerHandler(mReflector);
    mLooper->start();

    mRTCPSocket=0;
    mRTPSocket = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK_GE(mRTPSocket, 0);

    memset(&mIFrameTime, 0, sizeof(mIFrameTime));

    memset(mRTPAddr.sin_zero, 0, sizeof(mRTPAddr.sin_zero));
    mRTPAddr.sin_family = AF_INET;

#if 0
    mRTPAddr.sin_addr.s_addr = INADDR_ANY;
#else
    mRTPAddr.sin_addr.s_addr = inet_addr("172.28.5.78");
#endif

    mRTPAddr.sin_port = htons(5634);
    CHECK_EQ(0, ntohs(mRTPAddr.sin_port) & 1);

    mRTCPAddr = mRTPAddr;
    mRTCPAddr.sin_port = htons(ntohs(mRTPAddr.sin_port) | 1);

#if LOG_TO_FILES
    mRTPFd = open(
            "/sdcard/rtpout.bin",
            O_WRONLY | O_CREAT | O_TRUNC,
            0644);
    CHECK_GE(mRTPFd, 0);

    mRTCPFd = open(
            "/sdcard/rtcpout.bin",
            O_WRONLY | O_CREAT | O_TRUNC,
            0644);
    CHECK_GE(mRTCPFd, 0);

    mAMRFd = open(
            "/sdcard/amr.bin",
            O_WRONLY | O_CREAT | O_TRUNC,
            0644);
    CHECK_GE(mAMRFd, 0);
#endif
}

ARTPStreamer::~ARTPStreamer() {
#if LOG_TO_FILES
    close(mRTCPFd);
    mRTCPFd = -1;

    close(mRTPFd);
    mRTPFd = -1;

    close(mAMRFd);
    mAMRFd = -1;
#endif
    LOGV("~ARTPStreamer");
    mRTPSocket = -1;

    if (mFd != -1) {
        close(mFd);
        mFd = -1;
    }
}

status_t ARTPStreamer::addSource(const sp<MediaSource> &source) {
    mSource = source;
    return OK;
}

bool ARTPStreamer::reachedEOS() {
    Mutex::Autolock autoLock(mLock);
    return (mFlags & kFlagEOS) != 0;
}

status_t ARTPStreamer::start(MetaData *params) {
    LOGV("ARTPStreamer::start enter");
    mFrameCounter = 0;
    Mutex::Autolock autoLock(mLock);
    if (mFlags & kFlagStarted) {
        return INVALID_OPERATION;
    }

    mFlags &= ~kFlagEOS;
    mSourceID = rand();
    mSeqNo = UniformRand(65536);
    mRTPTimeBase = rand();
    mNumRTPSent = 0;
    mNumRTPOctetsSent = 0;
    mLastRTPTime = 0;
    mLastNTPTime = 0;
    mNumSRsSent = 0;

    const char *mime;
    CHECK(mSource->getFormat()->findCString(kKeyMIMEType, &mime));

    mMode = INVALID;
    if (!strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_AVC)) {
        mMode = H264;
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_H263)) {
        mMode = H263;
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_NB)) {
        mMode = AMR_NB;
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB)) {
        mMode = AMR_WB;
    } else {
        TRESPASS();
    }

    (new AMessage(kWhatStart, mReflector->id()))->post();

    while (!(mFlags & kFlagStarted)) {
        mCondition.wait(mLock);
    }

    LOGV("ARTPStreamer::start leave");
    return OK;
}

status_t ARTPStreamer::stop() {
    LOGV("ARTPStreamer::stop() enter");
    {
        Mutex::Autolock autoLock(mLock);
        mFrameCounter = 0;
        if (!(mFlags & kFlagStarted)) {
            return OK;
        }
    }

    LOGV("Calling stop on mSource");
    mLooper->unregisterHandler(mReflector->id());
    mLooper.clear();
    mLooper=NULL;
    CHECK_EQ(mSource->stop(), (status_t)OK);
    LOGV("Sending BYE");
    sendBye();

    LOGV("Settings STOP state");
    {
        Mutex::Autolock autoLock(mLock);
        mFlags &= ~kFlagStarted;
    }
    LOGV("ARTPStreamer::stop() leave");
    return OK;
}

status_t ARTPStreamer::pause() {
    return OK;
}

static void StripStartcode(MediaBuffer *buffer) {
    if (buffer->range_length() < 4) {
        return;
    }

    const uint8_t *ptr =
        (const uint8_t *)buffer->data() + buffer->range_offset();

    if (!memcmp(ptr, "\x00\x00\x00\x01", 4)) {
        buffer->set_range(
                buffer->range_offset() + 4, buffer->range_length() - 4);
    }
}

void ARTPStreamer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatStart:
        {
            LOGV("ARTPStreamer::onMessageReceived kWhatStart");
            CHECK_EQ(mSource->start(), (status_t)OK);

            if (mMode == H264) {
                MediaBuffer *buffer;
                CHECK_EQ(mSource->read(&buffer), (status_t)OK);

                StripStartcode(buffer);
                makeH264SPropParamSets(buffer);
                
                /* Sending first chunk of data from encoder to remote end */
                {
                    if (buffer->range_length() > 0) {
                        LOGV("read buffer of size %d", buffer->range_length());

                        if (mMode == H264) {
							mCounter = 0;
							mCurrentBitRate = 0;
							mLastPFrameCounter =0;
							mPFrameCounter  =0;
                            StripStartcode(buffer);
                            sendAVCData(buffer);
                        } else if (mMode == H263) {
                            sendH263Data(buffer);
                        } else if (mMode == AMR_NB || mMode == AMR_WB) {
                            sendAMRData(buffer);
                        }
                    }
                }
                
                buffer->release();
                buffer = NULL;
            }

            dumpSessionDesc();

            {
                Mutex::Autolock autoLock(mLock);
                mFlags |= kFlagStarted;
                mCondition.signal();
            }

            (new AMessage(kWhatRead, mReflector->id()))->post();
            (new AMessage(kWhatSendSR, mReflector->id()))->post();
            break;
        }

        case kWhatStop:
        {
            LOGV("ARTPStreamer::onMessageReceived kWhatStop");

            CHECK_EQ(mSource->stop(), (status_t)OK);

            sendBye();

            {
                Mutex::Autolock autoLock(mLock);
                mFlags &= ~kFlagStarted;
                mCondition.signal();
            }
            break;
        }

        case kWhatRead:
        {
            LOGV("ARTPStreamer::onMessageReceived kWhatRead");
            {
                Mutex::Autolock autoLock(mLock);
                if (!(mFlags & kFlagStarted)) {
                    break;
                }
            }
            onRead(msg);
            break;
        }

        case kWhatSendSR:
        {
            {
                Mutex::Autolock autoLock(mLock);
                if (!(mFlags & kFlagStarted)) {
                    break;
                }
            }

            //onSendSR(msg);
            break;
        }

        default:
            TRESPASS();
            break;
    }
}
unsigned ARTPStreamer::parseUE(ABitReader *br) {
    unsigned numZeroes = 0;
    while (br->getBits(1) == 0) {
        ++numZeroes;
    }
    unsigned x = br->getBits(numZeroes);
    return x + (1u << numZeroes) - 1;
}
bool ARTPStreamer::IsIFrame(const sp<ABuffer> &buffer) {

    const uint8_t *data = buffer->data();
    //LOGE(" IsIFrame Buffer Size :%d",buffer->size());
    size_t size = buffer->size();

    bool isIFrame = false;
    const uint8_t *nalStart;
    size_t nalSize;

	sp<ABuffer> sliceTypeBuffer = FindNAL(data, size, 5, NULL);
	if(sliceTypeBuffer == NULL) {
		sliceTypeBuffer = FindNAL(data, size, 1, NULL);
	}
	else {
		// always iFrame
		isIFrame = true;
	}
	if(sliceTypeBuffer != NULL && !isIFrame)
	{
		const uint8_t *sliceTypeData =(const uint8_t *)sliceTypeBuffer->data();
   
		#if 0
        for(int i=0; i<10;i++) {
				LOGE("sliceTypeBuffer[%d] : %x",i,sliceTypeData[i]);
		}
		#endif

		ABitReader br(sliceTypeBuffer->data()+1, sliceTypeBuffer->size()-1);

#if 0
		unsigned profile_idc = br.getBits(8);
		LOGV("Profile IDC: %d",profile_idc);
		br.skipBits(16);
		unsigned seq_parameter_set_id = parseUE(&br);
		LOGV("seq_parameter_set_id %d",seq_parameter_set_id);
#endif

#if 1
		//LOGE("Number of Bits Left [1] :-> %d", br.numBitsLeft());
		unsigned int first_mb_in_slice = parseUE(&br);  // first_mb_in_slice
		//LOGE("Number of Bits Left [2] :-> %d", br.numBitsLeft());
		unsigned int slice_type = parseUE(&br); //slice_type
		//LOGE("Number of Bits Left [3] :-> %d", br.numBitsLeft());
		unsigned int pic_parameter_set_id = parseUE(&br);
		LOGV("slice TYPE : %u and first_mb : %u and pic-parameter %u ",slice_type,first_mb_in_slice,pic_parameter_set_id);
		if(slice_type == 2 ||  slice_type == 7) {
			isIFrame = true;
		}
#endif
	}
	else {
		LOGV("sliceTypeBuffer NULL");
	}
/*
    while (getNextNALUnit(&data, &size, &nalStart, &nalSize, true) == OK) {
        CHECK_GT(nalSize, 0u);

        unsigned nalType = nalStart[0] & 0x1f;
		LOGE("\n NAL TYPE : %d\n",nalType);
		if(nalType == 1){
			sp<ABuffer> sliceTypeBuffer = FindNAL(buffer->data(), buffer->size(), 1, NULL);
			if(sliceTypeBuffer != NULL){
				LOGE(" SlieTypeBuffer: %s",sliceTypeBuffer->data());
			}
			else {
				LOGE("sliceTypeBuffer NULL");
			}
		}
    }
*/
	if(isIFrame) {
		mLastPFrameCounter = mPFrameCounter;
		mPFrameCounter=0;
	}
	else {
		mPFrameCounter++;
	}
    return isIFrame;
}
sp<ABuffer> ARTPStreamer::FindNAL(
        const uint8_t *data, size_t size, unsigned nalType,
        size_t *stopOffset) {
    const uint8_t *nalStart;
    size_t nalSize;
    //LOGE("Buffer size in FindNal function : %d",size);
    while (getNextNALUnit(&data, &size, &nalStart, &nalSize, true) == OK) {
		LOGV("NALU TYPE: %d",(nalStart[0] & 0x1f));
        if ((nalStart[0] & 0x1f) == nalType) {
			#if 0
			LOGE("\n Now NAL SIZE : %d",nalSize);
			LOGE("NALU BUFFER[0]: %x",nalStart[0]);
			LOGE("NALU BUFFER[1]: %x",nalStart[1]);
			LOGE("NALU BUFFER[2]: %x",nalStart[2]);
			LOGE("NALU BUFFER[3]: %x",nalStart[3]);
			#endif
            sp<ABuffer> buffer = new ABuffer(nalSize);
            memcpy(buffer->data(), nalStart, nalSize);
            return buffer;
        }
    }

    return NULL;
}
status_t ARTPStreamer::getNextNALUnit(
        const uint8_t **_data, size_t *_size,
        const uint8_t **nalStart, size_t *nalSize,
        bool startCodeFollows) {
    const uint8_t *data = *_data;
    size_t size = *_size;

	//LOGE("getNextNALUnit Size: %d",*_size);
    *nalStart = NULL;
    *nalSize = 0;

    if (size == 0) {
		//LOGE("Size is Zero");
        return -EAGAIN;
    }

    // Skip any number of leading 0x00.

    size_t offset = 0;
    while (offset < size && data[offset] == 0x00) {
        ++offset;
    }

    if (offset == size) {
		//LOGE("Offset same as SIZE ");
        return -EAGAIN;
    }

    // A valid startcode consists of at least two 0x00 bytes followed by 0x01.

    if (offset < 2 || data[offset] != 0x01) {
		//LOGE("Malformed");
        return ERROR_MALFORMED;
    }

    ++offset;

    size_t startOffset = offset;

    for (;;) {
        while (offset < size && data[offset] != 0x01) {
            ++offset;
        }

        if (offset == size) {
            if (startCodeFollows) {
                offset = size + 2;
                break;
            }
			//LOGE("Error startcode failed");
            return -EAGAIN;
        }

        if (data[offset - 1] == 0x00 && data[offset - 2] == 0x00) {
            break;
        }

        ++offset;
    }

    size_t endOffset = offset - 2;
    while (data[endOffset - 1] == 0x00) {
        --endOffset;
    }

    *nalStart = &data[startOffset];
    *nalSize = endOffset - startOffset;

    if (offset + 2 < size) {
        *_data = &data[offset - 2];
        *_size = size - offset + 2;
    } else {
        *_data = NULL;
        *_size = 0;
    }

    return OK;
}
int32_t ARTPStreamer::getFrameCounter() {

	//LOGD("Last Counter : %d",mLastCounter);
	//LOGD("Counter : %d",mCounter);

	if(mLastCounter == INVALID_VALUE) {
		mLastCounter = mCounter;
		return INVALID_VALUE;
	}
	int32_t frameRate = (mCounter - mLastCounter);
	if(mCounter == 0x7FFFFFFF) {
		mCounter = 0;
		mLastCounter = INVALID_VALUE;
	}
	else {
		mLastCounter = mCounter;
	}
	//LOGD("Return Frame Rate : %d",frameRate);
	return frameRate;

}
int32_t ARTPStreamer::getPFrameCounter() {
	return mLastPFrameCounter;
}
int32_t ARTPStreamer::getIFrameInterval() {

	if((mLastIFrameTimeUSeconds == INVALID_VALUE && mLastIFrameTimeSeconds ==INVALID_VALUE)) {
		return INVALID_VALUE;
	}
	LOGV("getIFrameInterval Last Frame: %i and %i",mLastIFrameTimeSeconds,mLastIFrameTimeUSeconds);
	LOGV("getIFrameInterval NOW Frame: %i and %i",mIFrameTime.tv_sec,mIFrameTime.tv_usec);
	int seconds, nseconds;
	seconds  = mIFrameTime.tv_sec  - mLastIFrameTimeSeconds;
    nseconds = mIFrameTime.tv_usec - mLastIFrameTimeUSeconds;
    //LOGV("Second : %d and microSecond:%d",seconds, nseconds);
    LOGV("Return value :%d",(((seconds) * 1000 + nseconds/1000.0) + 0.5));
    return (((seconds) * 1000 + nseconds/1000.0) + 0.5);

}

int32_t ARTPStreamer::getEncoderBitRate() {

	//LOGD("Last Bit Rate : %d",mLastBitRate);
	//LOGD("Encoder Bit Rate : %d",mCurrentBitRate);

	if(mLastBitRate == INVALID_VALUE) {
		mLastBitRate = mCurrentBitRate;
		return INVALID_VALUE;
	}
	int32_t bitRate = (mCurrentBitRate - mLastBitRate);
	if(mCurrentBitRate == 0x7FFFFFFF) {
		mCurrentBitRate = 0;
		mLastBitRate = INVALID_VALUE;
	}
	else {
		mLastBitRate = mCurrentBitRate;
	}
	//LOGD("Return Bit Rate : %d",bitRate);
	return bitRate;

}

void ARTPStreamer::onRead(const sp<AMessage> &msg) {
    MediaBuffer *mediaBuf;

#ifdef ENABLE_PROFILING
    mFrameCounter++;

    if (mMode == H264) {
        LOGD("VEncode: -> Encode, Num: %d, time=%lld", mFrameCounter, getNowms());
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        LOGD("AEncode: -> Encode, Num: %d, time=%lld", mFrameCounter, getNowms());
    }
#endif

    status_t err = mSource->read(&mediaBuf);

    if (err != OK) {
        LOGI("reached EOS.");

        Mutex::Autolock autoLock(mLock);
        mFlags |= kFlagEOS;
        return;
    }

#ifdef ENABLE_PROFILING
    if (mMode == H264) {
        LOGD("VEncode: Encode->Stream, Num: %d, time=%lld", mFrameCounter, getNowms());
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        LOGD("AEncode: Encode->Stream, Num: %d, time=%lld", mFrameCounter, getNowms());
    }
#endif

    if (mediaBuf->range_length() > 0) {
       LOGV("read buffer of size %d", mediaBuf->range_length());

        if (mMode == H264) {
			mCounter++;
			mCurrentBitRate = mCurrentBitRate + mediaBuf->range_length();
 
			//LOGE("Media Buffer Length :%d",mediaBuf->range_length());
            //LOGE("Media Buffer Offset :%d",mediaBuf->range_offset());
            const uint8_t *mediaData =(const uint8_t *)mediaBuf->data() + mediaBuf->range_offset();

			#if 0
            for(int i=0; i<10;i++) {
				LOGE("MediaBuffer[%d] : %x",i,mediaData[i]);
			}
			#endif
			sp<ABuffer> buffer = new ABuffer((void *)mediaData,mediaBuf->range_length());
            if(IsIFrame(buffer)) {
				mLastIFrameTimeSeconds = mIFrameTime.tv_sec;
				mLastIFrameTimeUSeconds = mIFrameTime.tv_usec;
				LOGV("Last IFrame Time:%l and %l",mLastIFrameTimeSeconds,mLastIFrameTimeUSeconds);
				gettimeofday(&mIFrameTime, NULL);
				LOGV("now, Lastest Frame Time: %i and :%i",mIFrameTime.tv_sec,mIFrameTime.tv_usec);
			}

            StripStartcode(mediaBuf);
            sendAVCData(mediaBuf);
#ifdef ENABLE_PROFILING
            LOGD("VEncode: Stream->Done, Num: %d, time=%lld", mFrameCounter, getNowms());
#endif
        } else if (mMode == H263) {
            sendH263Data(mediaBuf);
        } else if (mMode == AMR_NB || mMode == AMR_WB) {
            sendAMRData(mediaBuf);
#ifdef ENABLE_PROFILING
            LOGD("AEncode: Stream->Done, Num: %d, time=%lld", mFrameCounter, getNowms());
#endif
        }
    }

    mediaBuf->release();
    mediaBuf = NULL;

    msg->post();
}

void ARTPStreamer::onSendSR(const sp<AMessage> &msg) {
    sp<ABuffer> buffer = new ABuffer(65536);
    buffer->setRange(0, 0);

    addSR(buffer);
    addSDES(buffer);

    LOGV("Sending RTCP packet..");
	if (mRTCPSocket != 0) {
	    send(buffer, (const struct sockaddr *)&mRTCPAddr, mRTCPSocket);
	} else {
		send(buffer, (const struct sockaddr *)&mRTCPAddr, mRTPSocket);
	}

    ++mNumSRsSent;
    msg->post(3000000);
}

void ARTPStreamer::send(const sp<ABuffer> &buffer, const struct sockaddr *sockAddr, int socketId) {
    ssize_t n = sendto(
            socketId, buffer->data(), buffer->size(), 0,
            sockAddr,
            sizeof(struct sockaddr));

    CHECK_EQ(n, (ssize_t)buffer->size());
}


void ARTPStreamer::send(const sp<ABuffer> &buffer, bool isRTCP) {
    ssize_t n = sendto(
            mRTPSocket, buffer->data(), buffer->size(), 0,
            (const struct sockaddr *)(isRTCP ? &mRTCPAddr : &mRTPAddr),
            sizeof(mRTCPAddr));

    CHECK_EQ(n, (ssize_t)buffer->size());

#if LOG_TO_FILES
    int fd = isRTCP ? mRTCPFd : mRTPFd;

    uint32_t ms = tolel(ALooper::GetNowUs() / 1000ll);
    uint32_t length = tolel(buffer->size());
    write(fd, &ms, sizeof(ms));
    write(fd, &length, sizeof(length));
    write(fd, buffer->data(), buffer->size());
#endif
}

void ARTPStreamer::addSR(const sp<ABuffer> &buffer) {
    uint8_t *data = buffer->data() + buffer->size();

    data[0] = 0x80 | 0;
    data[1] = 200;  // SR
    data[2] = 0;
    data[3] = 6;
    data[4] = mSourceID >> 24;
    data[5] = (mSourceID >> 16) & 0xff;
    data[6] = (mSourceID >> 8) & 0xff;
    data[7] = mSourceID & 0xff;

    data[8] = mLastNTPTime >> (64 - 8);
    data[9] = (mLastNTPTime >> (64 - 16)) & 0xff;
    data[10] = (mLastNTPTime >> (64 - 24)) & 0xff;
    data[11] = (mLastNTPTime >> 32) & 0xff;
    data[12] = (mLastNTPTime >> 24) & 0xff;
    data[13] = (mLastNTPTime >> 16) & 0xff;
    data[14] = (mLastNTPTime >> 8) & 0xff;
    data[15] = mLastNTPTime & 0xff;

    data[16] = (mLastRTPTime >> 24) & 0xff;
    data[17] = (mLastRTPTime >> 16) & 0xff;
    data[18] = (mLastRTPTime >> 8) & 0xff;
    data[19] = mLastRTPTime & 0xff;

    data[20] = mNumRTPSent >> 24;
    data[21] = (mNumRTPSent >> 16) & 0xff;
    data[22] = (mNumRTPSent >> 8) & 0xff;
    data[23] = mNumRTPSent & 0xff;

    data[24] = mNumRTPOctetsSent >> 24;
    data[25] = (mNumRTPOctetsSent >> 16) & 0xff;
    data[26] = (mNumRTPOctetsSent >> 8) & 0xff;
    data[27] = mNumRTPOctetsSent & 0xff;

    buffer->setRange(buffer->offset(), buffer->size() + 28);
}

void ARTPStreamer::addSDES(const sp<ABuffer> &buffer) {
    uint8_t *data = buffer->data() + buffer->size();
    data[0] = 0x80 | 1;
    data[1] = 202;  // SDES
    data[4] = mSourceID >> 24;
    data[5] = (mSourceID >> 16) & 0xff;
    data[6] = (mSourceID >> 8) & 0xff;
    data[7] = mSourceID & 0xff;

    size_t offset = 8;

    data[offset++] = 1;  // CNAME

    static const char *kCNAME = "someone@somewhere";
    data[offset++] = strlen(kCNAME);

    memcpy(&data[offset], kCNAME, strlen(kCNAME));
    offset += strlen(kCNAME);

    data[offset++] = 7;  // NOTE

    static const char *kNOTE = "Hell's frozen over.";
    data[offset++] = strlen(kNOTE);

    memcpy(&data[offset], kNOTE, strlen(kNOTE));
    offset += strlen(kNOTE);

    data[offset++] = 0;

    if ((offset % 4) > 0) {
        size_t count = 4 - (offset % 4);
        switch (count) {
            case 3:
                data[offset++] = 0;
            case 2:
                data[offset++] = 0;
            case 1:
                data[offset++] = 0;
        }
    }

    size_t numWords = (offset / 4) - 1;
    data[2] = numWords >> 8;
    data[3] = numWords & 0xff;

    buffer->setRange(buffer->offset(), buffer->size() + offset);
}

// static
uint64_t ARTPStreamer::GetNowNTP() {
    uint64_t nowUs = ALooper::GetNowUs();

    nowUs += ((70ll * 365 + 17) * 24) * 60 * 60 * 1000000ll;

    uint64_t hi = nowUs / 1000000ll;
    uint64_t lo = ((1ll << 32) * (nowUs % 1000000ll)) / 1000000ll;

    return (hi << 32) | lo;
}

#if 0
void ARTPWriter::dumpSessionDesc() {
    AString sdp;
    sdp = "v=0\r\n";

    sdp.append("o=- ");

    uint64_t ntp = GetNowNTP();
    sdp.append(ntp);
    sdp.append(" ");
    sdp.append(ntp);
    sdp.append(" IN IP4 127.0.0.0\r\n");

    sdp.append(
          "s=Sample\r\n"
          "i=Playing around\r\n"
          "c=IN IP4 ");

    struct in_addr addr;
    addr.s_addr = ntohl(INADDR_LOOPBACK);

    sdp.append(inet_ntoa(addr));

    sdp.append(
          "\r\n"
          "t=0 0\r\n"
          "a=range:npt=now-\r\n");

    sp<MetaData> meta = mSource->getFormat();

    if (mMode == H264 || mMode == H263) {
        sdp.append("m=video ");
    } else {
        sdp.append("m=audio ");
    }

    sdp.append(StringPrintf("%d", ntohs(mRTPAddr.sin_port)));
    sdp.append(
          " RTP/AVP " PT_STR "\r\n"
          "b=AS 320000\r\n"
          "a=rtpmap:" PT_STR " ");

    if (mMode == H264) {
        sdp.append("H264/90000");
    } else if (mMode == H263) {
        sdp.append("H263-1998/90000");
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        int32_t sampleRate, numChannels;
        CHECK(mSource->getFormat()->findInt32(kKeySampleRate, &sampleRate));
        CHECK(mSource->getFormat()->findInt32(kKeyChannelCount, &numChannels));

        CHECK_EQ(numChannels, 1);
        CHECK_EQ(sampleRate, (mMode == AMR_NB) ? 8000 : 16000);

        sdp.append(mMode == AMR_NB ? "AMR" : "AMR-WB");
        sdp.append(StringPrintf("/%d/%d", sampleRate, numChannels));
    } else {
        TRESPASS();
    }

    sdp.append("\r\n");

    if (mMode == H264 || mMode == H263) {
        int32_t width, height;
        CHECK(meta->findInt32(kKeyWidth, &width));
        CHECK(meta->findInt32(kKeyHeight, &height));

        sdp.append("a=cliprect 0,0,");
        sdp.append(height);
        sdp.append(",");
        sdp.append(width);
        sdp.append("\r\n");

        sdp.append(
              "a=framesize:" PT_STR " ");
        sdp.append(width);
        sdp.append("-");
        sdp.append(height);
        sdp.append("\r\n");
    }

    if (mMode == H264) {
        sdp.append(
              "a=fmtp:" PT_STR " profile-level-id=");
        sdp.append(mProfileLevel);
        sdp.append(";sprop-parameter-sets=");

        sdp.append(mSeqParamSet);
        sdp.append(",");
        sdp.append(mPicParamSet);
        sdp.append(";packetization-mode=1\r\n");
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        sdp.append("a=fmtp:" PT_STR " octed-align\r\n");
    }

    LOGI("%s", sdp.c_str());
}
#endif
void ARTPStreamer::dumpSessionDesc() {
    AString sdp;
    sdp = "v=0\r\n";

    sdp.append("o=- ");

    uint64_t ntp = GetNowNTP();
    sdp.append(ntp);
    sdp.append(" ");
    sdp.append(ntp);
    sdp.append(" IN IP4 172.28.5.67\r\n");

    sdp.append(
          "s=Sample\r\n"
          "i=Playing around\r\n"
          "c=IN IP4 ");

    struct in_addr addr;
    addr.s_addr = ntohl(INADDR_LOOPBACK);

    sdp.append(inet_ntoa(addr));

    sdp.append(
          "\r\n"
          "t=0 0\r\n"
          "a=range:npt=now-\r\n");

    sp<MetaData> meta = mSource->getFormat();

    if (mMode == H264 || mMode == H263) {
        sdp.append("m=video ");
    } else {
        sdp.append("m=audio ");
    }

    sdp.append(StringPrintf("%d", ntohs(mRTPAddr.sin_port)));
    if(mMode == H264 || mMode == H263) {
        sdp.append(
              " RTP/AVP " PT_VIDEO_STR "\r\n"
              "b=AS 320000\r\n"
              "a=rtpmap:" PT_VIDEO_STR " ");
    }
    else {
        sdp.append(
              " RTP/AVP " PT_AUDIO_STR "\r\n"
              "b=AS 320000\r\n"
              "a=rtpmap:" PT_AUDIO_STR " ");
    }

    if (mMode == H264) {
        sdp.append("H264/90000");
    } else if (mMode == H263) {
        sdp.append("H263-1998/90000");
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        int32_t sampleRate, numChannels;
        CHECK(mSource->getFormat()->findInt32(kKeySampleRate, &sampleRate));
        CHECK(mSource->getFormat()->findInt32(kKeyChannelCount, &numChannels));

        CHECK_EQ(numChannels, 1);
        CHECK_EQ(sampleRate, (mMode == AMR_NB) ? 8000 : 16000);

        sdp.append(mMode == AMR_NB ? "AMR" : "AMR-WB");
        sdp.append(StringPrintf("/%d/%d", sampleRate, numChannels));
    } else {
        TRESPASS();
    }

    sdp.append("\r\n");

    if (mMode == H264 || mMode == H263) {
        int32_t width, height;
        CHECK(meta->findInt32(kKeyWidth, &width));
        CHECK(meta->findInt32(kKeyHeight, &height));

        sdp.append("a=cliprect 0,0,");
        sdp.append(height);
        sdp.append(",");
        sdp.append(width);
        sdp.append("\r\n");

        sdp.append(
              "a=framesize:" PT_VIDEO_STR " ");
        sdp.append(width);
        sdp.append("-");
        sdp.append(height);
        sdp.append("\r\n");
    }

    if (mMode == H264) {
        sdp.append(
              "a=fmtp:" PT_VIDEO_STR " profile-level-id=");
        sdp.append(mProfileLevel);
        sdp.append(";sprop-parameter-sets=");

        sdp.append(mSeqParamSet);
        sdp.append(",");
        sdp.append(mPicParamSet);
        sdp.append(";packetization-mode=1\r\n");
    } else if (mMode == AMR_NB || mMode == AMR_WB) {
        sdp.append("a=fmtp:" PT_AUDIO_STR " octed-align\r\n");
    }

    LOGI("%s", sdp.c_str());
}

void ARTPStreamer::makeH264SPropParamSets(MediaBuffer *buffer) {
    LOGV("Inside makeH264SPropParamSets");
    static const char kStartCode[] = "\x00\x00\x00\x01";

    const uint8_t *data =
        (const uint8_t *)buffer->data() + buffer->range_offset();
    size_t size = buffer->range_length();

    CHECK_GE(size, 0u);

    size_t startCodePos = 0;
    while (startCodePos + 3 < size
            && memcmp(kStartCode, &data[startCodePos], 4)) {
        ++startCodePos;
    }

    CHECK_LT(startCodePos + 3, size);

    CHECK_EQ((unsigned)data[0], 0x27u);

    mProfileLevel =
        StringPrintf("%02X%02X%02X", data[1], data[2], data[3]);

    encodeBase64(data, startCodePos, &mSeqParamSet);

    encodeBase64(&data[startCodePos + 4], size - startCodePos - 4,
                 &mPicParamSet);

	if (m_sdpcb != NULL){
		LOGV("profile:%s seq-param:%s pic-param:%s", mProfileLevel.c_str(), mSeqParamSet.c_str(), mPicParamSet.c_str());
		m_sdpcb (mSeqParamSet.c_str(), mPicParamSet.c_str(), mProfileLevel.c_str(), m_SDPCbData);
		LOGV("After application CB");
	}
}

void ARTPStreamer::sendBye() {
    sp<ABuffer> buffer = new ABuffer(8);
    uint8_t *data = buffer->data();
    *data++ = (2 << 6) | 1;
    *data++ = 203;
    *data++ = 0;
    *data++ = 1;
    *data++ = mSourceID >> 24;
    *data++ = (mSourceID >> 16) & 0xff;
    *data++ = (mSourceID >> 8) & 0xff;
    *data++ = mSourceID & 0xff;
    buffer->setRange(0, 8);

    send(buffer, true /* isRTCP */);
}

void ARTPStreamer::sendAVCData(MediaBuffer *mediaBuf) {
    // 12 bytes RTP header + 2 bytes for the FU-indicator and FU-header.
    CHECK_GE(kMaxPacketSize, 12u + 2u);

    int64_t timeUs;
    CHECK(mediaBuf->meta_data()->findInt64(kKeyTime, &timeUs));

    LOGV("Velu - Calling emptyBuffer Encoded SeqNum=%d, timeUs=%lld", mSeqNo, timeUs);

    uint32_t rtpTime = mRTPTimeBase + (timeUs * 9 / 100ll);

    const uint8_t *mediaData =
        (const uint8_t *)mediaBuf->data() + mediaBuf->range_offset();

    sp<ABuffer> buffer = new ABuffer(kMaxPacketSize);
    if (mediaBuf->range_length() + 12 <= buffer->capacity()) {
        // The data fits into a single packet
        uint8_t *data = buffer->data();
        data[0] = 0x80;
        data[1] = (1 << 7) | PT_VIDEO;  // M-bit
        data[2] = (mSeqNo >> 8) & 0xff;
        data[3] = mSeqNo & 0xff;
        data[4] = rtpTime >> 24;
        data[5] = (rtpTime >> 16) & 0xff;
        data[6] = (rtpTime >> 8) & 0xff;
        data[7] = rtpTime & 0xff;
        data[8] = mSourceID >> 24;
        data[9] = (mSourceID >> 16) & 0xff;
        data[10] = (mSourceID >> 8) & 0xff;
        data[11] = mSourceID & 0xff;

        memcpy(&data[12],
               mediaData, mediaBuf->range_length());

        buffer->setRange(0, mediaBuf->range_length() + 12);

        send(buffer, false /* isRTCP */);

        ++mSeqNo;
        ++mNumRTPSent;
        mNumRTPOctetsSent += buffer->size() - 12;
    } else {
        // FU-A

        unsigned nalType = mediaData[0];
        size_t offset = 1;

        bool firstPacket = true;
        while (offset < mediaBuf->range_length()) {
            size_t size = mediaBuf->range_length() - offset;
            bool lastPacket = true;
            if (size + 12 + 2 > buffer->capacity()) {
                lastPacket = false;
                size = buffer->capacity() - 12 - 2;
            }

            uint8_t *data = buffer->data();
            data[0] = 0x80;
            data[1] = (lastPacket ? (1 << 7) : 0x00) | PT_VIDEO;  // M-bit
            data[2] = (mSeqNo >> 8) & 0xff;
            data[3] = mSeqNo & 0xff;
            data[4] = rtpTime >> 24;
            data[5] = (rtpTime >> 16) & 0xff;
            data[6] = (rtpTime >> 8) & 0xff;
            data[7] = rtpTime & 0xff;
            data[8] = mSourceID >> 24;
            data[9] = (mSourceID >> 16) & 0xff;
            data[10] = (mSourceID >> 8) & 0xff;
            data[11] = mSourceID & 0xff;

            data[12] = 28 | (nalType & 0xe0);

            CHECK(!firstPacket || !lastPacket);

            data[13] =
                (firstPacket ? 0x80 : 0x00)
                | (lastPacket ? 0x40 : 0x00)
                | (nalType & 0x1f);

            memcpy(&data[14], &mediaData[offset], size);

            buffer->setRange(0, 14 + size);

            send(buffer, false /* isRTCP */);

            ++mSeqNo;
            ++mNumRTPSent;
            mNumRTPOctetsSent += buffer->size() - 12;

            firstPacket = false;
            offset += size;
        }
    }
    mLastRTPTime = rtpTime;
    mLastNTPTime = GetNowNTP();
}

void ARTPStreamer::sendH263Data(MediaBuffer *mediaBuf) {
    CHECK_GE(kMaxPacketSize, 12u + 2u);

    int64_t timeUs;
    CHECK(mediaBuf->meta_data()->findInt64(kKeyTime, &timeUs));

    uint32_t rtpTime = mRTPTimeBase + (timeUs * 9 / 100ll);

    const uint8_t *mediaData =
        (const uint8_t *)mediaBuf->data() + mediaBuf->range_offset();

    // hexdump(mediaData, mediaBuf->range_length());

    CHECK_EQ((unsigned)mediaData[0], 0u);
    CHECK_EQ((unsigned)mediaData[1], 0u);

    size_t offset = 2;
    size_t size = mediaBuf->range_length();

    while (offset < size) {
        sp<ABuffer> buffer = new ABuffer(kMaxPacketSize);
        // CHECK_LE(mediaBuf->range_length() -2 + 14, buffer->capacity());

        size_t remaining = size - offset;
        bool lastPacket = (remaining + 14 <= buffer->capacity());
        if (!lastPacket) {
            remaining = buffer->capacity() - 14;
        }

        uint8_t *data = buffer->data();
        data[0] = 0x80;
        data[1] = (lastPacket ? 0x80 : 0x00) | PT_VIDEO;  // M-bit
        data[2] = (mSeqNo >> 8) & 0xff;
        data[3] = mSeqNo & 0xff;
        data[4] = rtpTime >> 24;
        data[5] = (rtpTime >> 16) & 0xff;
        data[6] = (rtpTime >> 8) & 0xff;
        data[7] = rtpTime & 0xff;
        data[8] = mSourceID >> 24;
        data[9] = (mSourceID >> 16) & 0xff;
        data[10] = (mSourceID >> 8) & 0xff;
        data[11] = mSourceID & 0xff;

        data[12] = (offset == 2) ? 0x04 : 0x00;  // P=?, V=0
        data[13] = 0x00;  // PLEN = PEBIT = 0

        memcpy(&data[14], &mediaData[offset], remaining);
        offset += remaining;

        buffer->setRange(0, remaining + 14);

        send(buffer, false /* isRTCP */);

        ++mSeqNo;
        ++mNumRTPSent;
        mNumRTPOctetsSent += buffer->size() - 12;
    }

    mLastRTPTime = rtpTime;
    mLastNTPTime = GetNowNTP();
}

static size_t getFrameSize(bool isWide, unsigned FT) {
    static const size_t kFrameSizeNB[8] = {
        95, 103, 118, 134, 148, 159, 204, 244
    };
    static const size_t kFrameSizeWB[9] = {
        132, 177, 253, 285, 317, 365, 397, 461, 477
    };

    size_t frameSize = isWide ? kFrameSizeWB[FT] : kFrameSizeNB[FT];

    // Round up bits to bytes and add 1 for the header byte.
    frameSize = (frameSize + 7) / 8 + 1;

    return frameSize;
}

void ARTPStreamer::sendAMRData(MediaBuffer *mediaBuf) {
    const uint8_t *mediaData =
        (const uint8_t *)mediaBuf->data() + mediaBuf->range_offset();

    size_t mediaLength = mediaBuf->range_length();

#if LOG_TO_FILES
    int fd = mAMRFd;

    uint32_t length = tolel(mediaBuf->size());
    write(fd, mediaBuf->data(), mediaBuf->size());
#endif

    CHECK_GE(kMaxPacketSize, 12u + 1u + mediaLength);

    const bool isWide = (mMode == AMR_WB);

    int64_t timeUs;
    CHECK(mediaBuf->meta_data()->findInt64(kKeyTime, &timeUs));
    uint32_t rtpTime = mRTPTimeBase + (timeUs / (isWide ? 250 : 125));

    // hexdump(mediaData, mediaLength);

    Vector<uint8_t> tableOfContents;
    size_t srcOffset = 0;
    while (srcOffset < mediaLength) {
        uint8_t toc = mediaData[srcOffset];

        unsigned FT = (toc >> 3) & 0x0f;
        CHECK((isWide && FT <= 8) || (!isWide && FT <= 7));

        tableOfContents.push(toc);
        srcOffset += getFrameSize(isWide, FT);
    }
    CHECK_EQ(srcOffset, mediaLength);

    sp<ABuffer> buffer = new ABuffer(kMaxPacketSize);
    CHECK_LE(mediaLength + 12 + 1, buffer->capacity());

    // The data fits into a single packet
    uint8_t *data = buffer->data();
    data[0] = 0x80;
    data[1] = PT_AUDIO;
    if (mNumRTPSent == 0) {
        // Signal start of talk-spurt.
        data[1] |= 0x80;  // M-bit
    }
    data[2] = (mSeqNo >> 8) & 0xff;
    data[3] = mSeqNo & 0xff;
    data[4] = rtpTime >> 24;
    data[5] = (rtpTime >> 16) & 0xff;
    data[6] = (rtpTime >> 8) & 0xff;
    data[7] = rtpTime & 0xff;
    data[8] = mSourceID >> 24;
    data[9] = (mSourceID >> 16) & 0xff;
    data[10] = (mSourceID >> 8) & 0xff;
    data[11] = mSourceID & 0xff;

    data[12] = 0xf0;  // CMR=15, RR=0

    size_t dstOffset = 13;

    for (size_t i = 0; i < tableOfContents.size(); ++i) {
        uint8_t toc = tableOfContents[i];

        if (i + 1 < tableOfContents.size()) {
            toc |= 0x80;
        } else {
            toc &= ~0x80;
        }

        data[dstOffset++] = toc;
    }

    srcOffset = 0;
    for (size_t i = 0; i < tableOfContents.size(); ++i) {
        uint8_t toc = tableOfContents[i];
        unsigned FT = (toc >> 3) & 0x0f;
        size_t frameSize = getFrameSize(isWide, FT);

        ++srcOffset;  // skip toc
        memcpy(&data[dstOffset], &mediaData[srcOffset], frameSize - 1);
        srcOffset += frameSize - 1;
        dstOffset += frameSize - 1;
    }

    buffer->setRange(0, dstOffset);

    send(buffer, false /* isRTCP */);

    ++mSeqNo;
    ++mNumRTPSent;
    mNumRTPOctetsSent += buffer->size() - 12;

    mLastRTPTime = rtpTime;
    mLastNTPTime = GetNowNTP();
}


