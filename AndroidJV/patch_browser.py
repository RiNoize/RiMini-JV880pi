"""Guard browser rows across expansion changes. UI only; no ROM/DSP modifications."""
from pathlib import Path
import hashlib
import json


def replace_function(text: str, signature: str, body: str) -> str:
    begin = text.index(signature)
    left = text.index('{', begin)
    depth, end = 1, left + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[:left] + body + text[end:]


def patch_browser(root: Path) -> None:
    src = root / 'Source'
    before = {str(f.relative_to(src)): hashlib.sha256(f.read_bytes()).hexdigest()
              for f in src.rglob('*') if f.is_file()}
    legacy = root / 'browser-v014'
    legacy.mkdir(exist_ok=True)
    for name in ('PatchBrowser.h', 'PatchBrowser.cpp'):
        (legacy / name).write_bytes((src / 'ui' / name).read_bytes())
    h = src / 'ui/PatchBrowser.h'
    t = h.read_text()
    assert 'bool changingGroup' not in t, 'Patch must apply to fresh generated 0.1.4 sources'
    t = t.replace('VirtualJVProcessor &processor;', 'VirtualJVProcessor &processor;\n  bool changingGroup = false;')
    t = t.replace('if (rowNumber < NUM_EXPS + 1)',
                  'if (rowNumber >= 0 && rowNumber < NUM_EXPS + 1 && rowNumber < (int)std::size(groupNames))')
    start = t.index('    int getNumRows() override {', t.index('class PatchesListModel'))
    end = t.index('    void paintListBoxItem(', start)
    t = t[:start] + '''    // JUCE explicitly permits paint callbacks beyond getNumRows(), including
    // recycled rows while an expansion's shorter/empty list is being displayed.
    const std::vector<VirtualJVProcessor::PatchInfo*>* currentGroup() const {
      if (!parent->processor.loaded || groupI < 0 || groupI > NUM_EXPS ||
          (size_t)groupI >= parent->processor.patchInfoPerGroup.size()) return nullptr;
      if (groupI > 0) {
        const size_t rom = romCountRequired + (size_t)groupI;
        if (rom >= romCount || !romInfos[rom].loaded) return nullptr;
        if (groupI == 1 && !romInfos[romCountRequired].loaded) return nullptr;
      }
      return &parent->processor.patchInfoPerGroup[(size_t)groupI];
    }

    int getNumRows() override {
      const auto* group = currentGroup();
      if (!group || startI < 0 || endI <= startI || (size_t)startI >= group->size()) return 0;
      return (int)std::min((size_t)(endI - startI), group->size() - (size_t)startI);
    }

    VirtualJVProcessor::PatchInfo* patchAtRow(int row) const {
      const auto* group = currentGroup();
      if (!group || row < 0 || startI < 0 || endI <= startI ||
          (size_t)startI >= group->size()) return nullptr;
      const size_t count = std::min((size_t)(endI - startI), group->size() - (size_t)startI);
      if ((size_t)row >= count) return nullptr; // Check BEFORE forming row + startI.
      auto* info = (*group)[(size_t)startI + (size_t)row];
      if (!info || !info->present || !info->name || info->nameLength <= 0 ||
          info->nameLength > 128 || info->iInList < 0 ||
          info->iInList >= parent->processor.getNumPrograms()) return nullptr;
      return info;
    }

''' + t[end:]
    start = t.index('    void paintListBoxItem(', t.index('class PatchesListModel'))
    end = t.index('    void changeListenerCallback(', start)
    t = t[:start] + '''    void paintListBoxItem(int rowNumber, juce::Graphics &g, int width,
                          int height, bool rowIsSelected) override {
      g.fillAll(rowIsSelected ? juce::Colour(0xff42A2C8) : juce::Colour(0xff263238));
      const auto* info = patchAtRow(rowNumber);
      if (!info) return; // A placeholder row is blank, never a ROM/patch lookup.
      g.setColour(rowIsSelected ? juce::Colours::black : juce::Colours::white);
      g.drawFittedText(juce::String(info->name, info->nameLength), {5, 0, width, height - 2},
                       juce::Justification::left, 1);
      g.setColour(juce::Colours::white.withAlpha(0.4f));
      g.drawRect(0, height - 1, width, 1);
    }

''' + t[end:]
    t = replace_function(t, '    void changeListenerCallback(', '''{
      if (source != categoriesListModel || !owner) return;
      const juce::ScopedValueSetter<bool> transition(parent->changingGroup, true);
      // Discard the old bank's selection while it still belongs to that bank.
      owner->deselectAllRows();
      groupI = categoriesListBox->getSelectedRow();
      parent->processor.status.selectedRom = groupI;
      parent->processor.status.selectedPatch = -1;
      owner->updateContent();
      owner->getViewport()->setViewPosition(0, 0);
      owner->repaint();
    }''')
    t = replace_function(t, '    void selectedRowsChanged(int lastRowSelected)', '''{
      if (!owner || lastRowSelected < 0 || parent->changingGroup ||
          categoriesListBox->getSelectedRow() != groupI) return;
      auto* info = patchAtRow(owner->getSelectedRow());
      if (!info) return;
      for (size_t i = 0; i < columns; ++i)
        if (parent->patchesListBoxes[i] != owner)
          parent->patchesListBoxes[i]->deselectAllRows();
      parent->processor.status.selectedPatch = owner->getSelectedRow() + startI;
      parent->processor.setCurrentProgram(info->iInList);
    }''')
    h.write_text(t)
    p = src / 'ui/PatchBrowser.cpp'
    t = p.read_text()
    t = t.replace('    delete patchesListModels[i];\n    delete patchesListBoxes[i];', '''    // ListBox must stop using its model before the model is destroyed.
    patchesListBoxes[i]->setModel(nullptr);
    delete patchesListBoxes[i];
    delete patchesListModels[i];''')
    p.write_text(t)
    after = {str(f.relative_to(src)): hashlib.sha256(f.read_bytes()).hexdigest()
             for f in src.rglob('*') if f.is_file()}
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k))
    assert changed == ['ui/PatchBrowser.cpp', 'ui/PatchBrowser.h'], changed
    report = {'base': '0.1.4', 'changed_source_files': changed,
              'unchanged_source_files': len(before) - len(changed),
              'processor_rom_loader_emulator_src_lcd_unchanged': True,
              'before': before, 'after': after}
    (root / 'BROWSER-SAFETY-SCOPE.json').write_text(json.dumps(report, indent=2) + '\n')
