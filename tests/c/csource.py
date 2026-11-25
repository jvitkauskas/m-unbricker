#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The C reference for the tests: M-Unbricker's last C (bd5ea96, from this repository's own history), unpacked into
build/c-src, with the C compiler of this computer (CC, default clang: it evaluates `a() | b()` left to right, as
pi32v2's clang did)."""
import os, pathlib, subprocess, tarfile, io

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMIT = "bd5ea96"
SRC = ROOT / "build" / "c-src"


def extract():
    """bd5ea96's src/ and tools/, once."""
    if not (SRC / "src" / "writer.c").exists():
        data = subprocess.run(["git", "-C", str(ROOT), "archive", COMMIT, "src", "tools", "rescue"],
                              check=True, capture_output=True).stdout
        SRC.mkdir(parents=True, exist_ok=True)
        tarfile.open(fileobj=io.BytesIO(data)).extractall(SRC, filter="data")
    return SRC


def cc():
    return os.environ.get("CC", "clang")
