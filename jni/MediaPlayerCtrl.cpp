#define LOG_NDEBUG 0
#define LOG_TAG "MediaPlayerCtrl"

#include "debugging.hpp"
#undef LOG // Collides with Android

#include "MediaPlayerCtrl.h"

#include "MediaConstants.h"
#include <camera/CameraParameters.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/CameraSource.h>
#include <media/stagefright/TimeSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <sys/endian.h>
#include <media/MediaProfiles.h>
#include <utils/Errors.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include "utils/Log.h"
#include "utility.h"
#include "surface-hack.h"

#include "ARTPStreamer.h"

using namespace android;

/* Don't change the values without changing it in ARTPStreamer.cpp */
#define PT_VIDEO      97
#define PT_VIDEO_STR  "97"

#define PT_AUDIO      96
#define PT_AUDIO_STR  "96"

struct PlayerEvent : public DSPGTimedEventQueue::Event {
    PlayerEvent(
            MediaPlayerCtrl *player,
            void (MediaPlayerCtrl::*method)())
        : mPlayer(player),
          mMethod(method) {
    }

protected:
    virtual ~PlayerEvent() {}

    virtual void fire(DSPGTimedEventQueue *queue, int64_t /* now_us */) {
        (mPlayer->*mMethod)();
    }

private:
    MediaPlayerCtrl *mPlayer;
    void (MediaPlayerCtrl::*mMethod)();

    PlayerEvent(const PlayerEvent &);
    PlayerEvent &operator=(const PlayerEvent &);
};


MediaPlayerCtrl::MediaPlayerCtrl():mQueueStarted(false), mIsStopReqPending(false) {
}

MediaPlayerCtrl:: ~MediaPlayerCtrl(){
    if (mQueueStarted) {
        mQueue.stop();
    }
}

void MediaPlayerCtrl::onPrepareEvent(){
    F_LOG;
    status_t retVal = m_mediaPlayer->play();
    LOGV("m_mediaPlayer->start: %d", retVal);
    //TBD: Need to check what to do incase start fails
    if (retVal != 0)
        LOGE("OOOpppss failed to start DSPG player !!!");
}

void MediaPlayerCtrl::PlayerCb (int msg, int ext1, int ext2, void * cbData){
    LOGV("PlayerCb msg:%d ext1:%d ext2:%d", msg, ext1, ext2);
    
    MediaPlayerCtrl *playerInstance = (MediaPlayerCtrl *)cbData;
    switch (msg ){
        case MEDIA_PREPARED:{
            LOGD("Received MEDIA_PREPARED..");
            playerInstance->mIsPrepareInProgress = false;
            if (playerInstance->mIsStopReqPending == true) {
                playerInstance->mPrepareCheck.signal();
                playerInstance->mIsStopReqPending = false ;
            } else {
                playerInstance->mQueue.postEvent(playerInstance->mPrepareEvent);
            }
        }
        break;
        case MEDIA_PLAYBACK_COMPLETE:{
            LOGV("Plaback completed...");
        } break;
        case MEDIA_ERROR:{
            LOGV("Oooopppss MEDIA_ERROR from DSPGMediaPlayer !!!!!!");
        } break;        
        default:
            LOGV("Unhandled mediaplyer event..");
    }
    return ;
}
void MediaPlayerCtrl::getDecoderInfo(int *decoderInfo) {
	if(m_mediaPlayer != NULL){
		m_mediaPlayer->getDecoderInfo(decoderInfo);
	}
	return;
}

status_t MediaPlayerCtrl::prepare(const android::sp<android::MetaData> &metaData, int aRTPSocket, int aRTCPSocket, 
                                  int vRTPSocket, int vRTCPSocket){
    status_t    retVal = OK ;
    int enableAudio, enableVideo, enableAudioViaCss;
    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);

	m_mediaPlayer = new DSPGMediaPlayer();	
    if (m_mediaPlayer != NULL) {
		if (enableAudio && !enableAudioViaCss)
		{
			if ((m_audioOutput = new DSPGAudioOutput()) != NULL)
			{
			
				LOGV("Settings data source");
				/* Setting audio output on earpiece */
				m_audioOutput->setAudioStreamType(AudioSystem::VOICE_CALL);
				m_mediaPlayer->setAudioSink(m_audioOutput);
			}        	
			else {
			        LOGV("Failed to create AudioOutput instance !!!");
			        retVal = NO_MEMORY;
			}
		}

        Surface *remoteSurface;

        m_mediaPlayer->setListener (PlayerCb, (void *)this);

        m_mediaPlayer->setDataSource(m_psdpInfo->c_str() /*"rtsp://dspg/test"*/, NULL);
    	metaData->findPointer(kKeyP2PRemoteSurface, (void **) &remoteSurface);
        LOGV("Calling setVideoSurface..");
        {
            sp<ISurface> iSurface;
            Test::getISurface (remoteSurface, iSurface);
	        m_mediaPlayer->setISurface (iSurface);
	    }

        retVal = m_mediaPlayer->prepareAsync(aRTPSocket, aRTCPSocket, vRTPSocket, vRTCPSocket);
        if (retVal == OK)
            mIsPrepareInProgress = true;
            
        LOGV("preparing awesome player retVal:%d..", retVal);
        
        retVal = m_mediaPlayer->setVideoLagThreshold(100000);
        retVal = m_mediaPlayer->setVideoAheadThreshold(100000);
        //retVal = m_mediaPlayer->start();
        LOGV("start retVal:%d..", retVal);
    } 
	else {
        LOGV("Failed to create player instance !!!");
        retVal = NO_MEMORY;
    }
    
    return retVal ;
}

status_t MediaPlayerCtrl::init (){
    if (!mQueueStarted) {
        mQueue.start();
        mQueueStarted = true;
    }
    
    if (mPrepareEvent.get() == NULL){
        mPrepareEvent = new PlayerEvent(this, &MediaPlayerCtrl::onPrepareEvent);
    }
    
    return OK;
}

status_t MediaPlayerCtrl::play(const android::sp<android::MetaData> &metaData, int aRTPSocket, int aRTCPSocket, 
                               int vRTPSocket, int vRTCPSocket){
	F_LOG;

    status_t    retVal = OK ;
    int         enableAudio, enableVideo;
    char        *videomimeType ;

	metaData->findCString(kKeyMIMEType, (const char**)&videomimeType); //should be MEDIA_MIME_TYPE_VIDEO_AVC
    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);

    LOGV("enableAudio:%d enableVideo:%d videomimeType:%s", enableAudio, enableVideo, videomimeType);
    if (enableAudio == false && enableVideo == false) {
        LOGE("Both video & audio playback is not enabled !!!");
        retVal = UNKNOWN_ERROR;
    } else {
    	/* For H264, need to wait till we get SDP info from encoder */
        if (enableVideo == true && strcmp(videomimeType, MEDIA_MIMETYPE_VIDEO_AVC) == 0) { //H264
            while (m_psdpInfo == NULL) {
                mCondition.wait(mLock);
            }
        } else {
            m_psdpInfo = new AString ();
            if (m_psdpInfo == NULL){
                LOGE("Failed to instantiate AString");
                return NO_MEMORY;
            }
        }

        retVal = createSDP (*m_psdpInfo, metaData);
        if (retVal == OK) {
            retVal = prepare(metaData, aRTPSocket, aRTCPSocket, vRTPSocket, vRTCPSocket);
        }
    }
    
    return retVal;
}

status_t MediaPlayerCtrl::stop(){
	F_LOG;
	if (m_mediaPlayer.get() != NULL) {
	
	    /* If player is in between prepare process, wait till prepare is complete */
        while (mIsPrepareInProgress == true) {
            mIsStopReqPending = true;
            mPrepareCheck.wait(mLock);
        }
        
	    cancelPlayerEvents();
	    LOGV("Calling forceStop");
    	m_mediaPlayer->forceStop();
	    m_mediaPlayer->setListener(0, NULL);
	    LOGV("Calling m_mediaPlayer->stop");
    	m_mediaPlayer->pause();
        {
            sp<DSPGMediaPlayer> p = m_mediaPlayer;
    	    m_mediaPlayer.clear();
    	    p->reset();
	    }
	    LOGV("Awesome after Calling m_mediaPlayer->disconnect");
	    m_mediaPlayer = NULL ;
	    mIsStopReqPending = false;
	}
	m_audioOutput.clear();
	m_audioOutput = NULL;
	return OK;
}

void MediaPlayerCtrl::cancelPlayerEvents() {
    mQueue.cancelEvent(mPrepareEvent->eventID());
    return ;
}

status_t MediaPlayerCtrl::createSDP(AString &sdp, const sp<MetaData> &metaData) {
    int remotePort, width, height, audiosamplerate, channelCnt ;
    char *premoteIp, *plocalIp, *videomimeType ;

	metaData->findInt32(kKeyWidth, &width);
	metaData->findInt32(kKeyHeight, &height);
	metaData->findInt32(kKeyP2PRemotePort, &remotePort);
	metaData->findCString(kKeyP2PRemoteIP, (const char**)&premoteIp);
	metaData->findCString(kKeyP2PLocalIP, (const char**)&plocalIp);
 
	metaData->findCString(kKeyMIMEType, (const char**)&videomimeType); //should be MEDIA_MIME_TYPE_VIDEO_AVC
	metaData->findInt32(kKeyP2PAudioSampleRate, &audiosamplerate);
	audiosamplerate = 8000; //TBD yet to set this value in NativeCodeCaller.cpp
	metaData->findInt32(kKeyP2PAudioChannelCnt, &channelCnt);
	channelCnt = 1; //TBD yet to set this value in NativeCodeCaller.cpp

    int enableAudio, enableVideo, enableAudioViaCss;
    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);

    LOGV("Playback Audio enabled :%d; ViaCss :%d", enableAudio, enableAudioViaCss);
    LOGV("Playback Video enabled :%d", enableVideo);
    
    sdp = "sdp://dspg/test\r\n";
    sdp.append ("v=0\r\n");
    sdp.append("o=- ");

    uint64_t ntp = ARTPStreamer::GetNowNTP();
    sdp.append(ntp);
    sdp.append(" ");
    sdp.append(ntp);
    sdp.append(" IN IP4 ");
    sdp.append(premoteIp);
    sdp.append("\r\n");
//    sdp.append("\r\n");
    sdp.append(
          "s=Sample\r\n"
          "i=Playing around\r\n"
          "c=IN IP4 ");
    sdp.append(plocalIp);
    sdp.append(
          "\r\n"
          "t=0 0\r\n"
          "a=range:npt=now-\r\n");

    if(enableVideo) {
        //Video SDP
        sdp.append("m=video ");
        sdp.append(StringPrintf("%d", remotePort));
        sdp.append(
              " RTP/AVP " PT_VIDEO_STR "\r\n"
              "b=AS 320000\r\n"
              "a=rtpmap:" PT_VIDEO_STR " ");

        LOGV("video mime type: %s width:%d height=%d", videomimeType, width, height);
        if (strcmp(videomimeType, MEDIA_MIMETYPE_VIDEO_AVC) == 0) { //H264
            sdp.append("H264/90000");
            sdp.append("\r\n");
            sdp.append("a=fmtp:"PT_VIDEO_STR);

            //Profile level ID                
            sdp.append(" profile-level-id=");    //TBD: Need to see if this profile ID holds good for all video mimetype
            sdp.append(mProfileLevel.c_str());
            sdp.append(";");

            //sequence param set
            sdp.append("sprop-parameter-sets=");
            sdp.append(mSeqParamSet.c_str());
            sdp.append(",");

            //pcture param set
            sdp.append(mPicParamSet.c_str());
            sdp.append(";");

            sdp.append("packetization-mode=1\r\n");
        } else if (videomimeType, MEDIA_MIMETYPE_VIDEO_H263) {
            sdp.append("H263-1998/90000");
        } else if (videomimeType, MEDIA_MIMETYPE_VIDEO_MPEG4) { //TBD: need to check what to append
            //sdp.append("H263-1998/90000");
        } else {
            TRESPASS(); //should not be here
        }
    }

    if(enableAudio & !enableAudioViaCss) {
        /* Now starts audio SDP */
        sdp.append("m=audio ");
        sdp.append(StringPrintf("%d", remotePort+2)); //audio_port=video_port+2
        sdp.append(
              " RTP/AVP " PT_AUDIO_STR "\r\n"
              "b=AS:30\r\n"
              "a=rtpmap:" PT_AUDIO_STR " ");

        sdp.append("AMR");
        //TBD:    sdp.append(mMode == AMR_NB ? "AMR" : "AMR-WB"); Need to us this when we support different audio codecs
        sdp.append(StringPrintf("/%d/%d", audiosamplerate, channelCnt));
        sdp.append("\r\n");
        sdp.append("a=fmtp:" PT_AUDIO_STR " octet-align\r\n");
    }

    /* TBD: As of now we are not including video height & width details inside SDP */
#if 0
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
#endif

    LOGI("%s", sdp.c_str());
    return OK;
}

void MediaPlayerCtrl::encoderSdpHandler (const char *pseqParam, const char *ppicParam, const char *pprofileId) {
    F_LOG;

    mProfileLevel = pprofileId;
    mSeqParamSet = pseqParam;
    mPicParamSet = ppicParam;
    m_psdpInfo = new AString ();
    if (m_psdpInfo == NULL){
        LOGE("Failed to instantiate m_psdpInfo !!!");
    }
    
    mCondition.signal();
}


