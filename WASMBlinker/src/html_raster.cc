#include "blink/html_raster.h"

#include "blink/html_parser.h"
#include "blink/paint.h"

#ifdef BLINK_HAS_ULTRALIGHT
namespace blink {
bool UltralightPaintHTML(const std::string& html, const std::string& base_url, uint32_t w,
                         uint32_t h, HtmlFrame* out);
bool UltralightPaintURL(const std::string& url, uint32_t w, uint32_t h, HtmlFrame* out);
void UltralightFireClick(float x, float y);
void UltralightFireKey(uint32_t vk, const std::string& text);
}  // namespace blink
#endif

namespace blink {
namespace {

#ifdef BLINK_HAS_PAINT_PIPELINE
HtmlFrame SkiaFallbackHTML(const std::string& html, const std::string& base_url, uint32_t w,
                           uint32_t h) {
  HtmlFrame out;
  HtmlDocument doc = ParseHtml(html);
  doc.base_url = base_url;
  RawFrameResult raw = CaptureRawFrame(doc, w, h);
  out.ok = raw.ok;
  out.rgba = std::move(raw.rgba);
  out.width = raw.width;
  out.height = raw.height;
  out.error = raw.error;
  out.title = doc.Title();
  PaintResult snap = PaintDocument(doc, w);
  if (snap.ok) out.png = std::move(snap.png);
  return out;
}
#else
HtmlFrame SkiaFallbackHTML(const std::string&, const std::string&, uint32_t, uint32_t) {
  HtmlFrame out;
  out.error = "no HTML raster (need Ultralight WebCore or WASMSkia paint)";
  return out;
}
#endif

}  // namespace

HtmlRaster& HtmlRaster::Get() {
  static HtmlRaster g;
  return g;
}

bool HtmlRaster::HasWebCore() const {
#ifdef BLINK_HAS_ULTRALIGHT
  return true;
#else
  return false;
#endif
}

HtmlFrame HtmlRaster::PaintHTML(const std::string& html, const std::string& base_url, uint32_t w,
                                uint32_t h) {
  if (w < 1) w = 800;
  if (h < 1) h = 600;
#ifdef BLINK_HAS_ULTRALIGHT
  HtmlFrame out;
  if (UltralightPaintHTML(html, base_url, w, h, &out)) return out;
#endif
  return SkiaFallbackHTML(html, base_url, w, h);
}

HtmlFrame HtmlRaster::PaintURL(const std::string& url, uint32_t w, uint32_t h) {
  if (w < 1) w = 800;
  if (h < 1) h = 600;
#ifdef BLINK_HAS_ULTRALIGHT
  HtmlFrame out;
  if (UltralightPaintURL(url, w, h, &out)) return out;
#endif
  HtmlFrame miss;
  miss.error = "PaintURL needs Ultralight WebCore";
  return miss;
}

void HtmlRaster::FireClick(float x, float y) {
#ifdef BLINK_HAS_ULTRALIGHT
  UltralightFireClick(x, y);
#else
  (void)x;
  (void)y;
#endif
}

void HtmlRaster::FireKey(uint32_t vk, const std::string& text) {
#ifdef BLINK_HAS_ULTRALIGHT
  UltralightFireKey(vk, text);
#else
  (void)vk;
  (void)text;
#endif
}

}  // namespace blink
