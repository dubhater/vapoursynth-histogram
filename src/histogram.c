#include <VapourSynth4.h>

void VS_CC classicCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC levelsCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC colorCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC color2Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
void VS_CC lumaCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);


VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.nodame.histogram", "hist", "VapourSynth Histogram Plugin", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 1, plugin);
    vspapi->registerFunction("Classic", "clip:vnode;histonly:int:opt;", "clip:vnode;", classicCreate, NULL, plugin);
    vspapi->registerFunction("Levels", "clip:vnode;factor:float:opt;", "clip:vnode;", levelsCreate, NULL, plugin);
    vspapi->registerFunction("Color", "clip:vnode;", "clip:vnode;", colorCreate, NULL, plugin);
    vspapi->registerFunction("Color2", "clip:vnode;", "clip:vnode;", color2Create, NULL, plugin);
    vspapi->registerFunction("Luma", "clip:vnode;", "clip:vnode;", lumaCreate, NULL, plugin);
}
