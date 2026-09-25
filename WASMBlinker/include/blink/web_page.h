#ifndef BLINK_WEB_PAGE_H_
#define BLINK_WEB_PAGE_H_

#include <cstdint>
#include <string>

namespace blink {

// Load HTML on the process page LocalFrame (classic scripts + bruja_dom when
// enabled), cascade author CSS, return serialized HTML. Used by after-Lime
// to sync live DOM into RmlUi #page-shadow (WebGPU paint stays downstream).
std::string RunWebPageDocument(const std::string& html, float viewport_w, float viewport_h);

// PNG snapshot of a page after JS: the same LocalFrame LoadHTML + WASMSkia.
// One frame for the process — do not construct another. HitTestWebPage maps
// clicks on the bitmap through that frame's document.
struct WebPagePaint {
  bool ok = false;
  std::string png;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string error;
};

struct WebPageHit {
  bool ok = false;
  std::string href;
  std::string gk_click;
  std::string tag;
};

WebPagePaint LoadAndPaintWebPage(const std::string& html, float viewport_w, float viewport_h);
WebPageHit HitTestWebPage(float x, float y);

}  // namespace blink

#endif
