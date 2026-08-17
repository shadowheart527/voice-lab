#include "spectrogram.h"
#include "../genderscore.h"
#include "../timings.h"
#include <iostream>
#include <qnamespace.h>
#include <QKeyEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace Main::View;

// Median of the values of (time, value) points with time in [t0, t1].
// Returns -1.0 when there are none (never NaN: see below).
static double medianInWindow(
        const rpm::vector<std::pair<double, double>>& pts, double t0, double t1)
{
    rpm::vector<double> vals;
    for (const auto& [t, v] : pts) {
        if (t >= t0 && t <= t1) {
            vals.push_back(v);
        }
    }
    if (vals.empty()) {
        // Sentinel instead of NaN: the project compiles with -ffast-math, under
        // which std::isnan may be constant-folded to false and NaN guards silently
        // become dead code (this produced a literal "nan Hz" on screen).
        return -1.0;
    }
    std::nth_element(vals.begin(), std::next(vals.begin(), (int) vals.size() / 2), vals.end());
    return vals[vals.size() / 2];
}

static QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    return QColor(
        (int) std::lround(a.red() + t * (b.red() - a.red())),
        (int) std::lround(a.green() + t * (b.green() - a.green())),
        (int) std::lround(a.blue() + t * (b.blue() - a.blue())));
}

// The model itself lives in genderscore.h, shared with the live WebSocket feed.
static double pitchGenderP(double f)
{
    return Main::GenderScore::pitchP(f);
}

static double formantGenderP(int i, double f)
{
    return Main::GenderScore::formantP(i, f);
}

// The blue-grey-pink gradient used by the meter bar, the track coloring and the
// history graphs.
static QColor genderColor(double p)
{
    static const QColor gBlue(96, 165, 250);
    static const QColor gGrey(158, 155, 166);
    static const QColor gPink(244, 114, 182);
    p = std::clamp(p, 0.0, 1.0);
    return p < 0.5 ? lerpColor(gBlue, gGrey, p * 2.0)
                   : lerpColor(gGrey, gPink, (p - 0.5) * 2.0);
}

// Nearest note name for a frequency, e.g. 196 Hz -> "G3".
static std::string noteName(double f)
{
    static const char *names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int midi = (int) std::lround(69.0 + 12.0 * std::log2(f / 440.0));
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", names[((midi % 12) + 12) % 12], midi / 12 - 1);
    return buf;
}

// Collect the defined points of an optional track in [t0, t1] and median-filter the
// values over `win` neighbouring points. A small median suppresses single-frame
// outliers (formant estimates on real speech flick between candidates) without the
// lag or corner-rounding of a moving average.
static rpm::vector<std::pair<double, double>> definedPointsSmoothed(
        OptionalTimeTrack<double>& track, double t0, double t1, int win)
{
    rpm::vector<std::pair<double, double>> pts;
    for (auto it = track.lower_bound(t0); it != track.upper_bound(t1); ++it) {
        if (it->second.has_value()) {
            pts.emplace_back(it->first, *(it->second));
        }
    }

    if (win < 3 || (int) pts.size() < win) {
        return pts;
    }

    const int h = win / 2;
    rpm::vector<std::pair<double, double>> out(pts.size());
    rpm::vector<double> window;
    for (int i = 0; i < (int) pts.size(); ++i) {
        const int a = std::max(0, i - h);
        const int b = std::min((int) pts.size() - 1, i + h);
        window.clear();
        for (int k = a; k <= b; ++k) {
            window.push_back(pts[k].second);
        }
        std::nth_element(window.begin(),
                std::next(window.begin(), (int) window.size() / 2), window.end());
        out[i] = { pts[i].first, window[window.size() / 2] };
    }
    return out;
}

Spectrogram::Spectrogram()
{
}

Spectrogram::~Spectrogram()
{
}

bool Spectrogram::onKeyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_R) {
        mStatsResetRequested = true;
        return true;
    }
    return false;
}

void Spectrogram::render(QPainterWrapper *painter, Config *config, DataStore *dataStore)
{
    const double rawTime = dataStore->getRealTime();

    // Fixed-step display clock: advance by a smoothed frame interval, slaved to the
    // real clock with a small bounded correction. Content then moves a constant
    // amount per frame regardless of when this render happened to sample the clock,
    // which is what makes the scroll hold steady under adaptive sync.
    ++mFramesRendered;
    if (mSmoothTime < 0.0) {
        mSmoothTime = rawTime;
        mLastRawTime = rawTime;
    }
    const double rawDt = rawTime - mLastRawTime;
    mLastRawTime = rawTime;

    if (rawDt <= 1e-9) {
        // Real-time clock frozen (paused): hold the display clock too.
    }
    else if (rawDt > 0.25 || std::abs(rawTime - mSmoothTime) > 0.35) {
        // Startup, resume after a compositor stall, or runaway divergence: snap.
        mSmoothTime = rawTime;
    }
    else {
        mEmaFrameDt += 0.05 * (rawDt - mEmaFrameDt);
        const double err = rawTime - mSmoothTime;
        const double correction = std::clamp(err * 0.02,
                -0.10 * mEmaFrameDt, 0.10 * mEmaFrameDt);
        mSmoothTime += mEmaFrameDt + correction;
    }

    const double realTimeEnd = mSmoothTime;

    dataStore->beginWrite();
    constexpr double keepDuration = 50.0;
    const double keepTimeStart = realTimeEnd - keepDuration;
    dataStore->getSpectrogram().remove_before(keepTimeStart);
    dataStore->getSoundTrack().remove_before(keepTimeStart);
    dataStore->getGifTrack().remove_before(keepTimeStart);
    dataStore->endWrite();

    dataStore->beginRead();

    auto& spectrogram = dataStore->getSpectrogram();
    auto& pitchTrack = dataStore->getPitchTrack();

    double newestSpec = std::numeric_limits<double>::quiet_NaN();
    {
        auto it = spectrogram.upper_bound(1e18);
        if (it != spectrogram.lower_bound(-1e18)) {
            --it;
            newestSpec = it->first;
        }
    }

    // Opt-in probe (IF_DEBUG_JITTER=1): one CSV line per rendered frame with the
    // wall clock, the view clock and the newest data timestamps. This is what
    // separates "frames render unevenly" from "data lags the view window unevenly".
    static const bool debugJitter = (std::getenv("IF_DEBUG_JITTER") != nullptr);
    if (debugJitter) {
        const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        double newestPitch = std::numeric_limits<double>::quiet_NaN();
        {
            auto it = pitchTrack.upper_bound(1e18);
            if (it != pitchTrack.lower_bound(-1e18)) {
                --it;
                newestPitch = it->first;
            }
        }

        std::cout << "JIT]," << wall << "," << realTimeEnd << ","
                  << newestSpec << "," << newestPitch << std::endl;
    }

    const double viewDuration = config->getViewTimeSpan();

    // Adaptive display delay, replacing the old fixed 50 ms. The newest spectrogram
    // slice trails the wall-clock view edge by a fluctuating gap (audio buffering +
    // analysis batching); with a fixed 50 ms the leading edge starved whenever the
    // gap exceeded it, so the front of the spectrogram wobbled as it scrolled. Track
    // the gap's mean and deviation and sit the edge just behind it, slewing slowly so
    // the scroll speed never visibly changes.
    static double sGapEma = 0.08;
    static double sGapDev = 0.01;
    static double sDelay = 0.08;
    if (!std::isnan(newestSpec)) {
        const double gap = realTimeEnd - newestSpec;
        if (gap > -1.0 && gap < 1.0) {
            sGapEma += 0.02 * (gap - sGapEma);
            sGapDev += 0.02 * (std::abs(gap - sGapEma) - sGapDev);
        }
        const double target = std::clamp(sGapEma + 3.0 * sGapDev + 0.005, 0.05, 0.25);
        // Fast slew only while converging at startup; afterwards the slew is kept
        // far below one pixel per frame, because moving the window is
        // indistinguishable from scroll-speed wobble.
        const double maxSlew = (mFramesRendered < 300) ? 0.0010 : 0.0002;
        sDelay += std::clamp(target - sDelay, -maxSlew, maxSlew);
    }

    const double timeDelay = sDelay;
    const double timeEnd = realTimeEnd - timeDelay;
    const double timeStart = timeEnd - viewDuration;

    // Snapshot everything the draw needs while holding the read lock, then release
    // it BEFORE drawing. This render runs continuously at the display refresh rate;
    // holding the lock across the whole draw (~3-5 ms per frame) made every analysis
    // thread insert wait behind it, which measured as ~5 ms of pure lock-wait per
    // 20 ms analysis tick and pushed the pipeline out of real time.
    const bool showSpectrogram = config->getViewShowSpectrogram();
    const bool showPitch = config->getViewShowPitch();
    const bool showFormants = config->getViewShowFormants();
    const bool showHud = config->getViewShowHud();
    const int formantCount = showFormants ? config->getViewFormantCount() : 0;
    // The HUD reads F1-F3 even when the formant dot layer is hidden (F3 feeds the
    // gender-read estimate as the vocal-tract-length proxy).
    const int formantSnapshotCount = std::max(formantCount, showHud ? 3 : 0);
    const int smoothWin = config->getViewFormantSmoothing() ? 5 : 1;

    rpm::vector<std::pair<double, SpectrogramCoefs>> slices;
    if (showSpectrogram) {
        slices.assign(spectrogram.lower_bound(timeStart), spectrogram.upper_bound(timeEnd));
    }

    rpm::vector<std::pair<double, double>> pitchPoints;
    if (showPitch || showHud) {
        // Median-3: on real voices (vibrato, shimmer, breath noise) the pitch
        // solver emits isolated wild estimates ~1-2% of frames -- measured 12.6 Hz
        // spikes on a signal that never left 195-206 Hz. A 3-point median removes
        // isolated outliers entirely while tracking genuine pitch movement with at
        // most one frame (20 ms) of lag. The HUD and session stats read the same
        // cleaned points, so outliers stop polluting the in-band percentage too.
        const int pitchWin = config->getViewPitchSmoothing() ? 3 : 1;
        pitchPoints = definedPointsSmoothed(pitchTrack, timeStart, timeEnd, pitchWin);
    }

    rpm::vector<rpm::vector<std::pair<double, double>>> formantPoints(formantSnapshotCount);
    for (int i = 0; i < formantSnapshotCount; ++i) {
        formantPoints[i] = definedPointsSmoothed(
                dataStore->getFormantTrack(i), timeStart, timeEnd, smoothWin);
    }

    static const bool debugAudio = (std::getenv("IF_DEBUG_AUDIO") != nullptr);
    if (debugAudio && showSpectrogram) {
        static int dbgCount = 0;
        if ((dbgCount++ % 60) == 0) {
            std::cout << "View::Spectrogram] realTimeEnd=" << realTimeEnd
                      << " window=[" << timeStart << "," << timeEnd << "]"
                      << " slicesInWindow=" << slices.size();
            if (!slices.empty()) {
                std::cout << " magnitudes=" << slices.back().second.magnitudes.size()
                          << " specFs=" << slices.back().second.sampleRate;
            }
            std::cout << std::endl;
        }
    }

    dataStore->endRead();

    // Draw from the snapshots, without the lock.
    const bool light = config->getUiLightMode();
    painter->setLightMode(light);
    painter->setTimeRange(timeStart, timeEnd);

    const int W = painter->viewport().width();
    const int H = painter->viewport().height();

    // Theme palette. The canvas background is a rect rather than the GL clear color
    // so the theme lives entirely in this draw pass.
    const QColor themeBg = light ? QColor(250, 249, 252) : QColor(0, 0, 0);
    const QColor colorNeutral = light ? QColor(78, 72, 92) : QColor(205, 202, 214);
    const QColor colorDim = light ? QColor(122, 116, 136) : QColor(150, 147, 160);
    const QColor colorIn = light ? QColor(30, 148, 58) : QColor(111, 224, 111);
    const QColor colorOut = light ? QColor(186, 106, 16) : QColor(242, 160, 61);

    painter->drawHudRect(0, 0, (float) W, (float) H, themeBg);

    if (showSpectrogram) {
        painter->drawSpectrogram(slices);
    }

    const double bandLo = config->getTargetPitchMin();
    const double bandHi = config->getTargetPitchMax();

    // Deeper hues in light mode: the dark-mode pastels wash out on white.
    const QColor mascBlue = light ? QColor(37, 99, 235) : QColor(96, 165, 250);
    const QColor femPink = light ? QColor(219, 39, 119) : QColor(244, 114, 182);

    if (config->getViewShowTargetBand()) {
        // Blue = typical masculine range (reference), pink = feminine target range
        // (also what the HUD judges pitch against, adjustable from the sidebar).
        painter->drawTargetBand(config->getMascPitchMin(), config->getMascPitchMax(), mascBlue);
        painter->drawTargetBand(bandLo, bandHi, femPink);
    }

    if (config->getViewShowFormantBands()) {
        // Same blue/pink treatment for the typical running-speech zone of each
        // formant. These are conversational-median zones, not per-vowel ranges.
        for (int i = 0; i < 3; ++i) {
            painter->drawTargetBand(config->getFormantBand(i, false, false),
                    config->getFormantBand(i, false, true), mascBlue);
            painter->drawTargetBand(config->getFormantBand(i, true, false),
                    config->getFormantBand(i, true, true), femPink);
        }
    }

    // Track identity: outline colors and edge labels distinguish the tracks while
    // the point colors carry the masc-to-fem gradient. Hues chosen to stay clear of
    // the blue/pink gender axis on both themes.
    const QColor identityColors[5] = {
        light ? QColor(40, 36, 50) : QColor(255, 255, 255),  // pitch
        QColor(255, 171, 64),                                 // F1 amber
        QColor(0, 196, 180),                                  // F2 teal
        QColor(171, 205, 60),                                 // F3 lime
        light ? QColor(214, 212, 220) : QColor(70, 68, 78),   // F4 dim grey
    };
    static const char *trackNames[5] = { "pitch", "F1", "F2", "F3", "F4" };

    const bool genderColors = config->getViewGenderColors();
    const bool asLines = config->getViewTrackLines();

    struct TrackLabel {
        float y;
        int id;
    };
    rpm::vector<TrackLabel> trackLabels;

    auto drawTrack = [&](const rpm::vector<std::pair<double, double>>& pts, int id) {
        if (pts.empty()) {
            return;
        }
        // Split into contiguous runs so lines break across unvoiced gaps.
        size_t runStart = 0;
        for (size_t i = 1; i <= pts.size(); ++i) {
            if (i == pts.size() || pts[i].first - pts[i - 1].first > 0.08) {
                rpm::vector<std::pair<double, double>> run(
                        pts.begin() + runStart, pts.begin() + i);
                rpm::vector<QColor> colors;
                if (genderColors && id >= 4) {
                    // F4 and above carry no gender signal and track noisily;
                    // draw them dim so they stop dominating the canvas.
                    colors.push_back(light ? QColor(196, 194, 204) : QColor(96, 94, 104));
                }
                else if (genderColors) {
                    colors.reserve(run.size());
                    for (const auto& [t, f] : run) {
                        colors.push_back(genderColor(id == 0
                                ? pitchGenderP(f) : formantGenderP(id - 1, f)));
                    }
                }
                else if (id == 0) {
                    colors.push_back(QColor(0, 255, 255));
                }
                else {
                    const auto [r, g, b] = config->getViewFormantColor(id - 1);
                    colors.push_back(QColor::fromRgbF(r, g, b));
                }
                const QColor outline = genderColors
                        ? identityColors[std::min(id, 4)]
                        : (light ? QColor(255, 255, 255) : QColor(0, 0, 0));
                painter->drawGenderTrack(run, colors,
                        asLines ? 3.4f : 3.0f, asLines, outline);
                runStart = i;
            }
        }
        trackLabels.push_back({ (float) painter->mapFrequencyToY(pts.back().second), id });
    };

    if (showPitch) {
        drawTrack(pitchPoints, 0);
    }
    for (int i = 0; i < formantCount; ++i) {
        drawTrack(formantPoints[i], i + 1);
    }

    painter->drawTimeAxis();
    painter->drawFrequencyScale();

    // Identity labels riding each track's newest value, decluttered vertically.
    if (genderColors && !trackLabels.empty()) {
        std::sort(trackLabels.begin(), trackLabels.end(),
                [](const TrackLabel& a, const TrackLabel& b) { return a.y < b.y; });
        float lastY = -100.0f;
        for (const auto& tl : trackLabels) {
            float ly = std::clamp(std::max(tl.y, lastY + 15.0f), 14.0f, (float) H - 8.0f);
            painter->drawHudTextSmall((float) W - 92.0f, ly + 4.0f,
                    identityColors[std::min(tl.id, 4)], trackNames[std::min(tl.id, 4)]);
            lastY = ly;
        }
    }

    if (showHud) {
        // Fold pitch points not seen before into the session statistics. Points
        // arrive at ~50/s and this render runs faster, so this is 0-1 points per
        // frame; timestamps are on the shared analysis clock and only increase.
        if (mStatsResetRequested.exchange(false)) {
            mSessionPitches.clear();
            mVoicedCount = 0;
            mInBandCount = 0;
            mSessionStartWall = realTimeEnd;
            mLastMedianCalc = -1.0;
        }
        if (mSessionStartWall < 0.0) {
            mSessionStartWall = realTimeEnd;
        }

        for (const auto& [t, f] : pitchPoints) {
            if (t > mLastStatTime) {
                mLastStatTime = t;
                ++mVoicedCount;
                if (f >= bandLo && f <= bandHi) {
                    ++mInBandCount;
                }
                mSessionPitches.push_back((float) f);
            }
        }

        if (!mSessionPitches.empty()
                && (mLastMedianCalc < 0.0 || realTimeEnd - mLastMedianCalc > 1.0)) {
            rpm::vector<float> tmp(mSessionPitches);
            std::nth_element(tmp.begin(),
                    std::next(tmp.begin(), (int) tmp.size() / 2), tmp.end());
            mCachedMedian = tmp[tmp.size() / 2];
            mLastMedianCalc = realTimeEnd;
        }

        char line[160];
        float y = 10.0f;
        QRect box;

        // Sample the displayed values at 4 Hz and hold them between updates. Values
        // PERSIST through silence (they only ever move forward to newer valid
        // readings); staleness is shown by dimming, never by blanking.
        if (mHudLastUpdate < 0.0 || realTimeEnd - mHudLastUpdate >= 0.25) {
            const double newPitch = medianInWindow(pitchPoints, timeEnd - 0.35, timeEnd);
            if (newPitch > 0.0) {
                mHudPitch = newPitch;
                mHudLastValid = realTimeEnd;
            }
            const double newF1 = formantPoints.size() > 0
                    ? medianInWindow(formantPoints[0], timeEnd - 1.0, timeEnd) : -1.0;
            if (newF1 > 0.0) mHudF1 = newF1;
            const double newF2 = formantPoints.size() > 1
                    ? medianInWindow(formantPoints[1], timeEnd - 1.0, timeEnd) : -1.0;
            if (newF2 > 0.0) mHudF2 = newF2;
            const double newF3 = formantPoints.size() > 2
                    ? medianInWindow(formantPoints[2], timeEnd - 1.0, timeEnd) : -1.0;
            if (newF3 > 0.0) mHudF3 = newF3;

            // Gender-read estimate over the last two seconds. Deliberately a
            // simplification: an F0 logistic centered on the ~160 Hz perceptual
            // boundary (male-read below ~145, ambiguous to ~165, reliably
            // female-read 165+ per Gelfer & Schofield), combined 55/45 with a
            // resonance score weighting F2 highest (most gender-salient formant),
            // then F3 (vocal-tract-length proxy), then F1 (most vowel-dependent).
            // The 55/45 split follows Hillenbrand & Clark (2009): F0 is the
            // strongest single cue but flipping perceived gender reliably needs
            // the spectral envelope too.
            const double gPitch = medianInWindow(pitchPoints, timeEnd - 2.0, timeEnd);
            const double gF1 = formantPoints.size() > 0
                    ? medianInWindow(formantPoints[0], timeEnd - 2.0, timeEnd) : -1.0;
            const double gF2 = formantPoints.size() > 1
                    ? medianInWindow(formantPoints[1], timeEnd - 2.0, timeEnd) : -1.0;
            const double gF3 = formantPoints.size() > 2
                    ? medianInWindow(formantPoints[2], timeEnd - 2.0, timeEnd) : -1.0;

            double histPitch = -1.0, histRes = -1.0, histScore = -1.0;

            if (gPitch > 0.0 && gF1 > 0.0 && gF2 > 0.0 && gF3 > 0.0) {
                const double pF0 = GenderScore::pitchP(gPitch);
                const double pRes = GenderScore::resonanceP(gF1, gF2, gF3);

                mHudGenderScore = GenderScore::overallP(pF0, pRes);
                mHudPitchClass = pF0 < 0.40 ? 0 : (pF0 > 0.60 ? 2 : 1);
                mHudResClass = pRes < 0.40 ? 0 : (pRes > 0.60 ? 2 : 1);
                histPitch = pF0;
                histRes = pRes;
                histScore = mHudGenderScore;

                // Class with hysteresis: entering a class requires crossing the
                // boundary by 0.04, so the label cannot flap on the line.
                const double s = mHudGenderScore;
                int cls = mHudGenderClass;
                if (cls < 0) {
                    cls = s < 0.38 ? 0 : (s > 0.62 ? 2 : 1);
                }
                else if (cls == 0) {
                    if (s > 0.66) cls = 2;
                    else if (s > 0.42) cls = 1;
                }
                else if (cls == 2) {
                    if (s < 0.34) cls = 0;
                    else if (s < 0.58) cls = 1;
                }
                else {
                    if (s < 0.34) cls = 0;
                    else if (s > 0.66) cls = 2;
                }
                mHudGenderClass = cls;

                static const bool debugAudio = (std::getenv("IF_DEBUG_AUDIO") != nullptr);
                if (debugAudio) {
                    std::cout << "GEND]," << mHudGenderScore << "," << cls
                              << "," << pF0 << "," << pRes << std::endl;
                }
            }
            // (When invalid, the previous score and classes persist for display;
            // only the history gets a gap marker.)

            // Rolling gender history for the right-side graphs (invalid samples
            // are kept as gap markers so silence shows as breaks, not lines).
            mGenderHistory.push_back({ (float) realTimeEnd,
                    (float) histPitch, (float) histRes, (float) histScore });
            while (!mGenderHistory.empty()
                    && realTimeEnd - mGenderHistory.front().t > 610.0) {
                mGenderHistory.erase(mGenderHistory.begin());
            }

            mHudLastUpdate = realTimeEnd;
        }

        // Line 1: current pitch. Holds the last reading through silence, dimmed
        // once it is stale.
        const bool hudStale = mHudLastValid < 0.0 || realTimeEnd - mHudLastValid > 2.5;
        const double nowPitch = mHudPitch;
        if (nowPitch > 0.0) {
            const bool in = (nowPitch >= bandLo && nowPitch <= bandHi);
            const char *hint = in ? "" : (nowPitch < bandLo ? "  (low)" : "  (high)");
            std::snprintf(line, sizeof(line), "%.0f Hz  %s%s",
                    nowPitch, noteName(nowPitch).c_str(), hint);
            box = painter->hudTextBoundsNormal(line);
            y += box.height();
            painter->drawHudTextNormal(10, y,
                    hudStale ? colorDim : (in ? colorIn : colorOut), line);
        }
        else {
            std::snprintf(line, sizeof(line), "--- Hz");
            box = painter->hudTextBoundsNormal(line);
            y += box.height();
            painter->drawHudTextNormal(10, y, colorDim, line);
        }

        // Line 2: resonance trend, medians over the last second. Always drawn (with
        // placeholders during silence). Deliberately not colored against a target:
        // F1/F2 legitimately roam the vowel space in running speech, so a fixed
        // in/out judgement would be misleading.
        {
            char f1s[16], f2s[16];
            if (mHudF1 > 0.0) std::snprintf(f1s, sizeof(f1s), "%4.0f Hz", mHudF1);
            else std::snprintf(f1s, sizeof(f1s), " ---");
            if (mHudF2 > 0.0) std::snprintf(f2s, sizeof(f2s), "%4.0f Hz", mHudF2);
            else std::snprintf(f2s, sizeof(f2s), " ---");
            std::snprintf(line, sizeof(line), "F1 %s   F2 %s", f1s, f2s);
            box = painter->hudTextBoundsSmall(line);
            y += box.height() + 6;
            painter->drawHudTextSmall(10, y, colorNeutral, line);
        }

        // Line 3: session statistics, always drawn.
        if (mVoicedCount > 0) {
            const double pct = 100.0 * (double) mInBandCount / (double) mVoicedCount;
            const int secs = (int) std::max(0.0, realTimeEnd - mSessionStartWall);
            std::snprintf(line, sizeof(line),
                    "session: median %.0f Hz - %.0f%% in band - %d:%02d - R resets",
                    mCachedMedian, pct, secs / 60, secs % 60);
        }
        else {
            std::snprintf(line, sizeof(line), "session: no voice yet - R resets");
        }
        box = painter->hudTextBoundsSmall(line);
        y += box.height() + 6;
        painter->drawHudTextSmall(10, y, colorDim, line);

        // Line 4: gender-read meter. A blue-to-pink bar with a marker at the
        // combined score, and the class label beside it.
        {
            const QColor meterBlue(96, 165, 250);
            const QColor meterPink(244, 114, 182);
            const QColor meterGrey(158, 155, 166);

            const float barX = 10.0f;
            const float barW = 210.0f;
            const float barH = 9.0f;
            y += 14.0f;

            const int segs = 24;
            for (int si = 0; si < segs; ++si) {
                const double t = (si + 0.5) / segs;
                QColor c = t < 0.5 ? lerpColor(meterBlue, meterGrey, t * 2.0)
                                   : lerpColor(meterGrey, meterPink, (t - 0.5) * 2.0);
                c.setAlphaF(0.80);
                painter->drawHudRect(barX + barW * si / segs, y,
                        barW / segs + 0.5f, barH, c);
            }

            if (mHudGenderScore >= 0.0 && mHudGenderClass >= 0) {
                painter->drawHudDot(
                        barX + (float) (mHudGenderScore * barW),
                        y + barH / 2.0f, 4.5f, Qt::white);

                static const char *clsName[3] = { "male", "androgynous", "female" };
                const QColor clsColor = hudStale ? colorDim
                        : (mHudGenderClass == 0 ? meterBlue
                        : (mHudGenderClass == 2 ? meterPink : colorNeutral));
                std::snprintf(line, sizeof(line), "reads: %s", clsName[mHudGenderClass]);
                painter->drawHudTextSmall(barX + barW + 12, y + barH + 1, clsColor, line);

                if (mHudPitchClass >= 0 && mHudResClass >= 0) {
                    static const char *axisName[3] = { "masc", "andro", "fem" };
                    std::snprintf(line, sizeof(line), "pitch %s - resonance %s",
                            axisName[mHudPitchClass], axisName[mHudResClass]);
                    box = painter->hudTextBoundsSmall(line);
                    y += barH + box.height() + 6;
                    painter->drawHudTextSmall(10, y, colorDim, line);
                }
            }
            else {
                painter->drawHudTextSmall(barX + barW + 12, y + barH + 1,
                        colorDim, "reads: ---");
            }
        }

        // Right side: rolling history graphs of how masc/andro/fem the pitch, the
        // resonance and the overall read have been over the last two minutes.
        if (config->getViewShowHistory()) {
            const float panelW = 160.0f;
            const float panelX = (float) W - panelW - 64.0f;
            const float stripH = 40.0f;
            const double kSpan = std::clamp(config->getViewHistorySpan(), 10.0, 600.0);
            const bool areaStyle = config->getViewHistoryArea();
            float py = 12.0f;

            painter->drawHudRect(panelX - 10.0f, py - 6.0f, panelW + 20.0f,
                    3.0f * (stripH + 26.0f) + 8.0f,
                    light ? QColor(255, 255, 255, 165) : QColor(0, 0, 0, 130));

            static const char *stripName[3] = { "pitch", "resonance", "overall" };
            for (int s = 0; s < 3; ++s) {
                painter->drawHudTextSmall(panelX, py + 10.0f, colorDim, stripName[s]);
                const float top = py + 16.0f;

                // Frame: fem edge on top (pink), masc edge on the bottom (blue),
                // faint centerline at androgynous.
                painter->drawHudRect(panelX, top, panelW, 1.2f, QColor(244, 114, 182, 210));
                painter->drawHudRect(panelX, top + stripH, panelW, 1.2f, QColor(96, 165, 250, 210));
                painter->drawHudRect(panelX, top + stripH / 2.0f, panelW, 1.0f,
                        QColor(158, 155, 166, 110));

                // Fill colors for the area style: pink below the curve, blue above,
                // like a filled mountain silhouette of the score.
                const QColor areaBelow = light ? QColor(244, 158, 196, 200)
                                               : QColor(214, 105, 160, 175);
                const QColor areaAbove = light ? QColor(164, 212, 244, 200)
                                               : QColor(92, 146, 210, 130);

                rpm::vector<QPointF> seg;
                rpm::vector<QColor> segColors;
                float lastT = -1e9f;
                auto flush = [&]() {
                    if (!seg.empty()) {
                        if (areaStyle && seg.size() >= 2) {
                            painter->drawHudArea(seg, top, top + stripH,
                                    areaAbove, areaBelow);
                            painter->drawHudTrack(seg, segColors, 1.6f, true,
                                    QColor(0, 0, 0, 0));
                        }
                        else {
                            painter->drawHudTrack(seg, segColors, 2.2f, seg.size() >= 2,
                                    QColor(0, 0, 0, 0));
                        }
                    }
                    seg.clear();
                    segColors.clear();
                };

                for (const auto& gs : mGenderHistory) {
                    const float v = s == 0 ? gs.pitch : (s == 1 ? gs.res : gs.overall);
                    const double age = realTimeEnd - gs.t;
                    if (age > kSpan) {
                        continue;
                    }
                    if (v < 0.0f) {
                        flush();
                        lastT = gs.t;
                        continue;
                    }
                    if (gs.t - lastT > 2.0f) {
                        flush();
                    }
                    lastT = gs.t;
                    seg.emplace_back(
                            panelX + panelW * (float) (1.0 - age / kSpan),
                            top + stripH * (1.0f - std::clamp(v, 0.0f, 1.0f)));
                    segColors.push_back(genderColor(v));
                }
                flush();

                py = top + stripH + 10.0f;
            }
        }
    }
}
