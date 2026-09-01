package com.abysscore.exgc;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

public class ExgActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        UsbSerial.init(this);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onDestroy() {
        UsbSerial.close();
        super.onDestroy();
    }
}
