#!/usr/bin/env python3
"""Apply the 0.1.1 Android startup repair to the pinned 0.1.0 integration.
No emulator/DSP sources or ROM contents are changed. Safe to run repeatedly.
"""
from pathlib import Path

APP_JAVA = r'''package com.rmsl.juce;

import android.app.Application;
import android.util.Log;

/** Initialise JUCE before Android dispatches the first Activity lifecycle event. */
public final class RiJVApp extends Application {
    public static String startupError;
    @Override public void onCreate() {
        super.onCreate();
        try {
            Log.i("RiJV880Boot", "Application: initialising JUCE");
            Java.initialiseJUCE(this);
            Log.i("RiJV880Boot", "Application: JUCE ready");
        } catch (Throwable error) {
            startupError = Log.getStackTraceString(error);
            Log.e("RiJV880Boot", "Application startup failed", error);
        }
    }
}
'''
ACTIVITY_JAVA = r'''package com.rmsl.juce;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.Intent;
import android.graphics.Color;
import android.view.WindowManager;
import android.widget.TextView;
import android.util.Log;

public class JuceActivity extends Activity {
    private native int nativeUiStatus();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int attempts;
    private boolean bootComplete;
    private final Runnable checkUi = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed() || bootComplete) return;
            try {
                int status = nativeUiStatus();
                if ((status & 8) != 0) {
                    bootComplete = true;
                    Log.i("RiJV880Boot", "UI_READY: first frame rendered; status=" + status);
                    return;
                }
                if (++attempts >= 40) {
                    showMessage("RiJV880 0.1.1: no se pudo dibujar el panel.\n\n"
                        + "Estado de arranque: " + status
                        + "\nEnvia una captura de esta pantalla para el diagnostico.");
                    Log.e("RiJV880Boot", "UI_TIMEOUT status=" + status);
                    return;
                }
                handler.postDelayed(this, 500);
            } catch (Throwable error) {
                showMessage("RiJV880: error de interfaz\n\n" + Log.getStackTraceString(error));
                Log.e("RiJV880Boot", "UI check failed", error);
            }
        }
    };
    private void showMessage(String message) {
        TextView text = new TextView(this);
        text.setTextColor(Color.WHITE);
        text.setBackgroundColor(Color.rgb(22, 28, 35));
        text.setTextSize(19);
        text.setText(message);
        text.setTextIsSelectable(true);
        int pad = Math.round(24 * getResources().getDisplayMetrics().density);
        text.setPadding(pad, pad, pad, pad);
        setContentView(text);
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        showMessage(RiJVApp.startupError == null
            ? "RiJV880 0.1.1\n\nIniciando panel...\nNo necesitas ROMs para ver los controles."
            : "RiJV880: error de arranque\n\n" + RiJVApp.startupError);
        Log.i("RiJV880Boot", "Activity: onCreate");
    }
    @Override protected void onResume() {
        super.onResume();
        Log.i("RiJV880Boot", "Activity: onResume");
        if (RiJVApp.startupError == null && !bootComplete) {
            attempts = 0;
            handler.removeCallbacks(checkUi);
            handler.postDelayed(checkUi, 500);
        }
    }
    @Override protected void onPause() {
        handler.removeCallbacks(checkUi);
        super.onPause();
    }
    @Override protected void onDestroy() {
        handler.removeCallbacks(checkUi);
        super.onDestroy();
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }
}
'''
NEW_APP = r'''class RiJVApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "RiJV880 Test";}
    const juce::String getApplicationVersion() override {return "0.1.1";}
    void initialise(const juce::String&) override {
        riBootState.fetch_or(1);
        __android_log_print(ANDROID_LOG_INFO, "RiJV880Boot", "C++ application initialise");
        window=std::make_unique<Window>();
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
    void suspended() override { if(window) window->panel->suspend(); }
    class Window final : public juce::DocumentWindow {
    public:
        Window():DocumentWindow("RiJV880",juce::Colour(0xff161c23),0,false) {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            panel=new MainPanel();
            setContentOwned(panel,true);
            riBootState.fetch_or(2);
            // Attach to the actual Activity content, not a second WindowManager
            // window. This also supplies Android's required first content frame.
            auto activity=juce::getCurrentActivity();
            if (activity == nullptr) activity=juce::getMainActivity();
            if (activity == nullptr) throw std::runtime_error("No hay Activity para alojar el panel");
            addToDesktop(0, activity.get());
            setFullScreen(true);
            setVisible(true);
            riBootState.fetch_or(4);
            __android_log_print(ANDROID_LOG_INFO, "RiJV880Boot", "Panel attached: %d x %d", getWidth(),getHeight());
        }
        void closeButtonPressed() override {juce::JUCEApplication::getInstance()->systemRequestedQuit();}
        MainPanel* panel=nullptr;
    };
    std::unique_ptr<Window> window;
};
extern "C" JNIEXPORT jint JNICALL
Java_com_rmsl_juce_JuceActivity_nativeUiStatus(JNIEnv*, jobject) {
    return (jint)riBootState.load();
}
START_JUCE_APPLICATION(RiJVApplication)
'''

def once(text, old, new):
    if new in text:
        return text
    if old not in text:
        raise RuntimeError('Startup patch anchor missing: ' + old[:100])
    return text.replace(old, new)

def apply(root: Path):
    main = root/'Main.cpp'
    t=main.read_text()
    if 'riBootState' not in t:
        t=once(t, '#include <thread>', '#include <thread>\n#include <android/log.h>\nstatic std::atomic<int> riBootState{0};')
        old='    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff161c23)); }'
        new='''    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff161c23));
        if (!editor) {
            auto r=getLocalBounds().reduced(24);
            r.removeFromTop(100); r.removeFromBottom(60);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(22.0f));
            g.drawFittedText(busy ? "Preparando el motor..." : "Bienvenido a RiJV880\\n\\nPulsa Importar ROMs para cargar los archivos del JV-880.",
                            r,juce::Justification::centred,5);
        }
        if ((riBootState.fetch_or(8) & 8) == 0)
            __android_log_print(ANDROID_LOG_INFO,"RiJV880Boot","FIRST_PAINT %d x %d",getWidth(),getHeight());
    }'''
        t=once(t,old,new)
        t=once(t, '        scanMidi(); startTimerHz(4); load({});', '''        startTimerHz(4);
        juce::Timer::callAfterDelay(100,[safe=juce::Component::SafePointer<MainPanel>(this)]{
            if (safe) { safe->scanMidi(); safe->load({}); }
        });''')
        t=t[:t.index('class RiJVApplication final')]+NEW_APP
    main.write_text(t.replace('0.1.0','0.1.1'))
    manifest=root/'AndroidManifest.xml'
    t=manifest.read_text().replace('android:versionCode="1"','android:versionCode="2"').replace('0.1.0','0.1.1')
    t=once(t,'<application android:label=', '<application android:name="com.rmsl.juce.RiJVApp" android:label=')
    manifest.write_text(t)
    java=root/'java/com/rmsl/juce'
    java.mkdir(parents=True,exist_ok=True)
    (java/'JuceActivity.java').write_text(ACTIVITY_JAVA)
    (java/'RiJVApp.java').write_text(APP_JAVA)
    conf=root/'BuildConfig.h'
    conf.write_text(conf.read_text().replace('0.1.0','0.1.1').replace('0x100','0x101'))
    cmake=root/'CMakeLists.txt'
    t=cmake.read_text().replace('if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")','if(NOT ANDROID OR NOT ANDROID_ABI MATCHES "^(arm64-v8a|x86_64)$")').replace('Android arm64-v8a toolchain required','Android arm64-v8a or x86_64 toolchain required')
    cmake.write_text(t)
    build=root/'build.py'
    t=build.read_text().replace('0.1.0','0.1.1')
    t=once(t, "p.add_argument('--jobs',type=int,default=2);a=p.parse_args()", "p.add_argument('--abi',choices=['arm64-v8a','x86_64'],default='arm64-v8a');p.add_argument('--jobs',type=int,default=2);a=p.parse_args()\n    artifact_abi='arm64' if a.abi=='arm64-v8a' else 'x86_64'")
    t=t.replace("build=HERE/'build-arm64'", "build=HERE/('build-'+artifact_abi)")
    t=t.replace("'-DANDROID_ABI=arm64-v8a'", "f'-DANDROID_ABI={a.abi}'")
    t=t.replace("'lib/arm64-v8a/libjuce_jni.so'", "f'lib/{a.abi}/libjuce_jni.so'")
    t=t.replace("dist/'RiJV880-AndroidTest-v0.1.1-arm64.apk'", "dist/f'RiJV880-AndroidTest-v0.1.1-{artifact_abi}.apk'")
    t=t.replace("'abi':'arm64-v8a'", "'abi':a.abi")
    t=t.replace("'build-arm64','apk-work'", "'build-arm64','build-x86_64','apk-work'")
    build.write_text(t)
    readme=root/'LEEME.txt'
    t=readme.read_text().replace('0.1.0','0.1.1')
    if 'CORRECCION DE ARRANQUE' not in t:
        t+='''\n\nCORRECCION DE ARRANQUE 0.1.1\nInicializa JUCE desde Application antes de la primera Activity, aloja el panel en el contenido de la Activity y muestra estado de inicio en vez de una pantalla vacia. Primer inicio sin ROMs: deben aparecer Importar ROMs y el mensaje de bienvenida. El motor de sonido no se modifica.\nSe conserva el identificador de paquete y la clave de prueba de 0.1.0; instalar como actualizacion sin borrar datos. La verificacion de arranque en emulador no equivale a prueba de audio en la tablet.\nDesde el repositorio Git, ejecutar primero python3 AndroidJV/startup_fix.py. El ZIP de fuentes de esta version ya contiene los cambios aplicados. --abi x86_64 es solo para pruebas en emulador; la tablet usa arm64-v8a.\n'''
    readme.write_text(t)

if __name__=='__main__':
    apply(Path(__file__).resolve().parent)
