"""Allowlist for public source snapshots and Companion's corresponding source."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = (
    'README.md', 'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', '.gitignore', '.gitattributes',
    'CMakeLists.txt', 'CMakePresets.json', 'lv_conf.h', 'Build.ps1',
    'Build-Companion.ps1', 'FetchDependencies.ps1', 'GenerateFonts.ps1', 'Firmware.ps1',
    'Starten.cmd', 'Flashen.cmd', 'Firmware_bauen.cmd', 'Handbuch.cmd', 'Manual_EN.cmd',
    'BLE-Pruefen.cmd', 'BleMidiDiagnose.ps1', 'PowerDiagnose.ps1',
    'PWR-Auslesen.cmd', 'PWR-Diagnose.cmd',
)
SOURCE_DIRS = (
    'core', 'include', 'ui', 'platform', 'launcher', 'tests', 'licenses',
    'experimental/bounce', 'experimental/ripples',
    'firmware/main', 'firmware/components', 'firmware/compat',
    'vendor/waveshare-bsp-1.75c',
)
DOCS = ('BUILD.md', 'RELEASE_V1_0.md', 'RELEASE_V1_1.md', 'COMPANION.md', 'BOUNCE.md',
        'MANUAL_DE.md', 'MANUAL_EN.md', 'MANUAL_BUILD.md')
EXCLUDED_TOOLS = {'cleanup_project.ps1', 'render_chord_piano.py', 'render_chord_listening.py',
                  'render_ripples_listening.py', 'preview_chord_library.py',
                  'piano-preview-requirements.txt'}


def source_files(manual_artifacts=False):
    files = set(ROOT / path for path in ROOT_FILES)
    for folder in SOURCE_DIRS:
        for path in (ROOT / folder).rglob('*'):
            if (path.is_file() and not any(part in ('.git', '__pycache__') for part in path.parts)
                    and path.suffix not in ('.pyc', '.log')):
                files.add(path)
    for path in (ROOT / 'tools').iterdir():
        if path.is_file() and path.name not in EXCLUDED_TOOLS:
            files.add(path)
    for path in (ROOT / 'assets').rglob('*'):
        if path.is_file() and path.name != 'design_reference.png':
            files.add(path)
    for name in ('CMakeLists.txt', 'sdkconfig.defaults', 'partitions.csv',
                 'lv_conf.h', 'dependencies.lock.in', 'README.md'):
        path = ROOT / 'firmware' / name
        if path.exists():
            files.add(path)
    for name in DOCS:
        files.add(ROOT / 'docs' / name)
    # Manual sources in source archives must be rebuildable without fetching
    # screenshots separately. PDFs are optional because Companion includes them
    # once, outside its corresponding-source archive.
    for language in ('DE', 'EN'):
        if manual_artifacts:
            files.add(ROOT / 'docs' / f'BAX-KI-ZL_Manual_{language}.pdf')
        text = (ROOT / 'docs' / f'MANUAL_{language}.md').read_text(encoding='utf-8')
        files.update(ROOT / 'docs' / relative
                     for relative in re.findall(r'!\[[^\]]*\]\(([^)]+)\)', text))
        files.update(ROOT / 'docs' / relative
                     for relative in re.findall(r'<img\s+[^>]*src=[\"\']([^\"\']+)[\"\']', text))
    files.add(ROOT / 'docs/manual-assets/Barlow-OFL.txt')
    text = (ROOT / 'README.md').read_text(encoding='utf-8-sig')
    files.update(ROOT / relative for relative in re.findall(r'!\[[^\]]*\]\(([^)]+)\)', text))
    for path in files:
        if not path.is_file():
            raise RuntimeError(f'Missing publication input: {path.relative_to(ROOT)}')
    return sorted(files, key=lambda path: path.relative_to(ROOT).as_posix())
