#include "delogo_engine.hpp"

__m128i _simd_compute_add(const __m128i &data_p, const int shift, const __m128i &zero, const __m128i &one, const __m128i &max_dp_shifted, const __m128i &data_c, const __m128i &data_d) {
  // data = (data * d - c) * (1<<31 / LOGO_MAX_DP);

  const __m128i data_ps = _mm_slli_epi32(data_p, shift + 2); // shift to 18 bit

  const __m128i data_hi_pxl = _mm_unpackhi_epi32(data_ps, zero);
  const __m128i data_hi_d   = _mm_unpackhi_epi32(data_d, zero);
  const __m128i data_hi_c   = _mm_unpackhi_epi32(data_c, zero);
  const __m128i data_hi_pmu = _mm_mul_epu32(data_hi_pxl, data_hi_d);      // pmu = pxl * d
  const __m128i data_hi_pms = _mm_sub_epi32(data_hi_pmu, data_hi_c);      // pms = pmu - c
  const __m128i data_hi_pmz = _mm_max_epi32(data_hi_pms, zero);           // pmz = pms.saturate 0
  const __m128i data_hi_rtm = _mm_mul_epu32(data_hi_pmz, max_dp_shifted); // rtm = pms * d
  const __m128i data_hi_rts = _mm_srli_epi64(data_hi_rtm, 33);            // rts = rtm.shift-back -2
  const __m128i data_hi_rtM = _mm_add_epi64(data_hi_rts, one);            // rtM = trs.rounding
  const __m128i data_hi_rtS = _mm_srli_epi64(data_hi_rtM, 2);             // rts = rtm.shift-back

  const __m128i data_lo_pxl = _mm_unpacklo_epi32(data_ps, zero);
  const __m128i data_lo_d   = _mm_unpacklo_epi32(data_d, zero);
  const __m128i data_lo_c   = _mm_unpacklo_epi32(data_c, zero);
  const __m128i data_lo_pmu = _mm_mul_epu32(data_lo_pxl, data_lo_d);
  const __m128i data_lo_pms = _mm_sub_epi32(data_lo_pmu, data_lo_c);
  const __m128i data_lo_pmz = _mm_max_epi32(data_lo_pms, zero);
  const __m128i data_lo_rtm = _mm_mul_epu32(data_lo_pmz, max_dp_shifted);
  const __m128i data_lo_rts = _mm_srli_epi64(data_lo_rtm, 33);
  const __m128i data_lo_rtM = _mm_add_epi64(data_lo_rts, one);
  const __m128i data_lo_rtS = _mm_srli_epi64(data_lo_rtM, 2);

  // We are at 16 bit depth, so it's safe to do saturate unsigned int 16 pack, clamp it to 0~2^16
  const __m128i result_epu16 = _mm_packus_epi32(data_lo_rtS, data_hi_rtS);
  // Now shift it down to final bit depth
  const __m128i result_epu = _mm_srli_epi32(result_epu16, shift);

  #ifdef DEBUG
    int64_t data = data_p.m128i_i32[0];
    data <<= shift + 2;
    int64_t c = data_c.m128i_i32[0];
    int64_t d = data_d.m128i_i32[0];
    data = (data * d - c) / LOGO_MAX_DP;
    if (data < 0) data = 0;
    data >>= shift + 4;
    data = MATH_MIN(data, (1 << 8) - 1);
    if (fabs(data - result_epu.m128i_i32[0]) > 1)
      assert(data == result_epu.m128i_i32[0]);
  #endif

  return result_epu;
}

template<> template<>
void DelogoEngine<ADD_LOGO>::realProcess<uint8_t>(uint8_t* cellptr, int upbound, int* array_c, int* array_d) {
  const int shift = (16 - _ebpc);

  const __m128i zero = _mm_setzero_si128();
  const __m128i one = _mm_set1_epi64x(1);
  auto max_dp_shifted = _mm_set1_epi32((1u << 31) / LOGO_MAX_DP);
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
    data1_4 = _simd_compute_add(data1_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+4]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+4]));
    data2_4 = _simd_compute_add(data2_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+8]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+8]));
    data3_4 = _simd_compute_add(data3_4, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+12]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+12]));
    data4_4 = _simd_compute_add(data4_4, shift, zero, one, max_dp_shifted, data_c, data_d);

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

template<> template<>
void DelogoEngine<ADD_LOGO>::realProcess<uint16_t>(uint16_t* cellptr, int upbound, int* array_c, int* array_d) {
  const int shift = (16 - _ebpc);

  const __m128i zero = _mm_setzero_si128();
  const __m128i one = _mm_set1_epi64x(1);
  auto max_dp_shifted = _mm_set1_epi32((1u << 31) / LOGO_MAX_DP);
  for (int j = 0; j < upbound; j += 8) {
    auto data = _mm_stream_load_si128((__m128i *)(cellptr+j));

    auto data1_2 = _mm_unpacklo_epi16(data, zero);
    auto data2_2 = _mm_unpackhi_epi16(data, zero);

    __m128i data_c, data_d;

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j]));
    data1_2 = _simd_compute_add(data1_2, shift, zero, one, max_dp_shifted, data_c, data_d);

    data_c = _mm_stream_load_si128((__m128i *)(&array_c[j+4]));
    data_d = _mm_stream_load_si128((__m128i *)(&array_d[j+4]));
    data2_2 = _simd_compute_add(data2_2, shift, zero, one, max_dp_shifted, data_c, data_d);

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
