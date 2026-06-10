#include "core/row_kernel.hpp"

#include "core/logo.h"

#include <algorithm>
#include <cstdint>

namespace delogohd::core {

namespace {

template <class Pixel>
void process_add_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  const int pixel_max = (1 << bit_depth) - 1;
  const int shift = 18 - bit_depth;
  for (int j = 0; j < upbound; ++j) {
    std::int64_t data = cellptr[j];
    data <<= shift;
    const std::int64_t c = array_c[j];
    const std::int64_t d = array_d[j];
    data = (data * d - c) / LOGO_MAX_DP;
    if (data < 0) {
      data = 0;
    }
    data >>= shift;
    data++;
    data >>= 2;
    cellptr[j] = static_cast<Pixel>(std::min<std::int64_t>(data, pixel_max));
  }
}

template <class Pixel>
void process_erase_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  const int pixel_max = (1 << bit_depth) - 1;
  const int shift = 20 - bit_depth;
  for (int j = 0; j < upbound; ++j) {
    std::int64_t data = cellptr[j];
    data <<= shift;
    const std::int64_t c = array_c[j];
    const std::int64_t d = (1 << 30) / array_d[j];
    data = (data * LOGO_MAX_DP + c) / d;
    if (data < 0) {
      data = 0;
    }
    data >>= shift - 4;
    data++;
    data >>= 2;
    cellptr[j] = static_cast<Pixel>(std::min<std::int64_t>(data, pixel_max));
  }
}

} // namespace

void process_add_row_c(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_add_row(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_add_row_c(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_add_row(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_erase_row_c(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_erase_row(cellptr, upbound, array_c, array_d, bit_depth);
}

void process_erase_row_c(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_erase_row(cellptr, upbound, array_c, array_d, bit_depth);
}

} // namespace delogohd::core
