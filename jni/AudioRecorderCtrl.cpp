
#define LOG_NDEBUG 0
#define LOG_TAG "AudioRecorderCtrl"

#include <utils/Log.h>
#include "utility.h"

#include "MediaConstants.h"
#include "AudioRecorderCtrl.h"

#include <binder/IPCThreadState.h>
#include <media/stagefright/AudioSource.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>
#include <media/MediaProfiles.h>
#include <utils/Errors.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include "ARTPStreamer.h"


using namespace android;


AudioRecorderCtrl::AudioRecorderCtrl() {
    F_LOG;
}

AudioRecorderCtrl::~AudioRecorderCtrl() {
    F_LOG;
}

int AudioRecorderCtrl::prepare(){
    F_LOG;
    return OK;
}

int AudioRecorderCtrl::start(const sp<MetaData> &metaData, int remoteport, int aRTPSocket, int aRTCPSocket){
    F_LOG;

    const char *remoteIP ;

	metaData->findCString(kKeyP2PRemoteIP, (const char**)&remoteIP);
    return startRTPRecording(remoteIP, remoteport, aRTPSocket, aRTCPSocket);
}

int AudioRecorderCtrl::stop() {
    F_LOG;
    status_t err = OK;
    if (mWriter != NULL) {
        err = mWriter->stop();
        mWriter.clear();
        mWriter = NULL;
    }
    mAudioSourceNode.clear();
    mAudioSourceNode = NULL;
    return OK;
}

int AudioRecorderCtrl::setDefaults() {
    F_LOG;
    mAudioSource = AUDIO_SOURCE_MIC;
    mAudioEncoder = AUDIO_ENCODER_AMR_NB;
    mSampleRate    = 8000;
    mAudioChannels = 1;
    mAudioBitRate  = 32000;
    mAudioTimeScale  = -1;
    mAudioSourceNode = 0;
    mListener = NULL;
    return OK;
}


status_t AudioRecorderCtrl::setAudioSource(audio_source as) {
    LOGV("setAudioSource: %d", as);
    if (as < AUDIO_SOURCE_DEFAULT ||
        as >= AUDIO_SOURCE_LIST_END) {
        LOGE("Invalid audio source: %d", as);
        return BAD_VALUE;
    }

    if (as == AUDIO_SOURCE_DEFAULT) {
        mAudioSource = AUDIO_SOURCE_MIC;
    } else {
        mAudioSource = as;
    }

    return OK;
}

status_t AudioRecorderCtrl::setAudioEncoder(audio_encoder ae) {
    LOGV("setAudioEncoder: %d", ae);
    if (ae < AUDIO_ENCODER_DEFAULT ||
        ae >= AUDIO_ENCODER_LIST_END) {
        LOGE("Invalid audio encoder: %d", ae);
        return BAD_VALUE;
    }

    if (ae == AUDIO_ENCODER_DEFAULT) {
        mAudioEncoder = AUDIO_ENCODER_AMR_NB;
    } else {
        mAudioEncoder = ae;
    }

    return OK;
}

// Attempt to parse an int64 literal optionally surrounded by whitespace,
// returns true on success, false otherwise.
static bool safe_strtoi64(const char *s, int64_t *val) {
    char *end;
    *val = strtoll(s, &end, 10);

    if (end == s || errno == ERANGE) {
        return false;
    }

    // Skip trailing whitespace
    while (isspace(*end)) {
        ++end;
    }

    // For a successful return, the string must contain nothing but a valid
    // int64 literal optionally surrounded by whitespace.

    return *end == '\0';
}

// Return true if the value is in [0, 0x007FFFFFFF]
static bool safe_strtoi32(const char *s, int32_t *val) {
    int64_t temp;
    if (safe_strtoi64(s, &temp)) {
        if (temp >= 0 && temp <= 0x007FFFFFFF) {
            *val = static_cast<int32_t>(temp);
            return true;
        }
    }
    return false;
}

// Trim both leading and trailing whitespace from the given string.
static void TrimString(String8 *s) {
    size_t num_bytes = s->bytes();
    const char *data = s->string();

    size_t leading_space = 0;
    while (leading_space < num_bytes && isspace(data[leading_space])) {
        ++leading_space;
    }

    size_t i = num_bytes;
    while (i > leading_space && isspace(data[i - 1])) {
        --i;
    }

    s->setTo(String8(&data[leading_space], i - leading_space));
}


status_t AudioRecorderCtrl::setParamAudioSamplingRate(int32_t sampleRate) {
    LOGV("setParamAudioSamplingRate: %d", sampleRate);
    if (sampleRate <= 0) {
        LOGE("Invalid audio sampling rate: %d", sampleRate);
        return BAD_VALUE;
    }

    // Additional check on the sample rate will be performed later.
    mSampleRate = sampleRate;
    return OK;
}

status_t AudioRecorderCtrl::setParamAudioNumberOfChannels(int32_t channels) {
    LOGV("setParamAudioNumberOfChannels: %d", channels);
    if (channels <= 0 || channels >= 3) {
        LOGE("Invalid number of audio channels: %d", channels);
        return BAD_VALUE;
    }

    // Additional check on the number of channels will be performed later.
    mAudioChannels = channels;
    return OK;
}

status_t AudioRecorderCtrl::setParamAudioEncodingBitRate(int32_t bitRate) {
    LOGV("setParamAudioEncodingBitRate: %d", bitRate);
    if (bitRate <= 0) {
        LOGE("Invalid audio encoding bit rate: %d", bitRate);
        return BAD_VALUE;
    }

    // The target bit rate may not be exactly the same as the requested.
    // It depends on many factors, such as rate control, and the bit rate
    // range that a specific encoder supports. The mismatch between the
    // the target and requested bit rate will NOT be treated as an error.
    mAudioBitRate = bitRate;
    return OK;
}


status_t AudioRecorderCtrl::setParameter(
        const String8 &key, const String8 &value) {
    LOGV("setParameter: key (%s) => value (%s)", key.string(), value.string());
    if (key == "audio-param-sampling-rate") {
        int32_t sampling_rate;
        if (safe_strtoi32(value.string(), &sampling_rate)) {
            return setParamAudioSamplingRate(sampling_rate);
        }
    } else if (key == "audio-param-number-of-channels") {
        int32_t number_of_channels;
        if (safe_strtoi32(value.string(), &number_of_channels)) {
            return setParamAudioNumberOfChannels(number_of_channels);
        }
    } else if (key == "audio-param-encoding-bitrate") {
        int32_t audio_bitrate;
        if (safe_strtoi32(value.string(), &audio_bitrate)) {
            return setParamAudioEncodingBitRate(audio_bitrate);
        }
    } else {
        LOGE("setParameter: failed to find key %s", key.string());
    }
    return BAD_VALUE;
}

sp<MediaSource> AudioRecorderCtrl::createAudioSource() {

    LOGD("audio source : rate=%d, channel=%d, source=%d", mSampleRate, mAudioChannels, mAudioSource);

    sp<DSPGAudioSource> audioSource =
        new DSPGAudioSource(
                mAudioSource,
                mSampleRate,
                mAudioChannels);

    status_t err = audioSource->initCheck();

    if (err != OK) {
        LOGE("audio source is not initialized");
        return NULL;
    }

    sp<MetaData> encMeta = new MetaData;
    const char *mime;
    mAudioEncoder = AUDIO_ENCODER_AMR_NB;
    switch (mAudioEncoder) {
        case AUDIO_ENCODER_AMR_NB:
        case AUDIO_ENCODER_DEFAULT:
            mime = MEDIA_MIMETYPE_AUDIO_AMR_NB;
            break;
        case AUDIO_ENCODER_AMR_WB:
            mime = MEDIA_MIMETYPE_AUDIO_AMR_WB;
            break;
        case AUDIO_ENCODER_AAC:
            mime = MEDIA_MIMETYPE_AUDIO_AAC;
            break;
        default:
            LOGE("Unknown audio encoder: %d", mAudioEncoder);
            return NULL;
    }
    encMeta->setCString(kKeyMIMEType, mime);

    int32_t maxInputSize;
    CHECK(audioSource->getFormat()->findInt32(
                kKeyMaxInputSize, &maxInputSize));

    encMeta->setInt32(kKeyMaxInputSize, maxInputSize);
    encMeta->setInt32(kKeyChannelCount, mAudioChannels);
    encMeta->setInt32(kKeySampleRate, mSampleRate);
    encMeta->setInt32(kKeyBitRate, mAudioBitRate);
    if (mAudioTimeScale > 0) {
        encMeta->setInt32(kKeyTimeScale, mAudioTimeScale);
    }

    OMXClient client;
    CHECK_EQ(client.connect(), OK);

    sp<MediaSource> audioEncoder =
        OMXCodec::Create(client.interface(), encMeta,
                         true /* createEncoder */, audioSource);
    mAudioSourceNode = audioSource;

    return audioEncoder;
}

status_t AudioRecorderCtrl::startRTPRecording(const char *remoteIP, int remoteport, int aRTPSocket, int aRTCPSocket) {

    LOGD("startRTPRecording..");
    LOGD("mAudioSource : %d ", mAudioSource);
    if (mAudioSource == AUDIO_SOURCE_LIST_END) {
        // Must have exactly one source.
        LOGD("returning mAudioSource....");
        return BAD_VALUE;
    }

    sp<MediaSource> source;

    if (mAudioSource != AUDIO_SOURCE_LIST_END) {
        source = createAudioSource();
    }

    mWriter = new ARTPStreamer(remoteport, aRTPSocket, aRTCPSocket, remoteIP);
    if (mWriter != NULL) {
        mWriter->addSource(source);
        mWriter->setListener(mListener);
        return mWriter->start();
    } else {
        LOGD("Failed to instantiate ARTPStreamer");
        return NO_MEMORY;
    }
}
