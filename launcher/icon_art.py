"""Continuous five-sector note ring with a central waveform, rendered at each icon size."""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from brand import ACCENT_RGB

ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def render(size):
    scale = 8 if size <= 64 else 4
    image = Image.new('RGBA', (size*scale, size*scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    center = size/2
    outer, inner = size*.46, size*.225
    stroke = max(.9, size*.027)
    color = ACCENT_RGB+(255,)
    black = (0, 0, 0, 255)

    def disk(radius, fill):
        draw.ellipse(tuple(v*scale for v in
                     (center-radius, center-radius, center+radius, center+radius)), fill=fill)

    def point(radius, angle):
        return (center+radius*math.cos(angle), center+radius*math.sin(angle))

    disk(outer, color)
    disk(inner, black)
    # Five black windows cut into a single continuous ring. Parallel spokes
    # and rounded window corners reproduce the reference without doubled edges.
    for index in range(5):
        angle = -math.pi/2+index*2*math.pi/5
        left, right = angle-math.pi/5, angle+math.pi/5
        ro, ri = outer-stroke, inner+stroke
        lo, hi = left+math.asin(stroke/(2*ro)), right-math.asin(stroke/(2*ro))
        li, hj = left+math.asin(stroke/(2*ri)), right-math.asin(stroke/(2*ri))
        rounding = size*.024
        points = []

        def arc(radius, start, end):
            points.extend(point(radius, start+(end-start)*j/48) for j in range(49))

        def curve(start, corner, end):
            for j in range(1, 17):
                t = j/16
                points.append(tuple((1-t)**2*start[k]+2*(1-t)*t*corner[k]+t*t*end[k] for k in (0, 1)))

        arc(ro, lo+rounding/ro, hi-rounding/ro)
        curve(points[-1], point(ro, hi), point(ro-rounding, hi))
        points.append(point(ri+rounding, hj))
        curve(points[-1], point(ri, hj), point(ri, hj-rounding/ri))
        arc(ri, hj-rounding/ri, li+rounding/ri)
        curve(points[-1], point(ri, li), point(ri+rounding, li))
        points.append(point(ro-rounding, lo))
        curve(points[-1], point(ro, lo), point(ro, lo+rounding/ro))
        draw.polygon([(round(x*scale), round(y*scale)) for x, y in points], fill=black)

    # Five separate rounded waveform bars: accent / white / accent / white / accent.
    width = max(.85, size*.041)
    spacing = max(1.65, size*.064)
    for index, height in enumerate((.064, .15, .235, .15, .064)):
        x = center+(index-2)*spacing
        half = max(width/2, size*height/2)
        box = ((x-width/2)*scale, (center-half)*scale,
               (x+width/2)*scale, (center+half)*scale)
        draw.rounded_rectangle(box, radius=width*scale/2,
                               fill=color if index%2 == 0 else (255, 255, 255, 255))
    return image.resize((size, size), Image.Resampling.LANCZOS)


def write_icons(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    frames = [render(size) for size in ICON_SIZES]
    frames[-1].save(folder/'app_icon.png')
    frames[-1].save(folder/'app_icon.ico', sizes=[(size, size) for size in ICON_SIZES],
                    append_images=frames[:-1])
    return folder/'app_icon.ico', folder/'app_icon.png'
