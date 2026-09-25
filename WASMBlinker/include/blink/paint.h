#ifndef BLINK_PAINT_H_
#define BLINK_PAINT_H_

#include "blink/dom.h"
#include "blink/font_set.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blink {

// The FontSet layout and paint already share for real text metrics (see
// blink/layout.h ComputeLayout). Every face is null, and FontSet::valid()
// (if checked) false, when built without the optional WASMSkia sibling
// (BLINK_HAS_PAINT_PIPELINE) -- callers needing real metrics should check
// before relying on it.
const FontSet& GetPaintFonts();

struct PaintResult {
  bool ok = false;
  std::string png;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string error;
};

// A one-off PNG snapshot (real block layout -- see blink/layout.h -- real
// TrueType text via wasmskia::Font/DrawText, real backgrounds/borders for
// boxes like buttons, encoded via WASMRasta). Meant for actual "take a
// picture of this page" callers (thumbnailing, tests checking pixel
// output) -- NOT what a live window should repaint from; see
// CaptureRawFrame below for that. Without the optional ../WASMSkia +
// ../WASMRasta siblings (BLINK_HAS_PAINT_PIPELINE), this is the same kind
// of honest stub LocalFrameImpl::CaptureSnapshot already was.
PaintResult PaintDocument(const HtmlDocument& document, uint32_t viewport_width);

struct RawFrameResult {
  bool ok = false;
  std::vector<uint8_t> rgba;  // width * height * 4 bytes, unpremultiplied RGBA8888.
  uint32_t width = 0;
  uint32_t height = 0;
  std::string error;
};

// The real live-rendering path: same layout/paint as PaintDocument, but
// hands back raw pixels directly -- no PNG encode, and critically no
// decode on the receiving end either (see lime::FrameWindow, which used
// to WIC-decode a PNG on every single repaint purely to get back the
// pixels it had just encoded two calls prior -- a real, wasteful round
// trip this replaces, not a stylistic preference).
RawFrameResult CaptureRawFrame(const HtmlDocument& document, uint32_t viewport_width,
                               uint32_t min_height = 0);

// Re-lays-out `document` at `viewport_width` (cheap and deterministic for
// documents this size -- no caching needed) and returns the DOM node
// whose real layout rectangle contains (x, y), or nullptr if the click
// missed everything painted. The returned pointer is into `document`'s
// own long-lived tree (not the transient layout tree), so it's valid for
// as long as `document` is.
const Node* HitTestDocument(const HtmlDocument& document, uint32_t viewport_width, float x,
                            float y);

// Updates scroll offsets on `document` (viewport or element container under
// the pointer). Returns true when any scroll offset changed.
bool WheelScrollDocument(HtmlDocument* document, uint32_t viewport_width,
                         uint32_t viewport_height, float x, float y, float delta_x,
                         float delta_y);

}  // namespace blink

#endif  // BLINK_PAINT_H_
