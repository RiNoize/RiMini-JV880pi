#!/usr/bin/env python3
"""Exercise production waveform-menu functions with real JUCE notifications.
ROM-name provider only is synthetic. Does not execute JV firmware or test audio.
"""
from pathlib import Path
import argparse
import subprocess


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    left = text.index('{', start)
    depth = 1
    end = left + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--juce', type=Path, required=True)
    p.add_argument('--upstream', type=Path, required=True)
    p.add_argument('--generated', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    functions = []
    classes = []
    for original, kind, menu in [('EditToneTab', 'Tone', 'juce::ComboBox'), ('EditRhythmTab', 'Rhythm', 'Menu')]:
        for fixed in [False, True]:
            cls = kind + ('Fixed' if fixed else 'Legacy')
            source = (args.generated if fixed else args.upstream) / 'Source/ui' / (original + '.cpp')
            body = function(source.read_text(encoding='utf-8-sig'), f'void {original}::updateWaveformComboBox(')
            body = body.replace(f'{original}::', f'{cls}::')
            extra = ', juce::NotificationType notification = juce::dontSendNotification' if fixed else ''
            classes.append(f'''struct {cls} {{
    FakeProcessor processor; FakeEditor host; FakeEditor* editor = &host;
    {menu} waveGroupComboBox, waveformComboBox;
    int cachedWaveRom = -1;
    void updateWaveformComboBox({menu}& wfMenu{extra});
}};''')
            functions.append(body)
    cpp = r'''#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using Menu = juce::ComboBox;
struct FakeEditor { unsigned int romIdx=3; unsigned int getSelectedRomIdx() const {return romIdx;} };
struct FakeProcessor {
    int reads=0; bool empty=false;
    std::vector<std::string> readMultisampleNames(unsigned int) {
        ++reads; std::vector<std::string> result;
        if (!empty) for(int i=0;i<129;++i)result.emplace_back("Synthetic "+std::to_string(i));
        return result;
    }
};
void require(bool ok,const char* msg) {if(!ok)throw std::runtime_error(msg);}
void pump() {juce::MessageManager::getInstance()->runDispatchLoopUntil(25);}
''' + '\n'.join(classes + functions) + r'''
template<class T> int refreshTest(bool legacy) {
    T ui;int changes=0;
    ui.waveformComboBox.onChange=[&]{++changes;};
    ui.waveGroupComboBox.setSelectedItemIndex(-1,juce::dontSendNotification);
    ui.waveGroupComboBox.addItem("Internal",1);ui.waveGroupComboBox.addItem("Expansion",2);
    ui.waveGroupComboBox.setSelectedItemIndex(0,juce::dontSendNotification);
    ui.updateWaveformComboBox(ui.waveformComboBox);
    ui.waveformComboBox.setSelectedItemIndex(0,juce::dontSendNotification);
    pump();changes=0;int reads=ui.processor.reads;
    for (int patch=1;patch<=64;++patch) {
        ui.updateWaveformComboBox(ui.waveformComboBox);
        ui.waveformComboBox.setSelectedItemIndex(patch,juce::dontSendNotification);
        pump();
        require(ui.waveformComboBox.getSelectedItemIndex()==patch,"Patch waveform lost");
    }
    require(changes==(legacy?64:0),"Unexpected notification count on passive refresh");
    if(!legacy)require(ui.processor.reads==reads,"Same-ROM menu was rebuilt");
    return changes;
}
template<class T> void explicitEditTest() {
    T ui;int changes=0;
    ui.waveformComboBox.onChange=[&]{++changes;};
    ui.waveGroupComboBox.addItem("Internal",1);ui.waveGroupComboBox.addItem("Expansion",2);
    ui.waveGroupComboBox.setSelectedItemIndex(0,juce::dontSendNotification);
    ui.updateWaveformComboBox(ui.waveformComboBox);
    ui.waveformComboBox.setSelectedItemIndex(42,juce::dontSendNotification);pump();changes=0;
    ui.waveGroupComboBox.setSelectedItemIndex(1,juce::dontSendNotification);
    ui.updateWaveformComboBox(ui.waveformComboBox);pump();
    require(changes==0,"Cross-ROM passive refresh emitted an edit");
    require(ui.waveformComboBox.getSelectedItemIndex()==42,"ROM switch lost selection");
    ui.waveGroupComboBox.setSelectedItemIndex(0,juce::dontSendNotification);
    ui.updateWaveformComboBox(ui.waveformComboBox,juce::sendNotificationAsync);pump();
    require(changes==1,"Real wave-group change no longer notified");
    ui.waveformComboBox.setSelectedItemIndex(43,juce::sendNotificationSync);
    require(changes==2,"Real waveform change no longer notified");
    ui.processor.empty=true;ui.host.romIdx=4;
    ui.waveGroupComboBox.setSelectedItemIndex(1,juce::dontSendNotification);
    ui.updateWaveformComboBox(ui.waveformComboBox);pump();
    require(changes==2 && ui.waveformComboBox.getNumItems()==0,"Empty ROM handling failed");
}
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI init;
        const int oldTone=refreshTest<ToneLegacy>(true),oldRhythm=refreshTest<RhythmLegacy>(true);
        std::cout<<"REPRODUCED: legacy production functions emitted "<<oldTone
                 <<" Tone and "<<oldRhythm<<" Rhythm callbacks over 64 passive refreshes each.\n";
        require(refreshTest<ToneFixed>(false)==0 && refreshTest<RhythmFixed>(false)==0,"Regression failed");
        explicitEditTest<ToneFixed>();explicitEditTest<RhythmFixed>();
        std::cout<<"PASS: patched production functions emit ZERO callbacks during passive refresh.\n"
                 <<"PASS: same-ROM menus reused; cross-ROM refresh silent; explicit edits still notify; empty ROM safe.\n"
                 <<"Real JUCE message loop and ComboBox; synthetic ROM names only. JV firmware latency NOT tested.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
'''
    (out / 'test.cpp').write_text(cpp)
    (out / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(RiPatchUiTest VERSION 0.1.4 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${JUCE_ROOT}" juce EXCLUDE_FROM_ALL)
juce_add_console_app(patch-ui-test PRODUCT_NAME "RiJV Patch UI Regression")
target_sources(patch-ui-test PRIVATE test.cpp)
target_compile_definitions(patch-ui-test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(patch-ui-test PRIVATE juce::juce_gui_basics)
''')
    subprocess.run(['cmake','-S',str(out),'-B',str(out/'build'),'-G','Ninja',f'-DJUCE_ROOT={args.juce.resolve()}','-DCMAKE_BUILD_TYPE=Release'], check=True)
    subprocess.run(['cmake','--build',str(out/'build'),'--target','patch-ui-test','-j4'], check=True)
    exe = out / 'build/patch-ui-test_artefacts/Release/patch-ui-test'
    subprocess.run(['xvfb-run','-a',str(exe)], check=True)

if __name__ == '__main__':
    main()
