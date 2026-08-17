#include "qpainterwrapper.h"
#include <Eigen/src/Core/Array.h>
#include <QPainterPath>
#include <iostream>

QPainterWrapper::QPainterWrapper(Gui::CanvasRenderer *p)
    : p(p),
      mTimeStart(0.0),
      mTimeEnd(5.0),
      mFrequencyScale(FrequencyScale::Mel),
      mMinFrequency(60),
      mMaxFrequency(8000),
      mMaxGain(0)
{
}

QRect QPainterWrapper::viewport() const
{
    return p->viewport();
}

void QPainterWrapper::setZoom(double scale)
{
    p->setZoomScale(scale);
}

void QPainterWrapper::setTimeRange(double start, double end)
{
    mTimeStart = start;
    mTimeEnd = end;
}

void QPainterWrapper::setFrequencyScale(FrequencyScale scale)
{
    mFrequencyScale = scale;
}

void QPainterWrapper::setMinFrequency(double minFrequency)
{
    mMinFrequency = minFrequency;
}

void QPainterWrapper::setMaxFrequency(double maxFrequency)
{
    mMaxFrequency = maxFrequency;
}

void QPainterWrapper::setMaxGain(double maxGain)
{
    mMaxGain = maxGain;
}

double QPainterWrapper::transformFrequency(double frequency)
{
    return transformFrequency(frequency, mFrequencyScale);
}

double QPainterWrapper::inverseFrequency(double value)
{
    return inverseFrequency(value, mFrequencyScale);
}

double QPainterWrapper::mapTimeToX(double time)
{
    return mapTimeToX(time, p->viewport().width(), mTimeStart, mTimeEnd);
}

double QPainterWrapper::mapFrequencyToY(double frequency)
{
    return mapFrequencyToY(frequency, p->viewport().height(), mFrequencyScale, mMinFrequency, mMaxFrequency);
}

static std::string numberToString(double val)
{
    std::stringstream ss;
    ss << val;
    return ss.str();
}

void QPainterWrapper::drawTimeAxis()
{
    const QColor axisFg = mLightMode ? QColor(45, 40, 58) : QColor(255, 255, 255);
    const QColor axisBg = mLightMode ? QColor(250, 249, 252) : QColor(0, 0, 0);
    rpm::vector<double> majorTicks;
    rpm::vector<double> minorTicks;
    rpm::vector<double> minorMinorTicks;

    int timeStart = std::floor(mTimeStart);
    int timeEnd = std::ceil(mTimeEnd);

    for (int timeInt = timeStart; timeInt <= timeEnd; ++timeInt) {
        majorTicks.push_back(timeInt);
        
        // No need to be so detailed for negative time stamps.
        if (timeInt < 0)
            continue;

        for (int division = 1; division <= 9; ++division) {
            const double time = timeInt + division / 10.0;
            if (division == 5)
                minorTicks.push_back(time);
            else
                minorMinorTicks.push_back(time);
        }
    }

    int y1 = viewport().height();
    std::vector<bool> bits(viewport().width(), false);
    
    for (const double val : majorTicks) {
        const double x = mapTimeToX(val);
        const auto valstr = numberToString(val);
        QRect rect = p->textBoundsSmall(valstr);
        rect.translate(x - rect.width() / 2, y1 - 10);
        bool covered = false;
        for (int tx = rect.x(); tx <= rect.x() + rect.width(); ++tx) {
            if (tx >= 0 && tx < bits.size()
                    && bits[tx]) {
                covered = true;
                break;
            }
        }
        p->drawLine(x, y1, x, y1 - 8, axisFg, 3);
        if (!covered && val >= 0) {
            p->drawTextSmallOutlined(x - rect.width() / 2, y1 - 10, axisFg, valstr, axisBg);
            for (int tx = rect.x(); tx <= rect.x() + rect.width(); ++tx) {
                if (tx >= 0 && tx < bits.size())
                    bits[tx] = true;
            }
        }
    }

    for (const double val : minorTicks) {
        const double x = mapTimeToX(val);
        const auto valstr = numberToString(val);
        QRect rect = p->textBoundsSmaller(valstr);
        rect.translate(x - rect.width() / 2, y1 - 10);
        bool covered = false;
        for (int tx = rect.x(); tx <= rect.x() + rect.width(); ++tx) {
            if (tx >= 0 && tx < bits.size()
                    && bits[tx]) {
                covered = true;
                break;
            }
        }
        p->drawLine(x, y1, x, y1 - 4, axisFg, 2);
        if (!covered) {
            p->drawTextSmallerOutlined(x - rect.width() / 2, y1 - 10, axisFg, valstr, axisBg);
            for (int tx = rect.x(); tx <= rect.x() + rect.width(); ++tx) {
                if (tx >= 0 && tx < bits.size())
                    bits[tx] = true;
            }
        }
    }
    
    for (const double val : minorMinorTicks) {
        const double x = mapTimeToX(val);
        p->drawLine(x, y1, x, y1 - 2, axisFg, 1.5);
    }
}

void QPainterWrapper::drawFrequencyScale()
{
    const QColor axisFg = mLightMode ? QColor(45, 40, 58) : QColor(255, 255, 255);
    const QColor axisBg = mLightMode ? QColor(250, 249, 252) : QColor(0, 0, 0);
    rpm::vector<double> majorTicks;
    rpm::vector<double> minorTicks;
    rpm::vector<double> minorMinorTicks;

    if (mFrequencyScale == FrequencyScale::Linear) {
        int loFreq = std::floor(mMinFrequency / 1000) * 1000;
        int hiFreq = std::ceil(mMaxFrequency / 1000) * 1000;

        for (int freqInt = loFreq; freqInt <= hiFreq; freqInt += 1000) {
            majorTicks.push_back(freqInt);

            for (int division = 1; division <= 9; ++division) {
                const double freq = freqInt + division * 100.0;
                if (division == 5)
                    minorTicks.push_back(freq);
                else
                    minorMinorTicks.push_back(freq);
            }
        }
    }
    else {
        double loLog = log10(mMinFrequency);
        double hiLog = log10(mMaxFrequency);
        int loDecade = (int) floor(loLog);

        double val;
        double startDecade = pow(10.0, (double) loDecade);

        // Major ticks are the decades.
        double decade = startDecade;
        double delta = hiLog - loLog, steps = fabs(delta);
        double step = delta >= 0 ? 10 : 0.1;
        double rMin = std::min(mMinFrequency, mMaxFrequency);
        double rMax = std::max(mMinFrequency, mMaxFrequency);
        for (int i = 0; i <= steps; ++i) { 
            val = decade;
            if (val >= rMin && val < rMax) {
                majorTicks.push_back(val);
            }
            decade *= step;
        }

        // Minor ticks are multiple of decades.
        decade = startDecade;
        float start, end, mstep;
        if (delta > 0) {
            start = 2; end = 9; mstep = 1;
        }
        else {
            start = 9; end = 2; mstep = -1;
        }
        ++steps;
        for (int i = 0; i <= steps; ++i) {
            for (int j = start; mstep > 0 ? j <= end : j >= end; j += mstep) {
                val = decade * j;
                if (val >= rMin && val < rMax) {
                    minorTicks.push_back(val);
                }
            }
            decade *= step;
        }

        // MinorMinor ticks are multiple of decades.
        decade = startDecade;
        if (delta > 0) {
            start = 10; end = 100; mstep = 1;
        }
        else {
            start = 100; end = 10; mstep = -1;
        }
        ++steps;
        for (int i = 0; i <= steps; ++i) {
            if (decade >= 10.0) {
                for (int f = start; mstep > 0 ? f <= end : f >= end; f += mstep) {
                    if ((int) (f / 10) != f / 10.0) {
                        val = decade * f / 10;
                        if (val >= rMin && val < rMax) {
                            minorMinorTicks.push_back(val);
                        }
                    }
                }
            }
            decade *= step;
        }
    }

    int x1 = viewport().width();
    std::vector<bool> bits(viewport().height(), false);
    
    for (const double val : majorTicks) {
        const double y = mapFrequencyToY(val);
        const auto valstr = numberToString(val);
        QRect rect = p->textBoundsNormal(valstr);
        rect.translate(x1 - 12 - rect.width(), y + rect.height() / 2);
        bool covered = false;
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size()
                    && bits[ty]) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }
        p->drawLine(x1 - 8, y, x1, y, axisFg, 3);
        p->drawTextNormalOutlined(rect.x(), rect.y(), axisFg, valstr, axisBg);
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size())
                bits[ty] = true;
        }
    }

    for (const double val : minorTicks) {
        const double y = mapFrequencyToY(val);
        const auto valstr = numberToString(val);
        QRect rect = p->textBoundsSmall(valstr);
        rect.translate(x1 - 12 - rect.width(), y + rect.height() / 2);
        bool covered = false;
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size()
                    && bits[ty]) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }
        p->drawLine(x1 - 6, y, x1, y, axisFg, 2);
        p->drawTextSmallOutlined(rect.x(), rect.y(), axisFg, valstr, axisBg);
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size())
                bits[ty] = true;
        }
    }

    for (const double val : minorMinorTicks) {
        const double y = mapFrequencyToY(val);
        const auto valstr = numberToString(val);
        QRect rect = p->textBoundsSmaller(valstr);
        rect.translate(x1 - 12 - rect.width(), y + rect.height() / 2);
        bool covered = false;
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size()
                    && bits[ty]) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }
        p->drawLine(x1 - 4, y, x1, y, axisFg, 2);
        p->drawTextSmallerOutlined(rect.x(), rect.y(), axisFg, valstr, axisBg);
        for (int ty = rect.y(); ty <= rect.y() + rect.height(); ++ty) {
            if (ty >= 0 && ty < bits.size())
                bits[ty] = true;
        }
    }
}

void QPainterWrapper::drawFrequencyTrack(
            const TimeTrack<double>::const_iterator& begin,
            const TimeTrack<double>::const_iterator& end,
            float radius,
            const QColor &color)
{
    rpm::vector<QPointF> points;

    for (auto it = begin; it != end; ++it) {
        double time = it->first;
        double pitch = it->second;

        double x = mapTimeToX(time);
        double y = mapFrequencyToY(pitch);

        points.emplace_back(x, y);
    }

    p->drawScatterWithOutline(points, radius, color);
}

void QPainterWrapper::drawHudTextNormal(float x, float y, const QColor &color, const std::string &text)
{
    p->drawTextNormalOutlined(x, y, color, text);
}

void QPainterWrapper::drawHudTextSmall(float x, float y, const QColor &color, const std::string &text)
{
    p->drawTextSmallOutlined(x, y, color, text);
}

QRect QPainterWrapper::hudTextBoundsNormal(const std::string &text)
{
    return p->textBoundsNormal(text);
}

QRect QPainterWrapper::hudTextBoundsSmall(const std::string &text)
{
    return p->textBoundsSmall(text);
}

const QVector<QRgb>& QPainterWrapper::lightCmap()
{
    static QVector<QRgb> map;
    if (map.isEmpty()) {
        struct Anchor { double t; int r, g, b; };
        static const Anchor anchors[] = {
            { 0.00, 252, 251, 254 },
            { 0.25, 233, 222, 248 },
            { 0.50, 178, 122, 231 },
            { 0.75, 103, 38, 163 },
            { 1.00, 28, 8, 48 },
        };
        map.resize(256);
        for (int i = 0; i < 256; ++i) {
            const double t = i / 255.0;
            int a = 0;
            while (a < 3 && anchors[a + 1].t < t) ++a;
            const double f = (t - anchors[a].t) / (anchors[a + 1].t - anchors[a].t);
            const int r = (int) std::lround(anchors[a].r + f * (anchors[a + 1].r - anchors[a].r));
            const int g = (int) std::lround(anchors[a].g + f * (anchors[a + 1].g - anchors[a].g));
            const int b = (int) std::lround(anchors[a].b + f * (anchors[a + 1].b - anchors[a].b));
            map[i] = qRgb(r, g, b);
        }
    }
    return map;
}

void QPainterWrapper::setLightMode(bool light)
{
    mLightMode = light;
}

void QPainterWrapper::drawHudTrack(const rpm::vector<QPointF>& points,
        const rpm::vector<QColor>& colors, float size, bool asLine,
        const QColor &outline)
{
    p->drawPolyTrack(points, colors, size, asLine, outline);
}

void QPainterWrapper::drawHudArea(const rpm::vector<QPointF>& points, float yTop,
        float yBottom, const QColor &above, const QColor &below)
{
    p->drawAreaStrip(points, yTop, yBottom, above, below);
}

void QPainterWrapper::drawGenderTrack(
        const rpm::vector<std::pair<double, double>>& trackPoints,
        const rpm::vector<QColor>& colors,
        float size, bool asLine, const QColor &identity)
{
    rpm::vector<QPointF> screen;
    screen.reserve(trackPoints.size());
    for (const auto& [time, frequency] : trackPoints) {
        screen.emplace_back(mapTimeToX(time), mapFrequencyToY(frequency));
    }
    p->drawPolyTrack(screen, colors, size, asLine, identity);
}

void QPainterWrapper::drawHudRect(float x, float y, float w, float h, const QColor &color)
{
    p->drawFilledRect(x, y, w, h, color);
}

void QPainterWrapper::drawHudDot(float x, float y, float radius, const QColor &fill)
{
    rpm::vector<QPointF> pt { QPointF(x, y) };
    p->drawScatterWithOutline(pt, radius, fill);
}

void QPainterWrapper::drawTargetBand(double minFrequency, double maxFrequency, const QColor &color)
{
    const double yLo = mapFrequencyToY(minFrequency);
    const double yHi = mapFrequencyToY(maxFrequency);
    const double yTop = std::min(yLo, yHi);
    const double height = std::abs(yLo - yHi);
    const int width = viewport().width();

    // Modest fill, strong edges: over a bright spectrogram a translucent fill alone
    // disappears, and it's the edge lines that make the band readable.
    QColor fill = color;
    fill.setAlphaF(mLightMode ? 0.24 : 0.26);
    p->drawFilledRect(0, (float) yTop, (float) width, (float) height, fill);

    p->drawLine(0, (float) yLo, (float) width, (float) yLo, color, 2.2f);
    p->drawLine(0, (float) yHi, (float) width, (float) yHi, color, 2.2f);
}

void QPainterWrapper::drawFrequencyTrack(
            const rpm::vector<std::pair<double, double>>& trackPoints,
            float radius,
            const QColor &color)
{
    rpm::vector<QPointF> points;

    for (const auto& [time, frequency] : trackPoints) {
        points.emplace_back(mapTimeToX(time), mapFrequencyToY(frequency));
    }

    p->drawScatterWithOutline(points, radius, color);
}

void QPainterWrapper::drawFrequencyTrack(
            const OptionalTimeTrack<double>::const_iterator& begin,
            const OptionalTimeTrack<double>::const_iterator& end,
            float radius,
            const QColor &color)
{
    rpm::vector<QPointF> points;

    for (auto it = begin; it != end; ++it) {
        if (it->second.has_value()) {
            double time = it->first;
            double pitch = *(it->second);

            double x = mapTimeToX(time);
            double y = mapFrequencyToY(pitch);

            points.emplace_back(x, y);
        }
    }

    p->drawScatterWithOutline(points, radius, color);
}

