#!/bin/bash
# Build the shared analysis core to WebAssembly.
#
# These are the SAME sources the desktop app compiles: same pitch solvers, same
# linear-prediction and formant solvers, same spectral-tilt measure, same
# gender model, and the same FFTW and libsamplerate. DeepFormants (libtorch)
# and gci/sigma (armadillo) are excluded; neither has a browser equivalent and
# neither is on this analysis path.
set -e
# Resolve the repository root ONCE, before any cd: deriving it a second time
# afterwards would resolve against the new working directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT/engine"
OUT=../browser/wasm
SCRATCH="$ROOT/.wasm-build"
OBJ="$SCRATCH/wasm-obj"
mkdir -p "$OUT" "$OBJ"

INCLUDES="-I src -I src/analysis -I external/rpmalloc/rpmalloc -I external/eigen
          -I external/tomlplusplus/include -I external/armadillo/include
          -I external/fftw-wasm/include -I external/libsamplerate/src -I wasm-support"

CPP_SRC="src/wasm/voicelab_wasm.cpp
src/analysis/weight/tilt.cpp
src/analysis/fft/realfft.cpp
src/analysis/fft/complexfft.cpp
src/analysis/fft/realrealfft.cpp
src/analysis/fft/fft_n.cpp
src/analysis/fft/wisdom.cpp
src/analysis/pitch/yin.cpp
src/analysis/pitch/mpm.cpp
src/analysis/pitch/rapt.cpp
src/analysis/linpred/autocorr.cpp
src/analysis/linpred/burg.cpp
src/analysis/linpred/covar.cpp
src/analysis/formant/filteredlp.cpp
src/analysis/formant/simplelp.cpp
src/analysis/util/aberth.cpp
src/analysis/util/calc_formant.cpp"

for f in src/analysis/filter/*.cpp src/analysis/util/*.cpp; do
    case "$f" in *aberth.cpp|*calc_formant.cpp) continue;; esac
    CPP_SRC="$CPP_SRC $f"
done

C_SRC="external/libsamplerate/src/samplerate.c
external/libsamplerate/src/src_linear.c
external/libsamplerate/src/src_sinc.c
external/libsamplerate/src/src_zoh.c"

OBJS=""
for f in $C_SRC; do
    o="$OBJ/$(basename "$f" .c).o"
    emcc -O3 $INCLUDES -DEMSCRIPTEN -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in $CPP_SRC; do
    o="$OBJ/$(basename "$f" .cpp).oxx"
    emcc -O3 -std=c++17 $INCLUDES -DEMSCRIPTEN -DWITHOUT_SYNTH -c "$f" -o "$o"
    OBJS="$OBJS $o"
done

em++ $OBJS external/fftw-wasm/lib/libfftw3.a \
    -O3 \
    -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
    -s EXPORT_NAME=createVoiceLab \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ENVIRONMENT=web,worker,node \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPF32"]' -s EXPORTED_FUNCTIONS='["_malloc","_free","_vl_init","_vl_analyze","_vl_voiced","_vl_pitch","_vl_f1","_vl_f2","_vl_f3","_vl_f4","_vl_tilt","_vl_pitch_score","_vl_resonance_score","_vl_resonance_r","_vl_overall_score","_vl_site_resonance","_vl_weight","_vl_spectrogram"]' \
    -o "$OUT/voicelab.mjs"

echo "built $OUT/voicelab.wasm: $(stat -c%s "$OUT/voicelab.wasm") bytes"
