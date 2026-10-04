#!/usr/bin/env python3
"""Exercise the unmodified LCD renderer and GUI-only filter, with synthetic text.
The minimal MCU only supplies renderer mode flags; no firmware/ROMs are loaded.
"""
from pathlib import Path
import argparse, shutil, subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--generated',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
for name in ('lcd.cpp','lcd.h','lcd_font.h'):
    shutil.copy2(a.generated/'Source/emulator'/name,out/name)
(out/'mcu.h').write_text('''#pragma once
struct MCU { bool mcu_cm300=false, mcu_st=false, mcu_scb55=false, mcu_jv880=true;
unsigned mcu_button_pressed=0; };
''')
(out/'submcu.h').write_text('#pragma once\n')
(out/'test.cpp').write_text(r'''#include "lcd.h"
#include "mcu.h"
#include "RiBulkDisplay.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
using namespace RiBulkDisplay;
void set(LCD& lcd,const Frame& f) {
    std::memcpy(lcd.LCD_Data,f.text.data(),f.text.size());
    std::memcpy(lcd.LCD_CG,f.glyphs.data(),f.glyphs.size());
    lcd.LCD_C=f.cursor; lcd.LCD_DD_RAM=f.address;
}
std::vector<uint32_t> render(LCD& lcd) {
    auto* p=lcd.LCD_Update();assert(p);
    std::vector<uint32_t> out;
    for(int y=0;y<100;++y)for(int x=0;x<820;++x)out.push_back(p[y*1024+x]);
    return out;
}
void ppm(const char* name,const std::vector<uint32_t>& pixels) {
    std::ofstream f(name,std::ios::binary);f<<"P6\n820 100\n255\n";
    for(auto p:pixels)for(auto shift:{16,8,0})f.put((char)((p>>shift)&255));
}
int main() {
    MCU mcu;auto lcd=std::make_unique<LCD>(&mcu);
    lcd->lcd_width=820;lcd->lcd_height=100;lcd->LCD_Init();
    Filter filter;const Context context{1,0,false};
    auto clean=fallback("Warm Pad",false);
    auto receiving=fallback("",false);receiving.text.fill(' ');
    constexpr char message[]="Now Bulk Receiving...";
    std::copy(message,message+sizeof(message)-1,receiving.text.begin());
    set(*lcd,clean);const auto normal=render(*lcd);ppm("normal.ppm",normal);
    assert(filter.select(clean,context,clean,true)==clean);
    const auto saved=receiving;
    set(*lcd,filter.select(receiving,context,clean,true));
    const auto suppressed=render(*lcd);ppm("hidden.ppm",suppressed);
    assert(suppressed==normal);assert(receiving==saved);
    set(*lcd,filter.select(receiving,context,clean,false));
    const auto original=render(*lcd);ppm("original.ppm",original);
    assert(original!=normal);
    auto error=fallback("",false);error.text.fill(' ');
    constexpr char bad[]="MIDI Buffer Full!";
    std::copy(bad,bad+sizeof(bad)-1,error.text.begin());
    set(*lcd,filter.select(error,context,clean,true));
    const auto visibleError=render(*lcd);ppm("error.ppm",visibleError);
    set(*lcd,error);assert(render(*lcd)==visibleError);
    std::cout<<"PASS: actual unchanged LCD renderer; hidden notice pixel-identical to previous screen; original notice restored when disabled; error pixels preserved\n";
}
''')
subprocess.run(['clang++','-std=c++20','-O1','-g','-fsanitize=address,undefined',
                '-I'+str(Path(__file__).resolve().parent),str(out/'lcd.cpp'),str(out/'test.cpp'),
                '-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],cwd=out,check=True)
