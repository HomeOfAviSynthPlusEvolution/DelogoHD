#include "delogo_engine.hpp"

// using namespace std;
template <EOperation EOP>
LOGO_PIXEL* DelogoEngine<EOP>::readLogo(const char* logofile, const char* logoname) {
  if (logofile == NULL) throw "logo file not specified.";
  FILE* lfp = fopen(logofile, "rb");
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

  src_logoheader = logoheader;

  // Now we can read it and return
  LOGO_PIXEL* lgd = new LOGO_PIXEL[logoheader.h * logoheader.w];
  if (lgd == NULL) throw "unable to allocate memory";
  fread(lgd, LOGO_PIXELSIZE(&logoheader), 1, lfp);
  fclose(lfp);
  return lgd;
}

// Normalize logo to mod(MIN_MODULO), and cut any invisible part
template <EOperation EOP>
LOGO_PIXEL* DelogoEngine<EOP>::shiftLogo(LOGO_PIXEL* data, int left, int top) {
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
    newt = oldt - padt;

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

template <EOperation EOP>
void DelogoEngine<EOP>::convertLogo(LOGO_PIXEL* data, bool mono) {
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
      if (EOP == ERASE_LOGO)
        logo_yd[y][x] = (1<<28) / (LOGO_MAX_DP - data[i].dp_y);
      else
        logo_yd[y][x] = (LOGO_MAX_DP - data[i].dp_y) << 2;
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

      if (EOP == ERASE_LOGO) {
        logo_ud[dstposy][dstposx] = (1<<30) / ((LOGO_MAX_DP << 2) - (ud << (2 - _wsubsampling - _hsubsampling)));
        logo_vd[dstposy][dstposx] = (1<<30) / ((LOGO_MAX_DP << 2) - (vd << (2 - _wsubsampling - _hsubsampling)));
      } else {
        logo_ud[dstposy][dstposx] = (LOGO_MAX_DP << 2) - (ud << (2 - _wsubsampling - _hsubsampling));
        logo_vd[dstposy][dstposx] = (LOGO_MAX_DP << 2) - (vd << (2 - _wsubsampling - _hsubsampling));
      }

      uc = uc / wstep / hstep;
      ud = ud / wstep / hstep;
      vc = vc / wstep / hstep;
      vd = vd / wstep / hstep;

      logo_uc[dstposy][dstposx] = AUCC2CC(uc, ud);
      logo_vc[dstposy][dstposx] = AUCC2CC(vc, vd);
    }
  }
}

template
LOGO_PIXEL* DelogoEngine<ADD_LOGO>::readLogo(const char* logofile, const char* logoname);
template
LOGO_PIXEL* DelogoEngine<ADD_LOGO>::shiftLogo(LOGO_PIXEL* data, int left, int top);
template
void DelogoEngine<ADD_LOGO>::convertLogo(LOGO_PIXEL* data, bool mono);

template
LOGO_PIXEL* DelogoEngine<ERASE_LOGO>::readLogo(const char* logofile, const char* logoname);
template
LOGO_PIXEL* DelogoEngine<ERASE_LOGO>::shiftLogo(LOGO_PIXEL* data, int left, int top);
template
void DelogoEngine<ERASE_LOGO>::convertLogo(LOGO_PIXEL* data, bool mono);
