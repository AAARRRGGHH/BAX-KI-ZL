"""Shared project branding; packaged builds read the same canonical JSON."""
from pathlib import Path
import json
import sys
path = (Path(sys._MEIPASS)/'payload/brand.json' if getattr(sys, 'frozen', False)
        else Path(__file__).resolve().parents[1]/'assets/brand.json')
BRAND = json.loads(path.read_text(encoding='utf-8'))
ACCENT = BRAND['accent']
ACCENT_RGB = tuple(int(ACCENT[i:i+2],16) for i in (1,3,5))
DARK_ACCENT = '#'+''.join(f'{round(v*.14):02x}' for v in ACCENT_RGB)


def simulator_accent(data):
    """Read the simulator's confirmed color; never modify its settings."""
    try:
        fields = (Path(data)/'simulator/settings.cfg').read_text(encoding='utf-8').split()
        if not 2 <= int(fields[0]) <= 19:
            return ACCENT
        color = int(fields[4])
        if 0 <= color <= 0xffffff:
            return f'#{color:06x}'
    except (OSError, UnicodeError, ValueError, IndexError):
        pass
    return ACCENT


def dark_accent(accent):
    return '#'+''.join(f'{round(int(accent[i:i+2],16)*.14):02x}' for i in (1,3,5))


def tint_logo(image, accent):
    """Separate white/black from the brand mask, preserving antialiased edges."""
    from PIL import Image, ImageChops
    red, green, blue = image.convert('RGB').split()
    low = ImageChops.darker(ImageChops.darker(red, green), blue)
    high = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    mask = ImageChops.subtract(high, low).point(
        lambda value: round(value*255/(max(ACCENT_RGB)-min(ACCENT_RGB))))
    base = ImageChops.subtract(low, mask.point(lambda v: round(v*min(ACCENT_RGB)/255)))
    rgb = tuple(int(accent[i:i+2],16) for i in (1,3,5))
    return Image.merge('RGB', tuple(ImageChops.add(base, mask.point(lambda v, c=c: round(v*c/255))) for c in rgb))
