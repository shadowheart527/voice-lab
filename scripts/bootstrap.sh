#!/bin/bash
# Fetch the large payloads that are deliberately kept out of git, and set up
# the build container. Safe to re-run: everything is skipped if already present.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

LIBTORCH_VERSION=2.4.0
CONTAINER=informant-build

say() { printf '\n=== %s\n' "$1"; }

say "distrobox container ($CONTAINER)"
if command -v distrobox >/dev/null && ! distrobox list 2>/dev/null | grep -q "$CONTAINER"; then
    echo "creating Fedora 44 container (the host is immutable and ships no Qt6/FFTW/Pulse headers)"
    distrobox create --name "$CONTAINER" --image registry.fedoraproject.org/fedora:44 --yes
    distrobox enter --name "$CONTAINER" -- bash -lc '
        sudo dnf install -y -q gcc-c++ cmake ninja-build git \
            qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtcharts-devel \
            qt6-qtwebsockets-devel fftw-devel pulseaudio-libs-devel \
            alsa-lib-devel freetype-devel eigen3-devel libstdc++-static sox'
else
    echo "already present (or distrobox unavailable), skipping"
fi

say "libtorch (only needed for the DeepFormants build)"
if [ -d engine/external/libtorch ]; then
    echo "already present, skipping"
else
    URL="https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-${LIBTORCH_VERSION}%2Bcpu.zip"
    echo "downloading ~750 MB from pytorch.org"
    tmp=$(mktemp -d)
    curl -L -o "$tmp/libtorch.zip" "$URL"
    unzip -q "$tmp/libtorch.zip" -d engine/external/
    rm -rf "$tmp"
fi

say "python environment for the ML tools"
if [ -d ml/.venv ]; then
    echo "already present, skipping"
else
    /usr/bin/python3 -m venv ml/.venv
    ml/.venv/bin/pip install -q --upgrade pip
    ml/.venv/bin/pip install -q numpy soundfile onnxruntime
    echo "installed numpy, soundfile, onnxruntime"
fi

say "browser runtime + model (onnxruntime-web, gender probe)"
if [ -d browser/vendor ] && [ -f browser/models/ecapa_gender_int8.onnx ]; then
    echo "already present, skipping"
elif command -v npm >/dev/null; then
    ( cd browser && npm install --silent onnxruntime-web@1.20.1 )
    mkdir -p browser/vendor browser/models
    cp browser/node_modules/onnxruntime-web/dist/ort.wasm.min.mjs browser/vendor/
    cp browser/node_modules/onnxruntime-web/dist/ort-wasm-simd-threaded*.mjs browser/vendor/
    cp browser/node_modules/onnxruntime-web/dist/ort-wasm-simd-threaded*.wasm browser/vendor/
    if [ -f ml/models/ecapa_gender_int8.onnx ]; then
        cp ml/models/ecapa_gender_int8.onnx browser/models/
    else
        echo "run: ml/.venv/bin/python -m gender_probe.fetch_model  (then re-run this)"
    fi
else
    echo "npm not found; the browser build works without the neural probe"
fi

say "engine copy for the genderspace pages"
if [ -f "$ROOT/browser/wasm/voicelab.wasm" ]; then
    bash "$ROOT/scripts/sync-engine.sh"
else
    echo "browser/wasm not built yet; run scripts/build-wasm.sh then scripts/sync-engine.sh"
fi

say "generated web tables"
python3 scripts/gen-web-tables.py

cat <<'EOF'

Bootstrap complete. Next:

  engine/launcher/rebuild.sh        build the desktop engine
  engine/launcher/rebuild.sh df     ... with the DeepFormants tracker

  cd browser && python3 -m http.server 8181 --bind 127.0.0.1
                                    serve the browser build
  browser/tools/build-static.sh      assemble a hostable copy (see docs/deploy.md)

Model weights for the gender probe are fetched separately:
  ml/.venv/bin/python ml/gender_probe/fetch_model.py
EOF
