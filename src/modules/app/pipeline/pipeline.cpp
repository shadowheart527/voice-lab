#include "pipeline.h"
#include "../../../analysis/filter/filter.h"
#include "../../../synthesis/synthesis.h"
#include "../../../context/timings.h"
#include "../../../context/contextmanager.h"

#include "processors/spectrogram.h"
#include "processors/pitch.h"
#include "processors/formants.h"
#include "processors/oscilloscope.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <rnnoise.h>

using namespace Module::App;

Pipeline::Pipeline(Module::Audio::Buffer *captureBuffer,
                Main::DataStore *dataStore, Main::Config *config,
                std::shared_ptr<Analysis::PitchSolver>& pitchSolver,
                std::shared_ptr<Analysis::LinpredSolver>& linpredSolver,
                std::shared_ptr<Analysis::FormantSolver>& formantSolver,
                std::shared_ptr<Analysis::InvglotSolver>& invglotSolver)
    : mCaptureBuffer(captureBuffer),
      mDataStore(dataStore),
      mConfig(config),
      mTime(0),
      mThreadRunning(false),
      mStopThread(false),
      mBuffer(16000)
{
    mProcessors.push_back(std::make_unique<Processors::Spectrogram>(config, dataStore));
    mProcessors.push_back(std::make_unique<Processors::Pitch>(config, dataStore, pitchSolver));
    mProcessors.push_back(std::make_unique<Processors::Formants>(config, dataStore, linpredSolver, formantSolver));
    mProcessors.push_back(std::make_unique<Processors::Oscilloscope>(config, dataStore, invglotSolver));
}

Pipeline::~Pipeline()
{
    mThreadRunning = false;
    mStopThread = true;

    Module::Audio::Buffer::cancelPulls();

    if (mProcessingThread.joinable())
        mProcessingThread.join();

    if (mDenoiseState != nullptr) {
        rnnoise_destroy(mDenoiseState);
        mDenoiseState = nullptr;
    }
}

void Pipeline::computeVoiceActivity(const rpm::vector<double>& block)
{
    // RNNoise is used for its voice-activity detection ONLY; the analyses always
    // run on the raw signal. Feeding the trackers RNNoise's denoised output turned
    // out to be a trap: a steady held tone is exactly what a speech denoiser
    // treats as stationary background, so sustained-note drills (and low drones)
    // were progressively eaten -- a 110 Hz test tone came out tracking at 202 Hz.
    // Gating on its VAD keeps the actual win (no tracking of background noise)
    // without ever altering what is analysed.
    if ((int) std::lround(mSampleRate) != 48000) {
        if (!mDenoiseBypassLogged) {
            std::cout << "Pipeline] Noise gate needs 48 kHz capture, got "
                      << mSampleRate << " Hz; bypassing" << std::endl;
            mDenoiseBypassLogged = true;
        }
        mDataStore->setVoiceActivity(1.0);
        return;
    }

    if (mDenoiseState == nullptr) {
        mDenoiseState = rnnoise_create(nullptr);
    }

    const int frameSize = rnnoise_get_frame_size();

    // RNNoise expects float samples at int16 scale.
    for (const double& v : block) {
        mDenoiseInFifo.push_back((float) (v * 32768.0));
    }

    static rpm::vector<float> frameOut;
    frameOut.resize(frameSize);

    double vadMax = 0.0;
    bool processed = false;
    while ((int) mDenoiseInFifo.size() >= frameSize) {
        const float vad = rnnoise_process_frame(mDenoiseState, frameOut.data(), mDenoiseInFifo.data());
        mDenoiseInFifo.erase(mDenoiseInFifo.begin(), std::next(mDenoiseInFifo.begin(), frameSize));
        vadMax = std::max(vadMax, (double) vad);
        processed = true;
    }

    if (processed) {
        // Fast attack, ~120 ms release: speech pauses between words should not
        // slam the gate shut mid-sentence.
        mVadSmooth = std::max(vadMax, mVadSmooth * 0.85);
        mDataStore->setVoiceActivity(mVadSmooth);
    }
}

void Pipeline::callbackProcessing()
{
    rpm::vector<double> block;
    rpm::vector<double> slidingWindow;

    double time = 0;

    while (mThreadRunning && !mStopThread) {
        const double granularity = mConfig->getAnalysisGranularity() / 1000;

        block.resize(granularity * mSampleRate);
        mBuffer.pull(block.data(), (int) block.size());

        if (mConfig->getAnalysisDenoise()) {
            computeVoiceActivity(block);
        }
        else {
            mDataStore->setVoiceActivity(1.0);
            if (!mDenoiseInFifo.empty()) {
                mDenoiseInFifo.clear();
                mVadSmooth = 0.0;
            }
        }

        timer_guard timer(timings::update);

        double maxFrameLength = granularity;
        for (const auto& processor : mProcessors) {
            const double frameLength = processor->getFrameLength();
            if (frameLength > maxFrameLength)
                maxFrameLength = frameLength;
        }

        slidingWindow.resize(maxFrameLength * mSampleRate, 0.0);
        std::rotate(slidingWindow.begin(),
                std::next(slidingWindow.begin(), block.size()),
                slidingWindow.end());
        std::copy(block.begin(), block.end(), std::prev(slidingWindow.end(), block.size()));

        // Opt-in probe (IF_DEBUG_PROF=1): per-processor cost in microseconds, printed
        // per tick. Index order: 0=spectrogram 1=pitch 2=formants 3=oscilloscope.
        static const bool debugProf = (std::getenv("IF_DEBUG_PROF") != nullptr);

        int processed = 0;
        for (size_t pi = 0; pi < mProcessors.size(); ++pi) {
            auto& processor = mProcessors[pi];
            if (processor->canProcess(time)) {
                if (debugProf) {
                    const auto t0 = std::chrono::steady_clock::now();
                    processor->process(slidingWindow, mSampleRate, time);
                    const auto t1 = std::chrono::steady_clock::now();
                    std::cout << "PROF]," << pi << ","
                              << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                              << std::endl;
                }
                else {
                    processor->process(slidingWindow, mSampleRate, time);
                }
                ++processed;
            }
        }

        // If analysis fell behind (a system stall, a burst of lock contention), drain
        // the backlog by advancing the sliding window without re-running the
        // analyses. Without this the loop consumes exactly one block per iteration
        // regardless of backlog, so every overrun accumulates forever and the data
        // clock drifts steadily behind the wall clock.
        int drained = 0;
        while ((int) mBuffer.getLength() > 3 * (int) block.size() && drained < 50) {
            mBuffer.pull(block.data(), (int) block.size());
            std::rotate(slidingWindow.begin(),
                    std::next(slidingWindow.begin(), block.size()),
                    slidingWindow.end());
            std::copy(block.begin(), block.end(),
                    std::prev(slidingWindow.end(), block.size()));
            time += granularity;
            ++drained;
        }
        if (drained > 0) {
            std::cout << "Pipeline] Fell behind; skipped analysis on " << drained
                      << " blocks to catch up" << std::endl;
        }

        // Opt-in probe (IF_DEBUG_AUDIO=1): reports what actually reaches the analysis
        // stage, which is the fastest way to tell "no audio arriving" apart from
        // "audio arriving but not drawn".
        static const bool debugAudio = (std::getenv("IF_DEBUG_AUDIO") != nullptr);
        if (debugAudio) {
            static int dbgCount = 0;
            if ((dbgCount++ % 50) == 0) {
                double sumsq = 0.0;
                for (double v : block) sumsq += v * v;
                const double rms = block.empty() ? 0.0 : std::sqrt(sumsq / block.size());
                std::cout << "Pipeline] block=" << block.size()
                          << " fs=" << mSampleRate
                          << " rms=" << rms
                          << " processorsRun=" << processed << "/" << mProcessors.size()
                          << " time=" << time << std::endl;
            }
        }

        time += granularity;
    }
}

void Pipeline::processAll()
{
    const double fs = (double) mCaptureBuffer->getSampleRate();

    if (mTime == 0) {
        mDataStore->startRealTime();
    }

    static int blockSize = 512;
    rpm::vector<double> data(blockSize);
    mCaptureBuffer->pull(data.data(), (int) data.size());
    mTime = mTime + blockSize / fs;

    mDataStore->setTime(mTime);
    mSampleRate = fs;

    rpm::vector<float> fdata(data.begin(), data.end());
    mBuffer.push(fdata.data(), (int) data.size());

    bool shouldNotBeRunning = false;
    if (mThreadRunning.compare_exchange_strong(shouldNotBeRunning, true)) {
        mProcessingThread = std::thread(std::mem_fn(&Pipeline::callbackProcessing), this);
    }

    // dynamically adjust blockSize to consume all the buffer.
    static int lastBufferLength = 0;
    int bufferLength = mCaptureBuffer->getLength();
    if (blockSize <= 16384 && lastBufferLength - bufferLength >= 8192) {
        blockSize += 128;
        std::cout << "Processing too slowly, "
                  << bufferLength << " samples remaining. "
                  << "Adjusted block size to "
                  << blockSize << " samples" << std::endl;
    }
    else if (blockSize >= 512 && lastBufferLength - bufferLength <= -1024) {
        blockSize -= 128;
        std::cout << "Processing fast enough, "
                  << "adjusted block size to "
                  << blockSize << " samples" << std::endl;
    }
    lastBufferLength = bufferLength;
} 
