/*
 * Copyright (C) 2012, DSP Group Inc.
 *
 * Component        : player
 * Module           : P2PVideoPhoneApp
 * Filename         : DSPGAudioOutput.h
 * Description      : Implemented based on android's MediaPlayerService::AudioOutput class
 *
 */
 
#ifndef DSPG_AUDIO_OUTPUT_H
#define DSPG_AUDIO_OUTPUT_H

#include <utils/Log.h>
#include <utils/threads.h>
#include <utils/List.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include <media/IMediaPlayerService.h>
#include <media/MediaPlayerInterface.h>
#include <media/Metadata.h>
#include <media/AudioTrack.h>

namespace android {

class IMediaRecorder;
class IMediaMetadataRetriever;
class IOMX;
class MediaRecorderClient;

class DSPGAudioOutput : public MediaPlayerBase::AudioSink
{
public:
//                            DSPGAudioOutput(int sessionId);
                            DSPGAudioOutput();
    virtual                 ~DSPGAudioOutput();

    virtual bool            ready() const { return mTrack != NULL; }
    virtual bool            realtime() const { return true; }
    virtual ssize_t         bufferSize() const;
    virtual ssize_t         frameCount() const;
    virtual ssize_t         channelCount() const;
    virtual ssize_t         frameSize() const;
    virtual uint32_t        latency() const;
    virtual float           msecsPerFrame() const;
    virtual status_t        getPosition(uint32_t *position);
    virtual int             getSessionId();

    virtual status_t        open(
            uint32_t sampleRate, int channelCount,
            int format, int bufferCount,
            AudioCallback cb, void *cookie);

    virtual void            start();
    virtual ssize_t         write(const void* buffer, size_t size);
    virtual void            stop();
    virtual void            flush();
    virtual void            pause();
    virtual void            close();
            void            setAudioStreamType(int streamType) { mStreamType = streamType; }
            void            setVolume(float left, float right);
            status_t        setAuxEffectSendLevel(float level);
            status_t        attachAuxEffect(int effectId);
    virtual status_t        dump(int fd, const Vector<String16>& args) const;

    static bool             isOnEmulator();
    static int              getMinBufferCount();
private:
    static void             setMinBufferCount();
    static void             CallbackWrapper(
            int event, void *me, void *info);

    AudioTrack*             mTrack;
    AudioCallback           mCallback;
    void *                  mCallbackCookie;
    int                     mStreamType;
    float                   mLeftVolume;
    float                   mRightVolume;
    float                   mMsecsPerFrame;
    uint32_t                mLatency;
    int                     mSessionId;
    float                   mSendLevel;
    int                     mAuxEffectId;
    static bool             mIsOnEmulator;
    static int              mMinBufferCount;  // 12 for emulator; otherwise 4

};

}; // namespace android

#endif //DSPG_AUDIO_OUTPUT_H

