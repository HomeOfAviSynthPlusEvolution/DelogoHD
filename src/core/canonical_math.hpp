#pragma once

#include "core/logo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace delogohd::core {

constexpr int kLumaOffset = 67584;
constexpr int kLumaScale = 219;
constexpr int kChromaOffset = 32896;
constexpr int kChromaScale = 14;
constexpr int kChromaInternalScale = 16;

inline int clamp_logo_depth(int depth) noexcept {
  return std::clamp(depth, 0, LOGO_MAX_DP);
}

inline int luma_to_internal_color(int color) noexcept {
  return kLumaOffset + color * kLumaScale;
}

inline int chroma_to_internal_color(int color) noexcept {
  return (kChromaOffset + color * kChromaScale) * kChromaInternalScale;
}

inline int internal_shift_for_depth(int bit_depth) noexcept {
  return 20 - bit_depth;
}

inline std::int64_t sample_to_internal(int sample, int bit_depth) noexcept {
  return static_cast<std::int64_t>(sample) << internal_shift_for_depth(bit_depth);
}

inline int internal_to_sample(std::int64_t value, int bit_depth) noexcept {
  if (value < 0) {
    return 0;
  }
  const int shift = 18 - bit_depth;
  value >>= shift;
  ++value;
  value >>= 2;
  return static_cast<int>(value);
}

inline int scaled_depth(int depth, double opacity) noexcept {
  return clamp_logo_depth(static_cast<int>(std::lround(depth * opacity)));
}

inline int apply_add_alpha(
  int sample,
  int logo_color,
  int depth,
  int bit_depth
) noexcept {
  const int alpha = clamp_logo_depth(depth);
  const auto source = sample_to_internal(sample, bit_depth);
  const auto mixed =
    (source * (LOGO_MAX_DP - alpha) +
     static_cast<std::int64_t>(logo_color) * alpha +
     LOGO_MAX_DP / 2) / LOGO_MAX_DP;
  return internal_to_sample(mixed, bit_depth);
}

inline int apply_erase_alpha(
  int sample,
  int logo_color,
  int depth,
  int bit_depth
) noexcept {
  int alpha = clamp_logo_depth(depth);
  if (alpha >= LOGO_MAX_DP) {
    alpha = LOGO_MAX_DP - 1;
  }
  const int remaining = LOGO_MAX_DP - alpha;
  const auto source = sample_to_internal(sample, bit_depth);
  const auto restored =
    (source * LOGO_MAX_DP -
     static_cast<std::int64_t>(logo_color) * alpha +
     remaining / 2) / remaining;
  return internal_to_sample(restored, bit_depth);
}

inline int round_divide_signed(std::int64_t numerator, int denominator) noexcept {
  if (denominator == 0) {
    return 0;
  }
  if (numerator < 0) {
    return -static_cast<int>((-numerator + denominator / 2) / denominator);
  }
  return static_cast<int>((numerator + denominator / 2) / denominator);
}

} // namespace delogohd::core
