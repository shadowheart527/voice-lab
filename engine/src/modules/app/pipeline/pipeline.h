#ifndef APP_PIPELINE_H
#define APP_PIPELINE_H

#include "rpcxx.h"
#include "../../../analysis/analysis.h"
#include "../../audio/audio.h"
#include "../../../context/datastore.h"
#include "../../../context/config.h"
#include "processors/base.h"

#include <atomic>
#include <thread>
#include <chrono>

struct DenoiseState;

namespace Module::App
{
    using millis = std::chrono::milliseconds;
    using namespace std::chrono_literals;

    class Pipeline {
    public:
        Pipeline(Module::Audio::Buffer *captureBuffer,
                Main::DataStore *dataStore, Main::Config *config,
                std::shared_ptr<Analysis::PitchSolver>& pitchSolver,
                std::shared_ptr<Analysis::LinpredSolver>& linpredSolver,
                std::shared_ptr<Analysis::FormantSolver>& formantSolver,
                std::shared_ptr<Analysis::InvglotSolver>& invglotSolver);
        ~Pipeline();

        void processAll();

    private:
        Module::Audio::Buffer *mCaptureBuffer;
        Main::DataStore *mDataStore;
        Main::Config *mConfig;

        std::atomic<double> mTime;
        std::atomic_bool mThreadRunning;
        std::atomic_bool mStopThread;

        std::thread mProcessingThread;

        Module::Audio::Buffer mBuffer;
        double mSampleRate;

        rpm::vector<std::unique_ptr<Processors::BaseProcessor>> mProcessors;

        // RNNoise denoiser state (analysis thread only). The FIFOs adapt the
        // analysis block size to RNNoise's fixed 480-sample (10 ms at 48 kHz) frame.
        ::DenoiseState *mDenoiseState = nullptr;
        rpm::vector<float> mDenoiseInFifo;
        double mVadSmooth = 0.0;
        bool mDenoiseBypassLogged = false;

        void computeVoiceActivity(const rpm::vector<double>& block);

        void callbackProcessing();
    };
}

#endif // APP_PIPELINE_H

