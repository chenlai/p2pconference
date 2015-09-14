/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGMediaPlayer.h
 * Description      : Implemented based on android AwesomePlayer.h
 *
 */
 
 /*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef DSPG_MEDIA_PLAYER_H_

#define DSPG_MEDIA_PLAYER_H_

#include "NuHTTPDataSource.h"
#include "utils/DSPGTimedEventQueue.h"

#include <media/MediaPlayerInterface.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/TimeSource.h>
#include <utils/threads.h>
#include <media/stagefright/foundation/AHandlerReflector.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

struct DSPGAudioPlayer;
struct DataSource;
struct MediaBuffer;
struct MediaExtractor;
struct MediaSource;
struct NuCachedSource2;

struct ALooper;
struct DSPGARTSPController;
struct DSPGARTPSession;
struct DSPGUDPPusher;

struct DECODERINFO {
	long frameNumber;
	long timeStamp;
	int  numberofcorruptedMacroBlock;
};

struct DSPGPlayerRenderer : public RefBase {
    DSPGPlayerRenderer() {}

    virtual status_t initCheck() const = 0;
    virtual void render(MediaBuffer *buffer) = 0;

private:
    DSPGPlayerRenderer(const DSPGPlayerRenderer &);
    DSPGPlayerRenderer &operator=(const DSPGPlayerRenderer &);
};

typedef void (*DSPGPlayerCb)(int , int , int , void *);

struct DSPGMediaPlayer : public RefBase{
    DSPGMediaPlayer();
    ~DSPGMediaPlayer();

    enum {
        kWhatVideoEvent  = 'vevt',
    };
//    void setListener(const wp<MediaPlayerBase> &listener);
    void setListener (DSPGPlayerCb appCb, void *userData);

    status_t setDataSource(
            const char *uri,
            const KeyedVector<String8, String8> *headers = NULL);

    status_t setDataSource(int fd, int64_t offset, int64_t length);

    void reset();

    status_t prepare();
    status_t prepare_l();
    status_t prepareAsync();
    status_t prepareAsync_l();
    status_t prepareAsync(int aRTPSocket, int aRTCPSocket, int vRTPSocket, int vRTCPSocket);

    status_t play();
    status_t pause();

    bool isPlaying() const;

    void setISurface(const sp<ISurface> &isurface);
    void setAudioSink(const sp<MediaPlayerBase::AudioSink> &audioSink);
    status_t setLooping(bool shouldLoop);
// DSPG Code Change Start
    // Video conference sync threshold configuration APIs
    status_t setVideoLagThreshold(int msec);
    status_t setVideoAheadThreshold(int msec);

    status_t forceStop();
// DSPG Code Change End
    status_t getDuration(int64_t *durationUs);
    status_t getPosition(int64_t *positionUs);
// DSPG Code Change Start
    int64_t  getRTPQueueDurationUs(bool *eos);
// DSPG Code Change End
    status_t seekTo(int64_t timeUs);

    status_t getVideoDimensions(int32_t *width, int32_t *height) const;

    status_t suspend();
    status_t resume();

    // This is a mask of MediaExtractor::Flags.
    uint32_t flags() const;
    void getDecoderInfo(int *decoderInfo);

    void postAudioEOS();
    void postAudioSeekComplete();

    virtual void onMessageReceived(const android::sp<android::AMessage> &msg);

private:
    friend struct AwesomeEvent;

    android::sp<android::ALooper> mLocalLooper;
    android::sp<android::AHandlerReflector<DSPGMediaPlayer> > mLocalReflector;

    enum {
        PLAYING             = 1,
        LOOPING             = 2,
        FIRST_FRAME         = 4,
        PREPARING           = 8,
        PREPARED            = 16,
        AT_EOS              = 32,
        PREPARE_CANCELLED   = 64,
        CACHE_UNDERRUN      = 128,
        AUDIO_AT_EOS        = 256,
        VIDEO_AT_EOS        = 512,
        AUTO_LOOPING        = 1024,
    };

    /* Application cb */
    DSPGPlayerCb m_appCb;
    void         *m_cbUserData;

    
    /* sockets created by application */
    int maRTPSocket;
    int maRTCPSocket;
    int mvRTPSocket;
    int mvRTCPSocket;

    void *m_corruptedmacroBlock;
	int mFrameRate;
	int mCorruptedFrameRate;
	int mCorruptedMacroBlock;


    mutable Mutex mLock;
    Mutex mMiscStateLock;

    OMXClient mClient;

    IOMX *mOMX;
    
    DSPGTimedEventQueue mQueue;
    bool mQueueStarted;
    wp<MediaPlayerBase> mListener;

    sp<ISurface> mISurface;
    sp<MediaPlayerBase::AudioSink> mAudioSink;

    SystemTimeSource mSystemTimeSource;
    TimeSource *mTimeSource;

    String8 mUri;
    KeyedVector<String8, String8> mUriHeaders;

    sp<DataSource> mFileSource;

    sp<MediaSource> mVideoTrack;
    sp<MediaSource> mVideoSource;
    sp<DSPGPlayerRenderer> mVideoRenderer;
    bool mVideoRendererIsPreview;

    sp<MediaSource> mAudioTrack;
    sp<MediaSource> mAudioSource;
    DSPGAudioPlayer *mAudioPlayer;
    int64_t mDurationUs;

    uint32_t mFlags;
    uint32_t mExtractorFlags;

    int32_t mVideoWidth, mVideoHeight;
    int64_t mTimeSourceDeltaUs;
    int64_t mVideoTimeUs;
	// used for profiling
    int mFrameCounter;

    bool mSeeking;
    bool mSeekNotificationSent;
    int64_t mSeekTimeUs;

    int64_t mBitrate;  // total bitrate of the file (in bps) or -1 if unknown.

    bool mWatchForAudioSeekComplete;
    bool mWatchForAudioEOS;

    sp<DSPGTimedEventQueue::Event> mVideoEvent;
    bool mVideoEventPending;
    sp<DSPGTimedEventQueue::Event> mStreamDoneEvent;
    bool mStreamDoneEventPending;
    sp<DSPGTimedEventQueue::Event> mBufferingEvent;
    bool mBufferingEventPending;
    sp<DSPGTimedEventQueue::Event> mCheckAudioStatusEvent;
    bool mAudioStatusEventPending;

    sp<DSPGTimedEventQueue::Event> mAsyncPrepareEvent;
    Condition mPreparedCondition;
    bool mIsAsyncPrepare;
    status_t mPrepareResult;
    status_t mStreamDoneStatus;

    void postVideoEvent_l(int64_t delayUs = -1);
    void postBufferingEvent_l();
    void postStreamDoneEvent_l(status_t status);
    void postCheckAudioStatusEvent_l();
    status_t play_l();

    MediaBuffer *mLastVideoBuffer;
    MediaBuffer *mVideoBuffer;

    sp<NuHTTPDataSource> mConnectingDataSource;
    sp<NuCachedSource2> mCachedSource;

    sp<ALooper> mLooper;
    sp<DSPGARTSPController> mRTSPController;
    sp<DSPGARTPSession> mRTPSession;
    sp<DSPGUDPPusher> mRTPPusher, mRTCPPusher;

// DSPG Code Change Start
    // Video conference sync threshold configuration params
    int32_t mVideoLagThreshold, mVideoAheadThreshold;
    int32_t mOutofSyncCount;
    bool mIsVideoCall;
// DSPG Code Change End

    struct SuspensionState {
        String8 mUri;
        KeyedVector<String8, String8> mUriHeaders;
        sp<DataSource> mFileSource;

        uint32_t mFlags;
        int64_t mPositionUs;

        void *mLastVideoFrame;
        size_t mLastVideoFrameSize;
        int32_t mColorFormat;
        int32_t mVideoWidth, mVideoHeight;
        int32_t mDecodedWidth, mDecodedHeight;

        SuspensionState()
            : mLastVideoFrame(NULL) {
        }

        ~SuspensionState() {
            if (mLastVideoFrame) {
                free(mLastVideoFrame);
                mLastVideoFrame = NULL;
            }
        }
    } *mSuspensionState;

    status_t setDataSource_l(
            const char *uri,
            const KeyedVector<String8, String8> *headers = NULL);

    status_t setDataSource_l(const sp<DataSource> &dataSource);
    status_t setDataSource_l(const sp<MediaExtractor> &extractor);
    void reset_l();
    void partial_reset_l();
    status_t seekTo_l(int64_t timeUs);
    status_t pause_l(bool at_eos = false);
    status_t initRenderer_l();
    void seekAudioIfNecessary_l();

    void cancelPlayerEvents(bool keepBufferingGoing = false);

    void setAudioSource(sp<MediaSource> source);
    status_t initAudioDecoder();

    void setVideoSource(sp<MediaSource> source);
    status_t initVideoDecoder(uint32_t flags = 0);

    void onStreamDone();

    void notifyListener_l(int msg, int ext1 = 0, int ext2 = 0);

    void onVideoEvent();
    void onBufferingUpdate();
    void onCheckAudioStatus();
    void onPrepareAsyncEvent();
    void abortPrepare(status_t err);
    void finishAsyncPrepare_l();

    bool getCachedDuration_l(int64_t *durationUs, bool *eos);

    status_t finishSetDataSource_l();

    static bool ContinuePreparation(void *cookie);

    static void OnRTSPSeekDoneWrapper(void *cookie);
    void onRTSPSeekDone();

    bool getBitrate(int64_t *bitrate);

    void finishSeekIfNecessary(int64_t videoTimeUs);

    DSPGMediaPlayer(const DSPGMediaPlayer &);
    DSPGMediaPlayer &operator=(const DSPGMediaPlayer &);

#ifdef DYNAMIC_SCALING
    bool mReleaseBuffersImediately;
#endif

};

}  // namespace android

#endif  // DSPG_AWESOME_PLAYER_H_

