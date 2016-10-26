#pragma once

#include "logo.h"
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace std;

class DelogoEngine {
protected:
  LOGO_HEADER logoheader;
  int* logo_yc, * logo_yd, * logo_uc, * logo_ud, * logo_vc, * logo_vd;
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
    convertLogo(data, mono);
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

  LOGO_PIXEL* shiftLogo(LOGO_PIXEL* data, int left, int top) {
    int oldleft = logoheader.x + left;
    int oldtop = logoheader.y + top;
    int neww = logoheader.w;
    int newh = logoheader.h;

    // TODO: support delogo from negative position
    if (oldleft < 0) oldleft = 0;
    if (oldtop  < 0) oldtop  = 0;

    // Align to mod16/mod2 boundary
    int padleft = oldleft % 16;
    int newleft = oldleft - padleft;
    neww += padleft;
    neww = neww + (16 - neww % 16) % 16;

    int padtop = oldtop % 2;
    int newtop = oldtop - padtop;
    newh += padtop;
    newh = newh + newh % 2;

    if (neww == logoheader.w && newh == logoheader.h)
      return data;

    LOGO_PIXEL* lgd = new LOGO_PIXEL[neww * newh]();
    for (int i = 0; i < logoheader.h; ++i) {
      memcpy(
        (void *)(lgd + padleft + (i + padtop) * neww),
        (void *)(data + i * logoheader.w),
        logoheader.w * sizeof(LOGO_PIXEL)
      );
    }
    delete data;
    logoheader.w = neww;
    logoheader.h = newh;
    logoheader.x = newleft;
    logoheader.y = newtop;
    return lgd;
  }

  void convertLogo(LOGO_PIXEL* data, bool mono) {
    // Y
    int size = logoheader.w * logoheader.h;
    logo_yc = new int[size];
    logo_yd = new int[size];
    for (int i = 0; i < size; i++) {
      if (data[i].dp_y < _cutoff) {
        data[i].dp_y = 0;
        data[i].dp_cb = 0;
        data[i].dp_cr = 0;
      }
      if (data[i].dp_y >= LOGO_MAX_DP)
        data[i].dp_y = LOGO_MAX_DP - 1;
      logo_yc[i] = AUYC2YC(data[i].y, data[i].dp_y);
      logo_yd[i] = (LOGO_MAX_DP - data[i].dp_y) << 2;
      if (mono) {
        data[i].cb = data[i].cr = 0;
        data[i].dp_cb = data[i].dp_cr = data[i].dp_y;
      }
    }

    // UV
    size >>= (_hsubsampling + _wsubsampling);
    int wstep = 1 << _wsubsampling;
    int hstep = 1 << _hsubsampling;
    logo_uc = new int[size];
    logo_ud = new int[size];
    logo_vc = new int[size];
    logo_vd = new int[size];
    for (int i = 0; i < logoheader.h; i += hstep) {
      for (int j = 0; j < logoheader.w; j += wstep) {
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

        int dstpos = (i * logoheader.w / hstep + j) / wstep;
        logo_ud[dstpos] = (LOGO_MAX_DP << 2) - (ud << (2 - _wsubsampling - _hsubsampling));
        logo_vd[dstpos] = (LOGO_MAX_DP << 2) - (vd << (2 - _wsubsampling - _hsubsampling));

        uc = uc / wstep / hstep;
        ud = ud / wstep / hstep;
        vc = vc / wstep / hstep;
        vd = vd / wstep / hstep;

        logo_uc[dstpos] = AUCC2CC(uc, ud);
        logo_vc[dstpos] = AUCC2CC(vc, vd);
      }
    }
  }

  // All pixels processed under 32 bit
  // 0x 00 00 DD DD for 16 bit input
  // 0x 00 00 DD 00 for 8 bit input (left shifted)
  // (DD * MAXDP + YC) / YDP
  // Right shift, then mm_store
  template <typename PIX>
  void processImage(PIX* ptr, int stride, int maxwidth, int maxheight, int plane) {
    int * array_c, * array_d;
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
    if (plane != PLANAR_Y) {
      logox >>= _wsubsampling;
      logow >>= _wsubsampling;
      logoy >>= _hsubsampling;
      logoh >>= _hsubsampling;
    }
    if (array_c == nullptr || array_d == nullptr)
      throw("Something wrong!");

    // TODO: check if left or top < 0
    // TODO: SIMD?

    PIX* rowptr = ptr + stride * logoy;
    int logorowpos = 0;
    for (int i = logoy; i < logoy + logoh && i < maxheight; i++) {
      PIX* cellptr = rowptr + logox;
      int logocellpos = logorowpos;
      for (int j = logox; j < logox + logow && j < maxwidth; j++) {
        int data = *cellptr;
        data <<= (16 - _ebpc);              // Left shift to 16 bit
        data = (data * LOGO_MAX_DP + array_c[logocellpos]) / array_d[logocellpos];
        data += 1 << (14 - _ebpc - 1);      // +0.5
        data >>= (14 - _ebpc);              // Right shift to original bitdepth
        *cellptr = Clamp(data);  // Saturate downscale
        cellptr++;
        logocellpos++;
      }
      rowptr += stride;
      logorowpos += logow;
    }
  }

  // Old algorithm: AUY2Y((AUY * MAXDP - C * DP) / (MAXDP - DP) + 0.5)
  // New algorithm: (Y * MAXDP - n * MAXDP - C * DP * m) / (MAXDP - DP) + n
  //                              \===== offset =====/     \ maxdp_dp /   \== bonus
  // Where m and n are parameters of AUYtoY, thus
  // m = 219/4096, n = 67584/4096
  // Y' = (Y * MAXDP + YC) / YDP
  int AUYC2YC(int auy_color, int auy_dp) {
    const double m = 219.0 / 4096.0;
    const double n = 16.5;
    double maxdp_dp = LOGO_MAX_DP - auy_dp;
    double offset = n * LOGO_MAX_DP + auy_color * auy_dp * m;
    double bonus = n * maxdp_dp;

    return static_cast<int>(round((bonus - offset) * 256.0));
  }

  int AUCC2CC(int auc_color, int auc_dp) {
    const double m = 7.0 / 128.0;
    const double n = 128.5;
    double maxdp_dp = LOGO_MAX_DP - auc_dp;
    double offset = n * LOGO_MAX_DP + auc_color * auc_dp * m;
    double bonus = n * maxdp_dp;

    return static_cast<int>(round((bonus - offset) * 256.0));
  }

  inline unsigned char Clamp(int n) {
    n = n>255 ? 255 : n;
    return n<0 ? 0 : n;
  }
};