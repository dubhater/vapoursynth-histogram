#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "VapourSynth.h"


#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))


// The classic mode

typedef struct {
   const VSNodeRef *node;
   VSVideoInfo vi;

   int E167;
   uint8_t exptab[256];
} ClassicData;


static void VS_CC classicInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
   ClassicData *d = (ClassicData *) * instanceData;
   vsapi->setVideoInfo(&d->vi, node);

   const double K = log(0.5/219)/255;

   d->exptab[0] = 16;
   int i;
   for (i = 1; i < 255; i++) {
      d->exptab[i] = (uint8_t)(16.5 + 219 * (1 - exp(i * K)));
      if (d->exptab[i] <= 235-68)
         d->E167 = i;
   }
   d->exptab[255] = 235;
}


static const VSFrameRef *VS_CC classicGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
   ClassicData *d = (ClassicData *) * instanceData;

   if (activationReason == arInitial) {
      vsapi->requestFrameFilter(n, d->node, frameCtx);
   } else if (activationReason == arAllFramesReady) {
      const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

      const VSFormat *fi = d->vi.format;
      int height = d->vi.height;
      int width = d->vi.width;

      // When creating a new frame for output it is VERY EXTREMELY SUPER IMPORTANT to
      // supply the "domainant" source frame to copy properties from. Frame props
      // are an essential part of the filter chain and you should NEVER break it.
      VSFrameRef *dst = vsapi->newVideoFrame(fi, width, height, src, core);

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

         // Now draw the histogram in the right side of dst.
         if (plane == 0) {
            for (y = 0; y < h; y++) {
               int hist[256] = {0};
               for (x = 0; x < w; x++) {
                  hist[dstp[x]] += 1;
               }
               for (x = 0; x < 256; x++) {
                  if (x < 16 || x == 124 || x > 235) {
                     dstp[x + w] = d->exptab[MIN(d->E167, hist[x])] + 68; // Magic numbers!
                  } else {
                     dstp[x + w] = d->exptab[MIN(255, hist[x])];
                  }
               }
               dstp += dst_stride;
            }
         } else {
            const int subs = fi->subSamplingW;
            const int factor = 1 << subs;

            for (y = 0; y < h; y++) {
               for (x = 0; x < 256; x += factor) {
                  if (x < 16 || x > 235) {
                     // Blue. Because I can.
                     dstp[(x >> subs) + w] = (plane == 1) ? 200 : 128;
                  } else if (x == 124) {
                     dstp[(x >> subs) + w] = (plane == 1) ? 160 : 16;
                  } else {
                     dstp[(x >> subs) + w] = 128;
                  }
               }
               dstp += dst_stride;
            }
         }
      }

      // Release the source frame
      vsapi->freeFrame(src);

      // A reference is consumed when it is returned so saving the dst ref somewhere
      // and reusing it is not allowed.
      return dst;
   }

   return 0;
}


static void VS_CC classicFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
   ClassicData *d = (ClassicData *)instanceData;
   vsapi->freeNode(d->node);
   free(d);
}


static void VS_CC classicCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
   ClassicData d;
   ClassicData *data;
   const VSNodeRef *cref;
   int err;

   d.node = vsapi->propGetNode(in, "clip", 0, 0);
   d.vi = *vsapi->getVideoInfo(d.node);

   // In this first version we only want to handle 8bit integer formats. Note that
   // vi->format can be 0 if the input clip can change format midstream.
   if (!d.vi.format || d.vi.format->sampleType != stInteger || d.vi.format->bitsPerSample != 8) {
      vsapi->setError(out, "Classic: only constant format 8bit integer input supported");
      vsapi->freeNode(d.node);
      return;
   }

   d.vi.width += 256;

   data = malloc(sizeof(d));
   *data = d;

   cref = vsapi->createFilter(in, out, "Classic", classicInit, classicGetFrame, classicFree, fmParallel, 0, data, core);
   vsapi->propSetNode(out, "clip", cref, 0);
   vsapi->freeNode(cref);
   return;
}


// The levels mode

typedef struct {
   const VSNodeRef *node;
   VSVideoInfo vi;
   double factor;
} LevelsData;


static void VS_CC levelsInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
   LevelsData *d = (LevelsData *) * instanceData;
   vsapi->setVideoInfo(&d->vi, node);
}


static const VSFrameRef *VS_CC levelsGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
   LevelsData *d = (LevelsData *) * instanceData;

   if (activationReason == arInitial) {
      vsapi->requestFrameFilter(n, d->node, frameCtx);
   } else if (activationReason == arAllFramesReady) {
      const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

      const VSFormat *fi = d->vi.format;
      int height = d->vi.height;
      int width = d->vi.width;

      // When creating a new frame for output it is VERY EXTREMELY SUPER IMPORTANT to
      // supply the "domainant" source frame to copy properties from. Frame props
      // are an essential part of the filter chain and you should NEVER break it.
      VSFrameRef *dst = vsapi->newVideoFrame(fi, width, height, src, core);


      const uint8_t *srcp[fi->numPlanes];
      int src_stride[fi->numPlanes];

      uint8_t *dstp[fi->numPlanes];
      int dst_stride[fi->numPlanes];

      int src_height[fi->numPlanes];
      int src_width[fi->numPlanes];

      int dst_height[fi->numPlanes];
      int dst_width[fi->numPlanes];

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
         dst_width[plane] = vsapi->getFrameWidth(dst, plane);

         // Copy src to dst one line at a time.
         for (y = 0; y < src_height[plane]; y++) {
            memcpy(dstp[plane] + dst_stride[plane] * y,
                   srcp[plane] + src_stride[plane] * y,
                   src_stride[plane]);
         }

         // If src was less than 256 px tall, make the extra lines black.
         if (src_height[plane] < dst_height[plane]) {
            memset(dstp[plane] + src_height[plane] * dst_stride[plane],
                   (plane == 0) ? 16 : 128,
                   (dst_height[plane] - src_height[plane]) * dst_stride[plane]);
         }

         // Fill the hist arrays.
         for (y = 0; y < src_height[plane]; y++) {
            for (x = 0; x < src_width[plane]; x++) {
               hist[plane][srcp[plane][y * src_stride[plane] + x]]++;
            }
         }
      }

      enum yuv_planes {
         Y = 0,
         U,
         V
      };

      // Start drawing.

      // Clear the luma.
      for (y = 0; y < dst_height[Y]; y++) {
         memset(dstp[Y] + y * dst_stride[Y] + src_width[Y], 16, 256);
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
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 210/2;
         }
         for (/*x = 15*/; x <= 128; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((128 - x) * 15) >> 3; // *1.875 // wtf is this?
         }
         for (/*x = 129*/; x <= 240; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((x - 128) * 24001) >> 16; // *0.366 // and this?
         }
         for (/*x = 241*/; x < 256; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 41/2;
         }
      }

      // Draw unsafe zones and gradient for V graph.
      // Original comment: // x=0-16, R=0, G=B=255; x=128, R=G=B=0; x=240-255, R=255, G=B=0
      for (y = 128 + 32; y <= 128 + 64 + 32; y++) {
         for (x = 0; x < 15; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 170/2;
         }
         for (/*x = 15*/; x <= 128; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((128 - x) * 99515) >> 16; // *1.518
         }
         for (/*x = 129*/; x <= 240; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = ((x - 128) * 47397) >> 16; // *0.723
         }
         for (/*x = 241*/; x < 256; x++) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 81/2;
         }
      }

      // Draw dotted line in the center.
      for (y = 0; y <= 256-32; y++) {
         if ((y & 3) > 1) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + 128] = 128;
         }
      }

      // Finally draw the actual histograms, starting with the luma.
      const int clampval = (int)((src_width[Y] * src_height[Y]) * d->factor / 100.0);
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
         int left = (int)(220.0f * (scaled_h - (float)((int)scaled_h)));

         for (y = 64 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
         }
         dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16 + left;
      }

      // Draw the histogram of the U plane.
      const int clampvalUV = (int)((src_width[U] * src_height[U]) * d->factor / 100.0);

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
         int left = (int)(220.0f * (scaled_h - (float)((int)scaled_h)));

         for (y = 128 + 16 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
         }
         dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16 + left;
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
         int left = (int)(220.0f * ((int)scaled_h - scaled_h));

         for (y = 192 + 32 + 1; y > h; y--) {
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = 235;
         }
         dstp[Y][src_width[Y] + h * dst_stride[Y] + x] = 16 + left;
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
         for ( ; x <= (240 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = x << subW;
         }
         for ( ; x < (256 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = 240 - 112 / 2;
         }
      }

      // Draw unsafe zones and gradient for V graph.
      for (y = ((128 + 32) >> subH); y <= ((128 + 64 + 32) >> subH); y++) {
         for (x = 0; x < (16 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 16 + 112 / 2;
         }
         for ( ; x <= (240 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = x << subW;
         }
         for ( ; x < (256 >> subW); x++) {
            dstp[V][src_width[V] + y * dst_stride[V] + x] = 240 - 112 / 2;
         }
      }


      // Release the source frame
      vsapi->freeFrame(src);

      // A reference is consumed when it is returned so saving the dst ref somewhere
      // and reusing it is not allowed.
      return dst;
   }

   return 0;
}


static void VS_CC levelsFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
   LevelsData *d = (LevelsData *)instanceData;
   vsapi->freeNode(d->node);
   free(d);
}


static void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
   LevelsData d;
   LevelsData *data;
   const VSNodeRef *cref;
   int err;

   d.node = vsapi->propGetNode(in, "clip", 0, 0);
   d.vi = *vsapi->getVideoInfo(d.node);

   d.factor = vsapi->propGetFloat(in, "factor", 0, &err);
   if (err) {
      d.factor = 100.0;
   }

   // Comparing them directly?
   if (d.factor < 0.0 || d.factor > 100.0) {
      vsapi->setError(out, "Levels: factor must be between 0 and 100 (inclusive)");
      vsapi->freeNode(d.node);
      return;
   }

   // In this first version we only want to handle 8bit integer formats. Note that
   // vi->format can be 0 if the input clip can change format midstream.
   if (!d.vi.format || d.vi.format->sampleType != stInteger || d.vi.format->bitsPerSample != 8) {
      vsapi->setError(out, "Levels: only constant format 8bit integer input supported");
      vsapi->freeNode(d.node);
      return;
   }

   d.vi.width += 256;
   d.vi.height = MAX(256, d.vi.height);

   data = malloc(sizeof(d));
   *data = d;

   cref = vsapi->createFilter(in, out, "Levels", levelsInit, levelsGetFrame, levelsFree, fmParallel, 0, data, core);
   vsapi->propSetNode(out, "clip", cref, 0);
   vsapi->freeNode(cref);
   return;
}


// The color mode

typedef struct {
   const VSNodeRef *node;
   VSVideoInfo vi;
} ColorData;


static void VS_CC colorInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
   ColorData *d = (ColorData *) * instanceData;
   vsapi->setVideoInfo(&d->vi, node);
}


static const VSFrameRef *VS_CC colorGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
   ColorData *d = (ColorData *) * instanceData;

   if (activationReason == arInitial) {
      vsapi->requestFrameFilter(n, d->node, frameCtx);
   } else if (activationReason == arAllFramesReady) {
      const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

      const VSFormat *fi = d->vi.format;
      int height = d->vi.height;
      int width = d->vi.width;

      // When creating a new frame for output it is VERY EXTREMELY SUPER IMPORTANT to
      // supply the "domainant" source frame to copy properties from. Frame props
      // are an essential part of the filter chain and you should NEVER break it.
      VSFrameRef *dst = vsapi->newVideoFrame(fi, width, height, src, core);


      const uint8_t *srcp[fi->numPlanes];
      int src_stride[fi->numPlanes];

      uint8_t *dstp[fi->numPlanes];
      int dst_stride[fi->numPlanes];

      int src_height[fi->numPlanes];
      int src_width[fi->numPlanes];

      int dst_height[fi->numPlanes];
      int dst_width[fi->numPlanes];

      int y;
      int x;

      int plane;

      for (plane = 0; plane < fi->numPlanes; plane++) {
         srcp[plane] = vsapi->getReadPtr(src, plane);
         src_stride[plane] = vsapi->getStride(src, plane);

         dstp[plane] = vsapi->getWritePtr(dst, plane);
         dst_stride[plane] = vsapi->getStride(dst, plane);

         src_height[plane] = vsapi->getFrameHeight(src, plane);
         src_width[plane] = vsapi->getFrameWidth(src, plane);

         dst_height[plane] = vsapi->getFrameHeight(dst, plane);
         dst_width[plane] = vsapi->getFrameWidth(dst, plane);

         // Copy src to dst one line at a time.
         for (y = 0; y < src_height[plane]; y++) {
            memcpy(dstp[plane] + dst_stride[plane] * y,
                   srcp[plane] + src_stride[plane] * y,
                   src_stride[plane]);
         }

         // If src was less than 256 px tall, make the extra lines black.
         if (src_height[plane] < dst_height[plane]) {
            memset(dstp[plane] + src_height[plane] * dst_stride[plane],
                   (plane == 0) ? 16 : 128,
                   (dst_height[plane] - src_height[plane]) * dst_stride[plane]);
         }
      }

      // FIXME: move this out of the function.
      enum yuv_planes {
         Y = 0,
         U,
         V
      };

      // Why not histUV[256][256] ?
      int histUV[256*256] = {0};

      for (y = 0; y < src_height[U]; y++) {
         for (x = 0; x < src_width[U]; x++) {
            histUV[srcp[V][y * src_stride[V] + x] * 256 + srcp[U][y * src_stride[U] + x]]++;
         }
      }

      int maxval = 1;

      // Original comment: // Should we adjust the divisor (maxval)??

      // Draw the luma.
      for (y = 0; y < 256; y++) {
         for (x = 0; x < 256; x++) {
            int disp_val = histUV[x + y *256] / maxval;
            if (y < 16 || y > 240 || x < 16 || x > 240) {
               disp_val -= 16;
            }
            dstp[Y][src_width[Y] + y * dst_stride[Y] + x] = MIN(235, 16 + disp_val);
         }
      }

      int subW = fi->subSamplingW;
      int subH = fi->subSamplingH;

      // Draw the chroma.
      for (y = 0; y < (256 >> subH); y++) {
         for (x = 0; x < (256 >> subW); x++) {
            dstp[U][src_width[U] + y * dst_stride[U] + x] = x << subW;
            dstp[V][src_width[V] + y * dst_stride[V] + x] = y << subH;
         }
      }

      // Clear the luma under the histogram.
      for (y = 256; y < dst_height[Y]; y++) {
         memset(dstp[Y] + src_width[Y] + y * dst_stride[Y], 16, 256);
      }

      // Clear the chroma under the histogram.
      for (y = (256 >> subH); y < dst_height[U]; y++) {
         // The third argument was originally "(256 >> subW) - 1",
         // leaving the last column uninitialised. (Why?)
         memset(dstp[U] + src_width[U] + y * dst_stride[U], 128, 256 >> subW);
         memset(dstp[V] + src_width[V] + y * dst_stride[V], 128, 256 >> subW);
      }


      // Release the source frame
      vsapi->freeFrame(src);

      // A reference is consumed when it is returned so saving the dst ref somewhere
      // and reusing it is not allowed.
      return dst;
   }

   return 0;
}


static void VS_CC colorFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
   ColorData *d = (ColorData *)instanceData;
   vsapi->freeNode(d->node);
   free(d);
}


static void VS_CC colorCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
   ColorData d;
   ColorData *data;
   const VSNodeRef *cref;

   d.node = vsapi->propGetNode(in, "clip", 0, 0);
   d.vi = *vsapi->getVideoInfo(d.node);


   // In this first version we only want to handle 8bit integer formats. Note that
   // vi->format can be 0 if the input clip can change format midstream.
   if (!d.vi.format || d.vi.format->sampleType != stInteger || d.vi.format->bitsPerSample != 8) {
      vsapi->setError(out, "Color: only constant format 8bit integer input supported");
      vsapi->freeNode(d.node);
      return;
   }

   d.vi.width += 256;
   d.vi.height = MAX(256, d.vi.height);

   data = malloc(sizeof(d));
   *data = d;

   cref = vsapi->createFilter(in, out, "Color", colorInit, colorGetFrame, colorFree, fmParallel, 0, data, core);
   vsapi->propSetNode(out, "clip", cref, 0);
   vsapi->freeNode(cref);
   return;
}



void VS_CC VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
   configFunc("com.nodame.histogram", "hist", "VapourSynth Histogram Plugin", VAPOURSYNTH_API_VERSION, 1, plugin);
   registerFunc("Classic", "clip:clip;", classicCreate, 0, plugin);
   registerFunc("Levels", "clip:clip;factor:float:opt;", levelsCreate, 0, plugin);
   registerFunc("Color", "clip:clip;", colorCreate, 0, plugin);
}
