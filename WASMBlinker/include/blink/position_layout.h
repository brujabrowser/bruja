#ifndef BLINK_POSITION_LAYOUT_H_
#define BLINK_POSITION_LAYOUT_H_

#include "blink/layout.h"

namespace blink {

// CSS Positioned Layout: apply `position: sticky` constraints after the
// in-flow position is known. `viewport_scroll_y` is the viewport scroll
// offset; `scrollport_top` is the top of the nearest scrollport's visible
// area in document coordinates (viewport top or a scroll container's
// padding edge). `cb_top`/`cb_bottom` bound that scrollport.
void ResolveStickyPositions(LayoutBox* box, const HtmlDocument* document, float scrollport_top,
                          float scrollport_scroll_y, float cb_top, float cb_bottom);

}  // namespace blink

#endif  // BLINK_POSITION_LAYOUT_H_
