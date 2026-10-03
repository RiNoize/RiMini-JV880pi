#!/usr/bin/env python3
"""Copy SAF documents using Android's Java ContentResolver before native parsing.
Run after startup_fix.py, import_fix.py and eof_fix.py. No synth core changes.
"""
from pathlib import Path
JAVA=r'''
    // Import-only, worker-thread I/O. The selected document URI stays opaque;
    // only the private cache copy is later opened by native code.
    public String selectedDocumentName(String text) {
        android.net.Uri uri=android.net.Uri.parse(text);
        try (android.database.Cursor cursor=getContentResolver().query(uri,
                new String[]{android.provider.OpenableColumns.DISPLAY_NAME},null,null,null)) {
            if (cursor!=null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getString(0);
        } catch (Exception ignored) {}
        String name=uri.getLastPathSegment();
        return name==null ? "documento seleccionado" : name;
    }
    public long selectedDocumentSize(String text) {
        try (android.database.Cursor cursor=getContentResolver().query(android.net.Uri.parse(text),
                new String[]{android.provider.OpenableColumns.SIZE},null,null,null)) {
            if (cursor!=null && cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getLong(0);
        } catch (Exception ignored) {}
        return -1;
    }
    public String cacheSelectedDocument(String text,long limit) throws java.io.IOException {
        if (limit<0 || limit>268435456L) throw new java.io.IOException("Limite de importacion invalido");
        android.net.Uri uri=android.net.Uri.parse(text);
        if (!"content".equals(uri.getScheme())) throw new java.io.IOException("Se esperaba un documento Android");
        long expected=selectedDocumentSize(text);
        if (expected>limit) throw new java.io.IOException("Archivo demasiado grande");
        java.io.File cache=java.io.File.createTempFile("rijv-import-",".cache",getCacheDir());
        boolean complete=false;
        try {
            long total=0;
            try (java.io.InputStream input=getContentResolver().openInputStream(uri);
                 java.io.OutputStream output=new java.io.FileOutputStream(cache)) {
                if (input==null) throw new java.io.IOException("Android no pudo abrir el documento");
                byte[] buffer=new byte[32768];
                for (;;) {
                    int count=input.read(buffer);
                    if (count==-1) break;
                    if (count==0) continue;
                    if (total>limit-count) throw new java.io.IOException("Archivo demasiado grande");
                    output.write(buffer,0,count); total+=count;
                }
                if (expected>=0 && total!=expected) throw new java.io.IOException("Lectura incompleta del documento");
            }
            complete=true;
            Log.i("RiJV880Import","CACHE_COPY bytes="+total);
            return cache.getAbsolutePath();
        } finally {
            if (!complete) cache.delete();
        }
    }
'''
CPP=r'''
#if JUCE_ANDROID
inline void checkDocumentJavaError() {
    auto* env=juce::getEnv();
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        throw std::runtime_error("Android no pudo leer el documento seleccionado");
    }
}
inline juce::String documentJavaString(const juce::URL& url, bool copyToCache) {
    auto* env=juce::getEnv();
    auto app=juce::getAppContext();
    if (app==nullptr) throw std::runtime_error("Contexto Android no disponible");
    juce::LocalRef<jclass> cls(env->GetObjectClass(app.get()));
    const auto method=env->GetMethodID(cls.get(),copyToCache ? "cacheSelectedDocument" : "selectedDocumentName",
                                     copyToCache ? "(Ljava/lang/String;J)Ljava/lang/String;" : "(Ljava/lang/String;)Ljava/lang/String;");
    checkDocumentJavaError();
    auto text=juce::javaString(url.toString(true));
    juce::LocalRef<jstring> result((jstring)(copyToCache
        ? env->CallObjectMethod(app.get(),method,text.get(),(jlong)(256u*1024*1024))
        : env->CallObjectMethod(app.get(),method,text.get())));
    checkDocumentJavaError();
    if (result==nullptr) throw std::runtime_error("Android no devolvio el documento");
    return juce::juceString(result.get());
}
inline juce::int64 documentJavaSize(const juce::URL& url) {
    auto* env=juce::getEnv();auto app=juce::getAppContext();
    if (app==nullptr) throw std::runtime_error("Contexto Android no disponible");
    juce::LocalRef<jclass> cls(env->GetObjectClass(app.get()));
    const auto method=env->GetMethodID(cls.get(),"selectedDocumentSize","(Ljava/lang/String;)J");
    checkDocumentJavaError();
    const auto result=env->CallLongMethod(app.get(),method,juce::javaString(url.toString(true)).get());
    checkDocumentJavaError();return (juce::int64)result;
}
class CachedDocumentStream final : public juce::InputStream {
public:
    explicit CachedDocumentStream(const juce::String& path):file(path),stream(file.createInputStream()) {}
    ~CachedDocumentStream() override {stream.reset();file.deleteFile();}
    bool valid() const {return stream!=nullptr;}
    juce::int64 getTotalLength() override {return stream->getTotalLength();}
    juce::int64 getPosition() override {return stream->getPosition();}
    bool setPosition(juce::int64 position) override {return stream->setPosition(position);}
    bool isExhausted() override {return stream->isExhausted();}
    int read(void* dest,int bytes) override {return stream->read(dest,bytes);}
private:
    juce::File file;
    std::unique_ptr<juce::FileInputStream> stream;
};
#endif
'''

def apply(root:Path):
    p=root/'java/com/rmsl/juce/RiJVApp.java';t=p.read_text()
    if 'cacheSelectedDocument(' not in t:
        pos=t.rfind('\n}');assert pos>=0;t=t[:pos]+JAVA+t[pos:]
    p.write_text(t)
    p=root/'RomImport.h';t=p.read_text()
    if 'class CachedDocumentStream' not in t:
        t=t.replace('namespace RiRom {','namespace RiRom {\n'+CPP,1)
        start=t.index('inline std::unique_ptr<juce::InputStream> openSelectedInput(')
        end=t.index('inline std::unique_ptr<juce::OutputStream> openSelectedOutput(',start)
        t=t[:start]+'''inline std::unique_ptr<juce::InputStream> openSelectedInput(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") {
        auto stream=std::make_unique<CachedDocumentStream>(documentJavaString(url,true));
        if (!stream->valid()) return nullptr;
        return stream;
    }
#endif
    return url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
}
'''+t[end:]
        start=t.index('inline juce::String selectedName(');end=t.index('inline juce::File directory()',start)
        t=t[:start]+'''inline juce::String selectedName(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") return documentJavaString(url,false);
#endif
    return url.getFileName();
}
'''+t[end:]
        start=t.index('inline juce::int64 selectedSize(');end=t.index('inline juce::String identifyAndWrite(',start)
        t=t[:start]+'''inline juce::int64 selectedSize(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") return documentJavaSize(url);
#endif
    return -1;
}
'''+t[end:]
    p.write_text(t)
    p=root/'test_import.py';t=p.read_text()
    old="""def require(root,text,long=False):
 n=find(root,text)
 if n is None:raise RuntimeError('Missing '+text+': '+str([label(n) for n in root.iter('node')]))
 tap(n,long)"""
    new="""def require(root,text,long=False):
 for attempt in range(10):
  n=find(root,text)
  if n is not None:
   tap(n,long);return
  time.sleep(0.8)
  root=dump('wait-for-'+re.sub(r'[^a-zA-Z0-9]','_',text))
 raise RuntimeError('Missing '+text+': '+str([label(n) for n in root.iter('node')]))"""
    if old in t:t=t.replace(old,new)
    t=t.replace("time.sleep(2);shot('startup')","time.sleep(12);shot('startup')")
    compile(t,str(p),'exec');p.write_text(t)
    p=root/'LEEME.txt';t=p.read_text()
    if 'LECTOR JAVA Y CACHE PRIVADA' not in t:
        t+='\nLECTOR JAVA Y CACHE PRIVADA\nLa version entregada lee el documento con ContentResolver.openInputStream en Java y copia a cache privada limitada antes de pasarlo al parser nativo. Se evita la ruta de lectura de documentos de JUCE que fallo en la prueba del emulador. Se comprueba el tamano informado por Android. La copia temporal se elimina al terminar y no se modifica el archivo original. No se agregan permisos generales de almacenamiento.\nAl compilar desde Git, aplicar tambien python3 AndroidJV/native_uri_fix.py despues de eof_fix.py. Las fuentes distribuidas ya estan preparadas.\n'
    p.write_text(t)

if __name__=='__main__':apply(Path(__file__).resolve().parent)
