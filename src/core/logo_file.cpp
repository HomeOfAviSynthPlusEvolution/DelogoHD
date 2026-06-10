#include "core/logo_file.hpp"

#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>

namespace delogohd::core {

namespace {

[[noreturn]] void throw_logo_error(const char* message) {
  throw std::runtime_error(message);
}

template <class T>
void read_object(std::ifstream& file, T& value, const char* error) {
  file.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
  if (!file) {
    throw_logo_error(error);
  }
}

void read_bytes(std::ifstream& file, std::span<LOGO_PIXEL> pixels, const char* error) {
  auto bytes = std::as_writable_bytes(pixels);
  file.read(
    reinterpret_cast<char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size())
  );
  if (!file) {
    throw_logo_error(error);
  }
}

std::size_t pixel_count(const LOGO_HEADER& header) {
  if (header.h <= 0 || header.w <= 0) {
    throw_logo_error("invalid logo dimensions");
  }
  return static_cast<std::size_t>(header.h) * static_cast<std::size_t>(header.w);
}

} // namespace

LogoImage read_logo_file(const char* logofile, const char* logoname) {
  if (logofile == nullptr) {
    throw_logo_error("logo file not specified.");
  }

  std::ifstream file(logofile, std::ios::binary | std::ios::ate);
  if (!file) {
    throw_logo_error("unable to open logo file, wrong file name?");
  }

  const auto file_length = file.tellg();
  if (file_length < sizeof(LOGO_HEADER) + LOGO_FILE_HEADER_STR_SIZE) {
    throw_logo_error("too small for a logo file, wrong file?");
  }

  LOGO_FILE_HEADER file_header{};
  file.seekg(0, std::ios::beg);
  read_object(file, file_header, "failed to read from logo file, disk error?");

  const unsigned long logo_count = SWAP_ENDIAN(file_header.logonum.l);
  LOGO_HEADER logo_header{};
  unsigned long logo_index = 0;
  for (; logo_index < logo_count; ++logo_index) {
    read_object(file, logo_header, "failed to read from logo file, disk error?");
    if (logoname == nullptr || std::strcmp(logoname, logo_header.name) == 0) {
      break;
    }
    file.seekg(
      static_cast<std::streamoff>(pixel_count(logo_header) * sizeof(LOGO_PIXEL)),
      std::ios::cur
    );
    if (!file) {
      throw_logo_error("failed to read from logo file, disk error?");
    }
  }

  if (logo_index == logo_count) {
    throw_logo_error("unable to find a matching logo");
  }

  LogoImage image{};
  image.header = logo_header;
  image.pixels.resize(pixel_count(logo_header));
  read_bytes(file, image.pixels, "failed to read from logo file, disk error?");
  return image;
}

} // namespace delogohd::core
