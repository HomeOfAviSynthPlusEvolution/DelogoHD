#include "wrapper/vs_filter.hpp"
#include "version.hpp"
#include "delogohd_filter.hpp"

void VS_CC
logoInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
  VSFilter *d = *reinterpret_cast<VSFilter**>(instanceData);
  vsapi->setVideoInfo(&d->vi._vi, 1, node);
}

void VS_CC
logoFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
  VSFilter *d = static_cast<VSFilter*>(instanceData);
  vsapi->freeNode(d->child->_clip);
  delete d;
}

const VSFrameRef *VS_CC
logoGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
  VSFilter *d = *reinterpret_cast<VSFilter**>(instanceData);
  if (activationReason == arInitial) {
    d->GetFramePre(frameCtx, core, vsapi, n);
    return nullptr;
  }
  if (activationReason != arAllFramesReady)
    return nullptr;

  return d->GetFrame(frameCtx, core, vsapi, n);
}

template <EOperation EOP>
static void VS_CC
logoCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
  DelogoHDFilter<VSFilter, EOP> *data = nullptr;

  try {
    data = new DelogoHDFilter<VSFilter, EOP>(in, out, core, vsapi);
    data->initialize();
    vsapi->createFilter(in, out, "DelogoHD", logoInit, logoGetFrame, logoFree, fmParallel, 0, data, core);
  }
  catch(const char *err){
    char msg_buff[256];
    snprintf(msg_buff, 256, "%s(" PLUGIN_VERSION "): %s", data ? data->name() : "DelogoHD", err);
    vsapi->setError(out, msg_buff);
  }
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
  configFunc("in.7086.delogohd", "delogohd",
    "VapourSynth DelogoHD Filter " PLUGIN_VERSION,
    VAPOURSYNTH_API_VERSION, 1, plugin);
  const char * options =
    "clip:clip;"
    "logofile:data;"
    "logoname:data:opt;"
    "left:int:opt;"
    "top:int:opt;"
    "start:int:opt;"
    "end:int:opt;"
    "fadein:int:opt;"
    "fadeout:int:opt;"
    "mono:int:opt;"
    "cutoff:int:opt;"
    ;
  registerFunc("DelogoHD", options, logoCreate<ERASE_LOGO>, nullptr, plugin);
  registerFunc("AddlogoHD", options, logoCreate<ADD_LOGO>, nullptr, plugin);
}
