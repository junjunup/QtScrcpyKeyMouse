#include <QDebug>

extern "C"
{
#include "libavutil/hwcontext.h"
}

#include "compat.h"
#include "decoder.h"
#include "hwframe.h"
#include "videobuffer.h"

Decoder::Decoder(std::function<void(int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int)> onFrame, bool tryHardware, QObject *parent)
    : IDecoder(parent)
    , m_vb(new VideoBuffer())
    , m_onFrame(onFrame)
    , m_tryHardware(tryHardware)
{
    m_vb->init();
    connect(this, &Decoder::newFrame, this, &Decoder::onNewFrame, Qt::QueuedConnection);
    connect(m_vb, &VideoBuffer::updateFPS, this, &Decoder::updateFPS);
}

Decoder::~Decoder() {
    m_vb->deInit();
    delete m_vb;
}

bool Decoder::open()
{
    // codec
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qCritical("H.264 decoder not found");
        return false;
    }

    // codec context
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("Could not allocate decoder context");
        return false;
    }
    if (m_tryHardware) {
        initHardware();
    }
    if (avcodec_open2(m_codecCtx, codec, NULL) < 0) {
        qCritical("Could not open H.264 codec");
        return false;
    }
    m_isCodecCtxOpen = true;
    return true;
}

void Decoder::close()
{
    if (m_vb) {
        m_vb->interrupt();
    }

    if (!m_codecCtx) {
        return;
    }
    if (m_isCodecCtxOpen) {
        avcodec_close(m_codecCtx);
    }
    avcodec_free_context(&m_codecCtx);
    av_buffer_unref(&m_hwDeviceCtx);
    av_frame_free(&m_hwTransferFrame);
}

void Decoder::initHardware()
{
    // D3D11VA first (Windows 8+), DXVA2 for older drivers. FFmpeg builds without
    // these hwaccels (like the one bundled for development) simply fail here.
    const AVHWDeviceType types[] = {
#ifdef Q_OS_WIN
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#endif
        AV_HWDEVICE_TYPE_NONE
    };
    const AVCodec *codec = m_codecCtx->codec;
    for (int i = 0; types[i] != AV_HWDEVICE_TYPE_NONE; ++i) {
        int pixFmt = -1;
        for (int j = 0;; ++j) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config) {
                break;
            }
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == types[i]) {
                pixFmt = config->pix_fmt;
                break;
            }
        }
        if (pixFmt < 0) {
            continue;
        }
        if (av_hwdevice_ctx_create(&m_hwDeviceCtx, types[i], NULL, NULL, 0) < 0) {
            continue;
        }
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_codecCtx->opaque = this;
        m_codecCtx->get_format = &Decoder::getFormat;
        m_hwPixFmt = pixFmt;
        m_hwTransferFrame = av_frame_alloc();
        qInfo("hardware decoding: %s", av_hwdevice_get_type_name(types[i]));
        return;
    }
    qInfo("hardware decoding is not available, using software decoding");
}

enum AVPixelFormat Decoder::getFormat(AVCodecContext *ctx, const enum AVPixelFormat *formats)
{
    const Decoder *self = static_cast<const Decoder *>(ctx->opaque);
    for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        if (self && *p == self->m_hwPixFmt) {
            return *p;
        }
    }
    // the GPU cannot decode this stream: FFmpeg decodes it in software
    qWarning("hardware decoding does not support this stream, using software decoding");
    return avcodec_default_get_format(ctx, formats);
}

bool Decoder::downloadHardwareFrame(AVFrame *frame)
{
    if (!m_hwTransferFrame) {
        return false;
    }
    av_frame_unref(m_hwTransferFrame);
    int ret = av_hwframe_transfer_data(m_hwTransferFrame, frame, 0);
    // give the GPU surface back to the decoder right away
    av_frame_unref(frame);
    if (ret < 0 || !nv12ToYuv420p(m_hwTransferFrame, frame)) {
        if (!m_hwErrorLogged) {
            m_hwErrorLogged = true;
            qCritical("Could not copy the hardware decoded frame (%d), choose software decoding if the picture stays black", ret);
        }
        av_frame_unref(m_hwTransferFrame);
        return false;
    }
    av_frame_unref(m_hwTransferFrame);
    return true;
}

void Decoder::onVideoSessionChanged(const QSize &size)
{
    Q_UNUSED(size);
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
}

void Decoder::setRenderExpiredFrames(bool enabled)
{
    if (m_vb) {
        m_vb->setRenderExpiredFrames(enabled);
    }
}

bool Decoder::push(const AVPacket *packet)
{
    if (!m_codecCtx || !m_vb) {
        return false;
    }
    AVFrame *decodingFrame = m_vb->decodingFrame();
#ifdef QTSCRCPY_LAVF_HAS_NEW_ENCODING_DECODING_API
    int ret = -1;
    if ((ret = avcodec_send_packet(m_codecCtx, packet)) < 0) {
        char errorbuf[255] = { 0 };
        av_strerror(ret, errorbuf, 254);
        qCritical("Could not send video packet: %s", errorbuf);
        return false;
    }
    if (decodingFrame) {
        ret = avcodec_receive_frame(m_codecCtx, decodingFrame);
    }
    if (!ret && decodingFrame->format == m_hwPixFmt && !downloadHardwareFrame(decodingFrame)) {
        return true;
    }
    if (!ret) {
        // a frame was received
        pushFrame();

        //emit getOneFrame(yuvDecoderFrame->data[0], yuvDecoderFrame->data[1], yuvDecoderFrame->data[2],
        //        yuvDecoderFrame->linesize[0], yuvDecoderFrame->linesize[1], yuvDecoderFrame->linesize[2]);

        /*
        // m_conver转换yuv为rgb是使用cpu转的，占用cpu太高，改用opengl渲染yuv
        // QImage的copy也非常占用内存，此方案不考虑
        if (!m_conver.isInit()) {
            qDebug() << "decoder frame format" << decodingFrame->format;
            m_conver.setSrcFrameInfo(codecCtx->width, codecCtx->height, AV_PIX_FMT_YUV420P);
            m_conver.setDstFrameInfo(codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB32);
            m_conver.init();
        }
        if (!outBuffer) {
            outBuffer=new quint8[avpicture_get_size(AV_PIX_FMT_RGB32, codecCtx->width, codecCtx->height)];
            avpicture_fill((AVPicture *)rgbDecoderFrame, outBuffer, AV_PIX_FMT_RGB32, codecCtx->width, codecCtx->height);
        }
        m_conver.convert(decodingFrame, rgbDecoderFrame);
        //QImage tmpImg((uchar *)outBuffer, codecCtx->width, codecCtx->height, QImage::Format_RGB32);
        //QImage image = tmpImg.copy();
        //emit getOneImage(image);
        */
    } else if (ret != AVERROR(EAGAIN)) {
        qCritical("Could not receive video frame: %d", ret);
        return false;
    }
#else
    int gotPicture = 0;
    int len = -1;
    if (decodingFrame) {
        len = avcodec_decode_video2(m_codecCtx, decodingFrame, &gotPicture, packet);
    }
    if (len < 0) {
        qCritical("Could not decode video packet: %d", len);
        return false;
    }
    if (gotPicture) {
        pushFrame();
    }
#endif
    return true;
}

void Decoder::peekFrame(std::function<void (int, int, uint8_t *)> onFrame)
{
    if (!m_vb) {
        return;
    }
    m_vb->peekRenderedFrame(onFrame);
}

void Decoder::pushFrame()
{
    if (!m_vb) {
        return;
    }
    bool previousFrameSkipped = true;
    m_vb->offerDecodedFrame(previousFrameSkipped);
    if (previousFrameSkipped) {
        // the previous newFrame will consume this frame
        return;
    }
    emit newFrame();
}

void Decoder::onNewFrame() {
    if (!m_onFrame) {
        return;
    }

    m_vb->lock();
    const AVFrame *frame = m_vb->consumeRenderedFrame();
    m_onFrame(frame->width, frame->height, frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], frame->linesize[1], frame->linesize[2]);
    m_vb->unLock();
}
