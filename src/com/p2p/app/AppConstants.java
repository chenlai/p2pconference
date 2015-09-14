package com.p2p.app;

import java.io.InputStream;
import java.net.DatagramSocket;
import java.net.InetAddress;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;



interface AppConstants {

    /* Error types */
    public static final int OK = 0;
    
    public static final int K_ERROR=-1000;
    public static final int K_INAVLID_REMOTE_IP = K_ERROR-1;
    public static final int K_INAVLID_PORT = K_ERROR-2;
    public static final int K_INVALID_AUDIO_CODES = K_ERROR-3;
    
    public static final long NB_SAMPLING_RATE = 80000;
    public static final long WB_SAMPLING_RATE = 160000;
    
    /* Video mime types: Taken from frameworks/base/media/libstagefright/MediaDefs.cpp */
    public static final String MEDIA_MIMETYPE_VIDEO_AVC="video/avc";
    public static final String MEDIA_MIMETYPE_VIDEO_MPEG4="video/mp4v-es";
    public static final String MEDIA_MIMETYPE_VIDEO_H263="video/3gpp";

    /* Audio mime types: Taken from frameworks/base/media/libstagefright/MediaDefs.cpp */
    public static final String MEDIA_MIMETYPE_AUDIO_AMR_NB="audio/3gpp";
    public static final String MEDIA_MIMETYPE_AUDIO_G711U="audio/pcmu"; //0
    public static final String MEDIA_MIMETYPE_AUDIO_G711A="audio/pcma"; // 8
    public static final String MEDIA_MIMETYPE_AUDIO_G722="audio/g722"; // 9
    public static final String MEDIA_MIMETYPE_AUDIO_G729="audio/g729"; // 18
   
    /* Resolutions supported: Do not change this without changing pref_resolution_entries array in arrays.xml */
    public static final String M_VGA = "VGA";
    public static final String M_QVGA = "QVGA";
    public static final String M_QCIF = "QCIF";
    public static final String M_HD720 = "720p";
    public static final String M_DEFAULT_RESOLUTION = "QVGA";
    
    public static final int M_FPS_15 = 15;
    public static final int M_FPS_30 = 30;
    public static final int M_FPS_DEFAULT = 30;

    
    public static final String M_QUALITY_HIGH = "High";
    public static final String M_QUALITY_MEDIUM = "Meduim";
    public static final String M_QUALITY_LOW = "Low";
    public static final int M_QUALITY_DEFAULT_INDEX = 2;
        
    public static final String KEY_REMOTE_IP = "remote_ip";
    public static final String KEY_LOCAL_PORT = "local_port";
    public static final String KEY_REMOTE_PORT = "remote_port";
    public static final String KEY_ENABLE_VIDEO = "enable_video";
    public static final String KEY_ENABLE_AUDIO = "enable_audio";
    public static final String KEY_ENABLE_AUDIO_VIA_CSS = "enable_audio_via_css";
    public static final String KEY_AUDIO_CODECS = "audio_codecs";
    public static final String KEY_AUDIO_CHANNEL = "audio_channel";
    public static final String KEY_VIDEO_CODECS = "video_codecs";
    public static final String KEY_ENABLE_LOGGING = "enable_logging";
    public static final String KEY_RESOLUTION = "video_resolution";
    public static final String KEY_VIDEO_FPS = "video_fps";
    public static final String KEY_VIDEO_QUALITY = "video_quality";
    public static final String KEY_FULL_SCREEN = "enable_full_screen";
    
    
    // Path where is stored the shared preference file - !!!should be replaced by some system variable!!!
    public final String sharedPrefsPath = "/data/data/com.p2p.app/shared_prefs/";
    // Shared preference file name - !!!should be replaced by some system variable!!!
    public final String SHARED_PREF_FILE = "com.p2p.app.preferences";
    
}
