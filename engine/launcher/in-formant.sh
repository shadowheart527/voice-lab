#!/bin/bash
# Launch in-formant (standard build).
#
# Bazzite is an immutable image and does not ship the Qt6/FFTW/PulseAudio development
# headers, so in-formant is built and run inside the `informant-build` Fedora 44
# distrobox. distrobox shares the Wayland/PipeWire sockets and /dev/dri, so the GUI
# and audio behave as they would natively.
set -e

CONTAINER=informant-build
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$SRC/build-fedora/in-formant"

if [ ! -x "$BIN" ]; then
    echo "in-formant is not built yet: $BIN missing." >&2
    echo "Rebuild with:  $SRC/launcher/rebuild.sh" >&2
    exit 1
fi

# in-formant appends InFormant.log to its working directory, so run it from a state
# directory (not the repo) and keep only the previous run's log.
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/in-formant"
mkdir -p "$LOGDIR"
[ -f "$LOGDIR/InFormant.log" ] && mv -f "$LOGDIR/InFormant.log" "$LOGDIR/InFormant.log.1"

exec distrobox enter --name "$CONTAINER" -- \
    bash -lc "cd '$LOGDIR' && exec '$BIN' \"\$@\"" -- "$@"
