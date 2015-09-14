

#define LOG_NDEBUG 0
#define LOG_TAG "NativeCodeCaller"

#include "jni.h"
#include "nativehelper/JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"
#include <surfaceflinger/Surface.h>
#include <stdlib.h>

#include "utils/Log.h"
#include "utility.h"
#include "Transport.h"
#include "MediaController.h"
#include "MediaConstants.h"
#include "surface-hack.h"
#include "player/DSPGMediaPlayer.h"

using namespace android;

// ----------------------------------------------------------------------------
static const char* const kClassPathName = "com/p2p/app/NativeCodeCaller";

using android::sp;
using android::Surface;
using android::ISurface;


struct ENCODERINFO{
      int frameRate;
      int bitRate;
      int colorFormat;
      int iFrameInterval;
      int IDRPeriod;
      int pFrameInterval;
      int encoderFrameRate;
      int encoderBitRate;
      int encoderiFrameInterval;
      int encoderPFrameCounter;
}encoderInformation;


struct fields_t {
	jfieldID	context; // Refers to this.mContext
	jfieldID	surface; // Refers to android.view.Surface
    jfieldID    m_frameRate ;
    jfieldID    m_width ;
    jfieldID    m_height ;
    jfieldID    m_videoQuality ;
    jfieldID    m_videoEncoder ; //TBD: Initially only H264 is supported
    jfieldID    m_localIP ;
    jfieldID    m_enableAudio ;
    jfieldID    m_enableVideo ;
    jfieldID    m_enableAudioViaCss;
    jfieldID    m_audioCodecs;	
};

struct st_playback {
} ;

struct st_recording {
} ;

static fields_t fields;
static Mutex sLock;


class JNINativeContext : public RefBase {
private:
	jobject     mNativeJObjectWeak;	// Weak reference to java object
	jclass      mNativeJClass;		// Strong reference to java class

    sp<MetaData>        m_confMetaData ;
	MediaController     *m_mediaController ;
	sp<Surface> 		m_mainSurface ;	
	sp<Surface> 		m_subSurface ;


public:
	JNINativeContext(JNIEnv* env, jobject weak_this, jclass clazz){
    	F_LOG;
        mNativeJObjectWeak = env->NewGlobalRef(weak_this);
	    mNativeJClass = (jclass)env->NewGlobalRef(clazz);
    }

	~JNINativeContext() {
    	F_LOG;
	    stop();
	    //delete m_mediaController ;
    }
    
	void init(void) {
		F_LOG;
		status_t ret ;

		m_mediaController = new MediaController();
		if (m_mediaController != NULL) {		    		
			m_mediaController->Init();
		} else {
			LOGE("Failed to create MediaController instance !!!!!!!");
			ret = NO_MEMORY;
		}
	}

	void shutdown(void) {
		F_LOG;
		status_t ret ;
		
		if (m_mediaController != NULL) {
			delete m_mediaController;
			m_mediaController = NULL;
		}
	}
	
    status_t initMetadata(JNIEnv *env, jobject cls, const char *remoteIP, int remoteport, int localport) {
    	F_LOG;
        if (m_confMetaData.get() == NULL) {        
            m_confMetaData = new MetaData();
            if (m_confMetaData == NULL) {
                LOGE("Failed to allocate MetaData !!!");
                return NO_MEMORY;
            }
        }

        //remote port & IP        
        m_confMetaData->setInt32(kKeyP2PLocalPort, localport);
        m_confMetaData->setInt32(kKeyP2PRemotePort, remoteport);
        m_confMetaData->setCString(kKeyP2PRemoteIP, remoteIP);

        //video frame rate
        int tmpfielddval = env->GetIntField(cls, fields.m_frameRate);
        m_confMetaData->setInt32(kKeySampleRate, tmpfielddval);

        //video resolution
        tmpfielddval = env->GetIntField(cls, fields.m_width);
        m_confMetaData->setInt32(kKeyWidth, tmpfielddval);
        tmpfielddval = env->GetIntField(cls, fields.m_height);
        m_confMetaData->setInt32(kKeyHeight, tmpfielddval);
        
        //video quality
        tmpfielddval = env->GetIntField(cls, fields.m_videoQuality);
        m_confMetaData->setInt32(kKeyP2PVideoQuality, tmpfielddval);
        
        //mimetype
        jstring jtmpstr = (jstring)env->GetObjectField(cls, fields.m_videoEncoder);
        const char *mimetype = env->GetStringUTFChars(jtmpstr, NULL);
        if (mimetype == NULL) {
            LOGE("Unable to fetch mimetype !!!!");
        } else {
            LOGV("call mimetype: %s", mimetype);
            m_confMetaData->setCString(kKeyMIMEType, mimetype);
        }
        env->ReleaseStringUTFChars(jtmpstr, mimetype);

		//Audio Codecs
        jtmpstr= (jstring)env->GetObjectField(cls, fields.m_audioCodecs);
        const char *audioCodecs = env->GetStringUTFChars(jtmpstr, NULL);
        if (audioCodecs == NULL) {
            LOGE("Unable to fetch audioCodecs !!!!");
        } else {
            LOGE("call audioCodecs: %s", audioCodecs);
            m_confMetaData->setCString(kKeyP2PAudioCodecs, audioCodecs);
        }
        env->ReleaseStringUTFChars(jtmpstr, audioCodecs);

        //local IP
        jtmpstr = (jstring)env->GetObjectField(cls, fields.m_localIP);
        const char *localIP = env->GetStringUTFChars(jtmpstr, NULL);
        if (localIP == NULL) {
            LOGE("Unable to fetch local IP !!!!");
        } else {
            m_confMetaData->setCString(kKeyP2PLocalIP, localIP);
        }
        env->ReleaseStringUTFChars(jtmpstr, localIP);
        
        jboolean btmpval = env->GetBooleanField(cls, fields.m_enableAudio);
        m_confMetaData->setInt32(kKeyP2PAudioEnabled, btmpval);

        btmpval = env->GetBooleanField(cls, fields.m_enableAudioViaCss);
        m_confMetaData->setInt32(kKeyP2PAudioEnabledViaCss, btmpval);

        btmpval = env->GetBooleanField(cls, fields.m_enableVideo);
        m_confMetaData->setInt32(kKeyP2PVideoEnabled, btmpval);

        /* Set the preview and remote surface handles */
        m_confMetaData->setPointer(kKeyP2PPreviewSurface, m_subSurface.get());
        m_confMetaData->setPointer(kKeyP2PRemoteSurface, m_mainSurface.get());
        return OK;
    }	

	status_t start(void) {
	    F_LOG;
	    status_t ret ;
	    int enableAudio, enableVideo, enableAudioViaCss;

        m_confMetaData->findInt32(kKeyP2PAudioEnabled, &enableAudio);
        m_confMetaData->findInt32(kKeyP2PVideoEnabled, &enableVideo);
		m_confMetaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);

		
        //Invalid case, both audio & video disabled
        if (enableAudio == false && enableVideo == false){
            LOGE("Both audio and video options are disabled !!!");
            return UNKNOWN_ERROR;
        }

        m_mediaController->createSockets(m_confMetaData);
        /* start local video-audio recording */
        ret = m_mediaController->StartRecording(m_confMetaData);
        if (ret != OK)
            LOGE("Failed to start media recording !!!!!");

        /* start remote video-audio playback */
        ret = m_mediaController->StartPlayback(m_confMetaData);
        if (ret != OK)
            LOGE("Failed to start media playback !!!!!");

		/* If Audio Via CSS is configured, start CSS Audio */
		if (enableAudio && enableAudioViaCss)
		{
			ret = m_mediaController->StartCssAudio(m_confMetaData);
			if (ret != OK)
			{
					LOGE("Failed to start CSS audio!!!!");
					m_mediaController->StopRecording();
			        m_mediaController->StopPlayback();
					m_mediaController->closeSockets();
			}
        }
         LOGE("start end bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
		return ret;
	}


	status_t stop() {
    	F_LOG;
		int enableAudioViaCss, ret;
	    if (m_mediaController != NULL) {	
			/* If Audio Via CSS is configured, start CSS Audio */
			if(m_confMetaData != NULL) {
				m_confMetaData->findInt32(kKeyP2PAudioEnabledViaCss, &enableAudioViaCss);
			}
			if (enableAudioViaCss)
			{
				ret = m_mediaController->StopCssAudio();
				if (ret != OK)
				        LOGE("Failed to stop CSS audio!!!!!");
			}				
		    m_mediaController->StopRecording();
		    m_mediaController->StopPlayback();
		
			m_mediaController->closeSockets();			
		}
		return OK;
	}
	
	status_t configFrameRateInfo (int fps) {
	    F_LOG;
	    
	    LOGV("New FPS: %d", fps);
	    status_t retval = OK;
	    if (m_mediaController != NULL) {
	        int previousVal ;
	        
            m_confMetaData->findInt32(kKeySampleRate, &previousVal);
            
	        /* Ignore if its same value */
            if (previousVal != fps) {
                m_confMetaData->setInt32(kKeySampleRate, fps);
                retval = m_mediaController->configureFramerate(m_confMetaData);
            } else {
                retval = OK;
            }
        } else {
            LOGE("Call is not established !!!!!");
            retval = UNKNOWN_ERROR;
        }
        return retval;
	}
	
	status_t configQuality (int videoquality) {
	    F_LOG;

	    status_t retval = OK;
	    LOGV("New VideoQuality: %d", videoquality);
	    if (m_mediaController != NULL) {
	        int previousVal;

            m_confMetaData->findInt32(kKeyP2PVideoQuality, &previousVal);
	        
	        /* Ignore if its same value */
	        if (previousVal != videoquality) {
                m_confMetaData->setInt32(kKeyP2PVideoQuality, videoquality);
                retval = m_mediaController->configureQuality(m_confMetaData);
            } else {
                retval = OK;
            }
        } else {
            LOGE("Call is not established !!!!!");
            retval = UNKNOWN_ERROR;
        }
        return retval;
	}

	status_t requestIFrame (void) {
	    F_LOG;
	    return m_mediaController->requestIFrame();
	}
	
	bool setVideoSurface(sp<Surface> videoSurface) {
    	F_LOG;
		m_mainSurface = videoSurface ;
		return true ;
	}

	bool setPreviewSurface(sp<Surface> surface) {
    	F_LOG;
		m_subSurface = surface;
		return true ;
	}

	bool toggleVideoDisplay(sp<Surface> camerasurface, sp<Surface> remotesurface) {
    	F_LOG;
		m_mediaController->toggleVideoDisplay (camerasurface, remotesurface);		
		return true ;
	}
	void getEncoderInfo(int *encoderInfo) {
		F_LOG;
		m_mediaController->getEncoderInfo(encoderInfo);
	}

	#ifndef USE_AWESOME_PLAYER
		void getDecoderInfo(int *decoderInfo) {
			F_LOG;
			m_mediaController->getDecoderInfo(decoderInfo);
		}
	#endif



};

JNINativeContext * getContext(JNIEnv *env, jobject & thiz) {
	return reinterpret_cast<JNINativeContext*>(env->GetIntField(thiz, fields.context));
}


static void JNICALL native_setup(JNIEnv *env, jobject thiz, jobject weak_this) {
	F_LOG;

	jclass clazz = env->GetObjectClass(thiz);
	if (clazz == NULL) {
		jniThrowException(env, "java/lang/RuntimeException", "Can't find com/p2p/app/NativeCodeCaller");
		return;
	}

	sp<JNINativeContext> context = new JNINativeContext(env, weak_this, clazz);
	context->incStrong(thiz);

	// Save context in opaque field
	env->SetIntField(thiz, fields.context, (int)context.get());

	// Initialize the library
	getContext(env, thiz)->init();
}

static void JNICALL native_release(JNIEnv *env, jobject thiz) {
	F_LOG;

	JNINativeContext* context = NULL;
	{
		Mutex::Autolock _l(sLock);
		context = reinterpret_cast<JNINativeContext*>(env->GetIntField(thiz, fields.context));

		// Make sure we do not attempt to callback on a deleted Java object.
		env->SetIntField(thiz, fields.context, 0);
	}
	// Clean up if release has not been called before
	if (context != NULL) {
		// Remove context to prevent further Java access
		context->decStrong(thiz);
	}
}

static void JNICALL getEncoderInfo(JNIEnv *env, jobject thiz,jobject info) {
	F_LOG;
	int *encoderInfo = new int[10];
	int *decoderInfo = new int[3];

	jclass clazz;
	jfieldID fid;

	getContext(env, thiz)->getEncoderInfo(encoderInfo);

	#ifndef USE_AWESOME_PLAYER
	getContext(env, thiz)->getDecoderInfo(decoderInfo);
	#endif

	#if 0
	LOGV("FrameRate %d",encoderInfo[0]);
	LOGV("BitRate : %d",encoderInfo[1]);
	LOGV("Compression format : %d",encoderInfo[2]);
	LOGV("IFRAME interval %d",encoderInfo[3]);
	LOGV("IDR Period:%d",encoderInfo[4]);
	LOGV("pFrame:%d",encoderInfo[5]);
	LOGV("Actual Frame Rate: %d",encoderInfo[6]);
	LOGV("Actual Bit Rate: %d",encoderInfo[7]);
	LOGV("IFrameInterval :%d",encoderInfo[8]);
	LOGV("PFrameInterval :%d",encoderInfo[9]);
	#endif

	#if 1
		#ifndef USE_AWESOME_PLAYER
		LOGV("Frame Rate: %d",decoderInfo[0]);
		LOGV("Corrupted Frame Rate: %d",decoderInfo[1]);
		LOGV("Number of corruped MB:%d",decoderInfo[2]);
		#endif
	#endif

	clazz = env->GetObjectClass(info);
	if (0 == clazz)
	{
		printf("GetObjectClass returned 0\n");
		return;
	}
	fid = env->GetFieldID(clazz,"frameRate","I");
	env->SetIntField(info,fid,encoderInfo[0]);

	fid = env->GetFieldID(clazz,"bitRate","I");
	env->SetIntField(info,fid,encoderInfo[1]);

	fid = env->GetFieldID(clazz,"colorFormat","I");
	env->SetIntField(info,fid,encoderInfo[2]);

	fid = env->GetFieldID(clazz,"iFrameInterval","I");
	env->SetIntField(info,fid,encoderInfo[3]);

	fid = env->GetFieldID(clazz,"IDRPeriod","I");
	env->SetIntField(info,fid,encoderInfo[4]);

	fid = env->GetFieldID(clazz,"pFrameInterval","I");
	env->SetIntField(info,fid,encoderInfo[5]);

	fid = env->GetFieldID(clazz,"encoderFrameRate","I");
	env->SetIntField(info,fid,encoderInfo[6]);
	
	fid = env->GetFieldID(clazz,"encoderBitRate","I");
	env->SetIntField(info,fid,encoderInfo[7]);
	
	fid = env->GetFieldID(clazz,"encoderiFrameInterval","I");
	env->SetIntField(info,fid,encoderInfo[8]);

	fid = env->GetFieldID(clazz,"encoderPFrameCounter","I");
	env->SetIntField(info,fid,encoderInfo[9]);

	#ifndef USE_AWESOME_PLAYER
	fid = env->GetFieldID(clazz,"decoderFrameRate","I");
	env->SetIntField(info,fid,decoderInfo[0]);

	fid = env->GetFieldID(clazz,"decoderCorruptedFrameRate","I");
	env->SetIntField(info,fid,decoderInfo[1]);

	fid = env->GetFieldID(clazz,"decoderCorruptedMarcoBlock","I");
	env->SetIntField(info,fid,decoderInfo[2]);
	#endif

	#ifdef USE_AWESOME_PLAYER
	fid = env->GetFieldID(clazz,"decoderFrameRate","I");
	env->SetIntField(info,fid,0);

	fid = env->GetFieldID(clazz,"decoderCorruptedFrameRate","I");
	env->SetIntField(info,fid,0);

	fid = env->GetFieldID(clazz,"decoderCorruptedMarcoBlock","I");
	env->SetIntField(info,fid,0);
	#endif
	return;
}

static jint JNICALL startAVHosts(JNIEnv *env, jobject thiz, jint localport, jint remoteport, jstring remoteIP) {
	F_LOG;
	Mutex::Autolock _m(sLock);

	const char *IPaddr = env->GetStringUTFChars(remoteIP, NULL);
	if (IPaddr == NULL) {
		LOGE("!!! Invalid IP address !!!");
		return UNKNOWN_ERROR;
    }
    else {
	    LOGV("startAVHosts lport:%d rport:%d ip:%s", localport, remoteport, IPaddr);
	    jint result = getContext(env, thiz)->initMetadata(env, thiz, IPaddr, remoteport, localport);
	    if (result == OK) {
	        result = getContext(env, thiz)->start();
	    } else {
	        LOGE("initMetadata failed ret:%d", result);
	    }
	    env->ReleaseStringUTFChars(remoteIP, IPaddr);
        LOGE("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	    return result ;
	}
}

static jboolean JNICALL stopAVHosts(JNIEnv *env, jobject thiz) {
	F_LOG;
	Mutex::Autolock _m(sLock);
	return getContext(env, thiz)->stop() ? JNI_TRUE : JNI_FALSE;
}

static void JNICALL setVideoDisplay(JNIEnv *env, jobject thiz, jobject jSurface) {
	F_LOG;
	Mutex::Autolock _m(sLock);
	sp<Surface> surface = NULL;
	if (jSurface != NULL) {
		surface = reinterpret_cast<Surface*>(env->GetIntField(jSurface, fields.surface));
		getContext(env, thiz)->setVideoSurface(surface);
	}
}

static void JNICALL setPreviewDisplay(JNIEnv *env, jobject thiz, jobject jSurface) {
	F_LOG;
	Mutex::Autolock _m(sLock);
	sp<Surface> surface = NULL;
	if (jSurface != NULL) {
		surface = reinterpret_cast<Surface*>(env->GetIntField(jSurface, fields.surface));
		if (surface->isValid()) {
			LOGV("valid surface");
		} else {
			LOGE("invalid surface");
		}
		getContext(env, thiz)->setPreviewSurface(surface);
	}
}

static void JNICALL toggleVideoDisplay(JNIEnv *env, jobject thiz, jobject jCameraSurface, jobject jRemoteVideoSurface) {
	F_LOG;
	Mutex::Autolock _m(sLock);
	sp<Surface> camerasurface = NULL;
	sp<Surface> remotesurface = NULL;
	if (jCameraSurface != NULL && jRemoteVideoSurface != NULL) {
		camerasurface = reinterpret_cast<Surface*>(env->GetIntField(jCameraSurface, fields.surface));
		remotesurface = reinterpret_cast<Surface*>(env->GetIntField(jRemoteVideoSurface, fields.surface));
		if (camerasurface->isValid() && camerasurface->isValid()) {
			LOGV("valid surface");
		} else {
			LOGE("invalid surface");
		}
		getContext(env, thiz)->toggleVideoDisplay(camerasurface, remotesurface);
	}
}

static jint JNICALL requestIFrame(JNIEnv *env, jobject thiz) {
    F_LOG;
    return getContext(env, thiz)->requestIFrame();
}

static jint JNICALL configFrameRateInfo(JNIEnv *env, jobject thiz, jint framerate) {
    F_LOG;
    return getContext(env, thiz)->configFrameRateInfo(framerate);
}

static jint JNICALL configQualityInfo(JNIEnv *env, jobject thiz, jint videoquality) {
    return getContext(env, thiz)->configQuality(videoquality);
}

static jint JNICALL configResolutionInfo(JNIEnv *env, jobject thiz, jint width, jint height) {
    F_LOG;
    return OK;
}

/* ----------------------------------------------------------------------------
    JNI Initialisation part
 ---------------------------------------------------------------------------- */
static JNINativeMethod methods[] = {
	// Name			    ,Signature		        ,funcPtr
	{"native_setup"		,"(Ljava/lang/Object;)V",(void*)native_setup},
	{"native_release"	,"()V"				,(void*)native_release},
	{"startAVHosts"	,"(IILjava/lang/String;)I"		,(void*)startAVHosts},
	{"getEncoderInfo"	,"(Lcom/p2p/app/EncoderInfo;)V"				,(void*)getEncoderInfo},
	{"stopAVHosts"	,"()I"					,(void*)stopAVHosts},
	{"setVideoDisplay"	,"(Landroid/view/Surface;)V",(void*)setVideoDisplay},
	{"setPreviewDisplay","(Landroid/view/Surface;)V",(void*)setPreviewDisplay},
	{"toggleVideoDisplay","(Landroid/view/Surface;Landroid/view/Surface;)V",(void*)toggleVideoDisplay},
	{"requestIFrame"	        ,"()I"                 ,(void*)requestIFrame},
	{"configFrameRateInfo"	    ,"(I)I"					,(void*)configFrameRateInfo},
	{"configResolutionInfo"	    ,"(II)I"				,(void*)configResolutionInfo}, //TBD: Not yet implemeted
	{"configQualityInfo"    	    ,"(I)I"				,(void*)configQualityInfo},
};

struct field {
	const char *class_name;
	const char *field_name;
	const char *field_type;
	jfieldID *jfield;
};


#ifndef ANDROID_VIEW_SURFACE_JNI_ID
#define ANDROID_VIEW_SURFACE_JNI_ID "mSurface"
#endif

// Lifted from android_hardware_Camera
static int find_fields(JNIEnv *env, field *fields, int count) {
	for (int i = 0; i < count; i++) {
		field *f = &fields[i];
		jclass clazz = env->FindClass(f->class_name);
		if (clazz == NULL) {
			Log("Can't find %s", f->class_name);
			return -1;
		}

		jfieldID field = env->GetFieldID(clazz, f->field_name, f->field_type);
		if (field == NULL) {
			LOGE("Can't find %s.%s", f->class_name, f->field_name);
			return -1;
		}

		*(f->jfield) = field;
	}

	return OK;
}

int register_the_class(JNIEnv *_env) {
	// Cache the fieldIDs
	field fields_to_find[] = {
		{ kClassPathName		, "mNativeContext", "I", &fields.context },
		{ "android/view/Surface", ANDROID_VIEW_SURFACE_JNI_ID, "I", &fields.surface },
		{ kClassPathName		, "m_frameRate", "I", &fields.m_frameRate },
		{ kClassPathName		, "m_width", "I", &fields.m_width },
		{ kClassPathName		, "m_height", "I", &fields.m_height },
		{ kClassPathName		, "m_videoQuality", "I", &fields.m_videoQuality },
		{ kClassPathName		, "m_videoEncoder", "Ljava/lang/String;", &fields.m_videoEncoder },
		{ kClassPathName		, "m_localIP", "Ljava/lang/String;", &fields.m_localIP },
		{ kClassPathName		, "m_enableAudio", "Z", &fields.m_enableAudio },
		{ kClassPathName		, "m_enableVideo", "Z", &fields.m_enableVideo },
		{ kClassPathName		, "m_enableAudioViaCss", "Z", &fields.m_enableAudioViaCss },
		{ kClassPathName		, "m_audioEncoder", "Ljava/lang/String;", &fields.m_audioCodecs },
	};

	Log("find_fields: %d fields", lenof_x(fields_to_find));
	if (find_fields(_env, fields_to_find, NELEM(fields_to_find)) < 0) {
	    Log ("find_fields API faile!!!!!!!!!!");
		return -1;
	}

	Log("registerNativeMethods: %d functions", lenof_x(methods));
	return android::AndroidRuntime::registerNativeMethods(
		_env, kClassPathName, methods,
		lenof_x(methods)
		);
}



// Boilerplate OnLoad
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
	F_LOG;

	JNIEnv* env = 0;
	jint result = -1;

	if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK) {
		goto bail;
	}
	assert(env != 0);

	if (register_the_class(env) < 0) {
		goto bail;
	}

	result = JNI_VERSION_1_4;
bail:
	return result;
}
