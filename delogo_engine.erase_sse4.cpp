#include "delogo_engine.hpp"

// using namespace std;
// #define MATH_MIN(a,b) ((a) > (b) ? (b) : (a))

// To support future AVX512, data needs be aligned.
// Logo is 16 bit so mod-32 pixel padding is required.
// Data is 8..16 bit so mod-64 pixel padding is required.
// YUV420 has subsampling so 128 pixel padding is required.

// However we are only supporting SSE4.1 right now.
// #define MIN_MODULO 32

__m128i _mm_shift_multiply_add(const __m128i data_p, const int leftshift, const __m128i &max_dp, const __m128i &data_c) {
  // data <<= leftshift;
  // data = (data * LOGO_MAX_DP + c);
  __m128i data_ps = _mm_slli_epi32(data_p, leftshift);
  data_ps = _mm_mullo_epi32(data_ps, max_dp);
  data_ps = _mm_add_epi32(data_ps, data_c);
  return data_ps;
}

__m128i _mm_multiply_shift(const __m128i data_p, const __m128i data_d, const __m128i &zero, const int rightshift) {
  // We need to right shift 32 bits to restore it to 16 bit number, clamp it
  // Then shift it down to final bit depth

  const __m128i data_ph = _mm_unpackhi_epi32(data_p, zero);
  const __m128i data_dh = _mm_unpackhi_epi32(data_d, zero);
  const __m128i data_rh = _mm_mul_epu32(data_ph, data_dh);
  const __m128i data_rhs = _mm_srli_epi64(data_rh, 32);

  const __m128i data_pl = _mm_unpacklo_epi32(data_p, zero);
  const __m128i data_dl = _mm_unpacklo_epi32(data_d, zero);
  const __m128i data_rl = _mm_mul_epu32(data_pl, data_dl);
  const __m128i data_rls = _mm_srli_epi64(data_rl, 32);

  // We are at 16 bit depth, so it's safe to do saturate unsigned int 16 pack, clamp it to 0~2^16
  const __m128i result_epu32 = _mm_packus_epi32(data_rls, data_rhs);
  // Now shift it down to final bit depth
  const __m128i result_epuXX = _mm_srli_epi32(result_epu32, rightshift);
  return result_epuXX;
}

// (DD * MAXDP + YC) / YDP
template <>
void DelogoEngine::realProcess<uint8_t>(uint8_t* cellptr, int upbound, int* array_c, int* array_d) {
  const int leftshift = (20 - _ebpc); // Left shift to 18 bit
  const int rightshift = (16 - _ebpc);

  auto zero = _mm_setzero_si128();
  auto max_dp = _mm_set1_epi32(LOGO_MAX_DP);
  for (int j = 0; j < upbound; j += 16) {
    auto data = _mm_stream_load_si128((__m128i *)(cellptr+j));

    auto data1_2 = _mm_unpacklo_epi8(data, zero);
    auto data2_2 = _mm_unpackhi_epi8(data, zero);

    auto data1_4 = _mm_unpacklo_epi16(data1_2, zero);
    auto data2_4 = _mm_unpackhi_epi16(data1_2, zero);
    auto data3_4 = _mm_unpacklo_epi16(data2_2, zero);
    auto data4_4 = _mm_unpackhi_epi16(data2_2, zero);

    __m128i data_c, data_d;

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j]));
    data1_4 = _mm_shift_multiply_add(data1_4, leftshift, max_dp, data_c);
    data1_4 = _mm_max_epi16(data1_4, zero);
    data1_4 = _mm_multiply_shift(data1_4, data_d, zero, rightshift);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+4]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+4]));
    data2_4 = _mm_shift_multiply_add(data2_4, leftshift, max_dp, data_c);
    data2_4 = _mm_max_epi16(data2_4, zero);
    data2_4 = _mm_multiply_shift(data2_4, data_d, zero, rightshift);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+8]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+8]));
    data3_4 = _mm_shift_multiply_add(data3_4, leftshift, max_dp, data_c);
    data3_4 = _mm_max_epi16(data3_4, zero);
    data3_4 = _mm_multiply_shift(data3_4, data_d, zero, rightshift);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+12]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+12]));
    data4_4 = _mm_shift_multiply_add(data4_4, leftshift, max_dp, data_c);
    data4_4 = _mm_max_epi16(data4_4, zero);
    data4_4 = _mm_multiply_shift(data4_4, data_d, zero, rightshift);

    data1_2 = _mm_packus_epi32(data1_4, data2_4);
    data2_2 = _mm_packus_epi32(data3_4, data4_4);
    data = _mm_packus_epi16(data1_2, data2_2);
    if (j + 16 <= upbound)
      _mm_store_si128((__m128i *)(cellptr+j), data);
    else {
      if (j + 8 <= upbound) {
        _mm_storel_epi64((__m128i *)(cellptr+j), data);
        j += 8;
      }
      if (j + 0 < upbound) cellptr[j + 0] = static_cast<uint8_t>(_mm_extract_epi8(data,  8));
      if (j + 1 < upbound) cellptr[j + 1] = static_cast<uint8_t>(_mm_extract_epi8(data,  9));
      if (j + 2 < upbound) cellptr[j + 2] = static_cast<uint8_t>(_mm_extract_epi8(data, 10));
      if (j + 3 < upbound) cellptr[j + 3] = static_cast<uint8_t>(_mm_extract_epi8(data, 11));
      if (j + 4 < upbound) cellptr[j + 4] = static_cast<uint8_t>(_mm_extract_epi8(data, 12));
      if (j + 5 < upbound) cellptr[j + 5] = static_cast<uint8_t>(_mm_extract_epi8(data, 13));
      if (j + 6 < upbound) cellptr[j + 6] = static_cast<uint8_t>(_mm_extract_epi8(data, 14));
    }
  }
}

template <>
void DelogoEngine::realProcess<uint16_t>(uint16_t* cellptr, int upbound, int* array_c, int* array_d) {
  const int leftshift = (20 - _ebpc); // Left shift to 18 bit
  const int rightshift = (16 - _ebpc);

  auto zero = _mm_setzero_si128();
  auto max_dp = _mm_set1_epi32(LOGO_MAX_DP);
  for (int j = 0; j < upbound; j += 8) {
    auto data = _mm_stream_load_si128((__m128i *)(cellptr+j));

    auto data1_2 = _mm_unpacklo_epi16(data, zero);
    auto data2_2 = _mm_unpackhi_epi16(data, zero);

    __m128i data_c, data_d;

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j]));
    data1_2 = _mm_shift_multiply_add(data1_2, leftshift, max_dp, data_c);
    data1_2 = _mm_max_epi16(data1_2, zero);
    data1_2 = _mm_multiply_shift(data1_2, data_d, zero, rightshift);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+4]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+4]));
    data2_2 = _mm_shift_multiply_add(data2_2, leftshift, max_dp, data_c);
    data2_2 = _mm_max_epi16(data2_2, zero);
    data2_2 = _mm_multiply_shift(data2_2, data_d, zero, rightshift);

    data = _mm_packus_epi32(data1_2, data2_2);
    if (j + 8 <= upbound)
      _mm_store_si128((__m128i *)(cellptr+j), data);
    else {
      if (j + 4 <= upbound) {
        _mm_storel_epi64((__m128i *)(cellptr+j), data);
        j += 4;
      }
      if (j + 0 < upbound) cellptr[j + 0] = static_cast<uint16_t>(_mm_extract_epi16(data, 4));
      if (j + 1 < upbound) cellptr[j + 1] = static_cast<uint16_t>(_mm_extract_epi16(data, 5));
      if (j + 2 < upbound) cellptr[j + 2] = static_cast<uint16_t>(_mm_extract_epi16(data, 6));
    }
  }
}
