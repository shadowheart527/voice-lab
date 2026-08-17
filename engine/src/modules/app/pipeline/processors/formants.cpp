#include "formants.h"

#include "../../../../analysis/analysis.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace Module::App::Processors;

Formants::Formants(Main::Config *config, Main::DataStore *dataStore,
            std::shared_ptr<Analysis::LinpredSolver>& linpredSolver,
            std::shared_ptr<Analysis::FormantSolver>& formantSolver)
    : BaseProcessor(config->getAnalysisFormantSpacing(),
                    config->getAnalysisFormantWindow()),
      mConfig(config),
      mDataStore(dataStore),
      mLinpredSolver(linpredSolver),
      mFormantSolver(formantSolver),
      mLastSample(0.0)
{
}

void Formants::processData(const rpm::vector<double>& data, double sampleRate)
{
    // Same voice-activity gate as the pitch processor; formant estimates on
    // background noise are meaningless and this also skips the solver cost.
    if (mDataStore->getVoiceActivity() < 0.5) {
        mDataStore->beginWrite();
        for (int i = 0; i < mDataStore->getFormantTrackCount(); ++i) {
            mDataStore->getFormantTrack(i).insert(getCenteredTime(), std::nullopt);
        }
        mDataStore->endWrite();
        return;
    }

    constexpr double preemphFrequency = 200.0;
    const double preemphFactor = exp(-(2.0 * M_PI * preemphFrequency) / sampleRate);

    if (mWindow.size() != data.size()) {
        mWindow = Analysis::gaussianWindow((int) data.size(), 2.5);
    }

    constexpr double fsLPC = 11000;
    mResamplerLPC.setRate(sampleRate, fsLPC);

#ifdef ENABLE_TORCH
    constexpr double fs16k = 16000;
    mResampler16k.setRate(sampleRate, fs16k);
#endif

    auto data2 = data;
    for (int i = (int) data.size() - 1; i >= 1; --i) {
        data2[i] = mWindow[i] * (data2[i] - preemphFactor * data2[i - 1]);
    }
    data2[0] = mWindow[0] * (data[0] - preemphFactor * mLastSample);
    mLastSample = data[0];

    static const bool debugProf = (std::getenv("IF_DEBUG_PROF") != nullptr);
    const auto tResample0 = std::chrono::steady_clock::now();

    auto mLPC = mResamplerLPC.process(data2);

    const auto tResample1 = std::chrono::steady_clock::now();

    rpm::vector<double> lpc;

#ifdef ENABLE_TORCH
    if (auto dfSolver = dynamic_cast<Analysis::Formant::DeepFormants *>(mFormantSolver.get())) {
        auto m16k = mResampler16k.process(data2);
        dfSolver->setFrameAudio(m16k);
    }
    else {
#endif
        double gain;
        lpc = mLinpredSolver->solve(mLPC.data(), (int) mLPC.size(), 10, &gain);
#ifdef ENABLE_TORCH
    }
#endif

    const auto tLpc1 = std::chrono::steady_clock::now();

    auto formantResult = mFormantSolver->solve(lpc.data(), (int) lpc.size(), fsLPC);

    if (debugProf) {
        const auto tSolve1 = std::chrono::steady_clock::now();
        using us = std::chrono::microseconds;
        std::cout << "PROFF],"
                  << std::chrono::duration_cast<us>(tResample1 - tResample0).count() << ","
                  << std::chrono::duration_cast<us>(tLpc1 - tResample1).count() << ","
                  << std::chrono::duration_cast<us>(tSolve1 - tLpc1).count() << std::endl;
    }

    static const bool debugTracks = (std::getenv("IF_DEBUG_TRACKS") != nullptr);
    if (debugTracks) {
        std::cout << "TRKF]," << getCenteredTime();
        for (int i = 0; i < std::min(3, (int) formantResult.formants.size()); ++i) {
            std::cout << "," << formantResult.formants[i].frequency;
        }
        std::cout << std::endl;
    }

    mDataStore->beginWrite();

    const int actualFormantCount = std::min(
        mDataStore->getFormantTrackCount(),
        (int) formantResult.formants.size());

    for (int i = 0; i < actualFormantCount; ++i) {
        const double frequency = formantResult.formants[i].frequency;
        if (std::isnormal(frequency)) {
            mDataStore->getFormantTrack(i).insert(getCenteredTime(), frequency);
        }
        else {
            mDataStore->getFormantTrack(i).insert(getCenteredTime(), std::nullopt);
        }
    }

    for (int i = (int) formantResult.formants.size(); i < mDataStore->getFormantTrackCount(); ++i) {
        mDataStore->getFormantTrack(i).insert(getCenteredTime(), std::nullopt);
    }

    mDataStore->endWrite();
}