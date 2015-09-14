package com.p2p.app;

import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Color;
import android.content.Context;
import android.util.AttributeSet;

public class VideoSurfaceView extends SurfaceView
{
	boolean mShowTextView;
	String mString ;
	VideoSurfaceView(Context context)
	{
		super(context);
	}
	public VideoSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public VideoSurfaceView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }
	public void invalidar()
	{
		invalidate();
	}
	@Override
	public void onDraw(Canvas canvas) 
	{
		super.onDraw(canvas);
	/*
		Paint paint = new Paint(Color.RED);
		paint.setTextSize(15);
		paint.setColor(Color.WHITE); 
		// nothing gets drawn :(
		if(mShowTextView && mString != null) {
			canvas.drawText(mString, canvas.getWidth()/4,canvas.getHeight() / 2, paint);
		}
		else {
			canvas.drawText("", canvas.getWidth()/4,canvas.getHeight() / 2, paint);
		}
	*/
	}
	/*
	public void showTextView(boolean show) {
		mShowTextView = show;
	}
	public void setTextView(String string) {
		mString=string;
	}
	*/
}
