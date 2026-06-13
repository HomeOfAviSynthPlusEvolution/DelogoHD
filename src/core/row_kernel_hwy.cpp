// Highway multi-target dispatch for the default SIMD-compatible row kernels.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/row_kernel_hwy.cpp"

// These integer row kernels do not use the feature-specific float, dot-product,
// AES, or BF16 operations that justify Highway's finest-grained dispatch
// targets. MSVC/x86 is faster on the SSE4 path for these integer kernels than
// on AVX2/AVX3 because the wider paths spend more on lane packing and u32
// high-multiply emulation.
#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS                                                \
  (HWY_SSSE3 | HWY_AVX2 | HWY_AVX3 | HWY_AVX3_DL | HWY_AVX3_ZEN4 |          \
   HWY_AVX3_SPR | HWY_AVX10_2 | HWY_NEON_BF16 | HWY_SVE_256 | HWY_SVE2_128)
#endif

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#include "core/reciprocal_math.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

HWY_BEFORE_NAMESPACE();
namespace delogohd::core::HWY_NAMESPACE {

namespace {

namespace hn = hwy::HWY_NAMESPACE;

template <class Pixel, class Operation>
void process_row(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth,
  Operation operation
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const auto value = operation(row[j], colors[j], depths[j], bit_depth);
    row[j] = static_cast<Pixel>(std::min(value, pixel_max));
  }
}

#if HWY_TARGET != HWY_SCALAR

template <class D32, class Pixel>
auto load_pixels_as_u32(
  D32 d32,
  std::span<Pixel> row,
  std::size_t offset,
  std::size_t lane_count
) {
  if constexpr (std::is_same_v<Pixel, std::uint8_t>) {
    const hn::Rebind<std::uint8_t, D32> d8;
    const hn::Rebind<std::uint16_t, D32> d16;
    return hn::PromoteTo(
      d32,
      hn::PromoteTo(d16, hn::LoadN(d8, row.data() + offset, lane_count))
    );
  } else {
    const hn::Rebind<std::uint16_t, D32> d16;
    return hn::PromoteTo(d32, hn::LoadN(d16, row.data() + offset, lane_count));
  }
}

template <class D32, class Pixel, class V32>
void store_pixels_from_u32(
  D32 d32,
  std::span<Pixel> row,
  std::size_t offset,
  V32 values,
  std::size_t lane_count
) {
  if constexpr (std::is_same_v<Pixel, std::uint8_t>) {
    const hn::Rebind<std::uint16_t, D32> d16;
    const hn::Rebind<std::uint8_t, D32> d8;
    const auto out16 = hn::DemoteTo(d16, values);
    const auto out8 = hn::DemoteTo(d8, out16);
    hn::StoreN(out8, d8, row.data() + offset, lane_count);
  } else {
    const hn::Rebind<std::uint16_t, D32> d16;
    const auto out16 = hn::DemoteTo(d16, values);
    hn::StoreN(out16, d16, row.data() + offset, lane_count);
  }
}

template <class D32, class V32>
auto mul_high_u32(D32 d32, V32 left, V32 right) {
  const hn::Half<D32> dh32;
  const auto even = hn::ShiftRight<32>(hn::MulEven(left, right));
  const auto odd = hn::ShiftRight<32>(hn::MulOdd(left, right));
  const auto even32 = hn::DemoteTo(dh32, even);
  const auto odd32 = hn::DemoteTo(dh32, odd);
  return hn::Combine(
    d32,
    hn::InterleaveUpper(dh32, even32, odd32),
    hn::InterleaveLower(dh32, even32, odd32)
  );
}

template <class D32, class V32>
auto divide_u32_by_reciprocal(D32 d32, V32 numerator, V32 reciprocal, V32 denominator) {
  const auto one = hn::Set(d32, std::uint32_t{1});
  auto quotient = mul_high_u32(d32, numerator, reciprocal);
  const auto next_quotient = hn::Add(quotient, one);
  const auto needs_increment = hn::Le(hn::Mul(next_quotient, denominator), numerator);
  return hn::IfThenElse(needs_increment, next_quotient, quotient);
}

template <int kDenominator, class D32, class V32>
auto divide_u32_by_constant(D32 d32, V32 numerator) {
  const auto denominator = hn::Set(d32, static_cast<std::uint32_t>(kDenominator));
  const auto reciprocal = hn::Set(d32, kLogoReciprocalTable32[kDenominator]);
  auto quotient = mul_high_u32(d32, numerator, reciprocal);
  const auto next_quotient = hn::Add(quotient, hn::Set(d32, std::uint32_t{1}));
  const auto needs_increment = hn::Le(hn::Mul(next_quotient, denominator), numerator);
  return hn::IfThenElse(needs_increment, next_quotient, quotient);
}

template <class D32, class V32I, class V32U>
auto divide_i32_by_reciprocal(D32 d32, V32I numerator, V32U reciprocal, V32U denominator) {
  const hn::Rebind<std::uint32_t, D32> du32;
  const auto zero_i32 = hn::Zero(d32);
  const auto negative = hn::Lt(numerator, zero_i32);
  const auto magnitude = hn::IfThenElse(
    hn::RebindMask(du32, negative),
    hn::BitCast(du32, hn::Neg(numerator)),
    hn::BitCast(du32, numerator)
  );
  const auto quotient = divide_u32_by_reciprocal(du32, magnitude, reciprocal, denominator);
  return hn::IfThenElse(
    negative,
    hn::Neg(hn::BitCast(d32, quotient)),
    hn::BitCast(d32, quotient)
  );
}

template <int kDenominator, class D32, class V32I>
auto divide_i32_by_constant(D32 d32, V32I numerator) {
  const hn::Rebind<std::uint32_t, D32> du32;
  const auto zero_i32 = hn::Zero(d32);
  const auto negative = hn::Lt(numerator, zero_i32);
  const auto magnitude = hn::IfThenElse(
    hn::RebindMask(du32, negative),
    hn::BitCast(du32, hn::Neg(numerator)),
    hn::BitCast(du32, numerator)
  );
  const auto quotient = divide_u32_by_constant<kDenominator>(du32, magnitude);
  return hn::IfThenElse(
    negative,
    hn::Neg(hn::BitCast(d32, quotient)),
    hn::BitCast(d32, quotient)
  );
}

template <int kBitDepth, class D32, class V32I>
auto sample_from_internal(D32 d32, V32I values) {
  const hn::Rebind<std::uint32_t, D32> du32;
  const auto zero_i32 = hn::Zero(d32);
  const auto one = hn::Set(du32, std::uint32_t{1});
  const auto pixel_max = hn::Set(du32, std::uint32_t{(1 << kBitDepth) - 1});
  auto value = hn::BitCast(du32, hn::Max(values, zero_i32));
  value = hn::ShiftRight<18 - kBitDepth>(value);
  value = hn::ShiftRight<2>(hn::Add(value, one));
  return hn::Min(value, pixel_max);
}

template <int kBitDepth, class Pixel>
std::size_t process_add_row_vector(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths
) {
  const hn::ScalableTag<std::uint32_t> du32;
  const hn::Rebind<std::int32_t, decltype(du32)> di32;
  const std::size_t lane_count = hn::Lanes(du32);
  std::size_t j = 0;

  const auto logo_max = hn::Set(di32, LOGO_MAX_DP);
  const auto round = hn::Set(di32, LOGO_MAX_DP / 2);

  for (; j + lane_count <= row.size(); j += lane_count) {
    const auto sample = load_pixels_as_u32(du32, row, j, lane_count);
    const auto source = hn::BitCast(di32, hn::ShiftLeft<20 - kBitDepth>(sample));
    const auto color = hn::LoadN(di32, colors.data() + j, lane_count);
    const auto depth = hn::LoadN(di32, depths.data() + j, lane_count);
    const auto remaining = hn::Sub(logo_max, depth);
    auto numerator = hn::Mul(source, remaining);
    numerator = hn::Add(numerator, hn::Mul(color, depth));
    numerator = hn::Add(numerator, round);

    const auto mixed = divide_i32_by_constant<LOGO_MAX_DP>(di32, numerator);
    const auto value = sample_from_internal<kBitDepth>(di32, mixed);

    store_pixels_from_u32(du32, row, j, value, lane_count);
  }

  return j;
}

template <int kBitDepth, class Pixel>
std::size_t process_erase_row_vector(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals
) {
  const hn::ScalableTag<std::uint32_t> du32;
  const hn::Rebind<std::int32_t, decltype(du32)> di32;
  const std::size_t lane_count = hn::Lanes(du32);
  std::size_t j = 0;

  const auto logo_max = hn::Set(di32, LOGO_MAX_DP);
  const auto max_alpha = hn::Set(di32, LOGO_MAX_DP - 1);

  for (; j + lane_count <= row.size(); j += lane_count) {
    const auto sample = load_pixels_as_u32(du32, row, j, lane_count);
    const auto source = hn::BitCast(di32, hn::ShiftLeft<20 - kBitDepth>(sample));
    const auto color = hn::LoadN(di32, colors.data() + j, lane_count);
    const auto depth = hn::LoadN(di32, depths.data() + j, lane_count);
    const auto reciprocal = hn::LoadN(du32, reciprocals.data() + j, lane_count);
    const auto alpha = hn::Min(depth, max_alpha);
    const auto remaining = hn::Sub(logo_max, alpha);

    auto numerator = hn::Mul(source, logo_max);
    numerator = hn::Sub(numerator, hn::Mul(color, alpha));
    numerator = hn::Add(numerator, hn::ShiftRight<1>(remaining));

    const auto restored = divide_i32_by_reciprocal(
      di32,
      numerator,
      reciprocal,
      hn::BitCast(du32, remaining)
    );
    const auto value = sample_from_internal<kBitDepth>(di32, restored);

    store_pixels_from_u32(du32, row, j, value, lane_count);
  }

  return j;
}

#else

template <int kBitDepth, class Pixel>
std::size_t process_add_row_vector(
  std::span<Pixel>,
  std::span<const int>,
  std::span<const int>
) {
  return 0;
}

template <int kBitDepth, class Pixel>
std::size_t process_erase_row_vector(
  std::span<Pixel>,
  std::span<const int>,
  std::span<const int>,
  std::span<const std::uint32_t>
) {
  return 0;
}

#endif

template <int kBitDepth, class Pixel, class Operation>
void process_row_for_depth(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  Operation operation
) {
  constexpr int pixel_max = (1 << kBitDepth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const auto value = operation(row[j], colors[j], depths[j], kBitDepth);
    row[j] = static_cast<Pixel>(std::min(value, pixel_max));
  }
}

template <int kBitDepth, class Pixel>
void process_add_row_for_depth(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths
) {
  const std::size_t processed = process_add_row_vector<kBitDepth>(row, colors, depths);
  process_row_for_depth<kBitDepth>(
    row.subspan(processed),
    colors.subspan(processed),
    depths.subspan(processed),
    apply_add_alpha_reciprocal
  );
}

template <int kBitDepth, class Pixel>
void process_erase_row_for_depth(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals
) {
  const std::size_t processed =
    process_erase_row_vector<kBitDepth>(row, colors, depths, reciprocals);
  process_row_for_depth<kBitDepth>(
    row.subspan(processed),
    colors.subspan(processed),
    depths.subspan(processed),
    apply_erase_alpha_reciprocal
  );
}

template <class Pixel>
void process_add_row_dispatch(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  switch (bit_depth) {
    case 8:
      process_add_row_for_depth<8>(row, colors, depths);
      break;
    case 10:
      process_add_row_for_depth<10>(row, colors, depths);
      break;
    case 12:
      process_add_row_for_depth<12>(row, colors, depths);
      break;
    case 14:
      process_add_row_for_depth<14>(row, colors, depths);
      break;
    case 16:
      process_add_row_for_depth<16>(row, colors, depths);
      break;
    default:
      process_row(row, colors, depths, bit_depth, apply_add_alpha_reciprocal);
      break;
  }
}

template <class Pixel>
void process_erase_row_dispatch(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
) {
  switch (bit_depth) {
    case 8:
      process_erase_row_for_depth<8>(row, colors, depths, reciprocals);
      break;
    case 10:
      process_erase_row_for_depth<10>(row, colors, depths, reciprocals);
      break;
    case 12:
      process_erase_row_for_depth<12>(row, colors, depths, reciprocals);
      break;
    case 14:
      process_erase_row_for_depth<14>(row, colors, depths, reciprocals);
      break;
    case 16:
      process_erase_row_for_depth<16>(row, colors, depths, reciprocals);
      break;
    default:
      process_row(row, colors, depths, bit_depth, apply_erase_alpha_reciprocal);
      break;
  }
}

} // namespace

void AddRowU8(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_add_row_dispatch(row, colors, depths, bit_depth);
}

void AddRowU16(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_add_row_dispatch(row, colors, depths, bit_depth);
}

void EraseRowU8(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
) {
  process_erase_row_dispatch(row, colors, depths, reciprocals, bit_depth);
}

void EraseRowU16(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
) {
  process_erase_row_dispatch(row, colors, depths, reciprocals, bit_depth);
}

} // namespace delogohd::core::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include "hwy/per_target.h"

#include "core/row_kernel.hpp"

namespace delogohd::core {

HWY_EXPORT(AddRowU8);
HWY_EXPORT(AddRowU16);
HWY_EXPORT(EraseRowU8);
HWY_EXPORT(EraseRowU16);

void process_add_row_hwy(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(AddRowU8)(row, colors, depths, bit_depth);
}

void process_add_row_hwy(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(AddRowU16)(row, colors, depths, bit_depth);
}

void process_erase_row_hwy(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU8)(row, colors, depths, reciprocals, bit_depth);
}

void process_erase_row_hwy(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU16)(row, colors, depths, reciprocals, bit_depth);
}

} // namespace delogohd::core

#endif // HWY_ONCE
