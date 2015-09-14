#ifndef VIDEO_RECORD_CTRL_H_
#define VIDEO_RECORD_CTRL_H_

/**
 * Class to wrap encoder controls not available in stock SF
 * OMX direct handle calls are in here
 */

#include <media/stagefright/MediaSource.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaExtractor.h>

#include <surfaceflinger/ISurface.h>

#include <media/stagefright/CameraSource.h>
#include <media/MediaProfiles.h>
#include <ARTPWriter.h>

#include <VideoEncoderController.h>
#include <CameraController.h>
#include <surfaceflinger/Surface.h>
#include "ARTPStreamer.h"


class VideoRecorderCtrl {
public:

	VideoRecorderCtrl();
	~VideoRecorderCtrl();

    android::status_t prepare(const android::sp<android::MetaData> &videoMetaData, void *cbData, 
                                    int remote_port, int vRTPSocket, int vRTCPSocket, psdpcb sdpcb);
    android::status_t start();
    android::status_t stop();

	android::status_t setFramerate(const android::sp<android::MetaData> &videoMetaData);
	android::status_t setBitrate(const android::sp<android::MetaData> &videoMetaData, int bitrate=0);
	android::status_t configureQuality(const android::sp<android::MetaData> &videoMetaData);
    android::status_t requestIFrame (void);
    void getEncoderInfo(int *encoderInfo);

private:
    int32_t m_displyWidth, m_displayHeight, m_encodedWidth, m_encodedHeight;
    int32_t mFrameRate;
    int32_t mVideoBitRate;
    
    /* Stagefright instances */
	android::OMXClient                  m_omxClient;
	android::sp<android::IOMX>          m_IOMX;
    android::sp<ARTPStreamer>           m_pRTPWriter ;
    
	/* P2P instances */
    //android::sp<Transport>  m_VideoDatahandler;
    VideoEncoderController  m_encoderCtrl;
    CameraController        m_cameraCtrl;
};

#endif //VIDEO_RECORD_CTRL_H_
