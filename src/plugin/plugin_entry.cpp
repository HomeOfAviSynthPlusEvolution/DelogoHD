#include <avisynth.h>
#include <VapourSynth4.h>

#include <dualsynth/avisynth/video_bridge.hpp>
#include <dualsynth/vapoursynth/video_bridge.hpp>
#include <dualsynth/video_bridge.hpp>

#include "plugin/delogohd_filter.hpp"

#include <array>
#include <exception>
#include <string>
#include <utility>

#if defined(_WIN32)
#define DELOGOHD_AVS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#elif defined(__clang__) || defined(__GNUC__)
#define DELOGOHD_AVS_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define DELOGOHD_AVS_PLUGIN_EXPORT extern "C"
#endif

const AVS_Linkage* AVS_linkage = nullptr;

namespace {

bool set_avisynth_host_var(void* user, const char* name, const ds::ParamValue& value) {
  if (!user || !name) {
    return false;
  }

  auto* env = static_cast<IScriptEnvironment*>(user);
  AVSValue avs_value;
  if (!ds::avisynth::assign_avisynth_value(env, value, avs_value)) {
    return false;
  }

  env->SetVar(env->SaveString(name), avs_value);
  return true;
}

ds::HostVariableCallbacks avisynth_host_variable_callbacks(IScriptEnvironment* env) {
  if (!env) {
    return {};
  }
  return ds::HostVariableCallbacks{.user = env, .set = &set_avisynth_host_var};
}

template <class Bridge>
const char* avs_signature() {
  static const std::string signature = [] {
    const auto generated = ds::make_avisynth_signature(Bridge::descriptor());
    if (!generated.has_value()) {
      return std::string{"c[logofile]s[logoname]s[left]i[top]i[start]i[end]i[fadein]i[fadeout]i[mono]b[cutoff]i[opt]i"};
    }
    return generated.value();
  }();
  return signature.c_str();
}

const char* vs_signature() {
  return
    "clip:vnode;"
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
    "opt:int:opt;";
}

template <class Bridge>
void VS_CC create_vapoursynth_filter(
  const VSMap* in,
  VSMap* out,
  void*,
  VSCore* core,
  const VSAPI* vsapi
) {
  ds::vapoursynth::create_video_filter_bridge<Bridge>(in, out, core, vsapi);
}

template <class Bridge>
class AvisynthCompatVideoFilter final : public IClip {
public:
  using Filter = typename Bridge::Core;

  AvisynthCompatVideoFilter(
    const PClip& clip,
    ds::VideoInputInfo input_info,
    ds::VideoFilterState<Filter> state,
    ds::avisynth::MtMode mt_mode
  ) : clip_(clip),
      input_info_(input_info),
      state_(std::move(state)),
      mt_mode_(mt_mode),
      vi_(clip_->GetVideoInfo()) {}

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    try {
      PVideoFrame dst = new_output_frame(n, env);
      const auto frame_view = ds::avisynth::make_video_frame_view(dst, input_info_.format);
      std::array<ds::RequestedVideoFrame, 1> frames{
        ds::RequestedVideoFrame{.input_index = 0, .frame_number = n, .frame = frame_view}
      };
      ds::RequestedVideoFrameProvider provider(frames);

      const auto result = ds::process_video_filter<Filter>(
        n,
        provider,
        ds::avisynth::make_mutable_video_frame_view(dst, input_info_.format),
        state_
      );
      if (!result.has_value()) {
        env->ThrowError(result.error().message.c_str());
      }
      return dst;
    } catch (const AvisynthError&) {
      throw;
    } catch (const std::exception& error) {
      env->ThrowError(error.what());
    } catch (...) {
      env->ThrowError("DelogoHD: unhandled exception in AviSynth video wrapper");
    }
    return {};
  }

  bool __stdcall GetParity(int n) override {
    return clip_->GetParity(n);
  }

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {
    clip_->GetAudio(buf, start, count, env);
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    return ds::cache_hints_video_filter<Filter>(
      cachehints,
      frame_range,
      ds::avisynth::cache_hint_response(cachehints, frame_range, mt_mode_),
      state_
    );
  }

  const VideoInfo& __stdcall GetVideoInfo() override {
    return vi_;
  }

private:
  PVideoFrame new_output_frame(int n, IScriptEnvironment* env) {
    PVideoFrame src = clip_->GetFrame(n, env);
    PVideoFrame dst = src;
    if (env->MakeWritable(&dst)) {
      return dst;
    }

    dst = env->NewVideoFrame(vi_);
    ds::avisynth::copy_video_frame_pixels(src, dst, input_info_.format);
    return dst;
  }

  PClip clip_;
  ds::VideoInputInfo input_info_;
  ds::VideoFilterState<Filter> state_;
  ds::avisynth::MtMode mt_mode_;
  VideoInfo vi_{};
};

template <class Bridge>
// NOLINTNEXTLINE(performance-unnecessary-value-param) - AviSynth callback ABI passes AVSValue by value.
AVSValue __cdecl create_avisynth_filter(AVSValue args, void*, IScriptEnvironment* env) {
  using Filter = typename Bridge::Core;

  try {
    PClip clip = args[0].AsClip();
    const VideoInfo& vi = clip->GetVideoInfo();
    const auto format = ds::avisynth::make_video_format(vi);
    if (!vi.HasVideo() || !format.has_value() || !Bridge::accepts_video_format(format.value())) {
      env->ThrowError(Bridge::avs_format_error);
    }

    std::array<ds::VideoInputInfo, 1> input_infos{
      ds::VideoInputInfo{
        .width = vi.width,
        .height = vi.height,
        .num_frames = vi.num_frames,
        .format = format.value(),
        .fps = ds::FrameRate{.numerator = vi.fps_numerator, .denominator = vi.fps_denominator}
      }
    };

    auto params = ds::avisynth::read_params(args, Bridge::descriptor());
    if (!params.has_value()) {
      env->ThrowError(params.error().message.c_str());
    }

    auto init_result = ds::init_video_filter_instance<Filter>(
      input_infos,
      &params.value(),
      ds::avisynth::host_global_lock_callbacks(env),
      avisynth_host_variable_callbacks(env)
    );
    if (!init_result.has_value()) {
      env->ThrowError(init_result.error().message.c_str());
    }

    const ds::VideoOutputInfo& output = init_result.value().output;
    if (
      output.width != vi.width ||
      output.height != vi.height ||
      output.num_frames != vi.num_frames ||
      output.format != format.value()
    ) {
      env->ThrowError("DelogoHD: AviSynth compatibility bridge requires source-shaped output");
    }

    return new AvisynthCompatVideoFilter<Bridge>(
      std::move(clip),
      input_infos[0],
      std::move(init_result.value().state),
      ds::avisynth::bridge_mt_mode<Bridge>()
    );
  } catch (const AvisynthError&) {
    throw;
  } catch (const std::exception& error) {
    env->ThrowError(error.what());
  } catch (...) {
    env->ThrowError("DelogoHD: unhandled exception in AviSynth creation");
  }

  return {};
}

template <class Bridge>
void register_avisynth_filter(IScriptEnvironment* env, bool register_mt_mode) {
  env->AddFunction(
    Bridge::avs_name,
    avs_signature<Bridge>(),
    create_avisynth_filter<Bridge>,
    nullptr
  );
  if (register_mt_mode) {
    ds::avisynth::set_video_filter_mt_mode<Bridge>(env);
  }
}

const char* register_avisynth_filters(IScriptEnvironment* env, bool register_mt_mode) {
  register_avisynth_filter<delogohd::DelogoHDBridge>(env, register_mt_mode);
  register_avisynth_filter<delogohd::AddlogoHDBridge>(env, register_mt_mode);
  return delogohd::Plugin::Description;
}

} // namespace

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
  vspapi->configPlugin(
    delogohd::Plugin::Identifier,
    delogohd::Plugin::Namespace,
    delogohd::Plugin::Description,
    VS_MAKE_VERSION(9, 6),
    VAPOURSYNTH_API_VERSION,
    0,
    plugin
  );

  vspapi->registerFunction(
    delogohd::DelogoHDBridge::vs_name,
    vs_signature(),
    "clip:vnode;",
    create_vapoursynth_filter<delogohd::DelogoHDBridge>,
    nullptr,
    plugin
  );
  vspapi->registerFunction(
    delogohd::AddlogoHDBridge::vs_name,
    vs_signature(),
    "clip:vnode;",
    create_vapoursynth_filter<delogohd::AddlogoHDBridge>,
    nullptr,
    plugin
  );
}

DELOGOHD_AVS_PLUGIN_EXPORT const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env) {
  AVS_linkage = env->GetAVSLinkage();
  return register_avisynth_filters(env, false);
}

DELOGOHD_AVS_PLUGIN_EXPORT const char* __stdcall AvisynthPluginInit3(
  IScriptEnvironment* env,
  const AVS_Linkage* const vectors
) {
  AVS_linkage = vectors;
  return register_avisynth_filters(env, true);
}
