#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds M-Unbricker's helper: m-bootloader 3.0's RAM stage with stage/stage.rs (its logo screen) and stage/logo.rs,
# in a checkout of m-bootloader at tag v3.0 (build/m-bootloader), with m-bootloader's own Makefile and package tool.
# Writes web/assets/stage-{fm1,smk37-pro,smk37-elite}{,-listed}.fwsc.
#   tools/build_stage.sh [TOOLCHAIN [PI32V2_RUST]]
# TOOLCHAIN is pi32v2 LLVM 23.1.6-2 and PI32V2_RUST the pi32v2 Rust compiler 1.100.0-1, the releases v3.0 is built
# with (its README). They default to $TOOLCHAIN and $PI32V2_RUST, then to toolchain/ and pi32v2-rust/, as for `make`.
# cargo is needed too (the build compiles Rust's core with it), but upkr is not: only stage 1 and the installer use it.
# With these releases, m-bootloader's own stage.rs gives its v3.0 release's stage packages byte for byte.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TC=$(cd "${1:-${TOOLCHAIN:-$ROOT/toolchain}}" && pwd)
RS=$(cd "${2:-${PI32V2_RUST:-$ROOT/pi32v2-rust}}" && pwd)
"$TC/bin/clang" --version | grep -q ecf4f2342f853ba6e18b22ea1ea50720a9b491eb || {
	echo "$TC is not pi32v2 LLVM 23.1.6-2, which web/assets/ are built with"; exit 1; }
"$RS/bin/rustc" -vV | grep -q '^commit-hash: 4260eb24f787369d7f531a1d07f16f6ea52c34bd$' || {
	echo "$RS is not the pi32v2 Rust compiler 1.100.0-1, which web/assets/ are built with"; exit 1; }
MB=$ROOT/build/m-bootloader
if [ ! -d "$MB" ]; then
	git clone -q --depth 1 --branch v3.0 https://github.com/jvitkauskas/m-bootloader "$MB"
fi
test "$(git -C "$MB" rev-parse HEAD)" = cd3eecfba1e15511576d94877f7abb7780cced60 || {
	echo "build/m-bootloader is not v3.0 (cd3eecf), perhaps the 2.4 one of an earlier build:" \
		"remove it (rm -rf build/m-bootloader) and run this again"; exit 1; }
python3 "$ROOT/tools/make_logo.py"
# The checkout's own stage first, then ours over it: stage.rs's `mod logo` is rust/src/stage/logo.rs.
git -C "$MB" checkout -q -- rust/src/stage.rs
rm -rf "$MB/rust/src/stage"
mkdir "$MB/rust/src/stage"
cp "$ROOT/stage/stage.rs" "$MB/rust/src/stage.rs"
cp "$ROOT/stage/logo.rs" "$MB/rust/src/stage/logo.rs"
make -C "$MB" -s TOOLCHAIN="$TC" PI32V2_RUST="$RS" build/fm1/stage-ota.bin build/smk37/stage-ota.bin \
	build/fm1/stage.fwsc build/fm1/stage-listed.fwsc build/smk37/stage-pro.fwsc build/smk37/stage-pro-listed.fwsc \
	build/smk37/stage-elite.fwsc build/smk37/stage-elite-listed.fwsc
cat "$MB/build/fm1/stage-ota-size.json" "$MB/build/smk37/stage-ota-size.json"
cp "$MB/build/fm1/stage.fwsc" "$ROOT/web/assets/stage-fm1.fwsc"
cp "$MB/build/fm1/stage-listed.fwsc" "$ROOT/web/assets/stage-fm1-listed.fwsc"
for v in pro elite; do
	cp "$MB/build/smk37/stage-$v.fwsc" "$ROOT/web/assets/stage-smk37-$v.fwsc"
	cp "$MB/build/smk37/stage-$v-listed.fwsc" "$ROOT/web/assets/stage-smk37-$v-listed.fwsc"
done
ls -l "$ROOT"/web/assets/stage-*.fwsc
