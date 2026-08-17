#include "canvas.h"
#include "../context/timings.h"
#include "../context/rendercontext.h"
#include "qpainterwrapper.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <QQuickWindow>
#include <QScreen>

#ifdef _WIN32
/**
 * Export these symbols to signal to use dedicated graphics card on laptops.
 */
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

using namespace Gui;

void CanvasRenderer::initialize(Main::RenderContext *renderContext)
{
    mRenderContext = renderContext;
    initializeOpenGLFunctions();

#ifdef GL_DEBUG_OUTPUT
    const char *glDebugEnv = std::getenv("GL_DEBUG_OUTPUT");
    if (glDebugEnv != nullptr && strcmp(glDebugEnv, "1") == 0) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback([](GLenum source,
                                    GLenum type,
                                    GLuint id,
                                    GLenum severity,
                                    GLsizei length,
                                    const GLchar *message,
                                    const void *userParam) {
                                        std::cout << message << std::endl;
                                    }, nullptr);
    }
#endif

    std::cout << "Gui::CanvasRenderer] GL_VERSION: "
              << (const char *) glGetString(GL_VERSION) << std::endl;
    std::cout << "Gui::CanvasRenderer] GL_SHADING_LANGUAGE_VERSION: "
              << (const char *) glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "Gui::CanvasRenderer] GL_RENDERER: "
              << (const char *) glGetString(GL_RENDERER) << std::endl;

    mDevicePixelRatio = 1.0;
    mDpi = 96.0;

    // mZoomScaleText was previously left uninitialised here while initFonts() read it
    // to size every glyph, so the first atlas was rasterised from an indeterminate
    // value (undefined behaviour, and -ffast-math is on globally).
    mZoomScale = 1.0;
    mZoomScaleText = 1.0;

    ensureFonts();
    initShaders();

    initTexture(mSpecTex, 2048, 4096+2);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mSpecTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 2048, 4096+2, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CanvasRenderer::cleanup()
{
    deleteFonts();
    deleteShaders();

    glDeleteTextures(1, &mSpecTex);
}

void CanvasRenderer::synchronize(QQuickFramebufferObject *item)
{
    QQuickWindow *window = item->window();

    mDevicePixelRatio = window->devicePixelRatio();
    mWidth = item->width() * mDevicePixelRatio;
    mHeight = item->height() * mDevicePixelRatio;

    // screen() is null while the window is between screens; keep the previous DPI
    // rather than dereferencing it.
    if (QScreen *screen = window->screen()) {
        mDpi = screen->logicalDotsPerInch();
    }

    // This used to unconditionally tear down and re-rasterise all three font atlases
    // on every single frame -- roughly 765 glyph textures deleted and re-uploaded per
    // frame. ensureFonts() rebuilds only when the DPI, device pixel ratio or text zoom
    // actually changed.
    ensureFonts();
}

void CanvasRenderer::render()
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  
    glViewport(0, 0, mWidth, mHeight);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);

    QPainterWrapper painter(this);
    mRenderContext->render(&painter);

    // Render/update timings, opt-in: this corner now belongs to the voice HUD.
    static const bool showTimings = (std::getenv("IF_DEBUG_TIMINGS") != nullptr);
    if (showTimings) {
        std::stringstream ss1;
        ss1 << "Render: " << timings::render;
        auto renderTimeString = ss1.str();

        std::stringstream ss2;
        ss2 << "Update: " << timings::update;
        auto updateTimeString = ss2.str();

        float y = (float) mHeight - 10;
        QRect textBox;

        textBox = textBoundsNormal(renderTimeString);
        drawTextNormalOutlined(10, y, Qt::white, renderTimeString);
        y -= textBox.height() + 10;

        drawTextNormalOutlined(10, y, Qt::white, updateTimeString);
    }
}

void CanvasRenderer::setZoomScale(double zoomScale)
{
    if (mZoomScale != zoomScale) {
        mZoomScale = zoomScale;

        // Rescale max zoom from 0.5-5x to 0.75-2x for fonts.
        if (mZoomScale > 1) {
            mZoomScaleText = 1 + (mZoomScale - 1) * (2.0 / 5.0);
        }
        else {
            mZoomScaleText = 1 - (1 - mZoomScale) * (0.5 / 0.75);
        }

        ensureFonts();
    }
}

void CanvasRenderer::drawTextNormal(float x, float y, const QColor &color, const std::string &text)
{
    drawText(mFontNormal, x, y, color, text);
}

void CanvasRenderer::drawTextNormalOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor)
{
    drawTextOutlined(mFontNormal, x, y, color, text, outlineColor);
}

QRect CanvasRenderer::textBoundsNormal(const std::string &text)
{
    return textBounds(mFontNormal, text);
}

void CanvasRenderer::drawTextSmall(float x, float y, const QColor &color, const std::string &text)
{
    drawText(mFontSmall, x, y, color, text);
}

void CanvasRenderer::drawTextSmallOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor)
{
    drawTextOutlined(mFontSmall, x, y, color, text, outlineColor);
}

QRect CanvasRenderer::textBoundsSmall(const std::string &text)
{
    return textBounds(mFontSmall, text);
}

void CanvasRenderer::drawTextSmaller(float x, float y, const QColor &color, const std::string &text)
{
    drawText(mFontSmaller, x, y, color, text);
}

void CanvasRenderer::drawTextSmallerOutlined(float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor)
{
    drawTextOutlined(mFontSmaller, x, y, color, text, outlineColor);
}

QRect CanvasRenderer::textBoundsSmaller(const std::string &text)
{
    return textBounds(mFontSmaller, text);
}

void CanvasRenderer::drawDotsColored(const rpm::vector<QPointF> &points,
        const rpm::vector<QColor> &colors, float radius, float radiusAdd)
{
    if (points.empty()) {
        return;
    }

    // Batched: one interleaved buffer for every dot, one draw call. Colors are
    // per-dot; a single-element `colors` vector applies to all dots.
    const float radiusScale = mZoomScale * (mDpi * mDevicePixelRatio) / 96;

    static const float corners[6][2] = {
        { -1.5f, -1.5f }, { 1.5f, -1.5f }, { 1.5f, 1.5f },
        { -1.5f, -1.5f }, { 1.5f, 1.5f }, { -1.5f, 1.5f },
    };

    static rpm::vector<float> verts;
    verts.clear();
    verts.reserve(points.size() * 6 * 8);

    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        const QColor& col = colors.size() == 1 ? colors[0] : colors[i];
        const float r = (float) col.redF();
        const float g = (float) col.greenF();
        const float b = (float) col.blueF();
        for (const auto& c : corners) {
            verts.push_back(c[0]);
            verts.push_back(c[1]);
            verts.push_back((float) point.x());
            verts.push_back((float) point.y());
            verts.push_back(radius);
            verts.push_back(r);
            verts.push_back(g);
            verts.push_back(b);
        }
    }

    mDotsProgram->bind();

    QMatrix4x4 projection;
    projection.ortho(0, mWidth, 0, mHeight, -1, 1);
    mDotsProgram->setUniformValue("projection", projection);
    mDotsProgram->setUniformValue("radiusScale", radiusScale);
    mDotsProgram->setUniformValue("radiusAdd", radiusAdd);

    glBindVertexArray(mDotsVao);
    glBindBuffer(GL_ARRAY_BUFFER, mDotsVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei) (points.size() * 6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mDotsProgram->release();
}

void CanvasRenderer::drawScatterWithOutline(const rpm::vector<QPointF> &points, float radius, const QColor &fillColor, const QColor &outlineColor)
{
    rpm::vector<QColor> outline { outlineColor };
    rpm::vector<QColor> fill { fillColor };
    drawDotsColored(points, outline, radius, 0.6667f);
    drawDotsColored(points, fill, radius, 0.0f);
}

void CanvasRenderer::drawRibbon(const rpm::vector<QPointF> &points,
        const rpm::vector<QColor> &colors, float width)
{
    if (points.size() < 2) {
        return;
    }

    const float scale = mZoomScale * (mDpi * mDevicePixelRatio) / 96;
    const float halfW = 0.5f * width * scale;

    static rpm::vector<float> verts;
    verts.clear();
    verts.reserve((points.size() - 1) * 6 * 5);

    auto colorOf = [&](size_t i) -> const QColor& {
        return colors.size() == 1 ? colors[0] : colors[i];
    };

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const float x1 = (float) points[i].x(), y1 = (float) points[i].y();
        const float x2 = (float) points[i + 1].x(), y2 = (float) points[i + 1].y();
        float dx = x2 - x1, dy = y2 - y1;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < 1e-6f) {
            continue;
        }
        const float nx = -dy / d * halfW, ny = dx / d * halfW;

        const QColor& c1 = colorOf(i);
        const QColor& c2 = colorOf(i + 1);
        const float r1 = (float) c1.redF(), g1 = (float) c1.greenF(), b1 = (float) c1.blueF();
        const float r2 = (float) c2.redF(), g2 = (float) c2.greenF(), b2 = (float) c2.blueF();

        const float quad[6][5] = {
            { x1 - nx, y1 - ny, r1, g1, b1 },
            { x1 + nx, y1 + ny, r1, g1, b1 },
            { x2 + nx, y2 + ny, r2, g2, b2 },
            { x1 - nx, y1 - ny, r1, g1, b1 },
            { x2 + nx, y2 + ny, r2, g2, b2 },
            { x2 - nx, y2 - ny, r2, g2, b2 },
        };
        for (const auto& v : quad) {
            verts.insert(verts.end(), v, v + 5);
        }
    }

    if (verts.empty()) {
        return;
    }

    mRibbonProgram->bind();

    QMatrix4x4 projection;
    projection.ortho(0, mWidth, 0, mHeight, -1, 1);
    mRibbonProgram->setUniformValue("projection", projection);
    mRibbonProgram->setUniformValue("alphaMul", 1.0f);

    glBindVertexArray(mRibbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, mRibbonVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei) (verts.size() / 5));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mRibbonProgram->release();

    // Rounded joints, batched through the dots path (radius is pre-scale units).
    drawDotsColored(points, colors, 0.5f * width, 0.0f);
}

void CanvasRenderer::drawAreaStrip(const rpm::vector<QPointF> &points,
        float yTop, float yBottom, const QColor &above, const QColor &below)
{
    if (points.size() < 2) {
        return;
    }

    static rpm::vector<float> verts;

    auto emitQuads = [&](const QColor &color, bool fillBelow) {
        verts.clear();
        verts.reserve((points.size() - 1) * 6 * 5);
        const float r = (float) color.redF();
        const float g = (float) color.greenF();
        const float b = (float) color.blueF();
        const float edge = fillBelow ? yBottom : yTop;
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const float x1 = (float) points[i].x(), y1 = (float) points[i].y();
            const float x2 = (float) points[i + 1].x(), y2 = (float) points[i + 1].y();
            const float quad[6][5] = {
                { x1, y1, r, g, b },
                { x2, y2, r, g, b },
                { x2, edge, r, g, b },
                { x1, y1, r, g, b },
                { x2, edge, r, g, b },
                { x1, edge, r, g, b },
            };
            for (const auto& v : quad) {
                verts.insert(verts.end(), v, v + 5);
            }
        }

        mRibbonProgram->setUniformValue("alphaMul", (GLfloat) color.alphaF());
        glBindBuffer(GL_ARRAY_BUFFER, mRibbonVbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei) (verts.size() / 5));
    };

    mRibbonProgram->bind();

    QMatrix4x4 projection;
    projection.ortho(0, mWidth, 0, mHeight, -1, 1);
    mRibbonProgram->setUniformValue("projection", projection);

    glBindVertexArray(mRibbonVao);
    emitQuads(below, true);
    emitQuads(above, false);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mRibbonProgram->release();
}

void CanvasRenderer::drawPolyTrack(const rpm::vector<QPointF> &points,
        const rpm::vector<QColor> &colors, float size, bool asLine,
        const QColor &outlineColor)
{
    if (points.empty()) {
        return;
    }

    rpm::vector<QColor> outline { outlineColor };
    const bool wantOutline = outlineColor.alpha() > 0;
    if (asLine) {
        if (wantOutline) {
            drawRibbon(points, outline, size + 2.4f);
        }
        drawRibbon(points, colors, size);
    }
    else {
        if (wantOutline) {
            drawDotsColored(points, outline, size, 0.8f);
        }
        drawDotsColored(points, colors, size, 0.0f);
    }
}

void CanvasRenderer::drawPoint(const QPointF &point, float radius, const QColor &color)
{
    radius *= mZoomScale * (mDpi * mDevicePixelRatio) / 96;

    /*constexpr int N = 24;
    float vertices[2*(N+1)];
    for (int i = 0; i <= N; ++i) {
        float angle = 2 * M_PI * i / N;
        float x = cos(angle) * radius;
        float y = sin(angle) * radius;
        vertices[2*i] = point.x() + x;
        vertices[2*i+1] = mHeight - point.y() + y;
    }*/
    /*glColor4f(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, N+1);*/

    mCircleProgram->bind();

    float w = mWidth;
    float h = mHeight;

    QMatrix4x4 projection;
    projection.ortho(0, w, 0, h, -1, 1);
    mCircleProgram->setUniformValue("projection", projection);

    mCircleProgram->setUniformValue("resolution", mWidth, mHeight);
    mCircleProgram->setUniformValue("center", point.x(), point.y());
    mCircleProgram->setUniformValue("radius", radius);
    mCircleProgram->setUniformValue("fillColor", color.redF(), color.greenF(), color.blueF());
    
    float x1 = point.x() - 1.5 * radius;
    float x2 = point.x() + 1.5 * radius;
    float y1 = point.y() - 1.5 * radius;
    float y2 = point.y() + 1.5 * radius;

    glBindVertexArray(mCircleVao);

    float vertices[4][2] = {
        { x2, y1 },
        { x2, y2 },
        { x1, y2 },
        { x1, y1 },
    };

    glBindBuffer(GL_ARRAY_BUFFER, mCircleVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);

    mCircleProgram->release();
}

void CanvasRenderer::drawLine(float x1, float y1, float x2, float y2, const QColor &color, float thickness)
{
    thickness *= mZoomScale * (mDpi * mDevicePixelRatio) / 96;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float d = sqrt(dx * dx + dy * dy);

    float nx = -dy / d;
    float ny = dx / d;

    float qx1 = x1 - 0.5 * thickness * nx;
    float qx2 = x2 + 0.5 * thickness * nx;
    float qy1 = y1 - 0.5 * thickness * ny;
    float qy2 = y2 + 0.5 * thickness * ny;

    mCircleProgram->bind();

    float w = mWidth;
    float h = mHeight;

    QMatrix4x4 projection;
    projection.ortho(0, w, 0, h, -1, 1);
    mCircleProgram->setUniformValue("projection", projection);

    mCircleProgram->setUniformValue("resolution", mWidth, mHeight);
    mCircleProgram->setUniformValue("center", (qx1+qx2)/2, (qy1+qy2)/2);
    mCircleProgram->setUniformValue("radius", thickness);
    mCircleProgram->setUniformValue("fillColor", color.redF(), color.greenF(), color.blueF());

    glBindVertexArray(mCircleVao);

    float vertices[4][2] = {
        { qx1, qy2 },
        { qx1, qy1 },
        { qx2, qy1 },
        { qx2, qy2 },
    };

    glBindBuffer(GL_ARRAY_BUFFER, mCircleVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);

    mCircleProgram->release();
}

void CanvasRenderer::drawFilledRect(float x, float y, float w, float h, const QColor &color)
{
    mRectProgram->bind();

    QMatrix4x4 projection;
    projection.ortho(0, mWidth, 0, mHeight, -1, 1);
    mRectProgram->setUniformValue("projection", projection);
    mRectProgram->setUniformValue("fillColor",
            (GLfloat) color.redF(), (GLfloat) color.greenF(),
            (GLfloat) color.blueF(), (GLfloat) color.alphaF());

    glBindVertexArray(mRectVao);

    float vertices[4][2] = {
        { x,     y     },
        { x,     y + h },
        { x + w, y + h },
        { x + w, y     },
    };

    glBindBuffer(GL_ARRAY_BUFFER, mRectVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);

    mRectProgram->release();
}

void CanvasRenderer::drawSpectrogram(
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
        float timeEnd)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mSpecTex);

    if (chunkSize1 > 0) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, xOffset % 2048, 0, chunkSize1, 4096, GL_RED, GL_FLOAT, chunkData1.data());
    }
    if (chunkSize2 > 0) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunkSize2, 4096, GL_RED, GL_FLOAT, chunkData2.data());
    }

    std::array<GLfloat, 2048 * 2> extraData;
    for (int x = 0; x < 2048; ++x) {
        extraData[0 * 2048 + x] = nffts[x];
        extraData[1 * 2048 + x] = sampleRates[x];
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 4096, 2048, 2, GL_RED, GL_FLOAT, extraData.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    mSpecProgram->bind();

    float w = mWidth;
    float h = mHeight;

    QMatrix4x4 projection;
    projection.ortho(0, w, 0, h, -1, 1);
    mSpecProgram->setUniformValue("projection", projection);

    std::array<QVector3D, 256> cmap;
    for (int i = 0; i < 256; ++i) {
        QColor color = QColor::fromRgb(colorTable[i]);
        cmap[i] = QVector3D(color.redF(), color.greenF(), color.blueF());
    }
    mSpecProgram->setUniformValueArray("colorMap", cmap.data(), 256);

    mSpecProgram->setUniformValue("frequencyScale", static_cast<int>(freqScale));
    mSpecProgram->setUniformValue("minFrequency", minFrequency);
    mSpecProgram->setUniformValue("maxFrequency", maxFrequency);
    mSpecProgram->setUniformValue("maxGain", maxGain);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(mSpecVao);

    float x2 = w * (sliceTimeEnd - timeStart) / (timeEnd - timeStart);
    float x1 = w * (sliceTimeStart - timeStart) / (timeEnd - timeStart);
    float y2 = h;
    float y1 = 0;

    // Exact texel-center mapping. The old code spread `totalSize` slice columns
    // across a span of totalSize+1 columns anchored at the newest edge, so content
    // was progressively displaced toward the left of the view -- and since the
    // visible slice count oscillates by one as slices enter and leave the window on
    // different frames, the whole texture visibly breathed forward/back by 1-2
    // columns. Quad edge x2 is the newest slice's time, whose texel center is
    // head-0.5; x1 is the oldest visible slice's time, texel center
    // head-(totalSize-1)-0.5. Negative coordinates are fine: wrap S is GL_REPEAT.
    const float head = float(headIndex % 2048);
    float texX2 = (head - 0.5f) / 2048.0f;
    float texX1 = (head - float(totalSize) + 0.5f) / 2048.0f;
    float texY1 = 0.0f;
    float texY2 = 4095.0f / 4098.0f;

    float vertices[4][4] = {
        { x2, y1, texX2, texY2 },
        { x2, y2, texX2, texY1 },
        { x1, y2, texX1, texY1 },
        { x1, y1, texX1, texY2 },
    };

    glBindTexture(GL_TEXTURE_2D, mSpecTex);

    glBindBuffer(GL_ARRAY_BUFFER, mSpecVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    mSpecProgram->release();
}

QRect CanvasRenderer::viewport() const
{
    return QRect(0, 0, mWidth, mHeight);
}

void CanvasRenderer::drawText(Font *font, float x, float y, const QColor &color, const std::string &text)
{
    mTextProgram->bind();

    y = mHeight - y;

    QMatrix4x4 projection;
    projection.ortho(0, mWidth, mHeight, 0, -1, 1);
    mTextProgram->setUniformValue("projection", projection);
    mTextProgram->setUniformValue("textColor", color.redF(), color.greenF(), color.blueF());

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(mTextVao);

    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); ++c) {
        if (*c == '\0') break;

        FontCharacter ch = font->charFor(*c);

        float xpos = x + ch.bearingX;
        float ypos = y - (ch.height - ch.bearingY);

        float w = ch.width;
        float h = ch.height;

        float vertices[4][4] = {
            { xpos + w, ypos,     1.0f, 1.0f },
            { xpos + w, ypos + h, 1.0f, 0.0f },
            { xpos,     ypos + h, 0.0f, 0.0f },
            { xpos,     ypos,     0.0f, 1.0f },
        };

        glBindTexture(GL_TEXTURE_2D, ch.texture);
        
        // Update content of VBO memory
        glBindBuffer(GL_ARRAY_BUFFER, mTextVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Render quad
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        x += (ch.advance >> 6);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    mTextProgram->release();
}

void CanvasRenderer::drawTextOutlined(Font *font, float x, float y, const QColor &color, const std::string &text, const QColor &outlineColor)
{
    const float delta = 0.66667 * mZoomScaleText;

    for (int dxi = -1; dxi <= 1; dxi += 2) {
        for (int dyi = -1; dyi <= 1; dyi += 2) {
            drawText(font, x + delta * dxi, y + delta * dyi, outlineColor, text);
        }
    }
    drawText(font, x, y, color, text);
}

QRect CanvasRenderer::textBounds(Font *font, const std::string &text)
{
    float x = 0;
    float y = 0;
    
    QRect rect;
    float xmax = 0;
    float ymax = 0;

    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); ++c) {
        FontCharacter ch = font->charFor(*c);

        float xpos = x + ch.bearingX;
        float ypos = y - (ch.height - ch.bearingY);

        float w = ch.width;
        float h = ch.height;

        if (c == text.begin()) {
            rect.setX(xpos);
            rect.setY(ypos);
        }

        if (xpos + w > xmax) {
            xmax = xpos + w;
        }
        if (ypos + h > ymax) {
            ymax = ypos + h;
        }

        x += (ch.advance >> 6);
    }

    rect.setWidth(xmax - rect.x());
    rect.setHeight(ymax - rect.y());

    return rect;
}

void CanvasRenderer::initFonts()
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cout << "ERROR::FREETYPE: Could not init FreeType library" << std::endl;
        return;
    }

    mFontNormal = new Font(ft, ":/Roboto.ttf", std::min(14 * mDevicePixelRatio * mZoomScaleText, 24.0), mDpi);
    mFontSmall = new Font(ft, ":/Roboto.ttf", std::min(12 * mDevicePixelRatio * mZoomScaleText, 20.0), mDpi);
    mFontSmaller = new Font(ft, ":/Roboto.ttf", std::min(10 * mDevicePixelRatio * mZoomScaleText, 18.0), mDpi);

    FT_Done_FreeType(ft);
}

void CanvasRenderer::ensureFonts()
{
    if (mFontNormal != nullptr
            && mFontsBuiltDpr == mDevicePixelRatio
            && mFontsBuiltDpi == mDpi
            && mFontsBuiltZoomText == mZoomScaleText) {
        return;
    }

    deleteFonts();
    initFonts();

    mFontsBuiltDpr = mDevicePixelRatio;
    mFontsBuiltDpi = mDpi;
    mFontsBuiltZoomText = mZoomScaleText;
}

void CanvasRenderer::initShaders()
{
    mTextProgram = createShaderProgram(Shaders::textVertex, Shaders::textFragment);

    glGenVertexArrays(1, &mTextVao);
    glGenBuffers(1, &mTextVbo);
    glBindVertexArray(mTextVao);
    glBindBuffer(GL_ARRAY_BUFFER, mTextVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mSpecProgram = createShaderProgram(Shaders::specVertex, Shaders::specFragment);

    glGenVertexArrays(1, &mSpecVao);
    glGenBuffers(1, &mSpecVbo);
    glBindVertexArray(mSpecVao);
    glBindBuffer(GL_ARRAY_BUFFER, mSpecVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mCircleProgram = createShaderProgram(Shaders::circleVertex, Shaders::circleFragment);

    glGenVertexArrays(1, &mCircleVao);
    glGenBuffers(1, &mCircleVbo);
    glBindVertexArray(mCircleVao);
    glBindBuffer(GL_ARRAY_BUFFER, mCircleVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mRectProgram = createShaderProgram(Shaders::rectVertex, Shaders::rectFragment);

    glGenVertexArrays(1, &mRectVao);
    glGenBuffers(1, &mRectVbo);
    glBindVertexArray(mRectVao);
    glBindBuffer(GL_ARRAY_BUFFER, mRectVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mDotsProgram = createShaderProgram(Shaders::dotsVertex, Shaders::dotsFragment);

    const int locCorner = mDotsProgram->attributeLocation("corner");
    const int locCenter = mDotsProgram->attributeLocation("center");
    const int locRadius = mDotsProgram->attributeLocation("radiusBase");
    const int locDotColor = mDotsProgram->attributeLocation("dotColor");

    glGenVertexArrays(1, &mDotsVao);
    glGenBuffers(1, &mDotsVbo);
    glBindVertexArray(mDotsVao);
    glBindBuffer(GL_ARRAY_BUFFER, mDotsVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(locCorner);
    glVertexAttribPointer(locCorner, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *) 0);
    glEnableVertexAttribArray(locCenter);
    glVertexAttribPointer(locCenter, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *) (2 * sizeof(float)));
    glEnableVertexAttribArray(locRadius);
    glVertexAttribPointer(locRadius, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *) (4 * sizeof(float)));
    glEnableVertexAttribArray(locDotColor);
    glVertexAttribPointer(locDotColor, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *) (5 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    mRibbonProgram = createShaderProgram(Shaders::ribbonVertex, Shaders::ribbonFragment);

    const int locPos = mRibbonProgram->attributeLocation("pos");
    const int locVColor = mRibbonProgram->attributeLocation("vcolor");

    glGenVertexArrays(1, &mRibbonVao);
    glGenBuffers(1, &mRibbonVbo);
    glBindVertexArray(mRibbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, mRibbonVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(locPos);
    glVertexAttribPointer(locPos, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) 0);
    glEnableVertexAttribArray(locVColor);
    glVertexAttribPointer(locVColor, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) (2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void CanvasRenderer::deleteFonts()
{
    delete mFontNormal;
    delete mFontSmall;
    delete mFontSmaller;

    mFontNormal = nullptr;
    mFontSmall = nullptr;
    mFontSmaller = nullptr;
}

void CanvasRenderer::deleteShaders()
{
    delete mTextProgram;
    glDeleteVertexArrays(1, &mTextVao);
    glDeleteBuffers(1, &mTextVbo);

    delete mSpecProgram;
    glDeleteVertexArrays(1, &mSpecVao);
    glDeleteBuffers(1, &mSpecVbo);

    delete mRectProgram;
    glDeleteVertexArrays(1, &mRectVao);
    glDeleteBuffers(1, &mRectVbo);

    delete mDotsProgram;
    glDeleteVertexArrays(1, &mDotsVao);
    glDeleteBuffers(1, &mDotsVbo);

    delete mRibbonProgram;
    glDeleteVertexArrays(1, &mRibbonVao);
    glDeleteBuffers(1, &mRibbonVbo);
}

void CanvasRenderer::initTexture(GLuint &texture, int width, int height)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
}

QOpenGLShaderProgram *CanvasRenderer::createShaderProgram(
        const char *vertexSource, const char *fragmentSource)
{
    auto program = new QOpenGLShaderProgram();

    if (!program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        std::cout << "Gui::CanvasRenderer] Vertex shader failed to compile: "
                  << program->log().toStdString() << std::endl;
    }

    if (!program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        std::cout << "Gui::CanvasRenderer] Fragment shader failed to compile: "
                  << program->log().toStdString() << std::endl;
    }

    if (!program->link()) {
        std::cout << "Gui::CanvasRenderer] Shader program failed to link: "
                  << program->log().toStdString() << std::endl;
    }

    return program;
}

