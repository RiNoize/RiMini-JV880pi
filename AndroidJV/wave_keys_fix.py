#!/usr/bin/env python3
"""0.1.7: opt-in left/right navigation for the existing waveform popup only."""
from pathlib import Path
import hashlib
import json

KEY = 'RiJV880.WaveColumns'
METHOD = r'''    // RiJV880 opt-in: waveform columns are a grid, not submenus.
    bool riNavigateWaveColumn (const KeyPress& key)
    {
        const bool left = key.isKeyCode (KeyPress::leftKey);
        if (! left && ! key.isKeyCode (KeyPress::rightKey)) return false;
        if (parent != nullptr || componentAttachedTo == nullptr
            || ! static_cast<bool> (componentAttachedTo->getProperties()
                                        .getWithDefault ("RiJV880.WaveColumns", false)))
            return false;

        // Do not forward an edge key to ComboBox::keyPressed: it would edit the
        // actual value while the menu is still open. Only Enter/click commits.
        disableMouseMovesOnMenuAndAncestors();
        if (currentChild == nullptr)
        {
            selectNextItem (MenuSelectionDirection::current);
            return true;
        }

        const int x = currentChild->getX();
        const int y = currentChild->getBounds().getCentreY();
        ItemComponent* best = nullptr;
        int bestDx = 0, bestDy = 0;
        for (auto* candidate : items)
        {
            if (! canBeTriggered (candidate->item)) continue;
            const int dx = (candidate->getX() - x) * (left ? -1 : 1);
            if (dx <= 0) continue;
            const int dy = std::abs (candidate->getBounds().getCentreY() - y);
            if (best == nullptr || dx < bestDx || (dx == bestDx && dy < bestDy))
            {
                best = candidate; bestDx = dx; bestDy = dy;
            }
        }
        if (best != nullptr)
        {
            setCurrentlyHighlightedChild (best);
            ensureItemComponentIsVisible (*best, std::nullopt);
        }
        return true;
    }

'''

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def patch_wave_keys(work: Path, juce: Path) -> None:
    src = work / 'Source'
    before = {str(p.relative_to(src)): sha(p) for p in src.rglob('*') if p.is_file()}
    for name in ('EditToneTab', 'EditRhythmTab'):
        path = src / 'ui' / (name + '.cpp')
        t = path.read_text()
        signature = 'void ' + name + '::updateWaveformComboBox('
        start = t.index(signature)
        brace = t.index('{', start) + 1
        assert KEY not in t
        t = t[:brace] + '\n    wfMenu.getProperties().set("' + KEY + '", true);' + t[brace:]
        path.write_text(t)
    path = juce / 'modules/juce_gui_basics/menus/juce_PopupMenu.cpp'
    native_before = sha(path)
    t = path.read_text()
    marker = '    bool keyPressed (const KeyPress& key) override\n    {\n'
    if 'bool riNavigateWaveColumn' not in t:
        assert t.count(marker) == 1
        t = t.replace(marker, METHOD + marker + '        if (riNavigateWaveColumn (key)) return true;\n', 1)
        path.write_text(t)
    else:
        assert METHOD in t and 'if (riNavigateWaveColumn (key)) return true;' in t
    after = {str(p.relative_to(src)): sha(p) for p in src.rglob('*') if p.is_file()}
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k))
    assert changed == ['ui/EditRhythmTab.cpp', 'ui/EditToneTab.cpp'], changed
    report = {'base': '0.1.6', 'changed_source_files': changed,
              'unchanged_source_files': len(before) - len(changed),
              'juce_gui_patch': 'menus/juce_PopupMenu.cpp',
              'juce_before_sha256': native_before, 'juce_after_sha256': sha(path),
              'opt_in_property': KEY, 'no_audio_midi_rom_lcd_changes': True,
              'before': before, 'after': after}
    (work / 'WAVE-KEYS-SCOPE.json').write_text(json.dumps(report, indent=2) + '\n')


def apply(root: Path) -> None:
    path = root / 'build.py'; t = path.read_text()
    if 'from wave_keys_fix import patch_wave_keys' not in t:
        assert 'from patch_bulk import patch_bulk' in t
        t = t.replace('from patch_bulk import patch_bulk',
                      'from patch_bulk import patch_bulk\nfrom wave_keys_fix import patch_wave_keys')
        assert '    patch_bulk(work)' in t
        t = t.replace('    patch_bulk(work)', '    patch_bulk(work)\n    patch_wave_keys(work, juce)')
    path.write_text(t.replace('0.1.6', '0.1.7'))
    for name in ('Main.cpp', 'BuildConfig.h', 'AndroidManifest.xml', 'test_import.py'):
        path = root / name; t = path.read_text().replace('0.1.6', '0.1.7')
        if name == 'AndroidManifest.xml': t = t.replace('android:versionCode="7"', 'android:versionCode="8"')
        if name == 'BuildConfig.h': t = t.replace('0x106', '0x107')
        path.write_text(t)
    path = root / 'LEEME.txt'; t = path.read_text()
    if 'WAVES CON FLECHAS 0.1.7' not in t:
        t += '''\n\nWAVES CON FLECHAS 0.1.7\nCon el selector de ondas abierto, Izquierda/Derecha mueve el resaltado a la columna vecina, buscando la fila de la misma altura. Si la ultima columna es mas corta, toma la fila disponible mas cercana. En los extremos no hay salto circular. Arriba/Abajo mantienen la navegacion existente. Enter o click confirma; Escape cancela. Las flechas laterales no cambian el value real mientras el menu sigue abierto.\nSolo se activa en el menu Waveform de Tone y Rhythm. El resto de ComboBox, menus y sliders conserva sus teclas anteriores. No se implementa aqui el mapa QWERTY completo propuesto. No se tocan ROMs, MIDI, motor, audio, buffer ni el filtro Now Bulk Receiving.\nLa distribucion usa JUCE fijado mas un parche local documentado en wave_keys_fix.py, aplicado a juce_gui_basics/menus/juce_PopupMenu.cpp. build.py lo aplica de forma idempotente; no usar otra version de JUCE.\nInstalar encima de 0.1.6 sin borrar datos. Mantener buffer 512 y modo CPU. Pruebas de menus con JUCE real y catalogos sinteticos; teclado USB fisico y edicion con firmware en tablet pendientes.\nDesde Git, ejecutar wave_keys_fix.py despues de bulk_display_fix.py. El ZIP ya tiene la integracion aplicada.\n'''
    path.write_text(t)

if __name__ == '__main__': apply(Path(__file__).resolve().parent)
