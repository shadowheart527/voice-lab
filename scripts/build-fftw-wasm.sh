#!/bin/bash
# Build the real FFTW for WebAssembly, so the browser uses the same FFT the
# desktop does rather than a substitute with different numerics.
set -e
PREFIX="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/engine/external/fftw-wasm"
SRC="$(mktemp -d)"
mkdir -p "$SRC" && cd "$SRC"
if [ ! -d fftw-3.3.10 ]; then
    curl -sL http://www.fftw.org/fftw-3.3.10.tar.gz -o fftw.tar.gz
    tar xzf fftw.tar.gz
fi
cd fftw-3.3.10
emconfigure ./configure --prefix="$PREFIX" \
    --disable-fortran --disable-threads --disable-shared --enable-static \
    --host=x86_64-linux-gnu CFLAGS="-O3" > /dev/null
emmake make -j$(nproc) > /dev/null 2>&1
emmake make install > /dev/null 2>&1
echo "FFTW for wasm installed to $PREFIX"
ls -la "$PREFIX/lib/libfftw3.a"
