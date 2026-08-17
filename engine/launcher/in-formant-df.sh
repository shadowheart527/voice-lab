#!/bin/bash
# Launch in-formant (DeepFormants build).
#
# Same distrobox arrangement as in-formant.sh, but this binary is linked against the
# bundled libtorch (rpath is baked in, so no LD_LIBRARY_PATH needed) and additionally
# offers the neural DeepFormants tracker in the sidebar's formant algorithm list.
set -e

CONTAINER=informant-build
SRC=/var/home/shadowheart527/git/informant
BIN="$SRC/build-fedora-df/in-formant"

if [ ! -x "$BIN" ]; then
    echo "in-formant (DeepFormants) is not built yet: $BIN missing." >&2
    echo "Rebuild with:  $SRC/launcher/rebuild.sh df" >&2
    exit 1
fi

LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/in-formant"
mkdir -p "$LOGDIR"
[ -f "$LOGDIR/InFormant.log" ] && mv -f "$LOGDIR/InFormant.log" "$LOGDIR/InFormant.log.1"

exec distrobox enter --name "$CONTAINER" -- \
    bash -lc "cd '$LOGDIR' && exec '$BIN' \"\$@\"" -- "$@"
