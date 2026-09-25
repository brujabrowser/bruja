#ifndef BLINK_HTML_RASTER_H_
#define BLINK_HTML_RASTER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace blink {

// Final HTML paint for UniLoader / WASMWebView.
//
// WASMSkia stays the compositor for chrome, GML, and RML. Blinker's HTML
// layout is incomplete — that is why foreign HTML uses HtmlRaster
// (Ultralight WebCore when linked). The existing paint pipeline remains
// the fallback so the tree still builds; we do not strip Skia.
struct HtmlFrame {
  bool ok = false;
  std::vector<uint8_t> rgba;  // unpremul RGBA8888
  std::string png;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string title;
  std::string error;
};

class HtmlRaster {
 public:
  static HtmlRaster& Get();

  // True when Ultralight WebCore is the HTML engine (not Blinker HTML layout).
  bool HasWebCore() const;

  HtmlFrame PaintHTML(const std::string& html, const std::string& base_url, uint32_t w,
                      uint32_t h);
  HtmlFrame PaintURL(const std::string& url, uint32_t w, uint32_t h);

  void FireClick(float x, float y);
  void FireKey(uint32_t vk, const std::string& text);
};

}  // namespace blink

#endif  // BLINK_HTML_RASTER_H_
