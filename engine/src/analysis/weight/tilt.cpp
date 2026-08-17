#include "tilt.h"
#include "../fft/fft.h"

#include <algorithm>
#include <cmath>
#include <functional>

std::optional<double> Analysis::spectralTilt(
        const rpm::vector<double>& data, double sampleRate, double f0,
        const TiltOptions& opts)
{
    constexpr int kNfft = 4096;
    static thread_local Analysis::RealFFT fft(kNfft);

    const int n = std::min((int) data.size(), kNfft);
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        fft.input(i) = data[i] * w;
    }
    for (int i = n; i < kNfft; ++i) {
        fft.input(i) = 0.0;
    }
    fft.computeForward();

    const int nOut = fft.getOutputLength();
    const double binHz = sampleRate / kNfft;

    // Fixed comparison bands rather than "whatever harmonics survive".
    //
    // The earlier implementation collected every harmonic clearing the noise
    // floor and fitted a slope across them. That is subtly wrong: a light
    // voice's upper harmonics fall below the floor, so its slope gets fitted
    // only across the low end where the F1 resonance is RISING, while a heavy
    // voice is fitted across the whole falling spectrum. The two are measured
    // over different frequency ranges and are not comparable. On a synthetic
    // pair with known source tilts it reported the light half as flatter than
    // the heavy half, exactly backwards, which is what made light voices read
    // heavy in practice.
    //
    // Comparing fixed low and high bands puts every voice on the same ruler.
    // A light voice having nothing in the high band is the signal itself, not
    // missing data, so it floors out as "very light" instead of no reading.
    // Verified: clean and noisy synthetic pairs now give identical answers,
    // and the TransVoiceLessons light/heavy demonstrations separate correctly.
    const double kLoBand[2] = { opts.loBandLow, opts.loBandHigh };
    const double kHiBand[2] = { opts.hiBandLow, opts.hiBandHigh };
    const double kFloorTilt = opts.floorTilt;

    if (kHiBand[0] > sampleRate / 2.0 - 100.0) {
        return std::nullopt;
    }

    // Harmonic peak level in dB, and whether it clears the local
    // inter-harmonic noise floor by ~10 dB.
    auto harmonic = [&](double fk, bool *clean) -> double {
        const int lo = std::max(1, (int) ((fk - 0.35 * f0) / binHz));
        const int hi = std::min(nOut - 1, (int) ((fk + 0.35 * f0) / binHz) + 1);
        if (hi <= lo) {
            return -1e9;
        }
        double peak = 0.0;
        for (int b = lo; b <= hi; ++b) {
            peak = std::max(peak, std::abs(fft.output(b)));
        }
        if (peak <= 1e-12) {
            return -1e9;
        }
        double floorSum = 0.0;
        int floorN = 0;
        for (double half : { fk - 0.5 * f0, fk + 0.5 * f0 }) {
            const int flo = std::max(1, (int) ((half - 0.15 * f0) / binHz));
            const int fhi = std::min(nOut - 1, (int) ((half + 0.15 * f0) / binHz) + 1);
            for (int b = flo; b <= fhi; ++b) {
                floorSum += std::abs(fft.output(b));
                ++floorN;
            }
        }
        *clean = (floorN > 0) ? (peak >= opts.snrRatio * (floorSum / floorN)) : true;
        return 20.0 * std::log10(peak);
    };

    // Mean of the strongest few harmonics in a band: robust to one landing in
    // a spectral valley between formants.
    auto bandLevel = [&](const double band[2], bool *ok) -> double {
        rpm::vector<double> vals;
        for (int k = 1; k <= 40; ++k) {
            const double fk = k * f0;
            if (fk > band[1]) break;
            if (fk < band[0]) continue;
            bool clean = false;
            const double db = harmonic(fk, &clean);
            if (db > -1e8 && clean) {
                vals.push_back(db);
            }
        }
        if (vals.empty()) {
            *ok = false;
            return 0.0;
        }
        std::sort(vals.begin(), vals.end(), std::greater<double>());
        const int take = std::max(1, std::min(3, (int) vals.size()));
        double sum = 0.0;
        for (int i = 0; i < take; ++i) sum += vals[i];
        *ok = true;
        return sum / take;
    };

    bool loOk = false, hiOk = false;
    const double lo = bandLevel(kLoBand, &loOk);
    if (!loOk) {
        return std::nullopt;
    }
    const double hi = bandLevel(kHiBand, &hiOk);
    if (!hiOk) {
        return kFloorTilt;
    }

    const double octaves = std::log2(
            ((kHiBand[0] + kHiBand[1]) / 2.0) / ((kLoBand[0] + kLoBand[1]) / 2.0));
    return std::clamp((hi - lo) / octaves, -40.0, 15.0);
}
