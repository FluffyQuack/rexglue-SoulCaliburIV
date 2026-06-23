// sciv - ReXGlue Recompiled Project
//
// See sciv_screenshot.h for the contract.

#include "sciv_screenshot.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/filesystem.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/ui/presenter.h>

#include <lodepng.h>

namespace sciv {

namespace {

// Writes a 32-bit RGBA .png via lodepng. The image is encoded to an in-memory
// buffer and written through rex::filesystem so file I/O and path handling match
// the rest of the runtime (rather than lodepng's own fopen). PNG (lossless,
// compressed) keeps captures small and avoids the BMP->PNG conversion step when
// comparing against reference captures.
bool WritePng32(const std::filesystem::path& path, const rex::ui::RawImage& img) {
  const uint32_t w = img.width;
  const uint32_t h = img.height;
  if (w == 0 || h == 0 || img.data.empty()) return false;

  // RawImage is R8 G8 B8 X8 in memory with rows spaced by `stride`. lodepng wants
  // a tightly packed RGBA buffer; copy per row (the last row is not guaranteed to
  // be padded to `stride`) and force the alpha to opaque.
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* src = img.data.data() + static_cast<size_t>(y) * img.stride;
    uint8_t* dst = rgba.data() + static_cast<size_t>(y) * w * 4u;
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t* p = src + static_cast<size_t>(x) * 4u;
      dst[0] = p[0];   // R
      dst[1] = p[1];   // G
      dst[2] = p[2];   // B
      dst[3] = 255;    // A (opaque)
      dst += 4;
    }
  }

  std::vector<uint8_t> png;
  const unsigned err = lodepng::encode(png, rgba.data(), w, h, LCT_RGBA, 8);
  if (err) {
    REXLOG_WARN("guest-capture: lodepng encode failed ({}): {}", err, lodepng_error_text(err));
    return false;
  }

  FILE* f = rex::filesystem::OpenFile(path, "wb");
  if (!f) return false;
  const size_t written = std::fwrite(png.data(), 1, png.size(), f);
  std::fclose(f);
  return written == png.size();
}

}  // namespace

void DumpGuestOutput(rex::Runtime* runtime) {
  if (!runtime) {
    REXLOG_WARN("guest-capture: no runtime");
    return;
  }

  auto* graphics_system =
      static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  rex::ui::Presenter* presenter = graphics_system ? graphics_system->presenter() : nullptr;
  if (!presenter) {
    REXLOG_WARN("guest-capture: no presenter available");
    return;
  }

  rex::ui::RawImage image;
  if (!presenter->CaptureGuestOutput(image)) {
    REXLOG_WARN("guest-capture: CaptureGuestOutput failed (no frame presented yet?)");
    return;
  }

  std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

  const std::filesystem::path path =
      rex::filesystem::GetExecutableFolder() /
      ("sciv_guest_" + std::string(stamp) + ".png");

  if (WritePng32(path, image)) {
    REXLOG_INFO("guest-capture: wrote {}x{} guest output to {}", image.width, image.height,
                path.string());
  } else {
    REXLOG_WARN("guest-capture: failed to write {}", path.string());
  }
}

}  // namespace sciv
