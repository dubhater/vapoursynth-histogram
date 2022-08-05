#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <VapourSynth4.h>
#include "VSHelper4.h"

#include "common.h"

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    int histonly;

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
        int width = (d->histonly) ? vsapi->getFrameWidth(src, 0) : 
                                    vsapi->getFrameWidth(src, 0) + 256;

        
        VSFrame* dst = (d->histonly) ? vsapi->newVideoFrame(fi, d->vi.width, height, src, core) :
                                       vsapi->newVideoFrame(fi, width, height, src, core);

        int plane;
        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t* srcp = vsapi->getReadPtr(src, plane);
            int src_stride = vsapi->getStride(src, plane);
            uint8_t *dstp = vsapi->getWritePtr(dst, plane);
            int dst_stride = vsapi->getStride(dst, plane);
            int h = vsapi->getFrameHeight(src, plane);
            int y;
            int w = vsapi->getFrameWidth(src, plane);
            int x;

            const int draw_start_pt = (d->histonly) ? 0 : w; //starting draw point for histogram

            // Copy src to dst one line at a time. Applies only when histonly is off
            if (d->histonly < 1) {
                for (y = 0; y < h; y++) {
                    memcpy(dstp + dst_stride * y, srcp + src_stride * y, src_stride);
                }
            }                

            int bps = fi->bitsPerSample;
            if (bps == 8) {
                // Now draw the histogram in the right side of dst.
                if (plane == 0) {
                    for (y = 0; y < h; y++) {
                        int hist[256] = { 0 };
                        for (x = 0; x < w; x++) {
                            if (d->histonly)
                                hist[srcp[x]] += 1;
                            else
                                hist[dstp[x]] += 1;
                        }
                        for (x = 0; x < 256; x++) { // drawing start
                            if (x < 16 || x == 124 || x > 235) {
                                dstp[x + draw_start_pt] = d->exptab[MIN(d->E167, hist[x])] + 68; // Magic numbers!
                            }
                            else {
                                dstp[x + draw_start_pt] = d->exptab[MIN(255, hist[x])];
                            }
                        }
                        dstp += dst_stride;
                        if (d->histonly)
                            srcp += src_stride;
                    }
                }
                else {
                    const int subs = fi->subSamplingW;
                    const int factor = 1 << subs;

                    for (y = 0; y < h; y++) {
                        for (x = 0; x < 256; x += factor) {
                            if (x < 16 || x > 235) {
                                // Blue. Because I can.
                                dstp[(x >> subs) + draw_start_pt] = (plane == 1) ? 200 : 128;
                            }
                            else if (x == 124) {
                                dstp[(x >> subs) + draw_start_pt] = (plane == 1) ? 160 : 16;
                            }
                            else {
                                dstp[(x >> subs) + draw_start_pt] = 128;
                            }
                        }
                        dstp += dst_stride;
                    }
                }
            }
            else {
                uint16_t* dstp16 = (uint16_t*)dstp;
                uint16_t* srcp16 = (uint16_t*)srcp;
                // Now draw the histogram in the right side of dst.
                if (plane == 0) {
                    for (y = 0; y < h; y++) {
                        int hist[256] = { 0 };
                        for (x = 0; x < w; x++) {
                            // Add (1 << (bps - 8 - 1)) for rounding.                            
                            if (d->histonly)
                                hist[(srcp16[x] + (1 << (bps - 8 - 1))) >> (bps - 8)] += 1;
                            else
                                hist[(dstp16[x] + (1 << (bps - 8 - 1))) >> (bps - 8)] += 1;
                        }
                        for (x = 0; x < 256; x++) {
                            if (x < 16 || x == 124 || x > 235) {
                                dstp16[x + draw_start_pt] = d->exptab[MIN(d->E167, hist[x])] + 68; // Magic numbers!
                            }
                            else {
                                dstp16[x + draw_start_pt] = d->exptab[MIN(255, hist[x])];
                            }
                            dstp16[x + draw_start_pt] <<= (bps - 8);
                        }
                        dstp16 += dst_stride / 2;
                        if (d->histonly)
                            srcp16 += src_stride / 2;
                    }
                }
                else {
                    const int subs = fi->subSamplingW;
                    const int factor = 1 << subs;

                    for (y = 0; y < h; y++) {
                        for (x = 0; x < 256; x += factor) {
                            if (x < 16 || x > 235) {
                                // Blue. Because I can.
                                dstp16[(x >> subs) + draw_start_pt] = (plane == 1) ? 200 : 128;
                            }
                            else if (x == 124) {
                                dstp16[(x >> subs) + draw_start_pt] = (plane == 1) ? 160 : 16;
                            }
                            else {
                                dstp16[(x >> subs) + draw_start_pt] = 128;
                            }
                            dstp16[(x >> subs) + draw_start_pt] <<= (bps - 8);
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

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);

    const double K = log(0.5 / 219) / 255;
    d.exptab[0] = 16;
    int i;
    for (i = 1; i < 255; i++) {
        d.exptab[i] = (uint8_t)(16.5 + 219 * (1 - exp(i * K)));
        if (d.exptab[i] <= 235 - 68)
            d.E167 = i;
    }
    d.exptab[255] = 235;
    
    int err;
    d.histonly = vsapi->mapGetInt(in, "histonly", 0, &err);
    if (err)
        d.histonly = 0; // default to false

    d.vi = *vsapi->getVideoInfo(d.node); // video info of original

    if (!vsh_isConstantVideoFormat(&d.vi)
        || d.vi.format.sampleType != stInteger
        || d.vi.format.bitsPerSample > 16
        || d.vi.format.colorFamily != cfYUV) {
        vsapi->mapSetError(out, "Classic: only constant format 8 to 16 bit integer YUV input supported");
        vsapi->freeNode(d.node);
        return;
    }

    // rotate right
    VSPlugin* stdplugin = vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core);

    VSMap* tmpmap = vsapi->createMap();
    vsapi->mapConsumeNode(tmpmap, "clip", d.node, maReplace);
    VSMap* tmpmap2 = vsapi->invoke(stdplugin, "Transpose", tmpmap);
    vsapi->clearMap(tmpmap);
    tmpmap = vsapi->invoke(stdplugin, "FlipHorizontal", tmpmap2);
    vsapi->clearMap(tmpmap2);
    d.node = vsapi->mapGetNode(tmpmap, "clip", 0, 0);
    vsapi->clearMap(tmpmap);

    d.vi = *vsapi->getVideoInfo(d.node); // update video info after rotate

    d.vi.width = (d.histonly > 0) ? 256 : d.vi.width + 256; // set output width(height for final output)

    data = (ClassicData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { {d.node, rpStrictSpatial} };
    VSNode *node = vsapi->createVideoFilter2("Classic", &d.vi, classicGetFrame, classicFree, fmParallel, deps, 1, data, core);

    // rotate left
    vsapi->mapConsumeNode(tmpmap, "clip", node, maReplace);
    tmpmap2 = vsapi->invoke(stdplugin, "FlipHorizontal", tmpmap);
    vsapi->clearMap(tmpmap);
    tmpmap = vsapi->invoke(stdplugin, "Transpose", tmpmap2);
    vsapi->freeMap(tmpmap2);
    node = vsapi->mapGetNode(tmpmap, "clip", 0, 0);
    vsapi->freeMap(tmpmap);

    vsapi->mapSetNode(out, "clip", node, maAppend);
    vsapi->freeNode(node);
}