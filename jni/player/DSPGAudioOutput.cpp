/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAudioOutput.cpp
 * Description      : Implemented based on android's MediaPlayerService::AudioOutput class
 *
 */
  
 //#define LOG_NDEBUG 0
#define LOG_TAG "DSPGAudioOutput"
#include <utils/Log.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <string.h>

#include <cutils/atomic.h>
#include <cutils/properties.h> // for property_get

#include <utils/misc.h>

#include <android_runtime/ActivityManager.h>

#include <DSPGAudioOutput.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryBase.h>
#include <utils/Errors.h>  // for status_t
#include <utils/String8.h>
#include <utils/SystemClock.h>
#include <utils/Vector.h>
#include <cutils/properties.h>

#include <media/MediaPlayerInterface.h>
#include <media/mediarecorder.h>
#include <media/MediaMetadataRetrieverInterface.h>
#include <media/Metadata.h>
#include <media/AudioTrack.h>

#include <media/PVPlayer.h>
#include <OMX.h>

namespace android {

int DSPGAudioOutput::mMinBufferCount = 4;
bool DSPGAudioOutput::mIsOnEmulator = false;

status_t DSPGAudioOutput::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    result.append(" DSPGAudioOutput\n");
    snprintf(buffer, 255, "  stream type(%d), left - right volume(%f, %f)\n",
            mStreamType, mLeftVolume, mRightVolume);
    result.append(buffer);
    snprintf(buffer, 255, "  msec per frame(%f), latency (%d)\n",
            mMsecsPerFrame, mLatency);
    result.append(buffer);
    snprintf(buffer, 255, "  aux effect id(%d), send level (%f)\n",
            mAuxEffectId, mSendLevel);
    result.append(buffer);

    ::write(fd, result.string(), result.size());
    if (mTrack != 0) {
        mTrack->dump(fd, args);
    }
    return NO_ERROR;
}

DSPGAudioOutput::DSPGAudioOutput()
    : mCallback(NULL),
      mCallbackCookie(NULL),
      mSessionId(0) {
    LOGV("DSPGAudioOutput", sessionId);
    mTrack = 0;
    mStreamType = AudioSystem::MUSIC;
    mLeftVolume = 1.0;
    mRightVolume = 1.0;
    mLatency = 0;
    mMsecsPerFrame = 0;
    mAuxEffectId = 0;
    mSendLevel = 0.0;
    setMinBufferCount();
}

DSPGAudioOutput::~DSPGAudioOutput()
{
    close();
}

void DSPGAudioOutput::setMinBufferCount()
{
    char value[PROPERTY_VALUE_MAX];
    if (property_get("ro.kernel.qemu", value, 0)) {
        mIsOnEmulator = true;
        mMinBufferCount = 12;  // to prevent systematic buffer underrun for emulator
    }
}

bool DSPGAudioOutput::isOnEmulator()
{
    setMinBufferCount();
    return mIsOnEmulator;
}

int DSPGAudioOutput::getMinBufferCount()
{
    setMinBufferCount();
    return mMinBufferCount;
}

ssize_t DSPGAudioOutput::bufferSize() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameCount() * frameSize();
}

ssize_t DSPGAudioOutput::frameCount() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameCount();
}

ssize_t DSPGAudioOutput::channelCount() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->channelCount();
}

ssize_t DSPGAudioOutput::frameSize() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameSize();
}

uint32_t DSPGAudioOutput::latency () const
{
    return mLatency;
}

float DSPGAudioOutput::msecsPerFrame() const
{
    return mMsecsPerFrame;
}

status_t DSPGAudioOutput::getPosition(uint32_t *position)
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->getPosition(position);
}

status_t DSPGAudioOutput::open(
        uint32_t sampleRate, int channelCount, int format, int bufferCount,
        AudioCallback cb, void *cookie)
{
    mCallback = cb;
    mCallbackCookie = cookie;

    // Check argument "bufferCount" against the mininum buffer count
    if (bufferCount < mMinBufferCount) {
        LOGD("bufferCount (%d) is too small and increased to %d", bufferCount, mMinBufferCount);
        bufferCount = mMinBufferCount;

    }
    LOGV("open(%u, %d, %d, %d, %d)", sampleRate, channelCount, format, bufferCount,mSessionId);
    if (mTrack) close();
    int afSampleRate;
    int afFrameCount;
    int frameCount;

    if (AudioSystem::getOutputFrameCount(&afFrameCount, mStreamType) != NO_ERROR) {
        return NO_INIT;
    }
    if (AudioSystem::getOutputSamplingRate(&afSampleRate, mStreamType) != NO_ERROR) {
        return NO_INIT;
    }

    frameCount = (sampleRate*afFrameCount*bufferCount)/afSampleRate;

    AudioTrack *t;
    if (mCallback != NULL) {
        t = new AudioTrack(
                mStreamType,
                sampleRate,
                format,
                (channelCount == 2) ? AudioSystem::CHANNEL_OUT_STEREO : AudioSystem::CHANNEL_OUT_MONO,
                frameCount,
                0 /* flags */,
                CallbackWrapper,
                this,
                0,
                mSessionId);
    } else {
        t = new AudioTrack(
                mStreamType,
                sampleRate,
                format,
                (channelCount == 2) ? AudioSystem::CHANNEL_OUT_STEREO : AudioSystem::CHANNEL_OUT_MONO,
                frameCount,
                0,
                NULL,
                NULL,
                0,
                mSessionId);
    }

    if ((t == 0) || (t->initCheck() != NO_ERROR)) {
        LOGE("Unable to create audio track");
        delete t;
        return NO_INIT;
    }

    LOGV("setVolume");
    t->setVolume(mLeftVolume, mRightVolume);

    mMsecsPerFrame = 1.e3 / (float) sampleRate;
    mLatency = t->latency();
    mTrack = t;

    t->setAuxEffectSendLevel(mSendLevel);
    return t->attachAuxEffect(mAuxEffectId);;
}

void DSPGAudioOutput::start()
{
    LOGV("start");
    if (mTrack) {
        mTrack->setVolume(mLeftVolume, mRightVolume);
        mTrack->setAuxEffectSendLevel(mSendLevel);
        mTrack->start();
    }
}



ssize_t DSPGAudioOutput::write(const void* buffer, size_t size)
{
    LOG_FATAL_IF(mCallback != NULL, "Don't call write if supplying a callback.");

    //LOGV("write(%p, %u)", buffer, size);
    if (mTrack) {
        ssize_t ret = mTrack->write(buffer, size);
        return ret;
    }
    return NO_INIT;
}

void DSPGAudioOutput::stop()
{
    LOGV("stop");
    if (mTrack) mTrack->stop();
}

void DSPGAudioOutput::flush()
{
    LOGV("flush");
    if (mTrack) mTrack->flush();
}

void DSPGAudioOutput::pause()
{
    LOGV("pause");
    if (mTrack) mTrack->pause();
}

void DSPGAudioOutput::close()
{
    LOGV("close");
    delete mTrack;
    mTrack = 0;
}

void DSPGAudioOutput::setVolume(float left, float right)
{
    LOGV("setVolume(%f, %f)", left, right);
    mLeftVolume = left;
    mRightVolume = right;
    if (mTrack) {
        mTrack->setVolume(left, right);
    }
}

status_t DSPGAudioOutput::setAuxEffectSendLevel(float level)
{
    LOGV("setAuxEffectSendLevel(%f)", level);
    mSendLevel = level;
    if (mTrack) {
        return mTrack->setAuxEffectSendLevel(level);
    }
    return NO_ERROR;
}

status_t DSPGAudioOutput::attachAuxEffect(int effectId)
{
    LOGV("attachAuxEffect(%d)", effectId);
    mAuxEffectId = effectId;
    if (mTrack) {
        return mTrack->attachAuxEffect(effectId);
    }
    return NO_ERROR;
}

// static
void DSPGAudioOutput::CallbackWrapper(
        int event, void *cookie, void *info) {
    //LOGV("callbackwrapper");
    if (event != AudioTrack::EVENT_MORE_DATA) {
        return;
    }

    DSPGAudioOutput *me = (DSPGAudioOutput *)cookie;
    AudioTrack::Buffer *buffer = (AudioTrack::Buffer *)info;

    size_t actualSize = (*me->mCallback)(
            me, buffer->raw, buffer->size, me->mCallbackCookie);

    buffer->size = actualSize;

}

int DSPGAudioOutput::getSessionId()
{
    return mSessionId;
}

} // namespace android
