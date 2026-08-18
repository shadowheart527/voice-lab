#!/bin/bash
# Copy the built analysis engine next to the genderspace pages, so they can run
# standalone in a browser instead of needing the desktop app's WebSocket feed.
#
# This copies BUILD OUTPUT, not source: browser/ remains the one place the
# worklet and worker are written. Re-run after scripts/build-wasm.sh.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/genderspace/engine"
mkdir -p "$OUT"

if [ ! -f "$ROOT/browser/wasm/voicelab.wasm" ]; then
    echo "browser/wasm/voicelab.wasm missing; run scripts/build-wasm.sh first" >&2
    exit 1
fi

cp "$ROOT/browser/wasm/voicelab.mjs" "$ROOT/browser/wasm/voicelab.wasm" "$OUT/"
cp "$ROOT/browser/src/worklet/tap-worklet.js" "$OUT/"

# The worker imports the module by a path relative to itself; flatten it.
sed "s|from '../../wasm/voicelab.mjs'|from './voicelab.mjs'|" \
    "$ROOT/browser/src/worker/analyzer.worker.js" > "$OUT/analyzer.worker.js"

echo "engine synced to genderspace/engine/ ($(du -sh "$OUT" | cut -f1))"
