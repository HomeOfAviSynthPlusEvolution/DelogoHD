#pragma once

#include <climits>
#include "delogo_engine.hpp"

template <typename Interface>
class DelogoHDFilter: public Interface {

protected:
  int start, end;
  int fadein, fadeout;
  int left, top;
  bool mono;
  int cutoff;
  DelogoEngine* engine;

public:
  virtual const char* name() const { return "DelogoHD"; }
  virtual void initialize() {
    const char *logofile = ArgAsString(1, "logofile", NULL);
    const char *logoname = ArgAsString(2, "logoname", NULL);
    left    = ArgAsInt( 3, "left",    0);
    top     = ArgAsInt( 4, "top",     0);
    start   = ArgAsInt( 5, "start",   0);
    fadein  = ArgAsInt( 6, "fadein",  0);
    fadeout = ArgAsInt( 7, "fadeout", 0);
    end     = ArgAsInt( 8, "end",     INT_MAX);
    mono    = ArgAsBool(9, "mono",    0);
    cutoff  = ArgAsInt(10, "cutoff",  0);
    bit_per_channel = ArgAsInt(11, "input_depth", 8);
    byte_per_channel = bit_per_channel > 8 ? 2 : 1;

    // Check input
    if (!vi.HasVideo())
      throw("where's the video?");
    if (!(vi.IsYV12() || vi.IsYV16() || vi.IsYV24()))
      throw("only YV12, YV16, YV24 is supported.");
    if (width() & 15)
      throw("width is required to be mod-16");
    if (height() & 1)
      throw("height is required to be even");
    if (logofile == NULL)
      throw("where's the logo file?");
    engine = new DelogoEngine(logofile, logoname, depth(), ssw(), ssh(), left, top, mono, cutoff);
  }

  virtual auto get(int n) -> decltype(Interface::get(n)) {
    auto src = GetFrame(child, n);
    if (n < start || n > end)
      return src;
    if (left >= width(src, 0) || top >= height(src, 0))
      return src;
    double opacity = fade(n);
    if (opacity < 1e-2)
      return src;

    auto dst = Dup(src);
    engine->processImage(dst->GetWritePtr(PLANAR_Y), stride(dst, PLANAR_Y), width(dst, PLANAR_Y), height(dst, PLANAR_Y), PLANAR_Y, opacity);
    engine->processImage(dst->GetWritePtr(PLANAR_U), stride(dst, PLANAR_U), width(dst, PLANAR_U), height(dst, PLANAR_U), PLANAR_U, opacity);
    engine->processImage(dst->GetWritePtr(PLANAR_V), stride(dst, PLANAR_V), width(dst, PLANAR_V), height(dst, PLANAR_V), PLANAR_V, opacity);

    return dst;
  }

  double fade(int n) {
    if (n < start || (end < n && end >= start)) { // Out of frame range
      return 0;
    }
    if (n < start + fadein) // Fade in
      return (n - start + 1.0) / fadein;
    if (n > end - fadeout && end >= 0) // Fade out
      return (end - n + 1.0) / fadeout;

    return 1;
  }

public:
  using Interface::Interface;

};
