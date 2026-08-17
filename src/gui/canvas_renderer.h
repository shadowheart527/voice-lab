#ifndef GUI_CANVAS_RENDERER_H
#define GUI_CANVAS_RENDERER_H

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QQuickFramebufferObject>
#include <QColor>
#include <limits>
#include <string>

#include "font.h"
#include "shaders/spec.h"
#include "shaders/circle.h"
#include "shaders/rect.h"
#include "shaders/dots.h"
#include "shaders/ribbon.h"
#include "../context/datastore.h"

namespace Main {
    class RenderContext;
}

namespace Gui {

    class CanvasRenderer : protected QOpenGLExtraFunctions {
    public:
        void initialize(Main::RenderContext *renderContext);
        void cleanup();
        void synchronize(QQuickFramebufferObject *item);
        
        void render();

        void setZoomScale(double zoomScale);

        void drawTextNormal(float x, float y, const QColor &color, const std::string &text);
        void drawTextNormalOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor = Qt::black);
        QRect textBoundsNormal(const std::string &text);

        void drawTextSmall(float x, float y, const QColor &color, const std::string &text);
        void drawTextSmallOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor = Qt::black);
        QRect textBoundsSmall(const std::string &text);

        void drawTextSmaller(float x, float y, const QColor &color, const std::string &text);
        void drawTextSmallerOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor = Qt::black);
        QRect textBoundsSmaller(const std::string &text);

        void drawScatterWithOutline(const rpm::vector<QPointF> &points, float radius, const QColor &fillColor, const QColor &outlineColor = Qt::black);

        // Track with a per-point color and a constant identity outline, drawn either
        // as outlined dots or as a connected ribbon with rounded joints.
        void drawPolyTrack(const rpm::vector<QPointF> &points,
                           const rpm::vector<QColor> &colors,
                           float size, bool asLine, const QColor &outlineColor);

        // Filled area chart strip: the polyline is a boundary, with one color
        // filled below it (down to yBottom) and another above (up to yTop).
        void drawAreaStrip(const rpm::vector<QPointF> &points,
                           float yTop, float yBottom,
                           const QColor &above, const QColor &below);
    
        void drawPoint(const QPointF &point, float radius, const QColor &color);

        void drawLine(float x1, float y1, float x2, float y2, const QColor &color, float thickness);

        void drawFilledRect(float x, float y, float w, float h, const QColor &color);

        void prepareSpectrogramDraw();
        void drawSpectrogram(
                int xOffset,
                int headIndex,
                int chunkSize1,
                int chunkSize2,
                int totalSize,
                const std::array<GLint, 2048>& nffts,
                const std::array<GLfloat, 2048>& sampleRates,
                const rpm::vector<GLfloat>& chunkData1,
                const rpm::vector<GLfloat>& chunkData2,
                FrequencyScale freqScale,
                float minFrequency,
                float maxFrequency,
                float maxGain,
                const QVector<QRgb>& colorTable,
                float sliceTimeStart,
                float sliceTimeEnd,
                float timeStart,
                float timeEnd);

        QRect viewport() const;

    private:
        void initFonts();
        void ensureFonts();
        void initShaders();
        void deleteFonts();
        void deleteShaders();

        void initTexture(GLuint &texture, int width, int height);
        QOpenGLShaderProgram *createShaderProgram(const char *vertexSource, const char *fragmentSource);

        void drawText(Font *font, float x, float y, const QColor &color, const std::string &text);
        void drawTextOutlined(Font *font, float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor = Qt::black);
        QRect textBounds(Font *font, const std::string &text);

        Main::RenderContext *mRenderContext = nullptr;

        double mDevicePixelRatio = 1.0;
        int mWidth = 0, mHeight = 0;
        double mDpi = 96.0;
        double mZoomScale = 1.0, mZoomScaleText = 1.0;

        // Parameters the current font atlases were rasterised with, so ensureFonts()
        // can skip rebuilding them when nothing relevant has changed. NaN forces the
        // first build.
        double mFontsBuiltDpr = std::numeric_limits<double>::quiet_NaN();
        double mFontsBuiltDpi = std::numeric_limits<double>::quiet_NaN();
        double mFontsBuiltZoomText = std::numeric_limits<double>::quiet_NaN();

        Font *mFontNormal = nullptr;
        Font *mFontSmall = nullptr;
        Font *mFontSmaller = nullptr;

        QOpenGLShaderProgram *mTextProgram = nullptr;
        GLuint mTextVao = 0, mTextVbo = 0;

        QOpenGLShaderProgram *mSpecProgram = nullptr;
        GLuint mSpecVao = 0, mSpecVbo = 0;

        QOpenGLShaderProgram *mCircleProgram = nullptr;
        GLuint mCircleVao = 0, mCircleVbo = 0;

        QOpenGLShaderProgram *mRectProgram = nullptr;
        GLuint mRectVao = 0, mRectVbo = 0;

        QOpenGLShaderProgram *mDotsProgram = nullptr;
        GLuint mDotsVao = 0, mDotsVbo = 0;

        QOpenGLShaderProgram *mRibbonProgram = nullptr;
        GLuint mRibbonVao = 0, mRibbonVbo = 0;

        void drawDotsColored(const rpm::vector<QPointF> &points,
                             const rpm::vector<QColor> &colors,
                             float radius, float radiusAdd);
        void drawRibbon(const rpm::vector<QPointF> &points,
                        const rpm::vector<QColor> &colors, float width);

        GLuint mSpecTex = 0;
    };

}

#endif // GUI_CANVAS_RENDERER_H
