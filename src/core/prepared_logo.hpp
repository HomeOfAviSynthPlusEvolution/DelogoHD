#pragma once

#include "core/aligned_buffer.hpp"
#include "core/logo_file.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace delogohd::core {

struct DelogoProcessorConfig;

constexpr int kLogoAlignment = 32;
constexpr int kMinModulo = 32;

class LogoPlaneCoefficients {
public:
  void reset(int width, int height);

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  std::span<int> c_row(int y) noexcept;
  std::span<int> d_row(int y) noexcept;
  std::span<std::uint32_t> d_reciprocal_row(int y) noexcept;
  [[nodiscard]] std::span<const int> c_row(int y) const noexcept;
  [[nodiscard]] std::span<const int> d_row(int y) const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> d_reciprocal_row(int y) const noexcept;

private:
  int width_ = 0;
  int height_ = 0;
  AlignedBuffer<int, kLogoAlignment> c_;
  AlignedBuffer<int, kLogoAlignment> d_;
  AlignedBuffer<std::uint32_t, kLogoAlignment> d_reciprocal_;
};

class PreparedLogo {
public:
  explicit PreparedLogo(const DelogoProcessorConfig& config);

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] const LOGO_HEADER& source_header() const noexcept;
  [[nodiscard]] const LOGO_HEADER& logo_header() const noexcept;
  [[nodiscard]] const LogoPlaneCoefficients& plane(int index) const noexcept;
  [[nodiscard]] int subsampling_w() const noexcept;
  [[nodiscard]] int subsampling_h() const noexcept;

private:
  void convert(LogoImage& image, bool mono);

  LOGO_HEADER source_header_{};
  LOGO_HEADER logo_header_{};
  LogoPlaneCoefficients planes_[3];
  int subsampling_w_ = 0;
  int subsampling_h_ = 0;
  int cutoff_ = 0;
  bool active_ = false;
};

} // namespace delogohd::core
