#!/usr/bin/env python3
"""Repair SAF document I/O in the pinned RiJV880 Android integration (0.1.2).
Run after startup_fix.py. No firmware, PCM, DSP or ROM validation changes.
"""
from pathlib import Path

HELPERS = r'''
// Never translate content:// selections into filesystem paths. On scoped storage
// the selection grants access to a document URI, not to the underlying path.
inline std::unique_ptr<juce::InputStream> openSelectedInput(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") {
        auto document = juce::AndroidDocument::fromDocument(url);
        return document ? document.createInputStream() : nullptr;
    }
#endif
    return url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
}
inline std::unique_ptr<juce::OutputStream> openSelectedOutput(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") {
        auto document = juce::AndroidDocument::fromDocument(url);
        return document ? document.createOutputStream() : nullptr;
    }
#endif
    return url.createOutputStream();
}
inline juce::String selectedName(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") {
        auto document = juce::AndroidDocument::fromDocument(url);
        if (document) {
            auto name = document.getInfo().getName();
            if (name.isNotEmpty()) return name;
        }
        return "documento seleccionado";
    }
#endif
    return url.getFileName();
}
'''

def change(text: str, old: str, new: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise RuntimeError('Missing patch anchor: ' + old[:100])
    return text.replace(old, new)

def apply(root: Path) -> None:
    p = root/'RomImport.h'
    t = p.read_text()
    if 'openSelectedInput' not in t:
        t = change(t, '#include "sha1.h"', '#include "sha1.h"\n#if JUCE_ANDROID\n#include <android/log.h>\n#endif')
        t = change(t, 'namespace RiRom {', 'namespace RiRom {\n'+HELPERS)
        t = change(t, '        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));\n        if (!stream) throw std::runtime_error("No se pudo abrir el archivo seleccionado");', '''        const auto name = selectedName(url);
        auto stream = openSelectedInput(url);
        if (!stream) throw std::runtime_error(("No se pudo leer: " + name + ". Selecciona de nuevo el archivo en el selector de Android.").toStdString());''')
        t = change(t, '        auto data = readBounded(*stream, 256u*1024*1024);', '''        auto data = readBounded(*stream, 256u*1024*1024);
#if JUCE_ANDROID
        // Counts only: do not record document URIs, user paths or ROM contents.
        __android_log_print(ANDROID_LOG_INFO, "RiJV880Import", "IMPORT_READ scheme=%s bytes=%zu", url.getScheme().toRawUTF8(), data.getSize());
#endif''')
        t = change(t, 'identifyAndWrite(url.getFileName(), data)', 'identifyAndWrite(name, data)')
        t = change(t, '    return urls.isEmpty() ? juce::String()', '''#if JUCE_ANDROID
    if (!urls.isEmpty()) __android_log_print(ANDROID_LOG_INFO, "RiJV880Import", "IMPORT_RESULT selected=%d imported=%d ignored=%d", urls.size(), imported, ignored);
#endif
    return urls.isEmpty() ? juce::String()''')
    p.write_text(t)
    p = root/'Main.cpp'
    t = change(p.read_text(), 'auto output=c.getURLResult().createOutputStream();', 'auto output=RiRom::openSelectedOutput(c.getURLResult());')
    t = change(t, '                safe->resized();', '                safe->resized();\n                safe->repaint();')
    p.write_text(t)
    for relative in ['Main.cpp','BuildConfig.h','AndroidManifest.xml','build.py','LEEME.txt',
                     'java/com/rmsl/juce/JuceActivity.java','java/com/rmsl/juce/RiJVApp.java']:
        p = root/relative
        t = p.read_text().replace('0.1.1','0.1.2')
        if p.name == 'AndroidManifest.xml':
            t = t.replace('android:versionCode="2"', 'android:versionCode="3"')
        if p.name == 'BuildConfig.h':
            t = t.replace('JUCE_APP_VERSION_HEX 0x101', 'JUCE_APP_VERSION_HEX 0x102')
        p.write_text(t)
    p = root/'LEEME.txt'
    t = p.read_text()
    if 'DOCUMENTOS ANDROID 0.1.2' not in t:
        t += '''\n\nDOCUMENTOS ANDROID 0.1.2\nCorregida la lectura de ZIP y BIN elegidos en el selector de Android mediante AndroidDocument (Storage Access Framework). No se convierte content:// en una ruta local sin permiso. El mismo arreglo se aplica a Exportar NVRAM. No se piden permisos generales de almacenamiento.\nInstalar sobre 0.1.1 sin desinstalar ni borrar datos. Volver a seleccionar el ZIP original o los cinco BIN; no renombrar ni modificar las ROMs. Se mantienen los checksums, limites de tamano, expansiones admitidas y el motor original.\nCompilacion desde Git: python3 AndroidJV/startup_fix.py && python3 AndroidJV/import_fix.py; despues ejecutar build.py. En el ZIP de fuentes los cambios ya estan aplicados.\nLas pruebas de importacion con datos sinteticos no equivalen a validacion del firmware ni a prueba de sonido en la tablet.\n'''
    p.write_text(t)

if __name__ == '__main__':
    apply(Path(__file__).resolve().parent)
