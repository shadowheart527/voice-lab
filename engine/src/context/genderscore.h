#ifndef MAIN_CONTEXT_GENDERSCORE_H
#define MAIN_CONTEXT_GENDERSCORE_H

#include <algorithm>
#include <cmath>

// The gender-read model, shared by the on-screen meter/track coloring and the
// live WebSocket feed so the two can never drift apart.
//
// Anchors are in THIS tracker's units, not raw literature values: measured
// against synthesized ground truth, the LP tracker reads F1 ~40-95 Hz low and
// F3 ~200 Hz high, so the literature means (480/590, 2500/2950) are shifted
// accordingly. F2 tracks true. Pitch logistic centered at 162 Hz per Gelfer &
// Schofield's 164-199 Hz fem zone; the 55/45 pitch/resonance split follows
// Hillenbrand & Clark (2009).
namespace Main::GenderScore {

    inline double logistic(double x)
    {
        return 1.0 / (1.0 + std::exp(-x));
    }

    inline constexpr double kFormantAnchors[3][2] = {
        { 420.0, 550.0 },
        { 1350.0, 1650.0 },
        { 2700.0, 3150.0 },
    };

    // Position of a pitch value on the masc(0)..fem(1) perception axis.
    inline double pitchP(double f)
    {
        return logistic((f - 162.0) / 12.0);
    }

    // Normalized position of a formant between its masc(0) and fem(1) anchors.
    inline double formantR(int i, double f)
    {
        const double lo = kFormantAnchors[i][0];
        const double hi = kFormantAnchors[i][1];
        return std::clamp((f - lo) / (hi - lo), -0.5, 1.5);
    }

    // Same as pitchP for a single formant (i = 0..2 for F1..F3).
    inline double formantP(int i, double f)
    {
        if (i < 0 || i > 2) {
            return 0.5;
        }
        return logistic((formantR(i, f) - 0.5) / 0.25);
    }

    // Combined resonance position in r-space: blend weighting F2 highest
    // (most gender-salient formant), then F3 (vocal-tract-length proxy), then
    // F1 (most vowel-dependent). Roughly 0 at the masc anchors, 1 at the fem
    // anchors, clamped range -0.5..1.5. This linear value is the vocal-size
    // axis of the TVL fullness page: unlike the phoneme-normalized site
    // scale, it responds fully to whole-tract size shifts on sustained
    // vowels (the site scale self-normalizes those toward 0.5 by design).
    inline double resonanceR(double f1, double f2, double f3)
    {
        return 0.25 * formantR(0, f1)
             + 0.45 * formantR(1, f2)
             + 0.30 * formantR(2, f3);
    }

    inline double resonanceP(double f1, double f2, double f3)
    {
        return logistic((resonanceR(f1, f2, f3) - 0.5) / 0.25);
    }

    inline double overallP(double pF0, double pRes)
    {
        return 0.55 * pF0 + 0.45 * pRes;
    }

    // Vocal weight percept (TVL sense) from the uncorrected harmonic tilt
    // (Theil-Sen, SNR-gated): 0 = light (steep roll-off), 1 = heavy (flat).
    // Anchored against the TVL video's labelled demonstrations: their
    // "light sounds" measure ~-9 dB/oct (maps to ~0.19) and the "heavy sound"
    // ~-3.8 (~0.84). Recording chains shift this scale a few dB/oct either
    // way, so treat the anchors as a starting point tunable per setup.
    inline double weightP(double tiltDbOct)
    {
        return std::clamp((tiltDbOct + 14.82) / 9.16, 0.0, 1.0);
    }

}

#include "genderspace_table.h"

namespace Main::GenderScore {

    // Resonance on acousticgender.space's scale (0.5 = population average for
    // the phoneme being spoken), as opposed to the masc/fem logistic above,
    // which is steeper by design and reads far higher for fem-of-center
    // voices. Reproduces the site's per-phoneme z-score without knowing the
    // phoneme: map this tracker's F1/F2 into praat units (robust fits against
    // the site's 21 reference clips), soft-assign a phoneme by closeness in
    // z-space, and average the site's exact score formula under that
    // assignment. Cross-validated agreement with the official per-clip
    // medians: ~0.07 mean absolute error, ~0.27 worst case (extreme voices;
    // the residual is praat-vs-LP formant engine disagreement, not mapping).
    inline double siteResonance(double f1t, double f2t, bool deepFormants = false)
    {
        namespace T = Main::GenderSpaceTable;
        const double *m1 = deepFormants ? T::kMapDfF1 : T::kMapLpcF1;
        const double *m2 = deepFormants ? T::kMapDfF2 : T::kMapLpcF2;
        const double f1 = m1[0] * f1t + m1[1];
        const double f2 = m2[0] * f2t + m2[1];

        double num = 0.0, den = 0.0;
        for (int i = 0; i < T::kNumPhones; ++i) {
            const auto &st = T::kPhoneStats[i];
            const double z1 = (f1 - st.m1) / st.s1;
            const double z2 = (f2 - st.m2) / st.s2;
            const double w = std::exp(-0.5 * (z1 * z1 + z2 * z2));
            if (w < 1e-9) {
                continue;
            }
            const double r = std::clamp(
                    (T::kW1 * z1 + T::kW2 * z2) / 3.0 + 0.5, 0.0, 1.0);
            num += w * r;
            den += w;
        }
        if (den <= 0.0) {
            return 0.5;
        }
        return std::clamp(num / den, 0.0, 1.0);
    }

}

#endif // MAIN_CONTEXT_GENDERSCORE_H
