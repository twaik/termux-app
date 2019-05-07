package com.termux.Lorie;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;

public class LorieActivity extends Activity {
    @SuppressLint("ClickableViewAccessibility")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        NativeSurfaceView surface = new NativeSurfaceView(this);
        setContentView(surface);
    }
}
