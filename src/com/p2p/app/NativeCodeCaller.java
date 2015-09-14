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

package com.p2p.app;
import java.lang.ref.WeakReference;
import android.view.Surface;
import android.content.SharedPreferences;
import android.util.Log;
import java.net.NetworkInterface;
import java.net.InetAddress;
import java.util.Enumeration;


public class NativeCodeCaller implements AppConstants{
	public static final String TAG = "NativeCodeCaller" ;

    private String  m_remoteIP ;
    private int     m_localPort ;
    private int     m_remotePort ;
    private boolean m_enableAudio;
    private boolean m_enableAudioViaCss;
    private boolean m_enableVideo;
    private boolean m_enableLog;

    private String m_audioEncoder;
    private long   m_audioSamplingRate;
    private String m_audioChannel;
    
    private String m_videoEncoder; //TBD: Initially only H264 is supported
    
    /* Video configurations */
    private int     m_frameRate ;
    private int     m_width ;
    private int     m_height ;
    private int     m_videoQuality ; 
    private String  m_localIP ;

	@SuppressWarnings("unused")
	private SharedPreferences m_settingsPref ;
	private int mNativeContext; // Accessed by native methods

	static {
		System.loadLibrary("P2PConference");
	}

	protected void finalize() {
		native_release();
	}

	NativeCodeCaller(SharedPreferences settingsPref) {		
		m_settingsPref = settingsPref;
		
	}
	
	public int init () {
	    native_setup(new WeakReference<NativeCodeCaller>(this));  
	    updateStaticSettings ();
	    updateDynamicSettings ();
	    /* Get the device IP address */
	    m_localIP = getLocalIpAddress();
	    Log.i(TAG, "######## Local device IP : "+m_localIP+" ########");	    
	    return OK;
	}
	
	public int updateStaticSettings() {
    	m_remoteIP = m_settingsPref.getString(KEY_REMOTE_IP, Settings.DEFAULT_IP); 	    
	    String remoteport = m_settingsPref.getString(KEY_REMOTE_PORT, Settings.DEFAULT_PORT);
	    m_remotePort = Integer.parseInt(remoteport);

	    String localport = m_settingsPref.getString(KEY_LOCAL_PORT, Settings.DEFAULT_PORT);
	    m_localPort = Integer.parseInt(localport);

		m_enableAudio = m_settingsPref.getBoolean(KEY_ENABLE_AUDIO,true);
		m_enableAudioViaCss = m_settingsPref.getBoolean(KEY_ENABLE_AUDIO_VIA_CSS,false);
		m_enableVideo = m_settingsPref.getBoolean(KEY_ENABLE_VIDEO,true);
		m_enableLog   = m_settingsPref.getBoolean(KEY_ENABLE_LOGGING,false);
		
		m_audioEncoder = m_settingsPref.getString(KEY_AUDIO_CODECS,MEDIA_MIMETYPE_AUDIO_AMR_NB);
		Log.d(TAG,"Audio Codecs : "+m_audioEncoder);
		if(m_audioEncoder != null) {
            if(m_audioEncoder.equalsIgnoreCase(MEDIA_MIMETYPE_AUDIO_AMR_NB)) {
	            m_audioSamplingRate = NB_SAMPLING_RATE;
            }
            else {
	            m_audioSamplingRate = WB_SAMPLING_RATE;			
            }
		}
		
		m_audioChannel = m_settingsPref.getString(KEY_AUDIO_CHANNEL,"MONO");
		m_videoEncoder = m_settingsPref.getString(KEY_VIDEO_CODECS,MEDIA_MIMETYPE_VIDEO_AVC);
		//TBD: Only H264 is supported now, so hardcoding it
		m_videoEncoder = MEDIA_MIMETYPE_VIDEO_AVC;
	    
	    /* TBD: Video resolution needs to be made dynamic. Remove the following code and uncomment 
	    code in updateDynamicSettings() once implemented */
/* Dynamic config hack start */	    
		String prefValue = m_settingsPref.getString(KEY_RESOLUTION, M_DEFAULT_RESOLUTION);
		Log.i(TAG, "New resolution: "+prefValue);
		if (prefValue.equals(M_VGA) == true) {
		    m_width = 640;
		    m_height = 480;
		} else if (prefValue.equals(M_QVGA) == true) {
		    m_width = 320;
		    m_height = 240;
		} else if (prefValue.equals(M_QCIF) == true) {
		    m_width = 176;
		    m_height = 144;
		} else if (prefValue.equals(M_HD720) == true) {
		    m_width = 1280;
		    m_height = 720;
		}
		
		prefValue = m_settingsPref.getString(KEY_VIDEO_FPS, "30");
		m_frameRate = (new Integer(prefValue)).intValue();
		Log.i(TAG, "************ m_frameRate:"+m_frameRate);
/* Hack End */

	    return OK;
	}
	
	public int updateDynamicSettings() {
	    /* Video resolution */

/* Dynamic config hack start */	    
/*		String prefValue = m_settingsPref.getString(KEY_RESOLUTION, M_DEFAULT_RESOLUTION);
		if (prefValue == M_VGA) {
		    m_width = 640;
		    m_height = 480;
		} else if (prefValue == M_QVGA) {
		    m_width = 320;
		    m_height = 240;
		} else if (prefValue == M_QCIF) {
		    m_width = 176;
		    m_height = 144;
		} else if (prefValue.equals(M_HD720) == true) {
		    m_width = 1280;
		    m_height = 720;
		}

		m_frameRate = m_settingsPref.getInt(KEY_VIDEO_FPS, M_FPS_DEFAULT); */
/* Dynamic config hack End */	    
		
		m_videoQuality = m_settingsPref.getInt(KEY_VIDEO_QUALITY, M_QUALITY_DEFAULT_INDEX);
	    return OK;
	}

	
	public int startCall () {
	    if (m_remoteIP == null || m_remoteIP.length() == 0) {
	        Log.i(TAG, "Invalid IP !!!!");
	        return K_INAVLID_REMOTE_IP;
	    }
	        
	    if (m_remotePort == 0 || m_localPort == 0) {
	        Log.i(TAG, "Invalid port details !!!!");
	        return K_INAVLID_PORT;
	    }

		if(m_enableAudio && !m_enableAudioViaCss &&  !m_audioEncoder.equalsIgnoreCase(MEDIA_MIMETYPE_AUDIO_AMR_NB) ) {
			Log.i(TAG,"Invalid Audio Codecs Details");
			return K_INVALID_AUDIO_CODES;
			
		}
	    return startAVHosts(m_localPort, m_remotePort, m_remoteIP);
	}
	
	public int stopCall () {
	    Log.i(TAG, "stopCall");
	    return stopAVHosts();
	}
	
	/* This is called whenever preferences are modified */
	public int updateRemoteCallerSettings (){
    	m_remoteIP = m_settingsPref.getString(KEY_REMOTE_IP, Settings.DEFAULT_IP);
	    String remoteport = m_settingsPref.getString(KEY_REMOTE_PORT, Settings.DEFAULT_PORT);
	    m_remotePort = Integer.parseInt(remoteport);	    

	    String localport = m_settingsPref.getString(KEY_LOCAL_PORT, Settings.DEFAULT_PORT);
	    m_localPort = Integer.parseInt(localport);
	    //TBD: other values also need to ba added
	    return OK ;
	}
	
    /* API's to configure params @ run time */
	public int configFrameRate (int frameRate, boolean isInCall) {
	    Log.i(TAG, "configFrameRate");
	    m_frameRate = frameRate;	    
	    if (isInCall == true) {
	        return configFrameRateInfo (m_frameRate);
	    }
	    else
	        return OK;
	}

	public int configResolution (int width, int height, boolean isInCall) {
	    m_width = width ;
	    m_height = height ;
	    if (isInCall == true)
	        return configResolutionInfo (width, height) ;
	    else
	        return OK ;
	}

	public int configQuality (int qualityrate, boolean isInCall) {
	    m_videoQuality = qualityrate;
	    if (isInCall)
	        configQualityInfo(m_videoQuality);
	    return OK;
	}
	
	public void geneateIFrameRate(boolean isInCall) {
	    if (isInCall == true)
            requestIFrame();
	}

	
	private String getLocalIpAddress() {
        try {
            for (Enumeration<NetworkInterface> en = NetworkInterface.getNetworkInterfaces(); en.hasMoreElements();) {
                NetworkInterface intf = en.nextElement();
                for (Enumeration<InetAddress> enumIpAddr = intf.getInetAddresses(); enumIpAddr.hasMoreElements();) {
                    InetAddress inetAddress = enumIpAddr.nextElement();
                    if (!inetAddress.isLoopbackAddress()) {
                        return inetAddress.getHostAddress().toString();
                    }
                }
            }
        } catch (Exception ex) {
            Log.e(TAG, ex.toString());
            Log.e(TAG, "Failed to get the device IP !!!!");
        }
        return null;
    }
	
	private native int requestIFrame ();
	private native int configFrameRateInfo (int frameRate);
	private native int configResolutionInfo (int width, int height);
	private native int configQualityInfo (int videoquality);
	/*****/

	private native final void native_setup(Object caller_this);

	private native final void native_release();

	private native final int startAVHosts(int localport, int remoteport, String remoteIP);

	private native final int stopAVHosts();

	public native final void setVideoDisplay(Surface surface);

	public native final void setPreviewDisplay(Surface surface);


	public native final void toggleVideoDisplay(Surface camerasurface, Surface remotevideosurface);
	
	public native final void getEncoderInfo(EncoderInfo encoderInfo);
}
