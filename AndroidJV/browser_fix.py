#!/usr/bin/env python3
"""Apply after patch_change_fix.py. Source ZIP already has the integration applied."""
from pathlib import Path


def apply(root: Path) -> None:
    p = root / 'build.py'
    t = p.read_text()
    if 'from patch_browser import patch_browser' not in t:
        assert 'from patch_ui import patch_ui' in t
        t = t.replace('from patch_ui import patch_ui', 'from patch_ui import patch_ui\nfrom patch_browser import patch_browser')
        assert '    patch_ui(work)' in t
        t = t.replace('    patch_ui(work)', '    patch_ui(work)\n    patch_browser(work)')
    p.write_text(t.replace('0.1.4', '0.1.5'))
    for name in ('Main.cpp', 'BuildConfig.h', 'AndroidManifest.xml', 'test_import.py'):
        p = root / name
        t = p.read_text().replace('0.1.4', '0.1.5')
        if name == 'AndroidManifest.xml': t = t.replace('android:versionCode="5"', 'android:versionCode="6"')
        if name == 'BuildConfig.h': t = t.replace('0x104', '0x105')
        p.write_text(t)
    p = root / 'LEEME.txt'
    t = p.read_text()
    if 'CORRECCION DEL NAVEGADOR 0.1.5' not in t:
        t += '''\n\nCORRECCION DEL NAVEGADOR 0.1.5\nLa lista comprueba banco, columna y fila antes de leer un nombre o seleccionar un patch. Las filas recicladas fuera del banco son vacias. Al cambiar de categoria se descarta la seleccion del banco anterior; los callbacks pendientes no eligen patches de otra categoria. Los modelos se desconectan antes de destruir las listas.\nEl procesador, la emulacion, las ROMs, la carga de expansiones, el conversor y el LCD no cambian respecto de 0.1.4. La aplicacion no borra datos ni cache. Instalar encima de 0.1.4, sin desinstalar ni reimportar. Mantener las mismas 512 muestras.\nPruebas de navegador: JUCE real, listas/ROMs sinteticas y procesador simulado, ASan/UBSan. No son pruebas con expansiones o firmware reales ni mediciones de la tablet.\nDesde Git: ejecutar browser_fix.py despues de patch_change_fix.py. El ZIP tiene la integracion ya aplicada.\n'''
    p.write_text(t)

if __name__ == '__main__': apply(Path(__file__).resolve().parent)
