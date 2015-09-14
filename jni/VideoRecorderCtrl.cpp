
#define LOG_NDEBUG 0
#define LOG_TAG "VideoRecorderCtrl"


#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>

#include "utils/Log.h"
#include "utility.h"

#include "MediaConstants.h"
#include "VideoRecorderCtrl.h"

#include "OMX_Image.h"

using namespace android ;


template <typename T>
void INIT_PARAM(T &p, OMX_U32 portIndex) {
	memset(&p, 0, sizeof(T));
	p.nSize = sizeof(T);
	p.nVersion.s.nVersionMajor = 1;
	p.nPortIndex = portIndex;
}

VideoRecorderCtrl::VideoRecorderCtrl():m_pRTPWriter(NULL) {
    F_LOG;
}

VideoRecorderCtrl::~VideoRecorderCtrl() {
    F_LOG;
}

status_t VideoRecorderCtrl::prepare(const sp<MetaData> &videoMetaData, void *cbData, int remote_port, 
                                    int vRTPSocket, int vRTCPSocket, psdpcb sdpcb){
	F_LOG;
    
    const char *remoteIP ;
    Surface *previewSurface;
    status_t retval ;
    int64_t tmp ;
    
	videoMetaData->findCString(kKeyP2PRemoteIP, (const char**)&remoteIP);
	videoMetaData->findPointer(kKeyP2PPreviewSurface, (void **) &previewSurface);

	CHECK_EQ(m_omxClient.connect(), OK);
	m_IOMX = m_omxClient.interface();
	retval = m_cameraCtrl.setupCamera(videoMetaData, previewSurface);
    if (retval == OK) {
        retval = m_encoderCtrl.setupEncoder(m_IOMX, m_cameraCtrl.GetCameraSource(), videoMetaData);
        if (retval == OK) {
            m_pRTPWriter = new ARTPStreamer(remote_port, vRTPSocket, vRTCPSocket, remoteIP, cbData, sdpcb);
            if (m_pRTPWriter != NULL) {
                retval = m_pRTPWriter->addSource(m_encoderCtrl.GetEncoderMediaSource().get());
                if ( retval != OK) {
                    LOGE("RTPWriter->addSource failed !!!");
                }
            }else {
                LOGE("Unable to allocate ARTPWriter !!!");
                retval = NO_MEMORY;
            }
        }
        else {
            LOGE("m_encoderCtrl.setupEncoder failed !!!");
        }
    } else {
        LOGE("cameraCtrl.setupCamera failed !!!");
    }
    
    if (retval != OK)
        stop();

    return retval;
}

status_t VideoRecorderCtrl::start() {    
    F_LOG;
    status_t retval ;
        
    LOGE("Calling RTPWriter->start");
    retval = m_pRTPWriter->start(NULL);
    LOGE("RTPWriter->start err:%d", retval);
    return retval;
}

status_t VideoRecorderCtrl::stop() {
    F_LOG;
    
    if (m_pRTPWriter != NULL) {
        status_t err = m_pRTPWriter->stop();
        LOGI("m_pRTPWriter->stop err:%d", err);
        m_pRTPWriter.clear();
        m_pRTPWriter = NULL;
    }
	m_cameraCtrl.stopCamera();
	m_omxClient.disconnect();
    return OK;
}

status_t VideoRecorderCtrl::setFramerate(const android::sp<android::MetaData> &videoMetaData) {
    status_t    retval = OK ;
    int         fps ;
    
    F_LOG;
	videoMetaData->findInt32(kKeySampleRate, &fps) ;
    LOGV("fps:%d", fps);
    retval = m_cameraCtrl.setFramerate (fps);
    if (retval == OK) {        
	    retval = m_encoderCtrl.setFramerate (fps);
	} else {
	    LOGE("Failed to set camera frame rate !!!!!");
	}
	
	return retval ;
}

status_t VideoRecorderCtrl::setBitrate(const sp<MetaData> &videoMetaData, int bitrate) {
	LOGV("SetBitrate: bps: %d", bitrate);
	return m_encoderCtrl.setBitrate(videoMetaData, bitrate);
}

status_t VideoRecorderCtrl::configureQuality(const android::sp<android::MetaData> &videoMetaData){
    F_LOG;
    status_t retval;

    retval = m_encoderCtrl.setBitrate(videoMetaData);
    if (retval != OK)
        LOGV("Unable to configure encoder bitrate");

    retval = m_encoderCtrl.setAVCIntraPeriod(videoMetaData);
    if (retval != OK)
        LOGV("Unable to configure AVCIntraPeriod");
    return retval;
}

status_t VideoRecorderCtrl::requestIFrame (void) {
    F_LOG;
    return m_encoderCtrl.requestIFrame();
}

void VideoRecorderCtrl::getEncoderInfo(int *encoderInfo)
{
	F_LOG;
	int32_t encoderFrameRate = -1;
	int32_t encoderBitRate = -1;
	int32_t encoderIFrameInterval = -1;
	int32_t encoderPFrameCounter = -1;
	if(m_pRTPWriter != NULL)
	{
		encoderFrameRate = m_pRTPWriter->getFrameCounter();
		encoderBitRate = m_pRTPWriter->getEncoderBitRate();
		encoderIFrameInterval =  m_pRTPWriter->getIFrameInterval();
		encoderPFrameCounter = m_pRTPWriter->getPFrameCounter();
	}
	m_encoderCtrl.getEncoderInfo(encoderInfo, encoderFrameRate,encoderBitRate,encoderIFrameInterval,encoderPFrameCounter);
	return;
}
