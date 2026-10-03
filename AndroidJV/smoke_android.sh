#!/usr/bin/env bash
# Real Android framework launch test, without ROMs or an attached MIDI controller.
set -euo pipefail
SDK=${ANDROID_HOME:?ANDROID_HOME is required}
export PATH="$SDK/platform-tools:$SDK/emulator:$SDK/cmdline-tools/latest/bin:$PATH"
OUT=AndroidJV/test-results
mkdir -p "$OUT"
trap 'adb logcat -d > "$OUT/logcat.txt" 2>/dev/null || true; adb shell dumpsys activity activities > "$OUT/activities.txt" 2>/dev/null || true; adb emu kill >/dev/null 2>&1 || true' EXIT
sudo chmod 666 /dev/kvm
sdkmanager --install platform-tools emulator 'system-images;android-36;google_apis;x86_64'
echo no | avdmanager create avd --force --name rijv880-smoke --package 'system-images;android-36;google_apis;x86_64' --device 'pixel_tablet'
emulator -avd rijv880-smoke -no-window -no-audio -no-boot-anim -no-snapshot -gpu swiftshader_indirect -memory 2048 > "$OUT/emulator.txt" 2>&1 &
adb wait-for-device
for n in $(seq 1 120); do
  [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break
  sleep 2
done
[ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = 1 ]
adb shell settings put global window_animation_scale 0
adb shell settings put global transition_animation_scale 0
adb shell settings put global animator_duration_scale 0
adb shell input keyevent 82
adb install -r AndroidJV/dist/RiJV880-AndroidTest-v0.1.1-x86_64.apk
PKG=com.rinoize.rijv880test
ACT=$PKG/com.rmsl.juce.JuceActivity
for trial in 1 2; do
  adb shell am force-stop "$PKG"
  adb logcat -c
  adb shell am start -W -n "$ACT" > "$OUT/launch-$trial.txt"
  found=0
  for n in $(seq 1 35); do
    adb logcat -d -s RiJV880Boot:I '*:S' > "$OUT/boot-$trial.txt"
    if grep -q UI_READY "$OUT/boot-$trial.txt"; then found=1; break; fi
    sleep 1
  done
  adb exec-out screencap -p > "$OUT/startup-$trial.png"
  adb shell uiautomator dump /sdcard/rijv880.xml || true
  adb pull /sdcard/rijv880.xml "$OUT/startup-$trial.xml" || true
  cat "$OUT/boot-$trial.txt"
  [ "$found" = 1 ] || { echo 'FAIL: no JUCE first frame'; exit 1; }
  adb shell pidof "$PKG" > "$OUT/pid-$trial.txt"
done
python3 - <<'PY'
from pathlib import Path
import re, xml.etree.ElementTree as ET, subprocess
p=Path('AndroidJV/test-results/startup-2.xml')
root=ET.parse(p).getroot()
found=[]
for node in root.iter('node'):
    label=node.get('text','')+' '+node.get('content-desc','')
    if 'Importar ROMs' in label and node.get('enabled')=='true':
        bounds=list(map(int,re.findall(r'\d+',node.get('bounds',''))))
        if len(bounds)==4 and bounds[2]>bounds[0] and bounds[3]>bounds[1]:
            found.append(bounds)
assert found, 'Importar ROMs control is absent, disabled or has zero bounds'
b=found[0]
subprocess.run(['adb','shell','input','tap',str((b[0]+b[2])//2),str((b[1]+b[3])//2)],check=True)
print('PASS: Importar ROMs is visible, enabled, and tapped')
PY
sleep 3
adb shell dumpsys activity activities > "$OUT/import-picker.txt"
adb exec-out screencap -p > "$OUT/import-picker.png"
grep -Eq 'mResumedActivity.*documentsui|topResumedActivity.*documentsui' "$OUT/import-picker.txt"
adb shell input keyevent 4
sleep 2
adb shell pidof "$PKG"
adb shell input keyevent 3
adb shell am start -W -n "$ACT" > "$OUT/resume.txt"
sleep 2
adb shell pidof "$PKG"
adb exec-out screencap -p > "$OUT/resumed.png"
printf 'PASS: Android API 36 x86_64 cold launch twice, first painted frame, visible Importar ROMs, file picker open/cancel, resume.\nNo ROM, audio or physical ARM64 tablet validation.\n' | tee "$OUT/RESULT.txt"
