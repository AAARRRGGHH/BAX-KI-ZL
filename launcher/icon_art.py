"""Five unlabelled note pads, drawn separately for each Windows icon size."""
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
    draw.ellipse((size*.015*scale, size*.015*scale,
                  size*.985*scale, size*.985*scale), fill=(0, 0, 0, 255))
    stroke = max(1., size*.022)
    # Visible space between outlines, not between their centerlines. Keep at
    # least 2 physical pixels clear even in the 16px title-bar icon.
    visible_gap = max(2., size*.035)
    gap = visible_gap+stroke
    outer = size*.46-stroke/2
    inner = size*.215+stroke/2
    color = ACCENT_RGB+(255,)
    for index in range(5):
        angle = -math.pi/2 + index*2*math.pi/5
        left, right = angle-math.pi/5, angle+math.pi/5
        points = []
        # Parallel sides use the same perpendicular gap at both radii.
        for radius, start, end in ((outer, left+math.asin(gap/(2*outer)), right-math.asin(gap/(2*outer))),
                                   (inner, right-math.asin(gap/(2*inner)), left+math.asin(gap/(2*inner)))):
            for step in range(33):
                a = start+(end-start)*step/32
                points.append(((center+radius*math.cos(a))*scale,
                               (center+radius*math.sin(a))*scale))
        # Round the four geometric corners while preserving equal sectors and
        # parallel gaps. The sampled circular edges remain unchanged.
        rounded = []
        for j, point in enumerate(points):
            if j not in (0, 32, 33, 65):
                rounded.append(point)
                continue
            before, after = points[j-1], points[(j+1)%len(points)]
            a, b = math.dist(point, before), math.dist(point, after)
            inset = min(size*.009*scale, a*.45, b*.45)
            start = tuple(point[k]+(before[k]-point[k])*inset/a for k in (0, 1))
            end = tuple(point[k]+(after[k]-point[k])*inset/b for k in (0, 1))
            for t in (k/8 for k in range(9)):
                rounded.append(tuple((1-t)**2*start[k]+2*(1-t)*t*point[k]+t*t*end[k] for k in (0, 1)))
        draw.line(rounded+[rounded[0]], fill=color, width=max(1, round(stroke*scale)), joint='curve')
    return image.resize((size, size), Image.Resampling.LANCZOS)


def write_icons(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    frames = [render(size) for size in ICON_SIZES]
    frames[-1].save(folder/'app_icon.png')
    frames[-1].save(folder/'app_icon.ico', sizes=[(size, size) for size in ICON_SIZES],
                    append_images=frames[:-1])
    return folder/'app_icon.ico', folder/'app_icon.png'
