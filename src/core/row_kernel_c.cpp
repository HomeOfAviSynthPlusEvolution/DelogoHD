#include "core/row_kernel.hpp"

#include "core/canonical_math.hpp"
#include "core/logo.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace delogohd::core {

namespace {

template <class Pixel>
void process_add_row(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const int data = apply_add_alpha(row[j], colors[j], depths[j], bit_depth);
    row[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

template <class Pixel>
void process_erase_row(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const int data = apply_erase_alpha(row[j], colors[j], depths[j], bit_depth);
    row[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

} // namespace

void process_add_row_c(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_add_row(row, colors, depths, bit_depth);
}

void process_add_row_c(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_add_row(row, colors, depths, bit_depth);
}

void process_erase_row_c(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_erase_row(row, colors, depths, bit_depth);
}

void process_erase_row_c(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  process_erase_row(row, colors, depths, bit_depth);
}

} // namespace delogohd::core
