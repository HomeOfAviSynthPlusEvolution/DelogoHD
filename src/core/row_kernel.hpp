#pragma once

#include <cstdint>
#include <span>

namespace delogohd::core {

void process_add_row_c(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);
void process_add_row_c(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);
void process_erase_row_c(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);
void process_erase_row_c(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);

void process_add_row_hwy(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);
void process_add_row_hwy(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
);
void process_erase_row_hwy(
  std::span<std::uint8_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
);
void process_erase_row_hwy(
  std::span<std::uint16_t> row,
  std::span<const int> colors,
  std::span<const int> depths,
  std::span<const std::uint32_t> reciprocals,
  int bit_depth
);

} // namespace delogohd::core
