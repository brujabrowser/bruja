#ifndef BLINK_LAYOUT_H_
#define BLINK_LAYOUT_H_

#include "blink/css.h"
#include "blink/dom.h"
#include "blink/font_set.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wasmskia {
class Font;
}  // namespace wasmskia

namespace blink {

// One real per-element rectangle (border box, box-sizing:border-box) in a
// real layout tree. Cascade (blink/css.h) drives display (block/inline/
// inline-block/flex/grid/table/table-row/table-cell/list-item/none), the
// box model, colors, flex (including grow/shrink/basis), a simple
// equal-column grid, CSS tables (thead/tbody flatten to rows; auto column
// widths fill the table), list-item markers, and positioning
// (relative/absolute/fixed/sticky-as-relative). Inline content runs
// through a real inline formatting context (blink/inline_layout.h):
// nested inlines keep their own style and identity. Floats wrap lines.
// <img> is a replaced element using decoded pixels on
// HtmlDocument::images when present. Margin collapsing (sibling and
// parent/child), flex-wrap, table colspan/rowspan, grid fr/px tracks,
// sticky positioning, and named font resolution are supported.

// One positioned, styled run of text emitted by the inline formatting
// context. The predecessor of this type held only {text, x, y}, so every
// run on a line had to be painted with the *enclosing block's* style and
// had no box of its own to hit-test -- which made <b>, <a href> and
// nested <span> unrepresentable. Each fragment now carries the style that
// actually cascaded onto it plus the element it came from.
struct InlineFragment {
  std::string text;
  // Nearest element ancestor of the text -- the <a> for link text, so a
  // click resolves to the anchor and not to the enclosing paragraph.
  const Node* source = nullptr;
  ComputedStyle style;
  float x = 0;
  float y = 0;  // baseline, matching wasmskia::DrawText's own (x, y) contract.
  float width = 0;
  float ascent = 0;
  float descent = 0;

  float top() const { return y - ascent; }
  float height() const { return ascent + descent; }
  bool Contains(float px, float py) const {
    return px >= x && px < x + width && py >= top() && py < y + descent;
  }
};

struct LayoutBox {
  const Node* source = nullptr;  // the DOM node this box represents -- used for hit-testing.
  float x = 0, y = 0, width = 0, height = 0;
  ComputedStyle style;
  // This box's own inline formatting context output. Non-empty only for
  // boxes that directly contain inline content.
  std::vector<InlineFragment> fragments;
  std::vector<LayoutBox> children;
  // Maximum scroll offset along each axis for `overflow:auto|scroll`
  // containers, derived from descendant overflow past the padding box.
  float scroll_extent_x = 0.0f;
  float scroll_extent_y = 0.0f;
  // Replaced `content: url(...)` on a pseudo-element (not keyed by DOM node).
  const DecodedImage* content_image = nullptr;
};

struct LayoutResult {
  bool ok = false;
  LayoutBox root;
  float total_height = 0;
  float total_width = 0;
  std::string error;
};

// Lays out `document` (starting at its <body>, or <html> if there's no
// body) into a real box tree at the given viewport width. Requires a real
// font for word-wrapping (wasmskia::MeasureText) -- pass the same font a
// caller will paint with. Real CSS from every <style> element in the
// document is parsed and cascaded (blink/css.h); inline style="" is
// always applied too.
LayoutResult ComputeLayout(const HtmlDocument& document, float viewport_width,
                          const wasmskia::Font& font, float viewport_height = 0.0f);

// Same, with the full weight/style family. Callers that paint must pass
// the same FontSet they paint with: layout measures every run through
// FontSet::Resolve, so a different face here means text is positioned for
// glyphs that never get drawn.
LayoutResult ComputeLayout(const HtmlDocument& document, float viewport_width,
                          const FontSet& fonts, float viewport_height = 0.0f);

// Moves an already-positioned box subtree -- children and inline
// fragments included -- by (dx, dy).
void ShiftLayoutBox(LayoutBox* box, float dx, float dy);

// Fills `scroll_extent_x/y` on every scroll container in the tree.
void ComputeScrollExtents(LayoutBox* root);

// Scroll offset for paint / hit-testing, clamped to each box's extent.
float EffectiveScrollX(const HtmlDocument& document, const LayoutBox& box);
float EffectiveScrollY(const HtmlDocument& document, const LayoutBox& box);

// Depth-first search for the most specific (deepest) box whose rect
// contains (x, y), or nullptr if the point misses everything.
const LayoutBox* HitTestLayout(const LayoutBox& root, float x, float y,
                               const HtmlDocument* document = nullptr);

// Hit-test that can land on an inline fragment. Returns the innermost
// element at (x, y): the <a> inside a paragraph rather than the paragraph,
// which plain box hit-testing can never report because inline elements
// own no box. Falls back to HitTestLayout's box result.
const Node* HitTestInline(const LayoutBox& root, float x, float y,
                          const HtmlDocument* document = nullptr);

struct WheelScrollResult {
  bool consumed = false;
  bool scrolled_viewport = false;
  const Node* scroll_target = nullptr;  // nullptr when the viewport scrolled.
};

// Updates `document.scroll_y` or the deepest element scroll container under
// (x, y). Viewport coordinates: y is client Y (HandleClick convention).
WheelScrollResult ApplyWheelScroll(HtmlDocument* document, float viewport_width,
                                   float viewport_height, const FontSet& fonts, float x,
                                   float y, float delta_x, float delta_y);

}  // namespace blink

#endif  // BLINK_LAYOUT_H_
