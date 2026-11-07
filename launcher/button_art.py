"""Subpixel-rounded button outlines without Tk spline/line-join artifacts."""
from PIL import Image, ImageChops, ImageDraw


def render(width, height, scale, shape, edge, fill):
    ss = 4
    width, height = max(2, width), max(2, height)
    pad = max(2., 2.*scale)
    thickness = 1.8*scale

    def mask(inset):
        image = Image.new('L', (width*ss, height*ss))
        draw = ImageDraw.Draw(image)
        x0, y0 = (pad+inset)*ss, (pad+inset)*ss
        x1, y1 = (width-pad-inset)*ss, (height-pad-inset)*ss
        if x1 <= x0 or y1 <= y0:
            return image
        radius = (y1-y0)/2
        draw.rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=255)
        # Union a small-radius half onto the inward side. The opposite half
        # remains a true semicircle. Inset uses the same centers/tangencies.
        small = max(0, 5*scale-inset)*ss
        if shape == 'left':
            draw.rounded_rectangle((width*ss/2, y0, x1, y1), radius=small, fill=255)
        elif shape == 'right':
            draw.rounded_rectangle((x0, y0, width*ss/2, y1), radius=small, fill=255)
        return image

    outer, inner = mask(0), mask(thickness)
    image = Image.new('RGB', (width*ss, height*ss), 'black')
    image.paste(fill, (0, 0), outer)
    image.paste(edge, (0, 0), ImageChops.subtract(outer, inner))
    return image.resize((width, height), Image.Resampling.LANCZOS)
