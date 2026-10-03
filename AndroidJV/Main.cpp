// RiJV880 Android Test: platform integration; the VirtualJV emulator is retained.
#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RomImport.h"
#include <thread>

class MainPanel final : public juce::Component, private juce::Timer,
                        private juce::AudioIODeviceCallback, private juce::MidiInputCallback {
public:
    MainPanel() : keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
        setLookAndFeel(&look);
        setOpaque(true);
        title.setText("RiJV880  |  Android Test 0.1.0", juce::dontSendNotification);
        title.setFont(juce::FontOptions(18.0f));
        for (auto* c : std::initializer_list<juce::Component*>{&title,&importButton,&audioButton,&panicButton,&midi,&previous,&next,&volume,&buffer,&keysButton,&exportButton,&patchName,&info,&metrics,&keyboard}) addAndMakeVisible(c);
        importButton.onClick=[this]{ chooseRoms(); };
        audioButton.onClick=[this]{ if (running) stopAudio(); else startAudio(); };
        panicButton.onClick=[this]{ panic(); };
        previous.onClick=[this]{ changePatch(-1); };
        next.onClick=[this]{ changePatch(1); };
        exportButton.onClick=[this]{ exportNvram(); };
        keysButton.setClickingTogglesState(true);
        keysButton.setToggleState(false, juce::dontSendNotification);
        keysButton.onClick=[this]{ resized(); };
        volume.setSliderStyle(juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
        volume.setRange(0.0, 100.0, 1.0);
        volume.setTextValueSuffix(" %");
        volume.setValue(70.0, juce::dontSendNotification);
        volume.onValueChange=[this]{ gain.store((float)volume.getValue()/100.0f); };
        buffer.addItem("256 muestras",256); buffer.addItem("512 muestras",512); buffer.addItem("1024 muestras",1024);
        buffer.setSelectedId(512, juce::dontSendNotification);
        buffer.onChange=[this]{ if (running) {stopAudio(); startAudio();} };
        midi.onChange=[this]{ selectMidi(); };
        info.setJustificationType(juce::Justification::centredLeft);
        metrics.setJustificationType(juce::Justification::centredLeft);
        info.setFont(juce::FontOptions(14.0f)); metrics.setFont(juce::FontOptions(13.0f));
        keyboard.setAvailableRange(36,95); keyboard.setLowestVisibleKey(48); keyboard.setKeyWidth(35.0f);
        keyboard.setVisible(false);
        audio.addMidiInputDeviceCallback({}, this);
        midiCollector.reset(48000.0);
        midiBlock.ensureSize(65536);
        setSize(1200,720);
        scanMidi(); startTimerHz(4); load({});
    }
    ~MainPanel() override {
        stopTimer(); stopAudio();
        audio.removeMidiInputDeviceCallback({}, this);
        for (const auto& d : midiDevices) audio.setMidiInputDeviceEnabled(d.identifier, false);
        if (loader.joinable()) loader.join();
        editor.reset(); engine.reset(); setLookAndFeel(nullptr);
    }
    void suspend() { stopAudio(); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff161c23)); }
    void resized() override {
        auto area=getLocalBounds().reduced(8);
        auto r=area.removeFromTop(38);
        title.setBounds(r.removeFromLeft(juce::roundToInt(area.getWidth()*0.27f)));
        importButton.setBounds(r.removeFromLeft(juce::roundToInt(area.getWidth()*0.15f)).reduced(2));
        audioButton.setBounds(r.removeFromLeft(juce::roundToInt(area.getWidth()*0.13f)).reduced(2));
        panicButton.setBounds(r.removeFromRight(juce::roundToInt(area.getWidth()*0.11f)).reduced(2));
        midi.setBounds(r.reduced(4));
        area.removeFromTop(4); r=area.removeFromTop(36);
        previous.setBounds(r.removeFromLeft(40).reduced(2)); next.setBounds(r.removeFromLeft(40).reduced(2));
        exportButton.setBounds(r.removeFromRight(juce::roundToInt(area.getWidth()*0.16f)).reduced(2));
        keysButton.setBounds(r.removeFromRight(80).reduced(2));
        buffer.setBounds(r.removeFromRight(juce::roundToInt(area.getWidth()*0.15f)).reduced(2));
        volume.setBounds(r.removeFromRight(juce::roundToInt(area.getWidth()*0.18f)).reduced(2));
        patchName.setBounds(r.reduced(2));
        metrics.setBounds(area.removeFromBottom(24));
        info.setBounds(area.removeFromBottom(26));
        keyboard.setVisible(keysButton.getToggleState());
        if (keysButton.getToggleState()) keyboard.setBounds(area.removeFromBottom(80).reduced(2));
        area.removeFromTop(4);
        if (editor) editor->setBounds(area);
    }
private:
    struct LoadResult { std::unique_ptr<VirtualJVProcessor> processor; juce::String message; };
    void load(juce::Array<juce::URL> urls) {
        if (busy) return;
        stopAudio(); editor.reset(); engine.reset();
        if (loader.joinable()) loader.join();
        busy=true; enableControls(false);
        info.setText("Preparando ROMs y motor...", juce::dontSendNotification);
        auto safe=juce::Component::SafePointer<MainPanel>(this);
        loader=std::thread([safe,urls]{
            auto result=std::make_shared<LoadResult>();
            try {
                result->message=RiRom::importFiles(urls);
                result->processor=std::make_unique<VirtualJVProcessor>();
                if (result->processor->loaded) {
                    // Let the emulated firmware finish booting before accepting notes.
                    float left[512]{},right[512]{};
                    for (int i=0;i<400 && !result->processor->mcu->midi_ready;++i)
                        result->processor->mcu->updateSC55WithSampleRate(left,right,512,48000);
                    result->message += "Motor preparado. Pulsa Iniciar audio.";
                } else result->message += "Faltan ROMs validas: " + RiRom::missing();
            } catch (const std::exception& e) { result->message="Error: "+juce::String(e.what()); }
            juce::MessageManager::callAsync([safe,result]{
                if (!safe) return;
                if (safe->loader.joinable()) safe->loader.join();
                safe->engine=std::move(result->processor); safe->busy=false;
                if (safe->engine && safe->engine->loaded) {
                    safe->editor.reset(safe->engine->createEditor());
                    safe->addAndMakeVisible(*safe->editor);
                }
                safe->enableControls(safe->engine && safe->engine->loaded);
                safe->info.setText(result->message,juce::dontSendNotification);
                safe->resized();
            });
        });
    }
    void enableControls(bool ready) {
        importButton.setEnabled(!busy); audioButton.setEnabled(ready); panicButton.setEnabled(ready);
        previous.setEnabled(ready); next.setEnabled(ready); exportButton.setEnabled(ready);
    }
    void chooseRoms() {
        if (busy) return;
        stopAudio();
        chooser=std::make_unique<juce::FileChooser>("Importar ZIP o ROMs del JV-880",juce::File(),"*",true);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectMultipleItems,
            [safe=juce::Component::SafePointer<MainPanel>(this)](const juce::FileChooser& c){
                if (safe && !c.getURLResults().isEmpty()) safe->load(c.getURLResults());
            });
    }
    void exportNvram() {
        if (!engine || !engine->loaded || busy) return;
        stopAudio();
        auto bytes=std::make_shared<juce::MemoryBlock>(engine->mcu->nvram,32768);
        chooser=std::make_unique<juce::FileChooser>("Exportar NVRAM del JV-880",juce::File("jv880_nvram.bin"),"*.bin",true);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,
            [safe=juce::Component::SafePointer<MainPanel>(this),bytes](const juce::FileChooser& c){
                if (!safe || c.getURLResult().isEmpty()) return;
                auto output=c.getURLResult().createOutputStream();
                const bool ok=output && output->write(bytes->getData(),bytes->getSize());
                if (output) output->flush();
                safe->info.setText(ok?"NVRAM exportada. Pulsa Iniciar audio para continuar.":"No se pudo exportar la NVRAM.",juce::dontSendNotification);
            });
    }
    void startAudio() {
        if (busy || running || !engine || !engine->loaded) return;
        auto error=audio.initialise(0,2,nullptr,true);
        if (error.isNotEmpty()) { info.setText(error,juce::dontSendNotification); return; }
        auto setup=audio.getAudioDeviceSetup(); setup.bufferSize=buffer.getSelectedId();
        // Keep the device's native sample rate and resample from the emulated 64 kHz domain.
        error=audio.setAudioDeviceSetup(setup,true);
        audio.addAudioCallback(this); running=true; audioButton.setButtonText("Detener");
        selectMidi();
        info.setText(error.isEmpty()?"Audio activo. Sustain CC64 y MIDI USB habilitados.":"Se usa el buffer nativo: "+error,juce::dontSendNotification);
    }
    void stopAudio() {
        if (running) { audio.removeAudioCallback(this); audio.closeAudioDevice(); }
        running=false; audioButton.setButtonText("Iniciar audio");
        keyboardState.reset();
        midiCollector.reset(sampleRate.load()>0?sampleRate.load():48000.0);
        if (engine) engine->panicRequested.store(true);
    }
    void panic() { keyboardState.allNotesOff(0); if(engine) engine->panicRequested.store(true); }
    void changePatch(int delta) {
        if (!engine || !engine->loaded) return;
        panic();
        int count=engine->getNumPrograms(), index=engine->getCurrentProgram();
        if (count>0) engine->setCurrentProgram((index+delta+count)%count);
    }
    void scanMidi() {
        auto found=juce::MidiInput::getAvailableDevices();
        juce::String signature;
        for(const auto& d:found) signature += d.identifier+";";
        if(signature==midiSignature && midi.getNumItems()>0) return;
        midiSignature=signature; midiDevices=found;
        midi.clear(juce::dontSendNotification);
        midi.addItem(found.isEmpty()?"Sin dispositivo MIDI":"MIDI: todos",1);
        for(int i=0;i<found.size();++i) midi.addItem(found[i].name,i+2);
        midi.setSelectedId(1,juce::dontSendNotification); selectMidi();
    }
    void selectMidi() {
        for(int i=0;i<midiDevices.size();++i)
            audio.setMidiInputDeviceEnabled(midiDevices[i].identifier,midi.getSelectedId()==1 || midi.getSelectedId()==i+2);
    }
    void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& message) override {
        midiEvents.fetch_add(1,std::memory_order_relaxed); midiCollector.addMessageToQueue(message);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        const double sr=device->getCurrentSampleRate(); sampleRate.store(sr);
        actualBuffer.store(device->getCurrentBufferSizeSamples());
        midiCollector.reset(sr); midiBlock.clear(); keyboardState.reset();
        if(engine) {engine->setRateAndBufferSizeDetails(sr,device->getCurrentBufferSizeSamples());engine->prepareToPlay(sr,device->getCurrentBufferSizeSamples());}
    }
    void audioDeviceStopped() override { if(engine) engine->releaseResources(); }
    void audioDeviceError(const juce::String& message) override {
        juce::MessageManager::callAsync([safe=juce::Component::SafePointer<MainPanel>(this),message]{if(safe) safe->info.setText(message,juce::dontSendNotification);});
    }
    void audioDeviceIOCallbackWithContext(const float* const*,int,float* const* output,int channels,int samples,const juce::AudioIODeviceCallbackContext&) override {
        for(int c=0;c<channels;++c) if(output[c]) juce::FloatVectorOperations::clear(output[c],samples);
        if(!engine || !engine->loaded || channels<2 || !output[0] || !output[1] || samples<=0) return;
        auto t=juce::Time::getHighResolutionTicks();
        float* pointers[]={output[0],output[1]}; juce::AudioBuffer<float> block(pointers,2,samples);
        midiBlock.clear(); midiCollector.removeNextBlockOfMessages(midiBlock,samples);
        keyboardState.processNextMidiBuffer(midiBlock,0,samples,true);
        engine->processBlock(block,midiBlock); block.applyGain(gain.load());
        float peak=0.0f;
        for(int c=0;c<2;++c) for(int i=0;i<samples;++i) {
            float& value=output[c][i];
            if(!std::isfinite(value)) {value=0;invalidSamples.fetch_add(1,std::memory_order_relaxed);}
            peak=std::max(peak,std::abs(value)); value=juce::jlimit(-1.0f,1.0f,value);
        }
        signalPeak.store(peak);
        const double elapsed=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-t);
        const double ratio=elapsed*sampleRate.load()/samples;
        loadAverage.store(loadAverage.load()*0.95+ratio*0.05);
        if(ratio>loadPeak.load()) loadPeak.store(ratio);
        if(ratio>1.0) lateBlocks.fetch_add(1,std::memory_order_relaxed);
    }
    void timerCallback() override {
        if(++ticks%8==0) scanMidi();
        if(engine && engine->loaded) {
            auto name=engine->status.isDrums?juce::String("Rhythm Set"):juce::String((const char*)engine->status.patch,12);
            patchName.setText(name,juce::dontSendNotification);
        }
        int xruns=-1;
        if(auto* d=audio.getCurrentAudioDevice()) xruns=d->getXRunCount();
        juce::String text=juce::String(sampleRate.load(),0)+" Hz | buffer "+juce::String(actualBuffer.load())
            +" | DSP "+juce::String(loadAverage.load()*100,1)+"% / pico "+juce::String(loadPeak.load()*100,1)
            +"% | tarde "+juce::String((juce::int64)lateBlocks.load())+" | xrun "+(xruns<0?juce::String("n/d"):juce::String(xruns))
            +" | MIDI "+juce::String((juce::int64)midiEvents.load());
        if(engine) text += " | bloqueo "+juce::String((juce::int64)engine->skippedBlocks.load())+" | MIDI omitido "+juce::String((juce::int64)engine->midiDropped.load());
        metrics.setText(text,juce::dontSendNotification);
    }
    juce::LookAndFeel_V4 look;
    juce::Label title,patchName,info,metrics;
    juce::TextButton importButton{"Importar ROMs"},audioButton{"Iniciar audio"},panicButton{"Panico"},previous{"<"},next{">"},keysButton{"Teclado"},exportButton{"Exportar NVRAM"};
    juce::ComboBox midi,buffer; juce::Slider volume;
    juce::AudioDeviceManager audio; juce::MidiMessageCollector midiCollector; juce::MidiBuffer midiBlock;
    juce::MidiKeyboardState keyboardState; juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<VirtualJVProcessor> engine; std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::FileChooser> chooser; std::thread loader;
    juce::Array<juce::MidiDeviceInfo> midiDevices; juce::String midiSignature;
    std::atomic<float> gain{0.7f},signalPeak{0};
    std::atomic<double> sampleRate{48000},loadAverage{0},loadPeak{0};
    std::atomic<int> actualBuffer{0};
    std::atomic<uint64_t> lateBlocks{0},midiEvents{0},invalidSamples{0};
    bool running=false,busy=false; int ticks=0;
};
class RiJVApplication final : public juce::JUCEApplication {
public:
    juce::String getApplicationName() override {return "RiJV880 Test";}
    juce::String getApplicationVersion() override {return "0.1.0";}
    void initialise(const juce::String&) override { window=std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
    void suspended() override { if(window) window->panel->suspend(); }
    class Window final : public juce::DocumentWindow {
    public:
        Window():DocumentWindow("RiJV880",juce::Colour(0xff161c23),0) {
            setUsingNativeTitleBar(false);setTitleBarHeight(0);
            panel=new MainPanel();setContentOwned(panel,true);setFullScreen(true);setVisible(true);
        }
        void closeButtonPressed() override {juce::JUCEApplication::getInstance()->systemRequestedQuit();}
        MainPanel* panel=nullptr;
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(RiJVApplication)
