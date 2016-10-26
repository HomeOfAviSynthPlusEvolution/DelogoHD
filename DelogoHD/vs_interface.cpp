#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "version.hpp"
#include "wrapper/vs_filter.hpp"
#include "delogohd_filter.hpp"

void VS_CC
logoInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
  VSFilter *d = *reinterpret_cast<VSFilter**>(instanceData);
  vsapi->setVideoInfo(&d->vi, 1, node);
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

static void VS_CC
logoCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
  // int err;

  // VSNodeRef * node = vsapi->propGetNode(in, "clip", 0, 0);
  // VSVideoInfo * vi = new VSVideoInfo;
  // *vi = *vsapi->getVideoInfo(node);

  // FAIL_IF_ERROR(!vi->format || vi->width == 0 || vi->height == 0,
  //   "clip must be constant format");

  // FAIL_IF_ERROR(vi->format->sampleType != stInteger ||
  //   vi->format->bitsPerSample != 8 ||
  //   vi->format->colorFamily != cmYUV,
  //   "only YUV420P8 input supported. You can you up.");

  DelogoHDFilter<VSFilter> *data;

  try {
    data = new DelogoHDFilter<VSFilter>(in, out, core, vsapi);
    vsapi->createFilter(in, out, "DelogoHD", logoInit, logoGetFrame, logoFree, fmParallel, 0, data, core);
  } catch (const char *) {
  }
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
  configFunc("in.7086.delogohd", "delogohd",
    "VapourSynth DelogoHD Filter " PLUGIN_VERSION,
    VAPOURSYNTH_API_VERSION, 1, plugin);
  const char * options = "clip:clip;logofile:data;logoname:data:opt;left:int:opt;top:int:opt;start:int:opt;end:int:opt;fadein:int:opt;fadeout:int:opt;";
  registerFunc("DelogoHD", options, logoCreate, nullptr, plugin);
}
