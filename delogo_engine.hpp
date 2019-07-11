#pragma once

#include "logo.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <intrin.h>

using namespace std;
#define MATH_MIN(a,b) ((a) > (b) ? (b) : (a))

// To support future AVX512, data needs be aligned.
// Logo is 16 bit so mod-32 pixel padding is required.
// Data is 8..16 bit so mod-64 pixel padding is required.
// YUV420 has subsampling so 128 pixel padding is required.

// However we are only supporting SSE4.1 right now.
#define MIN_MODULO 32

class DelogoEngine {
protected:
  LOGO_HEADER logoheader;
  // Store ((1<<30) / real_value) for aligned_Xd
  int** logo_yc, ** logo_yd, ** logo_uc, ** logo_ud, ** logo_vc, ** logo_vd;
  int _wsubsampling, _hsubsampling;
  int _ebpc, _cutoff;

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

public:
  DelogoEngine(const char* logofile, const char* logoname, int bitdepth, int wsubsampling, int hsubsampling, int left, int top, bool mono, int cutoff) :
    logo_yc(nullptr), logo_yd(nullptr),
    logo_uc(nullptr), logo_ud(nullptr),
    logo_vc(nullptr), logo_vd(nullptr),
    _wsubsampling(wsubsampling), _hsubsampling(hsubsampling),
    _ebpc(bitdepth), _cutoff(cutoff)
  {
    auto data = readLogo(logofile, logoname);
    data = shiftLogo(data, left, top);
    if (data)
      convertLogo(data, mono);
  }

  ~DelogoEngine(void)
  {
    for (int y = 0; y < logoheader.h; y++) {
      _aligned_free(logo_yc[y]);
      _aligned_free(logo_yd[y]);
    }
    int hstep = 1 << _hsubsampling;
    for (int i = 0; i < logoheader.h / hstep; i++) {
      _aligned_free(logo_uc[i]);
      _aligned_free(logo_ud[i]);
      _aligned_free(logo_vc[i]);
      _aligned_free(logo_vd[i]);
    }
    delete[] logo_yc;
    delete[] logo_yd;
    delete[] logo_uc;
    delete[] logo_ud;
    delete[] logo_vc;
    delete[] logo_vd;
  }

  LOGO_PIXEL* readLogo(const char* logofile, const char* logoname) {
    if (logofile == NULL) throw "logo file not specified.";
    FILE* lfp;
    fopen_s(&lfp, logofile, "rb");
    if (!lfp) throw "unable to open logo file, wrong file name?";
    fseek(lfp, 0, SEEK_END);
    size_t flen = ftell(lfp);
    if (flen < sizeof(LOGO_HEADER)+LOGO_FILE_HEADER_STR_SIZE)
      throw "too small for a logo file, wrong file?";

    // Read header
    LOGO_FILE_HEADER lfh;
    size_t rbytes;
    fseek(lfp, 0, SEEK_SET);
    rbytes = fread(&lfh, sizeof(LOGO_FILE_HEADER), 1, lfp);
    if (!rbytes)
      throw "failed to read from logo file, disk error?";

    // Loop to read logo data
    unsigned long num = SWAP_ENDIAN(lfh.logonum.l);
    unsigned long i;
    for (i = 0; i < num; i++) {
      rbytes = fread(&logoheader, sizeof(LOGO_HEADER), 1, lfp);
      if (!rbytes)
        throw "failed to read from logo file, disk error?";
      if (logoname == NULL || strcmp(logoname, logoheader.name) == 0)
        break; // We found our logo
      // Skip the data block if not match
      fseek(lfp, LOGO_PIXELSIZE(&logoheader), SEEK_CUR);
    }

    if (i == num) // So we couldn't find a match
      throw "unable to find a matching logo";

    // Now we can read it and return
    LOGO_PIXEL* lgd = new LOGO_PIXEL[logoheader.h * logoheader.w];
    if (lgd == NULL) throw "unable to allocate memory";
    fread(lgd, LOGO_PIXELSIZE(&logoheader), 1, lfp);
    fclose(lfp);
    return lgd;
  }

  // Normalize logo to mod(MIN_MODULO), and cut any invisible part
  LOGO_PIXEL* shiftLogo(LOGO_PIXEL* data, int left, int top) {
    int oldl = logoheader.x + left;
    int oldt = logoheader.y + top;

    // new left and top
    int newl = oldl;
    int newt = oldt;
    // new width and height
    int neww = logoheader.w;
    int newh = logoheader.h;
    // operation on top left corner of logo
    int padl = 0;
    int padt = 0;

    // 1. Find new left and left padding.
    // new left must be positive or zero.
    // left padding can be negative, which will cut the logo.

    if (oldl < 0) {
      newl = 0;
      padl = oldl;
    } else if ((padl = oldl % MIN_MODULO) != 0)
      newl = oldl - padl;

    // 2. Find new top and top padding.
    // vertical only needs mod-2

    if (oldt < 0) {
      newt = 0;
      padt = oldt;
    } else if ((padt = oldt % 2) != 0)
      newt = oldt + padt;

    // 3. Pad width
    neww += padl;
    neww += ((MIN_MODULO-1) & ~(neww-1));

    if (neww <= 0) {
      delete data;
      return NULL;
    }

    // 4. Pad height
    newh += padt;
    newh += newh % 2;

    if (newh <= 0) {
      delete data;
      return NULL;
    }

    // 5. Copying logo data
    if (newl == oldl && newt == oldt && neww == logoheader.w && newh == logoheader.h)
      return data;

    int src_h, dst_h, len_h;
    int src_v, dst_v, len_v;

    if (padl >= 0) {
      src_h = 0;
      dst_h = padl;
      len_h = logoheader.w;
    } else {
      src_h = -padl;
      dst_h = 0;
      len_h = logoheader.w + padl;
    }

    if (padt >= 0) {
      src_v = 0;
      dst_v = padt;
      len_v = logoheader.h;
    } else {
      src_v = -padt;
      dst_v = 0;
      len_v = logoheader.h + padt;
    }

    LOGO_PIXEL* lgd = new LOGO_PIXEL[neww * newh]();
    for (int i = 0; i < len_v; i++) {
      memcpy(
        (void *)(lgd + dst_h + (i + dst_v) * neww),
        (void *)(data + src_h + (i + src_v) * logoheader.w),
        len_h * sizeof(LOGO_PIXEL)
      );
    }

    // Done
    delete data;
    logoheader.w = neww;
    logoheader.h = newh;
    logoheader.x = newl;
    logoheader.y = newt;
    return lgd;
  }

  void convertLogo(LOGO_PIXEL* data, bool mono) {
    // Y
    logo_yc = new int*[logoheader.h];
    logo_yd = new int*[logoheader.h];
    for (int y = 0; y < logoheader.h; y++) {
      logo_yc[y] = (int*)_aligned_malloc(logoheader.w * sizeof(int), MIN_MODULO);
      logo_yd[y] = (int*)_aligned_malloc(logoheader.w * sizeof(int), MIN_MODULO);
      for (int x = 0; x < logoheader.w; x++) {
        int i = y * logoheader.w + x;
        if (data[i].dp_y < _cutoff) {
          data[i].dp_y = 0;
          data[i].dp_cb = 0;
          data[i].dp_cr = 0;
        }
        if (data[i].dp_y >= LOGO_MAX_DP)
          data[i].dp_y = LOGO_MAX_DP - 1;
        logo_yc[y][x] = AUYC2YC(data[i].y, data[i].dp_y);
        logo_yd[y][x] = (1<<28) / (LOGO_MAX_DP - data[i].dp_y);
        if (mono) {
          data[i].cb = data[i].cr = 0;
          data[i].dp_cb = data[i].dp_cr = data[i].dp_y;
        }
      }
    }

    // UV
    int wstep = 1 << _wsubsampling;
    int hstep = 1 << _hsubsampling;
    logo_uc = new int*[logoheader.h >> _hsubsampling];
    logo_ud = new int*[logoheader.h >> _hsubsampling];
    logo_vc = new int*[logoheader.h >> _hsubsampling];
    logo_vd = new int*[logoheader.h >> _hsubsampling];
    int aligned_w = logoheader.w >> _wsubsampling;
    for (int i = 0; i < logoheader.h; i += hstep) {
      int dstposy = i / hstep;
      logo_uc[dstposy] = (int*)_aligned_malloc(aligned_w * sizeof(int), MIN_MODULO);
      logo_ud[dstposy] = (int*)_aligned_malloc(aligned_w * sizeof(int), MIN_MODULO);
      logo_vc[dstposy] = (int*)_aligned_malloc(aligned_w * sizeof(int), MIN_MODULO);
      logo_vd[dstposy] = (int*)_aligned_malloc(aligned_w * sizeof(int), MIN_MODULO);

      for (int j = 0; j < logoheader.w; j += wstep) {
        int dstposx = j / wstep;
        int uc = data[i * logoheader.w + j].cb;
        int ud = data[i * logoheader.w + j].dp_cb;
        int vc = data[i * logoheader.w + j].cr;
        int vd = data[i * logoheader.w + j].dp_cr;
        if (_wsubsampling) {
          uc += data[i * logoheader.w + j + 1].cb;
          ud += data[i * logoheader.w + j + 1].dp_cb;
          vc += data[i * logoheader.w + j + 1].cr;
          vd += data[i * logoheader.w + j + 1].dp_cr;
        }
        if (_hsubsampling) {
          uc += data[(i+1) * logoheader.w + j].cb;
          ud += data[(i+1) * logoheader.w + j].dp_cb;
          vc += data[(i+1) * logoheader.w + j].cr;
          vd += data[(i+1) * logoheader.w + j].dp_cr;
        }
        if (_wsubsampling && _hsubsampling) {
          uc += data[(i+1) * logoheader.w + j + 1].cb;
          ud += data[(i+1) * logoheader.w + j + 1].dp_cb;
          vc += data[(i+1) * logoheader.w + j + 1].cr;
          vd += data[(i+1) * logoheader.w + j + 1].dp_cr;
        }

        logo_ud[dstposy][dstposx] = (1<<30) / ((LOGO_MAX_DP << 2) - (ud << (2 - _wsubsampling - _hsubsampling)));
        logo_vd[dstposy][dstposx] = (1<<30) / ((LOGO_MAX_DP << 2) - (vd << (2 - _wsubsampling - _hsubsampling)));

        uc = uc / wstep / hstep;
        ud = ud / wstep / hstep;
        vc = vc / wstep / hstep;
        vd = vd / wstep / hstep;

        logo_uc[dstposy][dstposx] = AUCC2CC(uc, ud);
        logo_vc[dstposy][dstposx] = AUCC2CC(vc, vd);
      }
    }
  }

  // All pixels processed under 32 bit
  // 0x 00 00 DD DD for 16 bit input
  // 0x 00 00 DD 00 for 8 bit input (left shifted)
  // (DD * MAXDP + YC) / YDP
  // Right shift, then mm_store
  template <typename PIX>
  void processImage(unsigned char* ptr, int stride, int maxwidth, int maxheight, int plane, double opacity) {
    int ** array_c, ** array_d;
    int logox = logoheader.x;
    int logoy = logoheader.y;
    int logow = logoheader.w;
    int logoh = logoheader.h;
    if (plane == PLANAR_Y) {
      array_c = logo_yc;
      array_d = logo_yd;
    } else if (plane == PLANAR_U) {
      array_c = logo_uc;
      array_d = logo_ud;
    } else {
      array_c = logo_vc;
      array_d = logo_vd;
    }
    // Assuming it's no-op
    if (array_c == nullptr && array_d == nullptr)
      return;

    if (array_c == nullptr || array_d == nullptr)
      throw("Something wrong!");

    if (plane != PLANAR_Y) {
      logox >>= _wsubsampling;
      logow >>= _wsubsampling;
      logoy >>= _hsubsampling;
      logoh >>= _hsubsampling;
    }

    unsigned char* rowptr = ptr + stride * logoy;

    // For SIMD
    int pixel_max = (1 << _ebpc) - 1;
    const int leftshift = (20 - _ebpc); // Left shift to 18 bit
    const int rightshift = (16 - _ebpc);

    for (int i = 0; i < logoh && i < maxheight - logoy; i++) {
      PIX* cellptr = (PIX*)(rowptr) + logox;
      int upbound = MATH_MIN(logow, maxwidth - logox);

      if (opacity <= 1 - 1e-2) {
        for (int j = 0; j < upbound; j++) {
          int data = cellptr[j];
          data <<= leftshift;
          int c = array_c[i][j];
          int d = (1<<30) / array_d[i][j];
          if (plane == PLANAR_Y)
            c = YC2FadeYC(c, d, opacity);
          else
            c = CC2FadeCC(c, d, opacity);
          d = static_cast<int>(LOGO_MAX_DP * 4 * (1 - opacity) + d * opacity);
          data = (data * LOGO_MAX_DP + c) / d; // Max at 30 bit
          if (data < 0) data = 0;
          data >>= (leftshift - 2);
          cellptr[j] = MATH_MIN(data, pixel_max);           // Saturate downscale
        }
      } else {
        #ifdef PURE_C
        for (int j = 0; j < upbound; j++) {
          int data = cellptr[j];
          data <<= leftshift;
          int c = array_c[i][j];
          int d = (1<<30) / array_d[i][j];
          data = (data * LOGO_MAX_DP + c) / d;
          if (data < 0) data = 0;
          data >>= (leftshift - 2);
          cellptr[j] = MATH_MIN(data, pixel_max);;
        }
        #else
        auto zero = _mm_setzero_si128();
        auto max_dp = _mm_set1_epi32(LOGO_MAX_DP);
        if (sizeof(PIX) == 1)
          for (int j = 0; j < upbound; j += 16) {
            auto data = _mm_stream_load_si128((__m128i *)(cellptr+j));

            auto data1_2 = _mm_unpacklo_epi8(data, zero);
            auto data2_2 = _mm_unpackhi_epi8(data, zero);

            auto data1_4 = _mm_unpacklo_epi16(data1_2, zero);
            auto data2_4 = _mm_unpackhi_epi16(data1_2, zero);
            auto data3_4 = _mm_unpacklo_epi16(data2_2, zero);
            auto data4_4 = _mm_unpackhi_epi16(data2_2, zero);

            __m128i data_c, data_d;

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j]));
            data1_4 = _mm_shift_multiply_add(data1_4, leftshift, max_dp, data_c);
            data1_4 = _mm_max_epi16(data1_4, zero);
            data1_4 = _mm_multiply_shift(data1_4, data_d, zero, rightshift);

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j+4]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j+4]));
            data2_4 = _mm_shift_multiply_add(data2_4, leftshift, max_dp, data_c);
            data2_4 = _mm_max_epi16(data2_4, zero);
            data2_4 = _mm_multiply_shift(data2_4, data_d, zero, rightshift);

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j+8]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j+8]));
            data3_4 = _mm_shift_multiply_add(data3_4, leftshift, max_dp, data_c);
            data3_4 = _mm_max_epi16(data3_4, zero);
            data3_4 = _mm_multiply_shift(data3_4, data_d, zero, rightshift);

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j+12]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j+12]));
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
              if (j + 0 < upbound) cellptr[j + 0] = static_cast<PIX>(_mm_extract_epi8(data,  8));
              if (j + 1 < upbound) cellptr[j + 1] = static_cast<PIX>(_mm_extract_epi8(data,  9));
              if (j + 2 < upbound) cellptr[j + 2] = static_cast<PIX>(_mm_extract_epi8(data, 10));
              if (j + 3 < upbound) cellptr[j + 3] = static_cast<PIX>(_mm_extract_epi8(data, 11));
              if (j + 4 < upbound) cellptr[j + 4] = static_cast<PIX>(_mm_extract_epi8(data, 12));
              if (j + 5 < upbound) cellptr[j + 5] = static_cast<PIX>(_mm_extract_epi8(data, 13));
              if (j + 6 < upbound) cellptr[j + 6] = static_cast<PIX>(_mm_extract_epi8(data, 14));
            }
          }
        else if (sizeof(PIX) == 2)
          for (int j = 0; j < upbound; j += 8) {
            auto data = _mm_stream_load_si128((__m128i *)(cellptr+j));

            auto data1_2 = _mm_unpacklo_epi16(data, zero);
            auto data2_2 = _mm_unpackhi_epi16(data, zero);

            __m128i data_c, data_d;

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j]));
            data1_2 = _mm_shift_multiply_add(data1_2, leftshift, max_dp, data_c);
            data1_2 = _mm_max_epi16(data1_2, zero);
            data1_2 = _mm_multiply_shift(data1_2, data_d, zero, rightshift);

            data_c = _mm_stream_load_si128((__m128i *)(&array_c[i][j+4]));
            data_d = _mm_stream_load_si128((__m128i *)(&array_d[i][j+4]));
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
              if (j + 0 < upbound) cellptr[j + 0] = static_cast<PIX>(_mm_extract_epi16(data, 4));
              if (j + 1 < upbound) cellptr[j + 1] = static_cast<PIX>(_mm_extract_epi16(data, 5));
              if (j + 2 < upbound) cellptr[j + 2] = static_cast<PIX>(_mm_extract_epi16(data, 6));
            }
          }
        #endif
      }

      rowptr += stride;
    }
  }

  // Old algorithm: AUY2Y((AUY * MAXDP - C * DP) / (MAXDP - DP) + 0.5)
  // New algorithm: (Y * MAXDP - n * MAXDP - C * DP * m) / (MAXDP - DP) + n
  //                              \===== offset =====/     \ maxdp_dp /   \== bonus
  // Where m and n are parameters of AUYtoY, thus
  // m = 219/4096, n = 67584/4096
  // Y' = (Y * MAXDP + YC) / YDP
  int AUYC2YC(int auy_color, int auy_dp) {
    // Computing in <<12
    const int m = 219;
    const int n = 67584;
    int maxdp_dp = LOGO_MAX_DP - auy_dp;
    int offset = n * LOGO_MAX_DP + auy_color * auy_dp * m;
    int bonus = n * maxdp_dp;

    return bonus - offset;
  }

  int YC2FadeYC(int y_color, int y_dp, double opacity) {
    // Computing in <<12
    const int m = 219;
    const int n = 67584;
    if (opacity > 1 - 1e-2)
      return y_color;
    if (opacity < 1e-2)
      return 0;

    int auy_dp = LOGO_MAX_DP - y_dp / 4;
    int bonus = n * y_dp / 4;
    int offset = bonus - y_color;
    int cpm = offset - n * LOGO_MAX_DP;
    cpm = static_cast<int>(cpm * opacity);

    offset = n * LOGO_MAX_DP + cpm;
    bonus = static_cast<int>(n * (LOGO_MAX_DP - auy_dp * opacity));

    return bonus - offset;
  }

  int AUCC2CC(int auc_color, int auc_dp) {
    // Computing in <<8, storing in <<12
    const int m = 14;
    const int n = 32896;
    int maxdp_dp = LOGO_MAX_DP - auc_dp;
    int offset = n * LOGO_MAX_DP + auc_color * auc_dp * m;
    int bonus = n * maxdp_dp;

    return (bonus - offset) * 16;
  }

  int CC2FadeCC(int c_color, int c_dp, double opacity) {
    // Computing in <<8, storing in <<12
    const int m = 14;
    const int n = 32896;
    if (opacity > 1 - 1e-2)
      return c_color;
    if (opacity < 1e-2)
      return 0;

    int auc_dp = LOGO_MAX_DP - c_dp / 4;
    int bonus = n * c_dp / 4;
    int offset = bonus - c_color / 16;
    int cpm = offset - n * LOGO_MAX_DP;
    cpm = static_cast<int>(cpm * opacity);

    offset = n * LOGO_MAX_DP + cpm;
    bonus = static_cast<int>(n * (LOGO_MAX_DP - auc_dp * opacity));

    return (bonus - offset) * 16;
  }
};
