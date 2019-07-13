#include "delogo_engine.hpp"

using namespace std;
#define MATH_MIN(a,b) ((a) > (b) ? (b) : (a))

// (DD * MAXDP + YC) / YDP
template <typename PIX>
void DelogoEngine::processImage(unsigned char* ptr, int stride, int maxwidth, int maxheight, int plane, double opacity) {
  int ** array_c, ** array_d;
  int logox = logoheader.x;
  int logoy = logoheader.y;
  int logow = logoheader.w;
  int logoh = logoheader.h;
  if (plane == 0) {
    array_c = logo_yc;
    array_d = logo_yd;
  } else if (plane == 1) {
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

  if (plane != 0) {
    logox >>= _wsubsampling;
    logow >>= _wsubsampling;
    logoy >>= _hsubsampling;
    logoh >>= _hsubsampling;
  }

  unsigned char* rowptr = ptr + stride * logoy;

  const int pixel_max = (1 << _ebpc) - 1;
  const int shift = (20 - _ebpc);

  int upbound = MATH_MIN(logow, maxwidth - logox);

  if (opacity <= 1 - 1e-2) {
    for (int i = 0; i < logoh && i < maxheight - logoy; i++) {
      PIX* cellptr = (PIX*)(rowptr) + logox;

      for (int j = 0; j < upbound; j++) {
        int data = cellptr[j];
        data <<= shift;
        int c = array_c[i][j];
        int d = (1<<30) / array_d[i][j];
        if (plane == 0)
          c = YC2FadeYC(c, d, opacity);
        else
          c = CC2FadeCC(c, d, opacity);
        d = static_cast<int>(LOGO_MAX_DP * 4 * (1 - opacity) + d * opacity);
        data = (data * LOGO_MAX_DP + c) / d;
        if (data < 0) data = 0;
        data >>= shift - 4;
        data++;
        data >>= 2;
        cellptr[j] = MATH_MIN(data, pixel_max);
      }
      rowptr += stride;
    }
    return;
  }

  for (int i = 0; i < logoh && i < maxheight - logoy; i++) {
    PIX* cellptr = (PIX*)(rowptr) + logox;

    #ifdef PURE_C
    for (int j = 0; j < upbound; j++) {
      int64_t data = cellptr[j];
      data <<= shift;
      int64_t c = array_c[i][j];
      int64_t d = (1<<30) / array_d[i][j];
      data = (data * LOGO_MAX_DP + c) / d;
      if (data < 0) data = 0;
      data >>= shift - 4;
      data++;
      data >>= 2;
      cellptr[j] = MATH_MIN(data, pixel_max);
    }
    #else
    realProcess<PIX>(cellptr, upbound, array_c[i], array_d[i]);
    #endif

    rowptr += stride;
  }
}

template
void DelogoEngine::processImage<uint8_t>(unsigned char* ptr, int stride, int maxwidth, int maxheight, int plane, double opacity);

template
void DelogoEngine::processImage<uint16_t>(unsigned char* ptr, int stride, int maxwidth, int maxheight, int plane, double opacity);