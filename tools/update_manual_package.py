"""Refresh manuals in the existing portable ZIP without rebuilding the EXE."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def update(path, replacements):
    temporary = path.with_suffix('.manual-update.zip')
    with zipfile.ZipFile(path) as original, zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as output:
        previous = set()
        for entry in original.infolist():
            previous.add(entry.filename)
            if entry.filename in replacements:
                output.write(replacements[entry.filename], entry.filename)
            else:
                output.writestr(entry, original.read(entry.filename))
        for name, file in replacements.items():
            if name not in previous:
                output.write(file, name)
    with zipfile.ZipFile(temporary) as archive:
        bad = archive.testzip()
        if bad:
            raise RuntimeError(f'ZIP integrity check failed: {bad}')
    temporary.replace(path)


def main():
    source = ROOT / 'build/companion/Companion-Sources.zip'
    package = ROOT / 'dist/BAX-KI-ZL-Companion-Windows.zip'
    paths = ['tools/build_companion.py', 'tools/build_manual.py', 'tools/print_manual.py',
             'tools/print_manual.ps1', 'tools/capture_manual.ps1', 'tools/update_manual_package.py',
             'docs/MANUAL_DE.md', 'docs/MANUAL_EN.md', 'docs/MANUAL_BUILD.md', 'docs/COMPANION.md']
    update(source, {path: ROOT / path for path in paths})
    replacements = {'Companion-Sources.zip': source, 'README.md': ROOT / 'docs/COMPANION.md'}
    for language in ('DE', 'EN'):
        name = f'BAX-KI-ZL_Manual_{language}.pdf'
        replacements['manuals/' + name] = ROOT / 'docs' / name
    update(package, replacements)
    print(f'Updated {package.name}: German and English PDF manuals included; EXE preserved.')


if __name__ == '__main__':
    main()
