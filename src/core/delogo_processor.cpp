#include "core/delogo_processor.hpp"

#include "core/row_kernel.hpp"

#include <algorithm>
#include <cstdint>

namespace delogohd::core {

namespace {

constexpr double kOpaqueThreshold = 1.0 - 1e-2;
constexpr double kTransparentThreshold = 1e-2;

int auyc_to_fade_yc(int y_color, int y_dp, double opacity) {
  const int n = 67584;
  if (opacity > kOpaqueThreshold) {
    return y_color;
  }
  if (opacity < kTransparentThreshold) {
    return 0;
  }

  const int auy_dp = LOGO_MAX_DP - y_dp / 4;
  const int bonus = n * y_dp / 4;
  const int offset = bonus - y_color;
  int cpm = offset - n * LOGO_MAX_DP;
  cpm = static_cast<int>(cpm * opacity);

  const int faded_offset = n * LOGO_MAX_DP + cpm;
  const int faded_bonus = static_cast<int>(n * (LOGO_MAX_DP - auy_dp * opacity));
  return faded_bonus - faded_offset;
}

int aucc_to_fade_cc(int c_color, int c_dp, double opacity) {
  const int n = 32896;
  if (opacity > kOpaqueThreshold) {
    return c_color;
  }
  if (opacity < kTransparentThreshold) {
    return 0;
  }

  const int auc_dp = LOGO_MAX_DP - c_dp / 4;
  const int bonus = n * c_dp / 4;
  const int offset = bonus - c_color / 16;
  int cpm = offset - n * LOGO_MAX_DP;
  cpm = static_cast<int>(cpm * opacity);

  const int faded_offset = n * LOGO_MAX_DP + cpm;
  const int faded_bonus = static_cast<int>(n * (LOGO_MAX_DP - auc_dp * opacity));
  return (faded_bonus - faded_offset) * 16;
}

template <class Pixel>
void process_add_fade_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  int plane,
  double opacity
) {
  const int pixel_max = (1 << bit_depth) - 1;
  const int shift = 18 - bit_depth;
  for (int j = 0; j < upbound; ++j) {
    int data = cellptr[j];
    data <<= shift;
    int c = array_c[j];
    int d = array_d[j];
    if (plane == 0) {
      c = auyc_to_fade_yc(c, d, opacity);
    } else {
      c = aucc_to_fade_cc(c, d, opacity);
    }
    d = static_cast<int>(LOGO_MAX_DP * 4 * (1 - opacity) + d * opacity);
    data = (data * d - c) / LOGO_MAX_DP;
    if (data < 0) {
      data = 0;
    }
    data >>= shift;
    data++;
    data >>= 2;
    cellptr[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

template <class Pixel>
void process_erase_fade_row(
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth,
  int plane,
  double opacity
) {
  const int pixel_max = (1 << bit_depth) - 1;
  const int shift = 20 - bit_depth;
  for (int j = 0; j < upbound; ++j) {
    int data = cellptr[j];
    data <<= shift;
    int c = array_c[j];
    int d = (1 << 30) / array_d[j];
    if (plane == 0) {
      c = auyc_to_fade_yc(c, d, opacity);
    } else {
      c = aucc_to_fade_cc(c, d, opacity);
    }
    d = static_cast<int>(LOGO_MAX_DP * 4 * (1 - opacity) + d * opacity);
    data = (data * LOGO_MAX_DP + c) / d;
    if (data < 0) {
      data = 0;
    }
    data >>= shift - 4;
    data++;
    data >>= 2;
    cellptr[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

template <class Pixel>
void process_opaque_row(
  LogoOperation operation,
  Pixel* cellptr,
  int upbound,
  const int* array_c,
  const int* array_d,
  int bit_depth
) {
#if defined(PURE_C)
  if (operation == LogoOperation::Add) {
    process_add_row_c(cellptr, upbound, array_c, array_d, bit_depth);
  } else {
    process_erase_row_c(cellptr, upbound, array_c, array_d, bit_depth);
  }
#else
  if (operation == LogoOperation::Add) {
    process_add_row_sse4(cellptr, upbound, array_c, array_d, bit_depth);
  } else {
    process_erase_row_sse4(cellptr, upbound, array_c, array_d, bit_depth);
  }
#endif
}

} // namespace

DelogoProcessor::DelogoProcessor(const DelogoProcessorConfig& config)
  : operation_(config.operation),
    bit_depth_(config.bit_depth),
    logo_(config) {}

const LOGO_HEADER& DelogoProcessor::source_header() const noexcept {
  return logo_.source_header();
}

void DelogoProcessor::process(ds::MutableVideoFrameView& frame, double opacity) {
  if (!logo_.active()) {
    return;
  }

  if (frame.format.sample_format == ds::SampleFormat::UInt8) {
    for (int plane = 0; plane < 3; ++plane) {
      process_plane<std::uint8_t>(frame.plane(plane), plane, opacity);
    }
  } else {
    for (int plane = 0; plane < 3; ++plane) {
      process_plane<std::uint16_t>(frame.plane(plane), plane, opacity);
    }
  }
}

template <class Pixel>
void DelogoProcessor::process_plane(
  ds::MutablePlaneView& plane,
  int plane_index,
  double opacity
) {
  const LogoPlaneCoefficients& coefficients = logo_.plane(plane_index);
  if (!coefficients.active()) {
    return;
  }

  int logox = logo_.logo_header().x;
  int logoy = logo_.logo_header().y;
  int logow = logo_.logo_header().w;
  int logoh = logo_.logo_header().h;
  if (plane_index != 0) {
    logox >>= logo_.subsampling_w();
    logow >>= logo_.subsampling_w();
    logoy >>= logo_.subsampling_h();
    logoh >>= logo_.subsampling_h();
  }

  auto* rowptr = static_cast<unsigned char*>(plane.data) +
    static_cast<std::ptrdiff_t>(plane.stride_bytes) * logoy;
  const int upbound = std::min(logow, plane.width - logox);

  if (opacity <= kOpaqueThreshold) {
    for (int i = 0; i < logoh && i < plane.height - logoy; ++i) {
      auto* cellptr = reinterpret_cast<Pixel*>(rowptr) + logox;
      if (operation_ == LogoOperation::Add) {
        process_add_fade_row(
          cellptr,
          upbound,
          coefficients.c_row(i),
          coefficients.d_row(i),
          bit_depth_,
          plane_index,
          opacity
        );
      } else {
        process_erase_fade_row(
          cellptr,
          upbound,
          coefficients.c_row(i),
          coefficients.d_row(i),
          bit_depth_,
          plane_index,
          opacity
        );
      }
      rowptr += plane.stride_bytes;
    }
    return;
  }

  for (int i = 0; i < logoh && i < plane.height - logoy; ++i) {
    auto* cellptr = reinterpret_cast<Pixel*>(rowptr) + logox;
    process_opaque_row(
      operation_,
      cellptr,
      upbound,
      coefficients.c_row(i),
      coefficients.d_row(i),
      bit_depth_
    );
    rowptr += plane.stride_bytes;
  }
}

template void DelogoProcessor::process_plane<std::uint8_t>(
  ds::MutablePlaneView& plane,
  int plane_index,
  double opacity
);

template void DelogoProcessor::process_plane<std::uint16_t>(
  ds::MutablePlaneView& plane,
  int plane_index,
  double opacity
);

} // namespace delogohd::core
