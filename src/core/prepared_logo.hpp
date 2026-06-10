#pragma once

#include "core/aligned_buffer.hpp"
#include "core/logo_file.hpp"

#include <cstddef>

namespace delogohd::core {

struct DelogoProcessorConfig;

constexpr int kLogoAlignment = 32;
constexpr int kMinModulo = 32;

class LogoPlaneCoefficients {
public:
  void reset(int width, int height);

  bool active() const noexcept;
  int width() const noexcept;
  int height() const noexcept;
  int* c_row(int y) noexcept;
  int* d_row(int y) noexcept;
  const int* c_row(int y) const noexcept;
  const int* d_row(int y) const noexcept;

private:
  int width_ = 0;
  int height_ = 0;
  AlignedBuffer<int, kLogoAlignment> c_;
  AlignedBuffer<int, kLogoAlignment> d_;
};

class PreparedLogo {
public:
  explicit PreparedLogo(const DelogoProcessorConfig& config);

  bool active() const noexcept;
  const LOGO_HEADER& source_header() const noexcept;
  const LOGO_HEADER& logo_header() const noexcept;
  const LogoPlaneCoefficients& plane(int index) const noexcept;
  int subsampling_w() const noexcept;
  int subsampling_h() const noexcept;

private:
  void convert(LogoImage& image, bool mono);

  LOGO_HEADER source_header_{};
  LOGO_HEADER logo_header_{};
  LogoPlaneCoefficients planes_[3];
  int subsampling_w_ = 0;
  int subsampling_h_ = 0;
  int cutoff_ = 0;
  bool canonical_ = false;
  bool erase_ = true;
  bool active_ = false;
};

} // namespace delogohd::core
