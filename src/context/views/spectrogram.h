#ifndef MAIN_CONTEXT_VIEWS_SPECTROGRAM_H
#define MAIN_CONTEXT_VIEWS_SPECTROGRAM_H

#include "views.h"
#include <atomic>

namespace Main::View {

    class Spectrogram : public QObject, public AbstractView {
        Q_OBJECT

    public:
        Spectrogram();
        virtual ~Spectrogram();

    protected:
        void render(QPainterWrapper *painter, Config *config, DataStore *dataStore) override;
        bool onKeyPress(QKeyEvent *event) override;

    private:
        // Session statistics for the voice HUD. Written only from the render thread;
        // the GUI thread just raises the reset flag.
        std::atomic_bool mStatsResetRequested { false };

        rpm::vector<float> mSessionPitches;
        int64_t mVoicedCount = 0;
        int64_t mInBandCount = 0;
        double mLastStatTime = 0.0;      // audio-clock time of the last folded point
        double mSessionStartWall = -1.0; // view clock at session start
        double mCachedMedian = 0.0;
        double mLastMedianCalc = -1.0;

        // HUD values are sampled and held at 4 Hz: recomputing them every rendered
        // frame made the numbers (and the in/out-of-band color) flicker at the
        // display refresh rate.
        double mHudLastUpdate = -1.0;
        double mHudLastValid = -1.0;
        double mHudPitch = 0.0;
        double mHudF1 = 0.0;
        double mHudF2 = 0.0;
        double mHudF3 = 0.0;

        // Gender-read estimate (0 = male-read .. 1 = female-read), classified with
        // hysteresis so the label cannot flap at a class boundary.
        double mHudGenderScore = -1.0;
        int mHudGenderClass = -1;
        int mHudPitchClass = -1;
        int mHudResClass = -1;

        // Rolling history for the right-side graphs; negative values mark gaps.
        struct GenderSample {
            float t;
            float pitch;
            float res;
            float overall;
        };
        rpm::vector<GenderSample> mGenderHistory;

        // Fixed-step display clock. Sampling the wall clock raw at render time puts
        // its per-frame sampling noise (measured ~0.4-0.9 ms) straight into the
        // scroll position; under adaptive sync the display does not quantize that
        // away, and it reads as the content wobbling forward/back every frame.
        double mSmoothTime = -1.0;
        double mLastRawTime = -1.0;
        double mEmaFrameDt = 1.0 / 120.0;
        int mFramesRendered = 0;
    };

}

#endif
