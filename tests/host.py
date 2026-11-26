#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds a test's program for this computer: tests/<name>.rs with the app's modules it brings in (#[path]), against
m-bootloader's crate built for this computer (build/host/libmboot.rlib, `make build/host/libmboot.rlib`).

  host.py SOURCE.rs OUT [--board fm1|smk37|any] [--env NAME=VALUE ...] [--chip]

--chip links the simulated chip (tests/chip/chip.c), which needs the program at fixed low addresses: no PIE.
HOST_RUSTC and CC name the compilers (rustc and cc)."""
import argparse, os, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
FLAGS = ["--edition", "2024", "-C", "opt-level=1", "-C", "debuginfo=1", "-C", "overflow-checks=on", "-C",
         "debug-assertions=on"]


def build(source, out, board="fm1", env=(), chip=False):
    out = pathlib.Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [os.environ.get("HOST_RUSTC", "rustc"), *FLAGS, "--cfg", f'board="{board}"',
           "--extern", f"mboot={ROOT}/build/host/libmboot.rlib", "-L", f"{ROOT}/build/host", str(source), "-o", str(out)]
    if chip:
        lib = ROOT / "build" / "host" / "libchip.a"
        obj = lib.with_suffix(".o")
        src = ROOT / "tests" / "chip" / "chip.c"
        deps = (src, src.with_suffix(".h"))
        if not lib.exists() or any(lib.stat().st_mtime < d.stat().st_mtime for d in deps):
            subprocess.run([os.environ.get("CC", "clang"), "-std=gnu11", "-O2", "-fno-pie", "-c", str(src), "-o",
                            str(obj)], check=True)
            subprocess.run(["ar", "rcs", str(lib), str(obj)], check=True)
        cmd += ["-C", "relocation-model=static", "-C", "link-arg=-no-pie", "-l", "static=chip"]
    subprocess.run(cmd, check=True, env={**os.environ, **dict(e.split("=", 1) for e in env)})
    return out


if __name__ == "__main__":
    a = argparse.ArgumentParser()
    a.add_argument("source")
    a.add_argument("out")
    a.add_argument("--board", default="fm1")
    a.add_argument("--env", action="append", default=[])
    a.add_argument("--chip", action="store_true")
    o = a.parse_args()
    build(o.source, o.out, o.board, o.env, o.chip)
