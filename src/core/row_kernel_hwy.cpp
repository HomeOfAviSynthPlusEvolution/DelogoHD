// Highway multi-target dispatch for the default SIMD-compatible row kernels.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/row_kernel_hwy.cpp"

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#include "core/reciprocal_math.hpp"

#include <algorithm>
#include <cstdint>
#include <type_traits>

HWY_BEFORE_NAMESPACE();
namespace delogohd::core::HWY_NAMESPACE {

namespace {

namespace hn = hwy::HWY_NAMESPACE;

template <class Pixel, class Operation>
void process_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  Operation operation
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (int j = 0; j < upbound; ++j) {
    const auto value = operation(cellptr[j], array_c[j], array_d[j], bit_depth);
    cellptr[j] = static_cast<Pixel>(std::min(value, pixel_max));
  }
}

#if HWY_TARGET != HWY_SCALAR

template <class D64, class Pixel>
auto load_pixels_as_u64(D64 d64, const Pixel* cellptr, int offset, std::size_t lane_count) {
  const hn::Rebind<std::uint32_t, D64> d32;
  if constexpr (std::is_same_v<Pixel, std::uint8_t>) {
    const hn::Rebind<std::uint8_t, D64> d8;
    const hn::Rebind<std::uint16_t, D64> d16;
    return hn::PromoteTo(
      d64,
      hn::PromoteTo(d32, hn::PromoteTo(d16, hn::LoadN(d8, cellptr + offset, lane_count)))
    );
  } else {
    const hn::Rebind<std::uint16_t, D64> d16;
    return hn::PromoteTo(d64, hn::PromoteTo(d32, hn::LoadN(d16, cellptr + offset, lane_count)));
  }
}

template <class D64, class Pixel, class V64>
void store_pixels_from_u64(
  D64 d64,
  Pixel* cellptr,
  int offset,
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
    hn::StoreN(out8, d8, cellptr + offset, lane_count);
  } else {
    const hn::Rebind<std::uint16_t, D64> d16;
    const auto out16 = hn::DemoteTo(d16, values32);
    hn::StoreN(out16, d16, cellptr + offset, lane_count);
  }
}

template <int kBitDepth, class Pixel>
int process_add_row_vector(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d
) {
  const hn::ScalableTag<std::uint64_t> d64;
  const hn::Rebind<std::uint32_t, decltype(d64)> d32;
  const hn::Rebind<std::int32_t, decltype(d64)> di32;
  const std::size_t lane_count = hn::Lanes(d64);
  int j = 0;

  const auto logo_max = hn::Set(d64, std::uint64_t{LOGO_MAX_DP});
  const auto round = hn::Set(d64, std::uint64_t{LOGO_MAX_DP / 2});
  const auto reciprocal = hn::Set(d64, std::uint64_t{274877907});
  const auto one = hn::Set(d64, std::uint64_t{1});
  const auto pixel_max = hn::Set(d64, std::uint64_t{(1 << kBitDepth) - 1});

  for (; j + static_cast<int>(lane_count) <= upbound; j += static_cast<int>(lane_count)) {
    const auto sample = load_pixels_as_u64(d64, cellptr, j, lane_count);
    const auto source = hn::ShiftLeft<20 - kBitDepth>(sample);
    const auto color = hn::PromoteTo(d64, hn::BitCast(d32, hn::LoadN(di32, array_c + j, lane_count)));
    const auto depth = hn::PromoteTo(d64, hn::BitCast(d32, hn::LoadN(di32, array_d + j, lane_count)));
    const auto remaining = logo_max - depth;
    auto numerator = hn::Mul(source, remaining);
    numerator = numerator + hn::Mul(color, depth);
    numerator = numerator + round;

    auto value = hn::ShiftRight<38>(hn::Mul(numerator, reciprocal));
    value = hn::ShiftRight<18 - kBitDepth>(value);
    value = hn::ShiftRight<2>(value + one);
    value = hn::Min(value, pixel_max);

    store_pixels_from_u64(d64, cellptr, j, value, lane_count);
  }

  return j;
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

template <int kBitDepth, class Pixel>
int process_erase_row_vector(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d
) {
  const hn::ScalableTag<std::uint64_t> d64;
  const hn::Rebind<std::int64_t, decltype(d64)> di64;
  const hn::Rebind<std::uint32_t, decltype(d64)> d32;
  const hn::Rebind<std::int32_t, decltype(d64)> di32;
  const std::size_t lane_count = hn::Lanes(d64);
  int j = 0;

  const auto logo_max = hn::Set(d64, std::uint64_t{LOGO_MAX_DP});
  const auto max_alpha = hn::Set(d64, std::uint64_t{LOGO_MAX_DP - 1});
  const auto one = hn::Set(d64, std::uint64_t{1});
  const auto pixel_max = hn::Set(d64, std::uint64_t{(1 << kBitDepth) - 1});
  const auto zero_i64 = hn::Zero(di64);

  for (; j + static_cast<int>(lane_count) <= upbound; j += static_cast<int>(lane_count)) {
    const auto sample = load_pixels_as_u64(d64, cellptr, j, lane_count);
    const auto source = hn::ShiftLeft<20 - kBitDepth>(sample);
    const auto color = hn::PromoteTo(d64, hn::BitCast(d32, hn::LoadN(di32, array_c + j, lane_count)));
    const auto depth = hn::PromoteTo(d64, hn::BitCast(d32, hn::LoadN(di32, array_d + j, lane_count)));
    const auto alpha = hn::Min(depth, max_alpha);
    const auto remaining = logo_max - alpha;

    auto numerator = hn::BitCast(di64, hn::Mul(source, logo_max));
    numerator = numerator - hn::BitCast(di64, hn::Mul(color, alpha));
    numerator = numerator + hn::BitCast(di64, hn::ShiftRight<1>(remaining));

    const auto negative = hn::Lt(numerator, zero_i64);
    const auto magnitude = hn::IfThenElse(
      hn::RebindMask(d64, negative),
      hn::BitCast(d64, hn::Neg(numerator)),
      hn::BitCast(d64, numerator)
    );
    auto quotient = divide_u64_by_reciprocal_table(d64, magnitude, remaining);
    const auto restored = hn::IfThenElse(
      negative,
      hn::Neg(hn::BitCast(di64, quotient)),
      hn::BitCast(di64, quotient)
    );

    auto value = hn::BitCast(d64, hn::Max(restored, zero_i64));
    value = hn::ShiftRight<18 - kBitDepth>(value);
    value = hn::ShiftRight<2>(value + one);
    value = hn::Min(value, pixel_max);

    store_pixels_from_u64(d64, cellptr, j, value, lane_count);
  }

  return j;
}

#else

template <int kBitDepth, class Pixel>
int process_add_row_vector(
  Pixel*,
  int,
  const int*,
  const int*
) {
  return 0;
}

template <int kBitDepth, class Pixel>
int process_erase_row_vector(
  Pixel*,
  int,
  const int*,
  const int*
) {
  return 0;
}

#endif

template <int kBitDepth, class Pixel, class Operation>
void process_row_for_depth(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  Operation operation
) {
  constexpr int pixel_max = (1 << kBitDepth) - 1;
  for (int j = 0; j < upbound; ++j) {
    const auto value = operation(cellptr[j], array_c[j], array_d[j], kBitDepth);
    cellptr[j] = static_cast<Pixel>(std::min(value, pixel_max));
  }
}

template <int kBitDepth, class Pixel>
void process_add_row_for_depth(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d
) {
  const int processed = process_add_row_vector<kBitDepth>(cellptr, upbound, array_c, array_d);
  process_row_for_depth<kBitDepth>(
    cellptr + processed,
    upbound - processed,
    array_c + processed,
    array_d + processed,
    apply_add_alpha_reciprocal
  );
}

template <int kBitDepth, class Pixel>
void process_erase_row_for_depth(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d
) {
  const int processed = process_erase_row_vector<kBitDepth>(cellptr, upbound, array_c, array_d);
  process_row_for_depth<kBitDepth>(
    cellptr + processed,
    upbound - processed,
    array_c + processed,
    array_d + processed,
    apply_erase_alpha_reciprocal
  );
}

template <class Pixel>
void process_add_row_dispatch(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  switch (bit_depth) {
    case 8:
      process_add_row_for_depth<8>(cellptr, upbound, array_c, array_d);
      break;
    case 10:
      process_add_row_for_depth<10>(cellptr, upbound, array_c, array_d);
      break;
    case 12:
      process_add_row_for_depth<12>(cellptr, upbound, array_c, array_d);
      break;
    case 14:
      process_add_row_for_depth<14>(cellptr, upbound, array_c, array_d);
      break;
    case 16:
      process_add_row_for_depth<16>(cellptr, upbound, array_c, array_d);
      break;
    default:
      process_row(cellptr, upbound, array_c, array_d, bit_depth, apply_add_alpha_reciprocal);
      break;
  }
}

template <class Pixel>
void process_erase_row_dispatch(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  switch (bit_depth) {
    case 8:
      process_erase_row_for_depth<8>(cellptr, upbound, array_c, array_d);
      break;
    case 10:
      process_erase_row_for_depth<10>(cellptr, upbound, array_c, array_d);
      break;
    case 12:
      process_erase_row_for_depth<12>(cellptr, upbound, array_c, array_d);
      break;
    case 14:
      process_erase_row_for_depth<14>(cellptr, upbound, array_c, array_d);
      break;
    case 16:
      process_erase_row_for_depth<16>(cellptr, upbound, array_c, array_d);
      break;
    default:
      process_row(cellptr, upbound, array_c, array_d, bit_depth, apply_erase_alpha_reciprocal);
      break;
  }
}

} // namespace

void AddRowU8(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_add_row_dispatch(cellptr, upbound, array_c, array_d, bit_depth);
}

void AddRowU16(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_add_row_dispatch(cellptr, upbound, array_c, array_d, bit_depth);
}

void EraseRowU8(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_erase_row_dispatch(cellptr, upbound, array_c, array_d, bit_depth);
}

void EraseRowU16(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_erase_row_dispatch(cellptr, upbound, array_c, array_d, bit_depth);
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
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(AddRowU8)(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_add_row_hwy(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(AddRowU16)(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_erase_row_hwy(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU8)(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_erase_row_hwy(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  HWY_DYNAMIC_DISPATCH(EraseRowU16)(cellptr, upbound, array_c, array_d, bit_depth);
}

} // namespace delogohd::core

#endif // HWY_ONCE
