#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Rust on the simulated chip (tests/chip), against the C: every scenario of tests/drive.rs, run as
tests/c/drive.c runs the C's, gives the C's summary (tests/c-reference.txt) line for line: the same register
accesses in the same order (their count and SHA-256), the same DMA data, the same framebuffer, the same flash on the
other unit at the end, and the same marks.

Each scenario runs on its board's build (-fm1, -smk37; the others on the FM-1's), and every one also on the build for
both, given the scenario's board; the board-neutral ones on the SMK-37's build too. The detect-* scenarios compare only
their result (`board B jedec X`): the Rust reads the flash with m-bootloader's driver, by DMA, where the C read byte by
byte, so their accesses differ by design.

  chip_test.py [SCENARIO ...]      (CHIP_TRACE-style full traces: tests/chip/README.md)"""
import concurrent.futures, os, pathlib, re, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import host  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "test" / "chip"


# Where the Rust is meant to differ from the C, which had a bug there: only the outcome is checked, each pattern
# matching a line of the summary or of drive's stderr (`stop WHAT ADDRESS`, where the writer stopped).
# session-idfail-fm1: a failed read of the SMK-37's trimdat (the chip's fault at 0xC1224, counted in `faults`) refuses
# the unit (flow.rs which_unit), with nothing erased or written, where the C took it for an FM-1 and erased its
# calibration. session-topcrc-fm1: the CRC above 0xFF000 can't be read after the write; the C called that PROTECTED
# CHANGED, which it wasn't. session-shortcrc-fm1: a CRC answer of 4 bytes, not 16, is asked for again (a retry), where
# the C took the missing bytes for zeros. session-siestuck-fm1: a register read that times out during a bulk OUT isn't
# taken for an ACK (the C's read gave 0, which is one); the repair goes on.
OUTCOME_ONLY = {
    "session-siestuck-fm1": [r"-- session 1 ", r"-- loader .* retries 0 ", r"usb .* bad-cbw 0$",
                             r"jl reads .* bad-crc 0 faults 1$", r"flash expect repaired ok$"],
    "session-shortcrc-fm1": [r"-- session 1 ", r"-- loader .* retries 1 ", r"flash expect repaired ok$"],
    "session-topcrc-fm1": [r"-- session 0 ", r"stop PROTECTED CRC FF000$", r"flash programmed ", r"flash expect repaired ok$"],
    "session-idfail-fm1": [r"-- session -1 ", r"jl reads \d+ writes 0 bad-crc 0 faults [1-9]\d*$", r"flash erased none$",
                           r"flash programmed none$", r"flash expect unchanged ok$"],
}


def reference():
    blocks = {}
    for block in (ROOT / "tests" / "c-reference.txt").read_text().split("\n\n"):
        lines = block.strip().splitlines()
        if lines:
            blocks[lines[0].split()[1]] = lines
    return blocks


def compared(name, lines):
    """What is compared of a summary: for detect-*, the result marks without their access counts."""
    if name.startswith("detect-"):
        return [re.sub(r" @\d+$", "", l) for l in lines if l.startswith("-- board")]
    return lines


def run(exe, name):
    env = {**os.environ, "CHIP_RESCUE": str(ROOT / "rescue")}
    r = subprocess.run([str(exe), name], capture_output=True, text=True, env=env, timeout=600)
    return r.returncode, r.stdout.strip().splitlines(), r.stderr


def main():
    exes = {b: host.build(ROOT / "tests" / "drive.rs", WORK / f"drive-{b}", board=b, chip=True)
            for b in ("fm1", "smk37", "any")}
    ref = reference()
    names = sys.argv[1:]
    unchecked = []
    if not names:
        names = list(ref) + [n for n in OUTCOME_ONLY if n not in ref]
        # Every scenario drive.rs has must be checked: against the C's summary, or in OUTCOME_ONLY.
        listed = subprocess.run([str(exes["fm1"]), "--list"], capture_output=True, text=True, check=True).stdout.split()
        unchecked = [n for n in listed if n not in ref and n not in OUTCOME_ONLY]
    jobs = []
    for name in names:
        board = "smk37" if name.endswith("-smk37") else "fm1"
        jobs.append((board, name))
        jobs.append(("any", name))
        if not name.endswith(("-fm1", "-smk37")):
            jobs.append(("smk37", name))
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        results = list(pool.map(lambda j: (j, run(exes[j[0]], j[1])), jobs))
    for (board, name), (code, out, err) in results:
        if name in OUTCOME_ONLY:
            lines = out + err.splitlines()
            missing = [w for w in OUTCOME_ONLY[name] if not any(re.match(w, l) for l in lines)]
            if code or missing:
                failed += 1
                print(f"FAIL {name} ({board}): exit {code}; missing {missing}")
            else:
                print(f"ok   {name} ({board}): as it should be, not as the C")
            continue
        want = compared(name, ref[name])
        got = compared(name, out)
        if code or got != want:
            failed += 1
            diff = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
            print(f"FAIL {name} ({board}): exit {code}; line {diff + 1}: "
                  f"{got[diff] if diff < len(got) else '(none)'!r}, the C {want[diff] if diff < len(want) else '(none)'!r}")
            if err.strip():
                print("     " + err.strip().splitlines()[-1])
        else:
            trace = next((l for l in out if l.startswith("trace ")), "")
            print(f"ok   {name} ({board}): {len(got)} lines as the C's" + (f", {trace.split()[1]} accesses" if trace else ""))
    for name in unchecked:
        print(f"FAIL {name}: neither in the C's summaries (tests/c-reference.txt) nor in OUTCOME_ONLY")
    print(f"{len(results) - failed} of {len(results)} runs as the C's")
    sys.exit(1 if failed or unchecked else 0)


main()
