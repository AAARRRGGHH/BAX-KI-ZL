from pathlib import Path
import math
import sys
import tempfile
import unittest
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
from icon_art import ICON_SIZES, render, write_icons
from brand import ACCENT_RGB


class IconTests(unittest.TestCase):
    def test_saved_simulator_accent_and_invalid_fallback(self):
        from brand import ACCENT, simulator_accent
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'simulator/settings.cfg'
            path.parent.mkdir()
            self.assertEqual(simulator_accent(folder), ACCENT)
            for text in ('', '1 0 0', '19 0 0 1 -1', '19 0 0 1 16777216', 'broken', '20 0 0 1 255'):
                path.write_text(text)
                self.assertEqual(simulator_accent(folder), ACCENT)
            for color in (0, 0x2ee7e7, 0xffffff, 0xff8000):
                path.write_text(f'19 0 0 1 {color} 1 1 50 400')
                self.assertEqual(simulator_accent(folder), f'#{color:06x}')

    def test_logo_tint_preserves_white_and_black(self):
        from brand import tint_logo
        image = Image.new('RGB', (3, 1))
        image.putdata([(0, 0, 0), (255, 255, 255), ACCENT_RGB])
        self.assertEqual(list(tint_logo(image, '#2ee7e7').getdata()),
                         [(0, 0, 0), (255, 255, 255), (46, 231, 231)])

    def test_brand_matches_native_defaults_and_logo(self):
        import json
        import xml.etree.ElementTree as ET
        from brand import ACCENT
        from app import ACCENT as application_accent
        root = Path(__file__).resolve().parents[1]
        config = json.loads((root/'assets/brand.json').read_text(encoding='utf-8'))
        self.assertEqual(ACCENT, config['accent'])
        self.assertEqual(application_accent, ACCENT)
        self.assertIn('0x'+ACCENT[1:], (root/'include/brand.hpp').read_text(encoding='utf-8'))
        logo = ET.parse(root/'assets/boot_logo.svg')
        for group in logo.getroot():
            if group.get('id') == 'accent':
                self.assertEqual(group.get('stroke'), ACCENT)
            if group.get('id') == 'accent-dots':
                self.assertEqual(group.get('fill'), ACCENT)

    def test_transparent_corners_black_center_and_five_sectors(self):
        image = render(256)
        self.assertEqual(image.getpixel((0, 0))[3], 0)
        self.assertEqual(image.getpixel((128, 128)), (0, 0, 0, 255))
        for i in range(5):
            angle = -math.pi/2+i*2*math.pi/5
            x, y = round(128+116*math.cos(angle)), round(128+116*math.sin(angle))
            pixels = [image.getpixel((x+dx, y+dy)) for dx in range(-2, 3) for dy in range(-2, 3)]
            self.assertTrue(any(max(abs(p[k]-ACCENT_RGB[k]) for k in range(3))<30 and p[3]>200 for p in pixels))

    def test_five_outlines_stay_disconnected_in_every_size(self):
        for size in ICON_SIZES:
            image = render(size)
            remaining = {(x, y) for y in range(size) for x in range(size)
                         if image.getpixel((x, y))[1] > 80 and image.getpixel((x, y))[3] > 128}
            components = 0
            while remaining:
                components += 1
                todo = [remaining.pop()]
                while todo:
                    x, y = todo.pop()
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            point = (x+dx, y+dy)
                            if point in remaining:
                                remaining.remove(point)
                                todo.append(point)
            self.assertEqual(components, 5, f'{size}px outlines merged')

    def test_ico_contains_individually_rendered_sizes(self):
        with tempfile.TemporaryDirectory() as folder:
            ico, png = write_icons(folder)
            with Image.open(ico) as image:
                self.assertEqual(image.ico.sizes(), {(s, s) for s in ICON_SIZES})
                for size in ICON_SIZES:
                    frame = image.ico.getimage((size, size)).convert('RGBA')
                    self.assertEqual(frame.tobytes(), render(size).tobytes())
            with Image.open(png) as image:
                self.assertEqual(image.size, (256, 256))


if __name__ == '__main__':
    unittest.main()
