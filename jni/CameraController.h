#ifndef CAMERA_CTRL_H_
#define CAMERA_CTRL_H_

#include <camera/CameraParameters.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/CameraSource.h>
#include <media/stagefright/TimeSource.h>
#include <sys/endian.h>

/**
 * Just holds the variables for camera
 * and does basic start & stop
 */
// TODO Separate resolutions for preview and capture
// TODO Set preview res according to the passed surface
// TODO Set capture res according to the negotiated params
#define MAX_FRAME_RATE_VALUES (30 + 1)

struct CameraController {
	android::sp<android::Camera> mCamera;
	android::sp<android::ICamera> mICamera;
	android::sp<android::CameraSource> mCameraSource;
	android::CameraParameters *mCameraParams;

	CameraController();
	~CameraController();

	android::sp<android::CameraSource> GetCameraSource ();
	android::status_t setupCamera(const android::sp<android::MetaData> &videoMetaData,
	                                const android::sp<android::Surface> &previewSurface);
	android::status_t stopCamera();
	android::status_t setFramerate(int fps);
	android::status_t ModifyPreviewSurface (android::sp<android::Surface> previewSurface);
	android::status_t startPreview();
	bool mInitialized;
	int mSupportedFps[MAX_FRAME_RATE_VALUES];
};

#endif //CAMERA_CTRL_H_
