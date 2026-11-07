"""Regression for wavy horizontal borders at fractional window scales."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
from button_art import render


class ButtonRenderingTests(unittest.TestCase):
    def test_straight_outline_at_fractional_scales(self):
        for side in [620, 643, 697, 720, 779, 843, 980]:
            scale = side / 720
            width, height = round(225*scale), round(62*scale)
            for shape in ['left', 'right', 'pill']:
                image = render(width, height, scale, shape, '#2ee7e7', '#000000')
                # Every column on the straight section has exactly the same
                # antialiased coverage; no periodic scallops from spline joins.
                columns = [tuple(image.getpixel((x, y)) for y in range(height))
                           for x in range(height+5, width-height-5)]
                self.assertTrue(columns)
                self.assertTrue(all(column == columns[0] for column in columns), (side, shape))
                self.assertGreater(max(pixel[1] for pixel in columns[0][:height//2]), 180)
                self.assertLessEqual(max(image.getpixel((0, height//2))), 4)


if __name__ == '__main__':
    unittest.main()
