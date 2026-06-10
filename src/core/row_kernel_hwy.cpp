// Highway multi-target dispatch for the default SIMD-compatible row kernels.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/row_kernel_hwy.cpp"

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

template <class D64, class Pixel>
auto load_pixels_as_u64(
  D64 d64,
  std::span<Pixel> row,
  std::size_t offset,
  std::size_t lane_count
) {
  const hn::Rebind<std::uint32_t, D64> d32;
  if constexpr (std::is_same_v<Pixel, std::uint8_t>) {
    const hn::Rebind<std::uint8_t, D64> d8;
    const hn::Rebind<std::uint16_t, D64> d16;
    return hn::PromoteTo(
      d64,
      hn::PromoteTo(d32, hn::PromoteTo(d16, hn::LoadN(d8, row.data() + offset, lane_count)))
    );
  } else {
    const hn::Rebind<std::uint16_t, D64> d16;
    return hn::PromoteTo(d64, hn::PromoteTo(d32, hn::LoadN(d16, row.data() + offset, lane_count)));
  }
}

template <class D64, class Pixel, class V64>
void store_pixels_from_u64(
  D64 d64,
  std::span<Pixel> row,
  std::size_t offset,
  V64 values,
  std::size_t lane_count
) {
  const hn::Rebind<std::uint32_t, D64> d32;
  const auto values32 = hn::DemoteTo(d32, values);
  if constexpr (std::is_same_v<Pixel, std::uint8_t>) {
    const hn::Rebind<std::uint16_t, D64> d16;
    const hn::Rebind<std::uint8_t, D64> d8;
    const auto out16 = hn::DemoteTo(d16, values32);
    const auto out8 = hn::DemoteTo(d8, out16);
    hn::StoreN(out8, d8, row.data() + offset, lane_count);
  } else {
    const hn::Rebind<std::uint16_t, D64> d16;
    const auto out16 = hn::DemoteTo(d16, values32);
    hn::StoreN(out16, d16, row.data() + offset, lane_count);
  }
}

template <class D64, class V64>
auto divide_u64_by_reciprocal_table(D64 d64, V64 numerator, V64 denominator) {
  const hn::Rebind<std::int64_t, D64> di64;
  const auto one = hn::Set(d64, std::uint64_t{1});
  const auto reciprocal = hn::GatherIndex(
    d64,
    kLogoReciprocalTable.data(),
    hn::BitCast(di64, denominator)
  );
  auto quotient = hn::ShiftRight<kReciprocalShift>(hn::Mul(numerator, reciprocal));
  const auto next_quotient = quotient + one;
  const auto needs_increment = hn::Le(hn::Mul(next_quotient, denominator), numerator);
  return hn::IfThenElse(needs_increment, next_quotient, quotient);
}

template <class D64, class V64I, class V64U>
auto divide_i64_by_reciprocal_table(D64 d64, V64I numerator, V64U denominator) {
  const hn::Rebind<std::int64_t, D64> di64;
  const auto zero_i64 = hn::Zero(di64);
  const auto negative = hn::Lt(numerator, zero_i64);
  const auto magnitude = hn::IfThenElse(
    hn::RebindMask(d64, negative),
    hn::BitCast(d64, hn::Neg(numerator)),
    hn::BitCast(d64, numerator)
  );
  const auto quotient = divide_u64_by_reciprocal_table(d64, magnitude, denominator);
  return hn::IfThenElse(
    negative,
    hn::Neg(hn::BitCast(di64, quotient)),
    hn::BitCast(di64, quotient)
  );
}

template <int kBitDepth, class D64, class V64I>
auto sample_from_internal(D64 d64, V64I values) {
  const hn::Rebind<std::int64_t, D64> di64;
  const auto zero_i64 = hn::Zero(di64);
  const auto one = hn::Set(d64, std::uint64_t{1});
  const auto pixel_max = hn::Set(d64, std::uint64_t{(1 << kBitDepth) - 1});
  auto value = hn::BitCast(d64, hn::Max(values, zero_i64));
  value = hn::ShiftRight<18 - kBitDepth>(value);
  value = hn::ShiftRight<2>(value + one);
  return hn::Min(value, pixel_max);
}

template <int kBitDepth, class Pixel>
std::size_t process_add_row_vector(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths
) {
  const hn::ScalableTag<std::uint64_t> d64;
  const hn::Rebind<std::int64_t, decltype(d64)> di64;
  const hn::Rebind<std::int32_t, decltype(d64)> di32;
  const std::size_t lane_count = hn::Lanes(d64);
  std::size_t j = 0;

  const auto logo_max = hn::Set(di64, std::int64_t{LOGO_MAX_DP});
  const auto logo_max_u = hn::Set(d64, std::uint64_t{LOGO_MAX_DP});
  const auto round = hn::Set(di64, std::int64_t{LOGO_MAX_DP / 2});

  for (; j + lane_count <= row.size(); j += lane_count) {
    const auto sample = load_pixels_as_u64(d64, row, j, lane_count);
    const auto source = hn::BitCast(di64, hn::ShiftLeft<20 - kBitDepth>(sample));
    const auto color = hn::PromoteTo(di64, hn::LoadN(di32, colors.data() + j, lane_count));
    const auto depth = hn::PromoteTo(di64, hn::LoadN(di32, depths.data() + j, lane_count));
    const auto remaining = logo_max - depth;
    auto numerator = hn::Mul(source, remaining);
    numerator = numerator + hn::Mul(color, depth);
    numerator = numerator + round;

    const auto mixed = divide_i64_by_reciprocal_table(d64, numerator, logo_max_u);
    const auto value = sample_from_internal<kBitDepth>(d64, mixed);

    store_pixels_from_u64(d64, row, j, value, lane_count);
  }

  return j;
}

template <int kBitDepth, class Pixel>
std::size_t process_erase_row_vector(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths
) {
  const hn::ScalableTag<std::uint64_t> d64;
  const hn::Rebind<std::int64_t, decltype(d64)> di64;
  const hn::Rebind<std::int32_t, decltype(d64)> di32;
  const std::size_t lane_count = hn::Lanes(d64);
  std::size_t j = 0;

  const auto logo_max = hn::Set(di64, std::int64_t{LOGO_MAX_DP});
  const auto max_alpha = hn::Set(di64, std::int64_t{LOGO_MAX_DP - 1});

  for (; j + lane_count <= row.size(); j += lane_count) {
    const auto sample = load_pixels_as_u64(d64, row, j, lane_count);
    const auto source = hn::BitCast(di64, hn::ShiftLeft<20 - kBitDepth>(sample));
    const auto color = hn::PromoteTo(di64, hn::LoadN(di32, colors.data() + j, lane_count));
    const auto depth = hn::PromoteTo(di64, hn::LoadN(di32, depths.data() + j, lane_count));
    const auto alpha = hn::Min(depth, max_alpha);
    const auto remaining = logo_max - alpha;

    auto numerator = hn::Mul(source, logo_max);
    numerator = numerator - hn::Mul(color, alpha);
    numerator = numerator + hn::ShiftRight<1>(remaining);

    const auto restored = divide_i64_by_reciprocal_table(
      d64,
      numerator,
      hn::BitCast(d64, remaining)
    );
    const auto value = sample_from_internal<kBitDepth>(d64, restored);

    store_pixels_from_u64(d64, row, j, value, lane_count);
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
  std::span<const int>
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
  std::span<const int> depths
) {
  const std::size_t processed = process_erase_row_vector<kBitDepth>(row, colors, depths);
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
  int bit_depth
) {
  switch (bit_depth) {
    case 8:
      process_erase_row_for_depth<8>(row, colors, depths);
      break;
    case 10:
      process_erase_row_for_depth<10>(row, colors, depths);
      break;
    case 12:
      process_erase_row_for_depth<12>(row, colors, depths);
      break;
    case 14:
      process_erase_row_for_depth<14>(row, colors, depths);
      break;
    case 16:
      process_erase_row_for_depth<16>(row, colors, depths);
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
  int bit_depth
) {
  process_erase_row_dispatch(row, colors, depths, bit_depth);
}

void EraseRowU16(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_erase_row_dispatch(row, colors, depths, bit_depth);
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
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU8)(row, colors, depths, bit_depth);
}

void process_erase_row_hwy(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU16)(row, colors, depths, bit_depth);
}

} // namespace delogohd::core

#endif // HWY_ONCE
