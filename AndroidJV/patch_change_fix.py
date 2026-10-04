#!/usr/bin/env python3
"""0.1.4 UI-only refresh correction. Run after audio_fix.py, before build.py."""
from pathlib import Path

def apply(root: Path) -> None:
    f = root / 'build.py'
    t = f.read_text()
    if 'from patch_ui import patch_ui' not in t:
        t = t.replace('from audio_engine_patch import patch_audio', 'from audio_engine_patch import patch_audio\nfrom patch_ui import patch_ui')
        t = t.replace('    patch_audio(work,HERE)', '    patch_audio(work,HERE)\n    patch_ui(work)')
    f.write_text(t.replace('0.1.3', '0.1.4'))
    for name in ['Main.cpp', 'BuildConfig.h', 'LEEME.txt', 'test_import.py']:
        f = root / name
        f.write_text(f.read_text().replace('0.1.3', '0.1.4').replace('0x103', '0x104'))
    f = root / 'AndroidManifest.xml'
    f.write_text(f.read_text().replace('versionCode="4"', 'versionCode="5"').replace('0.1.3', '0.1.4'))
    for f in (root / 'java').rglob('*.java'):
        f.write_text(f.read_text().replace('0.1.3', '0.1.4'))
    f = root / 'LEEME.txt'
    t = f.read_text()
    if 'PATCHES 0.1.4' not in t:
        t += '''\n\nPATCHES 0.1.4 - SOLO INTERFAZ\nEl refresco de los menus de ondas de Tone 1-4 y Rhythm Set ya no envia notificaciones como si el usuario hubiera editado una onda. Reutiliza las listas cuando la ROM seleccionada es la misma. Las ediciones reales de grupo/onda siguen notificando.\nEl editor no mantiene el bloqueo del motor durante todo el refresco grafico. Settings solo lee dos bytes de NVRAM bajo un bloqueo breve. No reconstruye las pestanas al cambiar entre patches del mismo modo.\nNO modifica los archivos ROM ni el firmware; NO cambia la seleccion de patches/bancos ni el montaje de expansiones; NO cambia la emulacion, SRC, polifonia, efectos, afinidad o audio de 0.1.3. Conserva el refresco de 12 Hz del display y NO oculta ni sustituye los mensajes que genere el firmware. El sonido puede cambiar antes que el nombre mostrado.\nEl navegador sigue usando la ranura de trabajo y Program Change existentes. Los cambios de expansion o modo conservan los resets anteriores.\nInstalar sobre 0.1.3, sin borrar datos ni reimportar ROMs. Probar a 512 muestras, misma configuracion de CPU, primero cambios de patch sin editar y despues cambios reales de onda. El tiempo de respuesta del firmware y el sonido se deben comprobar en la tablet.\nDesde Git: ejecutar patch_change_fix.py despues de audio_fix.py y antes de build.py. Los archivos de la distribucion ya estan preparados. PATCH-UI-SCOPE.json verifica que solo cambiaron seis archivos de interfaz sobre el motor 0.1.3 regenerado.\n'''
    f.write_text(t)

if __name__ == '__main__':
    apply(Path(__file__).resolve().parent)
