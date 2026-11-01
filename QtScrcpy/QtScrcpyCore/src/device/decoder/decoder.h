#ifndef DECODER_H
#define DECODER_H
#include <QObject>

extern "C"
{
#include "libavcodec/avcodec.h"
}

#include <functional>

#include "IDecoder.h"

class VideoBuffer;
class Decoder : public IDecoder
{
    Q_OBJECT
public:
    // tryHardware: decode on the GPU when FFmpeg and the driver allow it
    Decoder(std::function<void(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV)> onFrame, bool tryHardware = false, QObject *parent = Q_NULLPTR);
    virtual ~Decoder();

    bool open() override;
    void close() override;
    bool push(const AVPacket *packet) override;
    void peekFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> onFrame) override;
    void onVideoSessionChanged(const QSize &size) override;
    void setRenderExpiredFrames(bool enabled) override;

private slots:
    void onNewFrame();

signals:
    void newFrame();

private:
    void pushFrame();
    void initHardware();
    // copies a GPU frame in the decoding frame to system memory as YUV420P
    bool downloadHardwareFrame(AVFrame *frame);
    static enum AVPixelFormat getFormat(AVCodecContext *ctx, const enum AVPixelFormat *formats);

private:
    VideoBuffer *m_vb = Q_NULLPTR;
    AVCodecContext *m_codecCtx = Q_NULLPTR;
    bool m_isCodecCtxOpen = false;
    std::function<void(int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int)> m_onFrame = Q_NULLPTR;
    bool m_tryHardware = false;
    AVBufferRef *m_hwDeviceCtx = Q_NULLPTR;
    int m_hwPixFmt = -1;
    AVFrame *m_hwTransferFrame = Q_NULLPTR;
    bool m_hwErrorLogged = false;
};

#endif // DECODER_H
