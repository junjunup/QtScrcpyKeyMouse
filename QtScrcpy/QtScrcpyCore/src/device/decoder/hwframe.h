#ifndef HWFRAME_H
#define HWFRAME_H

extern "C"
{
#include "libavutil/frame.h"
}

// Hardware decoders hand back NV12 (Y plane + interleaved UV plane) after the
// copy to system memory, the renderer takes YUV420P (three planes).
// Allocates dst and fills it from src; returns false if src is not NV12 or on
// allocation failure.
bool nv12ToYuv420p(const AVFrame *src, AVFrame *dst);

#endif // HWFRAME_H
