#!/usr/bin/env python3
"""Export imagegen source pictures and build their review sheet. Writes only here."""
from pathlib import Path
import json
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent

def nearest(im, palette):
    colors = np.array([tuple(bytes.fromhex(c[1:])) for c in palette], dtype=np.int32)
    a = np.array(im.convert('RGB'), dtype=np.int32)
    # Work a row stripe at a time to bound memory for large generated sources.
    out = np.empty(a.shape[:2], dtype=np.uint8)
    for y in range(0, a.shape[0], 128):
        d = a[y:y+128, :, None, :] - colors[None, None, :, :]
        out[y:y+128] = (d*d).sum(axis=3).argmin(axis=2)
    result = Image.fromarray(out, 'P')
    values = colors.astype(np.uint8).flatten().tolist()
    result.putpalette(values + [0] * (768-len(values)))
    return result

def device(record):
    palette = record['palette']
    raw = Image.open(ROOT / record['source']).convert('RGB')
    # Get the occupied bounds after colour classification, excluding black.
    classified = nearest(raw, palette)
    box = Image.fromarray((np.asarray(classified) != 0).astype('uint8') * 255).getbbox()
    if not box:
        raise ValueError('Empty device picture: ' + record['name'])
    cropped = raw.crop(box)
    cropped.thumbnail((432, 432), Image.Resampling.LANCZOS)
    canvas = Image.new('RGB', (480, 480), '#000000')
    canvas.paste(cropped, ((480-cropped.width)//2, (480-cropped.height)//2))
    result = nearest(canvas, palette)
    result.save(ROOT / 'device' / (record['name'] + '.png'), optimize=True, bits=2)
    return result

WEB_PALETTE = ['#f5f5f2', '#000000', '#ff7800', '#a8adb0', '#6b7278',
               '#ffffff', '#6de5a2', '#28ad70', '#ffbe45', '#f6c59e', '#dc956a']

def web(record):
    raw = Image.open(ROOT / record['source']).convert('RGB')
    # Fit, rather than crop, so no generated cable end is cut off.
    raw.thumbnail((960, 720), Image.Resampling.LANCZOS)
    canvas = Image.new('RGB', (960, 720), '#f5f5f2')
    canvas.paste(raw, ((960-raw.width)//2, (720-raw.height)//2))
    # Only the keyboard variant needs a dark-grey housing. Omitting that swatch
    # elsewhere keeps slight generated tonal noise in black knobs flat black.
    palette = WEB_PALETTE if record['name']=='smk37_variant' else [
        c for c in WEB_PALETTE if c!='#6b7278']
    result = nearest(canvas, palette).convert('RGB')
    # Neutral antialias pixels should stay neutral rather than become green specks.
    # This is web-only; device export always uses the full specified palette.
    a = np.array(canvas)
    neutral_mask = a.max(axis=2).astype('int16')-a.min(axis=2) < 24
    neutral_palette = [c for c in palette if
        max(bytes.fromhex(c[1:]))-min(bytes.fromhex(c[1:])) < 24]
    neutral = nearest(canvas, neutral_palette).convert('RGB')
    result = Image.composite(neutral,result,Image.fromarray(neutral_mask.astype('uint8')*255))
    # A border-connected light region is background, including generated off-white.
    arr = np.array(result)
    light = np.all(arr >= 235, axis=2)
    mask = Image.fromarray(np.where(light, 255, 0).astype('uint8'))
    for seed in [(0, 0), (959, 0), (0, 719), (959, 719)]:
        if mask.getpixel(seed) == 255:
            ImageDraw.floodfill(mask, seed, 128)
    arr[np.asarray(mask) == 128] = (245,245,242)
    result = Image.fromarray(arr)
    result.save(ROOT / 'web' / (record['name'] + '.png'), optimize=True)
    return result

def font(size):
    path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    return ImageFont.truetype(path, size)

DEVICE_ORDER = ['welcome_fm1','welcome_smk37','unplug_pc_fm1','unplug_pc_smk37',
                'connect_fm1','connect_smk37','switch_on','waking','found_fm1',
                'found_smk37','checking','press_play','repairing','success_fm1',
                'success_smk37','error','retry','leave']
WEB_ORDER = ['hero','plug_good','sending','move_cable','switch_on','press_play',
             'working','success','trouble','smk37_variant']

def contact_sheet(records):
    devices = {r['name']:r for r in records if r['set']=='device'}
    webs = {r['name']:r for r in records if r['set']=='web'}
    width, top, tile_w, tile_h = 1680, 88, 420, 330
    dev_h = math.ceil(len(devices)/4)*tile_h
    web_top = top+dev_h+80
    web_h = math.ceil(len(webs)/3)*448
    sheet = Image.new('RGB',(width,web_top+web_h+24),'#e9e9e5')
    d = ImageDraw.Draw(sheet)
    d.text((24,18),'DEVICE / 240 px review + actual 120 px',font=font(27),fill='#242424')
    for i,name in enumerate(n for n in DEVICE_ORDER if n in devices):
        x,y = (i%4)*tile_w, top+(i//4)*tile_h
        d.text((x+12,y),name,font=font(21),fill='#242424')
        im = Image.open(ROOT/'device'/(name+'.png')).convert('RGB')
        # NEAREST is intentional: preserves the shipped palette in review previews.
        sheet.paste(im.resize((240,240),Image.Resampling.NEAREST),(x+12,y+40))
        sheet.paste(im.resize((120,120),Image.Resampling.NEAREST),(x+276,y+40))
        d.text((x+276,y+172),'120 x 120',font=font(15),fill='#555555')
        for j,c in enumerate(devices[name]['palette']):
            d.rectangle((x+18+j*42,y+294,x+48+j*42,y+311),fill=c)
    d.text((24,web_top-54),'WEB / 960 x 720 originals',font=font(27),fill='#242424')
    for i,name in enumerate(n for n in WEB_ORDER if n in webs):
        x,y = (i%3)*560, web_top+(i//3)*448
        d.text((x+12,y),name,font=font(22),fill='#242424')
        im = Image.open(ROOT/'web'/(name+'.png')).convert('RGB')
        sheet.paste(im.resize((536,402),Image.Resampling.LANCZOS),(x+12,y+34))
    sheet.save(ROOT/'contact_sheet.png',optimize=True)

def main():
    records=json.loads((ROOT/'sources/generation-manifest.json').read_text())
    palettes={}
    checks=[]
    for r in records:
        if r['set']=='device':
            # Saturated green remains distinct from neutral-grey bodies in RGB distance.
            r['palette'] = ['#27d68a' if c=='#6de5a2' else c for c in r['palette']]
        result=device(r) if r['set']=='device' else web(r)
        colors=len(result.convert('RGB').getcolors(result.width*result.height))
        if r['set']=='device':
            palettes[r['name']]=r['palette']
            assert result.size==(480,480) and colors<=4
            rgb=np.array(result.convert('RGB'))
            assert not rgb[:24].any() and not rgb[-24:].any()
            assert not rgb[:,:24].any() and not rgb[:,-24:].any()
        else:
            assert result.size==(960,720)
            assert result.getpixel((0,0))==(245,245,242)
        checks.append({'file':r['set']+'/'+r['name']+'.png','size':list(result.size),
                       'colours':colors,'opaque':True})
    (ROOT/'device/palette.json').write_text(json.dumps(palettes,indent=2)+'\n')
    (ROOT/'validation.json').write_text(json.dumps(checks,indent=2)+'\n')
    contact_sheet(records)
    print(f'Exported {len(palettes)} device and {len(records)-len(palettes)} web pictures.')

if __name__=='__main__':
    main()
