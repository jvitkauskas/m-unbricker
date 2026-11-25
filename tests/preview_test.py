#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The pages, against the C's: every page that tools/preview.rs draws with the app's renderer (src/ui.rs) has the
pixels of the C's preview (tools/preview.c with src/ui.c at bd5ea96), whose SHA-256s are in
tests/c-reference/preview.txt. Also the build for both boards, with each board's key names: the same pages as that
board's build.

  preview_test.py          compares with tests/c-reference/preview.txt
  preview_test.py --c      makes it from the C"""
import hashlib, pathlib, struct, subprocess, sys, zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import host  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "test" / "preview"
REF = ROOT / "tests" / "c-reference" / "preview.txt"


def rgb888(rgb565):
    """RGB565, big endian, widened as tools/preview.py (and the C's) did."""
    out = bytearray()
    for i in range(0, len(rgb565), 2):
        v = rgb565[i] << 8 | rgb565[i + 1]
        out += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    return bytes(out)


def png_rgb(path):
    """The pixels of one of tools/preview.rs's PNGs: 240x240 RGB, each row filter 0."""
    data, at, idat = path.read_bytes(), 8, b""
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IDAT":
            idat += data[at + 8:at + 8 + n]
        at += 12 + n
    raw = zlib.decompress(idat)
    assert all(raw[r * 721] == 0 for r in range(240))
    return b"".join(raw[r * 721 + 1:(r + 1) * 721] for r in range(240))


def rust(board, init=None):
    exe = host.build(ROOT / "tools" / "preview.rs", WORK / f"preview-{board}", board=board)
    out = WORK / f"{board}-{init}"
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), str(out)] + ([str(init)] if init is not None else []), check=True, capture_output=True)
    return {n: hashlib.sha256(png_rgb(out / f"{n}.png")).hexdigest() for n in (out / "pages.txt").read_text().split()}


def c():
    sys.path.insert(0, str(ROOT / "tests" / "c"))
    import reference
    src = reference.extract()
    lines = []
    for board, bid in (("fm1", 1), ("smk37", 2)):
        out = WORK / f"c-{board}"
        out.mkdir(parents=True, exist_ok=True)
        exe = out / "preview"
        # (As the C's tools/preview.py built it. The C's preview.c doesn't use rescue_img.h.)
        subprocess.run([reference.cc(), "-O1", "-w", f"-DBOARD_ID={bid}", f'-DOUTDIR="{out}"', f"-I{src}/src",
                        f"-I{src}/src/mbl", "-o", str(exe), str(src / "tools" / "preview.c"), str(src / "src" / "ui.c")],
                       check=True)
        subprocess.run([str(exe)], check=True)
        for raw in sorted(out.glob("*.raw"), key=lambda p: p.stat().st_mtime_ns):
            lines.append(f"{board} {raw.stem} {hashlib.sha256(rgb888(raw.read_bytes())).hexdigest()}")
    REF.parent.mkdir(exist_ok=True)
    REF.write_text("\n".join(lines) + "\n")
    print(f"{REF}: {len(lines)} pages, from the C")


def main():
    if "--c" in sys.argv:
        return c()
    want = {}
    for line in REF.read_text().splitlines():
        board, name, digest = line.split()
        want.setdefault(board, {})[name] = digest
    failed = 0
    for board, got in (("fm1", rust("fm1")), ("smk37", rust("smk37")), ("any as fm1", rust("any", 1)),
                       ("any as smk37", rust("any", 2))):
        ref = want[board.split()[-1]]
        bad = [n for n in ref if got.get(n) != ref[n]] + [n for n in got if n not in ref]
        failed += bool(bad)
        print(f"{'FAIL' if bad else 'ok  '} preview {board}: {len(ref) - len(bad)} of {len(ref)} pages as the C's"
              + (f"; not: {', '.join(bad)}" if bad else ""))
    sys.exit(1 if failed else 0)


main()
