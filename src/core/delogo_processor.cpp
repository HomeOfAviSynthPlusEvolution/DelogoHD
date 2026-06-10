#include "core/delogo_processor.hpp"

#include "core/canonical_math.hpp"
#include "core/reciprocal_math.hpp"
#include "core/row_kernel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <span>

namespace delogohd::core {

namespace {

constexpr double kOpaqueThreshold = 1.0 - 1e-2;

template <class Pixel>
void process_add_fade_row(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth,
  double opacity,
  RowKernelBackend backend
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const int depth = scaled_depth(depths[j], opacity);
    const int data = backend == RowKernelBackend::Scalar
      ? apply_add_alpha(row[j], colors[j], depth, bit_depth)
      : apply_add_alpha_reciprocal(row[j], colors[j], depth, bit_depth);
    row[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

template <class Pixel>
void process_erase_fade_row(
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth,
  double opacity,
  RowKernelBackend backend
) {
  const int pixel_max = (1 << bit_depth) - 1;
  for (std::size_t j = 0; j < row.size(); ++j) {
    const int depth = scaled_depth(depths[j], opacity);
    const int data = backend == RowKernelBackend::Scalar
      ? apply_erase_alpha(row[j], colors[j], depth, bit_depth)
      : apply_erase_alpha_reciprocal(row[j], colors[j], depth, bit_depth);
    row[j] = static_cast<Pixel>(std::min(data, pixel_max));
  }
}

template <class Pixel>
void process_opaque_row(
  LogoOperation operation,
  RowKernelBackend backend,
  std::span<Pixel> row,
  std::span<const int> colors,
  std::span<const int> depths,
  int bit_depth
) {
  if (backend == RowKernelBackend::Scalar) {
    if (operation == LogoOperation::Add) {
      process_add_row_c(row, colors, depths, bit_depth);
    } else {
      process_erase_row_c(row, colors, depths, bit_depth);
    }
    return;
  }

  if (operation == LogoOperation::Add) {
    process_add_row_hwy(row, colors, depths, bit_depth);
  } else {
    process_erase_row_hwy(row, colors, depths, bit_depth);
  }
}

template <class Pixel>
std::span<Pixel> pixel_row(
  ds::PlaneView2D<Pixel> pixels,
  int y,
  int x,
  int width
) {
  const auto first = static_cast<std::size_t>(x);
  const auto last = first + static_cast<std::size_t>(width);
  auto row = std::submdspan(
    pixels,
    static_cast<std::size_t>(y),
    std::pair{first, last}
  );
  return {row.data_handle(), row.extent(0)};
}

} // namespace

DelogoProcessor::DelogoProcessor(const DelogoProcessorConfig& config)
  : operation_(config.operation),
    backend_(config.backend),
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

  const int upbound = std::min(logow, plane.width - logox);
  const int row_count = std::min(logoh, plane.height - logoy);
  if (upbound <= 0 || row_count <= 0) {
    return;
  }

  auto pixels = ds::as_plane_view<Pixel>(plane);
  const auto row_width = static_cast<std::size_t>(upbound);

  if (opacity <= kOpaqueThreshold) {
    for (int i = 0; i < row_count; ++i) {
      auto row = pixel_row(pixels, logoy + i, logox, upbound);
      auto colors = coefficients.c_row(i).first(row_width);
      auto depths = coefficients.d_row(i).first(row_width);
      if (operation_ == LogoOperation::Add) {
        process_add_fade_row(
          row,
          colors,
          depths,
          bit_depth_,
          opacity,
          backend_
        );
      } else {
        process_erase_fade_row(
          row,
          colors,
          depths,
          bit_depth_,
          opacity,
          backend_
        );
      }
    }
    return;
  }

  for (int i = 0; i < row_count; ++i) {
    auto row = pixel_row(pixels, logoy + i, logox, upbound);
    process_opaque_row(
      operation_,
      backend_,
      row,
      coefficients.c_row(i).first(row_width),
      coefficients.d_row(i).first(row_width),
      bit_depth_
    );
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
