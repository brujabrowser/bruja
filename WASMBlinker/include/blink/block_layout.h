#ifndef BLINK_BLOCK_LAYOUT_H_
#define BLINK_BLOCK_LAYOUT_H_

#include "blink/css.h"

namespace blink {

// CSS 2.1 section 8.3.1 margin collapsing between adjoining in-flow
// block-level boxes. Returns the single margin that survives the
// collapse (not the sum).
float CollapseMargins(float upper, float lower);

// True when `style` establishes an independent block formatting context
// (CSS 2.1 section 9.4.1 / BFC triggers used here). A BFC contains
// floats and prevents margin collapse through its root with the parent.
bool EstablishesBlockFormattingContext(const ComputedStyle& style);

// Per-axis overflow helpers (CSS overflow-x / overflow-y).
bool ClipsOverflowX(const ComputedStyle& style);
bool ClipsOverflowY(const ComputedStyle& style);
bool ScrollsOverflowX(const ComputedStyle& style);
bool ScrollsOverflowY(const ComputedStyle& style);

// True when either axis clips or scrolls.
bool ClipsOverflow(const ComputedStyle& style);
bool ScrollsOverflow(const ComputedStyle& style);

// Parent and first/last in-flow child margins collapse when the parent has
// no border or padding on that edge and does not clip overflow (CSS 2.1
// 8.3.1 / 8.4.3).
bool CanCollapseParentTopWithFirstChild(const ComputedStyle& parent);
bool CanCollapseParentBottomWithLastChild(const ComputedStyle& parent);

// Padding box of a laid-out border box. Used for overflow clips.
struct PaddingBox {
  float x = 0, y = 0, width = 0, height = 0;
};

PaddingBox ComputePaddingBox(const ComputedStyle& style, float x, float y, float width,
                             float height);

}  // namespace blink

#endif  // BLINK_BLOCK_LAYOUT_H_
