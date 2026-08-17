#include "canvas.h"
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QImage>
#include <QOpenGLFramebufferObject>
#include <QTextStream>

using namespace Gui;

CanvasItem::CanvasItem()
    : mRenderContext(nullptr)
{
    setObjectName("IfCanvas");
    setTextureFollowsItemSize(true);
}

void CanvasItem::setRenderContext(Main::RenderContext *renderContext)
{
    mRenderContext = renderContext;
}

class CanvasInFboRenderer : public QQuickFramebufferObject::Renderer
{
public:
    CanvasInFboRenderer(Main::RenderContext *renderContext) {
        mRenderer.initialize(renderContext);
    }

    ~CanvasInFboRenderer() {
        mRenderer.cleanup();
    }

protected:
    void render() override {
        mRenderer.render();

        // Opt-in probe (IF_DUMP_FRAMES=<dir>): capture the FBO contents of every
        // rendered frame for a couple of seconds, so per-frame content motion can
        // be measured directly. This is the app's output BEFORE the compositor
        // touches it: if wobble shows here it is ours, if these frames are smooth
        // the wobble is added downstream.
        static const char *dumpDir = std::getenv("IF_DUMP_FRAMES");
        if (dumpDir != nullptr) {
            captureFrame(dumpDir);
        }

        update();
    }

    void synchronize(QQuickFramebufferObject *item) override {
        mRenderer.synchronize(item);
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setSamples(4);
        return new QOpenGLFramebufferObject(size, format);
    }

private:
    void captureFrame(const char *dir) {
        constexpr int kWarmup = 90;
        constexpr int kCount = 240;

        ++mFrameNo;

        if (mFrameNo > kWarmup && mFrameNo <= kWarmup + kCount) {
            QOpenGLFramebufferObject *fbo = framebufferObject();
            if (fbo == nullptr) {
                return;
            }
            // toImage() resolves the multisampled FBO internally.
            QImage img = fbo->toImage();
            const int stripH = std::min(400, img.height());
            const int y0 = std::max(0, (img.height() - stripH) / 2);
            mStrips.push_back(img.copy(0, y0, img.width(), stripH)
                    .convertToFormat(QImage::Format_Grayscale8));
            mStamps.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        else if (mFrameNo == kWarmup + kCount + 1) {
            for (size_t i = 0; i < mStrips.size(); ++i) {
                mStrips[i].save(QString("%1/frame_%2.png")
                        .arg(dir).arg((int) i, 4, 10, QChar('0')));
            }
            QFile f(QString("%1/stamps.csv").arg(dir));
            if (f.open(QIODevice::WriteOnly)) {
                QTextStream ts(&f);
                for (size_t i = 0; i < mStamps.size(); ++i) {
                    ts << i << "," << mStamps[i] << "\n";
                }
            }
            std::cout << "CanvasItem] dumped " << mStrips.size()
                      << " frames to " << dir << std::endl;
            mStrips.clear();
            mStamps.clear();
        }
    }

    CanvasRenderer mRenderer;

    int mFrameNo = 0;
    rpm::vector<QImage> mStrips;
    rpm::vector<int64_t> mStamps;
};

QQuickFramebufferObject::Renderer *CanvasItem::createRenderer() const
{
    return new CanvasInFboRenderer(mRenderContext);
}
