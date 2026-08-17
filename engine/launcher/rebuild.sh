#!/bin/bash
# Rebuild in-formant inside the informant-build distrobox.
#   ./rebuild.sh          -> standard build   (build-fedora)
#   ./rebuild.sh df       -> DeepFormants     (build-fedora-df, needs external/libtorch)
set -e

CONTAINER=informant-build
SRC=/var/home/shadowheart527/git/informant

if [ "$1" = "df" ]; then
    BUILDDIR=build-fedora-df
    TORCH=ON
else
    BUILDDIR=build-fedora
    TORCH=OFF
fi

distrobox enter --name "$CONTAINER" -- bash -lc "
    set -e
    cd '$SRC'
    cmake -S . -B '$BUILDDIR' -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DENABLE_TORCH=$TORCH \
        -DCMAKE_PREFIX_PATH='$SRC/external/libtorch'
    cmake --build '$BUILDDIR' -j\$(nproc)
"

echo "built $BUILDDIR"
