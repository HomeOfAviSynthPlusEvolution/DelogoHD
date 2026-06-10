#include "core/row_kernel.hpp"

#include "core/logo.h"

#if _MSC_VER
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

namespace delogohd::core {

namespace {

__m128i compute_add(
  const __m128i& data_p,
  int shift,
  const __m128i& zero,
  const __m128i& one,
  const __m128i& max_dp_shifted,
  const __m128i& data_c,
  const __m128i& data_d
) {
  const __m128i data_ps = _mm_slli_epi32(data_p, shift + 2);

  const __m128i data_hi_pxl = _mm_unpackhi_epi32(data_ps, zero);
  const __m128i data_hi_d = _mm_unpackhi_epi32(data_d, zero);
  const __m128i data_hi_c = _mm_unpackhi_epi32(data_c, zero);
  const __m128i data_hi_pmu = _mm_mul_epu32(data_hi_pxl, data_hi_d);
  const __m128i data_hi_pms = _mm_sub_epi32(data_hi_pmu, data_hi_c);
  const __m128i data_hi_pmz = _mm_max_epi32(data_hi_pms, zero);
  const __m128i data_hi_rtm = _mm_mul_epu32(data_hi_pmz, max_dp_shifted);
  const __m128i data_hi_rts = _mm_srli_epi64(data_hi_rtm, 33);
  const __m128i data_hi_rtM = _mm_add_epi64(data_hi_rts, one);
  const __m128i data_hi_rtS = _mm_srli_epi64(data_hi_rtM, 2);

  const __m128i data_lo_pxl = _mm_unpacklo_epi32(data_ps, zero);
  const __m128i data_lo_d = _mm_unpacklo_epi32(data_d, zero);
  const __m128i data_lo_c = _mm_unpacklo_epi32(data_c, zero);
  const __m128i data_lo_pmu = _mm_mul_epu32(data_lo_pxl, data_lo_d);
  const __m128i data_lo_pms = _mm_sub_epi32(data_lo_pmu, data_lo_c);
  const __m128i data_lo_pmz = _mm_max_epi32(data_lo_pms, zero);
  const __m128i data_lo_rtm = _mm_mul_epu32(data_lo_pmz, max_dp_shifted);
  const __m128i data_lo_rts = _mm_srli_epi64(data_lo_rtm, 33);
  const __m128i data_lo_rtM = _mm_add_epi64(data_lo_rts, one);
  const __m128i data_lo_rtS = _mm_srli_epi64(data_lo_rtM, 2);

  const __m128i result_epu16 = _mm_packus_epi32(data_lo_rtS, data_hi_rtS);
  return _mm_srli_epi32(result_epu16, shift);
}

__m128i compute_erase(
  const __m128i& data_p,
  int shift,
  const __m128i& zero,
  const __m128i& one,
  const __m128i& max_dp_shifted,
  const __m128i& data_c,
  const __m128i& data_d
) {
  const __m128i data_ps = _mm_slli_epi32(data_p, shift);

  const __m128i data_hi_pxl = _mm_unpackhi_epi32(data_ps, zero);
  const __m128i data_hi_d = _mm_unpackhi_epi32(data_d, zero);
  const __m128i data_hi_c = _mm_unpackhi_epi32(data_c, zero);
  const __m128i data_hi_pmu = _mm_mul_epu32(data_hi_pxl, max_dp_shifted);
  const __m128i data_hi_pma = _mm_add_epi32(data_hi_pmu, data_hi_c);
  const __m128i data_hi_pmz = _mm_max_epi32(data_hi_pma, zero);
  const __m128i data_hi_rtm = _mm_mul_epu32(data_hi_pmz, data_hi_d);
  const __m128i data_hi_rts = _mm_srli_epi64(data_hi_rtm, 30);
  const __m128i data_hi_rtM = _mm_add_epi64(data_hi_rts, one);
  const __m128i data_hi_rtS = _mm_srli_epi64(data_hi_rtM, 2);

  const __m128i data_lo_pxl = _mm_unpacklo_epi32(data_ps, zero);
  const __m128i data_lo_d = _mm_unpacklo_epi32(data_d, zero);
  const __m128i data_lo_c = _mm_unpacklo_epi32(data_c, zero);
  const __m128i data_lo_pmu = _mm_mul_epu32(data_lo_pxl, max_dp_shifted);
  const __m128i data_lo_pma = _mm_add_epi32(data_lo_pmu, data_lo_c);
  const __m128i data_lo_pmz = _mm_max_epi32(data_lo_pma, zero);
  const __m128i data_lo_rtm = _mm_mul_epu32(data_lo_pmz, data_lo_d);
  const __m128i data_lo_rts = _mm_srli_epi64(data_lo_rtm, 30);
  const __m128i data_lo_rtM = _mm_add_epi64(data_lo_rts, one);
  const __m128i data_lo_rtS = _mm_srli_epi64(data_lo_rtM, 2);

  const __m128i result_epu16 = _mm_packus_epi32(data_lo_rtS, data_hi_rtS);
  return _mm_srli_epi32(result_epu16, shift);
}

template <class Compute>
void process_u8(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  __m128i max_dp_shifted,
  Compute compute
) {
  const int shift = 16 - bit_depth;
  const __m128i zero = _mm_setzero_si128();
  const __m128i one = _mm_set1_epi64x(1);
  for (int j = 0; j < upbound; j += 16) {
    auto data = _mm_stream_load_si128(reinterpret_cast<__m128i*>(cellptr + j));

    auto data1_2 = _mm_unpacklo_epi8(data, zero);
    auto data2_2 = _mm_unpackhi_epi8(data, zero);

    auto data1_4 = _mm_unpacklo_epi16(data1_2, zero);
    auto data2_4 = _mm_unpackhi_epi16(data1_2, zero);
    auto data3_4 = _mm_unpacklo_epi16(data2_2, zero);
    auto data4_4 = _mm_unpackhi_epi16(data2_2, zero);

    auto data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j)));
    auto data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j)));
    data1_4 = compute(data1_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j + 4)));
    data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j + 4)));
    data2_4 = compute(data2_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j + 8)));
    data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j + 8)));
    data3_4 = compute(data3_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j + 12)));
    data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j + 12)));
    data4_4 = compute(data4_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data1_2 = _mm_packus_epi32(data1_4, data2_4);
    data2_2 = _mm_packus_epi32(data3_4, data4_4);
    data = _mm_packus_epi16(data1_2, data2_2);
    if (j + 16 <= upbound) {
      _mm_store_si128(reinterpret_cast<__m128i*>(cellptr + j), data);
    } else {
      if (j + 8 <= upbound) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(cellptr + j), data);
        j += 8;
      }
      if (j + 0 < upbound) cellptr[j + 0] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 8));
      if (j + 1 < upbound) cellptr[j + 1] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 9));
      if (j + 2 < upbound) cellptr[j + 2] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 10));
      if (j + 3 < upbound) cellptr[j + 3] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 11));
      if (j + 4 < upbound) cellptr[j + 4] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 12));
      if (j + 5 < upbound) cellptr[j + 5] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 13));
      if (j + 6 < upbound) cellptr[j + 6] = static_cast<std::uint8_t>(_mm_extract_epi8(data, 14));
    }
  }
}

template <class Compute>
void process_u16(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  __m128i max_dp_shifted,
  Compute compute
) {
  const int shift = 16 - bit_depth;
  const __m128i zero = _mm_setzero_si128();
  const __m128i one = _mm_set1_epi64x(1);
  for (int j = 0; j < upbound; j += 8) {
    auto data = _mm_stream_load_si128(reinterpret_cast<__m128i*>(cellptr + j));

    auto data1_2 = _mm_unpacklo_epi16(data, zero);
    auto data2_2 = _mm_unpackhi_epi16(data, zero);

    auto data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j)));
    auto data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j)));
    data1_2 = compute(data1_2, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_c + j + 4)));
    data_d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<int*>(array_d + j + 4)));
    data2_2 = compute(data2_2, shift, zero, one, max_dp_shifted, data_c, data_d);

    data = _mm_packus_epi32(data1_2, data2_2);
    if (j + 8 <= upbound) {
      _mm_store_si128(reinterpret_cast<__m128i*>(cellptr + j), data);
    } else {
      if (j + 4 <= upbound) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(cellptr + j), data);
        j += 4;
      }
      if (j + 0 < upbound) cellptr[j + 0] = static_cast<std::uint16_t>(_mm_extract_epi16(data, 4));
      if (j + 1 < upbound) cellptr[j + 1] = static_cast<std::uint16_t>(_mm_extract_epi16(data, 5));
      if (j + 2 < upbound) cellptr[j + 2] = static_cast<std::uint16_t>(_mm_extract_epi16(data, 6));
    }
  }
}

} // namespace

void process_add_row_sse4(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_u8(
    cellptr,
    upbound,
    array_c,
    array_d,
    bit_depth,
    _mm_set1_epi32((1u << 31) / LOGO_MAX_DP),
    compute_add
  );
}

void process_add_row_sse4(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_u16(
    cellptr,
    upbound,
    array_c,
    array_d,
    bit_depth,
    _mm_set1_epi32((1u << 31) / LOGO_MAX_DP),
    compute_add
  );
}

void process_erase_row_sse4(
  std::uint8_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_u8(
    cellptr,
    upbound,
    array_c,
    array_d,
    bit_depth,
    _mm_set1_epi32(LOGO_MAX_DP << 4),
    compute_erase
  );
}

void process_erase_row_sse4(
  std::uint16_t* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
  process_u16(
    cellptr,
    upbound,
    array_c,
    array_d,
    bit_depth,
    _mm_set1_epi32(LOGO_MAX_DP << 4),
    compute_erase
  );
}

} // namespace delogohd::core
