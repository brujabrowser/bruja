#include "blink/paint.h"

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "blink/display_list.h"
#include "blink/layout.h"

#include "wasmskia/canvas.h"
#include "wasmskia/font.h"
#include "wasmskia/path.h"
#include "wasmskia/shader.h"

#include "rasta/rasta.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace blink {

namespace {

std::vector<uint8_t> ReadWholeFile(const std::string& path) {
  std::vector<uint8_t> data;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return data;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size > 0) {
    data.resize(static_cast<size_t>(size));
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
  }
  std::fclose(f);
  return data;
}

// No font is vendored anywhere in this family (WASMSkia's own tests use
// the same approach -- see its font_test.cc) -- this whole paint path is
// already Windows-only (WIC/Win32 elsewhere in the family), so reading a
// stock Windows font file is a real dependency, not a hidden one.
//
// Latin UI font first, then Segoe UI Emoji (COLR color glyphs) and
// Segoe UI Symbol so 😀/✅/etc. don't fall through to .notdef tofu
// squares. Faces are cached: seguemj.ttf is ~12MB and constructing a
// Font copies it.
std::vector<uint8_t> LoadFirstFont(std::initializer_list<const char*> paths) {
  for (const char* path : paths) {
    std::vector<uint8_t> data = ReadWholeFile(path);
    if (!data.empty()) return data;
  }
  return {};
}

std::vector<uint8_t> LoadSystemFont() {
  return LoadFirstFont({"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"});
}

void TryAddFallback(wasmskia::Font& font, const char* path) {
  std::vector<uint8_t> data = ReadWholeFile(path);
  if (data.empty()) return;
  wasmskia::Font fallback(data.data(), data.size());
  if (fallback.valid()) font.AddFallback(std::move(fallback));
}

const wasmskia::Font* PaintFont() {
  static wasmskia::Font font = []() {
    std::vector<uint8_t> ttf = LoadSystemFont();
    wasmskia::Font f(ttf.empty() ? nullptr : ttf.data(), ttf.size());
    if (!f.valid()) return f;
    TryAddFallback(f, "C:/Windows/Fonts/seguiemj.ttf");
    TryAddFallback(f, "C:/Windows/Fonts/seguisym.ttf");
    return f;
  }();
  return font.valid() ? &font : nullptr;
}

// A face per weight/style. wasmskia::DrawText has no weight argument and
// no synthetic emboldening, so `font-weight: bold` can only be honored by
// drawing from a real bold file.
const wasmskia::Font* LoadFace(std::initializer_list<const char*> paths) {
  static std::vector<std::unique_ptr<wasmskia::Font>> cache;
  std::vector<uint8_t> ttf = LoadFirstFont(paths);
  if (ttf.empty()) return nullptr;
  auto face = std::make_unique<wasmskia::Font>(ttf.data(), ttf.size());
  if (!face->valid()) return nullptr;
  TryAddFallback(*face, "C:/Windows/Fonts/seguiemj.ttf");
  TryAddFallback(*face, "C:/Windows/Fonts/seguisym.ttf");
  cache.push_back(std::move(face));
  return cache.back().get();
}

const wasmskia::Font* LoadFacePaths(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    if (const wasmskia::Font* face = LoadFace({path.c_str()})) return face;
  }
  return nullptr;
}

const wasmskia::Font* DynamicFontFamilyImpl(const std::string& family_lower) {
  static std::unordered_map<std::string, const wasmskia::Font*> cache;
  if (auto it = cache.find(family_lower); it != cache.end()) return it->second;

  std::string stem;
  stem.reserve(family_lower.size());
  for (char ch : family_lower) {
    if (ch == ' ' || ch == '\'' || ch == '"') continue;
    stem.push_back(ch);
  }
  if (stem.empty()) {
    cache[family_lower] = nullptr;
    return nullptr;
  }

  const wasmskia::Font* face = LoadFacePaths({
      "C:/Windows/Fonts/" + stem + ".ttf",
      "C:/Windows/Fonts/" + stem + ".otf",
      "C:/Windows/Fonts/" + stem + "b.ttf",
      "C:/Windows/Fonts/" + stem + "bd.ttf",
  });
  cache[family_lower] = face;
  return face;
}

// Layout and paint must agree on face selection, so both sides read this.
const FontSet& PaintFonts() {
  static FontSet fonts = []() {
    FontSet f;
    f.regular = PaintFont();
    f.bold = LoadFace({"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"});
    f.italic = LoadFace({"C:/Windows/Fonts/segoeuii.ttf", "C:/Windows/Fonts/ariali.ttf"});
    f.bold_italic = LoadFace({"C:/Windows/Fonts/segoeuiz.ttf", "C:/Windows/Fonts/arialbi.ttf"});
    f.mono = LoadFace({"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
                       "C:/Windows/Fonts/lucon.ttf"});
    f.mono_bold = LoadFace({"C:/Windows/Fonts/consolab.ttf", "C:/Windows/Fonts/courbd.ttf"});
    f.mono_italic = LoadFace({"C:/Windows/Fonts/consolai.ttf", "C:/Windows/Fonts/couri.ttf"});
    f.serif = LoadFace({"C:/Windows/Fonts/times.ttf", "C:/Windows/Fonts/georgia.ttf"});
    f.serif_bold = LoadFace({"C:/Windows/Fonts/timesbd.ttf", "C:/Windows/Fonts/georgiab.ttf"});
    f.serif_italic = LoadFace({"C:/Windows/Fonts/timesi.ttf", "C:/Windows/Fonts/georgiai.ttf"});
    f.georgia = LoadFace({"C:/Windows/Fonts/georgia.ttf"});
    f.consolas = LoadFace({"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"});
    return f;
  }();
  return fonts;
}

wasmskia::Path RoundRectPath(float x, float y, float w, float h, float tl, float tr, float br,
                             float bl) {
  float maxr = std::min(w, h) * 0.5f;
  if (maxr < 0) maxr = 0;
  tl = std::min(std::max(tl, 0.0f), maxr);
  tr = std::min(std::max(tr, 0.0f), maxr);
  br = std::min(std::max(br, 0.0f), maxr);
  bl = std::min(std::max(bl, 0.0f), maxr);
  wasmskia::Path p;
  // ArcTo oval is (x,y,w,h) of the corner's bounding box; 0° is 3-o'clock,
  // clockwise in y-down device space.
  p.MoveTo(x + tl, y);
  p.LineTo(x + w - tr, y);
  if (tr > 0.5f)
    p.ArcTo(x + w - 2.0f * tr, y, 2.0f * tr, 2.0f * tr, 270.0f, 90.0f);
  else
    p.LineTo(x + w, y);
  p.LineTo(x + w, y + h - br);
  if (br > 0.5f)
    p.ArcTo(x + w - 2.0f * br, y + h - 2.0f * br, 2.0f * br, 2.0f * br, 0.0f, 90.0f);
  else
    p.LineTo(x + w, y + h);
  p.LineTo(x + bl, y + h);
  if (bl > 0.5f)
    p.ArcTo(x, y + h - 2.0f * bl, 2.0f * bl, 2.0f * bl, 90.0f, 90.0f);
  else
    p.LineTo(x, y + h);
  p.LineTo(x, y + tl);
  if (tl > 0.5f)
    p.ArcTo(x, y, 2.0f * tl, 2.0f * tl, 180.0f, 90.0f);
  else
    p.LineTo(x, y);
  p.Close();
  return p;
}

wasmskia::Path ItemPath(const DisplayItem& item) {
  return RoundRectPath(item.x, item.y, item.width, item.height, item.radius_tl, item.radius_tr,
                       item.radius_br, item.radius_bl);
}

std::unique_ptr<wasmskia::Shader> MakeGradientShader(const DisplayItem& item) {
  if (item.gradient == BgGradientKind::kNone || item.stops.empty()) return nullptr;
  std::vector<wasmskia::GradientStop> stops;
  stops.reserve(item.stops.size());
  for (const BgGradientStop& s : item.stops) {
    stops.push_back({s.offset < 0 ? 0.0f : s.offset, s.color});
  }
  if (stops.size() == 1) stops.push_back(stops.front());
  if (item.gradient == BgGradientKind::kLinear) {
    const float rad = item.grad_angle * 3.14159265f / 180.0f;
    const float dx = std::sin(rad);
    const float dy = -std::cos(rad);
    const float cx = item.x + item.width * 0.5f;
    const float cy = item.y + item.height * 0.5f;
    float half = 0.5f * (std::abs(item.width * dx) + std::abs(item.height * dy));
    if (half < 1.0f) half = 1.0f;
    return std::make_unique<wasmskia::LinearGradient>(cx - dx * half, cy - dy * half, cx + dx * half,
                                                      cy + dy * half, std::move(stops));
  }
  const float cx = item.x + item.width * item.grad_cx;
  const float cy = item.y + item.height * item.grad_cy;
  float r = item.grad_radius * std::hypot(item.width, item.height);
  if (r < 1.0f) r = 1.0f;
  return std::make_unique<wasmskia::RadialGradient>(cx, cy, r, std::move(stops));
}

// Nearest-neighbour resample of decoded pixels into the destination rect.
std::vector<uint8_t> ScaleImage(const DecodedImage& img, int dest_w, int dest_h) {
  std::vector<uint8_t> scaled(static_cast<size_t>(dest_w) * dest_h * 4);
  for (int y = 0; y < dest_h; ++y) {
    const int sy = std::min(img.height - 1, y * img.height / dest_h);
    for (int x = 0; x < dest_w; ++x) {
      const int sx = std::min(img.width - 1, x * img.width / dest_w);
      const uint8_t* src = img.rgba.data() + (static_cast<size_t>(sy) * img.width + sx) * 4;
      uint8_t* dst = scaled.data() + (static_cast<size_t>(y) * dest_w + x) * 4;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = src[3];
    }
  }
  return scaled;
}

uint32_t ScaleAlpha(uint32_t color, float factor) {
  const float a = static_cast<float>((color >> 24) & 0xFFu) * factor;
  const uint32_t na = static_cast<uint32_t>(std::min(255.0f, std::max(0.0f, a)));
  return (na << 24) | (color & 0x00FFFFFFu);
}

void DrawShadowShape(wasmskia::Canvas& canvas, float sx, float sy, float sw, float sh_h,
                     const DisplayItem& item, uint32_t color) {
  if (sw <= 0.0f || sh_h <= 0.0f || (color >> 24) == 0) return;
  if (item.has_radius() && sw > 2.0f && sh_h > 2.0f) {
    wasmskia::Path path;
    path.MoveTo(sx + item.radius_tl, sy);
    path.LineTo(sx + sw - item.radius_tr, sy);
    path.QuadTo(sx + sw, sy, sx + sw, sy + item.radius_tr);
    path.LineTo(sx + sw, sy + sh_h - item.radius_br);
    path.QuadTo(sx + sw, sy + sh_h, sx + sw - item.radius_br, sy + sh_h);
    path.LineTo(sx + item.radius_bl, sy + sh_h);
    path.QuadTo(sx, sy + sh_h, sx, sy + sh_h - item.radius_bl);
    path.LineTo(sx, sy + item.radius_tl);
    path.QuadTo(sx, sy, sx + item.radius_tl, sy);
    path.Close();
    canvas.DrawPath(path, color);
  } else {
    canvas.DrawRect(sx, sy, sw, sh_h, color);
  }
}

// Replay. Nothing in here knows about the DOM, the cascade or paint
// order: all of that was decided while recording, which is why paint
// order is checkable without a surface or a font installed.
// WASMSkia::DrawRect ignores the canvas matrix, so viewport scroll is
// applied by shifting document coordinates here rather than Translate().
void RasterizeDisplayList(wasmskia::Canvas& canvas, const DisplayList& list, float scroll_x = 0.0f,
                          float scroll_y = 0.0f) {
  const wasmskia::Font* plain = PaintFont();
  int clip_depth = 0;
  float accum_scroll_x = scroll_x;
  float accum_scroll_y = scroll_y;
  std::vector<std::pair<float, float>> scroll_stack;
  for (const DisplayItem& raw : list.items) {
    switch (raw.kind) {
      case DisplayItemKind::kPushScroll:
        scroll_stack.push_back({accum_scroll_x, accum_scroll_y});
        accum_scroll_x += raw.x;
        accum_scroll_y += raw.y;
        continue;
      case DisplayItemKind::kPopScroll:
        if (!scroll_stack.empty()) {
          accum_scroll_x = scroll_stack.back().first;
          accum_scroll_y = scroll_stack.back().second;
          scroll_stack.pop_back();
        }
        continue;
      default:
        break;
    }
    DisplayItem item = raw;
    item.x -= accum_scroll_x;
    item.y -= accum_scroll_y;
    const bool fillable = item.width > 0.0f && item.height > 0.0f;
    const bool paths_ok = item.width > 2.0f && item.height > 2.0f;
    switch (item.kind) {
      case DisplayItemKind::kFillRect:
        if (fillable && (item.color >> 24) != 0)
          canvas.DrawRect(item.x, item.y, item.width, item.height, item.color);
        break;
      case DisplayItemKind::kFillRoundRect: {
        if (!fillable || (item.color >> 24) == 0) break;
        if (item.has_radius() && paths_ok) {
          wasmskia::Path path = ItemPath(item);
          if (!path.empty()) {
            canvas.DrawPath(path, item.color);
            break;
          }
        }
        canvas.DrawRect(item.x, item.y, item.width, item.height, item.color);
        break;
      }
      case DisplayItemKind::kGradient: {
        if (!fillable) break;
        std::unique_ptr<wasmskia::Shader> shader = MakeGradientShader(item);
        if (!shader) break;
        if (item.has_radius() && paths_ok) {
          wasmskia::Path path = ItemPath(item);
          if (!path.empty()) {
            canvas.DrawPath(path, *shader);
            break;
          }
        }
        canvas.DrawRect(item.x, item.y, item.width, item.height, *shader);
        break;
      }
      case DisplayItemKind::kStrokeRoundRect: {
        if (!paths_ok || item.stroke_width <= 0.0f) break;
        wasmskia::Path path = ItemPath(item);
        if (path.empty()) break;
        canvas.StrokePath(path, item.stroke_width, item.color, wasmskia::StrokeCap::kButt,
                          wasmskia::StrokeJoin::kRound);
        break;
      }
      case DisplayItemKind::kImage: {
        if (!item.image || !fillable) break;
        const int dest_w = static_cast<int>(item.width);
        const int dest_h = static_cast<int>(item.height);
        if (dest_w <= 0 || dest_h <= 0) break;
        std::vector<uint8_t> scaled = ScaleImage(*item.image, dest_w, dest_h);
        wasmskia::ImageShader shader(scaled.data(), dest_w, dest_h, wasmskia::TileMode::kClamp,
                                     wasmskia::TileMode::kClamp, item.x, item.y);
        canvas.DrawRect(item.x, item.y, item.width, item.height, shader);
        break;
      }
      case DisplayItemKind::kText: {
        if (item.text.empty()) break;
        const wasmskia::Font* face = PaintFonts().Resolve(item.family, item.bold, item.italic);
        if (!face) face = plain;
        if (!face) break;
        wasmskia::DrawText(canvas, *face, item.text, item.x, item.y, item.font_size, item.color);
        break;
      }
      case DisplayItemKind::kCircle:
        if (item.width > 0.0f) canvas.DrawCircle(item.x, item.y, item.width, item.color);
        break;
      case DisplayItemKind::kBoxShadow: {
        const BoxShadow& sh = item.shadow;
        const float ox = item.shadow_inset ? -sh.offset_x : sh.offset_x;
        const float oy = item.shadow_inset ? -sh.offset_y : sh.offset_y;
        if ((item.color >> 24) == 0) break;
        const int layers =
            sh.blur > 0.5f ? std::max(4, std::min(10, static_cast<int>(std::ceil(sh.blur / 2.0f))))
                           : 1;
        for (int layer = layers; layer >= 1; --layer) {
          const float t = static_cast<float>(layer) / static_cast<float>(layers);
          const float blur_pad = sh.blur > 0.5f ? sh.blur * t : 0.0f;
          const float pad = blur_pad + sh.spread;
          const float sx = item.x + ox - pad;
          const float sy = item.y + oy - pad;
          const float sw = item.width + pad * 2.0f;
          const float sh_h = item.height + pad * 2.0f;
          const float alpha =
              layers == 1 ? 1.0f : 0.24f * (1.05f - t);  // stacked rings, not one hard spread.
          const uint32_t color = layers == 1 ? item.color : ScaleAlpha(item.color, alpha);
          DrawShadowShape(canvas, sx, sy, sw, sh_h, item, color);
        }
        break;
      }
      case DisplayItemKind::kPushClipRoundRect: {
        wasmskia::Path path = ItemPath(item);
        if (path.empty()) break;
        canvas.Save();
        canvas.ClipPath(path);
        ++clip_depth;
        break;
      }
      case DisplayItemKind::kPushClipRect:
        canvas.Save();
        canvas.ClipRect(item.x, item.y, item.width, item.height);
        ++clip_depth;
        break;
      case DisplayItemKind::kPopClip:
        if (clip_depth > 0) {
          canvas.Restore();
          --clip_depth;
        }
        break;
      case DisplayItemKind::kPushTransform: {
        canvas.Save();
        const float ox = item.x;
        const float oy = item.y;
        const float deg =
            std::atan2(item.b, item.a) * 180.0f / 3.14159265358979323846f;
        const float sx = std::sqrt(item.a * item.a + item.b * item.b);
        const float sy = std::sqrt(item.c * item.c + item.d * item.d);
        canvas.Translate(ox + item.e, oy + item.f);
        canvas.Rotate(deg);
        if (sx > 0.001f || sy > 0.001f) canvas.Scale(sx > 0.001f ? sx : 1.0f, sy > 0.001f ? sy : 1.0f);
        canvas.Translate(-ox, -oy);
        break;
      }
      case DisplayItemKind::kPopTransform:
        canvas.Restore();
        break;
      case DisplayItemKind::kPushScroll:
      case DisplayItemKind::kPopScroll:
        break;
    }
  }
  // A truncated list (an image item dropped for a degenerate rect) must
  // not leave the canvas clipped for whoever draws next.
  while (clip_depth-- > 0) canvas.Restore();
}

struct RenderedFrame {
  bool ok = false;
  wasmskia::Canvas canvas{1, 1};
  std::string error;
};

RenderedFrame RenderToCanvas(const HtmlDocument& document, uint32_t viewport_width,
                             uint32_t min_height = 0) {
  RenderedFrame out;
  if (!PaintFonts().valid()) {
    out.error = "no system font found (expected segoeui.ttf or arial.ttf under C:/Windows/Fonts)";
    return out;
  }

  const float viewport_h =
      document.viewport_height > 0.0f
          ? document.viewport_height
          : (min_height > 0 ? static_cast<float>(min_height) : 0.0f);
  LayoutResult layout = ComputeLayout(document, static_cast<float>(viewport_width), PaintFonts(),
                                      viewport_h > 0.0f ? viewport_h : 0.0f);
  if (!layout.ok) {
    out.error = layout.error;
    return out;
  }

  const float scroll_x = document.scroll_x;
  const float scroll_y = document.scroll_y;
  const bool viewport_surface =
      viewport_h > 0.0f || scroll_y > 0.0f || scroll_x > 0.0f;
  uint32_t height;
  if (viewport_surface) {
    height = static_cast<uint32_t>(std::max(64.0f, viewport_h > 0.0f ? viewport_h : layout.total_height));
  } else {
    height = static_cast<uint32_t>(layout.total_height);
    if (height < 64) height = 64;
    if (min_height > height) height = min_height;
  }

  out.canvas = wasmskia::Canvas(static_cast<int>(viewport_width), static_cast<int>(height));
  // Shell theme (#070b12), not white — empty/partial CaptureFrame was flashing
  // a white page hole under FrameManager.
  out.canvas.Clear(0xFF070B12u);
  const float paint_scroll_x = viewport_surface ? scroll_x : 0.0f;
  const float paint_scroll_y = viewport_surface ? scroll_y : 0.0f;
  if (viewport_surface) {
    out.canvas.Save();
    out.canvas.ClipRect(0.0f, 0.0f, static_cast<float>(viewport_width), static_cast<float>(height));
  }
  RasterizeDisplayList(out.canvas, BuildDisplayList(layout.root, document), paint_scroll_x,
                       paint_scroll_y);
  if (viewport_surface) out.canvas.Restore();
  out.ok = true;
  return out;
}

}  // namespace

const wasmskia::Font* DynamicFontFamily(const std::string& family_lower) {
  return DynamicFontFamilyImpl(family_lower);
}

const FontSet& GetPaintFonts() { return PaintFonts(); }

PaintResult PaintDocument(const HtmlDocument& document, uint32_t viewport_width) {
  PaintResult result;
  RenderedFrame frame = RenderToCanvas(document, viewport_width);
  if (!frame.ok) {
    result.error = frame.error;
    return result;
  }
  rasta::Image image;
  image.width = frame.canvas.width();
  image.height = frame.canvas.height();
  const uint8_t* pixels = frame.canvas.pixels();
  image.pixels.assign(pixels,
                      pixels + static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
  result.png = rasta::EncodePNG(image);
  result.ok = true;
  result.width = viewport_width;
  result.height = static_cast<uint32_t>(frame.canvas.height());
  return result;
}

// Two-arg form: older translation units (Lime's last-built
// local_frame_impl.cc.obj) were compiled against a declaration without
// min_height and still look up this symbol.
RawFrameResult CaptureRawFrame(const HtmlDocument& document, uint32_t viewport_width) {
  return CaptureRawFrame(document, viewport_width, 0u);
}

RawFrameResult CaptureRawFrame(const HtmlDocument& document, uint32_t viewport_width,
                               uint32_t min_height) {
  RawFrameResult result;
  RenderedFrame frame = RenderToCanvas(document, viewport_width, min_height);
  if (!frame.ok) {
    result.error = frame.error;
    return result;
  }
  result.width = static_cast<uint32_t>(frame.canvas.width());
  result.height = static_cast<uint32_t>(frame.canvas.height());
  const uint8_t* pixels = frame.canvas.pixels();
  result.rgba.assign(pixels,
                     pixels + static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4);
  result.ok = true;
  return result;
}

const Node* HitTestDocument(const HtmlDocument& document, uint32_t viewport_width, float x,
                            float y) {
  if (!PaintFonts().valid()) return nullptr;
  const float viewport_h =
      document.viewport_height > 0.0f ? document.viewport_height : 0.0f;
  LayoutResult layout = ComputeLayout(document, static_cast<float>(viewport_width), PaintFonts(),
                                      viewport_h);
  if (!layout.ok) return nullptr;
  if (viewport_h > 0.0f && (y < 0.0f || y >= viewport_h)) return nullptr;
  const float viewport_w = static_cast<float>(viewport_width);
  if (viewport_h > 0.0f && (x < 0.0f || x >= viewport_w)) return nullptr;
  const float doc_x = x + document.scroll_x;
  const float doc_y = y + document.scroll_y;
  // Inline-aware: a click on link text must report the <a>, which box-only
  // hit-testing cannot do because inline elements own no box.
  if (const Node* inline_hit = HitTestInline(layout.root, doc_x, doc_y, &document)) return inline_hit;
  const LayoutBox* hit = HitTestLayout(layout.root, doc_x, doc_y, &document);
  return hit ? hit->source : nullptr;
}

bool WheelScrollDocument(HtmlDocument* document, uint32_t viewport_width,
                         uint32_t viewport_height, float x, float y, float delta_x,
                         float delta_y) {
  if (!document || !PaintFonts().valid()) return false;
  const float before_scroll_x = document->scroll_x;
  const float before_scroll_y = document->scroll_y;
  const auto before_element_scroll = document->element_scroll;
  const WheelScrollResult result =
      ApplyWheelScroll(document, static_cast<float>(viewport_width),
                       static_cast<float>(viewport_height), PaintFonts(), x, y, delta_x, delta_y);
  if (!result.consumed) return false;
  if (result.scrolled_viewport) {
    return document->scroll_x != before_scroll_x || document->scroll_y != before_scroll_y;
  }
  if (result.scroll_target) {
    const auto after_it = document->element_scroll.find(result.scroll_target);
    const auto before_it = before_element_scroll.find(result.scroll_target);
    const HtmlDocument::ElementScroll after =
        after_it != document->element_scroll.end() ? after_it->second : HtmlDocument::ElementScroll{};
    const HtmlDocument::ElementScroll before =
        before_it != before_element_scroll.end() ? before_it->second : HtmlDocument::ElementScroll{};
    return after.x != before.x || after.y != before.y;
  }
  return false;
}

}  // namespace blink

#else  // !BLINK_HAS_PAINT_PIPELINE

namespace blink {

PaintResult PaintDocument(const HtmlDocument& document, uint32_t viewport_width) {
  (void)document;
  (void)viewport_width;
  PaintResult result;
  result.error = "no native paint pipeline available (WASMSkia/WASMRasta siblings not present)";
  return result;
}

const FontSet& GetPaintFonts() {
  static FontSet empty;
  return empty;
}

RawFrameResult CaptureRawFrame(const HtmlDocument& document, uint32_t viewport_width,
                               uint32_t min_height) {
  (void)document;
  (void)viewport_width;
  (void)min_height;
  RawFrameResult result;
  result.error = "no native paint pipeline available (WASMSkia/WASMRasta siblings not present)";
  return result;
}

const Node* HitTestDocument(const HtmlDocument& document, uint32_t viewport_width, float x,
                            float y) {
  (void)document;
  (void)viewport_width;
  (void)x;
  (void)y;
  return nullptr;
}

bool WheelScrollDocument(HtmlDocument* document, uint32_t viewport_width,
                         uint32_t viewport_height, float x, float y, float delta_x,
                         float delta_y) {
  (void)document;
  (void)viewport_width;
  (void)viewport_height;
  (void)x;
  (void)y;
  (void)delta_x;
  (void)delta_y;
  return false;
}

}  // namespace blink

#endif  // BLINK_HAS_PAINT_PIPELINE
