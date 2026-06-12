#include "plugin/delogohd_filter.hpp"

#include <climits>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace delogohd {

namespace {

ds::Error invalid_argument(std::string message) {
  return ds::Error{.code = ds::ErrorCode::InvalidArgument, .message = std::move(message)};
}

const ds::ParamEntry* find_param(const ds::ParamValues& params, const std::string& name) {
  for (const auto& entry : params.entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

bool has_param(const ds::ParamValues& params, const std::string& name) {
  return find_param(params, name) != nullptr;
}

ds::Result<std::optional<std::string>> optional_string(
  const ds::ParamValues& params,
  const std::string& name
) {
  if (!has_param(params, name)) {
    return ds::Result<std::optional<std::string>>::success(std::nullopt);
  }
  auto value = params.get_string(name, "");
  if (!value.has_value()) {
    return ds::Result<std::optional<std::string>>::failure(value.error());
  }
  return ds::Result<std::optional<std::string>>::success(value.value());
}

template <class Parameters>
struct ParsedInitParameters {
  Parameters timeline;
  std::string logofile;
  std::optional<std::string> logoname;
  bool mono = false;
  int cutoff = 0;
};

template <class Parameters>
ds::Result<ParsedInitParameters<Parameters>> parse_init_parameters(
  const ds::ParamValues& params
) {
  auto logofile = optional_string(params, "logofile");
  if (!logofile.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(logofile.error());
  }
  auto parsed_logofile = std::move(logofile.value());
  if (!parsed_logofile.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(
      invalid_argument("where's the logo file?")
    );
  }

  auto logoname = optional_string(params, "logoname");
  if (!logoname.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(logoname.error());
  }

  auto left = params.get_int("left", 0);
  auto top = params.get_int("top", 0);
  auto start = params.get_int("start", 0);
  auto end = params.get_int("end", INT_MAX);
  auto fadein = params.get_int("fadein", 0);
  auto fadeout = params.get_int("fadeout", 0);
  auto opt = params.get_int("opt", 0);
  auto mono = params.get_bool("mono", false);
  auto cutoff = params.get_int("cutoff", 0);

  if (!left.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(left.error());
  }
  if (!top.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(top.error());
  }
  if (!start.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(start.error());
  }
  if (!end.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(end.error());
  }
  if (!fadein.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(fadein.error());
  }
  if (!fadeout.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(fadeout.error());
  }
  if (!opt.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(opt.error());
  }
  if (!mono.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(mono.error());
  }
  if (!cutoff.has_value()) {
    return ds::Result<ParsedInitParameters<Parameters>>::failure(cutoff.error());
  }

  ParsedInitParameters<Parameters> parsed{};
  parsed.timeline.left = left.value();
  parsed.timeline.top = top.value();
  parsed.timeline.start = start.value();
  parsed.timeline.end = end.value();
  parsed.timeline.fadein = fadein.value();
  parsed.timeline.fadeout = fadeout.value();
  parsed.timeline.opt = opt.value();
  parsed.logofile = std::move(*parsed_logofile);
  parsed.logoname = std::move(logoname.value());
  parsed.mono = mono.value();
  parsed.cutoff = cutoff.value();
  return ds::Result<ParsedInitParameters<Parameters>>::success(std::move(parsed));
}

template <core::LogoOperation Operation, class Parameters>
core::DelogoProcessorConfig make_processor_config(
  const ds::VideoInputInfo& input,
  const ParsedInitParameters<Parameters>& parsed
) {
  core::DelogoProcessorConfig processor_config{};
  processor_config.operation = Operation;
  processor_config.backend = parsed.timeline.opt == 1
    ? core::RowKernelBackend::Scalar
    : core::RowKernelBackend::Highway;
  processor_config.logofile = parsed.logofile.c_str();
  processor_config.logoname = parsed.logoname.has_value()
    ? parsed.logoname->c_str()
    : nullptr;
  processor_config.bit_depth = ds::bits_per_sample(input.format.sample_format);
  processor_config.subsampling_w = input.format.subsampling_w;
  processor_config.subsampling_h = input.format.subsampling_h;
  processor_config.left = parsed.timeline.left;
  processor_config.top = parsed.timeline.top;
  processor_config.mono = parsed.mono;
  processor_config.cutoff = parsed.cutoff;
  return processor_config;
}

ds::Result<bool> set_logo_host_vars(
  ds::VideoInitContext& context,
  const LOGO_HEADER& source_header
) {
  auto set_result = context.set_host_var("delogohd_left", source_header.x);
  if (!set_result.has_value()) {
    return ds::Result<bool>::failure(set_result.error());
  }
  set_result = context.set_host_var("delogohd_top", source_header.y);
  if (!set_result.has_value()) {
    return ds::Result<bool>::failure(set_result.error());
  }
  set_result = context.set_host_var("delogohd_width", source_header.w);
  if (!set_result.has_value()) {
    return ds::Result<bool>::failure(set_result.error());
  }
  set_result = context.set_host_var("delogohd_height", source_header.h);
  if (!set_result.has_value()) {
    return ds::Result<bool>::failure(set_result.error());
  }
  return ds::Result<bool>::success(true);
}

template <class Parameters>
double fade(const Parameters& params, int n) {
  if (n < params.start || (params.end < n && params.end >= params.start)) {
    return 0.0;
  }
  if (n < params.start + params.fadein) {
    return (n - params.start + 0.5) / params.fadein;
  }
  if (n > params.end - params.fadeout && params.end >= 0) {
    return (params.end - n + 0.5) / params.fadeout;
  }
  return 1.0;
}

bool is_supported_depth(ds::SampleFormat sample_format) {
  switch (sample_format) {
  case ds::SampleFormat::UInt8:
  case ds::SampleFormat::UInt10:
  case ds::SampleFormat::UInt12:
  case ds::SampleFormat::UInt14:
  case ds::SampleFormat::UInt16:
    return true;
  case ds::SampleFormat::Float32:
    return false;
  }
  return false;
}

bool is_supported_subsampling(int subsampling_w, int subsampling_h) {
  return (subsampling_w == 1 && subsampling_h == 1) ||
    (subsampling_w == 1 && subsampling_h == 0) ||
    (subsampling_w == 0 && subsampling_h == 0);
}

ds::FilterDescriptor make_descriptor(std::string name) {
  return ds::FilterDescriptor{
    .name = std::move(name),
    .params = std::vector<ds::ParamSpec>{
      ds::ParamSpec{
        .name = "clip",
        .type = ds::ParamType::Clip,
        .default_value = ds::ParamValue{},
        .required = true
      },
      ds::ParamSpec{.name = "logofile", .type = ds::ParamType::String},
      ds::ParamSpec{.name = "logoname", .type = ds::ParamType::String},
      ds::ParamSpec{.name = "left", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "top", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "start", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "end", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "fadein", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "fadeout", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "mono", .type = ds::ParamType::Boolean},
      ds::ParamSpec{.name = "cutoff", .type = ds::ParamType::Integer},
      ds::ParamSpec{.name = "opt", .type = ds::ParamType::Integer}
    }
  };
}

} // namespace

template <core::LogoOperation Operation>
LogoCore<Operation>::State::State() = default;

template <core::LogoOperation Operation>
LogoCore<Operation>::State::State(
  std::unique_ptr<core::DelogoProcessor> input_processor,
  Parameters input_params
) : processor(std::move(input_processor)),
    params(input_params) {}

template <core::LogoOperation Operation>
LogoCore<Operation>::State::~State() = default;

template <core::LogoOperation Operation>
LogoCore<Operation>::State::State(State&&) noexcept = default;

template <core::LogoOperation Operation>
typename LogoCore<Operation>::State& LogoCore<Operation>::State::operator=(State&&) noexcept =
  default;

template <core::LogoOperation Operation>
ds::Result<ds::VideoInitStateResult<typename LogoCore<Operation>::State>>
LogoCore<Operation>::init(ds::VideoInitContext& context) {
  try {
    const auto inputs = ds::collect_video_input_infos<LogoCore<Operation>>(context.inputs);
    if (!inputs.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(inputs.error());
    }

    const ds::ParamValues empty_params{};
    const ds::ParamValues& params = context.params != nullptr ? *context.params : empty_params;

    auto parsed_result = parse_init_parameters<Parameters>(params);
    if (!parsed_result.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(parsed_result.error());
    }
    auto parsed = std::move(parsed_result.value());

    const ds::VideoInputInfo& input = inputs.value()[0];
    auto processor_config = make_processor_config<Operation>(input, parsed);
    auto processor = std::make_unique<core::DelogoProcessor>(processor_config);

    auto host_vars = set_logo_host_vars(context, processor->source_header());
    if (!host_vars.has_value()) {
      return ds::Result<ds::VideoInitStateResult<State>>::failure(host_vars.error());
    }

    return ds::Result<ds::VideoInitStateResult<State>>::success(
      ds::VideoInitStateResult<State>{
        ds::VideoOutputInfo{
          .width = input.width,
          .height = input.height,
          .num_frames = input.num_frames,
          .format = input.format,
          .fps = input.fps
        },
        State{std::move(processor), parsed.timeline}
      }
    );
  } catch (const std::exception& error) {
    return ds::Result<ds::VideoInitStateResult<State>>::failure(invalid_argument(error.what()));
  } catch (...) {
    return ds::Result<ds::VideoInitStateResult<State>>::failure(
      invalid_argument("DelogoHD: unhandled initialization error")
    );
  }
}

template <core::LogoOperation Operation>
ds::Result<ds::VideoRequestResult> LogoCore<Operation>::request(
  ds::VideoRequestContext& /*context*/
) {
  return ds::Result<ds::VideoRequestResult>::success(ds::VideoRequestResult{});
}

template <core::LogoOperation Operation>
ds::Result<ds::VideoProcessResult> LogoCore<Operation>::process(ds::VideoProcessContext& context) {
  try {
    auto& state = context.state<State>();
    const Parameters& params = state.params;
    if (context.output_frame < params.start || context.output_frame > params.end) {
      return ds::Result<ds::VideoProcessResult>::success(ds::VideoProcessResult{});
    }

    const auto& y_plane = context.dst.plane(0);
    if (params.left >= y_plane.width || params.top >= y_plane.height) {
      return ds::Result<ds::VideoProcessResult>::success(ds::VideoProcessResult{});
    }

    const double opacity = fade(params, context.output_frame);
    if (opacity < 1e-2) {
      return ds::Result<ds::VideoProcessResult>::success(ds::VideoProcessResult{});
    }

    state.processor->process(context.dst, opacity);

    return ds::Result<ds::VideoProcessResult>::success(ds::VideoProcessResult{});
  } catch (const std::exception& error) {
    return ds::Result<ds::VideoProcessResult>::failure(invalid_argument(error.what()));
  } catch (...) {
    return ds::Result<ds::VideoProcessResult>::failure(
      invalid_argument("DelogoHD: unhandled processing error")
    );
  }
}

template <core::LogoOperation Operation>
int LogoCore<Operation>::cache_hints(ds::VideoCacheHintsContext& context) {
  return context.default_response;
}

template <class Core>
bool LogoBridgeBase<Core>::accepts_video_format(ds::VideoFormat format) {
  return format.color_family == ds::ColorFamily::Yuv &&
    format.plane_count >= 3 &&
    format.plane_count <= 4 &&
    is_supported_depth(format.sample_format) &&
    is_supported_subsampling(format.subsampling_w, format.subsampling_h);
}

template <class Core>
ds::FilterDescriptor LogoBridgeBase<Core>::descriptor() {
  return make_descriptor(Core::name);
}

template struct LogoCore<core::LogoOperation::Erase>;
template struct LogoCore<core::LogoOperation::Add>;
template struct LogoBridgeBase<DelogoHDCore>;
template struct LogoBridgeBase<AddlogoHDCore>;

} // namespace delogohd
