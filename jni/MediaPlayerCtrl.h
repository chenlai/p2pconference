
#ifndef MEDIA_PLAYER_CTRL_H_
#define MEDIA_PLAYER_CTRL_H_

#include "DSPGMediaPlayer.h"
#include "DSPGTimedEventQueue.h"
#include <surfaceflinger/Surface.h>
#include <media/mediaplayer.h>
#include <media/stagefright/foundation/AString.h>
#include "Transport.h"
#include "VideoRecorderCtrl.h"
#include "AudioRecorderCtrl.h"
#include "DSPGAudioOutput.h"


class MediaPlayerCtrl {
public:

	MediaPlayerCtrl();
	virtual ~MediaPlayerCtrl();

    android::status_t init ();
	android::status_t play(const android::sp<android::MetaData> &metaData, int aRTPSocket, int aRTCPSocket, 
	                       int vRTPSocket, int vRTCPSocket);
	android::status_t stop();

    static void PlayerCb (int msg, int ext1, int ext2, void * cbData);
    /* SDP callback */	
    void encoderSdpHandler (const char *pseqParam, const char *ppicParam, const char *pprofileId);
    void getDecoderInfo(int *decoderInfo);

private:
    android::Mutex                            mLock;
    android::Condition                        mCondition;
    android::sp<android::DSPGMediaPlayer>     m_mediaPlayer ;
    android::sp<android::DSPGAudioOutput>     m_audioOutput ;
    android::AString                          *m_psdpInfo ;

    android::DSPGTimedEventQueue                    mQueue;
    bool                                        mQueueStarted;
    android::sp<android::DSPGTimedEventQueue::Event>         mPrepareEvent;

    /* H264 SDP information */
    android::AString mProfileLevel;
    android::AString mSeqParamSet;
    android::AString mPicParamSet;

    bool                mIsPrepareInProgress;
    bool                mIsStopReqPending;
    android::Condition  mPrepareCheck;
    
    void onPrepareEvent();
    void cancelPlayerEvents();
	android::status_t prepare(const android::sp<android::MetaData> &metaData, int aRTPSocket, int aRTCPSocket, 
	                        int vRTPSocket, int vRTCPSocket);
    android::status_t createSDP(android::AString &sdp, const android::sp<android::MetaData> &metaData);
};


#endif /* MEDIA_PLAYER_CTRL_H_ */
