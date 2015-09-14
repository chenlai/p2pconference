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


class MediaRecorder {
public:

	MediaRecorder();
	~MediaRecorder();

    int     prepare(Metadata &metaData)=0;
    int     start()=0;
    int     stop()=0;
    
    int     Configure (int key, void *pdata) = 0;


};
