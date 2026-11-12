#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds M-Unbricker's helper: m-bootloader 2.3's RAM stage with stage/stage.c (its logo screen), in
# a checkout of m-bootloader at tag v2.3 (build/m-bootloader), with m-bootloader's own Makefile and
# package tool. Writes web/assets/stage-{fm1,smk37-pro,smk37-elite}{,-listed}.fwsc.
#   tools/build_stage.sh [TOOLCHAIN]   (default: toolchain/, as for `make`)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TC=$(cd "${1:-$ROOT/toolchain}" && pwd)
MB=$ROOT/build/m-bootloader
if [ ! -d "$MB" ]; then
	git clone -q --depth 1 --branch v2.3 https://github.com/jvitkauskas/m-bootloader "$MB"
fi
test "$(git -C "$MB" rev-parse --short HEAD)" = 6bc8f45 || { echo "build/m-bootloader is not v2.3 (6bc8f45)"; exit 1; }
python3 "$ROOT/tools/make_logo.py"
git -C "$MB" checkout -q -- src/stage.c
cp "$ROOT/stage/stage.c" "$ROOT/stage/logo.h" "$MB/src/"
make -C "$MB" -s TOOLCHAIN="$TC" build/fm1/stage-ota.bin build/smk37/stage-ota.bin \
	build/fm1/stage.fwsc build/fm1/stage-listed.fwsc build/smk37/stage-pro.fwsc build/smk37/stage-pro-listed.fwsc \
	build/smk37/stage-elite.fwsc build/smk37/stage-elite-listed.fwsc
for b in fm1 smk37; do cat "$MB/build/$b/stage-ota-size.json"; echo; done
cp "$MB/build/fm1/stage.fwsc" "$ROOT/web/assets/stage-fm1.fwsc"
cp "$MB/build/fm1/stage-listed.fwsc" "$ROOT/web/assets/stage-fm1-listed.fwsc"
for v in pro elite; do
	cp "$MB/build/smk37/stage-$v.fwsc" "$ROOT/web/assets/stage-smk37-$v.fwsc"
	cp "$MB/build/smk37/stage-$v-listed.fwsc" "$ROOT/web/assets/stage-smk37-$v-listed.fwsc"
done
ls -l "$ROOT"/web/assets/stage-*.fwsc
