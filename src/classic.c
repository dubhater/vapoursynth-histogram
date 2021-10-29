#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <VapourSynth4.h>
#include "VSHelper4.h"

#include "common.h"

typedef struct {
    VSNode *node;
    VSVideoInfo vi;

    int E167;
    uint8_t exptab[256];
} ClassicData;


static const VSFrame *VS_CC classicGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ClassicData *d = (ClassicData *) instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSVideoFormat *fi = &d->vi.format;
        int height = vsapi->getFrameHeight(src, 0);
        int width = vsapi->getFrameWidth(src, 0) + 256;

        VSFrame *dst = vsapi->newVideoFrame(fi, width, height, src, core);

        int plane;
        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t *srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);
            int y;
            int w = vsapi->getFrameWidth(src, plane);
            int x;

            // Copy src to dst one line at a time.
            for (y = 0; y < h; y++) {
                memcpy(dstp + dst_stride * y, srcp + src_stride * y, src_stride);
            }

            int bps = fi->bitsPerSample;
            if (bps == 8) {
                // Now draw the histogram in the right side of dst.
                if (plane == 0) {
                    for (y = 0; y < h; y++) {
                        int hist[256] = { 0 };
                        for (x = 0; x < w; x++) {
                            hist[dstp[x]] += 1;
                        }
                        for (x = 0; x < 256; x++) {
                            if (x < 16 || x == 124 || x > 235) {
                                dstp[x + w] = d->exptab[MIN(d->E167, hist[x])] + 68; // Magic numbers!
                            }
                            else {
                                dstp[x + w] = d->exptab[MIN(255, hist[x])];
                            }
                        }
                        dstp += dst_stride;
                    }
                }
                else {
                    const int subs = fi->subSamplingW;
                    const int factor = 1 << subs;

                    for (y = 0; y < h; y++) {
                        for (x = 0; x < 256; x += factor) {
                            if (x < 16 || x > 235) {
                                // Blue. Because I can.
                                dstp[(x >> subs) + w] = (plane == 1) ? 200 : 128;
                            }
                            else if (x == 124) {
                                dstp[(x >> subs) + w] = (plane == 1) ? 160 : 16;
                            }
                            else {
                                dstp[(x >> subs) + w] = 128;
                            }
                        }
                        dstp += dst_stride;
                    }
                }
            }
            else {
                uint16_t* dstp16 = (uint16_t*)dstp;
                // Now draw the histogram in the right side of dst.
                if (plane == 0) {
                    for (y = 0; y < h; y++) {
                        int hist[256] = { 0 };
                        for (x = 0; x < w; x++) {
                            // Add (1 << (bps - 8 - 1)) for rounding.
                            hist[(dstp16[x] + (1 << (bps - 8 - 1))) >> (bps - 8)] += 1;
                        }
                        for (x = 0; x < 256; x++) {
                            if (x < 16 || x == 124 || x > 235) {
                                dstp16[x + w] = d->exptab[MIN(d->E167, hist[x])] + 68; // Magic numbers!
                            }
                            else {
                                dstp16[x + w] = d->exptab[MIN(255, hist[x])];
                            }
                            dstp16[x + w] <<= (bps - 8);
                        }
                        dstp16 += dst_stride / 2;
                    }
                }
                else {
                    const int subs = fi->subSamplingW;
                    const int factor = 1 << subs;

                    for (y = 0; y < h; y++) {
                        for (x = 0; x < 256; x += factor) {
                            if (x < 16 || x > 235) {
                                // Blue. Because I can.
                                dstp16[(x >> subs) + w] = (plane == 1) ? 200 : 128;
                            }
                            else if (x == 124) {
                                dstp16[(x >> subs) + w] = (plane == 1) ? 160 : 16;
                            }
                            else {
                                dstp16[(x >> subs) + w] = 128;
                            }
                            dstp16[(x >> subs) + w] <<= (bps - 8);
                        }
                        dstp16 += dst_stride / 2;
                    }
                } // if plane
            } // if bps
        } // for plane

        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}


static void VS_CC classicFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ClassicData *d = (ClassicData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}


void VS_CC classicCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ClassicData d;
    ClassicData *data;

    const double K = log(0.5 / 219) / 255;

    d.exptab[0] = 16;
    int i;
    for (i = 1; i < 255; i++) {
        d.exptab[i] = (uint8_t)(16.5 + 219 * (1 - exp(i * K)));
        if (d.exptab[i] <= 235 - 68)
            d.E167 = i;
    }
    d.exptab[255] = 235;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!vsh_isConstantVideoFormat(&d.vi)
        || d.vi.format.sampleType != stInteger
        || d.vi.format.bitsPerSample > 16
        || d.vi.format.colorFamily != cfYUV) {
        vsapi->mapSetError(out, "Classic: only constant format 8 to 16 bit integer YUV input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi.width)
        d.vi.width += 256;
        
    data = (ClassicData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { {d.node, rpStrictSpatial} };
    vsapi->createVideoFilter(out, "Classic", &d.vi, classicGetFrame, classicFree, fmParallel, deps, 1, data, core);
}