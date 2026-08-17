// WebAssembly entry point: exposes the SAME analysis code the desktop app runs.
//
// The point of this file is that there is no second implementation. The pitch
// solvers, the linear-prediction and formant solvers, the spectral-tilt
// measure and the gender scoring are all compiled from engine/src, so the
// browser and the desktop cannot drift apart and cannot need separate
// calibration.
//
// Everything Qt- or app-shaped stays out: no Config, no DataStore, no
// pipeline. The browser owns buffering and scheduling and calls in with plain
// frames, which is what its Worker already does.

#include <emscripten/emscripten.h>

#include "../analysis/analysis.h"
#include "../analysis/weight/tilt.h"
#include "../context/genderscore.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace Analysis;

namespace {

    struct Engine {
        double sampleRate = 48000.0;
        std::shared_ptr<PitchSolver> pitch;
        std::shared_ptr<LinpredSolver> linpred;
        std::shared_ptr<FormantSolver> formant;

        // Scratch, reused so a 50 Hz analysis rate does not churn the heap.
        rpm::vector<double> frame;
        rpm::vector<double> preemph;
        rpm::vector<double> window;
        rpm::vector<double> resampled;
        double lastSample = 0.0;
    };

    Engine g;

    // Result slots read back by the JS side without a struct marshalling layer.
    double gPitch = -1.0;
    double gF1 = -1.0, gF2 = -1.0, gF3 = -1.0, gF4 = -1.0;
    double gTilt = -999.0;
    int gVoiced = 0;

}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void vl_init(double sampleRate, int pitchAlg, int linpredAlg, int formantAlg)
{
    g.sampleRate = sampleRate;
    g.pitch = std::shared_ptr<PitchSolver>(
            Main::makePitchSolver(static_cast<Main::PitchAlgorithm>(pitchAlg)));
    g.linpred = std::shared_ptr<LinpredSolver>(
            Main::makeLinpredSolver(static_cast<Main::LinpredAlgorithm>(linpredAlg)));
    g.formant = std::shared_ptr<FormantSolver>(
            Main::makeFormantSolver(static_cast<Main::FormantAlgorithm>(formantAlg)));
    g.lastSample = 0.0;
}

/// Analyse one frame of mono audio. Results are read with the vl_get_* calls.
EMSCRIPTEN_KEEPALIVE
void vl_analyze(const float *samples, int count)
{
    gPitch = -1.0;
    gF1 = gF2 = gF3 = gF4 = -1.0;
    gTilt = -999.0;
    gVoiced = 0;

    if (!g.pitch || count < 64) {
        return;
    }

    g.frame.resize(count);
    for (int i = 0; i < count; ++i) {
        g.frame[i] = samples[i];
    }

    // ---- pitch, exactly as the desktop pipeline calls it
    auto pitchResult = g.pitch->solve(g.frame.data(), count, (int) g.sampleRate);
    gVoiced = pitchResult.voiced ? 1 : 0;
    if (pitchResult.voiced) {
        gPitch = pitchResult.pitch;
    }

    // ---- vocal weight, from the shared measure
    if (pitchResult.voiced && pitchResult.pitch > 40.0) {
        auto t = spectralTilt(g.frame, g.sampleRate, pitchResult.pitch);
        if (t.has_value()) {
            gTilt = *t;
        }
    }

    // ---- formants: same pre-emphasis, window and 11 kHz LPC rate as
    // Processors::Formants, reproduced here because that class is bound to
    // Config and DataStore rather than to the maths.
    constexpr double preemphFrequency = 200.0;
    constexpr double fsLPC = 11000.0;
    const double preemphFactor = std::exp(-(2.0 * M_PI * preemphFrequency) / g.sampleRate);

    if ((int) g.window.size() != count) {
        g.window = gaussianWindow(count, 2.5);
    }
    g.preemph = g.frame;
    for (int i = count - 1; i >= 1; --i) {
        g.preemph[i] = g.window[i] * (g.preemph[i] - preemphFactor * g.preemph[i - 1]);
    }
    g.preemph[0] = g.window[0] * (g.frame[0] - preemphFactor * g.lastSample);
    g.lastSample = g.frame[0];

    // Plain linear-interpolation decimation to the LPC rate. libsamplerate is
    // avoided here purely to keep the WebAssembly module small; the ratio is
    // fixed and the signal is already band-limited by the pre-emphasis.
    const double ratio = fsLPC / g.sampleRate;
    const int outLen = (int) (count * ratio);
    g.resampled.resize(outLen);
    for (int i = 0; i < outLen; ++i) {
        const double pos = i / ratio;
        const int i0 = (int) pos;
        const int i1 = std::min(i0 + 1, count - 1);
        const double frac = pos - i0;
        g.resampled[i] = g.preemph[i0] * (1.0 - frac) + g.preemph[i1] * frac;
    }

    if (outLen > 32 && g.linpred && g.formant) {
        int lpcOrder = (int) std::round(fsLPC / 1000.0) + 1;
        double gain;
        auto lpc = g.linpred->solve(g.resampled.data(), outLen, lpcOrder, &gain);
        if (!lpc.empty()) {
            auto fr = g.formant->solve(lpc.data(), (int) lpc.size(), fsLPC);
            if (fr.formants.size() > 0) gF1 = fr.formants[0].frequency;
            if (fr.formants.size() > 1) gF2 = fr.formants[1].frequency;
            if (fr.formants.size() > 2) gF3 = fr.formants[2].frequency;
            if (fr.formants.size() > 3) gF4 = fr.formants[3].frequency;
        }
    }
}

EMSCRIPTEN_KEEPALIVE int    vl_voiced()  { return gVoiced; }
EMSCRIPTEN_KEEPALIVE double vl_pitch()   { return gPitch; }
EMSCRIPTEN_KEEPALIVE double vl_f1()      { return gF1; }
EMSCRIPTEN_KEEPALIVE double vl_f2()      { return gF2; }
EMSCRIPTEN_KEEPALIVE double vl_f3()      { return gF3; }
EMSCRIPTEN_KEEPALIVE double vl_f4()      { return gF4; }
EMSCRIPTEN_KEEPALIVE double vl_tilt()    { return gTilt; }

// ---- the shared gender model, so the browser cannot use a different one

EMSCRIPTEN_KEEPALIVE
double vl_pitch_score(double f) { return Main::GenderScore::pitchP(f); }

EMSCRIPTEN_KEEPALIVE
double vl_resonance_score(double f1, double f2, double f3)
{ return Main::GenderScore::resonanceP(f1, f2, f3); }

EMSCRIPTEN_KEEPALIVE
double vl_resonance_r(double f1, double f2, double f3)
{ return Main::GenderScore::resonanceR(f1, f2, f3); }

EMSCRIPTEN_KEEPALIVE
double vl_overall_score(double pF0, double pRes)
{ return Main::GenderScore::overallP(pF0, pRes); }

EMSCRIPTEN_KEEPALIVE
double vl_site_resonance(double f1, double f2, int deepFormants)
{ return Main::GenderScore::siteResonance(f1, f2, deepFormants != 0); }

EMSCRIPTEN_KEEPALIVE
double vl_weight(double tiltDbOct) { return Main::GenderScore::weightP(tiltDbOct); }

}
