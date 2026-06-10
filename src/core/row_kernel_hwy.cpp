// Highway multi-target dispatch for the default SIMD-compatible row kernels.
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "core/row_kernel_hwy.cpp"

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#include "core/logo.h"

#include <algorithm>
#include <cstdint>
#include <limits>

HWY_BEFORE_NAMESPACE();
namespace delogohd::core::HWY_NAMESPACE {

namespace {

std::uint16_t packus_epi32(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
    return 0;
  }
  if (value > std::numeric_limits<std::uint16_t>::max()) {
    return std::numeric_limits<std::uint16_t>::max();
  }
  return static_cast<std::uint16_t>(value);
}

std::uint32_t max_epi32_zero(std::uint32_t value) {
  const auto signed_value = static_cast<std::int32_t>(value);
  return signed_value > 0 ? value : 0;
}

std::uint16_t add_simd_value(
  std::uint32_t pixel,
  std::int32_t c,
  std::int32_t d,
  int bit_depth
) {
  const int shift = 16 - bit_depth;
  const auto shifted_pixel = static_cast<std::uint32_t>(pixel << (shift + 2));
  const auto product = static_cast<std::uint64_t>(shifted_pixel) *
    static_cast<std::uint32_t>(d);
  const auto adjusted = static_cast<std::uint32_t>(product) -
    static_cast<std::uint32_t>(c);
  const auto positive = max_epi32_zero(adjusted);
  const auto scaled = static_cast<std::uint64_t>(positive) *
    ((1u << 31) / LOGO_MAX_DP);
  const auto rounded = ((scaled >> 33) + 1) >> 2;
  return static_cast<std::uint16_t>(packus_epi32(rounded) >> shift);
}

std::uint16_t erase_simd_value(
  std::uint32_t pixel,
  std::int32_t c,
  std::int32_t d,
  int bit_depth
) {
  const int shift = 16 - bit_depth;
  const auto shifted_pixel = static_cast<std::uint32_t>(pixel << shift);
  const auto product = static_cast<std::uint64_t>(shifted_pixel) *
    static_cast<std::uint32_t>(LOGO_MAX_DP << 4);
  const auto adjusted = static_cast<std::uint32_t>(product) +
    static_cast<std::uint32_t>(c);
  const auto positive = max_epi32_zero(adjusted);
  const auto scaled = static_cast<std::uint64_t>(positive) *
    static_cast<std::uint32_t>(d);
  const auto rounded = ((scaled >> 30) + 1) >> 2;
  return static_cast<std::uint16_t>(packus_epi32(rounded) >> shift);
}

template <class Pixel, class Operation>
void process_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  Operation operation
) {
  const auto pixel_max = std::numeric_limits<Pixel>::max();
  for (int j = 0; j < upbound; ++j) {
    const auto value = operation(cellptr[j], array_c[j], array_d[j], bit_depth);
    cellptr[j] = static_cast<Pixel>(std::min<std::uint32_t>(value, pixel_max));
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
  process_row(cellptr, upbound, array_c, array_d, bit_depth, add_simd_value);
}

void AddRowU16(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_row(cellptr, upbound, array_c, array_d, bit_depth, add_simd_value);
}

void EraseRowU8(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_row(cellptr, upbound, array_c, array_d, bit_depth, erase_simd_value);
}

void EraseRowU16(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_row(cellptr, upbound, array_c, array_d, bit_depth, erase_simd_value);
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
