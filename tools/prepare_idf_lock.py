"""Materialize the pinned IDF lock with this checkout's local component paths."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    template = ROOT / 'firmware/dependencies.lock.in'
    output = ROOT / 'firmware/dependencies.lock'
    text = template.read_text(encoding='utf-8').replace('${PROJECT_ROOT}', ROOT.as_posix())
    if '${' in text:
        raise RuntimeError('Unexpected unresolved variable in IDF lock template.')
    if not output.exists() or output.read_text(encoding='utf-8') != text:
        output.write_text(text, encoding='utf-8', newline='\n')
    print('Prepared pinned IDF dependencies for this checkout.')


if __name__ == '__main__':
    main()
