#!/bin/sh
# SPDX-License-Identifier: MIT
# Copies web/ into a GitHub Pages checkout's m-unbricker/ folder (the argument) and
# stamps every "?v=dev" with this deploy's version, so browsers don't keep old files (Pages lets them
# cache for 10 minutes). Commit and push the checkout afterwards.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:?usage: tools/deploy_web.sh PAGES_CHECKOUT/m-unbricker}
V=$(date -u +%Y%m%d%H%M%S)
rsync -a --delete --exclude test "$ROOT/web/" "$DEST/"
for f in index.html app.js lib/assets.js; do sed -i "s/?v=dev/?v=$V/g" "$DEST/$f"; done
echo "deployed version $V to $DEST"
