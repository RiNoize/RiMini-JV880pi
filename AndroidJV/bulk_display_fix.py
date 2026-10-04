#!/usr/bin/env python3
"""Apply after browser_fix.py. Standalone GUI-only receive-notice revision."""
from pathlib import Path


def apply(root: Path) -> None:
    p = root / 'build.py'
    t = p.read_text()
    if 'from patch_bulk import patch_bulk' not in t:
        assert 'from patch_browser import patch_browser' in t
        t = t.replace('from patch_browser import patch_browser',
                      'from patch_browser import patch_browser\nfrom patch_bulk import patch_bulk')
        assert '    patch_browser(work)' in t
        t = t.replace('    patch_browser(work)', '    patch_browser(work)\n    patch_bulk(work)')
    p.write_text(t.replace('0.1.5', '0.1.6'))
    for name in ('Main.cpp', 'BuildConfig.h', 'AndroidManifest.xml', 'test_import.py'):
        p = root / name
        t = p.read_text().replace('0.1.5', '0.1.6')
        if name == 'AndroidManifest.xml': t = t.replace('android:versionCode="6"', 'android:versionCode="7"')
        if name == 'BuildConfig.h': t = t.replace('0x105', '0x106')
        p.write_text(t)
    p = root / 'LEEME.txt'
    t = p.read_text()
    if 'DISPLAY LIMPIO 0.1.6' not in t:
        t += '''\n\nDISPLAY LIMPIO 0.1.6\nPor defecto se oculta solo el mensaje completo Now Bulk Receiving en la copia grafica del display, conservando la ultima pantalla del mismo programa/expansion/modo. Si aun no existe una pantalla anterior, se muestra el nombre del patch seleccionado. Los valores de edicion siguen visibles en sus controles.\nSettings: desmarcar Ocultar aviso Now Bulk Receiving restaura los mensajes originales inmediatamente. La preferencia se guarda en un archivo privado de interfaz, separado de ROMs y NVRAM.\nNo se filtran ni se reducen los SysEx, ni se toca el firmware, los tiempos del motor, la carga de expansiones o el audio. Mensajes con texto adicional, errores o contenido no reconocido pasan sin cambios. No se intenta identificar errores por actividad MIDI. La recepcion sigue ocurriendo aunque el aviso no sea visible; para diagnosticar una transferencia, desactivar esta opcion.\nSe reconocen las grafias Receiving y Recieving, con diferencias de mayusculas, espacios y decoracion habitual. Variantes no reconocidas o textos parcialmente escritos pueden verse: no se fuerzan parches en la ROM para ocultarlos.\nInstalar encima de 0.1.5 sin borrar datos, cache o ROMs, y conservar la configuracion de audio.\nPruebas: detector/estado de vista y renderer LCD con pantallas sinteticas, no firmware real. La respuesta durante edicion real se confirma en la tablet.\nDesde Git: ejecutar bulk_display_fix.py despues de browser_fix.py. El ZIP de fuentes ya contiene la integracion.\n'''
    p.write_text(t)

if __name__ == '__main__': apply(Path(__file__).resolve().parent)
