package com.p2p.app;

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
import android.widget.Toast;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;
import android.preference.EditTextPreference;
import android.preference.CheckBoxPreference;
import android.preference.ListPreference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.preference.Preference;
import android.preference.PreferenceScreen;
import android.content.Intent;


public class Settings extends PreferenceActivity implements AppConstants,OnPreferenceChangeListener{

    public static final String TAG = "P2PSettingApplication" ;

    /* Default values */
    public static final String DEFAULT_PORT = "5000";
    public static final String DEFAULT_IP = "127.0.0.1";
    
    private EditTextPreference mRemoteIP;
    private EditTextPreference mLocalPort;
    private EditTextPreference mRemotePort;
    private CheckBoxPreference mEnableAudio;
    private CheckBoxPreference mEnableAudioViaCss;
    private CheckBoxPreference mEnableVideo;
    private CheckBoxPreference mEnableLog;
    private CheckBoxPreference mEnableFullScreen;
    private ListPreference mAudioEncoder;
    private ListPreference mAudioChannel;
    private ListPreference mVideoEncoder;
    private ListPreference mVideoResolution;
    private SharedPreferences settingPreference;


    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
		
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.preferences);
        
        PreferenceManager.setDefaultValues(Settings.this, R.xml.preferences, false);
        settingPreference = this.getSharedPreferences(SHARED_PREF_FILE, MODE_PRIVATE);
        PreferenceScreen prefSet = getPreferenceScreen();
        
        mRemoteIP = (EditTextPreference)prefSet.findPreference(KEY_REMOTE_IP);
        mRemoteIP.setOnPreferenceChangeListener(this);

        mLocalPort = (EditTextPreference)prefSet.findPreference(KEY_LOCAL_PORT);
        mLocalPort.setOnPreferenceChangeListener(this);
        
        mRemotePort = (EditTextPreference)prefSet.findPreference(KEY_REMOTE_PORT);
        mRemotePort.setOnPreferenceChangeListener(this);

        mEnableAudio = (CheckBoxPreference)prefSet.findPreference(KEY_ENABLE_AUDIO);
       
        mEnableAudioViaCss = (CheckBoxPreference)prefSet.findPreference(KEY_ENABLE_AUDIO_VIA_CSS);
       
        
        mEnableVideo = (CheckBoxPreference)prefSet.findPreference(KEY_ENABLE_VIDEO);
      
        
        mEnableLog = (CheckBoxPreference)prefSet.findPreference(KEY_ENABLE_LOGGING);
        
        mEnableFullScreen = (CheckBoxPreference)prefSet.findPreference(KEY_FULL_SCREEN);
       
        mAudioEncoder = (ListPreference)prefSet.findPreference(KEY_AUDIO_CODECS);
        mAudioEncoder.setOnPreferenceChangeListener(this);

     /*   mAudioChannel = (ListPreference)prefSet.findPreference(KEY_AUDIO_CHANNEL);
		  mAudioChannel.setOnPreferenceChangeListener(this);
	 */

        mVideoEncoder = (ListPreference)prefSet.findPreference(KEY_VIDEO_CODECS);
        mVideoEncoder.setOnPreferenceChangeListener(this);
        
	    /* TBD: Video resolution needs to be made dynamic */
        mVideoResolution = (ListPreference)prefSet.findPreference(KEY_RESOLUTION);
        mVideoResolution.setOnPreferenceChangeListener(this);
    }

    protected void onResume() {
		super.onResume();
    }
    protected void onDestroy() {
        setResult(0);
		super.onDestroy();
    }
    public void onBackPressed(){
		/*Intent intent = new Intent();
		intent.setClassName("com.p2p.app","com.p2p.app.P2PConferenceApp");
		try {
			startActivity(intent);
		}
		catch(Exception e) {
			Log.d(TAG,"Not able to start application");
			e.printStackTrace();
		}*/
		super.onBackPressed();
		//finish();
	}
    public boolean onPreferenceChange(Preference preference, Object newValue) {
		
		SharedPreferences.Editor prefsEditor = settingPreference.edit();
		String preferenceValue = String.valueOf((String) newValue);
		Log.d(TAG,"Preference change value "+(String)preferenceValue);
		
		if(preference == mRemoteIP) {
		    if (preferenceValue.length() <= 7) {
				Toast.makeText(getApplicationContext(), "Enter valid IP !!!", Toast.LENGTH_SHORT).show();
				return false;
		    } else {
       	        prefsEditor.putString(KEY_REMOTE_IP,preferenceValue);
    	    }
		}
		else if(preference == mRemotePort) {
		    if (preferenceValue.length() < 4) {
				Toast.makeText(getApplicationContext(), "Enter valid port !!!", Toast.LENGTH_SHORT).show();
				return false;
		    } else {
        	    int remotePort = Integer.parseInt(preferenceValue);
        	    if ((remotePort%2) != 0){
    				Toast.makeText(getApplicationContext(), "Port number can not be odd number !!!", Toast.LENGTH_SHORT).show();
    				return false;
        	    }else{
        	        prefsEditor.putString(KEY_REMOTE_PORT,preferenceValue);
        	    }
    	    }
		}
		else if(preference == mLocalPort) {
		    if (preferenceValue.length() < 4) {
				Toast.makeText(getApplicationContext(), "Enter valid port !!!", Toast.LENGTH_SHORT).show();
				return false;
		    } else {
        	    int localPort = Integer.parseInt(preferenceValue);
        	    if ((localPort%2) != 0){
    				Toast.makeText(getApplicationContext(), "Port number can not be odd number !!!", Toast.LENGTH_SHORT).show();
    				return false;
        	    }else{
        	        prefsEditor.putString(KEY_LOCAL_PORT,preferenceValue);
        	    }
    	    }
		}
		else if(preference == mAudioEncoder) {
			prefsEditor.putString(KEY_AUDIO_CODECS,preferenceValue);
		}
		else if(preference == mAudioChannel) {
			prefsEditor.putString(KEY_AUDIO_CHANNEL,preferenceValue);
		}
		else if(preference == mVideoEncoder) {
			prefsEditor.putString(KEY_VIDEO_CODECS,preferenceValue);
		} else if (preference == mVideoResolution) {
    	    /* TBD: Video resolution needs to be made dynamic */
		    prefsEditor.putString(KEY_RESOLUTION,preferenceValue);
		}
		
		prefsEditor.commit();
		return true;
	}
	@Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
		
		SharedPreferences.Editor prefsEditor = settingPreference.edit();
		Log.d(TAG,"onPreferenceTreeClick function called");
		if(preference == mEnableAudio) {
			if (mEnableAudio.isChecked()) {
				prefsEditor.putBoolean(KEY_ENABLE_AUDIO,true);
			}
			else {
				prefsEditor.putBoolean(KEY_ENABLE_AUDIO,false);				
			}
		}
		else if(preference == mEnableAudioViaCss) {
			if (mEnableAudioViaCss.isChecked()) {
				prefsEditor.putBoolean(KEY_ENABLE_AUDIO_VIA_CSS,true);				
			}
			else {
				prefsEditor.putBoolean(KEY_ENABLE_AUDIO_VIA_CSS,false);				
			}
		}
		else if(preference == mEnableVideo) {
			if (mEnableVideo.isChecked()) {
				prefsEditor.putBoolean(KEY_ENABLE_VIDEO,true);				
			}
			else {
				prefsEditor.putBoolean(KEY_ENABLE_VIDEO,false);				
			}
		}
		else if(preference == mEnableLog) {
			if (mEnableLog.isChecked()) {
				prefsEditor.putBoolean(KEY_ENABLE_LOGGING,true);				
			}
			else {
				prefsEditor.putBoolean(KEY_ENABLE_LOGGING,false);				
			}
		}
		else if(preference == mEnableFullScreen) {
			if (mEnableFullScreen.isChecked()) {
				prefsEditor.putBoolean(KEY_FULL_SCREEN,true);
			}
			else {
				prefsEditor.putBoolean(KEY_FULL_SCREEN,false);
			}
		}
		return true;
	}
}
