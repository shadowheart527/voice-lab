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

say "generated web tables"
python3 scripts/gen-web-tables.py

cat <<'EOF'

Bootstrap complete. Next:

  engine/launcher/rebuild.sh        build the desktop engine
  engine/launcher/rebuild.sh df     ... with the DeepFormants tracker

  cd browser && python3 -m http.server 8181 --bind 127.0.0.1
                                    serve the browser build

Model weights for the gender probe are fetched separately:
  ml/.venv/bin/python ml/gender_probe/fetch_model.py
EOF
