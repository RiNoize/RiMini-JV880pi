#!/usr/bin/env python3
"""Apply checked, Android-only patches to a disposable VirtualJV source copy."""
from pathlib import Path
import sys

def patch(root):
    def change(name, old, new, count=1):
        path=root/name
        text=path.read_text()
        found=text.count(old)
        if found < count: raise RuntimeError(f'{name}: expected patch anchor not found: {old[:90]!r}')
        path.write_text(text.replace(old,new,count))
    def replace_function(name, signature, body):
        path=root/name; text=path.read_text(); start=text.index(signature); left=text.index('{',start)
        depth=1; end=left+1
        while depth:
            if text[end]=='{': depth+=1
            elif text[end]=='}': depth-=1
            end+=1
        path.write_text(text[:left]+body+text[end:])
    change('Source/PluginProcessor.h','const uint8_t* expansionsDescr[NUM_EXPS];','const uint8_t* expansionsDescr[NUM_EXPS] = {};')
    change('Source/PluginProcessor.h','int currentExpansion{0};','int currentExpansion{-1};')
    change('Source/PluginProcessor.h','juce::SpinLock mcuLock;', '''juce::SpinLock mcuLock;
    std::atomic<uint64_t> skippedBlocks{0}, midiDropped{0};
    std::atomic<bool> panicRequested{false};
    juce::MidiBuffer deferredMidi;
    int currentProgramIndex = -1;''')
    change('Source/PluginProcessor.cpp','#include "PluginEditor.h"','#include "PluginEditor.h"\n#include "emulator/resample/libresample.h"')
    change('Source/PluginProcessor.cpp','.withInput("Input", juce::AudioChannelSet::stereo(), true)','.withInput("Input", juce::AudioChannelSet::disabled(), false)')
    change('Source/PluginProcessor.cpp','ownedNames.reserve(52);','ownedNames.reserve(1024);\n  deferredMidi.ensureSize(65536);')
    change('Source/PluginProcessor.cpp','loaded = true;', '''loaded = true;
  status.isDrums = mcu->nvram[0x11] != 1;
  memcpy(status.patch, &mcu->nvram[0x0d70], sizeof(status.patch));
  memcpy(status.drums, &mcu->nvram[0x67f0], sizeof(status.drums));''')
    change('Source/PluginProcessor.cpp','delete mcu;', '''delete mcu;
  for (auto& rom : loadedRoms) { free(rom); rom = nullptr; }''')
    replace_function('Source/PluginProcessor.cpp','int VirtualJVProcessor::getCurrentProgram()', '{ return currentProgramIndex; }')
    change('Source/PluginProcessor.cpp','int expansionI = patchInfos[index].expansionI;','currentProgramIndex = index;\n  int expansionI = patchInfos[index].expansionI;')
    change('Source/PluginProcessor.cpp','if (index < 0 || index >= getNumPrograms())\n    return {};','if (!loaded || index < 0 || index >= getNumPrograms())\n    return {};')
    replace_function('Source/PluginProcessor.cpp','void VirtualJVProcessor::prepareToPlay', '''{
  const juce::SpinLock::ScopedLockType lock(mcuLock);
  const int rate = (int)getSampleRate();
  if (!loaded || rate <= 0) return;
  mcu->midiQueue.reserve(2048);
  deferredMidi.clear();
  if (mcu->savedDestSampleRate != rate) {
    if (mcu->resampleL) resample_close(mcu->resampleL);
    if (mcu->resampleR) resample_close(mcu->resampleR);
    const double ratio = rate / 64000.0;
    mcu->resampleL = resample_open(1, ratio, ratio);
    mcu->resampleR = resample_open(1, ratio, ratio);
    mcu->savedDestSampleRate = rate;
    mcu->sample_write_ptr = 0; mcu->samplesError = 0;
  }
}''')
    replace_function('Source/PluginProcessor.cpp','void VirtualJVProcessor::processBlock', '''{
  buffer.clear();
  if (!loaded || buffer.getNumChannels() < 2 || getSampleRate() <= 0) return;
  if (!mcuLock.tryEnter()) {
    ++skippedBlocks;
    if (deferredMidi.getNumEvents() + midiMessages.getNumEvents() <= 2048)
      for (const auto m : midiMessages) deferredMidi.addEvent(m.getMessage(), 0);
    else { ++midiDropped; deferredMidi.clear(); panicRequested.store(true); }
    return;
  }
  juce::ScopedNoDenormals noDenormals;
  if (panicRequested.exchange(false)) {
    for (int ch=0; ch<16; ++ch) {
      const uint8_t sustain[]={(uint8_t)(0xb0|ch),64,0};
      const uint8_t off[]={(uint8_t)(0xb0|ch),120,0};
      mcu->postMidiSC55(sustain,3); mcu->postMidiSC55(off,3);
    }
  }
  auto enqueue = [this](const juce::MidiBuffer& messages, bool immediate) {
    for (const auto metadata : messages) {
      auto message=metadata.getMessage();
      if (message.getChannel()>0) message.setChannel(status.isDrums ? 10 : 1);
      if (message.getRawDataSize() >= MIDI_EVENT_DATA_SIZE || mcu->midiQueue.size() >= 2048) {++midiDropped; continue;}
      const int pos=immediate ? 0 : (int)(metadata.samplePosition*64000.0/getSampleRate());
      mcu->enqueueMidiSC55(message.getRawData(),message.getRawDataSize(),pos);
    }
  };
  enqueue(deferredMidi,true); deferredMidi.clear(); enqueue(midiMessages,false);
  mcu->updateSC55WithSampleRate(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),(int)getSampleRate());
  mcuLock.exit();
}''')
    # SysEx checksums are seven-bit, including the zero-sum case.
    change('Source/PluginProcessor.cpp','checksum = 128 - checksum;','checksum = (128 - checksum) & 127;')
    change('Source/PluginProcessor.cpp','const void *data, int /* sizeInBytes */','const void *data, int sizeInBytes')
    replace_function('Source/PluginProcessor.cpp','void VirtualJVProcessor::setStateInformation', '''{
  if (!loaded || !data || sizeInBytes != (int)sizeof(DataToSave)) return;
  DataToSave next; memcpy(&next,data,sizeof(next));
  if (next.currentExpansion < -1 || next.currentExpansion >= NUM_EXPS) return;
  if (next.currentExpansion >= 0 && !expansionsDescr[next.currentExpansion]) return;
  { const juce::SpinLock::ScopedLockType lock(mcuLock);
    status=next; mcu->nvram[0x0d] |= 1<<5;
    mcu->nvram[0x00]=status.masterTune;
    mcu->nvram[0x02]=status.reverbEnabled | (status.chorusEnabled<<1);
    if(status.currentExpansion>=0) memcpy(mcu->pcm.waverom_exp,expansionsDescr[status.currentExpansion],0x800000);
    mcu->nvram[0x11]=status.isDrums ? 0 : 1;
    memcpy(&mcu->nvram[0x67f0],status.drums,0xa7c);
    memcpy(&mcu->nvram[0x0d70],status.patch,0x16a); mcu->SC55_Reset();
  }
  if(auto* e=dynamic_cast<VirtualJVEditor*>(getActiveEditor())) {
    e->showToneOrRhythmEditTabs(status.isDrums); e->updateEditTabs();
  }
}''')
    # NVRAM is writable; program and wave ROM checks remain mandatory.
    change('Source/rom.cpp','romI < romCountChk))','romI < romCountChk && romI != 0))')
    change('Source/rom.cpp','bool preloadAll(std::array<uint8_t *, romCount> &cache) {','bool preloadAll(std::array<uint8_t *, romCount> &cache) {\n  for (auto& info : romInfos) info.loaded = false;')
    change('Source/rom.cpp','if (!loadRom(i, nullptr, cache) && i < romCountRequired)\n      return false;','loadRom(i, nullptr, cache);')
    change('Source/rom.cpp','  return true;\n}\n\nvoid unscrambleRom','  for (int i=0; i<(int)romCountRequired; ++i) if (!romInfos[i].loaded) return false;\n  return true;\n}\n\nvoid unscrambleRom')
    # Deterministic reset and resampler lifetime.
    change('Source/emulator/mcu.cpp','memset(ad_val, 0x00, sizeof(ga_int));','memset(ad_val, 0x00, sizeof(ad_val));')
    change('Source/emulator/mcu.h','    MCU();','    MCU();\n    ~MCU();')
    change('Source/emulator/mcu.cpp','MCU::MCU() : pcm(this), lcd(this), mcu_timer(this), sub_mcu(this) {}','''MCU::MCU() : pcm(this), lcd(this), mcu_timer(this), sub_mcu(this) { midiQueue.reserve(2048); }
MCU::~MCU() { if(resampleL) resample_close(resampleL); if(resampleR) resample_close(resampleR); }''')
    # Preserve the upstream 64-kHz carry fix; avoid rescanning already delivered MIDI events.
    change('Source/emulator/mcu.cpp','    int maxCycles = nFrames * 256;','    int maxCycles = nFrames * 256;\n    size_t nextMidiEvent = 0;')
    change('Source/emulator/mcu.cpp','''       for (int j = 0; j < midiQueue.size(); j++) {
           if (!midiQueue[j].processed && midiQueue[j].samplePos <= sample_write_ptr) {
               postMidiSC55(midiQueue[j].data, midiQueue[j].length);
               midiQueue[j].processed = true;
           }
       }''','''       while(nextMidiEvent < midiQueue.size() && midiQueue[nextMidiEvent].samplePos <= sample_write_ptr) {
           auto& event=midiQueue[nextMidiEvent++];
           postMidiSC55(event.data,event.length); event.processed=true;
       }''')
    # Wrap the original editing pages in viewports rather than scaling all controls down.
    change('Source/PluginEditor.h','    SettingsTab settingsTab;','    SettingsTab settingsTab;\n    std::array<juce::Viewport, 8> pages;')
    change('Source/PluginEditor.cpp','    setSize(820, 900);','''    juce::Component* content[] = {&patchBrowser,&settingsTab,&editCommonTab,&editTone1Tab,&editTone2Tab,&editTone3Tab,&editTone4Tab,&editRhythmTab};
    for(int i=0;i<8;++i) { pages[i].setViewedComponent(content[i],false); pages[i].setScrollBarsShown(true,true); pages[i].setScrollBarThickness(16); }
    tabs.setTabBarDepth(38);
    setSize(1100,600);''')
    for idx,name in enumerate(['patchBrowser','settingsTab','editCommonTab','editTone1Tab','editTone2Tab','editTone3Tab','editTone4Tab','editRhythmTab']):
        file=root/'Source/PluginEditor.cpp'; text=file.read_text(); text=text.replace('&'+name+', false)', '&pages['+str(idx)+'], false)'); file.write_text(text)
    replace_function('Source/PluginEditor.cpp','void VirtualJVEditor::resized()', '''{
    lcd.setBounds(0,0,getWidth(),60);
    tabs.setBounds(0,60,getWidth(),juce::jmax(0,getHeight()-60));
    for(int i=0;i<8;++i) {
        if(auto* content=pages[i].getViewedComponent()) {
            const int w=juce::jmax(820,tabs.getWidth()-24);
            const int h=i==0 ? juce::jmax(200,tabs.getHeight()-42) : (i==1 ? 260 : 800);
            content->setSize(w,h);
        }
    }
}''')
    change('Source/PluginEditor.cpp','void VirtualJVEditor::updateEditTabs()\n{','void VirtualJVEditor::updateEditTabs()\n{\n    const juce::SpinLock::ScopedLockType lock(processor.mcuLock);')
    change('Source/ui/PatchBrowser.h','const int columns = 6;','const int columns = 3;')
    change('Source/ui/PatchBrowser.h','const int rowPerColumn = 44;','const int rowPerColumn = 128;')
    change('Source/ui/PatchBrowser.h','''      return std::min(
          endI - startI,
          (int)parent->processor.patchInfoPerGroup[groupI].size() -
              startI);''','''      if (groupI < 0 || groupI >= (int)parent->processor.patchInfoPerGroup.size()) return 0;
      return std::max(0, std::min(endI-startI,(int)parent->processor.patchInfoPerGroup[groupI].size()-startI));''')
    change('Source/ui/PatchBrowser.h','void selectedRowsChanged(int lastRowSelected) override {','void selectedRowsChanged(int lastRowSelected) override {\n      if (lastRowSelected < 0 || groupI < 0 || groupI >= (int)parent->processor.patchInfoPerGroup.size()) return;')
    change('Source/ui/PatchBrowser.cpp','patchesListBoxes[i]->setRowHeight(17);','patchesListBoxes[i]->setRowHeight(34);')
    # The display copies only its small register state under the lock. Rendering is outside it.
    change('Source/ui/widgets/LCDisplay.h','    Color lcdColor;','    Color lcdColor = Color::Green;\n    std::unique_ptr<LCD> displayCopy;\n    juce::Image displayImage;')
    change('Source/ui/widgets/LCDisplay.cpp','  redrawTimer.startTimerHz(25);','''  displayCopy = std::make_unique<LCD>(p.mcu);
  displayCopy->lcd_width=820; displayCopy->lcd_height=100;
  displayImage=juce::Image(juce::Image::ARGB,820,100,true);
  redrawTimer.startTimerHz(12);''')
    replace_function('Source/ui/widgets/LCDisplay.cpp','void LCDisplay::paint', '''{
  if (!processor.loaded) return;
  if(processor.mcuLock.tryEnter()) {
    auto& live=processor.mcu->lcd;
    constexpr size_t bytes=offsetof(LCD,lcd_buffer)-offsetof(LCD,LCD_DL);
    memcpy(&displayCopy->LCD_DL,&live.LCD_DL,bytes);
    processor.mcuLock.exit();
    if(auto* pixels=displayCopy->LCD_Update()) {
      juce::Image::BitmapData map(displayImage,juce::Image::BitmapData::writeOnly);
      for(int y=0;y<100;++y) {
        auto* dst=reinterpret_cast<uint32_t*>(map.getLinePointer(y));
        for(int x=0;x<820;++x) dst[x]=pixels[y*1024+x] | 0xff000000u;
      }
    }
  }
  g.drawImageWithin(displayImage,0,0,getWidth(),getHeight(),juce::RectanglePlacement::centred);
}''')
    p=root/'Source/ui/widgets/LCDisplay.cpp'; p.write_text(p.read_text().replace('processor.mcu->lcd.lcd_','displayCopy->lcd_'))
    (root/'ANDROID-PATCHES.txt').write_text('RiJV880 Android Test 0.1.0: see patch_upstream.py for all platform and correctness changes. No ROMs bundled.\n')

if __name__ == '__main__': patch(Path(sys.argv[1]))
