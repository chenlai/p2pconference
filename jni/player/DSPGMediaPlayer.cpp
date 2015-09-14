/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGMediaPlayer.cpp
 * Description      : Implemented based on android AwesomePlayer.cpp
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

//#define LOG_NDEBUG 0
#define LOG_TAG "DSPGMediaPlayer"
#include <utils/Log.h>

#include <dlfcn.h>

#include "rtsp/DSPGARTSPController.h"
#include "DSPGMediaPlayer.h"
#include "LiveSource.h"
#include "SoftwareRenderer.h"
#include "NuCachedSource2.h"
#include "ThrottledSource.h"
#include "MPEG2TSExtractor.h"

#include "DSPGARTPSession.h"
#include "DSPGAPacketSource.h"
#include "DSPGASessionDescription.h"
#include "DSPGUDPPusher.h"

#include <binder/IPCThreadState.h>
#include <DSPGAudioPlayer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>

#include <surfaceflinger/ISurface.h>

#include <media/stagefright/foundation/ALooper.h>

#include <OMX.h>

#define VIDEO_DECODED_INFO "/data/data/com.p2p.app/decoded.dat"

namespace android {

static int64_t kLowWaterMarkUs = 2000000ll;  // 2secs
static int64_t kHighWaterMarkUs = 10000000ll;  // 10secs
static const size_t kLowWaterMarkBytes = 40000;
static const size_t kHighWaterMarkBytes = 200000;

struct DSPGPlayerEvent : public DSPGTimedEventQueue::Event {
    DSPGPlayerEvent(
            DSPGMediaPlayer *player,
            void (DSPGMediaPlayer::*method)())
        : mPlayer(player),
          mMethod(method) {
    }

protected:
    virtual ~DSPGPlayerEvent() {}

    virtual void fire(DSPGTimedEventQueue *queue, int64_t /* now_us */) {
        (mPlayer->*mMethod)();
    }

private:
    DSPGMediaPlayer *mPlayer;
    void (DSPGMediaPlayer::*mMethod)();

    DSPGPlayerEvent(const DSPGPlayerEvent &);
    DSPGPlayerEvent &operator=(const DSPGPlayerEvent &);
};

struct DSPGRemoteRenderer : public DSPGPlayerRenderer {
    DSPGRemoteRenderer(const sp<IOMXRenderer> &target)
        : mTarget(target) {
    }

    virtual status_t initCheck() const {
        return OK;
    }

    virtual void render(MediaBuffer *buffer) {
        void *id;
        if (buffer->meta_data()->findPointer(kKeyBufferID, &id)) {
            mTarget->render((IOMX::buffer_id)id);
        }
    }

private:
    sp<IOMXRenderer> mTarget;

    DSPGRemoteRenderer(const DSPGRemoteRenderer &);
    DSPGRemoteRenderer &operator=(const DSPGRemoteRenderer &);
};

struct DSPGLocalRenderer : public DSPGPlayerRenderer {
    DSPGLocalRenderer(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            int32_t rotationDegrees)
        : mInitCheck(NO_INIT),
          mTarget(NULL),
          mLibHandle(NULL) {
            mInitCheck = init(previewOnly, componentName,
                 colorFormat, surface, displayWidth,
                 displayHeight, decodedWidth, decodedHeight,
                 rotationDegrees);
    }

    virtual status_t initCheck() const {
        return mInitCheck;
    }

    virtual void render(MediaBuffer *buffer) {
        render((const uint8_t *)buffer->data() + buffer->range_offset(),
               buffer->range_length());
    }

    void render(const void *data, size_t size) {
        mTarget->render(data, size, NULL);
    }

protected:
    virtual ~DSPGLocalRenderer() {
        delete mTarget;
        mTarget = NULL;

        if (mLibHandle) {
            dlclose(mLibHandle);
            mLibHandle = NULL;
        }
    }

private:
    status_t mInitCheck;
    VideoRenderer *mTarget;
    void *mLibHandle;

    status_t init(
            bool previewOnly,
            const char *componentName,
            OMX_COLOR_FORMATTYPE colorFormat,
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            int32_t rotationDegrees);

    DSPGLocalRenderer(const DSPGLocalRenderer &);
    DSPGLocalRenderer &operator=(const DSPGLocalRenderer &);;
};

status_t DSPGLocalRenderer::init(
        bool previewOnly,
        const char *componentName,
        OMX_COLOR_FORMATTYPE colorFormat,
        const sp<ISurface> &surface,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight,
        int32_t rotationDegrees) {
    if (!previewOnly) {
        // We will stick to the vanilla software-color-converting renderer
        // for "previewOnly" mode, to avoid unneccessarily switching overlays
        // more often than necessary.

        mLibHandle = dlopen("libstagefrighthw.so", RTLD_NOW);

        if (mLibHandle) {
            typedef VideoRenderer *(*CreateRendererWithRotationFunc)(
                    const sp<ISurface> &surface,
                    const char *componentName,
                    OMX_COLOR_FORMATTYPE colorFormat,
                    size_t displayWidth, size_t displayHeight,
                    size_t decodedWidth, size_t decodedHeight,
                    int32_t rotationDegrees);

            typedef VideoRenderer *(*CreateRendererFunc)(
                    const sp<ISurface> &surface,
                    const char *componentName,
                    OMX_COLOR_FORMATTYPE colorFormat,
                    size_t displayWidth, size_t displayHeight,
                    size_t decodedWidth, size_t decodedHeight);

            CreateRendererWithRotationFunc funcWithRotation =
                (CreateRendererWithRotationFunc)dlsym(
                        mLibHandle,
                        "_Z26createRendererWithRotationRKN7android2spINS_8"
                        "ISurfaceEEEPKc20OMX_COLOR_FORMATTYPEjjjji");

            if (funcWithRotation) {
                mTarget =
                    (*funcWithRotation)(
                            surface, componentName, colorFormat,
                            displayWidth, displayHeight,
                            decodedWidth, decodedHeight,
                            rotationDegrees);
            } else {
                if (rotationDegrees != 0) {
                    LOGW("renderer does not support rotation.");
                }

                CreateRendererFunc func =
                    (CreateRendererFunc)dlsym(
                            mLibHandle,
                            "_Z14createRendererRKN7android2spINS_8ISurfaceEEEPKc20"
                            "OMX_COLOR_FORMATTYPEjjjj");

                if (func) {
                    mTarget =
                        (*func)(surface, componentName, colorFormat,
                            displayWidth, displayHeight,
                            decodedWidth, decodedHeight);
                }
            }
        }
    }

    if (mTarget != NULL) {
        return OK;
    }

    mTarget = new SoftwareRenderer(
            colorFormat, surface, displayWidth, displayHeight,
            decodedWidth, decodedHeight, rotationDegrees);

    return ((SoftwareRenderer *)mTarget)->initCheck();
}

DSPGMediaPlayer::DSPGMediaPlayer()
    : mQueueStarted(false),
      mTimeSource(NULL),
      mVideoRendererIsPreview(false),
      mAudioPlayer(NULL),
      mFlags(0),
      mExtractorFlags(0),
      mLastVideoBuffer(NULL),
      mVideoBuffer(NULL),
      mSuspensionState(NULL),
// DSPG Code Change Start
      mVideoLagThreshold(40000),
      mVideoAheadThreshold(-10000),
      mOutofSyncCount(0),
      mIsVideoCall(false),
      mFrameRate(0),
      mCorruptedFrameRate(0),
      mCorruptedMacroBlock(0),       
      mLocalLooper(new ALooper),
      mLocalReflector(new AHandlerReflector<DSPGMediaPlayer>(this)) {
// DSPG Code Change End
//    CHECK_EQ(mClient.connect(), OK);

    mLocalLooper->setName("dspg_video_play");
    mLocalLooper->registerHandler(mLocalReflector);
    /* Creating a thread with higher priority compared to OMXCallbackDispatcher and DSPGARTPSession
      threads, so that rendering thread does not get blocked */
    mLocalLooper->start(false, false, PRIORITY_URGENT_AUDIO);

    DataSource::RegisterDefaultSniffers();

    mVideoEvent = new DSPGPlayerEvent(this, &DSPGMediaPlayer::onVideoEvent);
    mVideoEventPending = false;
    mStreamDoneEvent = new DSPGPlayerEvent(this, &DSPGMediaPlayer::onStreamDone);
    mStreamDoneEventPending = false;
    mBufferingEvent = new DSPGPlayerEvent(this, &DSPGMediaPlayer::onBufferingUpdate);
    mBufferingEventPending = false;

    mCheckAudioStatusEvent = new DSPGPlayerEvent(
            this, &DSPGMediaPlayer::onCheckAudioStatus);

    /* Creating OMX instance with less priority to avoid renderer getting blocked till all the events
     in the OMXDispatcher event loop are done */            
    mOMX = new OMX(PRIORITY_URGENT_DISPLAY);

    mAudioStatusEventPending = false;

#ifdef DYNAMIC_SACLING
	mReleaseBuffersImediately = false;
#endif

    reset();
}

DSPGMediaPlayer::~DSPGMediaPlayer() {
    if (mQueueStarted) {
        mQueue.stop();
    }

    reset();

    mClient.disconnect();
}

void DSPGMediaPlayer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatVideoEvent:
        {
            LOGV("Looper videoevent");
            onVideoEvent();
        }
        break;
    }
}

void DSPGMediaPlayer::cancelPlayerEvents(bool keepBufferingGoing) {
    mQueue.cancelEvent(mVideoEvent->eventID());
    mVideoEventPending = false;
    mQueue.cancelEvent(mStreamDoneEvent->eventID());
    mStreamDoneEventPending = false;
    mQueue.cancelEvent(mCheckAudioStatusEvent->eventID());
    mAudioStatusEventPending = false;

    if (!keepBufferingGoing) {
        mQueue.cancelEvent(mBufferingEvent->eventID());
        mBufferingEventPending = false;
    }
}

void DSPGMediaPlayer::setListener (DSPGPlayerCb appCb, void *userData){
    LOGV("setListener");
    Mutex::Autolock autoLock(mLock);
    m_appCb = appCb;
    m_cbUserData = userData;
    return ;
}

status_t DSPGMediaPlayer::setDataSource(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    Mutex::Autolock autoLock(mLock);
    return setDataSource_l(uri, headers);
}

status_t DSPGMediaPlayer::setDataSource_l(
        const char *uri, const KeyedVector<String8, String8> *headers) {
    reset_l();

    mUri = uri;

    if (headers) {
        mUriHeaders = *headers;
    }

    // The actual work will be done during preparation in the call to
    // ::finishSetDataSource_l to avoid blocking the calling thread in
    // setDataSource for any significant time.

    return OK;
}

status_t DSPGMediaPlayer::setDataSource(
        int fd, int64_t offset, int64_t length) {
    Mutex::Autolock autoLock(mLock);

	LOGV("setDataSource(fd = %d, offset = %ld, length = %ld",fd, offset, length);

    reset_l();

    sp<DataSource> dataSource = new FileSource(fd, offset, length);

    status_t err = dataSource->initCheck();

    if (err != OK) {
        return err;
    }

    mFileSource = dataSource;

    return setDataSource_l(dataSource);
}

status_t DSPGMediaPlayer::setDataSource_l(
        const sp<DataSource> &dataSource) {
    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    return setDataSource_l(extractor);
}

status_t DSPGMediaPlayer::setDataSource_l(const sp<MediaExtractor> &extractor) {
    // Attempt to approximate overall stream bitrate by summing all
    // tracks' individual bitrates, if not all of them advertise bitrate,
    // we have to fail.

    int64_t totalBitRate = 0;

    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        int32_t bitrate;
        if (!meta->findInt32(kKeyBitRate, &bitrate)) {
            totalBitRate = -1;
            break;
        }

        totalBitRate += bitrate;
    }

    mBitrate = totalBitRate;

    LOGV("mBitrate = %lld bits/sec", mBitrate);

    bool haveAudio = false;
    bool haveVideo = false;
    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<MetaData> meta = extractor->getTrackMetaData(i);

        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));

        if (!haveVideo && !strncasecmp(mime, "video/", 6)) {
            setVideoSource(extractor->getTrack(i));
            haveVideo = true;
        } else if (!haveAudio && !strncasecmp(mime, "audio/", 6)) {
            setAudioSource(extractor->getTrack(i));
            haveAudio = true;

            if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS)) {
                // Only do this for vorbis audio, none of the other audio
                // formats even support this ringtone specific hack and
                // retrieving the metadata on some extractors may turn out
                // to be very expensive.
                sp<MetaData> fileMeta = extractor->getMetaData();
                int32_t loop;
                if (fileMeta != NULL
                        && fileMeta->findInt32(kKeyAutoLoop, &loop) && loop != 0) {
                    mFlags |= AUTO_LOOPING;
                }
            }
        }

        if (haveAudio && haveVideo) {
            break;
        }
    }

    if (!haveAudio && !haveVideo) {
        return UNKNOWN_ERROR;
    }

    mExtractorFlags = extractor->flags();

    return OK;
}

void DSPGMediaPlayer::reset() {
    Mutex::Autolock autoLock(mLock);
    reset_l();
}

void DSPGMediaPlayer::reset_l() {
    if (mFlags & PREPARING) {
        mFlags |= PREPARE_CANCELLED;
        if (mConnectingDataSource != NULL) {
            LOGI("interrupting the connection process");
            mConnectingDataSource->disconnect();
        }
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    cancelPlayerEvents();

    mCachedSource.clear();
    mAudioTrack.clear();
    mVideoTrack.clear();

    // Shutdown audio first, so that the respone to the reset request
    // appears to happen instantaneously as far as the user is concerned
    // If we did this later, audio would continue playing while we
    // shutdown the video-related resources and the player appear to
    // not be as responsive to a reset request.
    if (mAudioPlayer == NULL && mAudioSource != NULL) {
        // If we had an audio player, it would have effectively
        // taken possession of the audio source and stopped it when
        // _it_ is stopped. Otherwise this is still our responsibility.
        mAudioSource->stop();
    }
    mAudioSource.clear();

    mTimeSource = NULL;

    delete mAudioPlayer;
    mAudioPlayer = NULL;

    mVideoRenderer.clear();

    if (mLastVideoBuffer) {
        mLastVideoBuffer->release();
        mLastVideoBuffer = NULL;
    }

    if (mVideoBuffer) {
        mVideoBuffer->release();
        mVideoBuffer = NULL;
    }

    if (mRTSPController != NULL) {
        mRTSPController->disconnect();
        mRTSPController.clear();
    }

    mRTPPusher.clear();
    mRTCPPusher.clear();
    mRTPSession.clear();

    if (mVideoSource != NULL) {
        mVideoSource->stop();

        // The following hack is necessary to ensure that the OMX
        // component is completely released by the time we may try
        // to instantiate it again.
        wp<MediaSource> tmp = mVideoSource;
        mVideoSource.clear();
        while (tmp.promote() != NULL) {
            usleep(1000);
        }
        IPCThreadState::self()->flushCommands();
    }

    mDurationUs = -1;
    mFlags = 0;
    mExtractorFlags = 0;
    mVideoWidth = mVideoHeight = -1;
    mTimeSourceDeltaUs = 0;
    mVideoTimeUs = 0;

// DSPG Code Change Start
    mVideoLagThreshold = 40000;
    mVideoAheadThreshold = -10000;
    mOutofSyncCount = 0;
    mIsVideoCall = false;
// DSPG Code Change End

    mSeeking = false;
    mSeekNotificationSent = false;
    mSeekTimeUs = 0;

    mUri.setTo("");
    mUriHeaders.clear();

    mFileSource.clear();

    delete mSuspensionState;
    mSuspensionState = NULL;

    mBitrate = -1;
}

void DSPGMediaPlayer::notifyListener_l(int msg, int ext1, int ext2) {
    if (m_appCb != NULL) {
        LOGV("notifyListener_l: sending msg %d", msg);
        m_appCb(msg, ext1, ext2, m_cbUserData);
    }
    return ;
}

bool DSPGMediaPlayer::getBitrate(int64_t *bitrate) {
    off_t size;
    if (mDurationUs >= 0 && mCachedSource != NULL
            && mCachedSource->getSize(&size) == OK) {
        *bitrate = size * 8000000ll / mDurationUs;  // in bits/sec
        return true;
    }

    if (mBitrate >= 0) {
        *bitrate = mBitrate;
        return true;
    }

    *bitrate = 0;

    return false;
}

// DSPG Code Change Start
    //Added to get the queue duration.
    //This API currently not used or called.
int64_t DSPGMediaPlayer::getRTPQueueDurationUs(bool *eos) {
    *eos = true;
    int64_t minQueuedDurationUs = 0;
    for (size_t i = 0; i < mRTPSession->countTracks(); ++i) {
        sp<DSPGAPacketSource> source = mRTPSession->PtrackAt(i);

        bool newEOS;
        int64_t queuedDurationUs = source->getQueueDurationUs(&newEOS);

        if (!newEOS) {
            *eos = false;
        }

        if (i == 0 || queuedDurationUs < minQueuedDurationUs) {
            minQueuedDurationUs = queuedDurationUs;
        }
    }
    return minQueuedDurationUs;
}
// DSPG Code Change End

// Returns true iff cached duration is available/applicable.
bool DSPGMediaPlayer::getCachedDuration_l(int64_t *durationUs, bool *eos) {
    int64_t bitrate;

    if (mRTSPController != NULL) {
        *durationUs = mRTSPController->getQueueDurationUs(eos);
        return true;
// DSPG Code Change End
    // Get the RTP Queue duration if needed
    } else if ( mRTPSession != NULL) {
        *durationUs = getRTPQueueDurationUs(eos);
        return true;
// DSPG Code Change End
    } else if (mCachedSource != NULL && getBitrate(&bitrate)) {
        size_t cachedDataRemaining = mCachedSource->approxDataRemaining(eos);
        *durationUs = cachedDataRemaining * 8000000ll / bitrate;
        return true;
    }

    return false;
}

void DSPGMediaPlayer::onBufferingUpdate() {
    Mutex::Autolock autoLock(mLock);
    if (!mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = false;

    if (mCachedSource != NULL) {
        bool eos;
        size_t cachedDataRemaining = mCachedSource->approxDataRemaining(&eos);

        if (eos) {
            notifyListener_l(MEDIA_BUFFERING_UPDATE, 100);
            if (mFlags & PREPARING) {
                LOGV("cache has reached EOS, prepare is done.");
                finishAsyncPrepare_l();
            }
        } else {
            int64_t bitrate;
            if (getBitrate(&bitrate)) {
                size_t cachedSize = mCachedSource->cachedSize();
                int64_t cachedDurationUs = cachedSize * 8000000ll / bitrate;

                int percentage = 100.0 * (double)cachedDurationUs / mDurationUs;
                if (percentage > 100) {
                    percentage = 100;
                }

                notifyListener_l(MEDIA_BUFFERING_UPDATE, percentage);
            } else {
                // We don't know the bitrate of the stream, use absolute size
                // limits to maintain the cache.

                if ((mFlags & PLAYING) && !eos
                        && (cachedDataRemaining < kLowWaterMarkBytes)) {
                    LOGI("cache is running low (< %d) , pausing.",
                         kLowWaterMarkBytes);
                    mFlags |= CACHE_UNDERRUN;
                    pause_l();
                    notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
                } else if (eos || cachedDataRemaining > kHighWaterMarkBytes) {
                    if (mFlags & CACHE_UNDERRUN) {
                        LOGI("cache has filled up (> %d), resuming.",
                             kHighWaterMarkBytes);
                        mFlags &= ~CACHE_UNDERRUN;
                        play_l();
                        notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
                    } else if (mFlags & PREPARING) {
                        LOGV("cache has filled up (> %d), prepare is done",
                             kHighWaterMarkBytes);
                        finishAsyncPrepare_l();
                    }
                }
            }
        }
    }

    int64_t cachedDurationUs;
    bool eos;
    if (getCachedDuration_l(&cachedDurationUs, &eos)) {
        if ((mFlags & PLAYING) && !eos
                && (cachedDurationUs < kLowWaterMarkUs)) {
            LOGI("cache is running low (%.2f secs) , pausing.",
                 cachedDurationUs / 1E6);
            mFlags |= CACHE_UNDERRUN;
            pause_l();
            notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_START);
        } else if (eos || cachedDurationUs > kHighWaterMarkUs) {
            if (mFlags & CACHE_UNDERRUN) {
                LOGI("cache has filled up (%.2f secs), resuming.",
                     cachedDurationUs / 1E6);
                mFlags &= ~CACHE_UNDERRUN;
                play_l();
                notifyListener_l(MEDIA_INFO, MEDIA_INFO_BUFFERING_END);
            } else if (mFlags & PREPARING) {
                LOGV("cache has filled up (%.2f secs), prepare is done",
                     cachedDurationUs / 1E6);
                finishAsyncPrepare_l();
            }
        }
    }

    postBufferingEvent_l();
}

void DSPGMediaPlayer::partial_reset_l() {
    // Only reset the video renderer and shut down the video decoder.
    // Then instantiate a new video decoder and resume video playback.

    mVideoRenderer.clear();

    if (mLastVideoBuffer) {
        mLastVideoBuffer->release();
        mLastVideoBuffer = NULL;
    }

    if (mVideoBuffer) {
        mVideoBuffer->release();
        mVideoBuffer = NULL;
    }

    {
        mVideoSource->stop();

        // The following hack is necessary to ensure that the OMX
        // component is completely released by the time we may try
        // to instantiate it again.
        wp<MediaSource> tmp = mVideoSource;
        mVideoSource.clear();
        while (tmp.promote() != NULL) {
            usleep(1000);
        }
        IPCThreadState::self()->flushCommands();
    }

    CHECK_EQ(OK, initVideoDecoder(OMXCodec::kIgnoreCodecSpecificData));
}

void DSPGMediaPlayer::onStreamDone() {
    // Posted whenever any stream finishes playing.

    Mutex::Autolock autoLock(mLock);
    if (!mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = false;

    if (mStreamDoneStatus == INFO_DISCONTINUITY) {
        // This special status is returned because an http live stream's
        // video stream switched to a different bandwidth at this point
        // and future data may have been encoded using different parameters.
        // This requires us to shutdown the video decoder and reinstantiate
        // a fresh one.

        LOGV("INFO_DISCONTINUITY");

        CHECK(mVideoSource != NULL);

        partial_reset_l();
        postVideoEvent_l();
        return;
    } else if (mStreamDoneStatus != ERROR_END_OF_STREAM) {
        LOGV("MEDIA_ERROR %d", mStreamDoneStatus);

        notifyListener_l(
                MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, mStreamDoneStatus);

        pause_l(true /* at eos */);

        mFlags |= AT_EOS;
        return;
    }

    const bool allDone =
        (mVideoSource == NULL || (mFlags & VIDEO_AT_EOS))
            && (mAudioSource == NULL || (mFlags & AUDIO_AT_EOS));

    if (!allDone) {
        return;
    }

    if (mFlags & (LOOPING | AUTO_LOOPING)) {
        seekTo_l(0);

        if (mVideoSource != NULL) {
            postVideoEvent_l();
        }
    } else {
        LOGV("MEDIA_PLAYBACK_COMPLETE");
        notifyListener_l(MEDIA_PLAYBACK_COMPLETE);

        pause_l(true /* at eos */);

        mFlags |= AT_EOS;
    }
}

status_t DSPGMediaPlayer::play() {
    Mutex::Autolock autoLock(mLock);

    mFlags &= ~CACHE_UNDERRUN;

    return play_l();
}

status_t DSPGMediaPlayer::play_l() {
    if (mFlags & PLAYING) {
        return OK;
    }

    if (!(mFlags & PREPARED)) {
        status_t err = prepare_l();

        if (err != OK) {
            return err;
        }
    }

    mFlags |= PLAYING;
    mFlags |= FIRST_FRAME;

    bool deferredAudioSeek = false;

    if (mAudioSource != NULL) {
        if (mAudioPlayer == NULL) {
            if (mAudioSink != NULL) {
                mAudioPlayer = new DSPGAudioPlayer(mAudioSink, this);
                mAudioPlayer->setSource(mAudioSource);

// DSPG Code Change Start
                if(mRTPSession != NULL) {
                    mAudioPlayer->setConferenceMode(true);
                }
// DSPG Code Change End
                // We've already started the MediaSource in order to enable
                // the prefetcher to read its data.
                status_t err = mAudioPlayer->start(
                        true /* sourceAlreadyStarted */);

                if (err != OK) {
                    delete mAudioPlayer;
                    mAudioPlayer = NULL;

                    mFlags &= ~(PLAYING | FIRST_FRAME);

                    return err;
                }

                mTimeSource = mAudioPlayer;

                deferredAudioSeek = true;

                mWatchForAudioSeekComplete = false;
                mWatchForAudioEOS = true;
            }
        } else {
            mAudioPlayer->resume();
        }
    }

    if (mTimeSource == NULL && mAudioPlayer == NULL) {
        mTimeSource = &mSystemTimeSource;
    }

    if (mVideoSource != NULL) {
        // Kick off video playback
        postVideoEvent_l();
    }
// DSPG Code Change Start
    if(mRTPSession != NULL) {
        LOGD("DSPG conference Call Start.");
        mRTPSession->start();
    }
// DSPG Code Change End
    if (deferredAudioSeek) {
        // If there was a seek request while we were paused
        // and we're just starting up again, honor the request now.
        seekAudioIfNecessary_l();
    }

    if (mFlags & AT_EOS) {
        // Legacy behaviour, if a stream finishes playing and then
        // is started again, we play from the start...
        seekTo_l(0);
    }
    mFrameCounter = 0;

    return OK;
}

status_t DSPGMediaPlayer::initRenderer_l() {
    if (mISurface == NULL) {
        return OK;
    }

    sp<MetaData> meta = mVideoSource->getFormat();

    int32_t format;
    const char *component;
    int32_t decodedWidth, decodedHeight;
    CHECK(meta->findInt32(kKeyColorFormat, &format));
    CHECK(meta->findCString(kKeyDecoderComponent, &component));
    CHECK(meta->findInt32(kKeyWidth, &decodedWidth));
    CHECK(meta->findInt32(kKeyHeight, &decodedHeight));

    int32_t rotationDegrees;
    if (!mVideoTrack->getFormat()->findInt32(
                kKeyRotation, &rotationDegrees)) {
        rotationDegrees = 0;
    }

    mVideoRenderer.clear();

    // Must ensure that mVideoRenderer's destructor is actually executed
    // before creating a new one.
    IPCThreadState::self()->flushCommands();

    if (!strncmp("OMX.", component, 4)) {
        // Our OMX codecs allocate buffers on the media_server side
        // therefore they require a remote IOMXRenderer that knows how
        // to display them.

        sp<IOMXRenderer> native =
            /*mClient.interface()*/mOMX->createRenderer(
                    mISurface, component,
                    (OMX_COLOR_FORMATTYPE)format,
                    decodedWidth, decodedHeight,
                    mVideoWidth, mVideoHeight,
                    rotationDegrees);

        if (native == NULL) {
            return NO_INIT;
        }

        mVideoRenderer = new DSPGRemoteRenderer(native);
    } else {

        // Other decoders are instantiated locally and as a consequence
        // allocate their buffers in local address space.
        mVideoRenderer = new DSPGLocalRenderer(
            false,  // previewOnly
            component,
            (OMX_COLOR_FORMATTYPE)format,
            mISurface,
            mVideoWidth, mVideoHeight,
            decodedWidth, decodedHeight, rotationDegrees);
    }

    return mVideoRenderer->initCheck();
}

status_t DSPGMediaPlayer::pause() {
    Mutex::Autolock autoLock(mLock);

    mFlags &= ~CACHE_UNDERRUN;

    return pause_l();
}

status_t DSPGMediaPlayer::pause_l(bool at_eos) {
    if (!(mFlags & PLAYING)) {
        return OK;
    }

    cancelPlayerEvents(true /* keepBufferingGoing */);

    if (mAudioPlayer != NULL) {
        if (at_eos) {
            // If we played the audio stream to completion we
            // want to make sure that all samples remaining in the audio
            // track's queue are played out.
            mAudioPlayer->pause(true /* playPendingSamples */);
        } else {
            mAudioPlayer->pause();
        }
    }

    mFlags &= ~PLAYING;

    return OK;
}

bool DSPGMediaPlayer::isPlaying() const {
    return (mFlags & PLAYING) || (mFlags & CACHE_UNDERRUN);
}

void DSPGMediaPlayer::setISurface(const sp<ISurface> &isurface) {
    Mutex::Autolock autoLock(mLock);

    mISurface = isurface;
}

void DSPGMediaPlayer::setAudioSink(
        const sp<MediaPlayerBase::AudioSink> &audioSink) {
    Mutex::Autolock autoLock(mLock);

    mAudioSink = audioSink;
}

status_t DSPGMediaPlayer::setLooping(bool shouldLoop) {
    Mutex::Autolock autoLock(mLock);

    mFlags = mFlags & ~LOOPING;

    if (shouldLoop) {
        mFlags |= LOOPING;
    }

    return OK;
}

// DSPG Code Change Start
// Video conference sync threshold configuration APIs
status_t DSPGMediaPlayer::setVideoLagThreshold(int msec) {
    Mutex::Autolock autoLock(mLock);

    if (msec < 0) {
        return UNKNOWN_ERROR;
    }

    mVideoLagThreshold = msec;
    mIsVideoCall = true;

    return OK;
}

status_t DSPGMediaPlayer::setVideoAheadThreshold(int msec) {
    Mutex::Autolock autoLock(mLock);

    if (msec < 0) {
        return UNKNOWN_ERROR;
    }

    mVideoAheadThreshold = -msec;
    mIsVideoCall = true;

    return OK;
}

/* Purpose if this API: In P2P application we observed that, when there is no 
incoming media data (due to invalid IP or someother reason) decoder (OMXCodec or AMR codec) 
read() function was getting blocked forever waiting for decoded data. During this situation  
when application calls stop() it was entering into deadlock situation waiting for the 
mutex consumed before calling read(). To avoid this we are sending EOS event to make 
decoder read() API return immediately */
status_t DSPGMediaPlayer::forceStop() {
    LOGV("forceStop");
    if (mRTPSession != NULL) {
        mRTPSession->forceStop();
    }
    return OK;
}
// DSPG Code Change End

status_t DSPGMediaPlayer::getDuration(int64_t *durationUs) {
    Mutex::Autolock autoLock(mMiscStateLock);

    if (mDurationUs < 0) {
        return UNKNOWN_ERROR;
    }

    *durationUs = mDurationUs;

    return OK;
}

status_t DSPGMediaPlayer::getPosition(int64_t *positionUs) {
    if (mRTSPController != NULL) {
        *positionUs = mRTSPController->getNormalPlayTimeUs();
    }
    else if (mSeeking) {
        *positionUs = mSeekTimeUs;
    } else if (mVideoSource != NULL) {
        Mutex::Autolock autoLock(mMiscStateLock);
        *positionUs = mVideoTimeUs;
    } else if (mAudioPlayer != NULL) {
        *positionUs = mAudioPlayer->getMediaTimeUs();
    } else {
        *positionUs = 0;
    }

    return OK;
}

status_t DSPGMediaPlayer::seekTo(int64_t timeUs) {
    if (mExtractorFlags & MediaExtractor::CAN_SEEK) {
        Mutex::Autolock autoLock(mLock);
        return seekTo_l(timeUs);
    }

    return OK;
}

// static
void DSPGMediaPlayer::OnRTSPSeekDoneWrapper(void *cookie) {
    static_cast<DSPGMediaPlayer *>(cookie)->onRTSPSeekDone();
}

void DSPGMediaPlayer::onRTSPSeekDone() {
    notifyListener_l(MEDIA_SEEK_COMPLETE);
    mSeekNotificationSent = true;
}

status_t DSPGMediaPlayer::seekTo_l(int64_t timeUs) {
    if (mRTSPController != NULL) {
        mRTSPController->seekAsync(timeUs, OnRTSPSeekDoneWrapper, this);
        return OK;
    }

    if (mFlags & CACHE_UNDERRUN) {
        mFlags &= ~CACHE_UNDERRUN;
        play_l();
    }

    mSeeking = true;
    mSeekNotificationSent = false;
    mSeekTimeUs = timeUs;
    mFlags &= ~(AT_EOS | AUDIO_AT_EOS | VIDEO_AT_EOS);

    seekAudioIfNecessary_l();

    if (!(mFlags & PLAYING)) {
        LOGV("seeking while paused, sending SEEK_COMPLETE notification"
             " immediately.");

        notifyListener_l(MEDIA_SEEK_COMPLETE);
        mSeekNotificationSent = true;
    }

    return OK;
}

void DSPGMediaPlayer::seekAudioIfNecessary_l() {
    if (mSeeking && mVideoSource == NULL && mAudioPlayer != NULL) {
        mAudioPlayer->seekTo(mSeekTimeUs);

        mWatchForAudioSeekComplete = true;
        mWatchForAudioEOS = true;
        mSeekNotificationSent = false;
    }
}

status_t DSPGMediaPlayer::getVideoDimensions(
        int32_t *width, int32_t *height) const {
    Mutex::Autolock autoLock(mLock);

    if (mVideoWidth < 0 || mVideoHeight < 0) {
        return UNKNOWN_ERROR;
    }

    *width = mVideoWidth;
    *height = mVideoHeight;

    return OK;
}

void DSPGMediaPlayer::setAudioSource(sp<MediaSource> source) {
    CHECK(source != NULL);

    mAudioTrack = source;
}

status_t DSPGMediaPlayer::initAudioDecoder() {
    sp<MetaData> meta = mAudioTrack->getFormat();

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_RAW)) {
        mAudioSource = mAudioTrack;
    } else {
        mAudioSource = OMXCodec::Create(
                /*mClient.interface()*/mOMX, mAudioTrack->getFormat(),
                false, // createEncoder
                mAudioTrack);
    }

    if (mAudioSource != NULL) {
        int64_t durationUs;
        if (mAudioTrack->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            Mutex::Autolock autoLock(mMiscStateLock);
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
        }

        status_t err = mAudioSource->start();

        if (err != OK) {
            mAudioSource.clear();
            return err;
        }
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_QCELP)) {
        // For legacy reasons we're simply going to ignore the absence
        // of an audio decoder for QCELP instead of aborting playback
        // altogether.
        return OK;
    }

    return mAudioSource != NULL ? OK : UNKNOWN_ERROR;
}

void DSPGMediaPlayer::setVideoSource(sp<MediaSource> source) {
    CHECK(source != NULL);

    mVideoTrack = source;
}

void DSPGMediaPlayer::getDecoderInfo(int *decoderInfo) {

	/* Structure Detail:
	 * decoderInfo[0]:-> Frame Rate
	 * decoderInfo[1]:-> Corrupted Frame Rate
	 * decoderInfo[2]:-> Number of Macro Block Corrupted
	 */
	 /* Frame Rate */
	 decoderInfo[0] = mFrameRate;

	 /* Corrupted Frame Rate */
	 decoderInfo[1] = mCorruptedFrameRate;

	 /* Corrupted Number of Macro Block */
	 decoderInfo[2] = mCorruptedMacroBlock;

	 mCorruptedMacroBlock  = mCorruptedFrameRate = mFrameRate = 0;


}
status_t DSPGMediaPlayer::initVideoDecoder(uint32_t flags) {
    mVideoSource = OMXCodec::Create(
            /*mClient.interface()*/mOMX, mVideoTrack->getFormat(),
            false, // createEncoder
            mVideoTrack,
            NULL, flags);

    if (mVideoSource != NULL) {
        int64_t durationUs;
        if (mVideoTrack->getFormat()->findInt64(kKeyDuration, &durationUs)) {
            Mutex::Autolock autoLock(mMiscStateLock);
            if (mDurationUs < 0 || durationUs > mDurationUs) {
                mDurationUs = durationUs;
            }
        }

        CHECK(mVideoTrack->getFormat()->findInt32(kKeyWidth, &mVideoWidth));
        CHECK(mVideoTrack->getFormat()->findInt32(kKeyHeight, &mVideoHeight));

        // Reduce number of buffers used by omxil decoder layer
        // for real time operations.
        sp<MetaData> meta = new MetaData;
        meta->setInt32(kKeyDSPGRealTimeOptimizations, 1);
        status_t err = mVideoSource->start(meta.get());

        if (err != OK) {
            mVideoSource.clear();
            return err;
        }
    }

    return mVideoSource != NULL ? OK : UNKNOWN_ERROR;
}

void DSPGMediaPlayer::finishSeekIfNecessary(int64_t videoTimeUs) {
    if (!mSeeking) {
        return;
    }

    if (mAudioPlayer != NULL) {
        LOGV("seeking audio to %lld us (%.2f secs).", videoTimeUs, videoTimeUs / 1E6);

        // If we don't have a video time, seek audio to the originally
        // requested seek time instead.

        mAudioPlayer->seekTo(videoTimeUs < 0 ? mSeekTimeUs : videoTimeUs);
        mAudioPlayer->resume();
        mWatchForAudioSeekComplete = true;
        mWatchForAudioEOS = true;
    } else if (!mSeekNotificationSent) {
        // If we're playing video only, report seek complete now,
        // otherwise audio player will notify us later.
        notifyListener_l(MEDIA_SEEK_COMPLETE);
    }

    mFlags |= FIRST_FRAME;
    mSeeking = false;
    mSeekNotificationSent = false;
}

#ifdef ENABLE_PROFILING
static int64_t getNowms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (int64_t)(tv.tv_usec/1000) + tv.tv_sec * 1000ll;
}
#endif

void DSPGMediaPlayer::onVideoEvent() {
    Mutex::Autolock autoLock(mLock);
    if (!mVideoEventPending) {
        // The event has been cancelled in reset_l() but had already
        // been scheduled for execution at that time.
        return;
    }
    mVideoEventPending = false;

    if (mSeeking) {
        if (mLastVideoBuffer) {
            mLastVideoBuffer->release();
            mLastVideoBuffer = NULL;
        }

        if (mVideoBuffer) {
            mVideoBuffer->release();
            mVideoBuffer = NULL;
        }

        if (mCachedSource != NULL && mAudioSource != NULL) {
            // We're going to seek the video source first, followed by
            // the audio source.
            // In order to avoid jumps in the DataSource offset caused by
            // the audio codec prefetching data from the old locations
            // while the video codec is already reading data from the new
            // locations, we'll "pause" the audio source, causing it to
            // stop reading input data until a subsequent seek.

            if (mAudioPlayer != NULL) {
                mAudioPlayer->pause();
            }
            mAudioSource->pause();
        }
    }

    if (!mVideoBuffer) {
        MediaSource::ReadOptions options;
        if (mSeeking) {
            LOGV("seeking to %lld us (%.2f secs)", mSeekTimeUs, mSeekTimeUs / 1E6);

            options.setSeekTo(
                    mSeekTimeUs, MediaSource::ReadOptions::SEEK_CLOSEST_SYNC);
        }
        for (;;) {

#ifdef ENABLE_PROFILING
            mFrameCounter++;
            LOGD("VDecode: ->Decode, Num: %d, time=%lld", mFrameCounter, getNowms());
#endif
            status_t err = mVideoSource->read(&mVideoBuffer, &options);
#ifdef ENABLE_PROFILING
            LOGD("VDecode: Decode->Render, Num: %d, time=%lld", mFrameCounter, getNowms());
#endif
            // DSPG code change start
            mFrameRate++;
            // DSPG code change end
            options.clearSeekTo();

            if (err != OK) {
                CHECK_EQ(mVideoBuffer, NULL);

                if (err == INFO_FORMAT_CHANGED) {
                    LOGV("VideoSource signalled format change.");
#ifdef DYNAMIC_SCALING
					mReleaseBuffersImediately = false;
#endif

                    if (mVideoRenderer != NULL) {
                        mVideoRendererIsPreview = false;
                        err = initRenderer_l();

                        if (err == OK) {
                            continue;
                        }

                        // fall through
                    } else {
                        continue;
                    }
                }

#ifdef DYNAMIC_SCALING
                else if (err == INFO_RELEASE_BUFFERS) {
                    /* This event will come when we are trying to reconfigure the output
                       port of the decoder after the renderer has started to work.
                       We must release all the buffers on our side */
					LOGV("got INFO_RELEASE_BUFFERS");
					mReleaseBuffersImediately = true;

					if (mLastVideoBuffer) {
						mLastVideoBuffer->release();
						mLastVideoBuffer = NULL;
					}

					postVideoEvent_l();
                    return;
                }
#endif
                // So video playback is complete, but we may still have
                // a seek request pending that needs to be applied
                // to the audio track.
                if (mSeeking) {
                    LOGV("video stream ended while seeking!");
                }
                finishSeekIfNecessary(-1);

                mFlags |= VIDEO_AT_EOS;
                postStreamDoneEvent_l(err);
                return;
            }

            if (mVideoBuffer->range_length() == 0) {
                // Some decoders, notably the PV AVC software decoder
                // return spurious empty buffers that we just want to ignore.

                mVideoBuffer->release();
                mVideoBuffer = NULL;
                continue;
            }

            break;
        }
    }

    // DSPG code change start
	if (mVideoBuffer->meta_data()->findPointer(kKeyPlatformPrivate, &m_corruptedmacroBlock)) {
			if(m_corruptedmacroBlock != 0) {
				int corruptedMacroBlock = ((int)m_corruptedmacroBlock);
				LOGE("Number of blocks corrupted :%d",corruptedMacroBlock);
				if(corruptedMacroBlock != 0) {
					mCorruptedFrameRate++;
					mCorruptedMacroBlock = mCorruptedMacroBlock + (corruptedMacroBlock);
				}
				else {
					LOGE("no frame is corrupted");
				}
			}
	}
    // DSPG code change end

    int64_t timeUs;
    CHECK(mVideoBuffer->meta_data()->findInt64(kKeyTime, &timeUs));

    {
        Mutex::Autolock autoLock(mMiscStateLock);
        mVideoTimeUs = timeUs;
    }
#ifdef ENABLE_PROFILING
    LOGD("VDecode: Decode->Render, timeUs=%lld", timeUs);
#endif
    bool wasSeeking = mSeeking;
    finishSeekIfNecessary(timeUs);

    TimeSource *ts = (mFlags & AUDIO_AT_EOS) ? &mSystemTimeSource : mTimeSource;

    if (mFlags & FIRST_FRAME) {
        mFlags &= ~FIRST_FRAME;

        mTimeSourceDeltaUs = ts->getRealTimeUs() - timeUs;
    }

    int64_t realTimeUs, mediaTimeUs;
    if (!(mFlags & AUDIO_AT_EOS) && mAudioPlayer != NULL
        && mAudioPlayer->getMediaTimeMapping(&realTimeUs, &mediaTimeUs)) {
        mTimeSourceDeltaUs = realTimeUs - mediaTimeUs;
    }

    int64_t nowUs = ts->getRealTimeUs() - mTimeSourceDeltaUs;

    int64_t latenessUs = nowUs - timeUs;

    if (wasSeeking) {
        // Let's display the first frame after seeking right away.
        latenessUs = 0;
    }

    if (mRTPSession != NULL) {
        // We'll completely ignore timestamps for gtalk videochat
        // and we'll play incoming video as fast as we get it.
// DSPG Code Change Start
        // DSPG Video conferencing application needs sync timestamps
        //latenessUs = 0;
    }

    // LOGD("mOutofSyncCount = %d, lateness= %lld, lag=%d, ahead=%d" ,
    //            mOutofSyncCount, latenessUs, mVideoLagThreshold, mVideoAheadThreshold);

    if (latenessUs > mVideoLagThreshold) {
        // We're more than mVideoLagThreshold late.
        LOGD("we're late by %lld us (%.2f secs)", latenessUs, latenessUs / 1E6);
        if(mIsVideoCall && mOutofSyncCount > 10) {
            //giving up sync, wait till it catches up
        }
        else {
            mOutofSyncCount++;
            mVideoBuffer->release();
            mVideoBuffer = NULL;
            postVideoEvent_l();
            return;
        }
    }
    else {
        mOutofSyncCount  = 0;
    }

    if (latenessUs < mVideoAheadThreshold) {
        // We're more than mVideoAheadThreshold early.
        postVideoEvent_l(20000);
//DSPG Code Change End
        return;
    }

    if (mVideoRendererIsPreview || mVideoRenderer == NULL) {
        mVideoRendererIsPreview = false;

        status_t err = initRenderer_l();

        if (err != OK) {
            finishSeekIfNecessary(-1);

            mFlags |= VIDEO_AT_EOS;
            postStreamDoneEvent_l(err);
            return;
        }
    }

    if (mVideoRenderer != NULL) {
        mVideoRenderer->render(mVideoBuffer);
#ifdef ENABLE_PROFILING
        LOGD("VDecode: Render->Done, Num: %d, time=%lld", mFrameCounter, getNowms());
#endif
    }

    if (mLastVideoBuffer) {
        mLastVideoBuffer->release();
        mLastVideoBuffer = NULL;
    }
    mLastVideoBuffer = mVideoBuffer;
    mVideoBuffer = NULL;

#ifdef DYNAMIC_SCALING
	if (mReleaseBuffersImediately) {
		LOGV("We are in mReleaseBuffersImediately mode so release the current buffer");
		if (mLastVideoBuffer) {
			mLastVideoBuffer->release();
			mLastVideoBuffer = NULL;
		}
	}
#endif

	postVideoEvent_l();
}

void DSPGMediaPlayer::postVideoEvent_l(int64_t delayUs) {
    if (mVideoEventPending) {
        return;
    }

    mVideoEventPending = true;
    //mQueue.postEventWithDelay(mVideoEvent, delayUs < 0 ? 10000 : delayUs);
    (new AMessage(kWhatVideoEvent, mLocalReflector->id()))->post(delayUs < 0 ? 10000 : delayUs);
}

void DSPGMediaPlayer::postStreamDoneEvent_l(status_t status) {
    if (mStreamDoneEventPending) {
        return;
    }
    mStreamDoneEventPending = true;

    mStreamDoneStatus = status;
    mQueue.postEvent(mStreamDoneEvent);
}

void DSPGMediaPlayer::postBufferingEvent_l() {
    if (mBufferingEventPending) {
        return;
    }
    mBufferingEventPending = true;
    mQueue.postEventWithDelay(mBufferingEvent, 1000000ll);
}

void DSPGMediaPlayer::postCheckAudioStatusEvent_l() {
    if (mAudioStatusEventPending) {
        return;
    }
    mAudioStatusEventPending = true;
    mQueue.postEvent(mCheckAudioStatusEvent);
}

void DSPGMediaPlayer::onCheckAudioStatus() {
    Mutex::Autolock autoLock(mLock);
    if (!mAudioStatusEventPending) {
        // Event was dispatched and while we were blocking on the mutex,
        // has already been cancelled.
        return;
    }

    mAudioStatusEventPending = false;

    if (mWatchForAudioSeekComplete && !mAudioPlayer->isSeeking()) {
        mWatchForAudioSeekComplete = false;

        if (!mSeekNotificationSent) {
            notifyListener_l(MEDIA_SEEK_COMPLETE);
            mSeekNotificationSent = true;
        }

        mSeeking = false;
    }

    status_t finalStatus;
    if (mWatchForAudioEOS && mAudioPlayer->reachedEOS(&finalStatus)) {
        mWatchForAudioEOS = false;
        mFlags |= AUDIO_AT_EOS;
        mFlags |= FIRST_FRAME;
        postStreamDoneEvent_l(finalStatus);
    }
}

status_t DSPGMediaPlayer::prepare() {
    Mutex::Autolock autoLock(mLock);
    return prepare_l();
}

status_t DSPGMediaPlayer::prepare_l() {
    if (mFlags & PREPARED) {
        return OK;
    }

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;
    }

    mIsAsyncPrepare = false;
    status_t err = prepareAsync_l();

    if (err != OK) {
        return err;
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    return mPrepareResult;
}

status_t DSPGMediaPlayer::prepareAsync(int aRTPSocket, int aRTCPSocket, int vRTPSocket, int vRTCPSocket) {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    maRTPSocket = aRTPSocket;
    maRTCPSocket = aRTCPSocket;
    mvRTPSocket = vRTPSocket;
    mvRTCPSocket = vRTCPSocket;

    mIsAsyncPrepare = true;
    return prepareAsync_l();
}

status_t DSPGMediaPlayer::prepareAsync() {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    mIsAsyncPrepare = true;
    return prepareAsync_l();
}

status_t DSPGMediaPlayer::prepareAsync_l() {
    if (mFlags & PREPARING) {
        return UNKNOWN_ERROR;  // async prepare already pending
    }

    if (!mQueueStarted) {
        mQueue.start();
        mQueueStarted = true;
    }

    mFlags |= PREPARING;
    mAsyncPrepareEvent = new DSPGPlayerEvent(
            this, &DSPGMediaPlayer::onPrepareAsyncEvent);

    mQueue.postEvent(mAsyncPrepareEvent);

    return OK;
}

status_t DSPGMediaPlayer::finishSetDataSource_l() {
    sp<DataSource> dataSource;

    if (!strncasecmp("http://", mUri.string(), 7)) {
        mConnectingDataSource = new NuHTTPDataSource;

        mLock.unlock();
        status_t err = mConnectingDataSource->connect(mUri, &mUriHeaders);
        mLock.lock();

        if (err != OK) {
            mConnectingDataSource.clear();

            LOGI("mConnectingDataSource->connect() returned %d", err);
            return err;
        }

#if 0
        mCachedSource = new NuCachedSource2(
                new ThrottledSource(
                    mConnectingDataSource, 50 * 1024 /* bytes/sec */));
#else
        mCachedSource = new NuCachedSource2(mConnectingDataSource);
#endif
        mConnectingDataSource.clear();

        dataSource = mCachedSource;

        // We're going to prefill the cache before trying to instantiate
        // the extractor below, as the latter is an operation that otherwise
        // could block on the datasource for a significant amount of time.
        // During that time we'd be unable to abort the preparation phase
        // without this prefill.

        mLock.unlock();

        for (;;) {
            bool eos;
            size_t cachedDataRemaining =
                mCachedSource->approxDataRemaining(&eos);

            if (eos || cachedDataRemaining >= kHighWaterMarkBytes
                    || (mFlags & PREPARE_CANCELLED)) {
                break;
            }

            usleep(200000);
        }

        mLock.lock();

        if (mFlags & PREPARE_CANCELLED) {
            LOGI("Prepare cancelled while waiting for initial cache fill.");
            return UNKNOWN_ERROR;
        }
    } else if (!strncasecmp(mUri.string(), "httplive://", 11)) {
        String8 uri("http://");
        uri.append(mUri.string() + 11);

        sp<LiveSource> liveSource = new LiveSource(uri.string());

        mCachedSource = new NuCachedSource2(liveSource);
        dataSource = mCachedSource;

        sp<MediaExtractor> extractor =
            MediaExtractor::Create(dataSource, MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

        static_cast<MPEG2TSExtractor *>(extractor.get())
            ->setLiveSource(liveSource);

        return setDataSource_l(extractor);
    } else if (!strncmp("rtsp://gtalk/", mUri.string(), 13)) {
        if (mLooper == NULL) {
            mLooper = new ALooper;
            mLooper->setName("gtalk rtp");
            mLooper->start(
                    false /* runOnCallingThread */,
                    false /* canCallJava */,
                    PRIORITY_HIGHEST);
        }

        const char *startOfCodecString = &mUri.string()[13];
        const char *startOfSlash1 = strchr(startOfCodecString, '/');
        if (startOfSlash1 == NULL) {
            return BAD_VALUE;
        }
        const char *startOfWidthString = &startOfSlash1[1];
        const char *startOfSlash2 = strchr(startOfWidthString, '/');
        if (startOfSlash2 == NULL) {
            return BAD_VALUE;
        }
        const char *startOfHeightString = &startOfSlash2[1];

        String8 codecString(startOfCodecString, startOfSlash1 - startOfCodecString);
        String8 widthString(startOfWidthString, startOfSlash2 - startOfWidthString);
        String8 heightString(startOfHeightString);

#if 0
        mRTPPusher = new UDPPusher("/data/misc/rtpout.bin", 5434);
        mLooper->registerHandler(mRTPPusher);

        mRTCPPusher = new UDPPusher("/data/misc/rtcpout.bin", 5435);
        mLooper->registerHandler(mRTCPPusher);
#endif

        mRTPSession = new DSPGARTPSession;
        mLooper->registerHandler(mRTPSession);

#if 0
        // My AMR SDP
        static const char *raw =
            "v=0\r\n"
            "o=- 64 233572944 IN IP4 127.0.0.0\r\n"
            "s=QuickTime\r\n"
            "t=0 0\r\n"
            "a=range:npt=0-315\r\n"
            "a=isma-compliance:2,2.0,2\r\n"
            "m=audio 5434 RTP/AVP 97\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "b=AS:30\r\n"
            "a=rtpmap:97 AMR/8000/1\r\n"
            "a=fmtp:97 octet-align\r\n";
#elif 1
        String8 sdp;
        sdp.appendFormat(
            "v=0\r\n"
            "o=- 64 233572944 IN IP4 127.0.0.0\r\n"
            "s=QuickTime\r\n"
            "t=0 0\r\n"
            "a=range:npt=0-315\r\n"
            "a=isma-compliance:2,2.0,2\r\n"
            "m=video 5434 RTP/AVP 97\r\n"
            "c=IN IP4 127.0.0.1\r\n"
            "b=AS:30\r\n"
            "a=rtpmap:97 %s/90000\r\n"
            "a=cliprect:0,0,%s,%s\r\n"
            "a=framesize:97 %s-%s\r\n",

            codecString.string(),
            heightString.string(), widthString.string(),
            widthString.string(), heightString.string()
            );
        const char *raw = sdp.string();

#endif

        sp<DSPGASessionDescription> desc = new DSPGASessionDescription;
        CHECK(desc->setTo(raw, strlen(raw)));

        CHECK_EQ(mRTPSession->setup(desc), (status_t)OK);

        if (mRTPPusher != NULL) {
            mRTPPusher->start();
        }

        if (mRTCPPusher != NULL) {
            mRTCPPusher->start();
        }

        CHECK_EQ(mRTPSession->countTracks(), 1u);
        sp<MediaSource> source = mRTPSession->trackAt(0);

#if 0
        bool eos;
        while (((DSPGAPacketSource *)source.get())
                ->getQueuedDuration(&eos) < 5000000ll && !eos) {
            usleep(100000ll);
        }
#endif

        const char *mime;
        CHECK(source->getFormat()->findCString(kKeyMIMEType, &mime));

        if (!strncasecmp("video/", mime, 6)) {
            setVideoSource(source);
        } else {
            CHECK(!strncasecmp("audio/", mime, 6));
            setAudioSource(source);
        }

        mExtractorFlags = MediaExtractor::CAN_PAUSE;

        return OK;
// DSPG Code Change Start
    } else if (!strncmp("sdp://dspg/", mUri.string(), 11)){
        if (mLooper == NULL) {
            mLooper = new ALooper;
            mLooper->setName("dspg rtp");
            mLooper->start(
                    false /* runOnCallingThread */,
                    false /* canCallJava */,
                    PRIORITY_AUDIO);
        }

        mRTPSession = new DSPGARTPSession;
        mLooper->registerHandler(mRTPSession);

        const char *raw =mUri.string()+strlen("sdp://dspg/test\r\n");
        LOGD("********* SDP from DSPG*************");
        LOGD("%s", mUri.string());

        sp<DSPGASessionDescription> desc = new DSPGASessionDescription;
        CHECK(desc->setTo(raw, strlen(raw)));

        CHECK_EQ(mRTPSession->setup(desc, maRTPSocket, maRTCPSocket, mvRTPSocket, mvRTCPSocket), (status_t)OK);

        if (mRTPPusher != NULL) {
            mRTPPusher->start();
        }

        if (mRTCPPusher != NULL) {
            mRTCPPusher->start();
        }

        LOGI("DSPG RTP Session countTracks: %d", mRTPSession->countTracks());
        //CHECK_EQ(mRTPSession->countTracks(), 2u);

        for (size_t tcount = 0; tcount < mRTPSession->countTracks(); ++tcount) {
            sp<MediaSource> source = mRTPSession->trackAt(tcount);
            const char *mime;
            CHECK(source->getFormat()->findCString(kKeyMIMEType, &mime));
            LOGI("DSPG RTP Session Track mime: %s", mime);

            if (!strncasecmp("video/", mime, 6)) {
                setVideoSource(source);
            } else {
                CHECK(!strncasecmp("audio/", mime, 6));
                setAudioSource(source);
            }
        }

        mExtractorFlags = MediaExtractor::CAN_PAUSE;
        return OK;
//DSPG Code Change End
    } else if (!strncasecmp("rtsp://", mUri.string(), 7)) {
        if (mLooper == NULL) {
            mLooper = new ALooper;
            mLooper->setName("rtsp");
            mLooper->start();
        }
        mRTSPController = new DSPGARTSPController(mLooper);
        status_t err = mRTSPController->connect(mUri.string());

        LOGI("DSPGARTSPController::connect returned %d", err);

        if (err != OK) {
            mRTSPController.clear();
            return err;
        }

        sp<MediaExtractor> extractor = mRTSPController.get();
        return setDataSource_l(extractor);
    } else {
        dataSource = DataSource::CreateFromURI(mUri.string(), &mUriHeaders);
    }

    if (dataSource == NULL) {
        return UNKNOWN_ERROR;
    }

    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);

    if (extractor == NULL) {
        return UNKNOWN_ERROR;
    }

    return setDataSource_l(extractor);
}

void DSPGMediaPlayer::abortPrepare(status_t err) {
    CHECK(err != OK);

    if (mIsAsyncPrepare) {
        notifyListener_l(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN, err);
    }

    mPrepareResult = err;
    mFlags &= ~(PREPARING|PREPARE_CANCELLED);
    mAsyncPrepareEvent = NULL;
    mPreparedCondition.broadcast();
}

// static
bool DSPGMediaPlayer::ContinuePreparation(void *cookie) {
    DSPGMediaPlayer *me = static_cast<DSPGMediaPlayer *>(cookie);

    return (me->mFlags & PREPARE_CANCELLED) == 0;
}

void DSPGMediaPlayer::onPrepareAsyncEvent() {
    Mutex::Autolock autoLock(mLock);

    if (mFlags & PREPARE_CANCELLED) {
        LOGI("prepare was cancelled before doing anything");
        abortPrepare(UNKNOWN_ERROR);
        return;
    }

    if (mUri.size() > 0) {
        status_t err = finishSetDataSource_l();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mVideoTrack != NULL && mVideoSource == NULL) {
        status_t err = initVideoDecoder();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mAudioTrack != NULL && mAudioSource == NULL) {
        status_t err = initAudioDecoder();

        if (err != OK) {
            abortPrepare(err);
            return;
        }
    }

    if (mCachedSource != NULL || mRTSPController != NULL) {
        postBufferingEvent_l();
    } else {
        finishAsyncPrepare_l();
    }
}

void DSPGMediaPlayer::finishAsyncPrepare_l() {
    if (mIsAsyncPrepare) {
        if (mVideoWidth < 0 || mVideoHeight < 0) {
            notifyListener_l(MEDIA_SET_VIDEO_SIZE, 0, 0);
        } else {
            int32_t rotationDegrees;
            if (!mVideoTrack->getFormat()->findInt32(
                        kKeyRotation, &rotationDegrees)) {
                rotationDegrees = 0;
            }

#if 1
            if (rotationDegrees == 90 || rotationDegrees == 270) {
                notifyListener_l(
                        MEDIA_SET_VIDEO_SIZE, mVideoHeight, mVideoWidth);
            } else
#endif
            {
                notifyListener_l(
                        MEDIA_SET_VIDEO_SIZE, mVideoWidth, mVideoHeight);
            }
        }

        notifyListener_l(MEDIA_PREPARED);
    }

    mPrepareResult = OK;
    mFlags &= ~(PREPARING|PREPARE_CANCELLED);
    mFlags |= PREPARED;
    mAsyncPrepareEvent = NULL;
    mPreparedCondition.broadcast();
}

status_t DSPGMediaPlayer::suspend() {
    LOGV("suspend");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState != NULL) {
        if (mLastVideoBuffer == NULL) {
            //go into here if video is suspended again
            //after resuming without being played between
            //them
            SuspensionState *state = mSuspensionState;
            mSuspensionState = NULL;
            reset_l();
            mSuspensionState = state;
            return OK;
        }

        delete mSuspensionState;
        mSuspensionState = NULL;
    }

    if (mFlags & PREPARING) {
        mFlags |= PREPARE_CANCELLED;
        if (mConnectingDataSource != NULL) {
            LOGI("interrupting the connection process");
            mConnectingDataSource->disconnect();
        }
    }

    while (mFlags & PREPARING) {
        mPreparedCondition.wait(mLock);
    }

    SuspensionState *state = new SuspensionState;
    state->mUri = mUri;
    state->mUriHeaders = mUriHeaders;
    state->mFileSource = mFileSource;

    state->mFlags = mFlags & (PLAYING | AUTO_LOOPING | LOOPING | AT_EOS);
    getPosition(&state->mPositionUs);

    if (mLastVideoBuffer) {
        size_t size = mLastVideoBuffer->range_length();

        if (size) {
            int32_t unreadable;
            if (!mLastVideoBuffer->meta_data()->findInt32(
                        kKeyIsUnreadable, &unreadable)
                    || unreadable == 0) {
                state->mLastVideoFrameSize = size;
                state->mLastVideoFrame = malloc(size);
                memcpy(state->mLastVideoFrame,
                       (const uint8_t *)mLastVideoBuffer->data()
                            + mLastVideoBuffer->range_offset(),
                       size);

                state->mVideoWidth = mVideoWidth;
                state->mVideoHeight = mVideoHeight;

                sp<MetaData> meta = mVideoSource->getFormat();
                CHECK(meta->findInt32(kKeyColorFormat, &state->mColorFormat));
                CHECK(meta->findInt32(kKeyWidth, &state->mDecodedWidth));
                CHECK(meta->findInt32(kKeyHeight, &state->mDecodedHeight));
            } else {
                LOGV("Unable to save last video frame, we have no access to "
                     "the decoded video data.");
            }
        }
    }

    reset_l();

    mSuspensionState = state;

    return OK;
}

status_t DSPGMediaPlayer::resume() {
    LOGV("resume");
    Mutex::Autolock autoLock(mLock);

    if (mSuspensionState == NULL) {
        return INVALID_OPERATION;
    }

    SuspensionState *state = mSuspensionState;
    mSuspensionState = NULL;

    status_t err;
    if (state->mFileSource != NULL) {
        err = setDataSource_l(state->mFileSource);

        if (err == OK) {
            mFileSource = state->mFileSource;
        }
    } else {
        err = setDataSource_l(state->mUri, &state->mUriHeaders);
    }

    if (err != OK) {
        delete state;
        state = NULL;

        return err;
    }

    seekTo_l(state->mPositionUs);

    mFlags = state->mFlags & (AUTO_LOOPING | LOOPING | AT_EOS);

    if (state->mLastVideoFrame && mISurface != NULL) {
        mVideoRenderer =
            new DSPGLocalRenderer(
                    true,  // previewOnly
                    "",
                    (OMX_COLOR_FORMATTYPE)state->mColorFormat,
                    mISurface,
                    state->mVideoWidth,
                    state->mVideoHeight,
                    state->mDecodedWidth,
                    state->mDecodedHeight,
                    0);

        mVideoRendererIsPreview = true;

        ((DSPGLocalRenderer *)mVideoRenderer.get())->render(
                state->mLastVideoFrame, state->mLastVideoFrameSize);
    }

    if (state->mFlags & PLAYING) {
        play_l();
    }

    mSuspensionState = state;
    state = NULL;

    return OK;
}

uint32_t DSPGMediaPlayer::flags() const {
    return mExtractorFlags;
}

void DSPGMediaPlayer::postAudioEOS() {
    postCheckAudioStatusEvent_l();
}

void DSPGMediaPlayer::postAudioSeekComplete() {
    postCheckAudioStatusEvent_l();
}

}  // namespace android

