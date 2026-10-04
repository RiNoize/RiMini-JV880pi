"""Hide only the receive notice in the GUI copy. No firmware/audio/MIDI changes."""
from pathlib import Path
import hashlib
import json


def patch_bulk(root: Path) -> None:
    src = root / 'Source'
    before = {str(p.relative_to(src)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in src.rglob('*') if p.is_file()}
    def change(name: str, old: str, new: str) -> None:
        p = src / name
        text = p.read_text()
        assert text.count(old) == 1, (name, old[:70], text.count(old))
        p.write_text(text.replace(old, new, 1))

    change('ui/widgets/LCDisplay.h', '#include "../../PluginProcessor.h"',
           '#include "../../PluginProcessor.h"\n#include "RiBulkDisplay.h"')
    change('ui/widgets/LCDisplay.h', '    void setLCDColor(const Color color);', '''    void setLCDColor(const Color color);
    bool isBulkNoticeHidden() const noexcept { return hideBulkNotice; }
    void setBulkNoticeHidden(bool enabled);''')
    change('ui/widgets/LCDisplay.h', '    juce::Image displayImage;', '''    juce::Image displayImage;
    bool hideBulkNotice = true;
    RiBulkDisplay::Filter bulkDisplayFilter;''')
    change('ui/widgets/LCDisplay.cpp', '  redrawTimer.startTimerHz(12);', '''  // This preference belongs to Android's UI, not the JV's NVRAM or firmware.
  const auto preference = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("RiJV880-hide-bulk.txt");
  hideBulkNotice = preference.loadFileAsString().trim() != "0";
  redrawTimer.startTimerHz(12);''')
    change('ui/widgets/LCDisplay.cpp', '    if(auto* pixels=displayCopy->LCD_Update()) {', '''    // The snapshot and the emulator remain untouched. Filter a local text copy
    // immediately before rendering, then restore it for the next paint/toggle.
    RiBulkDisplay::Frame live;
    std::memcpy(live.text.data(), displayCopy->LCD_Data, live.text.size());
    std::memcpy(live.glyphs.data(), displayCopy->LCD_CG, live.glyphs.size());
    live.address = displayCopy->LCD_DD_RAM; live.cursor = displayCopy->LCD_C;
    const int program = processor.getCurrentProgram();
    const RiBulkDisplay::Context context{program, processor.status.currentExpansion,
                                         processor.status.isDrums};
    juce::String name;
    if (program >= 0 && program < processor.getNumPrograms())
        name = processor.getProgramName(program);
    else if (!context.rhythm)
        name = juce::String(reinterpret_cast<const char*>(processor.status.patch), 12);
    const auto alternate = RiBulkDisplay::fallback(name.toStdString(), context.rhythm);
    const auto shown = bulkDisplayFilter.select(live, context, alternate, hideBulkNotice);
    std::memcpy(displayCopy->LCD_Data, shown.text.data(), shown.text.size());
    std::memcpy(displayCopy->LCD_CG, shown.glyphs.data(), shown.glyphs.size());
    displayCopy->LCD_DD_RAM = shown.address; displayCopy->LCD_C = shown.cursor;
    auto* pixels = displayCopy->LCD_Update();
    std::memcpy(displayCopy->LCD_Data, live.text.data(), live.text.size());
    std::memcpy(displayCopy->LCD_CG, live.glyphs.data(), live.glyphs.size());
    displayCopy->LCD_DD_RAM = live.address; displayCopy->LCD_C = live.cursor;
    if(pixels) {''')
    change('ui/widgets/LCDisplay.cpp', 'void LCDisplay::setLCDColor(const Color color)', '''void LCDisplay::setBulkNoticeHidden(bool enabled)
{
    if (hideBulkNotice == enabled) return;
    hideBulkNotice = enabled;
    // Called only by the Settings checkbox on the GUI thread, not the audio thread.
    const auto preference = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("RiJV880-hide-bulk.txt");
    preference.replaceWithText(enabled ? "1" : "0");
    repaint();
}

void LCDisplay::setLCDColor(const Color color)''')
    change('ui/SettingsTab.h', '    void updateValues();', '''    void updateValues();
    std::function<void(bool)> onBulkNoticeVisibilityChanged;
    void setBulkNoticeHidden(bool hidden) {
        hideBulkToggle.setToggleState(hidden, juce::dontSendNotification);
    }''')
    change('ui/SettingsTab.h', '    juce::Label buildDateLabel;', '''    juce::Label buildDateLabel;
    juce::ToggleButton hideBulkToggle{"Ocultar aviso Now Bulk Receiving"};''')
    change('ui/SettingsTab.cpp', '  const auto buildTime = juce::Time::getCompilationDate();', '''  addAndMakeVisible(hideBulkToggle);
  hideBulkToggle.setToggleState(true, juce::dontSendNotification);
  hideBulkToggle.setTooltip("Solo cambia el display. Desmarcar para ver los mensajes originales del JV.");
  hideBulkToggle.onClick = [this] {
      if (onBulkNoticeVisibilityChanged)
          onBulkNoticeVisibilityChanged(hideBulkToggle.getToggleState());
  };

  const auto buildTime = juce::Time::getCompilationDate();''')
    change('ui/SettingsTab.cpp', '  buildDateLabel  .setBounds(10, 735, 800, 20);', '''  buildDateLabel  .setBounds(10, 735, 800, 20);
  hideBulkToggle .setBounds(20, 60, juce::jmax(200, getWidth() - 40), 36);''')
    change('PluginEditor.cpp', '    addAndMakeVisible(tabs);', '''    addAndMakeVisible(tabs);
    settingsTab.setBulkNoticeHidden(lcd.isBulkNoticeHidden());
    settingsTab.onBulkNoticeVisibilityChanged = [this](bool hidden) {
        lcd.setBulkNoticeHidden(hidden);
    };''')
    after = {str(p.relative_to(src)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in src.rglob('*') if p.is_file()}
    changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
    assert changed == ['PluginEditor.cpp', 'ui/SettingsTab.cpp', 'ui/SettingsTab.h',
                       'ui/widgets/LCDisplay.cpp', 'ui/widgets/LCDisplay.h'], changed
    (root / 'BULK-DISPLAY-SCOPE.json').write_text(json.dumps({
        'base': '0.1.5', 'changed_source_files': changed,
        'processor_rom_loader_emulator_src_unchanged': True,
        'before': before, 'after': after}, indent=2) + '\n')
