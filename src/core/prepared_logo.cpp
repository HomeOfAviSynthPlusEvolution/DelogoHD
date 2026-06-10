#include "core/prepared_logo.hpp"

#include "core/canonical_math.hpp"
#include "core/delogo_processor.hpp"

#include <algorithm>
#include <optional>
#include <span>

namespace delogohd::core {

namespace {

int chroma_depth_for_block(int sum_depth, int sample_count) {
  return clamp_logo_depth((sum_depth + sample_count / 2) / sample_count);
}

int chroma_color_for_block(std::int64_t weighted_color, int sum_depth) {
  if (sum_depth == 0) {
    return 0;
  }
  return round_divide_signed(weighted_color, sum_depth);
}

std::optional<LogoImage> shift_logo(LogoImage image, int left, int top) {
  const int oldl = image.header.x + left;
  const int oldt = image.header.y + top;

  int newl = oldl;
  int newt = oldt;
  int neww = image.header.w;
  int newh = image.header.h;
  int padl = 0;
  int padt = 0;

  if (oldl < 0) {
    newl = 0;
    padl = oldl;
  } else if ((padl = oldl % kMinModulo) != 0) {
    newl = oldl - padl;
  }

  if (oldt < 0) {
    newt = 0;
    padt = oldt;
  } else if ((padt = oldt % 2) != 0) {
    newt = oldt - padt;
  }

  neww += padl;
  neww += ((kMinModulo - 1) & ~(neww - 1));
  if (neww <= 0) {
    return std::nullopt;
  }

  newh += padt;
  newh += newh % 2;
  if (newh <= 0) {
    return std::nullopt;
  }

  if (
    newl == oldl &&
    newt == oldt &&
    neww == image.header.w &&
    newh == image.header.h
  ) {
    return image;
  }

  int src_h = 0;
  int dst_h = 0;
  int len_h = 0;
  if (padl >= 0) {
    src_h = 0;
    dst_h = padl;
    len_h = image.header.w;
  } else {
    src_h = -padl;
    dst_h = 0;
    len_h = image.header.w + padl;
  }

  int src_v = 0;
  int dst_v = 0;
  int len_v = 0;
  if (padt >= 0) {
    src_v = 0;
    dst_v = padt;
    len_v = image.header.h;
  } else {
    src_v = -padt;
    dst_v = 0;
    len_v = image.header.h + padt;
  }

  std::vector<LOGO_PIXEL> shifted(static_cast<std::size_t>(neww) * newh);
  auto src_pixels = std::span{image.pixels};
  auto dst_pixels = std::span{shifted};
  for (int y = 0; y < len_v; ++y) {
    const auto src_offset = src_h + static_cast<std::size_t>(y + src_v) * image.header.w;
    const auto dst_offset = dst_h + static_cast<std::size_t>(y + dst_v) * neww;
    auto src_row = src_pixels.subspan(src_offset, static_cast<std::size_t>(len_h));
    auto dst_row = dst_pixels.subspan(dst_offset, static_cast<std::size_t>(len_h));
    std::copy(src_row.begin(), src_row.end(), dst_row.begin());
  }

  image.header.w = static_cast<int16_t>(neww);
  image.header.h = static_cast<int16_t>(newh);
  image.header.x = static_cast<int16_t>(newl);
  image.header.y = static_cast<int16_t>(newt);
  image.pixels = std::move(shifted);
  return image;
}

} // namespace

void LogoPlaneCoefficients::reset(int width, int height) {
  width_ = width;
  height_ = height;
  c_.reset(static_cast<std::size_t>(width_) * height_);
  d_.reset(static_cast<std::size_t>(width_) * height_);
}

bool LogoPlaneCoefficients::active() const noexcept {
  return !c_.empty() && !d_.empty();
}

int LogoPlaneCoefficients::width() const noexcept {
  return width_;
}

int LogoPlaneCoefficients::height() const noexcept {
  return height_;
}

std::span<int> LogoPlaneCoefficients::c_row(int y) noexcept {
  return c_.subspan(
    static_cast<std::size_t>(y) * width_,
    static_cast<std::size_t>(width_)
  );
}

std::span<int> LogoPlaneCoefficients::d_row(int y) noexcept {
  return d_.subspan(
    static_cast<std::size_t>(y) * width_,
    static_cast<std::size_t>(width_)
  );
}

std::span<const int> LogoPlaneCoefficients::c_row(int y) const noexcept {
  return c_.subspan(
    static_cast<std::size_t>(y) * width_,
    static_cast<std::size_t>(width_)
  );
}

std::span<const int> LogoPlaneCoefficients::d_row(int y) const noexcept {
  return d_.subspan(
    static_cast<std::size_t>(y) * width_,
    static_cast<std::size_t>(width_)
  );
}

PreparedLogo::PreparedLogo(const DelogoProcessorConfig& config)
  : subsampling_w_(config.subsampling_w),
    subsampling_h_(config.subsampling_h),
    cutoff_(config.cutoff) {
  LogoImage image = read_logo_file(config.logofile, config.logoname);
  source_header_ = image.header;
  auto shifted = shift_logo(std::move(image), config.left, config.top);
  if (!shifted.has_value()) {
    logo_header_ = source_header_;
    return;
  }

  logo_header_ = shifted->header;
  convert(*shifted, config.mono);
  active_ = true;
}

bool PreparedLogo::active() const noexcept {
  return active_;
}

const LOGO_HEADER& PreparedLogo::source_header() const noexcept {
  return source_header_;
}

const LOGO_HEADER& PreparedLogo::logo_header() const noexcept {
  return logo_header_;
}

const LogoPlaneCoefficients& PreparedLogo::plane(int index) const noexcept {
  return planes_[index];
}

int PreparedLogo::subsampling_w() const noexcept {
  return subsampling_w_;
}

int PreparedLogo::subsampling_h() const noexcept {
  return subsampling_h_;
}

void PreparedLogo::convert(LogoImage& image, bool mono) {
  planes_[0].reset(logo_header_.w, logo_header_.h);
  for (int y = 0; y < logo_header_.h; ++y) {
    auto y_c = planes_[0].c_row(y);
    auto y_d = planes_[0].d_row(y);
    for (int x = 0; x < logo_header_.w; ++x) {
      const auto index = static_cast<std::size_t>(y) * logo_header_.w + x;
      LOGO_PIXEL& pixel = image.pixels[index];
      if (pixel.dp_y < cutoff_) {
        pixel.dp_y = 0;
        pixel.dp_cb = 0;
        pixel.dp_cr = 0;
      }
      pixel.dp_y = clamp_logo_depth(pixel.dp_y);
      y_c[x] = luma_to_internal_color(pixel.y);
      y_d[x] = pixel.dp_y;
      if (mono) {
        pixel.cb = 0;
        pixel.cr = 0;
        pixel.dp_cb = pixel.dp_y;
        pixel.dp_cr = pixel.dp_y;
      }
    }
  }

  const int wstep = 1 << subsampling_w_;
  const int hstep = 1 << subsampling_h_;
  const int chroma_width = logo_header_.w >> subsampling_w_;
  const int chroma_height = logo_header_.h >> subsampling_h_;
  planes_[1].reset(chroma_width, chroma_height);
  planes_[2].reset(chroma_width, chroma_height);

  for (int y = 0; y < logo_header_.h; y += hstep) {
    const int dst_y = y / hstep;
    auto u_c_row = planes_[1].c_row(dst_y);
    auto u_d_row = planes_[1].d_row(dst_y);
    auto v_c_row = planes_[2].c_row(dst_y);
    auto v_d_row = planes_[2].d_row(dst_y);

    for (int x = 0; x < logo_header_.w; x += wstep) {
      const int dst_x = x / wstep;
      int uc = 0;
      int ud = 0;
      int vc = 0;
      int vd = 0;
      std::int64_t weighted_uc = 0;
      std::int64_t weighted_vc = 0;
      int sample_count = 0;

      for (int by = 0; by < hstep; ++by) {
        for (int bx = 0; bx < wstep; ++bx) {
          const LOGO_PIXEL& pixel =
            image.pixels[static_cast<std::size_t>(y + by) * logo_header_.w + x + bx];
          const int sample_ud = clamp_logo_depth(pixel.dp_cb);
          const int sample_vd = clamp_logo_depth(pixel.dp_cr);
          ud += sample_ud;
          vd += sample_vd;
          weighted_uc += static_cast<std::int64_t>(pixel.cb) * sample_ud;
          weighted_vc += static_cast<std::int64_t>(pixel.cr) * sample_vd;
          ++sample_count;
        }
      }

      uc = chroma_color_for_block(weighted_uc, ud);
      vc = chroma_color_for_block(weighted_vc, vd);
      ud = chroma_depth_for_block(ud, sample_count);
      vd = chroma_depth_for_block(vd, sample_count);

      u_c_row[dst_x] = chroma_to_internal_color(uc);
      v_c_row[dst_x] = chroma_to_internal_color(vc);
      u_d_row[dst_x] = ud;
      v_d_row[dst_x] = vd;
    }
  }
}

} // namespace delogohd::core
