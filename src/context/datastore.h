#ifndef MAIN_CONTEXT_DATA_STORE_H
#define MAIN_CONTEXT_DATA_STORE_H

#include "rpcxx.h"
#include "../timetrack.h"
#include "../analysis/analysis.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <QReadWriteLock>

enum class FrequencyScale : unsigned int {
    Linear      = 0,
    Logarithmic = 1,
    Mel         = 2,
    ERB         = 3,
};

namespace Main {

    struct SpectrogramCoefs {
        rpm::vector<double> magnitudes;
        double sampleRate;
    };

    class DataStore {
    public:
        DataStore();

        void beginWrite();
        void endWrite();

        void beginRead();
        void endRead();

        double getTime() const;
        void setTime(double t);

        double getRealTime() const;
        void startRealTime();
        void stopRealTime();

        TimeTrack<SpectrogramCoefs>& getSpectrogram();

        OptionalTimeTrack<double>& getPitchTrack();

        // Vocal weight: formant-corrected harmonic spectral tilt in dB/octave
        // (flat = heavy/buzzy, steep = light). Written by the pitch processor.
        OptionalTimeTrack<double>& getWeightTrack();

        OptionalTimeTrack<double>& getFormantTrack(int i);
        int getFormantTrackCount() const;
        void setFormantTrackCount(int n);

        TimeTrack<rpm::vector<double>>& getSoundTrack();
        TimeTrack<rpm::vector<double>>& getGifTrack();

        // Voice activity from the denoiser (0..1). 1.0 when denoising is off, so
        // the gate is transparent. Atomic: written by the analysis thread, read by
        // the processors without taking the datastore lock.
        double getVoiceActivity() const;
        void setVoiceActivity(double p);

    private:
        int mTrackLength;

        QReadWriteLock mLock;

        volatile double mTime;

        std::atomic<double> mVoiceActivity { 1.0 };

        bool mIsRealTimeStarted;
        double mRealTimeOffset;
        std::chrono::time_point<std::chrono::high_resolution_clock> mRealTimeStart;

        TimeTrack<SpectrogramCoefs> mSpectrogram;
        
        OptionalTimeTrack<double> mPitchTrack;
        OptionalTimeTrack<double> mWeightTrack;
        rpm::vector<OptionalTimeTrack<double>> mFormantTracks;

        TimeTrack<rpm::vector<double>> mSoundTrack;
        TimeTrack<rpm::vector<double>> mGifTrack;
    };

}

#endif // MAIN_CONTEXT_DATA_STORE_H

