#!/usr/bin/env python3
"""Build RiJV880 Android Test on Linux with Java 17+, Python 3, CMake and Ninja."""
import argparse, hashlib, json, os, shutil, subprocess, zipfile
from pathlib import Path
from patch_upstream import patch
from compat import prepare
HERE=Path(__file__).resolve().parent
VJV='bc141fc8b7a5039e909807e85ff1070d5727584b'
JUCE='91ad83ae34a81e0833b1a2b0866f54846370ae53'

def run(*args):
    print('+',' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True)
def revision(path):
    # Extracted source distributions do not contain Git internals.
    if (path/'.git').exists():
        return subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    marker=path/'PINNED-COMMIT.txt'
    if marker.exists(): return marker.read_text().strip()
    raise RuntimeError(f'{path}: missing source revision metadata')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--upstream',type=Path)
    p.add_argument('--jobs',type=int,default=2);a=p.parse_args()
    prepare(HERE)
    sdk=a.sdk.resolve();deps=HERE/'deps';deps.mkdir(exist_ok=True)
    bundled=HERE.parent/'virtualjv-original'
    original=a.upstream.resolve() if a.upstream else (bundled if bundled.exists() else deps/'virtualjv-original')
    if not original.exists():
        run('git','clone','https://github.com/giulioz/jv880_juce.git',original);run('git','-C',original,'checkout',VJV)
    if revision(original)!=VJV: raise RuntimeError('VirtualJV revision mismatch')
    juce=deps/'JUCE'
    if not juce.exists(): run('git','clone','--depth','1','--branch','8.0.15','https://github.com/juce-framework/JUCE.git',juce)
    if revision(juce)!=JUCE: raise RuntimeError('JUCE revision mismatch')
    work=HERE/'generated'/'virtualjv'
    if work.exists(): shutil.rmtree(work)
    shutil.copytree(original,work,ignore=shutil.ignore_patterns('.git','Builds'))
    patch(work)
    ndk=sdk/'ndk'/'28.2.13676358';tools=sdk/'build-tools'/'35.0.0';jar=sdk/'platforms'/'android-35'/'android.jar'
    if not ndk.exists() or not tools.exists() or not jar.exists():
        candidates=list((sdk/'cmdline-tools').glob('*/bin/sdkmanager'))
        manager=next((f for f in candidates if '/latest/' in str(f)),candidates[0] if candidates else None)
        if manager is None: raise RuntimeError('Install Android SDK command-line tools, NDK 28.2.13676358, platform/build-tools 35')
        run(manager,'--install','ndk;28.2.13676358','platforms;android-35','build-tools;35.0.0')
    build=HERE/'build-arm64';dist=HERE/'dist';dist.mkdir(exist_ok=True)
    configure=['cmake','-S',HERE,'-B',build,'-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake','-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-29','-DANDROID_STL=c++_static',f'-DJUCE_ROOT={juce}',f'-DVIRTUALJV_ROOT={work}','-DCMAKE_BUILD_TYPE=Release']
    if shutil.which('ccache'): configure += ['-DCMAKE_C_COMPILER_LAUNCHER=ccache','-DCMAKE_CXX_COMPILER_LAUNCHER=ccache']
    run(*configure);run('cmake','--build',build,f'-j{max(1,a.jobs)}')
    lib=build/'libjuce_jni.so'
    run(ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip','--strip-unneeded',lib)
    apkWork=HERE/'apk-work'
    if apkWork.exists(): shutil.rmtree(apkWork)
    classes=apkWork/'classes';classes.mkdir(parents=True);dex=apkWork/'dex';dex.mkdir()
    sources=[]
    for folder in ['juce_core/native/javacore/init','juce_core/native/javacore/app','juce_gui_basics/native/javaopt/app']:
        sources += [f for f in (juce/'modules'/folder).rglob('*.java') if f.name!='JuceActivity.java']
    sources += list((HERE/'java').rglob('*.java'))
    run('javac','-source','8','-target','8','-classpath',jar,'-d',classes,*sources)
    appjar=apkWork/'app.jar';run('jar','cf',appjar,'-C',classes,'.')
    run(tools/'d8','--min-api','29','--lib',jar,'--output',dex,appjar)
    unsigned=apkWork/'unsigned.apk'
    run(tools/'aapt','package','-f','-M',HERE/'AndroidManifest.xml','-I',jar,'-F',unsigned)
    with zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as z:
        z.write(lib,'lib/arm64-v8a/libjuce_jni.so');z.write(dex/'classes.dex','classes.dex')
    aligned=apkWork/'aligned.apk';run(tools/'zipalign','-f','-P','16','4',unsigned,aligned)
    key=HERE/'signing'/'local-test.keystore';key.parent.mkdir(exist_ok=True)
    if not key.exists(): run('keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','rijv880test','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=RiJV880 Local Test')
    apk=dist/'RiJV880-AndroidTest-v0.1.0-arm64.apk'
    run(tools/'apksigner','sign','--ks',key,'--ks-key-alias','rijv880test','--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned)
    run(tools/'apksigner','verify','--verbose',apk);run(tools/'zipalign','-c','-P','16','4',apk)
    elf=subprocess.check_output([str(ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf'),'-l',str(lib)],text=True)
    (dist/'ELF-PROGRAM-HEADERS.txt').write_text(elf)
    manifest={'app':'RiJV880 Android Test','version':'0.1.0','abi':'arm64-v8a','min_api':29,'target_api':35,'ndk':'28.2.13676358','virtualjv_commit':VJV,'juce_commit':JUCE,'sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'hardware_tested':False,'roms_included':False}
    (dist/'BUILD-MANIFEST.json').write_text(json.dumps(manifest,indent=2))
    shutil.copy2(HERE/'LEEME.txt',dist/'RiJV880-AndroidTest-LEEME.txt')
    with zipfile.ZipFile(dist/'RiJV880-AndroidTest-v0.1.0-fuentes.zip','w',zipfile.ZIP_DEFLATED) as z:
        for f in HERE.rglob('*'):
            if not f.is_file():continue
            rel=f.relative_to(HERE)
            if rel.parts[0] in ('deps','generated','dist','build-arm64','apk-work','__pycache__'):continue
            z.write(f,Path('RiJV880/AndroidJV')/rel)
        for f in original.rglob('*'):
            if f.is_file() and '.git' not in f.relative_to(original).parts:
                z.write(f,Path('RiJV880/virtualjv-original')/f.relative_to(original))
        for f in work.rglob('*'):
            if f.is_file():z.write(f,Path('RiJV880/AndroidJV/generated/virtualjv')/f.relative_to(work))
        for f in (juce/'modules').rglob('*'):
            if f.is_file() and f.suffix.lower() not in ('.ttf','.otf','.woff','.woff2'):
                z.write(f,Path('RiJV880/AndroidJV/deps/JUCE/modules')/f.relative_to(juce/'modules'))
        for f in juce.glob('LICENSE*'):
            if f.is_file():z.write(f,Path('RiJV880/AndroidJV/deps/JUCE')/f.name)
        for f in (ndk/'sources/android/cpufeatures').rglob('*'):
            if f.is_file(): z.write(f,Path('RiJV880/third_party/cpufeatures')/f.relative_to(ndk/'sources/android/cpufeatures'))
        z.writestr('RiJV880/AndroidJV/deps/JUCE/PINNED-COMMIT.txt',JUCE+'\n')
        if not (original/'PINNED-COMMIT.txt').exists():z.writestr('RiJV880/virtualjv-original/PINNED-COMMIT.txt',VJV+'\n')
        z.write(dist/'BUILD-MANIFEST.json','RiJV880/BUILD-MANIFEST.json')
if __name__=='__main__': main()
