#ifndef QPAINTER_WRAPPER_H
#define QPAINTER_WRAPPER_H

#include "rpcxx.h"
#include "canvas_renderer.h"
#include "../timetrack.h"
#include "../context/datastore.h"
#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <QImage>

struct SpectrogramTextureData {
    double timeStart, timeEnd;
    double sampleRate;
    int nfft, segmentLen;
    rpm::vector<GLfloat> texture;
};

class QPainterWrapper {
public:
    QPainterWrapper(Gui::CanvasRenderer *p);
    
    QRect viewport() const;

    void setZoom(double scale);

    void setTimeRange(double start, double end);

    void setFrequencyScale(FrequencyScale scale);
    void setMinFrequency(double minFrequency);
    void setMaxFrequency(double maxFrequency);
    void setMaxGain(double maxGain);

    void drawTimeAxis();
    void drawFrequencyScale();

    void drawTimeSeries(const rpm::vector<double> &y, double xstart, double xend, double ymin, double ymax); 

    void drawFrequencyTrack(const TimeTrack<double>::const_iterator& begin,
                            const TimeTrack<double>::const_iterator& end,
                            float radius,
                            const QColor &color);

    void drawFrequencyTrack(const rpm::vector<std::pair<double, double>>& points,
                            float radius,
                            const QColor &color);

    void drawTargetBand(double minFrequency, double maxFrequency, const QColor &color);

    void drawHudTextNormal(float x, float y, const QColor &color, const std::string &text);
    void drawHudTextSmall(float x, float y, const QColor &color, const std::string &text);
    QRect hudTextBoundsNormal(const std::string &text);
    QRect hudTextBoundsSmall(const std::string &text);

    void drawHudRect(float x, float y, float w, float h, const QColor &color);
    void drawHudDot(float x, float y, float radius, const QColor &fill);

    // Track with per-point colors (masc-to-fem gradient) and a constant identity
    // outline, drawn as dots or as a connected line.
    void drawGenderTrack(const rpm::vector<std::pair<double, double>>& points,
                         const rpm::vector<QColor>& colors,
                         float size, bool asLine, const QColor &identity);

    // Screen-space polyline/dots for HUD widgets (history graphs).
    void drawHudTrack(const rpm::vector<QPointF>& points,
                      const rpm::vector<QColor>& colors,
                      float size, bool asLine, const QColor &outline);

    void drawHudArea(const rpm::vector<QPointF>& points, float yTop, float yBottom,
                     const QColor &above, const QColor &below);

    void setLightMode(bool light);
    bool isLightMode() const { return mLightMode; }

    void drawFrequencyTrack(const OptionalTimeTrack<double>::const_iterator& begin,
                            const OptionalTimeTrack<double>::const_iterator& end,
                            float radius,
                            const QColor &color);

    double mapTimeToX(double time);
    double mapFrequencyToY(double frequency);

    void drawSpectrogram(const rpm::vector<std::pair<double, Main::SpectrogramCoefs>>& slices);

    static double mapTimeToX(double time, int width, double startTime, double endTime);
    static double mapFrequencyToY(double frequency, int height, FrequencyScale scale, double minFrequency, double maxFrequency);
    static double mapYToFrequency(double y, int height, FrequencyScale scale, double minFrequency, double maxFrequency);

private:
    double transformFrequency(double frequency);
    double inverseFrequency(double value);
    
    Gui::CanvasRenderer *p;

    bool mLightMode = false;

    double mTimeStart;
    double mTimeEnd;

    FrequencyScale mFrequencyScale;
    double mMinFrequency;
    double mMaxFrequency;
    double mMaxGain;

    static double transformFrequency(double frequency, FrequencyScale scale);
    static double inverseFrequency(double value, FrequencyScale scale);

    static Eigen::SparseMatrix<double>& constructTransformY(int h, int vh, FrequencyScale freqScale, double freqMin, double freqMax, FrequencyScale sourceScale, double sourceMin, double sourceMax);

    static QVector<QRgb> cmap;
    static const QVector<QRgb>& lightCmap();
};

#endif // QPAINTER_WRAPPER_H
