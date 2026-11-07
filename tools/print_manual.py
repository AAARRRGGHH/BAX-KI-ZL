"""Print the offline manuals using a headless, isolated Microsoft Edge process."""
from pathlib import Path
import argparse
import os
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lang', choices=['DE', 'EN', 'all'], default='all', type=lambda v: 'all' if v.lower() == 'all' else v.upper())
    args = parser.parse_args()
    candidates = [Path(os.environ.get(key, 'C:/Program Files')) / 'Microsoft/Edge/Application/msedge.exe'
                  for key in ['ProgramFiles(x86)', 'ProgramFiles']]
    edge = next((path for path in candidates if path.is_file()), None)
    if edge is None:
        raise SystemExit('Microsoft Edge not found. Print the offline HTML from your browser instead.')
    for language in (['DE', 'EN'] if args.lang == 'all' else [args.lang]):
        source = ROOT / f'docs/BAX-KI-ZL_Manual_{language}.html'
        target = ROOT / f'docs/BAX-KI-ZL_Manual_{language}.pdf'
        if not source.is_file():
            raise SystemExit(f'Generate {source.name} first: python tools/build_manual.py')
        session = uuid.uuid4().hex[:10]
        profile = ROOT / f'build/manual-edge-{language}-{session}'
        staging = ROOT / f'build/manual-rendered-{language}-{session}.pdf'
        command = [str(edge), '--headless', '--disable-gpu', '--no-sandbox',
                   '--disable-crash-reporter', '--no-first-run', '--no-default-browser-check',
                   '--no-pdf-header-footer', '--virtual-time-budget=3000',
                   f'--user-data-dir={profile}', f'--print-to-pdf={staging}', source.as_uri()]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace',
                                timeout=60, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        (ROOT / f'build/manual-render-{language}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode or not staging.is_file() or not staging.read_bytes().startswith(b'%PDF-'):
            raise SystemExit(f'PDF export failed for {language}; see build/manual-render-{language}.log')
        staging.replace(target)
        print(f'PDF ready: {target.name} ({target.stat().st_size:,} bytes)')


if __name__ == '__main__':
    main()
