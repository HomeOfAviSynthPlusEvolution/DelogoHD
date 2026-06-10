#pragma once

#include <dualsynth/avisynth/video_bridge.hpp>
#include <dualsynth/format.hpp>
#include <dualsynth/param.hpp>
#include <dualsynth/video_bridge.hpp>
#include <dualsynth/video_filter.hpp>

#include "core/delogo_processor.hpp"
#include "version.hpp"

#include <climits>
#include <memory>

namespace delogohd {

template <core::LogoOperation Operation>
struct LogoCore {
  static constexpr const char* name =
    Operation == core::LogoOperation::Erase ? "DelogoHD" : "AddlogoHD";
  static constexpr int input_count = 1;
  static constexpr ds::OutputOrigin output_origin = ds::OutputOrigin::take_from_input(0);

  struct Parameters {
    int start = 0;
    int end = INT_MAX;
    int fadein = 0;
    int fadeout = 0;
    int left = 0;
    int top = 0;
  };

  struct State {
    State();
    State(std::unique_ptr<core::DelogoProcessor> processor, Parameters params);
    ~State();

    State(State&&) noexcept;
    State& operator=(State&&) noexcept;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    std::unique_ptr<core::DelogoProcessor> processor;
    Parameters params;
  };

  static ds::Result<ds::VideoInitStateResult<State>> init(ds::VideoInitContext& context);
  static ds::Result<ds::VideoRequestResult> request(ds::VideoRequestContext& context);
  static ds::Result<ds::VideoProcessResult> process(ds::VideoProcessContext& context);
  static int cache_hints(ds::VideoCacheHintsContext& context);
};

struct DelogoHDCore : LogoCore<core::LogoOperation::Erase> {
  static constexpr const char* name = "DelogoHD";
};

struct AddlogoHDCore : LogoCore<core::LogoOperation::Add> {
  static constexpr const char* name = "AddlogoHD";
};

template <class Core>
struct LogoBridgeBase : ds::SingleInputVideoBridgeDefaults<Core> {
  static constexpr const char* missing_input_error = "DelogoHD: missing required video clip";
  static constexpr const char* vs_format_error =
    "DelogoHD: only 8-16 bit integer YUV 420, 422, and 444 video is supported";
  static constexpr const char* avs_format_error =
    "DelogoHD: only 8-16 bit integer YUV 420, 422, and 444 video is supported";
  static constexpr ds::avisynth::MtMode avs_mt_mode = ds::avisynth::MtMode::NiceFilter;

  static bool accepts_video_format(ds::VideoFormat format);
  static ds::FilterDescriptor descriptor();
};

struct DelogoHDBridge : LogoBridgeBase<DelogoHDCore> {
  static constexpr const char* vs_name = "DelogoHD";
  static constexpr const char* avs_name = "DelogoHD";
};

struct AddlogoHDBridge : LogoBridgeBase<AddlogoHDCore> {
  static constexpr const char* vs_name = "AddlogoHD";
  static constexpr const char* avs_name = "AddlogoHD";
};

namespace Plugin {
inline constexpr const char* Identifier = "in.7086.delogohd";
inline constexpr const char* Namespace = "delogohd";
inline constexpr const char* Description = "DelogoHD Filter " PLUGIN_VERSION;
} // namespace Plugin

} // namespace delogohd
