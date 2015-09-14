package com.p2p.app;

import android.app.Application;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.util.Log;

public class P2PConferenceApplication extends Application implements AppConstants {
	
	private NativeCodeCaller nativeCode;
	private static P2PConferenceApplication sMe;
	private SharedPreferences m_settingsPref ;
	
	NativeCodeCaller getNativeCodeCaller() {
        return nativeCode;
    }
    public P2PConferenceApplication() {
		sMe = this;
	}
	public static P2PConferenceApplication getInstance() {
		return sMe;
	}

	@Override
    public void onCreate() {
		Log.v("P2PConferenceApplication", "onCreate()...");
        PreferenceManager.setDefaultValues(this, R.xml.preferences, false);
		m_settingsPref = PreferenceManager.getDefaultSharedPreferences(this);
		nativeCode = new NativeCodeCaller(m_settingsPref);
		if (nativeCode.init() != OK)
	        Log.d("P2PConferenceApplication", "Failed to initialise NativeCodeCaller instance !!!!!!");
	}
}
