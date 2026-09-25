#ifndef BLINK_INLINE_LAYOUT_H_
#define BLINK_INLINE_LAYOUT_H_

#include "blink/css.h"
#include "blink/dom.h"
#include "blink/font_set.h"
#include "blink/layout.h"

#include <functional>
#include <string>
#include <vector>

namespace blink {

// A clean-room inline formatting context, built from CSS 2.1 section 9.4.2
// and css-text-3 rather than from any browser's sources.
//
// The shape follows the same two-stage split a modern engine uses: the
// nested tree of inline boxes is first flattened into a linear list of
// items, each of which still remembers its own computed style and DOM
// node, and a second pass (the line breaker) walks that list filling line
// boxes and emitting one positioned fragment per run. Keeping style on the
// item is the whole point -- a flatten-to-one-string collector cannot
// represent `a <b>bold</b> word`, and that is what the previous engine
// did.
enum class InlineItemType {
  kText,          // a run of text sharing one computed style
  kAtomicInline,  // inline-block / replaced content: an opaque box on the line
  kBreak,         // forced line break (<br>)
  kFloat,         // floated box found inside inline content
};

struct InlineItem {
  InlineItemType type = InlineItemType::kText;
  // kText: the run's text, still uncollapsed. The line breaker applies
  // white-space collapsing, because whether a space is collapsible
  // depends on the run's own `white-space` and on what preceded it.
  std::string text;
  // Element the item belongs to. For text this is the nearest element
  // ancestor, so link text reports the <a>.
  const Node* source = nullptr;
  ComputedStyle style;
  // kAtomicInline / kFloat: the element to lay out as a box.
  const Node* box_node = nullptr;
  // ::before / ::after pseudo for generated atomic content.
  PseudoElement pseudo = PseudoElement::kNone;
};

// Flattens `parent`'s inline-level children into `out`. Descends through
// nested inline boxes, computing each one's style against its real parent
// so `<p><b>x <i>y</i></b></p>` yields bold and bold-italic runs rather
// than two copies of the paragraph's style. display:none is skipped;
// inline-block and replaced elements become kAtomicInline; floats become
// kFloat so the caller can hand them to its float machinery.
void CollectInlineItems(const Node& parent, const ComputedStyle& parent_style,
                        const Stylesheet& sheet, std::vector<InlineItem>* out);

// Appends the items for a single inline-level node whose style the caller
// has already cascaded. Block layout uses this to feed one child at a time
// into the line it is currently filling.
void CollectInlineItemsForNode(const Node& node, const ComputedStyle& style,
                               const Stylesheet& sheet, std::vector<InlineItem>* out);

// Lays out a box that is itself atomic on the line (inline-block, <img>).
// Supplied by the block layout code so this module does not have to depend
// on the block algorithms.
using AtomicLayoutFn = std::function<LayoutBox(const Node& node, const ComputedStyle& style,
                                               float available_width, PseudoElement pseudo)>;

// Reports the horizontal band actually free of floats over a vertical
// span, so text can wrap beside a float and widen again below it.
using AvailableBandFn =
    std::function<void(float y_top, float y_bottom, float* out_x, float* out_width)>;

struct InlineLayoutInput {
  // The block that establishes this inline formatting context. Supplies
  // text-align and the strut (the line box's minimum height).
  const ComputedStyle* block_style = nullptr;
  const FontSet* fonts = nullptr;
  float content_x = 0;
  float content_width = 0;
  float start_y = 0;
  AvailableBandFn available_band;  // optional; full content box when unset
  AtomicLayoutFn layout_atomic;    // optional; atomic items are skipped when unset
};

struct InlineLayoutOutput {
  std::vector<InlineFragment> fragments;
  std::vector<LayoutBox> atomic_boxes;
  float end_y = 0;    // bottom of the last line box
  float max_width = 0;  // widest line, for intrinsic sizing
};

// Fills line boxes from `items` and positions one fragment per run.
// Baselines are aligned across a line using FontMetrics, so a 28px <h1>
// span and a 13px <small> on the same line share a baseline instead of
// each sitting at its own font size below the line top.
InlineLayoutOutput LayoutInlineItems(const std::vector<InlineItem>& items,
                                     const InlineLayoutInput& input);

// Width of an atomic inline for intrinsic sizing. Supplied by the block
// layout code, which is the only side that knows how to size a box.
using AtomicWidthFn = std::function<float(const Node& node, const ComputedStyle& style)>;

// CSS Sizing 3 intrinsic contributions of an inline formatting context.
//
// max-content is the width the content would take with no wrapping at all;
// min-content is the width of the widest single unbreakable atom. Both are
// needed, because CSS 2.1's shrink-to-fit (10.3.5) is
// min(max(min-content, available), max-content) -- which is what sizes
// floats, inline-blocks, absolutely positioned boxes and table columns.
// The predecessor measured Node::TextContent() with a single face, so it
// ignored per-run font sizes and had no notion of min-content at all.
struct InlineIntrinsicWidths {
  float min_content = 0;
  float max_content = 0;
};

InlineIntrinsicWidths InlineIntrinsicSizes(const std::vector<InlineItem>& items,
                                           const FontSet& fonts,
                                           const AtomicWidthFn& atomic_width = AtomicWidthFn());

float InlineMaxContentWidth(const std::vector<InlineItem>& items, const FontSet& fonts);

}  // namespace blink

#endif  // BLINK_INLINE_LAYOUT_H_
