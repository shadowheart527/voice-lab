#ifndef ANALYSIS_WEIGHT_TILT_H
#define ANALYSIS_WEIGHT_TILT_H

#include "rpcxx.h"
#include <optional>

namespace Analysis {

    // Vocal weight in the TransVoiceLessons sense (the fold-mass percept),
    // measured as harmonic spectral roll-off in dB per octave: flat is
    // heavy/buzzy, steep is light.
    //
    // Fixed comparison bands rather than "whatever harmonics survive". The
    // obvious implementation, collecting every harmonic that clears the noise
    // floor and fitting a slope, is subtly wrong and inverts on real signals: a
    // light voice's upper harmonics fall below the floor, so its slope gets
    // fitted only across the low end where the F1 resonance is RISING, while a
    // heavy voice is fitted across the whole falling spectrum. Measured over
    // different frequency ranges, the two are not comparable.
    //
    // Comparing fixed bands puts every voice on the same ruler, and a light
    // voice having nothing in the high band is the signal itself rather than
    // missing data, so it floors out instead of returning nothing.
    //
    // Lives in the shared core so the desktop app and the WebAssembly build
    // compute it from the same source; it used to be duplicated, and the fix
    // above had to be made twice.
    struct TiltOptions {
        double loBandLow = 190.0;
        double loBandHigh = 900.0;
        double hiBandLow = 1500.0;
        double hiBandHigh = 3200.0;
        double floorTilt = -20.0;
        double snrRatio = 3.2;   // ~10 dB over the inter-harmonic valley
    };

    std::optional<double> spectralTilt(
            const rpm::vector<double>& data, double sampleRate, double f0,
            const TiltOptions& opts = TiltOptions());

}

#endif // ANALYSIS_WEIGHT_TILT_H
