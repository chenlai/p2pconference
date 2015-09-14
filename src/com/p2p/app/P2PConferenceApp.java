package com.p2p.app;

import java.io.InputStream;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.lang.Integer;

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
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

/* For Awesome player */
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.media.MediaPlayer.OnBufferingUpdateListener;
import android.media.MediaPlayer.OnCompletionListener;
import android.media.MediaPlayer.OnPreparedListener;
import android.media.MediaPlayer.OnVideoSizeChangedListener;
import 	android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.res.Resources;
import android.widget.Toast;
import android.view.LayoutInflater;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import com.p2p.app.Switcher;
import com.p2p.app.ShutterButton;
import android.view.ViewGroup;
import android.view.Display;
import android.view.WindowManager;
import android.os.PowerManager;
import android.content.Context;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.net.NetworkInterface;
import java.net.InetAddress;
import java.util.Enumeration;
import android.os.Handler;
import com.p2p.app.VideoSurfaceView;
import android.graphics.Canvas;
import android.widget.TextView;
import com.p2p.app.P2PController;
import android.view.MotionEvent;
import android.view.Window;
import android.content.pm.ActivityInfo;
import com.p2p.app.P2PConferenceApplication;

public class P2PConferenceApp extends Activity implements AppConstants,
	Switcher.OnSwitchListener,
	View.OnClickListener,P2PController.P2PConfControl {
		
	enum e_full_screen_video_type {E_FULL_SCREEN_CAMERA_PREVIEW,	E_FULL_SCREEN_REMOTE_VIDEO}

	public static final String TAG = "P2PConferenceApp" ;
	public static final int BUFFER_SIZE = 1024;
	private static final int TIME_INTERVAL = 3;

	public static final int KEY_MENU_SETTINGS =0;
	public static final int KEY_MENU_RESOLUTION = 1;
	public static final int KEY_MENU_FRAME_RATE = 2;
	public static final int KEY_MENU_VIDEO_QUALITY =3;
	public static final int KEY_MENU_I_FRAME_RATE = 4;
	
	private static final int MACRO_BLOCK_SIZE = 16*16;
	
	private VideoSurfaceView preview;
	private SurfaceHolder previewHolder = null;
	private VideoSurfaceView video;
	private SurfaceHolder videoHolder = null;
	
	private static final boolean END_CALL = true;
	private static boolean mPausing = false;

	private ImageButton btnCallStart, btnCallEnd, btnVideoToggle;
	private surfaceCallback videoCallback = new surfaceCallback();
	private mediaPlayerCallback mediaCallback = new mediaPlayerCallback();
	private NativeCodeCaller nativeCode;// = new NativeCodeCaller();
	private boolean m_IsinCall ;
	private SharedPreferences m_settingsPref ;
	private e_full_screen_video_type m_CurrFullScreenVideo;
	
	private ImageView mSettingButton;
	private ImageView startCallButton;
	private ImageView endCallButton;
	
	private MediaPlayer mMediaPlayer;
	private Switcher mSwitcher;
	private ImageView mShutterButton;
	
	private static PowerManager powermanager;
	private static PowerManager.WakeLock wakeuplock;
	private boolean mWakeupLockAcquire = false;
	private Handler mHandler = new Handler();
	private boolean mShowingEncoderInfo =  false;

	private LinearLayout mEncoderInfoLayout;
	private TextView mEncoderInfo;
	private P2PController mP2PController;
	private boolean mShowingFullScreen = false;
	private boolean mEnableVideo = true;

	private Runnable mUpdateEndoerConfiguration = new Runnable()
	{
		public void run()
		{
			EncoderInfo encodeInfo = new EncoderInfo();
			long current = System.currentTimeMillis();
			nativeCode.getEncoderInfo(encodeInfo);
			long back = System.currentTimeMillis();
			String resolution = "N/A";
			if (m_settingsPref != null) {
				resolution = m_settingsPref.getString(KEY_RESOLUTION, "N/A");	
			}
			long macroBlock = getNumberofMacroBlock(resolution);
			double errorRate = ((encodeInfo.decoderCorruptedMarcoBlock * 100) /macroBlock);
			Log.d(TAG,"time taken to read value:"+(back-current) + " and error rate" + errorRate);
			Log.d(TAG,"frame Rate: "+ encodeInfo.frameRate + "  bitRate: "+ encodeInfo.bitRate + " colorFormat: "+ encodeInfo.colorFormat + " iFrameInterval: "+ encodeInfo.iFrameInterval + " IDR Period-" + encodeInfo.IDRPeriod + " pFrameInterval-"+ encodeInfo.pFrameInterval);
			SurfaceHolder surfaceHolder = video.getHolder();
			String encoderInfo = "Encoder Frame Rate: "      +(encodeInfo.frameRate) + " FPS / " + (encodeInfo.encoderFrameRate/TIME_INTERVAL) + " FPS,\n" +
								 "\nBit Rate: "      + (encodeInfo.bitRate/(1000)) + " Kb / " + ((encodeInfo.encoderBitRate *8)/(1000*TIME_INTERVAL)) +" Kb,\n" +
								 "\nResolution : "+resolution+
								 "\niFrameInterval: "+encodeInfo.iFrameInterval + " / " + (encodeInfo.encoderiFrameInterval/1000) +" second" +
								 "\nPFrame : "+encodeInfo.encoderPFrameCounter + " Frame" +
								 
								 "\n\nDecoder Frame Rate: " + (encodeInfo.decoderFrameRate/TIME_INTERVAL) +
								 "\nMacro Block Error Rate: " + errorRate+ "%" +"(" + encodeInfo.decoderCorruptedFrameRate + "F, "+encodeInfo.decoderCorruptedMarcoBlock+"M)" ;
			mEncoderInfo.setText(encoderInfo);
			mHandler.postDelayed(this,(TIME_INTERVAL*1000));
		}
	};
	private long getNumberofMacroBlock(String resolution) {

		if(resolution.equalsIgnoreCase("VGA")) {
			return ((640*480)/MACRO_BLOCK_SIZE);
		}
		else if(resolution.equalsIgnoreCase("QVGA")) {
			return ((320*240)/MACRO_BLOCK_SIZE);
		}
		else if(resolution.equalsIgnoreCase("QCIF")) {
			return ((176*144)/MACRO_BLOCK_SIZE);
		}
		return -1;
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

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        powermanager = (PowerManager) getSystemService(Context.POWER_SERVICE);
		wakeuplock = powermanager.newWakeLock(PowerManager.FULL_WAKE_LOCK | PowerManager.ON_AFTER_RELEASE, "P2PApplication");

		String mLocalIP = getLocalIpAddress();
		
	    m_settingsPref = PreferenceManager.getDefaultSharedPreferences(this);
	    if (m_settingsPref == null)
                Log.e(TAG, "Invalid SharedPreferences instance!!!");
   
	    nativeCode = P2PConferenceApplication.getInstance().getNativeCodeCaller();

		if(mLocalIP == null || mLocalIP.equalsIgnoreCase("127.0.0.1")) {
			Log.d("p2p","ethernet connection down");
			AlertDialog alertDialog = new AlertDialog.Builder(this).create();
			alertDialog.setTitle("P2P Application");
			alertDialog.setMessage("Ethernet/Wifi Connection is not present");
			alertDialog.setCancelable(false);
			alertDialog.setButton(AlertDialog.BUTTON_POSITIVE, "exit",new DialogInterface.OnClickListener()
				{
					public void onClick(DialogInterface dialog, int which)
					{
						dialog.dismiss();
						finish();
					}
				});
			alertDialog.show();
			return;
		}

    }
    @Override
    public void onStart()
    {
		super.onStart();
		Log.d(TAG," Full Screen Option"+m_settingsPref.getBoolean(KEY_FULL_SCREEN,false));
        if(m_settingsPref != null && m_settingsPref.getBoolean(KEY_FULL_SCREEN,false)) {
            showVideoInFullScreen();
        }
        else {
			showVideoInScreen();
		}
	}
    private int getorientation()
    {
		Display display = getWindowManager().getDefaultDisplay();
		if(display.getWidth() < display.getHeight()){
			Log.d(TAG,"portrait mode ");
			return ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
		}else{ // if it is not any of the above it will defineitly be landscape
			Log.d(TAG,"landscape mode");
			return ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
		}
	}

    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
		if(mP2PController != null && !mP2PController.isShowing() && mShowingFullScreen) {
			mP2PController.show();
		}
        return super.onTouchEvent(event);
    }

    private void showVideoInFullScreen(){

        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setContentView(R.layout.main_with_full_screen);
        showVideoCommonView();
        mShowingFullScreen= true;

		LayoutInflater inflater = getLayoutInflater();
        View rootView = (View) findViewById(R.id.anchor_view);

        mP2PController = new P2PController(this,mEnableVideo);
        mP2PController.setAnchorView(rootView);
        mP2PController.setP2PConfPlayer(this);
        mP2PController.setOrienation(getorientation());

	}

	private void showVideoInScreen()
	{
		getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		setContentView(R.layout.main_without_full_screen);
		showVideoCommonView();
		mShowingFullScreen=false;

		LayoutInflater inflater = getLayoutInflater();
        ViewGroup rootView = (ViewGroup) findViewById(R.id.p2papplication);

        View view = LayoutInflater.from(getBaseContext()).inflate(R.layout.p2p_control,null);
        if(view != null) {
			rootView.addView(view);
		}

        startCallButton = (ImageView) findViewById(R.id.call_icon);
        startCallButton.setOnClickListener(this);

        endCallButton = (ImageView) findViewById(R.id.end_call_icon);
        endCallButton.setOnClickListener(this);


        mShutterButton = (ImageView) findViewById(R.id.call_toggle);
        mShutterButton.setOnClickListener(this);
        mShutterButton.setVisibility(View.VISIBLE);

        mSettingButton = (ImageView) findViewById(R.id.p2p_setting);
        mSettingButton.setOnClickListener(this);
        mSettingButton.setImageResource(R.drawable.settings);

        populateSettingsList ();
	}

	private void showVideoCommonView() {

		mEncoderInfoLayout = (LinearLayout)this.findViewById(R.id.encoder_info_panel);
		mEncoderInfo = (TextView)this.findViewById(R.id.encoder_info);
		mEncoderInfoLayout.setVisibility(View.GONE);

        // Wire up SurfaceViews
	    preview = (VideoSurfaceView) findViewById(R.id.preview);
	    if (preview != null) {
	        preview.setZOrderMediaOverlay(false);
		    previewHolder = preview.getHolder();
		    previewHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
		    previewHolder.addCallback(videoCallback);
	    }

	    video = (VideoSurfaceView) findViewById(R.id.video);
	    if (video != null) {
		    videoHolder = video.getHolder();
		    videoHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
		    videoHolder.addCallback(videoCallback);
	    }

	    if(m_settingsPref != null && !m_settingsPref.getBoolean(KEY_ENABLE_VIDEO,true)) {
			mEnableVideo = false;
        }
        else {
			mEnableVideo = true;
		}
	}
    
    public void populateSettingsList () {
        ListView settingsList = (ListView)findViewById(R.id.settings_list);
        if (settingsList != null) {
            String[] settingsEntries = getResources().getStringArray(R.array.p2p_setting_entries);
            ArrayAdapter<String>adapter = new ArrayAdapter<String>(this,
                            android.R.layout.simple_list_item_1, settingsEntries);
            	
            settingsList.setAdapter(adapter);
			settingsList.setEnabled(mEnableVideo);
		    settingsList.setOnItemClickListener(new ListView.OnItemClickListener() {
					public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
						Log.i(TAG, "onItemClick position:"+position+" id:"+id);
						switch (position){
						    case 0:{ //high-quality
						        nativeCode.configQuality (0, m_IsinCall);
						    }break ;
						    case 1:{ //medium-quality
						        nativeCode.configQuality (1, m_IsinCall);
						    }break ;
						    case 2:{ //low-quality
						        nativeCode.configQuality (2, m_IsinCall);
						    }break ;
						    case 3:{ //generate i-frame
						        nativeCode.geneateIFrameRate(m_IsinCall);
						    }break ;
						    case 4: { // get encode info
								if(m_IsinCall && !mShowingEncoderInfo) {
									mHandler.postDelayed(mUpdateEndoerConfiguration,5);
									mShowingEncoderInfo = true;
									mEncoderInfoLayout.setVisibility(View.VISIBLE);
								}
								else if(mShowingEncoderInfo) {
									mHandler.removeCallbacks(mUpdateEndoerConfiguration);
									mShowingEncoderInfo = false;
									mEncoderInfoLayout.setVisibility(View.GONE);
								}
								break;
							}
						    default:{
						        Log.e(TAG, "Invalid list index");
						    }
						    
						}
					}
				});
        } else {
            Log.e(TAG, "ListView creation failed");
        }        
    }
    
    
    public void onClick(View v) {
        switch (v.getId()) {
			case R.id.p2p_setting:
				Log.d(TAG,"Show Menu");
				//TBD: Floating view needs to be implemented
				//showConfiguration();
                break;
            case R.id.call_icon:
				Log.d(TAG,"Start Call");
				startCall();
				break;
            case R.id.end_call_icon:
				Log.d(TAG,"End call");
				endCall();
				break;
            case R.id.call_toggle:
                ToggleVideo();
                break;
		}
		return;	
	}

    public boolean onSwitchChanged(Switcher source, boolean onOff) {
		Log.d(TAG,"Swich changed value " + onOff);
        if (onOff == END_CALL) {
            endCall();
        } else {
            startCall();
        }
        return true;
    }
    
    public void onBackPressed (){
        Log.i(TAG, "onBackPressed..");
	    endCall();
	    super.onBackPressed();
	    finish();
	    return ;
    }
    
    protected void onDestroy() {
	    super.onDestroy();
    }
    protected void onResume() {
	    super.onResume();
	    if(wakeuplock != null && !mWakeupLockAcquire) {
			wakeuplock.acquire();
			mWakeupLockAcquire = true;
		}
	    mPausing = false;
    }
    
    protected void onStop() {
	    super.onStop();
	    mPausing = true;
	    endCall();
	    Log.d(TAG,"Pause Cakked wakeuplock.release");
		if(wakeuplock != null && mWakeupLockAcquire)
		{
			wakeuplock.release();
			mWakeupLockAcquire = false;
		}
    }

    public boolean onCreateOptionsMenu (Menu menu) {
    	    menu.add (0, KEY_MENU_SETTINGS, Menu.NONE, R.string.settings);
    	    return super.onCreateOptionsMenu (menu);
    }
    
    
    public boolean onPrepareOptionsMenu (Menu menu) {
    if (m_IsinCall == false) {    	    
    	    Log.i(TAG, "Adding menu1");
    	    return super.onPrepareOptionsMenu (menu);
    	} else {
    	    Log.i(TAG, "Discarding menu1");
    	    return false ;
    	}
    }
    
    private void showConfiguration() {
		final String videoOption[] = getResources().getStringArray(R.array.configuration_option);
		
		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setTitle("Setting Option");
		builder.setItems(videoOption, new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int item) 
			{
				dialog.dismiss();
				switch(item) {
					case 0:
						showResolution();
						break;
					case 1:
						showFrameRate();
						break;
					case 2:
						showVideoQuality();
						break;
					case 3:
						nativeCode.geneateIFrameRate(m_IsinCall);
						break;
				}
			}
		});
		builder.setPositiveButton("Cancel",new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int which)
			{
				dialog.dismiss();
			}
		});
		AlertDialog alertDialog = builder.create();
		alertDialog.show();		
	}

	private void showResolution() {
	    int resIndex = 0;
		final String videoOption[] = getResources().getStringArray(R.array.pref_resolution_entries);
		
		String prefValue = m_settingsPref.getString(KEY_RESOLUTION, M_DEFAULT_RESOLUTION);
		for (; resIndex < videoOption.length; resIndex++) {
		    if (videoOption[resIndex].equals(prefValue) == true){
    		    Log.i(TAG, "Current resolution:"+videoOption[resIndex]);
		        break ;
		    }
		}

		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setTitle("Video resolution");
		builder.setSingleChoiceItems(videoOption, resIndex, new DialogInterface.OnClickListener() 
		
		{
			public void onClick(DialogInterface dialog, int item) {
				dialog.dismiss();

        		String videoResolution[] = getResources().getStringArray(R.array.pref_resolution_entries);
		        SharedPreferences.Editor prefsEditor = m_settingsPref.edit();
				if (videoResolution[item].equals(M_VGA) == true) {
        			prefsEditor.putString(KEY_RESOLUTION, M_VGA);
				    nativeCode.configResolution (640, 480, m_IsinCall);
				} else if (videoResolution[item].equals(M_QVGA) == true) {
        			prefsEditor.putString(KEY_RESOLUTION, M_QVGA);
				    nativeCode.configResolution (320, 240, m_IsinCall);
				} else if (videoResolution[item].equals(M_QCIF) == true) {
        			prefsEditor.putString(KEY_RESOLUTION, M_QCIF);
				    nativeCode.configResolution (176, 144, m_IsinCall);
    			} else if (videoResolution[item].equals(M_HD720) == true) {
        			prefsEditor.putString(KEY_RESOLUTION, M_HD720);
				    nativeCode.configResolution (1280, 720, m_IsinCall);
				}
        		prefsEditor.commit();
				Toast.makeText(getApplicationContext(), videoOption[item], Toast.LENGTH_SHORT).show();
			}
		});

		builder.setPositiveButton("Cancel",new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int which)
			{
				dialog.dismiss();
			}
		});
		AlertDialog alertDialog = builder.create();
		alertDialog.show();
	}
	private void showFrameRate()
	{
		String videoOption[] = getResources().getStringArray(R.array.pref_frame_rate_entries);
		int prefValue = m_settingsPref.getInt(KEY_VIDEO_FPS, M_FPS_DEFAULT);
		int resIndex=0;
		String currfps = new Integer (prefValue).toString();
		for (; resIndex < videoOption.length; resIndex++) {
		    if (videoOption[resIndex].equals(currfps) == true){
    		    Log.i(TAG, "Current fps:"+videoOption[resIndex]);
		        break ;
		    }
		}

		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setTitle("Select Frame Rate");
		builder.setSingleChoiceItems(videoOption, resIndex, new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int item) 
			{
        		String videoFPS[] = getResources().getStringArray(R.array.pref_frame_rate_entries);
        		int selectedFps = (new Integer(videoFPS[item])).intValue();
				dialog.dismiss();
		        SharedPreferences.Editor prefsEditor = m_settingsPref.edit();
		        prefsEditor.putInt(KEY_VIDEO_FPS, selectedFps);
        		prefsEditor.commit();
				Toast.makeText(getApplicationContext(), videoFPS[item], Toast.LENGTH_SHORT).show();
				nativeCode.configFrameRate (selectedFps, m_IsinCall);
				return ;
			}
		});
		builder.setPositiveButton("Cancel",new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int which)
			{
				dialog.dismiss();
			}		
		});
		AlertDialog alertDialog = builder.create();
		alertDialog.show();
	}
	private void showVideoQuality()
	{
		String  videoOption[] = getResources().getStringArray(R.array.pref_video_quality_entries);
		int     qualityIndex = m_settingsPref.getInt(KEY_VIDEO_QUALITY, M_QUALITY_DEFAULT_INDEX);

		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setTitle("Video quality");
		builder.setSingleChoiceItems(videoOption, qualityIndex, new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int item) 
			{
        		String  videoOption[] = getResources().getStringArray(R.array.pref_video_quality_entries);
        		
				dialog.dismiss();
		        SharedPreferences.Editor prefsEditor = m_settingsPref.edit();
		        prefsEditor.putInt(KEY_VIDEO_QUALITY, item);
        		prefsEditor.commit();
        		nativeCode.configQuality(item, m_IsinCall);
				Toast.makeText(getApplicationContext(), videoOption[item], Toast.LENGTH_SHORT).show();
			}
		});
		builder.setPositiveButton("Cancel",new DialogInterface.OnClickListener() 
		{
			public void onClick(DialogInterface dialog, int which)
			{
				dialog.dismiss();
			}
		});
		AlertDialog alertDialog = builder.create();
		alertDialog.show();
	}

    public boolean onOptionsItemSelected (MenuItem item) {
        boolean result = super.onOptionsItemSelected(item);
        switch (item.getItemId()) 
        {
            case KEY_MENU_SETTINGS:
                    Intent intent = new Intent (this, Settings.class);
                    startActivityForResult (intent, 10);
                    break;
                     
			case KEY_MENU_RESOLUTION:
					showResolution();
					break;
					
			case KEY_MENU_FRAME_RATE:
					showFrameRate();
					break;
					
			case KEY_MENU_VIDEO_QUALITY:
					showVideoQuality();
					break;
					
			case KEY_MENU_I_FRAME_RATE:
					nativeCode.geneateIFrameRate(m_IsinCall);
					break;
        }
        return result;
    }
    
    protected void onActivityResult (int requestCode, int resultCode, Intent data){
        Log.d(TAG, "onActivityResult requestCode:"+requestCode+" resultCode:"+resultCode);
	    if (nativeCode.updateStaticSettings() != OK)
	        Log.d(TAG, "Failed to update settings !!!!!!");
    }

	public void ToggleVideo () {
		if (m_CurrFullScreenVideo == e_full_screen_video_type.E_FULL_SCREEN_REMOTE_VIDEO) {
			m_CurrFullScreenVideo = e_full_screen_video_type.E_FULL_SCREEN_CAMERA_PREVIEW;
			nativeCode.toggleVideoDisplay (videoHolder.getSurface(), previewHolder.getSurface());
		} else {
			m_CurrFullScreenVideo = e_full_screen_video_type.E_FULL_SCREEN_REMOTE_VIDEO;
			nativeCode.toggleVideoDisplay (previewHolder.getSurface(), videoHolder.getSurface());
		}
	}

	public void generateIFrame() {
		nativeCode.geneateIFrameRate(m_IsinCall);
	}
	public void showEncoderInformation() {
		if(m_IsinCall && !mShowingEncoderInfo) {
			mHandler.postDelayed(mUpdateEndoerConfiguration,5);
			mShowingEncoderInfo = true;
			mEncoderInfoLayout.setVisibility(View.VISIBLE);
		}
		else if(mShowingEncoderInfo) {
			mHandler.removeCallbacks(mUpdateEndoerConfiguration);
			mShowingEncoderInfo = false;
			mEncoderInfoLayout.setVisibility(View.GONE);
		}
	}
	public void setQuality(int pos) {
		nativeCode.configQuality (pos, m_IsinCall);
	}

    public void startCall() {
        Log.d (TAG, "startCall enter");
	    if (m_IsinCall == false) {
		    m_CurrFullScreenVideo = e_full_screen_video_type.E_FULL_SCREEN_REMOTE_VIDEO ;
		    /* start recording */
		    int retVal = nativeCode.startCall();
		    if (retVal == K_INAVLID_REMOTE_IP || retVal == K_INAVLID_PORT) {
                Toast.makeText(getApplicationContext(), "Enter port & IP details !!", Toast.LENGTH_SHORT).show();
            }
            else if(retVal == K_INVALID_AUDIO_CODES) {
				Toast.makeText(getApplicationContext(),"Invalid Audio codecs details !!",Toast.LENGTH_SHORT).show();
			}
            else if (retVal != OK) {
                Toast.makeText(getApplicationContext(), "Failed to initiate call !!", Toast.LENGTH_SHORT).show();
            }
            else {
                m_IsinCall = true ;
		
            }
	    }
    }

    public void endCall () {
        if (m_IsinCall == true) {
			mHandler.removeCallbacks(mUpdateEndoerConfiguration);
	        nativeCode.stopCall();
	        m_IsinCall = false ;	        
	    }
	    mEncoderInfoLayout.setVisibility(View.GONE);
    }

	// Used to catch surface creation, to pass handles down to JNI
	private class surfaceCallback implements SurfaceHolder.Callback {
		public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
			if (holder == previewHolder) {
				Log.e(TAG, "preview surfaceChanged: f: " + format + ", w: " + width + ", h: " + height);
				nativeCode.setPreviewDisplay(holder.getSurface());
			} else if (holder == videoHolder) {
				Log.e(TAG, "video surfaceChanged: f: " + format + ", w: " + width + ", h: " + height);
				nativeCode.setVideoDisplay(holder.getSurface());
			} else {
				Log.e(TAG, "Unknown surface: f: " + format + ", w: " + width + ", h: " + height);
				assert (false);
			}
		}

		public void surfaceCreated(SurfaceHolder holder) {
			Log.e(TAG, "surfaceCreated");
		}

		public void surfaceDestroyed(SurfaceHolder holder) {
			Log.e(TAG, "surfaceDestroyed");
		}
	}
	
	/* Awesome player changes */
	private class mediaPlayerCallback implements OnBufferingUpdateListener, OnCompletionListener,
        OnPreparedListener, OnVideoSizeChangedListener {
        public void onBufferingUpdate(MediaPlayer arg0, int percent) {
            Log.d(TAG, "onBufferingUpdate percent:" + percent);
        }

        public void onCompletion(MediaPlayer arg0) {
            Log.d(TAG, "onCompletion called");
        }

        public void onVideoSizeChanged(MediaPlayer mp, int width, int height) {
            Log.v(TAG, "onVideoSizeChanged called");
        }

        public void onPrepared(MediaPlayer mediaplayer) {
            Log.d(TAG, "onPrepared called");
            mMediaPlayer.start();
            /*mIsVideoReadyToBePlayed = true;
            if (mIsVideoReadyToBePlayed && mIsVideoSizeKnown) {
                startVideoPlayback();
            }*/
        }
    }
	
    /* Test code start: Mediaplayer creation */
	void startAwesomePlayer () {
	    try {
	        String path="rtsp://dspg/test";
            // Create a new media player and set the listeners
            mMediaPlayer = new MediaPlayer();
            mMediaPlayer.setDataSource(path);
            mMediaPlayer.setDisplay(videoHolder);
            mMediaPlayer.setAudioStreamType(AudioManager.STREAM_VOICE_CALL);
		    Log.i(TAG, "preparing awesome player");
            mMediaPlayer.prepareAsync();
            mMediaPlayer.setOnBufferingUpdateListener(mediaCallback);
            mMediaPlayer.setOnCompletionListener(mediaCallback);
            mMediaPlayer.setOnPreparedListener(mediaCallback);
            mMediaPlayer.setOnVideoSizeChangedListener(mediaCallback);
            //mMediaPlayer.start();
         } catch(Exception e) {
            Log.e(TAG, "error: " + e.getMessage(), e);
        }
    }
    
    void stopAwesomePlayer () {
		if (mMediaPlayer != null) {
            mMediaPlayer.release();
            mMediaPlayer = null;
        }
    }
    /* Test code end: Mediaplayer creation */

    /* Creating surface dynamicaly to test overlapped surface */    
    private RelativeLayout createUI () {
        RelativeLayout layout = new RelativeLayout (this);
        RelativeLayout.LayoutParams layoutparams = new RelativeLayout.LayoutParams (RelativeLayout.LayoutParams.WRAP_CONTENT, 
                                                                                    RelativeLayout.LayoutParams.WRAP_CONTENT);
        layoutparams.addRule (RelativeLayout.CENTER_HORIZONTAL);
        layoutparams.addRule (RelativeLayout.CENTER_VERTICAL);
        layout.setLayoutParams (layoutparams);
        
        video = new VideoSurfaceView (this);
        if (video != null) {
	        //video.setZOrderOnTop(true);
	        //video.setZOrderMediaOverlay(true);
	        //((View)video).setAlpha((float)50);
	        Log.d(TAG, "@@@@@@@@@@@@@@@@@@@@@@@@ setAlpha");
	        RelativeLayout.LayoutParams surfaceparams = new RelativeLayout.LayoutParams (620, 480);
            surfaceparams.addRule (RelativeLayout.CENTER_HORIZONTAL);
            surfaceparams.addRule (RelativeLayout.CENTER_VERTICAL);
            video.setLayoutParams (surfaceparams);

		    videoHolder = video.getHolder();
		    videoHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
		    videoHolder.addCallback(videoCallback);
		    layout.addView(video);
	    }

        preview = new VideoSurfaceView (this);
        if (preview != null) {
	        Log.d(TAG, "############### setZOrderMediaOverlay #####################");
	        RelativeLayout.LayoutParams surfaceparams = new RelativeLayout.LayoutParams (160, 120);
            surfaceparams.addRule (RelativeLayout.CENTER_HORIZONTAL);
            surfaceparams.addRule (RelativeLayout.CENTER_VERTICAL);
            preview.setLayoutParams (surfaceparams);

		    previewHolder = preview.getHolder();
		    previewHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
		    previewHolder.addCallback(videoCallback);
		    //layout.addView(preview);
		    addContentView (preview, surfaceparams);
	        //preview.setZOrderOnTop(true);
	        preview.setZOrderMediaOverlay(true);
	    }
	    
	    return layout ;
    }
}
