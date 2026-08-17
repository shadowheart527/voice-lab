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

    // No formant correction. Correcting with the TRUE tract envelope is exact
    // (verified on synthetics), but live we only have the tracked formants,
    // and those degrade precisely on the light voices that matter most --
    // little energy above 1 kHz leaves LPC fitting noise, and a wrong formant
    // makes the correction inject large slope errors. Measured across the
    // reference speakers, the uncorrected Theil-Sen slope tracks the
    // true-corrected one with a consistent +7 dB/oct offset (sd 1.5), i.e. it
    // preserves ordering and spread; the offset is absorbed by the weightP
    // anchors. Robustness beats exactness here.

    // Harmonic peak amplitudes: skip below 160 Hz (recording highpass region),
    // cap at 3.2 kHz / 14 harmonics.
    //
    // SNR gate: a peak only counts as a harmonic if it clears the local noise
    // floor (the between-harmonics valley) by a margin. Without this, a light
    // or breathy voice whose upper harmonics sink below the room/mic noise
    // floor gets the noise measured as "harmonics", the slope flattens, and
    // light voices read as heavy. (Exactly what a webcam mic in a normal room
    // produced in practice; the noise-free synthetic validation never saw it.)
    rpm::vector<double> octs, amps;
    double firstF = -1.0;
    for (int k = 1; k <= 14; ++k) {
        const double fk = k * f0;
        if (fk >= std::min(3200.0, sampleRate / 2.0 - 100.0)) {
            break;
        }
        if (fk < 160.0) {
            continue;
        }
        const int lo = std::max(1, (int) ((fk - 0.35 * f0) / binHz));
        const int hi = std::min(nOut - 1, (int) ((fk + 0.35 * f0) / binHz) + 1);
        if (hi <= lo) {
            continue;
        }
        double best = 0.0;
        double bestF = fk;
        for (int b = lo; b <= hi; ++b) {
            const double m = std::abs(fft.output(b));
            if (m > best) {
                best = m;
                bestF = b * binHz;
            }
        }
        if (best <= 1e-12) {
            continue;
        }

        // Noise floor at the inter-harmonic valley around (k +- 1/2) * f0.
        double floorMag = 0.0;
        int floorN = 0;
        for (double half : { fk - 0.5 * f0, fk + 0.5 * f0 }) {
            const int flo = std::max(1, (int) ((half - 0.15 * f0) / binHz));
            const int fhi = std::min(nOut - 1, (int) ((half + 0.15 * f0) / binHz) + 1);
            for (int b = flo; b <= fhi; ++b) {
                floorMag += std::abs(fft.output(b));
                ++floorN;
            }
        }
        if (floorN > 0) {
            floorMag /= floorN;
            // < ~10 dB above the valley: not a harmonic. The peak is a max over
            // ~10 bins, so noise alone clears a 6 dB bar too often.
            if (best < 3.2 * floorMag) {
                continue;
            }
        }

        const double db = 20.0 * std::log10(best);
        if (firstF < 0.0) {
            firstF = bestF;
        }
        octs.push_back(std::log2(bestF / firstF));
        amps.push_back(db);
    }

    // Three valid harmonics suffice: the SNR gate legitimately strips a light
    // voice down to its lowest few, and the per-frame noise is absorbed by the
    // one-second median downstream.
    if (octs.size() < 3) {
        return std::nullopt;
    }

    // Theil-Sen slope in dB per octave: the median of pairwise slopes shrugs
    // off the occasional noise peak that survives the SNR gate, where a
    // least-squares fit lets one flattened point drag the whole tail.
    const int m = (int) octs.size();
    rpm::vector<double> slopes;
    slopes.reserve(m * (m - 1) / 2);
    for (int i = 0; i < m; ++i) {
        for (int j = i + 1; j < m; ++j) {
            const double dx = octs[j] - octs[i];
            if (std::abs(dx) > 1e-9) {
                slopes.push_back((amps[j] - amps[i]) / dx);
            }
        }
    }
    if (slopes.empty()) {
        return std::nullopt;
    }
    std::nth_element(slopes.begin(),
            std::next(slopes.begin(), (int) slopes.size() / 2), slopes.end());
    const double slope = slopes[slopes.size() / 2];
    if (slope < -40.0 || slope > 15.0) {
        return std::nullopt;
    }
    return slope;
}