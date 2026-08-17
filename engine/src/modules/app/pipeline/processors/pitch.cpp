#include "pitch.h"

#include "../../../../analysis/analysis.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace Module::App::Processors;

Pitch::Pitch(Main::Config *config, Main::DataStore *dataStore,
            std::shared_ptr<Analysis::PitchSolver>& pitchSolver)
    : BaseProcessor(config->getAnalysisPitchSpacing(),
                    config->getAnalysisPitchWindow()),
      mConfig(config),
      mDataStore(dataStore),
      mPitchSolver(pitchSolver)
{
}

void Pitch::processData(const rpm::vector<double>& data, double sampleRate)
{
    // Voice-activity gate from the denoiser: on background noise the pitch solver
    // happily tracks hum and rumble as if it were voice. Skip the solve entirely
    // and record unvoiced instead. Activity is pinned to 1.0 when denoising is off.
    if (mDataStore->getVoiceActivity() < 0.5) {
        mDataStore->beginWrite();
        mDataStore->getPitchTrack().insert(getCenteredTime(), std::nullopt);
        mDataStore->getWeightTrack().insert(getCenteredTime(), std::nullopt);
        mDataStore->endWrite();
        return;
    }

    auto pitchResult = mPitchSolver->solve(data.data(), (int) data.size(), sampleRate);

    // Opt-in probe (IF_DEBUG_TRACKS=1): raw per-frame estimates, for quantifying
    // estimate jitter against a known constant input.
    static const bool debugTracks = (std::getenv("IF_DEBUG_TRACKS") != nullptr);
    if (debugTracks) {
        std::cout << "TRKP]," << getCenteredTime() << ","
                  << (pitchResult.voiced ? pitchResult.pitch : -1.0) << std::endl;
    }

    // Vocal weight (TVL sense: fold-mass percept, acoustically the spectral
    // roll-off of the source). Measured as the dB-per-octave slope across the
    // harmonic peaks, with the vocal tract's formant shaping removed
    // analytically so the tilt reflects phonation rather than vowel. Validated
    // against synthesized ground truth to within 0.05 dB/oct
    // (tools/genderspace-calib/weight_proto.py).
    std::optional<double> tilt;
    if (pitchResult.voiced && pitchResult.pitch > 40.0) {
        tilt = computeSpectralTilt(data, sampleRate, pitchResult.pitch);
    }

    mDataStore->beginWrite();

    if (pitchResult.voiced) {
        mDataStore->getPitchTrack().insert(getCenteredTime(), pitchResult.pitch);
    }
    else {
        mDataStore->getPitchTrack().insert(getCenteredTime(), std::nullopt);
    }
    mDataStore->getWeightTrack().insert(getCenteredTime(), tilt);

    mDataStore->endWrite();
}

std::optional<double> Pitch::computeSpectralTilt(
        const rpm::vector<double>& data, double sampleRate, double f0)
{
    constexpr int kNfft = 4096;
    static Analysis::RealFFT fft(kNfft);

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
    constexpr double kLoBand[2] = { 190.0, 900.0 };
    constexpr double kHiBand[2] = { 1500.0, 3200.0 };
    constexpr double kFloorTilt = -20.0;

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
        *clean = (floorN > 0) ? (peak >= 3.2 * (floorSum / floorN)) : true;
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