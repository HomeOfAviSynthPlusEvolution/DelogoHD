#include "wrapper/avs_filter.hpp"
#include "version.hpp"
#include "delogohd_filter.hpp"

template <EOperation EOP>
AVSValue __cdecl CreateAVSFilter(AVSValue args, void* user_data, IScriptEnvironment* env)
{
  auto filter = new DelogoHDFilter<AVSFilter, EOP>(args, env);
  try {
    filter->initialize();
  }
  catch (const char *err) {
    env->ThrowError("%s %s: %s", filter->name(), PLUGIN_VERSION, err);
  }
  return filter;
}

const AVS_Linkage *AVS_linkage = NULL;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, AVS_Linkage* vectors)
{
  AVS_linkage = vectors;
  env->AddFunction("DelogoHD", "c[logofile]s[logoname]s[left]i[top]i[start]i[end]i[fadein]i[fadeout]i[mono]b[cutoff]i", CreateAVSFilter<ERASE_LOGO>, 0);
  env->AddFunction("AddlogoHD", "c[logofile]s[logoname]s[left]i[top]i[start]i[end]i[fadein]i[fadeout]i[mono]b[cutoff]i", CreateAVSFilter<ADD_LOGO>, 0);
  return "DelogoHD";
}
