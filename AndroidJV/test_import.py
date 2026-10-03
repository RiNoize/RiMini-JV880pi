#!/usr/bin/env python3
"""Android SAF integration test using the real picker and synthetic data, no ROMs.
No app test hooks, no storage permissions, and no emulated sound checks.
"""
import os, re, subprocess as sp, sys, time, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'test-results'; OUT.mkdir(exist_ok=True)
PKG='com.rinoize.rijv880test'
SDK=Path(os.environ['ANDROID_HOME'])
os.environ['PATH']=':'.join(str(SDK/p) for p in ('platform-tools','emulator','cmdline-tools/latest/bin'))+':'+os.environ['PATH']
for k in ('ANDROID_SDK_HOME',): os.environ.pop(k,None)
for k,p in [('ANDROID_USER_HOME',Path.home()/'.android'),('ANDROID_EMULATOR_HOME',Path.home()/'.android'),('ANDROID_AVD_HOME',Path.home()/'.android/avd')]:
 os.environ[k]=str(p); p.mkdir(parents=True,exist_ok=True)

def run(*args,timeout=30,check=True):
 p=sp.run(list(map(str,args)),stdout=sp.PIPE,stderr=sp.STDOUT,timeout=timeout)
 s=p.stdout.decode(errors='replace')
 if check and p.returncode: raise RuntimeError(' '.join(map(str,args))+'\n'+s)
 return s

def adb(*args,**kw):return run('adb',*args,**kw)
def shell(*args,**kw):return adb('shell',*args,**kw)
def shot(name):
 p=sp.run(['adb','exec-out','screencap','-p'],stdout=sp.PIPE,timeout=20,check=True)
 (OUT/(name+'.png')).write_bytes(p.stdout)
def dump(name='picker'):
 for attempt in range(4):
  shell('rm','-f','/sdcard/ri-test.xml')
  shell('uiautomator','dump','--compressed','/sdcard/ri-test.xml',timeout=20,check=False)
  s=shell('cat','/sdcard/ri-test.xml',check=False)
  if s.startswith('<?xml'):
   (OUT/(name+'.xml')).write_text(s)
   return ET.fromstring(s)
  time.sleep(1)
 shot(name+'-no-xml')
 raise RuntimeError('No accessibility tree for '+name)
def label(n):return (n.get('text','')+' '+n.get('content-desc','')).strip()
def find(root,text):
 for n in root.iter('node'):
  if n.get('text','')==text or n.get('content-desc','')==text:return n
 for n in root.iter('node'):
  if text.lower() in label(n).lower():return n
 return None
def tap(n,long=False):
 b=list(map(int,re.findall(r'\d+',n.get('bounds',''))))
 assert len(b)==4 and b[2]>b[0] and b[3]>b[1],str(n.attrib)
 x,y=(b[0]+b[2])//2,(b[1]+b[3])//2
 if long:shell('input','swipe',x,y,x,y,900)
 else:shell('input','tap',x,y)
 time.sleep(0.6)
def require(root,text,long=False):
 n=find(root,text)
 if n is None:raise RuntimeError('Missing '+text+': '+str([label(n) for n in root.iter('node')]))
 tap(n,long)
def picker():
 # Exact bounds come from MainPanel::resized; the JUCE peer has no XML root.
 size=shell('wm','size'); w,h=map(int,re.findall(r'(\d+)x(\d+)',size)[-1])
 shell('input','tap',int(w*.348),int(h*.035));time.sleep(1.2)
 root=dump('picker-open')
 if find(root,'RiJV-Import-Test') is not None:
  require(root,'RiJV-Import-Test');return dump('fixture-folder')
 if any(find(root,n) is not None for n in ['01-fixtures.zip','jv880_nvram.bin']):return root
 if find(root,'Downloads') is None:
  require(root,'Show roots');root=dump('roots')
 require(root,'Downloads');root=dump('downloads')
 require(root,'RiJV-Import-Test');return dump('fixture-folder')
def result(test,pattern,count):
 for _ in range(25):
  log=adb('logcat','-d','-s','RiJV880Import:I','*:S')
  if pattern in log:
   assert log.count('IMPORT_READ scheme=content')==count,log
   (OUT/(test+'.txt')).write_text(log);shot(test)
   print('PASS',test,pattern,flush=True);return
  time.sleep(0.4)
 shot(test+'-failed');raise RuntimeError('No import result: '+log)

def main():
 sp.run(['sudo','chmod','666','/dev/kvm'],check=True)
 print(run('sdkmanager','--install','platform-tools','emulator','system-images;android-36;google_apis;x86_64',timeout=240),flush=True)
 p=sp.run(['avdmanager','create','avd','--force','--name','ri-import','--package','system-images;android-36;google_apis;x86_64','--device','pixel_tablet','--path',str(Path(os.environ['ANDROID_AVD_HOME'])/'ri-import.avd')],input=b'no\n',stdout=sp.PIPE,stderr=sp.STDOUT,timeout=45)
 if p.returncode:raise RuntimeError(p.stdout.decode())
 assert 'ri-import' in run('emulator','-list-avds')
 with (OUT/'emulator.txt').open('w') as f:
  sp.Popen(['emulator','-avd','ri-import','-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','software','-memory','2048'],stdout=f,stderr=sp.STDOUT)
 adb('wait-for-device',timeout=120)
 for _ in range(100):
  if shell('getprop','sys.boot_completed',check=False).strip()=='1':break
  time.sleep(2)
 assert shell('getprop','sys.boot_completed').strip()=='1'
 for s in ['window_animation_scale','transition_animation_scale','animator_duration_scale']:shell('settings','put','global',s,0)
 shell('input','keyevent',82)
 fixture=OUT/'RiJV-Import-Test';fixture.mkdir(exist_ok=True)
 files={'jv880_nvram.bin':32768,'jv880_rom1.bin':32768,'jv880_rom2.bin':262144,'jv880_waverom1.bin':2097152,'jv880_waverom2.bin':2097152}
 for i,(n,size) in enumerate(files.items()):
  (fixture/n).write_bytes(bytes([i+17])*size)
 with zipfile.ZipFile(fixture/'01-fixtures.zip','w',zipfile.ZIP_DEFLATED) as z:
  for n in files:z.write(fixture/n,'fixture/'+n)
 adb('push',fixture,'/sdcard/Download/')
 adb('install','--no-incremental','-r',ROOT/'dist/RiJV880-AndroidTest-v0.1.2-x86_64.apk',timeout=60)
 shell('am','start','-W','-n',PKG+'/com.rmsl.juce.JuceActivity')
 time.sleep(4);shot('startup')
 adb('logcat','-c');r=picker();require(r,'01-fixtures.zip')
 result('zip','IMPORT_RESULT selected=1 imported=1 ignored=4',1)
 adb('logcat','-c');r=picker();require(r,'jv880_nvram.bin')
 result('bin','IMPORT_RESULT selected=1 imported=1 ignored=0',1)
 adb('logcat','-c');r=picker();require(r,'jv880_nvram.bin',long=True)
 for n in files:
  if n!='jv880_nvram.bin':require(dump('multi-select'),n)
 r=dump('multi-selected');shot('multi-selected')
 for word in ('Open','Select'):
  n=find(r,word)
  if n is not None:tap(n);break
 else:raise RuntimeError('No selection confirmation button')
 result('five-bins','IMPORT_RESULT selected=5 imported=1 ignored=4',5)
 report='PASS: Android API 36 x86_64; real SAF picker; ZIP, single BIN, five BINs; content URI reads; synthetic NVRAM accepted; invalid firmware rejected.\nNo real JV ROMs, no audio, no physical tablet tested.\n'
 (OUT/'RESULT.txt').write_text(report);print(report,flush=True)

try:main()
finally:
 try:(OUT/'logcat.txt').write_text(adb('logcat','-d',timeout=15))
 except Exception:pass
 try:(OUT/'activities.txt').write_text(shell('dumpsys','activity','activities',timeout=15))
 except Exception:pass
 try:shot('final')
 except Exception:pass
 try:adb('emu','kill',timeout=10)
 except Exception:pass
