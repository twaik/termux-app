package com.termux.Lorie;

import android.content.Context;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

class NativeSurfaceView extends SurfaceView implements View.OnTouchListener, SurfaceHolder.Callback {
    long nativePtr = 0;
    Context ctx = null;
    public NativeSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    public NativeSurfaceView(Context context) {
        super(context);
        init(context);
    }

    private void init(Context context) {
        ctx = context;
        nativePtr = nativeInit();
        setOnTouchListener(this);
        getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        onSurfaceCreated(nativePtr, holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        onSurfaceChanged(nativePtr, holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        onSurfaceDestroyed(nativePtr, holder.getSurface());
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        int state  = 0;
        switch (event.getAction()) {
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_BUTTON_RELEASE:
                state = 0; //LORIE_INPUT_STATE_UP
                break;
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_BUTTON_PRESS:
                state = 1; //LORIE_INPUT_STATE_DOWN
                break;
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_HOVER_MOVE:
                state = 2; //LORIE_INPUT_STATE_MOTION
                break;
        }

        // LORIE_INPUT_BUTTON_LEFT = 1

        nativeTouch(nativePtr, 1, state, (int) event.getX(), (int) event.getY());
        return true;
    }

    private native long nativeInit();
    private native void nativeTouch(long nativePtr, int button, int state, int x, int y);

    private native void onSurfaceCreated(long nativePtr, Surface surface);
    private native void onSurfaceChanged(long nativePtr, Surface surface, int width, int height);
    private native void onSurfaceDestroyed(long nativePtr, Surface surface);

    static {
        System.loadLibrary("termux-native");
    }
}
