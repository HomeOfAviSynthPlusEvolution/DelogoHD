#include "core/logo_file.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace delogohd::core {

namespace {

struct FileCloser {
  void operator()(std::FILE* file) const noexcept {
    if (file) {
      std::fclose(file);
    }
  }
};

} // namespace

LogoImage read_logo_file(const char* logofile, const char* logoname) {
  if (logofile == nullptr) {
    throw "logo file not specified.";
  }

  std::unique_ptr<std::FILE, FileCloser> file(std::fopen(logofile, "rb"));
  if (!file) {
    throw "unable to open logo file, wrong file name?";
  }

  std::fseek(file.get(), 0, SEEK_END);
  const auto file_length = static_cast<std::size_t>(std::ftell(file.get()));
  if (file_length < sizeof(LOGO_HEADER) + LOGO_FILE_HEADER_STR_SIZE) {
    throw "too small for a logo file, wrong file?";
  }

  LOGO_FILE_HEADER file_header{};
  std::fseek(file.get(), 0, SEEK_SET);
  if (!std::fread(&file_header, sizeof(LOGO_FILE_HEADER), 1, file.get())) {
    throw "failed to read from logo file, disk error?";
  }

  const unsigned long logo_count = SWAP_ENDIAN(file_header.logonum.l);
  LOGO_HEADER logo_header{};
  unsigned long logo_index = 0;
  for (; logo_index < logo_count; ++logo_index) {
    if (!std::fread(&logo_header, sizeof(LOGO_HEADER), 1, file.get())) {
      throw "failed to read from logo file, disk error?";
    }
    if (logoname == nullptr || std::strcmp(logoname, logo_header.name) == 0) {
      break;
    }
    std::fseek(file.get(), LOGO_PIXELSIZE(&logo_header), SEEK_CUR);
  }

  if (logo_index == logo_count) {
    throw "unable to find a matching logo";
  }

  LogoImage image{};
  image.header = logo_header;
  image.pixels.resize(static_cast<std::size_t>(logo_header.h) * logo_header.w);
  std::fread(image.pixels.data(), LOGO_PIXELSIZE(&logo_header), 1, file.get());
  return image;
}

} // namespace delogohd::core
