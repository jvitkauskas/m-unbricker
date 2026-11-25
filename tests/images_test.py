#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The image packing, against the C's: tools/make_images.py and src/writer.rs give, sector by sector, what the C's
make_images.py and writer.c gave (tests/c-reference/images-*.txt). For the rescue images in rescue/, and for made-up
ones with an app area (app.bin), which exercise the stored blocks and the regenerated padding.

  images_test.py          compares with tests/c-reference/
  images_test.py --c      makes tests/c-reference/ from the C (bd5ea96)"""
import pathlib, random, shutil, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import host  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "test" / "images"
REF = ROOT / "tests" / "c-reference"
KEY = 0x980F


def fill(off):
    """A padding block: 0xFF encrypted with the chip key, as tools/make_images.py has it."""
    key, out = (KEY ^ (off >> 2)) & 0xFFFF, bytearray(32)
    for i in range(32):
        out[i] = 0xFF ^ (key & 0xFF)
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def made_up(d):
    """rescue/'s boot areas with an app area each: runs of stored blocks and of padding, of many lengths."""
    rng = random.Random(1)
    for board, size in (("fm1", 0x23000), ("smk37", 0x9000)):
        (d / board).mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / "rescue" / board / "boot.bin", d / board / "boot.bin")
        app, b = bytearray(), 0
        while len(app) < size:
            run = rng.choice([1, 1, 2, 3, 7, 40, 300])
            padding = rng.random() < 0.5
            for _ in range(run):
                if len(app) < size:
                    app += fill(b * 32) if padding else bytes(rng.randrange(256) for _ in range(32))
                    b += 1
        (d / board / "app.bin").write_bytes(app)


def cases():
    yield "rescue", ROOT / "rescue"
    d = WORK / "made-up"
    made_up(d)
    yield "app", d


def rust(name, rescue):
    out = WORK / name
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "make_images.py"), str(rescue), str(out / "rescue_img.rs")],
                   check=True, capture_output=True)
    exe = host.build(ROOT / "tests" / "images.rs", out / "images", env=[f"RESCUE_IMG={out / 'rescue_img.rs'}"])
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


def c(name, rescue):
    sys.path.insert(0, str(ROOT / "tests" / "c"))
    import reference
    src = reference.extract()
    out = WORK / ("c-" + name)
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(src / "tools" / "make_images.py"), str(rescue), str(out / "rescue_img.h")],
                   check=True, capture_output=True)
    exe = out / "images"
    subprocess.run([reference.cc(), "-O1", "-w", "-DBOARD_ID=1", f"-I{src}/src", f"-I{src}/src/mbl", f"-I{out}",
                    str(ROOT / "tests" / "c" / "images.c"), str(src / "src" / "jl.c"), "-o", str(exe)], check=True)
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


def main():
    make_c = "--c" in sys.argv
    REF.mkdir(exist_ok=True)
    failed = 0
    for name, rescue in cases():
        ref = REF / f"images-{name}.txt"
        if make_c:
            ref.write_text(c(name, rescue))
            print(f"{ref}: from the C")
            continue
        got, want = rust(name, rescue), ref.read_text()
        if got != want:
            failed += 1
            first = next(i for i, (a, b) in enumerate(zip(got.splitlines(), want.splitlines())) if a != b)
            print(f"FAIL images {name}: line {first + 1}: {got.splitlines()[first]!r}, the C {want.splitlines()[first]!r}")
        else:
            print(f"ok   images {name}: {len(got.splitlines())} lines as the C's")
    sys.exit(1 if failed else 0)


main()
