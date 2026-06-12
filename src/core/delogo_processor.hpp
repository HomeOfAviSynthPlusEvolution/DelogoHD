#pragma once

#include <dualsynth/frame.hpp>

#include "core/prepared_logo.hpp"

#include <cstdint>

namespace delogohd::core {

enum class LogoOperation : std::uint8_t {
  Add,
  Erase,
};

enum class RowKernelBackend : std::uint8_t {
  Highway,
  Scalar,
};

struct DelogoProcessorConfig {
  LogoOperation operation = LogoOperation::Erase;
  RowKernelBackend backend = RowKernelBackend::Highway;
  const char* logofile = nullptr;
  const char* logoname = nullptr;
  int bit_depth = 8;
  int subsampling_w = 1;
  int subsampling_h = 1;
  int left = 0;
  int top = 0;
  bool mono = false;
  int cutoff = 0;
};

class DelogoProcessor {
public:
  explicit DelogoProcessor(const DelogoProcessorConfig& config);

  [[nodiscard]] const LOGO_HEADER& source_header() const noexcept;

  void process(ds::MutableVideoFrameView& frame, double opacity);

private:
  template <class Pixel>
  void process_plane(ds::MutablePlaneView& plane, int plane_index, double opacity);

  LogoOperation operation_;
  RowKernelBackend backend_;
  int bit_depth_;
  PreparedLogo logo_;
};

} // namespace delogohd::core
