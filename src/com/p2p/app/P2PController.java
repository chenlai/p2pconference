package com.p2p.app;


import android.content.Context;
import android.graphics.PixelFormat;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Message;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.SeekBar.OnSeekBarChangeListener;
import com.android.internal.policy.PolicyManager;
import android.widget.ListView;
import java.util.Formatter;
import java.util.Locale;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ArrayAdapter;
import android.view.LayoutInflater;
import android.widget.AdapterView;
import android.content.pm.ActivityInfo;




public class P2PController extends FrameLayout {

    private P2PConferenceApp  mP2PConfPlayer;
    private static final String TAG = "P2PController";
    private Context             mContext;
    private View                mAnchor;
    private View                mRoot;
    private WindowManager       mWindowManager;
    private Window              mWindow;
    private View                mDecor;

    private boolean             mShowing;
    private static final int    sDefaultTimeout = 3000;
    private static final int    FADE_OUT = 1;
    private static final int    SHOW_PROGRESS = 2;

    private boolean             mFromXml;
    private boolean             mListenersSet;

	private ImageView mSettingButton;
	private ImageView startCallButton;
	private ImageView endCallButton;
	private ImageView mShutterButton;
	private ListView settingsList;
	private int mOrientation;
	private boolean mEnableVideo =true;

    public P2PController(Context context, AttributeSet attrs) {
        super(context, attrs);

        mRoot = this;
        mContext = context;
        mFromXml = true;
    }

    @Override
    public void onFinishInflate() {
        if (mRoot != null)
            initControllerView(mRoot);
    }

	public P2PController(Context context,boolean enableVideo)
	{
        super(context);
		this.mEnableVideo = enableVideo;
        mContext = context;
        initFloatingWindow();
    }

    private void initFloatingWindow()
    {
        mWindowManager = (WindowManager)mContext.getSystemService(Context.WINDOW_SERVICE);
        
        mWindow = PolicyManager.makeNewWindow(mContext);
        mWindow.setWindowManager(mWindowManager, null, null);
        mWindow.requestFeature(Window.FEATURE_NO_TITLE);
        
        mDecor = mWindow.getDecorView();
        mDecor.setOnTouchListener(mTouchListener);
        
        mWindow.setContentView(this);
        mWindow.setBackgroundDrawableResource(android.R.color.transparent);
        
        // While the media controller is up, the volume control keys should
        // affect the media stream type
        mWindow.setVolumeControlStream(AudioManager.STREAM_MUSIC);

        setFocusable(true);
        setFocusableInTouchMode(true);
        setDescendantFocusability(ViewGroup.FOCUS_AFTER_DESCENDANTS);
        requestFocus();
    }

    private OnTouchListener mTouchListener = new OnTouchListener()
    {
        public boolean onTouch(View v, MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                if (mShowing) {
                    hide();
                }
            }
            return false;
        }
    };
    
    public void setP2PConfPlayer(P2PConferenceApp player) {
        mP2PConfPlayer = player;
    }

    /**
     * Set the view that acts as the anchor for the control view.
     * This can for example be a VideoView, or your Activity's main view.
     * @param view The view to which to anchor the controller when it is visible.
     */
    public void setAnchorView(View view)
    {
        mAnchor = view;

        FrameLayout.LayoutParams frameParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        );

        removeAllViews();
        View v = makeControllerView();

        addView(v, frameParams);
    }
	public void setOrienation(int orientation) {

		mOrientation=orientation;
	}

    protected View makeControllerView()
    {

        LayoutInflater inflate = (LayoutInflater) mContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        mRoot = inflate.inflate(R.layout.p2p_controller, null);

        initControllerView(mRoot);
        return mRoot;
    }

    private void initControllerView(View view) 
    {
        mSettingButton = (ImageView) view.findViewById(R.id.p2p_setting);
        if (mSettingButton != null) {
            mSettingButton.requestFocus();
            mSettingButton.setOnClickListener(mSettingListener);
        }


		startCallButton = (ImageView) view.findViewById(R.id.call_icon);
        if (startCallButton != null) {
			startCallButton.requestFocus();
            startCallButton.setOnClickListener(mStartCallListener);
        }

        
        endCallButton = (ImageView) view.findViewById(R.id.end_call_icon);
        if (endCallButton != null) {
			endCallButton.requestFocus();
            endCallButton.setOnClickListener(mEndCallListener);
        }


		mShutterButton = (ImageView) view.findViewById(R.id.call_toggle);
		if(mShutterButton != null) {
			mShutterButton.setOnClickListener(mToggleListener);
			mShutterButton.requestFocus();
		}

		
		ListView settingsList = (ListView)view.findViewById(R.id.settings_list);
		if (settingsList != null) {
			String[] settingsEntries = mContext.getResources().getStringArray(R.array.p2p_setting_entries);
			ArrayAdapter<String> adapter = new ArrayAdapter<String>(mContext,android.R.layout.simple_list_item_1, settingsEntries);
			settingsList.setAdapter(adapter);
			settingsList.setEnabled(mEnableVideo);
			settingsList.setOnItemClickListener(mListViewListener);
		}

    }

    /**
     * Show the controller on screen. It will go away
     * automatically after 3 seconds of inactivity.
     */
    public void show() {
        show(sDefaultTimeout);
    }

    /**
     * Show the controller on screen. It will go away
     * automatically after 'timeout' milliseconds of inactivity.
     * @param timeout The timeout in milliseconds. Use 0 to show
     * the controller until hide() is called.
     */
    public void show(int timeout) {

        if (!mShowing && mAnchor != null) {

            int [] anchorpos = new int[2];
            mAnchor.getLocationOnScreen(anchorpos);

            WindowManager.LayoutParams p = new WindowManager.LayoutParams();
            if(mOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT) {

				p.gravity = Gravity.BOTTOM;
			}
			else {

				p.gravity = Gravity.TOP;				
			}
            p.width = LayoutParams.WRAP_CONTENT;
            p.height = mAnchor.getHeight();
            p.x = anchorpos[0] + mAnchor.getWidth() - p.width;;
            p.y = 0;
            p.format = PixelFormat.TRANSLUCENT;
            p.type = WindowManager.LayoutParams.TYPE_APPLICATION_PANEL;
            p.flags |= WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM;
            p.token = null;
            p.windowAnimations = 0; // android.R.style.DropDownAnimationDown;
            mWindowManager.addView(mDecor, p);
            mShowing = true;
        }
        // cause the progress bar to be updated even if mShowing
        // was already true.  This happens, for example, if we're
        // paused with the progress bar showing the user hits play.
        mHandler.sendEmptyMessage(SHOW_PROGRESS);

        Message msg = mHandler.obtainMessage(FADE_OUT);
        if (timeout != 0) {
            mHandler.removeMessages(FADE_OUT);
            mHandler.sendMessageDelayed(msg, timeout);
        }
    }
    
    public boolean isShowing() {
        return mShowing;
    }

    /**
     * Remove the controller from the screen.
     */
    public void hide() {
        if (mAnchor == null)
            return;

        if (mShowing) {
            try {
                mHandler.removeMessages(SHOW_PROGRESS);
                mWindowManager.removeView(mDecor);
            } catch (IllegalArgumentException ex) {
                Log.w("MediaController", "already removed");
            }
            mShowing = false;
        }
    }

    private Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            int pos;
            switch (msg.what) {
                case FADE_OUT:
                    hide();
                    break;
                case SHOW_PROGRESS:
                    if (mShowing) {
                        msg = obtainMessage(SHOW_PROGRESS);
                        sendMessageDelayed(msg, 1000);
                    }
                    break;
            }
        }
    };

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        show(sDefaultTimeout);
        return true;
    }

    @Override
    public boolean onTrackballEvent(MotionEvent ev) {
        show(sDefaultTimeout);
        return false;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) 
    {
        int keyCode = event.getKeyCode();
        if (event.getRepeatCount() == 0 && event.isDown() && (
                keyCode ==  KeyEvent.KEYCODE_HEADSETHOOK ||
                keyCode ==  KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE ||
                keyCode ==  KeyEvent.KEYCODE_SPACE)) 
        {
            show(sDefaultTimeout);
            return true;
        } 
        else if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP) 
        {
            // don't show the controls for volume adjustment
            return super.dispatchKeyEvent(event);
        } 
        else if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_MENU) 
        {
            hide();
            return true;
        } 
        else 
        {
            show(sDefaultTimeout);
        }
        return super.dispatchKeyEvent(event);
    }

    private View.OnClickListener mSettingListener = new View.OnClickListener() {
        public void onClick(View v) {
            show(sDefaultTimeout);
        }
    };
    
    private View.OnClickListener mToggleListener = new View.OnClickListener() {
        public void onClick(View v) {
			mP2PConfPlayer.ToggleVideo();
            show(sDefaultTimeout);
        }
    };
    
    private ListView.OnItemClickListener mListViewListener = new ListView.OnItemClickListener() 
    {
		public void onItemClick(AdapterView<?> parent, View view, int position, long id) 
		{
            show(sDefaultTimeout);
            Log.i(TAG, "onItemClick position:"+position+" id:"+id);
			switch (position)
			{
			    case 0:
			    { //High Quality
			        mP2PConfPlayer.setQuality(0);
				}
				break ;
				case 1:
				{ //Medium-Quality
					mP2PConfPlayer.setQuality(1);
				}
				break ;
				case 2:
				{ //Low-Quality
				   mP2PConfPlayer.setQuality(2);
				}
				break ;
				case 3:
				{ //Generate i-Frame
				   mP2PConfPlayer.generateIFrame();
				}
				break ;
				case 4:
				{ // Encoder Info	
					mP2PConfPlayer.showEncoderInformation();
				}
				break;		
			}	
		}	
	};


    @Override
    public void setEnabled(boolean enabled) {
        super.setEnabled(enabled);
    }

    private View.OnClickListener mEndCallListener = new View.OnClickListener() {
        public void onClick(View v) {
			mP2PConfPlayer.endCall();
            show(sDefaultTimeout);
        }
    };

    private View.OnClickListener mStartCallListener = new View.OnClickListener() {
        public void onClick(View v) {
			mP2PConfPlayer.startCall();
            show(sDefaultTimeout);
        }
    };


    public interface P2PConfControl {
        void    startCall();
        void    endCall();
        void    generateIFrame();
        void    showEncoderInformation();
        void    setQuality(int pos);
        void    ToggleVideo();
    }
}
