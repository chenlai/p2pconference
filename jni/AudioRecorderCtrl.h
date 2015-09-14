/**
 * Class to wrap encoder controls not available in stock SF
 * OMX direct handle calls are in here
 */

#ifndef AUDIORECORDER_CTRL_H_

#define AUDIORECORDER_CTRL_H_

#include <media/MediaRecorderBase.h>
#include <utils/String8.h>
#include <DSPGAudioSource.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaWriter.h>

struct AudioRecorderCtrl {
public:

	AudioRecorderCtrl();
	~AudioRecorderCtrl();

    android::status_t prepare();
    android::status_t start(const android::sp<android::MetaData> &metaData, int remoteport, int aRTPSocket, int aRTCPSocket);
    android::status_t stop();
    android::status_t setAudioSource(android::audio_source as);
    android::status_t setAudioEncoder(android::audio_encoder ae);
    android::status_t setDefaults();

private:
    android::audio_source mAudioSource;
    android::audio_encoder mAudioEncoder;
    int32_t mAudioBitRate;
    int32_t mAudioChannels;
    int32_t mSampleRate;
    android::sp<android::MediaSource> mAudioEncoderSource;
    android::sp<android::MediaWriter> mWriter;
    int32_t mAudioTimeScale;
    android::sp<android::DSPGAudioSource> mAudioSourceNode;
    android::sp<android::IMediaRecorderClient> mListener;

    AudioRecorderCtrl   *m_pAudioRecorder ;


    android::status_t startRTPRecording(const char *remoteIP, int remoteport, int aRTPSocket, int aRTCPSocket);
    android::sp<android::MediaSource> createAudioSource();
    android::status_t setupAudioEncoder(const android::sp<android::MediaWriter>& writer);

    // Encoding parameter handling utilities
    android::status_t setParameter(const android::String8 &key, const android::String8 &value);
    android::status_t setParamAudioEncodingBitRate(int32_t bitRate);
    android::status_t setParamAudioNumberOfChannels(int32_t channles);
    android::status_t setParamAudioSamplingRate(int32_t sampleRate);

    AudioRecorderCtrl(const AudioRecorderCtrl &);
    AudioRecorderCtrl &operator=(const AudioRecorderCtrl &);

};

//}

#endif
