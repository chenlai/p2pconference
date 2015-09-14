
#define LOG_NDEBUG 0
#define LOG_TAG "CameraController"

#include <camera/ICamera.h>
#include <camera/CameraParameters.h>
#include <media/stagefright/CameraSource.h>
#include <surfaceflinger/Surface.h>

#include "utils/Log.h"
#include "utility.h"
#include "CameraController.h"
#include "surface-hack.h"

using namespace android;

CameraController::CameraController() :
	mCamera(NULL),
	mICamera(NULL),
	mCameraParams(NULL),
	mInitialized(false)
{
	F_LOG;
}

CameraController::~CameraController() {
    F_LOG;
}

status_t CameraController::startPreview() {
	F_LOG;
	mCamera->startPreview();
	return OK;
}

status_t CameraController::setupCamera(const sp<MetaData> &videoMetaData, const sp<Surface> &previewSurface ) {
	F_LOG;
    int width, height, fps ;

	videoMetaData->findInt32(kKeyWidth, &width);
	videoMetaData->findInt32(kKeyHeight, &height);
	videoMetaData->findInt32(kKeySampleRate, &fps);
//	videoMetaData->findInt32(kKeyBitRate, &mVideoBitRate);
	//videoMetaData->findCString(kKeyMIMEType, &mVideoEncoder); //should be MEDIA_MIME_TYPE_VIDEO_AVC

	mCamera = android::Camera::connect(0);
	LOGD("After Camera::connect ");
	if (mCamera != NULL) {
		android::String8 s = mCamera->getParameters();
	    mCameraParams = new android::CameraParameters(s);
	    LOGV("Getting camera parameters");
	    char buf[50];
	    sprintf(buf, "%ux%u", width, height);
    	mCameraParams->set("video-size", buf);
	    mCameraParams->set("preview-format","yuv420sp");
	    mCameraParams->setPreviewSize(width, height);
	    mCameraParams->setPreviewFrameRate(fps);
	    LOGV("Setting camera params preview_size:%dx%d FPS:%d", width, height, fps);
	    mCamera->setParameters(mCameraParams->flatten());
	    
   	    mCameraSource = android::CameraSource::CreateFromCamera(mCamera);

	    LOGV("Setting preview");
	    mCamera->setPreviewDisplay(previewSurface);
	    // Get supported preview frame rates from camera driver
	    memset(mSupportedFps, 0, sizeof(mSupportedFps));
	    const char *fpsValues = mCameraParams->get(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES);
	    LOGV("Supported camera preview framerates: %s", fpsValues);
	    char *tokenString = new char[strlen(fpsValues)];
	    strcpy(tokenString, fpsValues);
	    char *fpsToken;
	    fpsToken = strtok(tokenString, ",");
	    while (fpsToken != NULL) {
		    if (atoi(fpsToken)< MAX_FRAME_RATE_VALUES) {
			    mSupportedFps[atoi(fpsToken)] = 1;
		    }
		    fpsToken = strtok(NULL, ",");
	    }

	    mInitialized = true;
	    setFramerate(fps);
	    return OK;
    } else {
		LOGE("************************* Failed to open camera ************************* ");
		return UNKNOWN_ERROR;
	}
}

status_t CameraController::stopCamera() {
	F_LOG;
	if (mCamera == NULL) {
		return false;
	}

	if ( mCamera.get() != NULL ) {
		if (mCamera->previewEnabled()) {
			mCamera->stopPreview();
		}
	}

	mCamera->disconnect();
	mCamera.clear();
	mCamera = NULL;
	return OK;
}

android::sp<android::CameraSource> CameraController::GetCameraSource (){
    return mCameraSource;
}

status_t CameraController::setFramerate(int fps) {
	F_LOG;
	status_t retval ;
	
	if (!mInitialized) {
		retval = UNKNOWN_ERROR;
	} else {
	    while ((fps > 0) && !mSupportedFps[fps]) {
		    fps--;
	    }
	    if (!fps) {
		    while ((fps < MAX_FRAME_RATE_VALUES) && !mSupportedFps[fps]) {
			    fps++;
		    }
	    }

	    if (mSupportedFps[fps]) {
		    LOGV("Actual set preview framerate: %d", fps);
		    mCameraParams->setPreviewFrameRate(fps);
		    retval = mCamera->setParameters(mCameraParams->flatten());
		    //mCamera->stopPreview();
		    //mCamera->startPreview();
		} else {
		    LOGE("Preview framerate not set");
		    retval = UNKNOWN_ERROR;
	    }
	}
	return retval ;
}

status_t CameraController::ModifyPreviewSurface (sp<Surface> previewSurface) {
	if (mCamera != NULL)
		mCamera->setPreviewDisplay(previewSurface);
		
	return OK;
}
