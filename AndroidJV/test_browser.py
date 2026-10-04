#!/usr/bin/env python3
"""Real JUCE browser regression. Only ROM catalogue/processor are synthetic.
Compiles the production PatchBrowser.h/.cpp for 0.1.4 and the fixed revision.
Legacy and fixed sources are renamed only to link them in a single test binary.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess

SUPPORT = r'''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <climits>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>
constexpr size_t romCount = 27, romCountRequired = 5;
constexpr int NUM_EXPS = 21;
struct RomInfo { bool loaded = true; };
inline RomInfo romInfos[romCount];
struct VirtualJVProcessor {
    struct PatchInfo {
        const char* name = "Test Patch"; const char* ptr = nullptr;
        int nameLength = 10, expansionI = 0xff, patchI = 0;
        bool present = true, drums = false; int iInList = 0;
    };
    struct State { int selectedRom = 0, selectedPatch = -1; } status;
    bool loaded = true;
    std::deque<PatchInfo> storage;
    std::vector<std::vector<PatchInfo*>> patchInfoPerGroup;
    int calls = 0, last = -1;
    VirtualJVProcessor() {
        const int counts[] = {195, 193, 8, 256, 61, 255, 0, 130, 2, 100, 260,
                               5, 65, 128, 129, 180, 30, 192, 3, 220, 20, 0};
        for (int count : counts) {
            auto& group = patchInfoPerGroup.emplace_back();
            group.reserve((size_t)count);
            for (int i = 0; i < count; ++i) {
                const int index = (int)storage.size();
                storage.emplace_back(); storage.back().iInList = index;
                group.push_back(&storage.back());
            }
            group.shrink_to_fit();
        }
    }
    int getNumPrograms() const { return (int)storage.size(); }
    void setCurrentProgram(int index) {
        if (index < 0 || index >= getNumPrograms()) throw std::runtime_error("bad program");
        last = index; ++calls;
    }
};
'''
MAIN = r'''#include "test_support.h"
#include "legacy/ui/LegacyBrowser.cpp"
#include "fixed/ui/SafeBrowser.cpp"
#include <cstdlib>
void require(bool yes, const char* text) { if (!yes) throw std::runtime_error(text); }
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(2); }
int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI init;
        juce::Image image(juce::Image::ARGB, 300, 34, true);
        juce::Graphics graphics(image);
        if (argc > 1 && std::string(argv[1]) == "--legacy-repro") {
            VirtualJVProcessor processor;
            LegacyBrowser browser(processor);
            // A row >= getNumRows() is a permitted JUCE paint callback, NOT an
            // invalid API call. Simulate the row cache after a shorter bank.
            auto* model = browser.patchesListModels[0]; model->groupI = 2;
            std::cout << "LEGACY_REPRO: production paint handler; 8-patch bank, placeholder row 8\n" << std::flush;
            model->paintListBoxItem(8, graphics, 300, 34, false);
            std::_Exit(0); // Reproduction must fail at painting, not destruction.
        }
        VirtualJVProcessor processor;
        const int sequence[] = {3, 2, 6, 10, 4, 21, 0, 1, 7, 8, 13, 14};
        int switches = 0, invalidPaints = 0, selected = 0;
        {
            SafeBrowser browser(processor);
            browser.setSize(1050, 440); browser.setVisible(true); pump();
            for (int round = 0; round < 50; ++round) for (int group : sequence) {
                const int previousCalls = processor.calls;
                browser.categoriesListBox.selectRow(group);
                // A stale event from the old group's row must not change sound.
                for (auto* m : browser.patchesListModels)
                    if (m->groupI != group) m->selectedRowsChanged(0);
                pump();
                require(processor.calls == previousCalls, "Category refresh selected a patch");
                ++switches;
                for (int column = 0; column < safeColumns; ++column) {
                    auto* model = browser.patchesListModels[column];
                    require(model->groupI == group, "Columns not updated to selected group");
                    const int available = std::max(0, std::min(safeRowPerColumn,
                         (int)processor.patchInfoPerGroup[group].size() - column * safeRowPerColumn));
                    require(model->getNumRows() == available, "Wrong per-column row count");
                    const int count = model->getNumRows();
                    for (int row : {-1, count, count + 1, INT_MAX}) {
                        require(model->patchAtRow(row) == nullptr, "Invalid row accepted");
                        model->paintListBoxItem(row, graphics, 300, 34, false); ++invalidPaints;
                    }
                    if (count > 0) {
                        model->paintListBoxItem(count-1, graphics, 300, 34, true);
                        const int before = processor.calls;
                        browser.patchesListBoxes[column]->selectRow(count-1);
                        require(processor.calls == before + 1, "Valid click not delivered once");
                        require(processor.last == processor.patchInfoPerGroup[group][column*safeRowPerColumn+count-1]->iInList,
                                "Selected program from wrong bank");
                        ++selected;
                    }
                    // Real JUCE paints the component and its recycled row children.
                    auto snapshot = browser.patchesListBoxes[column]->createComponentSnapshot(
                        browser.patchesListBoxes[column]->getLocalBounds());
                    require(snapshot.isValid(), "No browser image");
                }
            }
            for (int row : {-1, NUM_EXPS+1, INT_MAX})
                browser.categoriesListBox.getListBoxModel()->paintListBoxItem(row,graphics,300,34,false);
            auto* model = browser.patchesListModels[0];
            for (int group : {-1, NUM_EXPS+1, INT_MAX}) {
                model->groupI=group; require(model->getNumRows()==0, "Invalid group accepted");
                model->paintListBoxItem(0,graphics,300,34,false);
            }
            model->groupI=2; romInfos[romCountRequired+2].loaded=false;
            require(model->getNumRows()==0, "Unloaded expansion accepted");
            model->paintListBoxItem(0,graphics,300,34,false);
            romInfos[romCountRequired+2].loaded=true;
            auto*& first=processor.patchInfoPerGroup[2][0]; auto* saved=first; first=nullptr;
            model->paintListBoxItem(0,graphics,300,34,false);require(!model->patchAtRow(0),"Null patch accepted");
            first=saved; const char* name=first->name; first->name=nullptr;
            model->paintListBoxItem(0,graphics,300,34,false);first->name=name;
            processor.loaded=false; model->paintListBoxItem(0,graphics,300,34,false);processor.loaded=true;
        }
        for (int i=0;i<50;++i) {
            SafeBrowser browser(processor); browser.setSize(800,400);
            browser.categoriesListBox.selectRow(i%22); // queued callback at destruction
        }
        pump();
        std::cout << "PASS: " << switches << " production browser category changes, " << selected
                  << " valid selections, " << invalidPaints << " placeholder/out-of-range paints.\n"
                  << "PASS: large/small/empty/unloaded groups, stale events, null entries, real JUCE snapshots, 50 pending-callback destructions.\n"
                  << "ASan/UBSan instrument the browser/harness. JUCE GUI is real; ROM catalogue and processor are synthetic.\n"
                  << "No commercial ROMs, JV firmware, expansion audio or Android tablet crash trace tested.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
'''

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--juce',type=Path,required=True)
    p.add_argument('--generated',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args(); out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    (out/'test_support.h').write_text(SUPPORT)
    (out/'JuceHeader.h').write_text('#include <juce_gui_basics/juce_gui_basics.h>\n')
    for sub, cls, prefix, source in [
        ('legacy','LegacyBrowser','legacy',a.generated/'browser-v014'),
        ('fixed','SafeBrowser','safe',a.generated/'Source/ui')]:
        root=out/sub;(root/'ui').mkdir(parents=True,exist_ok=True)
        for f in ('PluginProcessor.h','rom.h'): (root/f).write_text('#include "../test_support.h"\n')
        for extension in ('.h','.cpp'):
            t=(source/('PatchBrowser'+extension)).read_text()
            for old,new in [('PatchBrowser',cls),('groupNames',prefix+'GroupNames'),
                            ('columns',prefix+'Columns'),('rowPerColumn',prefix+'RowPerColumn')]:
                t=re.sub(r'\b'+old+r'\b',new,t)
            (root/'ui'/(cls+extension)).write_text(t)
    (out/'test.cpp').write_text(MAIN)
    (out/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.22)
project(RiBrowserTest VERSION 0.1.5 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory("${JUCE_ROOT}" juce EXCLUDE_FROM_ALL)
juce_add_console_app(browser-test PRODUCT_NAME "RiJV Browser Regression")
target_sources(browser-test PRIVATE test.cpp)
target_include_directories(browser-test PRIVATE .)
target_compile_definitions(browser-test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
set_source_files_properties(test.cpp PROPERTIES COMPILE_OPTIONS "-O1;-g;-fsanitize=address,undefined;-fno-omit-frame-pointer")
target_link_options(browser-test PRIVATE -fsanitize=address,undefined)
target_link_libraries(browser-test PRIVATE juce::juce_gui_basics)
''')
    subprocess.run(['cmake','-S',str(out),'-B',str(out/'build'),'-G','Ninja',
        f'-DJUCE_ROOT={a.juce.resolve()}','-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++'],check=True)
    subprocess.run(['cmake','--build',str(out/'build'),'--target','browser-test','-j4'],check=True)
    exe=out/'build/browser-test_artefacts/Release/browser-test'
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    # GUI libraries can retain process-global allocations; memory access checking
    # stays enabled, leak detection is not part of this regression.
    result=subprocess.run(['xvfb-run','-a',str(exe),'--legacy-repro'],capture_output=True,text=True,env=env,timeout=40)
    report=result.stdout+result.stderr;(out/'legacy-asan.txt').write_text(report)
    assert result.returncode!=0 and 'LEGACY_REPRO' in report and 'heap-buffer-overflow' in report, report
    print('REPRODUCED: 0.1.4 production PatchBrowser::paintListBoxItem heap-buffer-overflow on placeholder row 8 in an 8-patch group.',flush=True)
    result=subprocess.run(['xvfb-run','-a',str(exe)],capture_output=True,text=True,env=env,timeout=150)
    (out/'fixed-result.txt').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,flush=True)
    result.check_returncode()

if __name__=='__main__':main()
