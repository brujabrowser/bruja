#include "blink/web_page.h"

#include "blink/css.h"
#include "blink/html_raster.h"
#include "blink/local_frame_impl.h"
#include "blink/url.h"

#ifdef BLINK_HAS_PAINT_PIPELINE
#include "blink/paint.h"
#endif

namespace blink {
namespace {

uint32_t g_web_vw = 800;
LocalFrameImpl* g_frame = nullptr;

LocalFrameImpl& PageFrame() {
  if (!g_frame) g_frame = new LocalFrameImpl("unused", 0, "web-page");
  return *g_frame;
}

}  // namespace

std::string RunWebPageDocument(const std::string& html, float viewport_w, float viewport_h) {
  if (html.empty()) return {};
  if (viewport_w < 1.0f) viewport_w = 800.0f;
  if (viewport_h < 1.0f) viewport_h = 600.0f;
  LocalFrameImpl& frame = PageFrame();
  frame.SetViewport(static_cast<uint32_t>(viewport_w), static_cast<uint32_t>(viewport_h));
  bool ok = false;
  frame.LoadHTML(html, [&](bool o, std::string, uint32_t, uint32_t, std::string) { ok = o; });
  if (!ok) return {};
#ifdef BLINK_HAS_DOM_BINDINGS
  std::string live = frame.LiveHtml();
  if (!live.empty()) return live;
#endif
  return SerializeHtml(frame.document());
}

WebPagePaint LoadAndPaintWebPage(const std::string& html, float viewport_w, float viewport_h) {
  WebPagePaint out;
  if (html.empty()) {
    out.error = "empty html";
    return out;
  }
  if (viewport_w < 1.0f) viewport_w = 800.0f;
  if (viewport_h < 1.0f) viewport_h = 600.0f;
  LocalFrameImpl& frame = PageFrame();
  g_web_vw = static_cast<uint32_t>(viewport_w);
  frame.SetViewport(g_web_vw, static_cast<uint32_t>(viewport_h));
  if (HtmlRaster::Get().HasWebCore()) {
    HtmlFrame painted = HtmlRaster::Get().PaintHTML(html, "", g_web_vw,
                                                    static_cast<uint32_t>(viewport_h));
    if (painted.ok && !painted.png.empty()) {
      out.ok = true;
      out.png = std::move(painted.png);
      out.width = painted.width ? painted.width : g_web_vw;
      out.height = painted.height ? painted.height : static_cast<uint32_t>(viewport_h);
      frame.LoadHTML(html, [&](bool, std::string, uint32_t, uint32_t, std::string) {});
      return out;
    }
    out.error = painted.error.empty() ? "ultralight paint failed" : painted.error;
    return out;
  }
  bool loaded = false;
  std::string load_err;
  frame.LoadHTML(html, [&](bool o, std::string, uint32_t, uint32_t, std::string e) {
    loaded = o;
    load_err = std::move(e);
  });
  if (!loaded) {
    out.error = load_err.empty() ? "LoadHTML failed" : load_err;
    return out;
  }
#ifdef BLINK_HAS_PAINT_PIPELINE
  bool painted = false;
  frame.CaptureSnapshot([&](bool o, std::string png, std::string e) {
    painted = o;
    out.png = std::move(png);
    out.error = std::move(e);
  });
  out.ok = painted && !out.png.empty();
  out.width = static_cast<uint32_t>(viewport_w);
  out.height = static_cast<uint32_t>(viewport_h);
  if (!out.ok && out.error.empty()) out.error = "paint failed";
#else
  (void)viewport_h;
  out.error = "no paint pipeline";
#endif
  return out;
}

WebPageHit HitTestWebPage(float x, float y) {
  WebPageHit hit;
  if (!g_frame) return hit;
  const HtmlDocument& doc = g_frame->document();
  if (!doc.root) return hit;
#ifdef BLINK_HAS_PAINT_PIPELINE
  const Node* n = HitTestDocument(doc, g_web_vw, x, y);
  for (const Node* cur = n; cur; cur = cur->parent) {
    if (cur->type != NodeType::kElement) continue;
    if (hit.tag.empty()) hit.tag = cur->tag_name;
    std::string gk = cur->GetAttribute("gk-click");
    if (gk.empty()) gk = cur->GetAttribute("gk-action");
    if (!gk.empty() && hit.gk_click.empty()) hit.gk_click = gk;
    std::string href = cur->GetAttribute("href");
    if (href.empty()) href = cur->GetAttribute("data-url");
    if (!href.empty() && hit.href.empty()) {
      hit.href = doc.base_url.empty() ? href : ResolveUrl(doc.base_url, href);
    }
    if (!hit.href.empty() || !hit.gk_click.empty()) {
      hit.ok = true;
      break;
    }
  }
#else
  (void)x;
  (void)y;
#endif
  return hit;
}

}  // namespace blink
