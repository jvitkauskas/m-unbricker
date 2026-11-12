#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render every page with the app's own renderer (tools/preview.c + src/ui.c)
for both boards, into build/preview/BOARD/PAGE.png and a contact sheet
build/preview/pages-BOARD.png (2x)."""
import pathlib, subprocess, sys
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
for board, bid in (("fm1", 1), ("smk37", 2)):
    out = ROOT / "build" / "preview" / board
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "preview"
    subprocess.run(["gcc", "-O1", "-w", f"-DBOARD_ID={bid}", f'-DOUTDIR="{out}"', f"-I{ROOT}/src", f"-I{ROOT}/src/mbl", f"-I{ROOT}/build",
                    "-o", str(exe), str(ROOT / "tools" / "preview.c"), str(ROOT / "src" / "ui.c")], check=True)
    subprocess.run([str(exe)], check=True)
    shots = []
    for raw in sorted(out.glob("*.raw"), key=lambda p: p.stat().st_mtime):
        d = raw.read_bytes()
        im = Image.new("RGB", (240, 240))
        px = []
        for i in range(0, len(d), 2):
            v = (d[i] << 8) | d[i + 1]
            px.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        im.putdata(px)
        im.save(raw.with_suffix(".png"))
        raw.unlink()
        shots.append((raw.stem, im))
    cols = 4
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 500, rows * 520), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(shots):
        x, y = (i % cols) * 500 + 10, (i // cols) * 520 + 10
        sheet.paste(im.resize((480, 480), Image.NEAREST), (x, y))
        dr.text((x, y + 484), name, fill=(220, 220, 220))
    sheet.save(ROOT / "build" / "preview" / f"pages-{board}.png")
    print(board, len(shots), "pages")
