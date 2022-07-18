#include <stdlib.h>
#include <string.h>
#include <VapourSynth4.h>
#include <VSHelper4.h>

#include "common.h"

typedef struct {
    VSNode *node;
    VSVideoInfo vi;
    double factor;
} LevelsData;


static void drawYUV(uint8_t *dstp[3], const int src_width[3], const int src_height[3], const int dst_height[3], const int dst_stride[3], int hist[3][256], double factor, const VSVideoFormat *fi) {
    int x, y;

    // Start drawing.

    // Clear the luma.
    for (y = 0; y < dst_height[Y]; y++) {
        memset(dstp[Y] + y * dst_stride[Y] + src_width[Y], 0, 256);
    }

    // Draw the background of the unsafe zones (0-15, 236-255) in the luma graph.
    for (y = 0; y <= 64; y++) {
        for (x = 0; x < 16; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 32;
        }
        for (x = 236; x < 256; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 32;
        }
    }

    // Draw unsafe zones and gradient for U graph. More magic numbers.
    // Original comment: // x=0-16, R=G=255, B=0; x=128, R=G=B=0; x=240-255, R=G=0, B=255
    // wtf does it mean?
    for (y = 64 + 16; y <= 128 + 16; y++) {
        // I wonder if it would be faster to do this shit for one line
        // and just copy it 63 times.
        for (x = 0; x < 15; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 210 / 2;
        }
        for (/*x = 15*/; x <= 128; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((128 - x) * 15) >> 3; // *1.875 // wtf is this?
        }
        for (/*x = 129*/; x <= 240; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((x - 128) * 24001) >> 16; // *0.366 // and this?
        }
        for (/*x = 241*/; x < 256; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 41 / 2;
        }
    }

    // Draw unsafe zones and gradient for V graph.
    // Original comment: // x=0-16, R=0, G=B=255; x=128, R=G=B=0; x=240-255, R=255, G=B=0
    for (y = 128 + 32; y <= 128 + 64 + 32; y++) {
        for (x = 0; x < 15; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 170 / 2;
        }
        for (/*x = 15*/; x <= 128; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((128 - x) * 99515) >> 16; // *1.518
        }
        for (/*x = 129*/; x <= 240; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((x - 128) * 47397) >> 16; // *0.723
        }
        for (/*x = 241*/; x < 256; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 81 / 2;
        }
    }

    // Draw dotted line in the center.
    for (y = 0; y <= 256 - 32; y++) {
        if ((y & 3) > 1) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + 128] = 128;
        }
    }

    // Finally draw the actual histograms, starting with the luma.
    const int clampval = (int)((src_width[Y] * src_height[Y]) * factor / 100.0);
    int maxval = 0;
    for (int i = 0; i < 256; i++) {
        if (hist[Y][i] > clampval) {
            hist[Y][i] = clampval;
        }
        maxval = MAX(hist[Y][i], maxval);
    }

    float scale = 64.0f / maxval; // Why float?

    for (x = 0; x < 256; x++) {
        float scaled_h = (float)hist[Y][x] * scale;
        int h = 64 - MIN((int)scaled_h, 64) + 1;

        for (y = 64 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
        }
        dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16;
    }

    if (fi->colorFamily == cfGray)
        return;

    // Draw the histogram of the U plane.
    const int clampvalUV = (int)((src_width[U] * src_height[U]) * factor / 100.0);

    maxval = 0;
    for (int i = 0; i < 256; i++) {
        if (hist[U][i] > clampvalUV) {
            hist[U][i] = clampvalUV;
        }
        maxval = MAX(hist[U][i], maxval);
    }

    scale = 64.0f / maxval;

    for (x = 0; x < 256; x++) {
        float scaled_h = (float)hist[U][x] * scale;
        int h = 128 + 16 - MIN((int)scaled_h, 64) + 1;

        for (y = 128 + 16 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
        }
        dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16;
    }

    // Draw the histogram of the V plane.
    maxval = 0;
    for (int i = 0; i < 256; i++) {
        if (hist[V][i] > clampvalUV) {
            hist[V][i] = clampvalUV;
        }
        maxval = MAX(hist[V][i], maxval);
    }

    scale = 64.0f / maxval;

    for (x = 0; x < 256; x++) {
        float scaled_h = (float)hist[V][x] * scale;
        int h = 192 + 32 - MIN((int)scaled_h, 64) + 1;

        for (y = 192 + 32 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
        }
        dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16;
    }


    // Draw the chroma.
    int subW = fi->subSamplingW;
    int subH = fi->subSamplingH;

    // Clear the chroma first.
    for (y = 0; y < dst_height[U]; y++) {
        memset(dstp[U] + src_width[U] + y * dst_stride[U], 128, 256 >> subW);
        memset(dstp[V] + src_width[V] + y * dst_stride[V], 128, 256 >> subW);
    }

    // Draw unsafe zones in the luma graph.
    for (y = 0; y <= (64 >> subH); y++) {
        for (x = 0; x < (16 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = 16;
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 160;
        }
        for (x = (236 >> subW); x < (256 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = 16;
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 160;
        }
    }

    // Draw unsafe zones and gradient for U graph.
    for (y = ((64 + 16) >> subH); y <= ((128 + 16) >> subH); y++) {
        for (x = 0; x < (16 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = 16 + 112 / 2;
        }
        for (; x <= (240 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = x << subW;
        }
        for (; x < (256 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = 240 - 112 / 2;
        }
    }

    // Draw unsafe zones and gradient for V graph.
    for (y = ((128 + 32) >> subH); y <= ((128 + 64 + 32) >> subH); y++) {
        for (x = 0; x < (16 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 16 + 112 / 2;
        }
        for (; x <= (240 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = x << subW;
        }
        for (; x < (256 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 240 - 112 / 2;
        }
    }
}


static void drawRGB(uint8_t *dstp[3], const int src_width[3], const int src_height[3], const int dst_height[3], const int dst_stride[3], int hist[3][256], double factor, const VSVideoFormat *fi) {
    const int clampval = (int)((src_width[0] * src_height[0]) * factor / 100.0);

    for (int plane = 0; plane < 3; plane++) {
        for (int y = (64 + 16) * plane; y < (64 + 16) * plane + 64; y++) {
            // Draw this channel's gradient.
            for (int x = 0; x < 256; x++)
                dstp[plane][y * dst_stride[plane] + src_width[plane] + x] = x;

            // Zero the other two channels.
            for (int i = 0; i < 3; i++)
                if (i != plane)
                    memset(dstp[i] + y * dst_stride[i] + src_width[i], 0, 256);
        }
    }

    for (int plane = 0; plane < 3; plane++) {
        // Zero the 16 pixels in between the histograms.
        for (int y = 64; y < 64 + 16; y++)
            memset(dstp[plane] + y * dst_stride[plane] + src_width[plane], 0, 256);

        for (int y = 64 + 16 + 64; y < 64 + 16 + 64 + 16; y++)
            memset(dstp[plane] + y * dst_stride[plane] + src_width[plane], 0, 256);

        // Draw the dotted line in the middle.
        for (int y = 0; y < 64 * 3 + 16 * 2; y++)
            if ((y & 3) > 1)
                dstp[plane][y * dst_stride[plane] + src_width[plane] + 128] = 255;

        // Zero the space below the histograms.
        for (int y = 64 * 3 + 16 * 2; y < dst_height[plane]; y++)
            memset(dstp[plane] + y * dst_stride[plane] + src_width[plane], 0, 256);
    }

    for (int plane = 0; plane < 3; plane++) {
        // Draw the histogram.
        int maxval = 0;
        for (int i = 0; i < 256; i++) {
            if (hist[plane][i] > clampval)
                hist[plane][i] = clampval;

            maxval = MAX(hist[plane][i], maxval);
        }

        float scale = 64.0f / maxval; // Why float?

        for (int x = 0; x < 256; x++) {
            float scaled_h = hist[plane][x] * scale;
            int h = 64 - MIN((int)scaled_h, 64);

            for (int y = (64 + 16) * plane + 64 - 1; y >= (64 + 16) * plane + h; y--)
                for (int i = 0; i < 3; i++)
                    dstp[i][src_width[i] + y * dst_stride[i] + x] = 255;
        }
    }
}


static const VSFrame *VS_CC levelsGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = (LevelsData *) instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSVideoFormat *fi = &d->vi.format;
        int height = MAX(256, vsapi->getFrameHeight(src, 0));
        int width = vsapi->getFrameWidth(src, 0) + 256;

        VSFrame *dst = vsapi->newVideoFrame(fi, width, height, src, core);

        const uint8_t *srcp[3];
        int src_stride[3];

        uint8_t *dstp[3];
        int dst_stride[3];

        int src_height[3];
        int src_width[3];

        int dst_height[3];

        int y;
        int x;

        int plane;

        // This better be the right way to get an array of 3 arrays of 256 ints each...
        // each array with its elements initialised to 0.
        int hist[3][256] = { {0}, {0}, {0} };

        for (plane = 0; plane < fi->numPlanes; plane++) {
            srcp[plane] = vsapi->getReadPtr(src, plane);
            src_stride[plane] = vsapi->getStride(src, plane);

            dstp[plane] = vsapi->getWritePtr(dst, plane);
            dst_stride[plane] = vsapi->getStride(dst, plane);

            src_height[plane] = vsapi->getFrameHeight(src, plane);
            src_width[plane] = vsapi->getFrameWidth(src, plane);

            dst_height[plane] = vsapi->getFrameHeight(dst, plane);

            // Copy src to dst one line at a time.
            for (y = 0; y < src_height[plane]; y++) {
                memcpy(dstp[plane] + dst_stride[plane] * y,
                    srcp[plane] + src_stride[plane] * y,
                    src_stride[plane]);
            }

            // If src was less than 256 px tall, make the extra lines black.
            if (src_height[plane] < dst_height[plane]) {
                memset(dstp[plane] + src_height[plane] * dst_stride[plane],
                    (plane == 0 || fi->colorFamily == cfRGB) ? 0 : 128,
                    (dst_height[plane] - src_height[plane]) * dst_stride[plane]);
            }

            // Fill the hist arrays.
            for (y = 0; y < src_height[plane]; y++) {
                for (x = 0; x < src_width[plane]; x++) {
                    hist[plane][srcp[plane][y * src_stride[plane] + x]]++;
                }
            }
        }

        (fi->colorFamily == cfRGB ? drawRGB : drawYUV)(dstp, src_width, src_height, dst_height, dst_stride, hist, d->factor, fi);

        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}


static void VS_CC levelsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    LevelsData *d = (LevelsData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}


void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    LevelsData d;
    LevelsData *data;
    int err;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);

    d.factor = vsapi->mapGetFloat(in, "factor", 0, &err);
    if (err) {
        d.factor = 100.0;
    }

    // Comparing them directly?
    if (d.factor < 0.0 || d.factor > 100.0) {
        vsapi->mapSetError(out, "Levels: factor must be between 0 and 100 (inclusive)");
        vsapi->freeNode(d.node);
        return;
    }

    if (!vsh_isConstantVideoFormat(&d.vi) || d.vi.format.sampleType != stInteger || d.vi.format.bitsPerSample != 8) {
        vsapi->mapSetError(out, "Levels: only constant format 8bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi.width)
        d.vi.width += 256;
    if (d.vi.height)
        d.vi.height = MAX(256, d.vi.height);

    data = (LevelsData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { {d.node, rpStrictSpatial} };
    vsapi->createVideoFilter(out, "Levels", &d.vi, levelsGetFrame, levelsFree, fmParallel, deps, 1, data, core);
}

