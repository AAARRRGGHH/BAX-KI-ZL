"""Render DE/EN manuals as offline HTML (screenshots/fonts embedded).

Install once: python -m pip install --target .tools/manual-python Markdown==3.7
Screenshots: tools/capture_manual.ps1
PDF: open the generated HTML and print, or use tools/print_manual.ps1.
"""
from pathlib import Path
import argparse
import base64
import html
import re
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/manual-python"))
try:
    import markdown
except ImportError:
    raise SystemExit("Install the project-local Markdown renderer; see this script's docstring.")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lang', choices=['DE', 'EN', 'all'], default='all')
args = parser.parse_args()
if args.lang == 'all':
    for language in ('DE', 'EN'):
        subprocess.run([sys.executable, str(Path(__file__).resolve()), '--lang', language], check=True)
    raise SystemExit(0)

source = ROOT / f"docs/MANUAL_{args.lang}.md"
body = markdown.markdown(source.read_text(encoding="utf-8"),
                         extensions=["tables", "toc", "attr_list", "sane_lists", "md_in_html"],
                         extension_configs={"toc": {"toc_depth": "2"}})

def figure(match):
    alt, src = match.groups()
    caption = html.escape(html.unescape(alt))
    return f'<figure><img src="{src}" alt="{alt}"><figcaption>{caption}</figcaption></figure>'

body = re.sub(r'<p>((?:<img[^>]*>\s*)+)</p>',
              lambda m: re.sub(r'<img alt="([^"]*)" src="([^"]*)"\s*/>', figure, m.group(1)), body)

def gallery(match):
    figures = re.findall(r'<figure>.*?</figure>', match.group(), re.S)
    return '\n'.join('<div class="gallery">'+''.join(figures[i:i+2])+'</div>'
                     for i in range(0, len(figures), 2))

body = re.sub(r'(?:<figure>.*?</figure>\s*)+', gallery, body, flags=re.S)

def image(match):
    src = html.unescape(match.group(1))
    if src.startswith(("https:", "http:", "data:")):
        raise ValueError("Manual images must be local screenshots")
    path = source.parent / src
    encoded = base64.b64encode(path.read_bytes()).decode("ascii")
    return 'src="data:image/png;base64,' + encoded + '"'

body = re.sub(r'src="([^"]+)"', image, body)

def font(name):
    path = ROOT / "assets/fonts" / name
    return "data:font/ttf;base64," + base64.b64encode(path.read_bytes()).decode("ascii")

css = """
@font-face{font-family:Barlow;src:url('REGULAR');font-weight:400;font-display:swap}
@font-face{font-family:Barlow;src:url('BOLD');font-weight:700;font-display:swap}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#000;color:#fff;font-family:Barlow,Arial,sans-serif;font-size:18px;line-height:1.5}
article{max-width:1040px;margin:32px auto;padding:50px 64px;background:#000;box-shadow:none}
a{color:#2ee7e7;text-decoration-thickness:1px;text-underline-offset:3px}h1,h2,h3{line-height:1.15;color:#fff}h2{font-size:32px;border-top:3px solid #2ee7e7;padding-top:18px;margin-top:60px}h3{font-size:24px;margin-top:32px}p{margin:14px 0}strong{font-weight:700}li{margin:6px 0}
.cover{text-align:center;min-height:850px;padding:12.5% 0 36px}.cover img{display:block;width:75%;max-width:75%;height:auto;margin:0 auto}.cover-modes{font-size:18px;margin:26px 0 0}.edition{font-size:16px;color:#ccc;margin:12px 0 0}.cover-hardware{font-size:15px;color:#ccc;margin:8px 0 0}

.contents-title{margin-top:28px}.toc ul{list-style:none;padding-left:0}.toc li{margin:8px 0}.toc a{text-decoration:none}.toc a:hover{text-decoration:underline}
table{border-collapse:collapse;width:100%;font-size:16px;margin:22px 0}th,td{text-align:left;vertical-align:top;border-bottom:1px solid #404646;padding:10px 12px}th{background:#111919;font-weight:700}th:first-child,td:first-child{min-width:110px}
.gallery{display:flex;gap:24px;justify-content:center;flex-wrap:wrap;margin:24px 0}.gallery figure{margin:0;max-width:370px;flex:1 1 280px}.gallery img{display:block;width:100%;height:auto;border-radius:6px}figcaption{font-size:14px;line-height:1.3;color:#ccc;margin:9px 3px 0}code{font-family:Consolas,monospace;font-size:.9em;background:#171d1d;padding:1px 4px;border-radius:3px}
@media(max-width:700px){article{margin:0;padding:24px}body{font-size:17px}.cover{min-height:0}.cover h1{font-size:44px}.gallery figure{max-width:420px}table{font-size:14px}th,td{padding:7px}}
@page{background:#000;size:A4;margin:17mm 16mm 18mm;@bottom-right{content:counter(page);font-family:Barlow,Arial,sans-serif;font-size:9pt;color:#ccc}}
@page:first{margin-top:16mm}
@media print{
 html{scroll-behavior:auto}body{background:#000;color:#fff;font-size:10pt;line-height:1.34;-webkit-print-color-adjust:exact;print-color-adjust:exact}article{max-width:none;margin:0;padding:0;box-shadow:none}
 .cover{min-height:244mm;padding:22.25mm 0 0;break-after:page}.cover img{width:75%;max-width:75%;margin:0 auto}.cover-modes{font-size:11pt;margin:8mm 0 0}.edition{font-size:10pt;margin:4mm 0 0}.cover-hardware{font-size:9pt;margin:2mm 0 0}

 h2{font-size:22pt;margin:0 0 6mm;padding-top:4mm;break-before:page;break-after:avoid}h2.contents-title{break-before:auto;margin-top:7mm}h3{font-size:14pt;margin-top:5mm;break-after:avoid}p{margin:3mm 0;orphans:3;widows:3;break-inside:avoid}li{margin:1.5mm 0}
 .toc ul{margin:4mm 0}.toc li{margin:3mm 0}table{font-size:9.1pt;margin:4mm 0}th,td{padding:1.6mm 2.5mm}thead{display:table-header-group}tr{break-inside:avoid}
 .gallery{gap:8mm;margin:5mm 0;break-inside:avoid;flex-wrap:nowrap}.gallery figure{width:68mm;max-width:68mm;flex:0 1 68mm;break-inside:avoid}.gallery img{border-radius:0}figcaption{font-size:8.6pt;margin-top:2mm}.gallery:has(figure:only-child) figure{max-width:68mm;width:68mm}
 a{color:#2ee7e7;text-decoration:none}code{background:transparent;padding:0}
 .compact-gallery .gallery figure{width:46mm;max-width:46mm;flex:0 1 46mm}
 .keep-together{break-inside:avoid}
}
""".replace("REGULAR", font("Barlow-Regular.ttf")).replace("BOLD", font("Barlow-Bold.ttf"))
edition_date = '07/11/2026' if args.lang == 'DE' else '11/07/2026'

document = (f'<!doctype html><html lang="{args.lang.lower()}"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>BAX:KI:ZL | Manual {args.lang} | V1.0 | {edition_date}</title><style>' + css + '</style>'
            '</head><body><article>' + body + '</article></body></html>')
output = ROOT / f"docs/BAX-KI-ZL_Manual_{args.lang}.html"
output.write_text(document, encoding="utf-8", newline="\n")
print(f"Generated {output.name}: {len(document):,} characters; images and Barlow fonts embedded")
