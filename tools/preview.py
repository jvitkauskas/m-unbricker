#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The contact sheets of `make preview`: build/preview/pages-BOARD.png (2x), from the pages that tools/preview.rs (the
app's own renderer, src/ui.rs) drew into build/preview/BOARD/PAGE.png, in the order of its pages.txt."""
import pathlib
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
for board in ("fm1", "smk37"):
    out = ROOT / "build" / "preview" / board
    names = (out / "pages.txt").read_text().split()
    shots = [(name, Image.open(out / f"{name}.png").convert("RGB")) for name in names]
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
