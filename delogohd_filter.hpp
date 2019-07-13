#pragma once

#include <climits>
#include "delogo_engine.hpp"

template <typename Interface, EOperation EOP>
class DelogoHDFilter: public Interface {

protected:
  int start, end;
  int fadein, fadeout;
  int left, top;
  bool mono;
  int cutoff;
  DelogoEngine<EOP>* engine;

public:
  virtual const char* name() const { return "DelogoHD"; }
  virtual void initialize() {
    const char *logofile = this->ArgAsString(1, "logofile", NULL);
    const char *logoname = this->ArgAsString(2, "logoname", NULL);
    left    = this->ArgAsInt( 3, "left",    0);
    top     = this->ArgAsInt( 4, "top",     0);
    start   = this->ArgAsInt( 5, "start",   0);
    end     = this->ArgAsInt( 6, "end",     INT_MAX);
    fadein  = this->ArgAsInt( 7, "fadein",  0);
    fadeout = this->ArgAsInt( 8, "fadeout", 0);
    mono    = this->ArgAsBool(9, "mono",    0);
    cutoff  = this->ArgAsInt(10, "cutoff",  0);
    this->bit_per_channel = this->vi.BitsPerComponent();
    this->byte_per_channel = this->bit_per_channel > 8 ? 2 : 1;

    // Check input
    if (!this->vi.HasVideo())
      throw("where's the video?");
    if (!this->supported_pixel())
      throw("pixel type is not supported");
    if (logofile == NULL)
      throw("where's the logo file?");
    engine = new DelogoEngine<EOP>(logofile, logoname, this->depth(), this->ssw(), this->ssh(), left, top, mono, cutoff);
  }

  virtual auto get(int n) -> decltype(Interface::get(n)) {
    auto src = this->GetFrame(this->child, n);
    if (n < start || n > end)
      return src;
    if (left >= this->width(src, 0) || top >= this->height(src, 0))
      return src;
    double opacity = fade(n);
    if (opacity < 1e-2)
      return src;

    auto dst = this->Dup(src);
    if (this->byte_per_channel == 1)
    {
      engine->template processImage<uint8_t>(dst->GetWritePtr(PLANAR_Y), this->stride(dst, PLANAR_Y), this->width(dst, PLANAR_Y), this->height(dst, PLANAR_Y), 0, opacity);
      engine->template processImage<uint8_t>(dst->GetWritePtr(PLANAR_U), this->stride(dst, PLANAR_U), this->width(dst, PLANAR_U), this->height(dst, PLANAR_U), 1, opacity);
      engine->template processImage<uint8_t>(dst->GetWritePtr(PLANAR_V), this->stride(dst, PLANAR_V), this->width(dst, PLANAR_V), this->height(dst, PLANAR_V), 2, opacity);
    }
    else
    {
      engine->template processImage<uint16_t>(dst->GetWritePtr(PLANAR_Y), this->stride(dst, PLANAR_Y), this->width(dst, PLANAR_Y), this->height(dst, PLANAR_Y), 0, opacity);
      engine->template processImage<uint16_t>(dst->GetWritePtr(PLANAR_U), this->stride(dst, PLANAR_U), this->width(dst, PLANAR_U), this->height(dst, PLANAR_U), 1, opacity);
      engine->template processImage<uint16_t>(dst->GetWritePtr(PLANAR_V), this->stride(dst, PLANAR_V), this->width(dst, PLANAR_V), this->height(dst, PLANAR_V), 2, opacity);

    }

    return dst;
  }

  double fade(int n) {
    if (n < start || (end < n && end >= start)) { // Out of frame range
      return 0;
    }
    if (n < start + fadein) // Fade in
      return (n - start + 0.5) / fadein;
    if (n > end - fadeout && end >= 0) // Fade out
      return (end - n + 0.5) / fadeout;

    return 1;
  }

public:
  using Interface::Interface;

};
