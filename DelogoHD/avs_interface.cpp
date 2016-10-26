#include "wrapper/avs_filter.hpp"
#include "delogohd_filter.hpp"

AVSValue __cdecl CreateAVSFilter(AVSValue args, void* user_data, IScriptEnvironment* env)
{
  auto filter = new DelogoHDFilter<AVSFilter>(args, env);
  try {
    filter->initialize();
  }
  catch (const char *err) {
    env->ThrowError("%s: %s", filter->name(), err);
  }
  return filter;
}

const AVS_Linkage *AVS_linkage = NULL;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, AVS_Linkage* vectors)
{
  AVS_linkage = vectors;
  env->AddFunction("DelogoHD", "c[logofile]s[logoname]s[left]i[top]i[start]i[fadein]i[fadeout]i[end]i[mono]b[cutoff]i[depth]i", CreateAVSFilter, 0);
  return "DelogoHD";
}
