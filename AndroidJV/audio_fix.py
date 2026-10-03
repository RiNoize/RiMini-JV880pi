#!/usr/bin/env python3
"""RiJV880 0.1.3 audio integration. Run after native_uri_fix.py."""
from pathlib import Path
from audio_engine_patch import replace
ROOT=Path(__file__).resolve().parent

def apply(root:Path):
    f=root/'Main.cpp';t=f.read_text()
    if 'RiAudioCpu.h' not in t:
        t=replace(t,'#include <thread>','#include <thread>\n#include "RiAudioCpu.h"')
        t=replace(t,'&keysButton,&exportButton,&patchName','&keysButton,&exportButton,&cpuMode,&patchName')
        t=replace(t,'        midi.onChange=[this]{ selectMidi(); };', '''        cpuMode.addItem("CPU: auto",1);
        cpuMode.addItem(cpuPolicy.isAvailable()?"CPU: rapidos":"CPU: auto (unico)",2);
        cpuMode.setSelectedId(cpuPolicy.isAvailable()?2:1,juce::dontSendNotification);
        cpuMode.onChange=[this]{cpuPolicy.selectFast(cpuMode.getSelectedId()==2);};
        midi.onChange=[this]{ selectMidi(); };''')
        t=replace(t,'        midi.setBounds(r.reduced(4));','        cpuMode.setBounds(r.removeFromRight(145).reduced(2));\n        midi.setBounds(r.reduced(4));')
        t=replace(t,'metrics.setBounds(area.removeFromBottom(24));','metrics.setBounds(area.removeFromBottom(48));')
        t=replace(t,'        auto error=audio.initialise(0,2,nullptr,true);', '''        loadAverage.store(0);loadPeak.store(0);cpuAverage.store(0);lateBlocks.store(0);
        cpuPolicy.beginStream();
        auto error=audio.initialise(0,2,nullptr,true);''')
        t=replace(t,'if (running) { audio.removeAudioCallback(this); audio.closeAudioDevice(); }','if (running) { audio.removeAudioCallback(this); cpuPolicy.restore(); audio.closeAudioDevice(); }')
        t=replace(t,'        auto t=juce::Time::getHighResolutionTicks();', '''        auto t=juce::Time::getHighResolutionTicks();
        const double cpuStart=RiAudioCpu::threadSeconds();
        cpuPolicy.onCallback();callbackFrames.store(samples);''')
        t=replace(t,'        const double ratio=elapsed*sampleRate.load()/samples;', '''        const double ratio=elapsed*sampleRate.load()/samples;
        const double cpuRatio=(RiAudioCpu::threadSeconds()-cpuStart)*sampleRate.load()/samples;
        cpuAverage.store(cpuAverage.load()*0.95+cpuRatio*0.05);''')
        start=t.index('        juce::String text=juce::String(sampleRate.load(),0)');end=t.index('        metrics.setText(text,juce::dontSendNotification);',start)
        t=t[:start]+'''        juce::String text=juce::String(sampleRate.load(),0)+" Hz | buffer "+juce::String(actualBuffer.load())
            +" / bloque "+juce::String(callbackFrames.load())
            +" | DSP "+juce::String(loadAverage.load()*100,1)+"% / pico "+juce::String(loadPeak.load()*100,1)
            +"% | tarde "+juce::String((juce::int64)lateBlocks.load())+" | xrun "+(xruns<0?juce::String("n/d"):juce::String(xruns));
        text += "\\nCPU trabajo "+juce::String(cpuAverage.load()*100,1)+"% | nucleo "+juce::String(cpuPolicy.core.load());
        if(cpuPolicy.status.load()<0)text+=" (afinidad no disponible)";
        if(engine)text += " | motor "+juce::String(engine->nativeRatio.load()*100,1)+"% | SRC "+juce::String(engine->srcRatio.load()*100,1)
            +"% | bloqueo "+juce::String((juce::int64)engine->skippedBlocks.load())
            +" | error motor "+juce::String((juce::int64)engine->renderErrors.load());
        text+=" | MIDI "+juce::String((juce::int64)midiEvents.load());
'''+t[end:]
        t=replace(t,'    juce::ComboBox midi,buffer;','    RiAudioCpu cpuPolicy;\n    juce::ComboBox midi,buffer,cpuMode;')
        t=replace(t,'sampleRate{48000},loadAverage{0},loadPeak{0}','sampleRate{48000},loadAverage{0},loadPeak{0},cpuAverage{0}')
        t=replace(t,'actualBuffer{0}','actualBuffer{0},callbackFrames{0}')
    f.write_text(t.replace('0.1.2','0.1.3'))
    f=root/'build.py';t=f.read_text()
    if 'from audio_engine_patch import patch_audio' not in t:
        t=replace(t,'from compat import prepare','from compat import prepare\nfrom audio_engine_patch import patch_audio')
        t=replace(t,'    patch(work)','    patch(work)\n    patch_audio(work,HERE)')
    f.write_text(t.replace('0.1.2','0.1.3'))
    f=root/'CMakeLists.txt';t=f.read_text().replace('COMPILE_OPTIONS "-O3"','COMPILE_OPTIONS "-O3;-flto=thin;-fvisibility=hidden"')
    if 'PRIVATE -flto=thin' not in t:t=t.replace('target_link_options(juce_jni PRIVATE ','target_link_options(juce_jni PRIVATE -flto=thin ')
    f.write_text(t)
    f=root/'AndroidManifest.xml';f.write_text(f.read_text().replace('versionCode="3"','versionCode="4"').replace('0.1.2','0.1.3'))
    for name in ['BuildConfig.h','LEEME.txt','test_import.py']:
        f=root/name;f.write_text(f.read_text().replace('0.1.2','0.1.3').replace('0x102','0x103'))
    for f in (root/'java').rglob('*.java'):f.write_text(f.read_text().replace('0.1.2','0.1.3'))
    f=root/'LEEME.txt';t=f.read_text()
    if 'AUDIO 0.1.3' not in t:
        t+='''\n\nAUDIO 0.1.3\nRemuestreador continuo FIR 64 taps, fases racionales y NEON en ARM64. Conserva el dominio interno de 64 kHz, ciclos de CPU, PCM, efectos y polifonia. No descarta bloques fraccionarios ni aplica correcciones de medio buffer. El filtro de conversion se modifica respecto a 0.1.2: la salida no es bit-identica.\nThinLTO en el motor, sin fast-math. El LCD usa una copia triple-buffer sin bloquear el motor durante el refresco. Los cambios de patch/edicion conservan la sincronizacion anterior y requieren pruebas en la tablet.\nCPU: rapidos selecciona el grupo de mayor capacidad/frecuencia entre los nucleos permitidos, si Android permite detectarlos. CPU: auto deja la asignacion al sistema. No eleva frecuencias ni modifica protecciones termicas. Puede consumir mas bateria.\nDSP/pico: tiempo transcurrido del callback respecto a su plazo, NO CPU total de la tablet. CPU trabajo: tiempo efectivo del hilo sin tiempo desprogramado. Motor y SRC: partes del tiempo transcurrido. Buffer y bloque pueden diferir. Contadores reiniciados al iniciar audio.\nPrueba: 512 muestras, CPU: rapidos si aparece, observar motor/SRC/DSP en reposo y tocando. No se promete rendimiento sin medida fisica.\nCompilar desde Git: ejecutar startup_fix.py, import_fix.py, eof_fix.py, native_uri_fix.py y audio_fix.py en ese orden antes de build.py. El ZIP contiene los cambios ya aplicados.\n'''
    f.write_text(t)
if __name__=='__main__':apply(ROOT)
