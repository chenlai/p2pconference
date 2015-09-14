/**
 * Copyright (C) 2011, Skype Limited
 *
 * All intellectual property rights, including but not limited to copyrights,
 * trademarks and patents, as well as know how and trade secrets contained in,
 * relating to, or arising from the internet telephony software of
 * Skype Limited (including its affiliates, "Skype"), including without
 * limitation this source code, Skype API and related material of such
 * software proprietary to Skype and/or its licensors ("IP Rights") are and
 * shall remain the exclusive property of Skype and/or its licensors.
 * The recipient hereby acknowledges and agrees that any unauthorized use of
 * the IP Rights is a violation of intellectual property laws.
 *
 * Skype reserves all rights and may take legal action against infringers of
 * IP Rights.
 *
 * The recipient agrees not to remove, obscure, make illegible or alter any
 * notices or indications of the IP Rights and/or Skype's rights and
 * ownership thereof.
 */

/**
 * Class to wrap encoder controls not available in stock SF
 * OMX direct handle calls are in here
 */

#ifndef VIDEO_ENCODER_CTRL_H_
#define VIDEO_ENCODER_CTRL_H_

#include <media/stagefright/CameraSource.h>
#include <media/MediaProfiles.h>

#if !defined(MODIFIED_METADATA)
namespace android {
enum {
	kKeyNodeId	= 'node',
};
} // namespace android
#endif



class VideoEncoderController {
    int32_t                 mVideoWidth, mVideoHeight;
    int32_t                 mFrameRate;
    int32_t 				mFrameinterval;
    android::MediaProfiles  *mEncoderProfiles;
    char                    *mVideoEncoder;
    
    int getBitrate(const android::sp<android::MetaData> &videoMetaData);
    int getIFrameInterval(const android::sp<android::MetaData> &videoMetaData);

public:
	android::sp<android::MediaSource> mEncoder;
	android::sp<android::IOMX> mIOMX;
	android::IOMX::node_id mNode;

	VideoEncoderController();
	android::status_t setupEncoder(android::sp<android::IOMX> & IOMX, android::sp<android::CameraSource> cameraSource, 
                    			const android::sp<android::MetaData> &videoMetaData);
	android::status_t stopEncoder();
    android::status_t initialiseAvcOutput (int initialBitrate);
    android::status_t requestIFrame (void) ;
	android::status_t setFramerate(int fps);
	android::status_t setBitrate(const android::sp<android::MetaData> &videoMetaData, int bitrate=0);
    android::status_t setAVCIntraPeriod (const android::sp<android::MetaData> &videoMetaData) ;
	void getEncoderInfo(int *encoderInfo,int encoderFrameRate,int encoderBitRate,int encoderIFrameInterval,int encoderPFrameCounter);
	void clipVideoBitRate();
	void clipVideoFrameRate();
	void clipVideoFrameWidth();
	void clipVideoFrameHeight();
    
    android::sp<android::MediaSource> GetEncoderMediaSource ();
};

#endif //VIDEO_ENCODER_CTRL_H_

