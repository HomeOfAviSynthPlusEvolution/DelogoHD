#pragma once

#include <cstdint>

namespace delogohd::core {

void process_add_row_c(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_add_row_c(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_erase_row_c(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_erase_row_c(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);

#if !defined(PURE_C)
void process_add_row_hwy(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_add_row_hwy(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_erase_row_hwy(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
void process_erase_row_hwy(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
);
#endif

} // namespace delogohd::core
