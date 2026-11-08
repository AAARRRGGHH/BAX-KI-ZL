"""Bundle the existing release binaries; never build or flash a board implicitly."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from publication_files import source_files

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.tools/launcher-python'
sys.path.insert(0, str(DEPS))
from PIL import Image
sys.path.insert(0, str(ROOT / 'launcher'))
from icon_art import write_icons


def main():
    output = ROOT / 'dist'
    work = ROOT / 'build/companion'
    staged_output = work / 'release'
    payload = work / 'payload'
    payload.mkdir(parents=True, exist_ok=True)
    output.mkdir(exist_ok=True)
    firmware = ROOT / 'firmware/build'
    args = json.loads((firmware / 'flasher_args.json').read_text())
    manifest = {'built': datetime.now().astimezone().isoformat(timespec='seconds'),
                'board': 'Waveshare ESP32-S3-Touch-AMOLED-1.75C / SKU 33691',
                'firmware': [], 'simulator': []}

    def copy(source, relative, group=None, **extra):
        target = payload / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if group:
            manifest[group].append(dict(file=relative,
                sha256=hashlib.sha256(target.read_bytes()).hexdigest(), **extra))

    if args['flash_settings'] != {'flash_mode': 'dio', 'flash_size': '32MB', 'flash_freq': '80m'}:
        raise RuntimeError('Unexpected board flash settings; inspect before releasing.')
    for offset, relative in args['flash_files'].items():
        copy(firmware / relative, 'firmware/' + relative, 'firmware', offset=int(offset, 0))
    copy(ROOT / 'build/Release/SmartMidiSimulator.exe', 'simulator/SmartMidiSimulator.exe', 'simulator')
    copy(ROOT / 'assets/simulator_mask.png', 'simulator/assets/simulator_mask.png', 'simulator')
    # The host executable uses the Visual C++ DLL runtime. Bundle its redistributable
    # files beside the simulator; never depend on Visual Studio on the target PC.
    redist = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio/2022/Community/VC/Redist/MSVC'
    choices = sorted(redist.glob('*/x64/Microsoft.VC143.CRT'), reverse=True)
    if not choices:
        raise RuntimeError('Visual C++ x64 redistributable files not found.')
    for dll in choices[0].glob('*.dll'):
        copy(dll, 'simulator/' + dll.name, 'simulator')
    copy(ROOT / 'assets/brand.json', 'brand.json')
    copy(ROOT / 'assets/fonts/Barlow-Regular.ttf', 'Barlow-Regular.ttf')
    copy(ROOT / 'assets/fonts/OFL.txt', 'licenses/Barlow-OFL.txt')
    copy(ROOT / 'vendor/SDL/LICENSE.txt', 'licenses/SDL-LICENSE.txt')
    copy(ROOT / 'vendor/lvgl/LICENCE.txt', 'licenses/LVGL-LICENCE.txt')
    for metadata in DEPS.glob('*.dist-info'):
        for item in metadata.rglob('*'):
            if item.is_file() and ('license' in item.name.lower() or 'copying' in item.name.lower()
                                   or 'licenses' in item.parts or item.name == 'METADATA'):
                copy(item, 'licenses/' + metadata.name + '/' + str(item.relative_to(metadata)).replace('\\', '/'))
    for name in ('LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md'):
        copy(ROOT / name, 'licenses/project/' + name)
    firmware_components = [ROOT / 'vendor/waveshare-bsp-1.75c/esp32_s3_touch_amoled_1_75c',
                           ROOT / 'firmware/components/waveshare__esp_lcd_touch_cst9217']
    firmware_components += list((ROOT / 'firmware/managed_components').iterdir())
    for component in firmware_components:
        if component.is_dir():
            for item in component.iterdir():
                if item.is_file() and any(word in item.name.lower() for word in ('license', 'licence', 'copying', 'notice')):
                    copy(item, 'licenses/firmware/' + component.name + '/' + item.name)
    idf_root=ROOT / '.tools/esp-idf'
    for folder in (idf_root, idf_root/'components'):
        candidates=folder.iterdir() if folder==idf_root else folder.rglob('*')
        for item in candidates:
            if item.is_file() and any(word in item.name.lower() for word in ('license', 'licence', 'copying', 'notice')):
                copy(item, 'licenses/firmware/ESP-IDF/' + item.relative_to(idf_root).as_posix())
    copy(Path(sys.base_prefix) / 'LICENSE.txt', 'licenses/Python-LICENSE.txt')
    # Use the editable SVG at 2x its vector canvas, so proportional resizing
    # always downsamples a sharp original rather than enlarging the firmware mask.
    subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                    '-File', str(ROOT / 'tools/render_companion_logo.ps1'),
                    '-OutputFile', str(payload / 'logo.png')], check=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)
    icon, icon_png = write_icons(ROOT / 'assets')
    copy(icon, 'companion.ico')
    copy(icon_png, 'companion-icon.png')
    (payload / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    env = os.environ.copy()
    env['PYTHONPATH'] = str(DEPS)
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--windowed',
               '--name', 'BAX-KI-ZL-Companion', '--distpath', str(staged_output), '--workpath', str(work / 'pyinstaller'),
               '--specpath', str(work), '--paths', str(DEPS), '--paths', str(ROOT / 'launcher'),
               '--collect-all', 'esptool', '--collect-submodules', 'serial.tools',
               '--add-data', str(payload) + ';payload', '--icon', str(payload / 'companion.ico'),
               str(ROOT / 'launcher/app.py')]
    subprocess.run(command, check=True, env=env)
    release_exe = output / 'BAX-KI-ZL-Companion.exe'
    pending = output / 'BAX-KI-ZL-Companion.new.exe'
    shutil.copyfile(staged_output / release_exe.name, pending)
    try:
        pending.replace(release_exe)
    except PermissionError:
        # A running Windows image may be renamed even when it cannot be replaced.
        # Retain it for the current session instead of terminating the user's app.
        previous = output / ('BAX-KI-ZL-Companion-previous-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.exe')
        try:
            release_exe.rename(previous)
            pending.replace(release_exe)
        except PermissionError:
            release_exe = output / ('BAX-KI-ZL-Companion-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.exe')
            pending.replace(release_exe)
    # Corresponding launcher/esptool source and licenses accompany a sharable ZIP.
    # No board backups, local settings or private developer data enter the release.
    source = work / 'Companion-Sources.zip'
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in source_files():
            archive.write(path, path.relative_to(ROOT).as_posix())
        archive.write(DEPS / 'esptool-4.12.0.dist-info/licenses/LICENSE', 'esptool-4.12.0/LICENSE')
        for item in (DEPS / 'esptool').rglob('*'):
            if item.is_file() and '__pycache__' not in item.parts:
                archive.write(item, 'esptool-4.12.0/esptool/' + item.relative_to(DEPS / 'esptool').as_posix())
    package = output / 'BAX-KI-ZL-Companion-Windows.zip'
    with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.write(release_exe, 'BAX-KI-ZL-Companion.exe')
        archive.write(source, source.name)
        archive.write(ROOT / 'docs/COMPANION.md', 'README.md')
        archive.write(ROOT / 'docs/RELEASE_V1_1.md', 'RELEASE_NOTES.md')
        for language in ('DE', 'EN'):
            manual = ROOT / f'docs/BAX-KI-ZL_Manual_{language}.pdf'
            if manual.is_file():
                archive.write(manual, 'manuals/' + manual.name)
        for item in (payload / 'licenses').rglob('*'):
            if item.is_file():
                archive.write(item, 'licenses/' + item.relative_to(payload / 'licenses').as_posix())
    print(f'\nStandalone EXE: {release_exe}')
    print(f'Portable ZIP:   {package}')


if __name__ == '__main__':
    main()
