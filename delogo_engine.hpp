#pragma once

#include "logo.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <assert.h>

using namespace std;
#define MATH_MIN(a,b) ((a) > (b) ? (b) : (a))

// To support future AVX512, data needs be aligned.
// Logo is 16 bit so mod-32 pixel padding is required.
// Data is 8..16 bit so mod-64 pixel padding is required.
// YUV420 has subsampling so 128 pixel padding is required.

// However we are only supporting SSE4.1 right now.
#define MIN_MODULO 32

enum EOperation { ADD_LOGO, ERASE_LOGO };

template <EOperation EOP>
class DelogoEngine {
protected:
  LOGO_HEADER logoheader;
  // Store ((1<<30) / real_value) for aligned_Xd
  int** logo_yc, ** logo_yd, ** logo_uc, ** logo_ud, ** logo_vc, ** logo_vd;
  int _wsubsampling, _hsubsampling;
  int _ebpc, _cutoff;

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

  LOGO_PIXEL* readLogo(const char* logofile, const char* logoname);
  LOGO_PIXEL* shiftLogo(LOGO_PIXEL* data, int left, int top);
  void convertLogo(LOGO_PIXEL* data, bool mono);

  template <typename PIX>
  void processImage(unsigned char* ptr, int stride, int maxwidth, int maxheight, int plane, double opacity);
  template <typename PIX>
  void realProcess(PIX* cellptr, int upbound, int* array_c, int* array_d);


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
    // const int m = 219;
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
    // const int m = 14;
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
