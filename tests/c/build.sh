#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds the simulated chip and the C driver for the host, from M-Unbricker's C at bd5ea96 (the repository's own
# history), into build/:
#   build/c-ref/        the C (src, rescue, tools) from `git archive`, ssync patched out, rescue_img.h generated
#                       from this tree's rescue/ boot areas (the C's were m-bootloader 2.4's; the C reads any)
#   build/chip/libchip.a  tests/chip/chip.c
#   build/c-drive/drive-fm1, drive-smk37, drive-any   (BOARD_ID 1, 2, 0)
#   build/preview-ref/fm1, smk37   tools/preview.c's raw pages (as tools/preview.py builds it, raws kept)
# Usage: tests/c/build.sh [--reference]   (--reference also runs every scenario and writes tests/c-reference.txt)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
REV=${REV:-bd5ea96}
B=$ROOT/build
CC=${CC:-clang} # the target's clang evaluates `a() | b()` left to right, as Rust does; host clang does too
CFLAGS="-std=gnu11 -O1 -g -no-pie -fno-pie -w"

rm -rf "$B/c-ref" "$B/c-drive"
mkdir -p "$B/c-ref/gen" "$B/chip" "$B/c-drive"
git -C "$ROOT" archive "$REV" src rescue tools | tar -x -C "$B/c-ref"
for b in fm1 smk37; do cp "$ROOT/rescue/$b/boot.bin" "$B/c-ref/rescue/$b/boot.bin"; done
python3 "$B/c-ref/tools/make_images.py" "$B/c-ref/rescue" "$B/c-ref/gen/rescue_img.h" >/dev/null
# x86 has no ssync: keep it a compiler barrier
sed -i 's/#define ssync() __asm__ volatile("ssync" ::: "memory")/#define ssync() __asm__ volatile("" ::: "memory")/' \
	"$B/c-ref/src/husb.c"
grep -q 'volatile("" ::: "memory")' "$B/c-ref/src/husb.c"

$CC -std=gnu11 -O2 -g -fno-pie -Wall -Wextra -c "$ROOT/tests/chip/chip.c" -o "$B/chip/chip.o"
rm -f "$B/chip/libchip.a"
ar rcs "$B/chip/libchip.a" "$B/chip/chip.o"

S=$B/c-ref/src
INC="-I$S -I$S/mbl -I$B/c-ref/gen -I$ROOT/tests/chip"
COMMON="$S/usbio.c $S/husb.c $S/bot.c $S/jl.c $S/writer.c $S/ui.c $S/lcd.c $S/board.c $S/mbl/hw.c"
build() { # name BOARD_ID extra-objects/flags...
	name=$1 id=$2
	shift 2
	$CC $CFLAGS -DHOST_TEST -DBOARD_ID=$id $INC "$@" "$ROOT/tests/c/drive.c" $COMMON \
		"$B/chip/libchip.a" -o "$B/c-drive/drive-$name"
}
build fm1 1 -DDISP_BOARD=1 "$S/mbl/keys.c"
build smk37 2 -DDISP_BOARD=2 "$S/mbl/keys.c"
# the build for both: the keys twice under other names, as the Makefile does
for k in fm1:1 smk:2; do
	n=${k%:*} id=${k#*:}
	$CC $CFLAGS -DHOST_TEST -DBOARD_ID=$id $INC -Dplat_boot_keys=${n}_plat_boot_keys \
		-Dplat_menu_scan=${n}_plat_menu_scan -Dplat_keys_off=${n}_plat_keys_off \
		-Dkeys_test_reset=${n}_keys_test_reset -c "$S/mbl/keys.c" -o "$B/c-drive/keys_$n.o"
done
build any 0 "$S/keys_any.c" "$B/c-drive/keys_fm1.o" "$B/c-drive/keys_smk.o"

# tools/preview.c, as tools/preview.py builds it (-O1 -w), keeping the raw pages
for p in fm1:1 smk37:2; do
	n=${p%:*} id=${p#*:}
	rm -rf "$B/preview-ref/$n"
	mkdir -p "$B/preview-ref/$n"
	$CC -O1 -w -DBOARD_ID=$id -DOUTDIR="\"$B/preview-ref/$n\"" -I$S -I$S/mbl -I$B/c-ref/gen \
		-o "$B/preview-ref/$n/preview" "$B/c-ref/tools/preview.c" "$S/ui.c"
	"$B/preview-ref/$n/preview"
done
echo "built: $B/c-drive/drive-fm1 drive-smk37 drive-any"

if [ "${1:-}" = "--reference" ]; then
	"$ROOT/tests/c/reference.sh"
fi
