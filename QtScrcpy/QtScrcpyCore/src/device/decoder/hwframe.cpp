#include <cstring>

extern "C"
{
#include "libavutil/pixfmt.h"
}

#include "hwframe.h"

bool nv12ToYuv420p(const AVFrame *src, AVFrame *dst)
{
    if (!src || !dst || src->format != AV_PIX_FMT_NV12 || src->width <= 0 || src->height <= 0) {
        return false;
    }

    av_frame_unref(dst);
    dst->format = AV_PIX_FMT_YUV420P;
    dst->width = src->width;
    dst->height = src->height;
    if (av_frame_get_buffer(dst, 32) < 0) {
        return false;
    }
    dst->pts = src->pts;

    const int width = src->width;
    const int height = src->height;
    for (int y = 0; y < height; ++y) {
        memcpy(dst->data[0] + y * dst->linesize[0], src->data[0] + y * src->linesize[0], static_cast<size_t>(width));
    }

    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    for (int y = 0; y < chromaHeight; ++y) {
        const uint8_t *uv = src->data[1] + y * src->linesize[1];
        uint8_t *u = dst->data[1] + y * dst->linesize[1];
        uint8_t *v = dst->data[2] + y * dst->linesize[2];
        for (int x = 0; x < chromaWidth; ++x) {
            u[x] = uv[2 * x];
            v[x] = uv[2 * x + 1];
        }
    }
    return true;
}
