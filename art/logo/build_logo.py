"""Finish the generated mark; all outputs stay beside this script.

Requires Pillow, NumPy and SciPy. No new image-generation call is made.
"""
from pathlib import Path
import json

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage

ROOT = Path(__file__).resolve().parent
PALETTE = ['#000000', '#ff7800', '#a8adb0', '#27d68a']
COLORS = np.array([tuple(bytes.fromhex(c[1:])) for c in PALETTE], dtype=np.int32)


def nearest(rgb):
    delta = np.asarray(rgb, dtype=np.int32)[..., None, :] - COLORS
    return np.argmin(np.sum(delta * delta, axis=-1), axis=-1)


def font(size):
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', size)


# Preserve the generated geometry and alpha; remove faint peripheral raster noise.
source = Image.open(ROOT / 'generated_source.png').convert('RGBA')
alpha = source.getchannel('A')
bounds = alpha.point(lambda a: 255 if a >= 128 else 0).getbbox()
source = source.crop(bounds)
source.thumbnail((884, 884), Image.Resampling.LANCZOS)
rgba = np.asarray(source).copy()
index = nearest(rgba[..., :3])
visible = rgba[..., 3] >= 128

# The three broad pads are independent black components in the lower body.
# Shrink each by ten pixels to make the orange dividers legible at screen size.
components, count = ndimage.label((index == 0) & visible)
pad_count = 0
for label in range(1, count + 1):
    mask = components == label
    ys, xs = np.where(mask)
    if len(xs) > 1000 and ys.mean() > source.height * .75:
        inner = ndimage.distance_transform_edt(mask) > 10
        index[mask & ~inner] = 1
        pad_count += 1
assert pad_count == 3, f'Expected three pads, found {pad_count}'

# Thicken the generated exterior black outline, including the cable-loop hole.
# This adds only black, and leaves the coloured silhouette from imagegen intact.
mask = Image.fromarray((visible * 255).astype('uint8'))
mask = Image.fromarray(np.pad(np.asarray(mask), 22))
outline = mask.filter(ImageFilter.MaxFilter(41))
piece = Image.new('RGBA', outline.size, (0, 0, 0, 0))
piece.putalpha(outline)
flat = Image.fromarray(np.concatenate([
    COLORS[index].astype('uint8'),
    np.where(visible, 255, 0).astype('uint8')[..., None],
], axis=-1), 'RGBA')
piece.alpha_composite(flat, (22, 22))

web = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
web.alpha_composite(piece, ((1024-piece.width)//2, (1024-piece.height)//2))
# At this resolution the deliberate flat raster edges need no smoothing.
web.save(ROOT / 'logo.png', optimize=True)

# Composite onto pure black, then nearest-colour quantise AFTER resizing.
device_rgba = web.resize((480, 480), Image.Resampling.LANCZOS)
black = Image.new('RGBA', (480, 480), (0, 0, 0, 255))
black.alpha_composite(device_rgba)
device_index = nearest(np.asarray(black)[..., :3]).astype('uint8')
# The connector's inset is too fine for the device's 16 px minimum detail.
# Use its solid silhouette at screen size; keep the inset in the web master.
grey = device_index == 2
device_index[ndimage.binary_fill_holes(grey)] = 2
# Fit the visible colour, since the black exterior outline merges with this canvas.
colour_mask = Image.fromarray((device_index != 0).astype('uint8') * 255)
colour_box = colour_mask.getbbox()
flat_device = Image.fromarray(COLORS[device_index].astype('uint8'))
flat_device = flat_device.crop(colour_box)
scale = 432 / max(flat_device.size)
flat_device = flat_device.resize(tuple(round(n * scale) for n in flat_device.size), Image.Resampling.NEAREST)
device_canvas = Image.new('RGB', (480, 480), '#000000')
device_canvas.paste(flat_device, ((480-flat_device.width)//2, (480-flat_device.height)//2))
device_index = nearest(np.asarray(device_canvas)).astype('uint8')
device = Image.fromarray(device_index, 'P')
device.putpalette(COLORS.astype('uint8').ravel().tolist() + [0] * (768-12))
device.save(ROOT / 'logo_device.png', optimize=True)
(ROOT / 'logo_device_palette.json').write_text(json.dumps(PALETTE) + '\n')


def icon(size):
    # Render at 1024 first; the rounded outer corners remain transparent.
    canvas = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    draw.rounded_rectangle((0, 0, 1023, 1023), radius=224, fill='#161615')
    mark = web.resize((960, 960), Image.Resampling.LANCZOS)
    canvas.alpha_composite(mark, (32, 32))
    return canvas.resize((size, size), Image.Resampling.LANCZOS)


icons = {size: icon(size) for size in [256, 64, 32]}
for size in [256, 32]:
    icons[size].save(ROOT / f'icon-{size}.png', optimize=True)

# Contact sheet: the device and icon samples are pasted at their true pixel size.
preview = Image.new('RGB', (1320, 850), '#e8e8e3')
draw = ImageDraw.Draw(preview)
draw.text((32, 24), 'M-Unbricker / logo review', fill='#161615', font=font(28))
panels = [
    ((32, 94, 418, 480), '#000000', 'DEVICE / 160 px'),
    ((467, 94, 853, 480), '#f5f5f2', 'WEB / light'),
    ((902, 94, 1288, 480), '#161615', 'WEB / dark'),
]
for box, color, label in panels:
    draw.rectangle(box, fill=color)
    draw.text((box[0], 65), label, fill='#454541', font=font(17))
preview.paste(device.convert('RGB').resize((160, 160), Image.Resampling.NEAREST), (145, 207))
web_sample = web.resize((320, 320), Image.Resampling.LANCZOS)
preview.paste(web_sample, (500, 127), web_sample)
preview.paste(web_sample, (935, 127), web_sample)
draw.text((32, 512), 'APP / BROWSER ICONS — actual pixel sizes', fill='#454541', font=font(18))
for size, pos in [(256, (32, 553)), (64, (375, 635)), (32, (565, 651))]:
    preview.paste(icons[size], pos, icons[size])
    draw.text((pos[0], 818), f'{size} px', fill='#454541', font=font(17))
draw.text((740, 567), 'DEVICE PALETTE / four flat colours', fill='#454541', font=font(18))
for i, color in enumerate(PALETTE):
    x = 740 + i * 137
    draw.rounded_rectangle((x, 610, x+100, 710), radius=12, fill=color)
    draw.text((x, 732), color, fill='#454541', font=font(16))
preview.save(ROOT / 'logo_preview.png', optimize=True)

# Mechanical delivery checks, recorded alongside the artwork.
rgb = np.asarray(device.convert('RGB'))
unique = sorted('#' + bytes(c).hex() for c in np.unique(rgb.reshape(-1, 3), axis=0))
assert unique == sorted(PALETTE)
assert device.size == (480, 480)
assert web.size == (1024, 1024)
margin = 24
assert not np.any(rgb[:margin]) and not np.any(rgb[-margin:])
assert not np.any(rgb[:, :margin]) and not np.any(rgb[:, -margin:])
assert web.getchannel('A').getextrema() == (0, 255)
assert all(web.getpixel(p)[3] == 0 for p in [(0,0), (1023,0), (0,1023), (1023,1023)])


def run_width(line, at):
    left = right = at
    while left > 0 and line[left-1] == line[at]:
        left -= 1
    while right+1 < len(line) and line[right+1] == line[at]:
        right += 1
    return right-left+1


# Check nominal feature thickness across straight sections, away from curves.
widths = {
    'orange_body_left': run_width(device_index[220], 70),
    'orange_body_right': run_width(device_index[220], 405),
    'orange_pad_divider': run_width(device_index[380], 188),
    'grey_cable': run_width(device_index[100], 348),
    'green_pulse_lead': run_width(device_index[:, 130], 250),
}
assert min(widths.values()) >= 16, widths
validation = {
    'device_size': list(device.size),
    'device_colours': unique,
    'device_quantisation': 'nearest RGB Euclidean distance; no dithering',
    'device_black_border_at_least_px': margin,
    'web_size': list(web.size),
    'web_alpha': True,
    'icon_sizes': [256, 32],
    'three_pad_dividers_widened': pad_count == 3,
    'nominal_feature_widths_px': widths,
    'device_connector_inset': 'omitted to avoid a sub-16px detail',
    'source': 'built-in imagegen; generated_source.png',
}
(ROOT / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n')
print(json.dumps(validation, indent=2))
