"""UI-only fix: passive refresh must never send a MIDI edit. No ROM/core changes."""
from pathlib import Path
import hashlib
import json


def change(text: str, old: str, new: str, count: int = 1) -> str:
    if text.count(old) != count:
        raise RuntimeError(f'Patch UI anchor count mismatch: {old[:90]!r}')
    return text.replace(old, new)


def patch_ui(root: Path) -> None:
    src = root / 'Source'
    before = {str(f.relative_to(src)): hashlib.sha256(f.read_bytes()).hexdigest()
              for f in src.rglob('*') if f.is_file()}
    for klass, menu_type in [('EditToneTab', 'juce::ComboBox'), ('EditRhythmTab', 'Menu')]:
        header = src / 'ui' / (klass + '.h')
        h = header.read_text(encoding='utf-8-sig')
        old = f'void updateWaveformComboBox({menu_type}& wfMenu);' if klass == 'EditRhythmTab' else 'void updateWaveformComboBox(juce::ComboBox &wfMenu);'
        h = change(h, old, f'void updateWaveformComboBox({menu_type}& wfMenu, juce::NotificationType notification = juce::dontSendNotification);\n    int cachedWaveRom = -1;')
        header.write_text(h)
        path = src / 'ui' / (klass + '.cpp')
        t = path.read_text()
        begin = t.index(f'void {klass}::updateWaveformComboBox(')
        end = t.index(f'void {klass}::updateValues()', begin)
        t = t[:begin] + f'''void {klass}::updateWaveformComboBox({menu_type}& wfMenu, juce::NotificationType notification)
{{
    if (!editor) {{ wfMenu.clear(juce::dontSendNotification); cachedWaveRom = -1; return; }}
    const auto romIdx = waveGroupComboBox.getSelectedItemIndex() == 0 ? 2U : editor->getSelectedRomIdx();
    // Reuse immutable waveform names; never touch ROM contents or the engine.
    if (cachedWaveRom == (int)romIdx && wfMenu.getNumItems() > 0) return;
    const int priorSelection = wfMenu.getSelectedItemIndex();
    auto names = processor.readMultisampleNames(romIdx);
    wfMenu.clear(juce::dontSendNotification);
    for (int i = 0; i < (int)names.size(); ++i) {{
        if (i > 0 && i % 32 == 0) wfMenu.getRootMenu()->addColumnBreak();
        wfMenu.addItem(std::to_string(i + 1) + ": " + names[i], i + 1);
    }}
    cachedWaveRom = names.empty() ? -1 : (int)romIdx;
    if (priorSelection >= 0 && wfMenu.getNumItems() > 0) {{
        const int newSelection = std::clamp(priorSelection, 0, wfMenu.getNumItems() - 1);
        // Passive refresh is silent. An actual user group edit explicitly notifies.
        wfMenu.setSelectedItemIndex(newSelection, notification);
    }}
}}

''' + t[end:]
        t = t.replace('waveGroupComboBox.setSelectedItemIndex(0);', 'waveGroupComboBox.setSelectedItemIndex(0, juce::dontSendNotification);')
        split = t.index(f'void {klass}::comboBoxChanged(')
        t = t[:split] + change(t[split:], 'updateWaveformComboBox(waveformComboBox);', 'updateWaveformComboBox(waveformComboBox, juce::sendNotificationAsync);')
        path.write_text(t)
    f = src / 'PluginEditor.cpp'
    t = f.read_text()
    t = change(t, '    const juce::SpinLock::ScopedLockType lock(processor.mcuLock);\n    editCommonTab.updateValues();', '''    // Editor patch/drum state is written on the message thread; imported name
    // tables are immutable. Settings snapshots its live bytes under a short lock.
    // Do not hold the engine lock while updating hundreds of graphical controls.
    editCommonTab.updateValues();''')
    t = change(t, 'void VirtualJVEditor::showToneOrRhythmEditTabs(const bool isRhythm)\n{', '''void VirtualJVEditor::showToneOrRhythmEditTabs(const bool isRhythm)
{
    // A patch change within the same mode does not require rebuilding tabs.
    if (tabs.getNumTabs() == (isRhythm ? 4 : 7)) return;''')
    f.write_text(t)
    f = src / 'ui' / 'SettingsTab.cpp'
    t = f.read_text()
    start = t.index('void SettingsTab::updateValues()')
    end = t.index('void SettingsTab::resized()', start)
    t = t[:start] + '''void SettingsTab::updateValues()
{
  int tune;
  uint8_t effects;
  {
    const juce::SpinLock::ScopedLockType lock(processor.mcuLock);
    tune = static_cast<int8_t>(processor.mcu->nvram[0x00]) + 64;
    effects = processor.mcu->nvram[0x02];
  }
  masterTuneSlider.setValue(tune, juce::dontSendNotification);
  reverbToggle.setToggleState((effects >> 0) & 1, juce::dontSendNotification);
  chorusToggle.setToggleState((effects >> 1) & 1, juce::dontSendNotification);
}

''' + t[end:]
    f.write_text(t)
    after = {str(f.relative_to(src)): hashlib.sha256(f.read_bytes()).hexdigest()
             for f in src.rglob('*') if f.is_file()}
    allowed = {'PluginEditor.cpp', 'ui/SettingsTab.cpp', 'ui/EditToneTab.cpp',
               'ui/EditToneTab.h', 'ui/EditRhythmTab.cpp', 'ui/EditRhythmTab.h'}
    changed = {name for name in before.keys() | after.keys() if before.get(name) != after.get(name)}
    if changed != allowed:
        raise RuntimeError(f'UI-only scope assertion failed: {sorted(changed)}')
    report = {'changed_files': sorted(changed), 'unchanged_source_files': len(before) - len(changed),
              'rom_loader_unchanged': before['rom.cpp'] == after['rom.cpp'],
              'plugin_processor_unchanged': before['PluginProcessor.cpp'] == after['PluginProcessor.cpp'],
              'core_unchanged': all(before[k] == after[k] for k in before if k.startswith('emulator/')),
              'lcd_unchanged': before['ui/widgets/LCDisplay.cpp'] == after['ui/widgets/LCDisplay.cpp'],
              'comparison': 'Byte hashes before/after UI patch, on regenerated 0.1.3 sources.'}
    (root / 'PATCH-UI-SCOPE.json').write_text(json.dumps(report, indent=2) + '\n')
