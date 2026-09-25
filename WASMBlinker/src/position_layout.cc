#include "blink/position_layout.h"

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "blink/block_layout.h"
#include "blink/dom.h"

namespace blink {

namespace {

float ScrollOffsetForBox(const HtmlDocument* document, const LayoutBox& box) {
  if (!document || !box.source) return 0.0f;
  auto it = document->element_scroll.find(box.source);
  if (it == document->element_scroll.end()) return 0.0f;
  return it->second.y;
}

}  // namespace

void ResolveStickyPositions(LayoutBox* box, const HtmlDocument* document, float scrollport_top,
                          float scrollport_scroll_y, float cb_top, float cb_bottom) {
  if (!box) return;
  const ComputedStyle& s = box->style;
  const float em = s.font_size > 0 ? s.font_size : 16.0f;
  const float pad_t = s.padding_top.Resolve(box->width, 0, em);
  const float pad_b = s.padding_bottom.Resolve(box->width, 0, em);
  const float inner_top = box->y + s.border_top + pad_t;
  const float inner_bottom = box->y + box->height - s.border_bottom - pad_b;

  float child_scrollport_top = scrollport_top;
  float child_scrollport_scroll = scrollport_scroll_y;
  float child_cb_top = cb_top;
  float child_cb_bottom = cb_bottom;
  if (ScrollsOverflowY(s) || ClipsOverflowY(s)) {
    const PaddingBox pad =
        ComputePaddingBox(s, box->x, box->y, box->width, box->height);
    child_scrollport_top = pad.y;
    child_scrollport_scroll = ScrollOffsetForBox(document, *box);
    child_cb_top = pad.y;
    child_cb_bottom = pad.y + pad.height;
  } else if (s.overflow_x != Overflow::kVisible || s.overflow_y != Overflow::kVisible) {
    child_cb_top = inner_top;
    child_cb_bottom = inner_bottom;
  }

  for (LayoutBox& child : box->children) {
    ResolveStickyPositions(&child, document, child_scrollport_top, child_scrollport_scroll,
                           child_cb_top, child_cb_bottom);
  }

  if (s.position != PositionType::kSticky) return;

  float y = box->y;
  if (!s.top.is_auto()) {
    const float sticky_top =
        scrollport_top + s.top.Resolve(0, 0, em) + scrollport_scroll_y;
    y = std::max(y, sticky_top);
  }
  if (!s.bottom.is_auto()) {
    const float sticky_max = cb_bottom - box->height - s.bottom.Resolve(0, 0, em);
    y = std::min(y, sticky_max);
  } else {
    y = std::min(y, cb_bottom - box->height);
  }
  ShiftLayoutBox(box, 0.0f, y - box->y);
}

}  // namespace blink

#endif  // BLINK_HAS_PAINT_PIPELINE
