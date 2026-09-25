#ifndef BLINK_DISPLAY_LIST_H_
#define BLINK_DISPLAY_LIST_H_

#include "blink/css.h"
#include "blink/dom.h"
#include "blink/layout.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blink {

// A clean-room paint pipeline, written against CSS 2.1 Appendix E rather
// than any browser's sources. See third_party/chromium/README.md for the
// concept mapping.
//
// Painting is split in two the way a real engine splits it: a recording
// pass walks the fragment tree and emits an ordered list of drawing
// operations, and a raster pass replays that list onto a surface. The
// split is what makes paint order testable -- Appendix E ordering is a
// property of the list, checkable without a canvas or a font.
enum class DisplayItemKind {
  kFillRect,
  kFillRoundRect,
  kGradient,        // linear/radial fill, rounded when radii are set
  kStrokeRoundRect, // single stroke for a rounded border
  kImage,
  kText,
  kCircle,          // list-item bullet
  kBoxShadow,       // outer box-shadow before background
  kPushClipRect,
  kPushClipRoundRect,
  kPopClip,
  kPushTransform,  // 2x3 matrix in a,b,c,d,e,f; origin at (x,y)
  kPopTransform,
  kPushScroll,  // scroll offset in (x, y) subtracted from descendant geometry
  kPopScroll,
};

struct DisplayItem {
  DisplayItemKind kind = DisplayItemKind::kFillRect;
  // The element this operation came from. Carried for debugging and for
  // tests that assert ordering by tag rather than by geometry.
  const Node* source = nullptr;

  float x = 0, y = 0, width = 0, height = 0;
  float radius_tl = 0, radius_tr = 0, radius_br = 0, radius_bl = 0;
  uint32_t color = 0;
  float stroke_width = 0;

  // kText: `y` is the baseline. Face selection is by these flags so raster
  // resolves the same FontSet entry layout measured with.
  std::string text;
  float font_size = 0;
  GenericFontFamily family = GenericFontFamily::kSansSerif;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool line_through = false;

  // kGradient
  BgGradientKind gradient = BgGradientKind::kNone;
  float grad_angle = 180.0f;
  float grad_cx = 0.5f, grad_cy = 0.5f, grad_radius = 0.7f;
  std::vector<BgGradientStop> stops;

  // kImage: pixels owned by HtmlDocument::images.
  const DecodedImage* image = nullptr;

  // kBoxShadow: geometry of the border box; offsets live in `shadow`.
  BoxShadow shadow;
  bool shadow_inset = false;

  // kPushTransform: CSS matrix() a b c d e f, applied about (x, y) as origin.
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

  bool has_radius() const {
    return radius_tl > 0.5f || radius_tr > 0.5f || radius_br > 0.5f || radius_bl > 0.5f;
  }
};

struct DisplayList {
  std::vector<DisplayItem> items;

  void Append(DisplayItem item) { items.push_back(std::move(item)); }
  bool empty() const { return items.empty(); }
  size_t size() const { return items.size(); }
};

// CSS 2.1 Appendix E paint phases. A stacking context is walked once per
// phase, which is what orders every block background in it before every
// float, and every float before any inline text.
enum class PaintPhase {
  kBlockBackground,  // backgrounds, borders and replaced content of blocks
  kFloat,            // non-positioned floats and their contents
  kForeground,       // inline content: text fragments, markers
};

// True for boxes that establish a stacking context: the root, opacity < 1,
// a non-none transform, and positioned boxes with a definite z-index.
// CSS Positioned Layout / CSS Transforms: those are the triggers Blink
// uses for a PaintLayer stacking context; overflow:hidden is a BFC, not
// a stacking context, and is handled as a clip instead.
bool IsStackingContext(const ComputedStyle& style, bool is_root);

// Records `root` into an Appendix E-ordered display list. `document`
// supplies decoded image pixels for replaced content.
DisplayList BuildDisplayList(const LayoutBox& root, const HtmlDocument& document);

}  // namespace blink

#endif  // BLINK_DISPLAY_LIST_H_
