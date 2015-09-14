#define LOG_NDEBUG 0
#define LOG_TAG "MediaController"

#include "debugging.hpp"
#undef LOG // Collides with Android

#include "MediaController.h"

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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <stdtypes.h>
#include <ch.h>
#include <rtp.h>
#include <rtp-query-interface.h>

using namespace android;

int gCssAudioInited = 0;
void * uchAppCb(IN s32 EP, IN s32 eventType, IN char* eventData, IN s32 eventDataSize)
{
    return NULL;
}

void ApplCBRTPData(int channel_id, int msgType, void *pRTCPResponse)
{
//	LOGD("ApplCBRTPData: Got callback for channel %d\n", channel_id);

	switch(msgType)
	{
		case RTP_QUERY:
		{
			PRTCP_QUERY_RESPONSE pResp = (PRTCP_QUERY_RESPONSE) pRTCPResponse;
			LOGD("ApplCBRTPDataGot DTMF event %d, vol %d, duration %d\n", pResp->pRtpEvent->bEvent,
				pResp->pRtpEvent->bVolume, ntohs(pResp->pRtpEvent->wDuration));
			break;
		}

		default:
//			LOGD("ApplCBRTPData: Got message type %d\n", msgType);
			break;
	}
}

#ifndef USE_AWESOME_PLAYER
MediaController::MediaController() :
    m_pvideoRecorder(NULL), m_pAudioRecorder(NULL), 
    m_pMediaPlayer(NULL), mvRTPSocketId(0xFF), 
    mvRTCPSocketId(0xFF), maRTPSocketId(0xFF),
    maRTCPSocketId(0xFF),mChAudioNode(0)
{
	F_LOG;
}

MediaController::~MediaController() {
	F_LOG;
    if (mvRTPSocketId != 0xFF)
        close (mvRTPSocketId);
    if (mvRTCPSocketId != 0xFF)
        close (mvRTCPSocketId);
    if (maRTPSocketId != 0xFF)
        close (maRTPSocketId);
    if (maRTCPSocketId != 0xFF)
        close (maRTCPSocketId);
}
#endif //USE_AWESOME_PLAYER

int MediaController::CreateUDPSocket (int local_port) {
    int socketId;
    if ((socketId=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
        return -1 ;
    }

    /* Do not bind to port if we are using android AwesomePlayer, since it will be done by AwesomePlayer */
#ifndef USE_AWESOME_PLAYER
    struct sockaddr_in sockaddr_in;
    sockaddr_in.sin_family = AF_INET;
    sockaddr_in.sin_port = htons(local_port);
    sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socketId, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in))==-1) {
        return -1 ;
    }
#endif
    return socketId ;
}

status_t MediaController::createSockets(const sp<MetaData> &metaData)
{
    int         localPort;

    metaData->findInt32(kKeyP2PLocalPort, &localPort);

    //TBD: port needs to be handled after adding UI
    mvRTPSocketId = CreateUDPSocket (localPort);
    mvRTCPSocketId = CreateUDPSocket (localPort+1);
    maRTPSocketId = CreateUDPSocket (localPort+2);
    maRTCPSocketId = CreateUDPSocket (localPort+3);

	return OK;
}

status_t MediaController::closeSockets()
{
    if (mvRTPSocketId != 0xFF)
        close (mvRTPSocketId);
    if (mvRTCPSocketId != 0xFF)
        close (mvRTCPSocketId);
    if (maRTPSocketId != 0xFF)
        close (maRTPSocketId);
    if (maRTCPSocketId != 0xFF)
        close (maRTCPSocketId);

	return OK;
}

status_t MediaController::Init() {
	F_LOG;
	status_t    retval ;

    m_pAudioRecorder = new AudioRecorderCtrl();
	if (m_pAudioRecorder != NULL) {
	    m_pAudioRecorder->setDefaults();
	    retval = OK ;
	} else {
	    LOGV("Failed to instantiate AudioRecorderCtrl !!!");
		retval = NO_MEMORY ;
	}

    m_pvideoRecorder = new VideoRecorderCtrl();
	if (m_pvideoRecorder == NULL) {
		LOGE ("Unable to create VideoRecorder instance !!!!");
		retval = NO_MEMORY;
	} else {
	    retval = OK ;
	}

#ifndef USE_AWESOME_PLAYER
	m_pMediaPlayer = new MediaPlayerCtrl();
	if (m_pMediaPlayer == NULL) {
		LOGE ("Unable to create MediaPlayerCtrl instance !!!!");
		retval = NO_MEMORY;
	} else {
	    retval = m_pMediaPlayer->init() ;
	    if (retval != OK)
	        LOGE("OOOpps unable to intialise the player !!!");
	}
#endif

	// Initialize the libch and librtp APIs
	if (gCssAudioInited != 1)
	{
		if ((retval = ch_Init((ch_callback)uchAppCb)) != 0)
		{
			LOGE ("Unable to Initialize libch Interface: ret = %d!!!!", retval);
			return -1;	
		};
		LOGV("CH INITIALIZED !!!!!!!!!!!!!!!!!!!!!!!!!!");

	    /* Open named pipe for recv, send and ctrl interface with the msgswitchd */
		if((retval=rtp_init())!=0){
			LOGE ("Unable to Initialize librtp Interface: ret = %d!!!!", retval);
			return -1;	
		}

		if (rtp_register((rtp_callback)ApplCBRTPData) < 0) {
			LOGD("RTP Registration failed!\n");
			return -1;
		}
		gCssAudioInited = 1;
	}
	
	return retval;
}

void MediaController::encoderSdpCb (const char *pseqParam, const char *ppicParam, const char *pprofileId, void *cbData) {
    F_LOG;
    MediaController *mediaCtrl = (MediaController *)cbData;
#ifdef USE_AWESOME_PLAYER
    mediaCtrl->encoderSdpHandler(pseqParam, ppicParam, pprofileId);
#else    
    mediaCtrl->m_pMediaPlayer->encoderSdpHandler(pseqParam, ppicParam, pprofileId);
#endif //USE_AWESOME_PLAYER
}

#ifndef USE_AWESOME_PLAYER
status_t MediaController::StartPlayback(const sp<MetaData> &metaData) {
	F_LOG;

    status_t    retVal = OK ;

    /*Its assumed that m_pMediaPlayer can not be NULL @ this point */
    retVal = m_pMediaPlayer->play (metaData, maRTPSocketId, maRTCPSocketId, 
                               mvRTPSocketId, mvRTCPSocketId);
    if (retVal != OK){
        LOGE("OOOpps failed to start playback !!!");
    }
    return retVal;
}

status_t MediaController::StopPlayback() {
	F_LOG;
	m_pMediaPlayer->stop();
//    delete m_pMediaPlayer;
//	m_pMediaPlayer = NULL;
	return OK;
}
#endif

status_t MediaController::StartRecording(const sp<MetaData> &metaData) {
	F_LOG;

	status_t retStatus = OK;

    int enableAudio, enableAudioViaCss, enableVideo, remotePort;

    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);
    metaData->findInt32(kKeyP2PRemotePort, &remotePort);

    LOGV("Recording Audio enabled :%d; ViaCss : %d", enableAudio, enableAudioViaCss);
    LOGV("Recording Video enabled :%d", enableVideo);

    /* This hack is to enable local loopback using android AwesomePlayer */
#ifdef AWESOME_LOCAL_LOOPBACK
    metaData->findInt32(kKeyP2PLocalPort, &remotePort);
#endif

    if(enableVideo) {
        LOGV("Calling prepare");
        retStatus = m_pvideoRecorder->prepare(metaData, (void *)this, remotePort, 
                                     mvRTPSocketId, mvRTCPSocketId, encoderSdpCb);
        if (retStatus == OK) {
        LOGV("Calling start");
            retStatus = m_pvideoRecorder->start();
        }
	    LOGV("VideoRecord->start status:%d", retStatus);
	}

    if(enableAudio && !enableAudioViaCss) {
        retStatus = m_pAudioRecorder->start(metaData, remotePort+2 /* ie., video_port+2 */, maRTPSocketId, maRTCPSocketId);
        LOGV("AudioRecord->start status:%d", retStatus);
    }
	return retStatus ;
}

status_t MediaController::StopRecording() {
	F_LOG;
	
	if (m_pvideoRecorder != NULL) {
		m_pvideoRecorder->stop();
//		delete m_pvideoRecorder;
//		m_pvideoRecorder = NULL ;
	}

	if (m_pAudioRecorder != NULL) {
    	m_pAudioRecorder->stop();
//		delete m_pAudioRecorder ;
//		m_pAudioRecorder = NULL ;
	}
	return OK;
}

status_t MediaController::StartCssAudio(const sp<MetaData> &metaData)
{
	int remotePort, codec, duration, ret = OK, localPort;
	char *premoteIp, *plocalIp, *videomimeType ;
	char *audioCodecsmimeType;

	metaData->findInt32(kKeyP2PRemotePort, &remotePort);
	metaData->findCString(kKeyP2PRemoteIP, (const char**)&premoteIp);
	metaData->findCString(kKeyP2PLocalIP, (const char**)&plocalIp);
	metaData->findCString(kKeyP2PAudioCodecs, (const char**)&audioCodecsmimeType);

	LOGE("Audio Codecs %s",audioCodecsmimeType);
	if(strcmp(audioCodecsmimeType,"audio/pcmu") == 0) {
		codec = 0;
	}
	else if(strcmp(audioCodecsmimeType,"audio/pcma") == 0) {
		codec = 8;
	}
	else if(strcmp(audioCodecsmimeType,"audio/g722") == 0) {
		codec = 9;
	}
	else if(strcmp(audioCodecsmimeType,"audio/g729") == 0) {
		codec = 18;
	}
	else {
		return -1003;
	}
	// For now, codec is hardcoded to G711U, and duration to 20ms
	LOGE("Audio Code Number : %d",codec);
	duration = 20;
	remotePort += 2; // configured remoteport is for Video, for Audio, we do +2
	localPort = remotePort ; // For now RemotePort and LocalPort are the same
	
	// Just for safety, release the node 0, if the app had crashed earlier
	if ((ret = ch_ReleaseNodeID(mChAudioNode)) != 0)
	{
		LOGD("could not disable  Node %d%d, ret = %d\n", mChAudioNode, ret);
		//return -1;
	}	
	if ((ret=ch_GetNodeId(&mChAudioNode)) != 0)
	{
		LOGD("could not get CH node, ret = %d\n", ret);
		return -1;
	}
	LOGD("got node id %d\n", mChAudioNode);

	// Assuming that no VoIP channels are occupied, connecting 1st VoIP Channel (EP 0) to the node
	if ((ret=ch_ConnectEPReq(0, mChAudioNode)) != 0)
	{
		LOGD("could not connect EP %d to Node %d, ret = %d\n", 0, mChAudioNode, ret);
		return -1;
	}

	// Assuming that SPK EP offset if 4 connecting SPK EP to the node
	if ((ret = ch_ConnectEPReq(4, mChAudioNode))!= 0)
	{
		LOGD("could not connect EP %d to Node %d, ret = %d\n", 4, mChAudioNode, ret);
		return -1;
	}
	
	if ((ret = ch_NodeEnable(mChAudioNode,2)) != 0)
	{
		LOGD("could not enable Node %d, ret = %d\n", mChAudioNode, ret);
		return -1;
	}
	// Now connection is done, start the RTP streams
	if ((ret = StartCssRtpStream(0, premoteIp, remotePort, localPort, codec, duration, 0)) < 0)
	{
		LOGD("could not start_stream for chan1 %d\n", 0);
		ch_ReleaseNodeID(mChAudioNode);
		return -1;
	}

	// Now as a last step, switch the TDM control to the CSS via ALSA
	AudioSystem::setPhoneState(AudioSystem::MODE_IN_CALL);
	AudioSystem::setForceUse(AudioSystem::FOR_COMMUNICATION, AudioSystem::FORCE_SPEAKER);
	return ret;
}

status_t MediaController::StopCssAudio() {
	F_LOG;
	int ret;

	// Assume always that the VoIP channel used is 0, since we always start 0
	StopCssRtpStream(0);
	if ((ret = ch_ReleaseNodeID(mChAudioNode)) != 0)
	{
		LOGD("could not enable Node %d%d, ret = %d\n", mChAudioNode, ret);
		return -1;
	}
	
	AudioSystem::setPhoneState(AudioSystem::MODE_NORMAL);

	return OK;
}

status_t MediaController::StartCssRtpStream(int chan, char* remoteIP, int remotePort, int localPort,
				int codec, int duration, int codecOnCss)
{
	int ret;
	struct sockaddr_in sa_remote, sa_local;
	struct rtp_session_config rtp_config;

	if (chan < 0) { 
		LOGE ("chan not found (instance=0x%x)\n", chan); 
		return -1; 
	}

#if 0 // No need to create socket and bind now. Done in initialization
	/* create socket to use for sending rtp data */
	if ((mCssRtpSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		LOGE ("Failed to open CSS RTP socket\n");
		return -1;
	}

	/* bind socket to PORT */
	memset(&sa_local, 0, sizeof(sa_local));
	sa_local.sin_family = AF_INET;
	sa_local.sin_port = htons(localPort);
	sa_local.sin_addr.s_addr = htonl(INADDR_ANY);

	if ((ret = bind(mCssRtpSocket, (struct sockaddr *)&sa_local, sizeof(sa_local))) < 0)
	{
		LOGE ("Failed to bind port %d to socket %d\n", localPort, mCssRtpSocket);
		return -1;
	}
#endif
	memset(&sa_remote, 0, sizeof(sa_remote));
	sa_remote.sin_family = AF_INET;
	sa_remote.sin_port = htons(remotePort);
	inet_aton(remoteIP, &sa_remote.sin_addr);
	LOGD(" Remote IP %s and port %d \n",remoteIP,remotePort); 

	/* setup & start rtp session */
	memset(&rtp_config, 0, sizeof(rtp_config));

	// Set some default RTP options
	rtp_config.codec.opts = (RTP_SESSION_OPT_DTMF | RTP_SESSION_OPT_USE_JIB | RTP_CODEC_OPT_VAD);

	LOGD(" \nrtp_config.codec.opts =%x",rtp_config.codec.opts);

	if (codecOnCss)
	{
		rtp_config.codec.opts |= RTP_SESSION_OPT_PCMVOIP_SESSION;				
		rtp_config.opts |= RTP_SESSION_OPT_USERSPACE;
	}
		
	rtp_config.codec.duration = duration;
	rtp_config.codec.rx_list[0].rx_pt = codec; // RTP_SESSION_PT_G722;
	rtp_config.codec.rx_list[1].rx_pt = RTP_SESSION_PT_G722;
	rtp_config.codec.rx_list[2].rx_pt = RTP_SESSION_PT_G711U; 
	rtp_config.codec.rx_list[3].rx_pt = RTP_SESSION_PT_G711A; 
	rtp_config.codec.rx_list[3].rx_pt = RTP_SESSION_PT_G729; 
	rtp_config.codec.tx_pt = codec; // RTP_SESSION_PT_G722;
	
	rtp_config.jib_config.max_len = 150;
	rtp_config.jib_config.min_len = 60;	

	memset(rtp_config.key_loc,1,30);
	memset(rtp_config.key_dist,2,30);	

	LOGD(" Codec  %d and string %s \n",rtp_config.codec.rx_list[0].rx_pt,rtp_config.codec.CodecStr);
	if ((ret = rtp_session_start(chan, maRTPSocketId, &sa_remote, &rtp_config)) < 0)
	{
		LOGD("rtp_sessions_start() error = %d\n", ret);
		return -1;
	}

	LOGD(" ###### (started udp session %d)", chan);

	return chan;
}

status_t MediaController::StopCssRtpStream(int chan)
{
	struct rtp_stream *stream;
	int ret;

	ret = rtp_session_stop(chan);

#if 0 // Dont close here, done in higher function
	/* close socket */
	close(mCssRtpSocket);
#endif
	LOGD(" (stopped udp session %d)", chan);
	return ret;
	
}

status_t MediaController::configureFramerate(const android::sp<android::MetaData> &metaData) {
	F_LOG;
	return m_pvideoRecorder->setFramerate(metaData);
}

status_t MediaController::requestIFrame (void) {
	F_LOG;
	return m_pvideoRecorder->requestIFrame();
}

#ifndef USE_AWESOME_PLAYER
void MediaController::getDecoderInfo(int *decoderInfo) {
	F_LOG;
	m_pMediaPlayer->getDecoderInfo(decoderInfo);
}
#endif

void MediaController::getEncoderInfo(int *encoderInfo) {
	F_LOG;
	return m_pvideoRecorder->getEncoderInfo(encoderInfo);
}


status_t MediaController::configureBitrate(const sp<MetaData> &videoMetaData, int bitrate) {
	F_LOG;
	Log("SetBitrate: bps: %d", bitrate);
	return m_pvideoRecorder->setBitrate(videoMetaData, bitrate);
}

status_t MediaController::configureQuality(const android::sp<android::MetaData> &videoMetaData){
    F_LOG;
    return m_pvideoRecorder->configureQuality(videoMetaData);
}

status_t MediaController::configureMaxPacketSize(int bytes) {
	F_LOG;
#ifdef M_REDUNDANT_ENABLE
	if (!pdata->threadRecorder) {
		return false;
	}
	pdata->threadRecorder->SetMaxPacketSize(bytes);
#endif
	return OK;
}

status_t MediaController::toggleVideoDisplay(const android::sp<android::Surface> &previewSurface, 
			const android::sp<android::Surface> &remoteviewSurface) {
#ifdef M_REDUNDANT_ENABLE
	pdata->cameraContext.ModifyPreviewSurface(previewSurface);
	{
		sp<ISurface> tmpISurface;
		android::Test::getISurface(remoteviewSurface, tmpISurface);		
		pdata->player.setISurface(tmpISurface);
	}
#endif
//toggle Video Overlay between main and sub video layers
	sp<ISurface> tmpISurface;
	android::Test::getISurface(remoteviewSurface, tmpISurface);
	tmpISurface->toggleVideoOverlay();	
	return OK ;
}

#ifdef USE_AWESOME_PLAYER
/* Don't change the values without changing it in ARTPStreamer.cpp */
#define PT_VIDEO      97
#define PT_VIDEO_STR  "97"

#define PT_AUDIO      96
#define PT_AUDIO_STR  "96"


MediaController::MediaController() :
    m_pvideoRecorder(NULL), m_pAudioRecorder(NULL), 
    m_psdpInfo(NULL), mChAudioNode(0),
//    m_pMediaPlayer(NULL),
    m_mediaPlayer(NULL){
	F_LOG;
}

MediaController::~MediaController() {
	F_LOG;
	if (m_psdpInfo != NULL)
	    delete m_psdpInfo;
}

class PlayerListener: public MediaPlayerListener {

public:
    PlayerListener(const MediaController &mediaCtrl);    
    virtual ~PlayerListener();
    virtual void notify (int msg, int ext1, int ext2) ;
    
private:
    const MediaController &m_mediaCtrl ; 
} ;

PlayerListener::PlayerListener(const MediaController &mediaCtrl):m_mediaCtrl(mediaCtrl){
    F_LOG;
}

PlayerListener::~PlayerListener() {
    F_LOG;
}

void PlayerListener::notify (int msg, int ext1, int ext2) {
    LOGV("PlayerListener::notify msg:%d ext1:%d ext2:%d", msg, ext1, ext2);
    switch (msg ){
        case MEDIA_PREPARED:{
            LOGV("Starting the player...");
            status_t retVal = m_mediaCtrl.m_mediaPlayer->start();
            LOGV("m_mediaPlayer->start: %d", retVal);
        }
        break;
        case MEDIA_PLAYBACK_COMPLETE:{
            LOGV("Plaback completed...");
        } break;
        case MEDIA_ERROR:{
            LOGV("Oooopppss MEDIA_ERROR from AwesomePlayer !!!!!!");
        } break;        
        default:
            LOGV("Unhandled mediaplyer event..");
    }
    return ;
}

void MediaController::encoderSdpHandler (const char *pseqParam, const char *ppicParam, const char *pprofileId) {
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

status_t MediaController::initPlayBack (const sp<MetaData> &metaData){
    status_t    retVal = OK ;

    m_mediaPlayer = new MediaPlayer ();
    if (m_mediaPlayer != NULL) {
        Surface *remoteSurface;

        m_mediaPlayer->setListener (new PlayerListener(*this));
        LOGV("Settings data source");
        m_mediaPlayer->setDataSource(m_psdpInfo->c_str() /*"rtsp://dspg/test"*/, NULL);

    	metaData->findPointer(kKeyP2PRemoteSurface, (void **) &remoteSurface);
        LOGV("Calling setVideoSurface..");
		retVal = m_mediaPlayer->setVideoSurface (remoteSurface);
        LOGV("Not Calling setAudioStreamType retVal:%d..", retVal);
//        retVal = m_mediaPlayer->setAudioStreamType(AudioSystem::VOICE_CALL);
        LOGV("preparing awesome player retVal:%d..", retVal);
        retVal = m_mediaPlayer->prepareAsync();
        LOGV("starting awesome player retVal:%d..", retVal);
        retVal = m_mediaPlayer->setVideoLagThreshold(100000);
        retVal = m_mediaPlayer->setVideoAheadThreshold(100000);
        //retVal = m_mediaPlayer->start();
        LOGV("start retVal:%d..", retVal);
    } else {
        LOGV("Failed to create player instance !!!");
        retVal = NO_MEMORY;
    }
    
    return retVal ;
}

status_t MediaController::StartPlayback(const sp<MetaData> &metaData) {
	F_LOG;

    status_t    retVal = OK ;
    int         enableAudio, enableVideo;
    char        *videomimeType ;

	metaData->findCString(kKeyMIMEType, (const char**)&videomimeType); //should be MEDIA_MIME_TYPE_VIDEO_AVC
    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);

    LOGV("enableAudio:%d enableVideo:%d videomimeType:%s", enableAudio, enableVideo, videomimeType);
    if (enableAudio == false && enableVideo == false) {
        LOGE("Playback is not enabled !!!");
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
            retVal = initPlayBack(metaData);
        }
    }
    return retVal;
}

status_t MediaController::StopPlayback() {
	F_LOG;
	if (m_mediaPlayer.get() != NULL) {
	    LOGV("Calling forceStop");
    	m_mediaPlayer->forceStop();
	    LOGV("Awesome Calling m_mediaPlayer->stop");
    	m_mediaPlayer->stop();
	    LOGV("Awesome Calling m_mediaPlayer->setListener");
	    m_mediaPlayer->setListener(0);
	    LOGV("Awesome Calling m_mediaPlayer->disconnect");
	    m_mediaPlayer->disconnect();
	    LOGV("Awesome after Calling m_mediaPlayer->disconnect");
	    m_mediaPlayer = NULL ;
	}
	return OK;
}

#if 0
        static const char *raw =
            "v=0\r\n"
            "o=- 15129716133464481848 15129716133464481848 IN IP4 172.28.5.82\r\n"
            "s=Sample\r\n"
            "i=Playing around\r\n"
            "c=IN IP4 172.28.5.82\r\n"
            "t=0 0\r\n"
            "a=range:npt=now-\r\n"
            "m=video 5000 RTP/AVP 97\r\n"
            "b=AS 320000\r\n"
            "a=rtpmap:97 H264/90000\r\n"
            //"a=fmtp:97 profile-level-id=42E01E;sprop-parameter-sets=J0LgHo1oCgPaEAAAAwAQAAADAeDznqA=,KM4C/IA=;packetization-mode=1\r\n";
            "a=fmtp:97 profile-level-id=42E01E;sprop-parameter-sets=J0LgHo1oFB+hAAADAAEAAAMAHg856g==,KM4C/IA=;packetization-mode=1\r\n"
            "m=audio 5004 RTP/AVP 96\r\n"
            "b=AS:30\r\n"
            "a=rtpmap:96 AMR/8000/1\r\n"
            "a=fmtp:96 octet-align\r\n";
#endif

status_t MediaController::createSDP(AString &sdp, const sp<MetaData> &metaData) {
    int localPort, width, height, audiosamplerate, channelCnt ;
    char *premoteIp, *plocalIp, *videomimeType ;

	metaData->findInt32(kKeyWidth, &width);
	metaData->findInt32(kKeyHeight, &height);
	metaData->findInt32(kKeyP2PLocalPort, &localPort);
	metaData->findCString(kKeyP2PRemoteIP, (const char**)&premoteIp);
	metaData->findCString(kKeyP2PLocalIP, (const char**)&plocalIp);
 
	metaData->findCString(kKeyMIMEType, (const char**)&videomimeType); //should be MEDIA_MIME_TYPE_VIDEO_AVC
	metaData->findInt32(kKeyP2PAudioSampleRate, &audiosamplerate);
	audiosamplerate = 8000; //TBD yet to set this value in NativeCodeCaller.cpp
	metaData->findInt32(kKeyP2PAudioChannelCnt, &channelCnt);
	channelCnt = 1; //TBD yet to set this value in NativeCodeCaller.cpp

    int enableAudio, enableAudioViaCss, enableVideo;
    metaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
    metaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);
    metaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);

    LOGV("Playback Audio enabled :%d: ViaCSS: %d", enableAudio, enableAudioViaCss);
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
        sdp.append(StringPrintf("%d", localPort));
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

    if(enableAudio && !enableAudioViaCss) {
        /* Now starts audio SDP */
        sdp.append("m=audio ");
        sdp.append(StringPrintf("%d", localPort+2)); //audio_port=video_port+2
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
#endif //USE_AWESOME_PLAYER
