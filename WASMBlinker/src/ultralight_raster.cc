#ifdef BLINK_HAS_ULTRALIGHT

#include "blink/html_raster.h"

#include <AppCore/Platform.h>
#include <Ultralight/Ultralight.h>

#include <memory>
#include <string>

#ifdef BLINK_HAS_PAINT_PIPELINE
#include "rasta/rasta.h"
#endif

namespace blink {
namespace {

using namespace ultralight;

struct UltralightSession {
  RefPtr<Renderer> renderer;
  RefPtr<View> view;
  uint32_t w = 0;
  uint32_t h = 0;
  bool ready = false;

  bool Ensure(uint32_t width, uint32_t height) {
    if (!renderer) {
      Config cfg;
      Platform::instance().set_config(cfg);
      Platform::instance().set_font_loader(GetPlatformFontLoader());
      Platform::instance().set_file_system(GetPlatformFileSystem("."));
      renderer = Renderer::Create();
      if (!renderer) return false;
    }
    if (view && w == width && h == height) return true;
    ViewConfig vc;
    vc.is_accelerated = false;
    vc.is_transparent = false;
    view = renderer->CreateView(width, height, vc, nullptr);
    w = width;
    h = height;
    return view.get() != nullptr;
  }

  void Pump(int frames = 48) {
    ready = false;
    for (int i = 0; i < frames; ++i) {
      renderer->Update();
      renderer->Render();
    }
    ready = true;
  }

  bool Copy(HtmlFrame* out) {
    if (!view || !out) return false;
    Surface* surface = view->surface();
    if (!surface) {
      out->error = "ultralight: no surface";
      return false;
    }
    BitmapSurface* bits = static_cast<BitmapSurface*>(surface);
    RefPtr<Bitmap> bmp = bits->bitmap();
    if (!bmp) {
      out->error = "ultralight: no bitmap";
      return false;
    }
    const uint32_t bw = bmp->width();
    const uint32_t bh = bmp->height();
    const uint32_t bpp = bmp->bpp();
    if (bw < 1 || bh < 1 || bpp < 4) {
      out->error = "ultralight: empty bitmap";
      return false;
    }
    void* pixels = bmp->LockPixels();
    if (!pixels) {
      out->error = "ultralight: lock failed";
      return false;
    }
    const uint32_t row = bmp->row_bytes();
    out->rgba.resize(static_cast<size_t>(bw) * bh * 4);
    const auto* src = static_cast<const uint8_t*>(pixels);
    for (uint32_t y = 0; y < bh; ++y) {
      const uint8_t* srow = src + static_cast<size_t>(y) * row;
      uint8_t* drow = out->rgba.data() + static_cast<size_t>(y) * bw * 4;
      for (uint32_t x = 0; x < bw; ++x) {
        // Ultralight BGRA → RGBA
        drow[x * 4 + 0] = srow[x * 4 + 2];
        drow[x * 4 + 1] = srow[x * 4 + 1];
        drow[x * 4 + 2] = srow[x * 4 + 0];
        drow[x * 4 + 3] = srow[x * 4 + 3];
      }
    }
    bmp->UnlockPixels();
    out->width = bw;
    out->height = bh;
    out->ok = true;
#ifdef BLINK_HAS_PAINT_PIPELINE
    rasta::Image image;
    image.width = bw;
    image.height = bh;
    image.pixels = out->rgba;
    out->png = rasta::EncodePNG(image);
#endif
    return true;
  }
};

UltralightSession& Session() {
  static UltralightSession s;
  return s;
}

}  // namespace

bool UltralightPaintHTML(const std::string& html, const std::string& base_url, uint32_t w,
                         uint32_t h, HtmlFrame* out) {
  if (!out) return false;
  UltralightSession& s = Session();
  if (!s.Ensure(w, h)) {
    out->error = "ultralight: renderer/view failed";
    return false;
  }
  if (!base_url.empty())
    s.view->LoadHTML(html.c_str(), base_url.c_str());
  else
    s.view->LoadHTML(html.c_str());
  s.Pump();
  return s.Copy(out);
}

bool UltralightPaintURL(const std::string& url, uint32_t w, uint32_t h, HtmlFrame* out) {
  if (!out) return false;
  UltralightSession& s = Session();
  if (!s.Ensure(w, h)) {
    out->error = "ultralight: renderer/view failed";
    return false;
  }
  s.view->LoadURL(url.c_str());
  s.Pump(96);
  return s.Copy(out);
}

void UltralightFireClick(float x, float y) {
  UltralightSession& s = Session();
  if (!s.view) return;
  MouseEvent down;
  down.type = MouseEvent::kType_MouseDown;
  down.button = MouseEvent::kButton_Left;
  down.x = static_cast<int>(x);
  down.y = static_cast<int>(y);
  s.view->FireMouseEvent(down);
  MouseEvent up = down;
  up.type = MouseEvent::kType_MouseUp;
  s.view->FireMouseEvent(up);
  if (s.renderer) {
    s.renderer->Update();
    s.renderer->Render();
  }
}

void UltralightFireKey(uint32_t vk, const std::string& text) {
  UltralightSession& s = Session();
  if (!s.view) return;
  KeyEvent ev;
  ev.type = KeyEvent::kType_Char;
  ev.virtual_key_code = static_cast<int>(vk);
  ev.text = text.c_str();
  ev.unmodified_text = text.c_str();
  s.view->FireKeyEvent(ev);
  if (s.renderer) {
    s.renderer->Update();
    s.renderer->Render();
  }
}

}  // namespace blink

#endif  // BLINK_HAS_ULTRALIGHT
