#include "pitch.h"

#include "../../../../analysis/analysis.h"
#include "../../../../analysis/weight/tilt.h"

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
        tilt = Analysis::spectralTilt(data, sampleRate, pitchResult.pitch);
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
