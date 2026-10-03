#!/usr/bin/env python3
"""Normalise Android document EOF and validate the reported input size.
Apply after startup_fix.py and import_fix.py; retains all ROM checksum checks.
"""
from pathlib import Path

READ = r'''inline juce::MemoryBlock readBounded(juce::InputStream& in, size_t maxSize,
                                    bool androidDocument = false, juce::int64 expectedSize = -1) {
    if (expectedSize >= 0 && (juce::uint64)expectedSize > (juce::uint64)maxSize)
        throw std::runtime_error("Archivo demasiado grande");
    juce::MemoryBlock result;
    char buffer[16384];
    for (;;) {
        const int n = in.read(buffer, sizeof(buffer));
        // JUCE 8's Android stream forwards Java's -1 at EOF. Other input
        // streams retain their usual error handling. Size/checksum checks
        // below still reject truncated documents rather than importing them.
        if (n < 0) {
            if (androidDocument && n == -1 && in.isExhausted()) break;
            throw std::runtime_error("Error al leer archivo");
        }
        if (n == 0) break;
        if (result.getSize() + (size_t)n > maxSize)
            throw std::runtime_error("Archivo demasiado grande");
        result.append(buffer, (size_t)n);
    }
    if (expectedSize >= 0 && (juce::uint64)result.getSize() != (juce::uint64)expectedSize)
        throw std::runtime_error("Archivo incompleto: no se leyeron todos los bytes esperados");
    return result;
}
inline juce::int64 selectedSize(const juce::URL& url) {
#if JUCE_ANDROID
    if (url.getScheme() == "content") {
        const auto document = juce::AndroidDocument::fromDocument(url);
        if (document) {
            const auto info = document.getInfo();
            if (info.isSizeInBytesValid()) return info.getSizeInBytes();
        }
    }
#endif
    return -1;
}
'''

def apply(root: Path):
    p=root/'RomImport.h';t=p.read_text()
    if 'bool androidDocument = false' not in t:
        start=t.index('inline juce::MemoryBlock readBounded(')
        end=t.index('inline juce::String identifyAndWrite(',start)
        t=t[:start]+READ+t[end:]
        old='auto data = readBounded(*stream, 256u*1024*1024);'
        assert old in t
        t=t.replace(old,'auto data = readBounded(*stream, 256u*1024*1024, url.getScheme() == "content", selectedSize(url));')
    p.write_text(t)
    p=root/'LEEME.txt';t=p.read_text()
    if 'FIN DE LECTURA ANDROID' not in t:
        t+='\nFIN DE LECTURA ANDROID\nLa lectura de documentos reconoce el -1 de fin de archivo de Java/JUCE. Se compara la cantidad leida con el tamano publicado por Android y se conservan las comprobaciones de ROMs y ZIP. No se aceptan documentos truncados con tamano conocido.\nDesde Git, ejecutar tambien python3 AndroidJV/eof_fix.py despues de import_fix.py y antes de build.py; el ZIP de fuentes ya incluye todos los cambios aplicados.\n'
    p.write_text(t)
    patch_test(root/'test_import.py')


def patch_test(p: Path):
    t=p.read_text()
    if 'launched=False' in t: return
    t=t.replace("  if n.get('text','')==text or n.get('content-desc','')==text:return n", "  if n.get('text','').lower()==text.lower() or n.get('content-desc','').lower()==text.lower():return n")
    old=" adb('push',fixture,'/sdcard/Download/')"
    new=''' for attempt in range(40):
  ready=shell('mkdir -p /sdcard/Download/RiJV-Import-Test && touch /sdcard/Download/RiJV-Import-Test/.ready && echo STORAGE_READY',check=False)
  if 'STORAGE_READY' in ready:break
  time.sleep(2)
 else:raise RuntimeError('Shared storage never became writable: '+ready)
 for f in fixture.iterdir():
  print(adb('push',f,'/sdcard/Download/RiJV-Import-Test/'+f.name),flush=True)'''
    assert old in t;t=t.replace(old,new)
    start=t.index('def picker():');end=t.index('def result(',start)
    t=t[:start]+r'''def picker():
 size=shell('wm','size'); w,h=map(int,re.findall(r'(\d+)x(\d+)',size)[-1])
 shell('input','tap',int(w*.348),int(h*.035));time.sleep(1.2)
 root=dump('picker-open')
 if find(root,'sdk_gphone64_x86_64') is None:
  require(root,'Show roots');root=dump('roots')
 require(root,'sdk_gphone64_x86_64');root=dump('internal-storage')
 require(root,'Download');root=dump('internal-download')
 require(root,'RiJV-Import-Test');return dump('fixture-folder')
'''+t[end:]
    old=" shell('am','start','-W','-n',PKG+'/com.rmsl.juce.JuceActivity')\n time.sleep(4);shot('startup')"
    new=''' launched=False
 launch_log=[]
 for attempt in range(5):
  launch_log.append(shell('am','start','-W','-n',PKG+'/com.rmsl.juce.JuceActivity'))
  (OUT/'launch.txt').write_text(''.join(launch_log))
  for tick in range(10):
   view=shell('dumpsys','activity','activities')
   boot=adb('logcat','-d','-s','RiJV880Boot:I','*:S')
   if 'UI_READY' in boot and any(PKG in line and 'ResumedActivity' in line for line in view.splitlines()):
    launched=True;break
   time.sleep(1)
  if launched:break
 (OUT/'startup-logcat.txt').write_text(adb('logcat','-d'))
 assert launched,'App did not reach UI_READY in foreground'
 time.sleep(2);shot('startup')'''
    assert old in t;t=t.replace(old,new)
    t=t.replace("print('PASS',test,pattern,flush=True);return", "print('PASS',test,pattern,flush=True);time.sleep(1);return")
    compile(t,str(p),'exec');p.write_text(t)

if __name__=='__main__':
    apply(Path(__file__).resolve().parent)
