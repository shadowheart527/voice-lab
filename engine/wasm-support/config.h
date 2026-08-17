/* Minimal config.h for building libsamplerate under Emscripten, which its
   CMake refuses to cross-compile. Only the feature flags its sources read. */
#define HAVE_STDBOOL_H 1
#define ENABLE_SINC_FAST_CONVERTER 1
#define ENABLE_SINC_MEDIUM_CONVERTER 1
#define ENABLE_SINC_BEST_CONVERTER 1
#define PACKAGE "libsamplerate"
#define VERSION "0.2.2"

/* wasm32 float-to-int conversion truncates toward zero and does not clip,
   matching the "no clipping" case libsamplerate compensates for. */
#define CPU_CLIPS_POSITIVE 0
#define CPU_CLIPS_NEGATIVE 0
