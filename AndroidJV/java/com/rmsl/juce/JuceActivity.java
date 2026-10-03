package com.rmsl.juce;
import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.view.WindowManager;
import android.widget.TextView;
import android.util.Log;

public class JuceActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        try {
            System.loadLibrary("juce_jni");
            Java.initialiseJUCE(getApplicationContext());
        } catch (Throwable error) {
            TextView text = new TextView(this);
            text.setText("RiJV880: error de arranque\n\n" + Log.getStackTraceString(error));
            text.setTextIsSelectable(true);
            text.setPadding(24, 24, 24, 24);
            setContentView(text);
        }
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }
}
