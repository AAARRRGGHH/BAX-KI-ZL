"""Copy an explicit source allowlist to a new, reviewable publication directory."""
from pathlib import Path
import argparse
import json
import shutil
from publication_files import ROOT, source_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/publication/repository')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT.is_relative_to(output):
        raise RuntimeError('Publication output must not replace the development project.')
    if output.exists() and any(output.iterdir()):
        raise RuntimeError('Use an empty publication directory; existing content is not removed.')
    paths = source_files(manual_artifacts=True)
    for source in paths:
        target = output / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    report = {'repository': str(output), 'files': len(paths),
              'bytes': sum(path.stat().st_size for path in paths)}
    (ROOT / 'build/publication').mkdir(parents=True, exist_ok=True)
    (ROOT / 'build/publication/manifest.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
