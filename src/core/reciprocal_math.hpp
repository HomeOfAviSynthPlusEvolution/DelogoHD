#pragma once

#include "core/canonical_math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace delogohd::core {

constexpr int kReciprocalShift = 32;
constexpr std::uint64_t kReciprocalScale = std::uint64_t{1} << kReciprocalShift;

constexpr std::array<std::uint64_t, LOGO_MAX_DP + 1> make_logo_reciprocal_table() noexcept {
  std::array<std::uint64_t, LOGO_MAX_DP + 1> table{};
  for (int denominator = 1; denominator <= LOGO_MAX_DP; ++denominator) {
    table[denominator] = kReciprocalScale / static_cast<std::uint64_t>(denominator);
  }
  return table;
}

inline constexpr auto kLogoReciprocalTable = make_logo_reciprocal_table();

inline std::uint64_t divide_floor_by_reciprocal(
  std::uint64_t numerator,
  int denominator
) noexcept {
  const auto divisor = static_cast<std::uint64_t>(denominator);
  std::uint64_t quotient =
    (numerator * kLogoReciprocalTable[static_cast<std::size_t>(denominator)]) >>
    kReciprocalShift;
  if ((quotient + 1) * divisor <= numerator) {
    ++quotient;
  }
  return quotient;
}

inline std::int64_t divide_signed_by_reciprocal(
  std::int64_t numerator,
  int denominator
) noexcept {
  if (numerator < 0) {
    const auto magnitude = static_cast<std::uint64_t>(-(numerator + 1)) + 1;
    return -static_cast<std::int64_t>(divide_floor_by_reciprocal(magnitude, denominator));
  }
  return static_cast<std::int64_t>(
    divide_floor_by_reciprocal(static_cast<std::uint64_t>(numerator), denominator)
  );
}

inline int apply_add_alpha_reciprocal(
  int sample,
  int logo_color,
  int depth,
  int bit_depth
) noexcept {
  const int alpha = clamp_logo_depth(depth);
  const auto source = sample_to_internal(sample, bit_depth);
  const auto numerator =
    source * (LOGO_MAX_DP - alpha) +
    static_cast<std::int64_t>(logo_color) * alpha +
    LOGO_MAX_DP / 2;
  return internal_to_sample(
    divide_signed_by_reciprocal(numerator, LOGO_MAX_DP),
    bit_depth
  );
}

inline int apply_erase_alpha_reciprocal(
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
  const auto numerator =
    source * LOGO_MAX_DP -
    static_cast<std::int64_t>(logo_color) * alpha +
    remaining / 2;
  return internal_to_sample(
    divide_signed_by_reciprocal(numerator, remaining),
    bit_depth
  );
}

} // namespace delogohd::core
