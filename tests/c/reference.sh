#!/bin/sh
# SPDX-License-Identifier: MIT
# Runs every scenario of the C driver (tests/c/build.sh builds it) and writes tests/c-reference.txt: each scenario's
# summary, a blank line between them. Board scenarios (-fm1, -smk37) run on that board's build, the rest on
# drive-fm1. Also checks, and fails on a difference:
#   - the build for both (drive-any) prints the same for every scenario; drive-smk37 the same for the board-neutral
#     ones;
#   - panel-<board>: each page's framebuffer equals tools/preview.c's raw page;
#   - the page each wake / session / full scenario ends on (tests/c/pages.txt), outside the status line;
#   - the repaired and unchanged flash expectations ("flash expect ... ok").
# Run times go to stderr. OUT=<file> writes elsewhere.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build
D=$B/c-drive
OUT=${OUT:-$ROOT/tests/c-reference.txt}
T=$B/c-run
rm -rf "$T"
mkdir -p "$T"
: >"$OUT.tmp"
fail=0
for s in $("$D/drive-fm1" --list); do
	case $s in
	*-smk37) exe=drive-smk37 board=smk37 ;;
	*-fm1) exe=drive-fm1 board=fm1 ;;
	*) exe=drive-fm1 board= ;;
	esac
	mkdir -p "$T/$s"
	t0=$(date +%s%N)
	CHIP_PANEL="$T/$s/fb.raw" CHIP_PANEL_DIR="$T/$s" "$D/$exe" "$s" >"$T/$s/out.txt"
	t1=$(date +%s%N)
	echo "$s $(((t1 - t0) / 1000000)) ms" >&2
	cat "$T/$s/out.txt" >>"$OUT.tmp"
	echo >>"$OUT.tmp"
	"$D/drive-any" "$s" >"$T/$s/any.txt"
	cmp -s "$T/$s/out.txt" "$T/$s/any.txt" || { echo "DIFF drive-any $s" >&2; fail=1; }
	if [ -z "$board" ]; then
		"$D/drive-smk37" "$s" >"$T/$s/smk.txt"
		cmp -s "$T/$s/out.txt" "$T/$s/smk.txt" || { echo "DIFF drive-smk37 $s" >&2; fail=1; }
	fi
	if grep -q "^flash expect .* FAIL" "$T/$s/out.txt"; then
		echo "FLASH $s: $(grep '^flash expect' "$T/$s/out.txt")" >&2
		fail=1
	fi
done
python3 -I - "$T" "$B/preview-ref" "$ROOT/tests/c/pages.txt" <<'EOF' || fail=1
import pathlib, sys
run, ref, pages = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
bad = 0
for board in ("fm1", "smk37"):
    for raw in sorted((ref / board).glob("*.raw")):
        got = run / f"panel-{board}" / raw.name
        if not got.exists() or got.read_bytes() != raw.read_bytes():
            print(f"PANEL panel-{board} {raw.stem} differs from tools/preview.c", file=sys.stderr); bad = 1
    print(f"panel-{board}: {len(list((ref / board).glob('*.raw')))} pages checked against tools/preview.c", file=sys.stderr)
ROW = 240 * 2
for line in open(pages):
    line = line.split("#")[0].split()
    if not line:
        continue
    scen, page = line
    board = "smk37" if scen.endswith("-smk37") else "fm1"
    a = (run / scen / "fb.raw").read_bytes()
    b = (ref / board / f"{page}.raw").read_bytes()
    mask = lambda d: d[:207 * ROW] + d[223 * ROW:]  # not the status line (rows 207-222)
    exact = a == b
    if mask(a) != mask(b):
        print(f"PAGE {scen}: not {page}", file=sys.stderr); bad = 1
    else:
        print(f"{scen}: ends on {page}{'' if exact else ' (its own status line)'}", file=sys.stderr)
sys.exit(bad)
EOF
mv "$OUT.tmp" "$OUT"
[ $fail = 0 ] && echo "reference: $OUT, all checks passed" >&2 || { echo "reference: $OUT, CHECKS FAILED" >&2; exit 1; }
