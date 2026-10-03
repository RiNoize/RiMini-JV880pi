"""Apply continuous SRC and lock-free display transfer after the base patches."""
from pathlib import Path
import shutil

def replace(text,old,new):
    if old not in text:raise RuntimeError('Audio patch anchor missing: '+old[:80])
    return text.replace(old,new)

def patch_audio(root:Path,integration:Path):
    source=root/'Source';emu=source/'emulator'
    shutil.copy2(integration/'RiRateConverter.h',emu/'RiRateConverter.h')
    shutil.copy2(integration/'RiSnapshot.h',source/'RiSnapshot.h')
    f=emu/'mcu.h';t=f.read_text()
    t=replace(t,'#include <stdint.h>','#include <stdint.h>\n#include "RiRateConverter.h"')
    t=replace(t,'    void* resampleL = 0;', '''    RiRateConverter rateConverter;
    double lastNativeSeconds=0,lastSrcSeconds=0;
    uint64_t audioRenderErrors=0;
    void* resampleL = 0;''');f.write_text(t)
    f=emu/'mcu.cpp';t=f.read_text()
    t=replace(t,'#include "mcu.h"','#include "mcu.h"\n#include <chrono>')
    start=t.index('void MCU::updateSC55WithSampleRate(');end=t.index('void MCU::SC55_Reset()',start)
    t=t[:start]+'''void MCU::updateSC55WithSampleRate(float *dataL,float *dataR,unsigned int nFrames,int destSampleRate) {
    using Clock=std::chrono::steady_clock;
    if(nFrames==0)return;
    // Prepared before playback; this fallback also permits offline firmware boot.
    if(rateConverter.getRate()!=destSampleRate) {
        rateConverter.prepare(destSampleRate);sample_write_ptr=0;savedDestSampleRate=destSampleRate;
    }
    const int needed=rateConverter.inputNeeded((int)nFrames);
    if(needed<0||needed>audio_buffer_size-2) {
        std::fill_n(dataL,nFrames,0.f);std::fill_n(dataR,nFrames,0.f);++audioRenderErrors;return;
    }
    const auto begin=Clock::now();size_t nextMidiEvent=0;unsigned int iterations=0;
    // Original instruction, interrupt, timer and PCM ordering and clock retained.
    while(sample_write_ptr<needed) {
        if(++iterations>nFrames*512u+4096u) {
            std::fill_n(dataL,nFrames,0.f);std::fill_n(dataR,nFrames,0.f);++audioRenderErrors;return;
        }
        while(nextMidiEvent<midiQueue.size()&&midiQueue[nextMidiEvent].samplePos<=sample_write_ptr) {
            auto& event=midiQueue[nextMidiEvent++];postMidiSC55(event.data,event.length);event.processed=true;
        }
        if(!mcu.ex_ignore)MCU_Interrupt_Handle(this);else mcu.ex_ignore=0;
        if(!mcu.sleep)MCU_ReadInstruction();
        mcu.cycles+=12;pcm.PCM_Update(mcu.cycles);mcu_timer.TIMER_Clock(mcu.cycles);
        if(!mcu_mk1&&!mcu_jv880)sub_mcu.SM_Update(mcu.cycles);
        else{MCU_UpdateUART_RX();MCU_UpdateUART_TX();}
        MCU_UpdateAnalog(mcu.cycles);
    }
    const auto converted=Clock::now();
    const int consumed=rateConverter.process(sample_buffer_l,sample_buffer_r,dataL,dataR,(int)nFrames);
    const int leftover=sample_write_ptr-consumed;
    // PCM posts samples in pairs: retain the unused sample verbatim.
    if(leftover>0) {
        memmove(sample_buffer_l,sample_buffer_l+consumed,leftover*sizeof(float));
        memmove(sample_buffer_r,sample_buffer_r+consumed,leftover*sizeof(float));
    }
    sample_write_ptr=leftover;
    lastNativeSeconds=std::chrono::duration<double>(converted-begin).count();
    lastSrcSeconds=std::chrono::duration<double>(Clock::now()-converted).count();
    for(auto& event:midiQueue)if(!event.processed)postMidiSC55(event.data,event.length);
    midiQueue.clear();
}

'''+t[end:]
    t=replace(t,'void MCU::SC55_Reset() {','void MCU::SC55_Reset() {\n    rateConverter.resetHistory();');f.write_text(t)
    f=source/'PluginProcessor.h';t=f.read_text()
    t=replace(t,'    juce::SpinLock mcuLock;', '''    static constexpr size_t lcdStateBytes=offsetof(LCD,lcd_buffer)-offsetof(LCD,LCD_DL);
    RiSnapshot<lcdStateBytes> lcdSnapshot;
    void publishLcdSnapshot() noexcept {
        RiSnapshot<lcdStateBytes>::Data value;
        memcpy(value.data(),&mcu->lcd.LCD_DL,lcdStateBytes);lcdSnapshot.publish(value);
    }
    std::atomic<double> nativeRatio{0},srcRatio{0};
    std::atomic<uint64_t> renderErrors{0};
    juce::SpinLock mcuLock;''')
    t=replace(t,'#pragma once','#pragma once\n#include "RiSnapshot.h"');f.write_text(t)
    f=source/'PluginProcessor.cpp';t=f.read_text()
    start=t.index('  if (mcu->savedDestSampleRate != rate) {',t.index('void VirtualJVProcessor::prepareToPlay'));end=t.index('\n}',start)
    t=t[:start]+'''  mcu->rateConverter.prepare(rate);mcu->sample_write_ptr=0;mcu->savedDestSampleRate=rate;
  mcu->audioRenderErrors=0;renderErrors.store(0);skippedBlocks.store(0);
  nativeRatio.store(0);srcRatio.store(0);
'''+t[end:]
    anchor='  mcu->updateSC55WithSampleRate(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),(int)getSampleRate());'
    t=replace(t,anchor,anchor+'''
  const double inverseDeadline=getSampleRate()/buffer.getNumSamples();
  nativeRatio.store(nativeRatio.load()*0.95+mcu->lastNativeSeconds*inverseDeadline*0.05);
  srcRatio.store(srcRatio.load()*0.95+mcu->lastSrcSeconds*inverseDeadline*0.05);
  renderErrors.store(mcu->audioRenderErrors);publishLcdSnapshot();''')
    t=replace(t,'  loaded = true;','  loaded = true;\n  publishLcdSnapshot();');f.write_text(t)
    f=source/'ui/widgets/LCDisplay.cpp';t=f.read_text()
    old='''  if(processor.mcuLock.tryEnter()) {
    auto& live=processor.mcu->lcd;
    constexpr size_t bytes=offsetof(LCD,lcd_buffer)-offsetof(LCD,LCD_DL);
    memcpy(&displayCopy->LCD_DL,&live.LCD_DL,bytes);
    processor.mcuLock.exit();'''
    new='''  {
    RiSnapshot<VirtualJVProcessor::lcdStateBytes>::Data value;
    if(processor.lcdSnapshot.read(value))memcpy(&displayCopy->LCD_DL,value.data(),value.size());'''
    f.write_text(replace(t,old,new))
