#include <stdlib.h>
#include <string.h>
#include <VapourSynth4.h>
#include "VSHelper4.h"

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    uint16_t maxVal;
} LumaData;


static const VSFrame *VS_CC lumaGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LumaData *d = (LumaData *) instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSVideoFormat *fi = &d->vi.format;
        const int src_height = vsapi->getFrameHeight(src, 0);
        const int src_width = vsapi->getFrameWidth(src, 0);

        VSFrame *dst = vsapi->newVideoFrame(fi, src_width, src_height, src, core);

        int src_stride = vsapi->getStride(src, 0);
        int dst_stride = vsapi->getStride(dst, 0);

        const uint8_t highBitDepth = fi->bitsPerSample / 9;

        int y;
        int x;

        //Some duplicate code due to lack of templates
        if (highBitDepth) {
            src_stride /= 2;
            dst_stride /= 2;
            const uint16_t *srcp = (const uint16_t *)vsapi->getReadPtr(src, 0);
            uint16_t *dstp = (uint16_t *)vsapi->getWritePtr(dst, 0);
            for (y = 0; y < src_height; y++) {
                for (x = 0; x < src_width; x++) {
                    int p = srcp[x] << 4;
                    dstp[x] = (p & (d->maxVal + 1)) ? (d->maxVal - (p & d->maxVal)) : p & d->maxVal;
                }
                srcp += src_stride;
                dstp += dst_stride;
            }
        }
        else {
            const uint8_t *srcp = vsapi->getReadPtr(src, 0);
            uint8_t *dstp = vsapi->getWritePtr(dst, 0);
            for (y = 0; y < src_height; y++) {
                for (x = 0; x < src_width; x++) {
                    int p = srcp[x] << 4;
                    dstp[x] = (p & (d->maxVal + 1)) ? (d->maxVal - (p & d->maxVal)) : p & d->maxVal;
                }
                srcp += src_stride;
                dstp += dst_stride;
            }
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}


static void VS_CC lumaFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    LumaData *d = (LumaData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}


void VS_CC lumaCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    LumaData d;
    LumaData *data;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!vsh_isConstantVideoFormat(&d.vi) || d.vi.format.sampleType != stInteger || d.vi.format.bitsPerSample > 16) {
        vsapi->mapSetError(out, "Luma: only constant format 8 to 16 bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    // We don't need any chroma.
    vsapi->queryVideoFormat(&d.vi.format, cfGray, stInteger, d.vi.format.bitsPerSample, 0, 0, core);

    d.maxVal = (1 << d.vi.format.bitsPerSample) - 1;

    data = (LumaData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { {d.node, rpStrictSpatial} };
    vsapi->createVideoFilter(out, "Luma", &d.vi, lumaGetFrame, lumaFree, fmParallel, deps, 1, data, core);
}

