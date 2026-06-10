#include "core/row_kernel.hpp"

#include "core/canonical_math.hpp"
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
  for (int j = 0; j < upbound; ++j) {
    const int data = apply_add_alpha(cellptr[j], array_c[j], array_d[j], bit_depth);
    cellptr[j] = static_cast<Pixel>(std::min(data, pixel_max));
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
  for (int j = 0; j < upbound; ++j) {
    const int data = apply_erase_alpha(cellptr[j], array_c[j], array_d[j], bit_depth);
    cellptr[j] = static_cast<Pixel>(std::min(data, pixel_max));
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
