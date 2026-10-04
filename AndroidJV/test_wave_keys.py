#!/usr/bin/env python3
"""Exercise the actual modified JUCE PopupMenu and production Menu widget.
Synthetic item names only; no firmware, MIDI device or physical keyboard.
Requires Xvfb and Openbox for real desktop/focus behavior (CI installs Openbox).
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

CPP = r'''#include <juce_gui_basics/juce_gui_basics.h>
#include "Menu.h"
#include <iostream>
#include <stdexcept>
using namespace juce;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void pump(int ms=25) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }
struct Look final : LookAndFeel_V4 {
    int highlighted=0; Component* popupParent=nullptr;
    PopupMenu::Options getOptionsForComboBoxPopupMenu(ComboBox& box, Label& label) override {
        auto result=LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box,label).withStandardItemHeight(18);
        return popupParent ? result.withParentComponent(popupParent) : result;
    }
    void drawPopupMenuItemWithOptions(Graphics& g,const Rectangle<int>& area,bool lit,
                                      const PopupMenu::Item& item,const PopupMenu::Options& options) override {
        if(lit) highlighted=item.itemID;
        LookAndFeel_V4::drawPopupMenuItemWithOptions(g,area,lit,item,options);
    }
};
struct Rig {
    Look look; Component host; Menu wave{1}; int changes=0; int initial=0;
    Rig() {
        host.setBounds(40,40,1350,850);host.setVisible(true);host.addToDesktop(0);
        host.addAndMakeVisible(wave);wave.setBounds(180,80,230,28);wave.setLookAndFeel(&look);
        wave.onChange=[this]{++changes;};pump(70);
    }
    ~Rig() { PopupMenu::dismissAllActiveMenus();pump();wave.setLookAndFeel(nullptr);host.removeFromDesktop(); }
    void fill(int count, bool enabled=true, int rows=32) {
        wave.clear(dontSendNotification);
        wave.getProperties().set("RiJV880.WaveColumns",enabled);
        for(int n=0;n<count;++n) {
            if(n>0 && n%rows==0)wave.getRootMenu()->addColumnBreak();
            wave.addItem(String(n+1)+": Synthetic Wave",n+1);
        }
        pump();changes=0;
    }
    Component* popup() { auto* p=Component::getCurrentlyModalComponent();require(p!=nullptr,"Popup missing");return p; }
    void open(int id) {
        wave.setSelectedId(id,dontSendNotification);pump();changes=0;initial=id;
        wave.showPopup();pump(50);require(wave.isPopupActive(),"Menu not open");
    }
    void key(int code) { require(popup()->keyPressed(KeyPress(code)),"Key not handled");pump(); }
    int highlight(const char* file=nullptr) {
        look.highlighted=0;auto* p=popup();auto image=p->createComponentSnapshot(p->getLocalBounds());
        if(file) { FileOutputStream out(File::getCurrentWorkingDirectory().getChildFile(file));PNGImageFormat png;png.writeImageToStream(image,out); }
        return look.highlighted;
    }
    void expect(int id) {
        const int actual=highlight();
        if(actual!=id) throw std::runtime_error("Highlighted "+std::to_string(actual)+", expected "+std::to_string(id)+", initial "+std::to_string(initial));
        require(wave.getSelectedId()==initial && changes==0,"Navigation edited value before confirmation");
    }
    void cancel() { key(KeyPress::escapeKey);pump(50);require(!wave.isPopupActive(),"Escape did not close");require(wave.getSelectedId()==initial && changes==0,"Cancel changed value"); }
    void commit(int id) { key(KeyPress::returnKey);pump(50);require(!wave.isPopupActive(),"Enter did not close");require(wave.getSelectedId()==id,"Enter selected wrong wave");require(changes==(id==initial?0:1),"Wrong number of value callbacks"); }
};
int main() {
    try {
        ScopedJuceInitialiser_GUI init;
        Rig r;
        r.fill(70,false);r.open(5);r.key(KeyPress::rightKey);
        require(r.highlight()==5,"Unmarked menu unexpectedly acquired column navigation");
        std::cout<<"BASELINE: unmarked JUCE route keeps highlight on wave 5 after Right (underlying value "<<r.wave.getSelectedId()<<").\n";
        PopupMenu::dismissAllActiveMenus();pump(60);
        for(bool parented:{false,true}) {
            r.look.popupParent=parented?&r.host:nullptr;
            r.fill(70);r.open(5);r.expect(5);
            r.key(KeyPress::rightKey);r.expect(37);
            r.key(KeyPress::downKey);r.expect(38);
            r.key(KeyPress::leftKey);r.expect(6);
            r.key(KeyPress::upKey);r.expect(5);
            r.key(KeyPress::rightKey);r.expect(37);
            if(!parented) r.highlight("wave-column-navigation.png");
            r.commit(37);
            r.open(63);r.key(KeyPress::rightKey);r.expect(70);
            r.key(KeyPress::leftKey);r.expect(38);r.cancel();
            r.open(1);r.key(KeyPress::leftKey);r.expect(1);r.cancel();
            r.open(70);r.key(KeyPress::rightKey);r.expect(70);r.cancel();
            r.fill(10);r.open(4);r.key(KeyPress::leftKey);r.expect(4);
            r.key(KeyPress::rightKey);r.expect(4);r.cancel();
            r.fill(70);r.wave.setItemEnabled(37,false);r.open(5);
            r.key(KeyPress::rightKey);r.expect(36);r.cancel();
            r.fill(70);r.open(0);r.key(KeyPress::rightKey);r.expect(1);r.commit(1);
            r.fill(0);r.open(0);r.key(KeyPress::leftKey);r.expect(0);r.cancel();
            r.fill(17,true,6);r.open(5);r.key(KeyPress::rightKey);r.expect(11);
            r.key(KeyPress::rightKey);r.expect(17);r.cancel();
            r.fill(160);r.open(8);
            for(int n=0;n<100;++n){r.key(KeyPress::rightKey);r.expect(40);r.key(KeyPress::leftKey);r.expect(8);}
            r.cancel();
            // Outside the popup the pre-existing ComboBox arrow behavior remains.
            r.wave.setSelectedId(5,dontSendNotification);pump();r.changes=0;
            r.wave.keyPressed(KeyPress(KeyPress::rightKey));pump();
            require(r.wave.getSelectedId()==6 && r.changes==1,"Closed combo behavior changed");
            std::cout<<"PASS: "<<(parented?"parented":"desktop")<<" popup; actual Menu/ComboBox, nearest row, shortened/disabled/empty columns, edges, different column size, Enter once, Escape no edit, 200 repeated lateral keys.\n";
        }
        std::cout<<"PASS: actual JUCE popup keyPressed/rendering with synthetic names. No audio/ROM/physical Android USB keyboard test.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
'''

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--juce',type=Path,required=True)
    p.add_argument('--generated',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    if shutil.which('openbox') is None:
        if os.environ.get('GITHUB_ACTIONS') != 'true':
            raise RuntimeError('Install Openbox and Xvfb before running this desktop popup test')
        subprocess.run(['sudo','apt-get','install','-y','-qq','openbox'],check=True)
    src=a.generated/'Source'
    for kind in ('EditToneTab','EditRhythmTab'):
        t=(src/'ui'/(kind+'.cpp')).read_text()
        assert 'wfMenu.getProperties().set("RiJV880.WaveColumns", true);' in t
    (out/'Menu.h').write_bytes((src/'ui/widgets/Menu.h').read_bytes())
    (out/'test.cpp').write_text(CPP)
    (out/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(RiWaveKeyTest VERSION 0.1.7 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${JUCE_ROOT}" juce EXCLUDE_FROM_ALL)
juce_add_console_app(wave-key-test PRODUCT_NAME "RiJV Wave Keyboard Test")
target_sources(wave-key-test PRIVATE test.cpp)
target_compile_definitions(wave-key-test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(wave-key-test PRIVATE juce::juce_gui_basics)
''')
    subprocess.run(['cmake','-S',str(out),'-B',str(out/'build'),'-G','Ninja',
        f'-DJUCE_ROOT={a.juce.resolve()}','-DCMAKE_BUILD_TYPE=Release'],check=True)
    subprocess.run(['cmake','--build',str(out/'build'),'--target','wave-key-test','-j4'],check=True)
    exe=out/'build/wave-key-test_artefacts/Release/wave-key-test'
    wm_script='openbox --sm-disable >openbox.log 2>&1 & wm=$!; trap "kill $wm 2>/dev/null || true" EXIT; sleep 1; "$@"'
    result=subprocess.run(['xvfb-run','-a','-s','-screen 0 1920x1080x24','bash','-c',wm_script,'--',str(exe)],
        cwd=out,capture_output=True,text=True,timeout=150)
    (out/'result.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,flush=True)
    result.check_returncode()

if __name__=='__main__':main()
