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

#define LOG_NDEBUG 0
#define LOG_TAG "VideoEncoderController"

#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>

#include "utils/Log.h"
#include "utility.h"
#include "VideoEncoderController.h"
#include "MediaConstants.h"

#include "OMX_Image.h"
#include "OMX_Video.h"
#if defined(NV_OMX)
// NVIDIA Tegra: OpenMAX extensions
#include "NVOMX_CameraExtensions.h"
#endif

using namespace android;

template <typename T>
void INIT_PARAM(T &p, OMX_U32 portIndex) {
	memset(&p, 0, sizeof(T));
	p.nSize = sizeof(T);
	p.nVersion.s.nVersionMajor = 1;
	p.nPortIndex = portIndex;
}

VideoEncoderController::VideoEncoderController() :
	mEncoder(NULL),
	mIOMX(NULL),
	mNode(0) {
	F_LOG;
	mEncoderProfiles = MediaProfiles::getInstance();	
}

status_t VideoEncoderController::setupEncoder(sp<IOMX> & IOMX, sp<CameraSource> cameraSource, 
                                            const sp<MetaData> &videoMetaData) {
	F_LOG;
	
	mIOMX = IOMX;
	videoMetaData->findInt32(kKeyWidth, &mVideoWidth);
	videoMetaData->findInt32(kKeyHeight, &mVideoHeight);
	videoMetaData->findInt32(kKeySampleRate, &mFrameRate);
	videoMetaData->findCString(kKeyMIMEType, (const char**)&mVideoEncoder); //should be MEDIA_MIME_TYPE_VIDEO_AVC
	LOGD("kKeyWidth:%d kKeyHeight:%d kKeySampleRate:%d kKeyMIMEType:%s",
	    mVideoWidth, mVideoHeight, mFrameRate, mVideoEncoder);

#if 0	
	clipVideoBitRate();
	clipVideoFrameRate();
	clipVideoFrameWidth();
	clipVideoFrameHeight();
#endif

	sp<android::MetaData> encoderMetaData = new android::MetaData;
	encoderMetaData->setCString(kKeyMIMEType, android::MEDIA_MIMETYPE_VIDEO_AVC);
	encoderMetaData->setInt32(kKeyBitRate, getBitrate(videoMetaData)); // bps
	encoderMetaData->setInt32(kKeySampleRate, mFrameRate); // video frame rate
	encoderMetaData->setInt32(kKeyIFramesInterval, getIFrameInterval(videoMetaData)); 

	// Get stride, slice height and color format from camera
	CHECK(cameraSource != 0);
	sp<MetaData> camMeta = cameraSource->getFormat();
	CHECK(camMeta != 0);

	int32_t stride, sliceHeight, colorFormat;
	int32_t width, height;
	CHECK(camMeta->findInt32(kKeyWidth, &width));
	CHECK(camMeta->findInt32(kKeyHeight, &height));
	CHECK(camMeta->findInt32(kKeyStride, &stride));
	CHECK(camMeta->findInt32(kKeySliceHeight, &sliceHeight));
	CHECK(camMeta->findInt32(kKeyColorFormat, &colorFormat));

	encoderMetaData->setInt32(kKeyWidth, width); //width);
	encoderMetaData->setInt32(kKeyHeight, height); //height);
	encoderMetaData->setInt32(kKeyStride, stride);
	encoderMetaData->setInt32(kKeySliceHeight, sliceHeight);
	encoderMetaData->setInt32(kKeyColorFormat, colorFormat);

	Log("Creating encoder (%ux%u) @%dFPS", mVideoWidth, mVideoHeight, mFrameRate);
	mEncoder = android::OMXCodec::Create(IOMX, encoderMetaData, true, cameraSource);
	if (!(mEncoder->getFormat()->findInt32(kKeyNodeId, (int32_t*) &mNode))) {
		LOGI("Failed to retrieve Encoder Node ID");
		return UNKNOWN_ERROR;
	}
	initialiseAvcOutput(getBitrate(videoMetaData));
	LOGI("Retrieved Encoder Node ID: %d", (status_t) mNode);
	return OK;
}

status_t VideoEncoderController::stopEncoder() {
	F_LOG;
	if (mEncoder == NULL) {
		return UNKNOWN_ERROR;
	}
	mEncoder.clear();
	mEncoder = NULL;
	return OK;
}

status_t VideoEncoderController::initialiseAvcOutput (int initialBitrate) {
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	INIT_PARAM(format, PORT_OUTPUT);

	// Quick sanity check
	status_t err = mIOMX->getParameter(mNode, OMX_IndexParamVideoPortFormat, &format, sizeof(format));
	CHECK_EQ(err, OK);

	LOGV("portIndex: %ld, index: %ld, eCompressionFormat=%d eColorFormat=%d",
			format.nPortIndex, format.nIndex, format.eCompressionFormat, format.eColorFormat);

    /* Setting AVC profile */			
   	OMX_VIDEO_PARAM_AVCTYPE h264type; // AVC params
	INIT_PARAM(h264type, PORT_OUTPUT);
	err = mIOMX->getParameter(mNode, OMX_IndexParamVideoAvc, &h264type, sizeof(h264type));
	CHECK_EQ(err, OK);
	LOGV("AVParam:\n\t"
	    "eProfile=%u\n\t"
	    "eLevel=%u\n\t"
	    "bEnableFMO=%u\n\t"
	    ,h264type.eProfile
	    ,h264type.eLevel
	    ,h264type.bEnableFMO);
    /* Set level to atelast 3.1 in order to achieve 720p encoding */
	h264type.eLevel = OMX_VIDEO_AVCLevel31;
	h264type.eProfile = OMX_VIDEO_AVCProfileBaseline;
	err = mIOMX->setParameter(mNode, OMX_IndexParamVideoAvc, &h264type, sizeof(h264type));
	Log("SetParam(VideoAvc) result=%u", err);

    /* Setting quantization */
	OMX_VIDEO_PARAM_QUANTIZATIONTYPE quatization;
	INIT_PARAM(quatization, PORT_OUTPUT);
	quatization.nQpI = M_QUNATIZATION_PI;
	quatization.nQpP = M_QUNATIZATION_PP;
	err = mIOMX->setParameter(mNode, OMX_IndexParamVideoQuantization, &quatization, sizeof(quatization));
	Log("SetParams(Quantization) result=%u", err);

    /* Setting initial bitrate */	
	OMX_VIDEO_PARAM_BITRATETYPE bitrateParm;
	INIT_PARAM(bitrateParm, PORT_OUTPUT);
	bitrateParm.eControlRate = OMX_Video_ControlRateVariable /*OMX_Video_ControlRateConstant*/;
	bitrateParm.nTargetBitrate = initialBitrate; // Target average bitrate to be generated in bps
	err = mIOMX->setParameter(mNode, OMX_IndexParamVideoBitrate, &bitrateParm, sizeof(bitrateParm));
	Log("SetParams(VideoBitrate) result=%u", err);
    return err;
}


status_t VideoEncoderController::requestIFrame (void) {
    F_LOG;
	if (mIOMX == NULL) {
		return UNKNOWN_ERROR;
	}

	OMX_CONFIG_INTRAREFRESHVOPTYPE refresh;
	INIT_PARAM(refresh, PORT_OUTPUT);
	refresh.IntraRefreshVOP = OMX_TRUE;
	status_t err = mIOMX->setConfig(mNode, OMX_IndexConfigVideoIntraVOPRefresh, &refresh, sizeof(refresh));
	if (OK != err) {
		Log("RequestKeyFrame ERROR: %d ( %x )", err, err);
	}
	return err;
}

status_t VideoEncoderController::setFramerate(int fps) {
	if (mIOMX == NULL) {
		return UNKNOWN_ERROR;
	}
	status_t err;

	OMX_CONFIG_FRAMERATETYPE framerateCfg; // Defines Encoder Frame Rate setting
	INIT_PARAM(framerateCfg, PORT_OUTPUT);
	framerateCfg.xEncodeFramerate = fps << 16; // Encoding framerate represented in Q16 format
	err = mIOMX->setConfig(mNode, OMX_IndexConfigVideoFramerate, &framerateCfg, sizeof(framerateCfg));
	if (OK != err) {
		Log("SetFramerate ERROR: %d (%x) rate: %d (%lu)", err, err, fps, framerateCfg.xEncodeFramerate);
		return UNKNOWN_ERROR;
	}
	else {
		mFrameRate = fps;
		Log("SetFramerate: %d (%lu)", fps, framerateCfg.xEncodeFramerate);
	}
	return err;
}

status_t VideoEncoderController::setBitrate(const sp<MetaData> &videoMetaData, int bitrate){
    F_LOG;
	if (mIOMX.get() == NULL) {
		return UNKNOWN_ERROR;
	}
	status_t    err;
	
	OMX_VIDEO_CONFIG_BITRATETYPE bitrateCfg; // Structure for dynamically configuring bitrate mode of a codec.
	INIT_PARAM(bitrateCfg, PORT_OUTPUT);
	if (bitrate != 0) {
    	bitrateCfg.nEncodeBitrate = bitrate;
	} else {
	    /* configure based on quality */
    	bitrateCfg.nEncodeBitrate = getBitrate (videoMetaData);
    }
    LOGV("[Lakshmi]bitrate:%d", bitrate);
	err = mIOMX->setConfig(mNode, OMX_IndexConfigVideoBitrate, &bitrateCfg, sizeof(bitrateCfg));
	if (OK != err) {
		Log("SetBitrate ERROR: %d ( %x ) rate: %d !!!!!", err, err, bitrate);
	}
	return err;
}

//only for H264
status_t VideoEncoderController::setAVCIntraPeriod (const sp<MetaData> &videoMetaData) {
    F_LOG;
	if (mIOMX.get() == NULL) {
	    LOGV("IOMX is not created !!!");
		return UNKNOWN_ERROR;
	}
	status_t    err;
	int         vQuality;
	
	videoMetaData->findInt32(kKeyP2PVideoQuality, &vQuality);

	OMX_VIDEO_CONFIG_AVCINTRAPERIOD avcintraperiod;
	INIT_PARAM(avcintraperiod, PORT_OUTPUT);
	switch (vQuality) {
	    case E_HIGH:{
	        avcintraperiod.nIDRPeriod = M_IDR_PERIOD_LOW;
	        avcintraperiod.nPFrames = M_NPFRAMES_LOW;
	    }break;
	    case E_MEDIUM:{
	        avcintraperiod.nIDRPeriod = M_IDR_PERIOD_MEDIUM;
	        avcintraperiod.nPFrames = M_NPFRAMES_MEDIUM;
	    }break;
	    case E_LOW:{
	        avcintraperiod.nIDRPeriod = M_IDR_PERIOD_HIGH;
	        avcintraperiod.nPFrames = M_NPFRAMES_HIGH;
	    }break;
	    default:
	        LOGV("Invalid quality type");
	}
	err = mIOMX->setConfig(mNode, OMX_IndexConfigVideoAVCIntraPeriod, &avcintraperiod, sizeof(avcintraperiod));
	if (OK != err) {
		Log("setAVCIntraPeriod ERROR: %d ( %x ) quality: %d !!!!!", err, err, vQuality);
	}
	return err;
}

android::sp<android::MediaSource> VideoEncoderController::GetEncoderMediaSource () {
    return mEncoder;
}

int VideoEncoderController::getBitrate(const android::sp<android::MetaData> &videoMetaData) {
    int vQuality, bitRate;
    
	videoMetaData->findInt32(kKeyP2PVideoQuality, &vQuality);
	switch (vQuality) {
	    case E_HIGH:{
        	bitRate = M_BITRATE_HIGH;
	    }break;
	    case E_MEDIUM:{
        	bitRate = M_BITRATE_MEDIUM;
	    }break;
	    case E_LOW:{
        	bitRate = M_BITRATE_LOW;
	    }break;
	    default: {
        	bitRate = M_BITRATE_LOW;
	        LOGV("Invalid quality type");
	    }
	}
	
	LOGV("Quality:%d bitrate:%d", vQuality, bitRate);
	return bitRate;
}

int VideoEncoderController::getIFrameInterval(const sp<MetaData> &videoMetaData) {
    int vQuality, iFrameinterval;
    
	videoMetaData->findInt32(kKeyP2PVideoQuality, &vQuality);
	switch (vQuality) {
	    case E_HIGH:{
        	iFrameinterval = M_NPFRAMES_LOW/mFrameRate;
	    }break;
	    case E_MEDIUM:{
        	iFrameinterval = M_NPFRAMES_MEDIUM/mFrameRate;
	    }break;
	    case E_LOW:{
        	iFrameinterval = M_NPFRAMES_HIGH/mFrameRate;
	    }break;
	    default: {
        	iFrameinterval = M_NPFRAMES_MEDIUM/mFrameRate;
	        LOGV("Invalid quality type");
	    }
	}
	mFrameinterval = iFrameinterval;
	LOGV("Quality:%d iFrameinterval:%d", vQuality, mFrameinterval);
	return mFrameinterval;
}
void VideoEncoderController::getEncoderInfo(int *encoderInfo ,int encoderFrameRate,int encoderBitRate,int encoderIFrameInterval,int encoderPFrameCounter)
{
	status_t err;
	/* Encoder Info Structure
	 * [0] :->  FrameRate
	 * [1] :->  Bit Rate
	 * [2] :->  Color Format
	 * [3] :->  IFrame Interval
	 * [4] :->  IDR Period
	 * [5] :->  PFrame Interval
	 * [6] :->  Actual Frame Rate
	 * [7] :->  Actual Bit Rate
	 * [8] :->  Actual IFrame Interval
	 * [9] :->  P Frame Interval
	 */
	 
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	INIT_PARAM(format, PORT_OUTPUT);
	err = mIOMX->getParameter(mNode, OMX_IndexParamVideoPortFormat, &format, sizeof(format));
	if(err == OK)
	{
		LOGV("FORMAT TYPE: portIndex: %ld, index: %ld, eCompressionFormat=%d eColorFormat=%d",format.nPortIndex, format.nIndex, format.eCompressionFormat, format.eColorFormat);
		encoderInfo[2] = format.eColorFormat;
	}
	OMX_VIDEO_PARAM_BITRATETYPE bitRate;
	INIT_PARAM(bitRate, PORT_OUTPUT);
	err = mIOMX->getParameter(mNode, OMX_IndexParamVideoBitrate, &bitRate, sizeof(bitRate));
	if(err == OK)
	{
		LOGV("BIT RATE : portIndex: %ld, Target BitRate : %d",bitRate.nPortIndex, bitRate.nTargetBitrate);
		encoderInfo[1] = bitRate.nTargetBitrate;
	}
	
	OMX_CONFIG_FRAMERATETYPE framerateCfg; // Defines Encoder Frame Rate setting
	INIT_PARAM(framerateCfg, PORT_OUTPUT);
	err = mIOMX->getConfig(mNode, OMX_IndexConfigVideoFramerate, &framerateCfg, sizeof(framerateCfg));
	if (OK == err) {
		Log("Origianl Value %f",framerateCfg.xEncodeFramerate);
		framerateCfg.xEncodeFramerate = (framerateCfg.xEncodeFramerate >> 16);
		Log("FRAME RATE: portIndex:%ld, Frame Rate: %d", framerateCfg.nPortIndex,framerateCfg.xEncodeFramerate);
		encoderInfo[0] = framerateCfg.xEncodeFramerate;
		/* Here, if framerate is not set dynamically, application will get frame rate as ZERO value.
		 * so, set frame rate as original value */
		if(encoderInfo[0] == 0)
		{
			Log("showing original framerate value");
			encoderInfo[0] = mFrameRate;
		}
	}
	
	encoderInfo[3] = mFrameinterval;
	Log(" IFrameRate: %ld",mFrameinterval);

	OMX_VIDEO_CONFIG_AVCINTRAPERIOD avcintraperiod;
	INIT_PARAM(avcintraperiod, PORT_OUTPUT);
	err = mIOMX->getConfig(mNode, OMX_IndexConfigVideoAVCIntraPeriod, &avcintraperiod, sizeof(avcintraperiod));
	if (OK == err) {
		Log("AVCIntraPeriod: portIndex:%ld, IDRPeriod:%d PFrames:%d", avcintraperiod.nPortIndex,avcintraperiod.nIDRPeriod,avcintraperiod.nPFrames);
		encoderInfo[4] = avcintraperiod.nIDRPeriod;
		encoderInfo[5] = avcintraperiod.nPFrames;
	}
	
	/* Encoder Frame Rate */
	encoderInfo[6] = encoderFrameRate;
	
	/* Encoder Bit Rate */
	encoderInfo[7] = encoderBitRate;

	/* IFrame Interval */
	encoderInfo[8] = encoderIFrameInterval;

	/* PFrame Interval */
	encoderInfo[9] = encoderPFrameCounter;
}


#if 0

//Picked from StagefrightRecorder
void VideoEncoderController::clipVideoFrameRate() {
    LOGV("clipVideoFrameRate: encoder %d", mVideoEncoder);
    int minFrameRate = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.fps.min", mVideoEncoder);
    int maxFrameRate = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.fps.max", mVideoEncoder);
    if (mFrameRate < minFrameRate) {
        LOGW("Intended video encoding frame rate (%d fps) is too small"
             " and will be set to (%d fps)", mFrameRate, minFrameRate);
        mFrameRate = minFrameRate;
    } else if (mFrameRate > maxFrameRate) {
        LOGW("Intended video encoding frame rate (%d fps) is too large"
             " and will be set to (%d fps)", mFrameRate, maxFrameRate);
        mFrameRate = maxFrameRate;
    }
}

void VideoEncoderController::clipVideoBitRate() {
    LOGV("clipVideoBitRate: encoder %d", mVideoEncoder);
    int minBitRate = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.bps.min", mVideoEncoder);
    int maxBitRate = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.bps.max", mVideoEncoder);
    if (mVideoBitRate < minBitRate) {
        LOGW("Intended video encoding bit rate (%d bps) is too small"
             " and will be set to (%d bps)", mVideoBitRate, minBitRate);
        mVideoBitRate = minBitRate;
    } else if (mVideoBitRate > maxBitRate) {
        LOGW("Intended video encoding bit rate (%d bps) is too large"
             " and will be set to (%d bps)", mVideoBitRate, maxBitRate);
        mVideoBitRate = maxBitRate;
    }
}

void VideoEncoderController::clipVideoFrameWidth() {
    LOGV("clipVideoFrameWidth: encoder %d", mVideoEncoder);
    int minFrameWidth = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.width.min", mVideoEncoder);
    int maxFrameWidth = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.width.max", mVideoEncoder);
    if (mVideoWidth < minFrameWidth) {
        LOGW("Intended video encoding frame width (%d) is too small"
             " and will be set to (%d)", mVideoWidth, minFrameWidth);
        mVideoWidth = minFrameWidth;
    } else if (mVideoWidth > maxFrameWidth) {
        LOGW("Intended video encoding frame width (%d) is too large"
             " and will be set to (%d)", mVideoWidth, maxFrameWidth);
        mVideoWidth = maxFrameWidth;
    }
}

void VideoEncoderController::clipVideoFrameHeight() {
    LOGV("clipVideoFrameHeight: encoder %d", mVideoEncoder);
    int minFrameHeight = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.height.min", mVideoEncoder);
    int maxFrameHeight = mEncoderProfiles->getVideoEncoderParamByName(
                        "enc.vid.height.max", mVideoEncoder);
    if (mVideoHeight < minFrameHeight) {
        LOGW("Intended video encoding frame height (%d) is too small"
             " and will be set to (%d)", mVideoHeight, minFrameHeight);
        mVideoHeight = minFrameHeight;
    } else if (mVideoHeight > maxFrameHeight) {
        LOGW("Intended video encoding frame height (%d) is too large"
             " and will be set to (%d)", mVideoHeight, maxFrameHeight);
        mVideoHeight = maxFrameHeight;
    }
}

#endif


