#include "base.h"
#include "../../../../context/contextmanager.h"
#include <exception>

using namespace Module::App::Processors;

BaseProcessor::BaseProcessor(double frameSpace, double frameLength)
    : mFrameSpace(frameSpace / 1000.0),
      mFrameLength(frameLength / 1000.0),
      mTime(0)
{
}

bool BaseProcessor::canProcess(double timeNow) const
{
    // Slack of half a millisecond: timeNow accumulates in floating-point granularity
    // steps, so an exact >= comparison intermittently skips a tick when frameSpace is
    // an exact multiple of the granularity (e.g. both 20 ms), halving the update rate
    // for that frame.
    return timeNow - mTime >= mFrameSpace - 0.0005;
}

void BaseProcessor::process(const rpm::vector<double>& slidingWindow, double sampleRate, double timeNow)
{
    const int frameSamples = (int) std::round(mFrameLength * sampleRate);

    mData.resize(frameSamples);

    std::copy(std::prev(slidingWindow.end(), frameSamples), slidingWindow.end(),
            mData.begin());

    // Set mTime before running the analysis: getCenteredTime() is called from inside
    // processData() to timestamp the results, and with the old order (mTime updated
    // after) every inserted point carried the PREVIOUS frame's time -- a full
    // frameSpace of systematic lag, and misalignment between the tracks and the
    // spectrogram.
    mTime = timeNow;

#ifdef _WIN32
    try {
        processData(mData, sampleRate);
    }
    catch (const std::exception& e) {
        StdExceptionHandler(e);
    }
#else
    processData(mData, sampleRate);
#endif
}

double BaseProcessor::getFrameSpace() const
{
    return mFrameSpace;
}

double BaseProcessor::getFrameLength() const
{
    return mFrameLength;
}

double BaseProcessor::getCenteredTime() const
{
    // mTime is the END of the analysed window (the current block time), so the
    // window's center sits half a frame length earlier.
    return mTime - mFrameLength / 2;
}

double BaseProcessor::getEndTime() const
{
    return mTime;
}