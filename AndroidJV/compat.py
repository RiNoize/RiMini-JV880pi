"""Idempotent JUCE 8 platform integration adjustments, applied before compilation."""
from pathlib import Path

def prepare(folder: Path):
    replacements = {
        'Main.cpp': [
            ('    juce::String getApplicationName()', '    const juce::String getApplicationName()'),
            ('    juce::String getApplicationVersion()', '    const juce::String getApplicationVersion()'),
            ('safe->engine->createEditor()', 'safe->engine->createEditorIfNeeded()'),
            ('juce::File("jv880_nvram.bin")', 'RiRom::directory().getChildFile("jv880_nvram.bin")'),
            ('int count=engine->getNumPrograms(), index=engine->getCurrentProgram();', 'int count=engine->getNumPrograms(), index=engine->getCurrentProgram();\n        if(index < 0) index = delta > 0 ? -1 : 0;'),
            ('result->message += "Motor preparado. Pulsa Iniciar audio.";', 'result->message += result->processor->mcu->midi_ready ? "Motor preparado. Pulsa Iniciar audio." : "ROMs cargadas; arranque MIDI aun no confirmado. Pulsa Iniciar audio.";'),
        ],
        'JuceHeader.h': [('#include <cstring>', '#include <cstring>\n#include <stdexcept>\n#include <cstddef>')],
        'LEEME.txt': [('multitimb rico', 'multitimbrico')],
    }
    for name, changes in replacements.items():
        file=folder/name; text=file.read_text()
        for old,new in changes:
            if new in text: continue
            if old not in text: raise RuntimeError(f'{name}: compatibility anchor missing: {old}')
            text=text.replace(old,new)
        file.write_text(text)
