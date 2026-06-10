#pragma once

#include "core/logo.h"

#include <vector>

namespace delogohd::core {

struct LogoImage {
  LOGO_HEADER header{};
  std::vector<LOGO_PIXEL> pixels;
};

LogoImage read_logo_file(const char* logofile, const char* logoname);

} // namespace delogohd::core
