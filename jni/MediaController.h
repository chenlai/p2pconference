
#ifndef MEDIACONTROLLER_H_
#define MEDIACONTROLLER_H_

#include <surfaceflinger/Surface.h>
#include <media/mediaplayer.h>
#include <media/stagefright/foundation/AString.h>
#include "Transport.h"
#include "VideoRecorderCtrl.h"
#include "AudioRecorderCtrl.h"
#include "MediaPlayerCtrl.h"


#define VIDEO_SEND ("data/data/com.p2p.app/send.dat")
#define VIDEO_RECEIVE ("data/data/com.p2p.app/receive.dat")

class PlayerListener ;

class MediaController {
public:

	MediaController();
	virtual ~MediaController();

	android::status_t Init();
	android::status_t createSockets(const android::sp<android::MetaData> &metaData);
	android::status_t closeSockets();
	android::status_t StartPlayback(const android::sp<android::MetaData> &metaData);
	android::status_t StopPlayback();

	android::status_t StartRecording(const android::sp<android::MetaData> &metaData);
	android::status_t StopRecording();

	android::status_t StartCssAudio(const android::sp<android::MetaData> &metaData);
	android::status_t StopCssAudio();
	android::status_t StartCssRtpStream(int chan, char* remoteIP, int remotePort, int localPort, int codec, int duration, int codecOnCss);
	android::status_t StopCssRtpStream(int chan);
	

    /* API's to configure video params at run time */	
	android::status_t configureFramerate(const android::sp<android::MetaData> &metaData);
	android::status_t configureBitrate(const android::sp<android::MetaData> &videoMetaData, int bitrate=0);
	android::status_t configureQuality(const android::sp<android::MetaData> &videoMetaData);
	android::status_t requestIFrame (void);
	android::status_t configureMaxPacketSize(int bytes);

	android::status_t toggleVideoDisplay(const android::sp<android::Surface> &previewSurface, 
				const android::sp<android::Surface> &remoteviewSurface);
				
	/* Media player call back */
	friend class PlayerListener;

    /* SDP callback */	
    static void encoderSdpCb (const char *pseqParam, const char *ppicParam, const char *pprofileId, void *cbData);
    void encoderSdpHandler (const char *pseqParam, const char *ppicParam, const char *pprofileId);
    void getEncoderInfo(int *encoderInfo);
    #ifndef USE_AWESOME_PLAYER
    void getDecoderInfo(int *decoderInfo);
    #endif

private:
    VideoRecorderCtrl   *m_pvideoRecorder ;
    AudioRecorderCtrl   *m_pAudioRecorder ;
    
    int mvRTPSocketId;
    int mvRTCPSocketId;
    int maRTPSocketId;
    int maRTCPSocketId;
    

#ifndef USE_AWESOME_PLAYER
    MediaPlayerCtrl     *m_pMediaPlayer ;
#else    
    android::sp<android::MediaPlayer>   m_mediaPlayer ;    
    /* H264 SDP information */
    android::AString mProfileLevel;
    android::AString mSeqParamSet;
    android::AString mPicParamSet;
    android::Mutex                      mLock;
    android::Condition                  mCondition;
    android::AString                    *m_psdpInfo ;
#endif //USE_AWESOME_PLAYER

    int mChAudioNode;

    android::status_t createSDP(android::AString &sdp, const android::sp<android::MetaData> &metaData);
    android::status_t initPlayBack (const android::sp<android::MetaData> &metaData);
    static int CreateUDPSocket (int local_port);   
};


#endif /* MEDIACONTROLLER_H_ */
