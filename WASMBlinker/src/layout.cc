#include "blink/layout.h"

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "blink/font_set.h"
#include "blink/inline_layout.h"
#include "blink/block_layout.h"
#include "blink/position_layout.h"
#include "blink/position_layout.h"

#include "wasmskia/font.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blink {

void ShiftLayoutBox(LayoutBox* box, float dx, float dy) {
  if (!box || (dx == 0.0f && dy == 0.0f)) return;
  box->x += dx;
  box->y += dy;
  for (InlineFragment& frag : box->fragments) {
    frag.x += dx;
    frag.y += dy;
  }
  for (LayoutBox& child : box->children) ShiftLayoutBox(&child, dx, dy);
}

namespace {

// One active float. Real CSS scopes a float to its own containing block,
// but does *not* stop it from narrowing the line boxes of inline content
// inside nested descendant blocks that don't themselves establish a new
// block formatting context (the common "floated image, paragraph wraps
// around it" case almost always has the paragraph as a plain sibling or
// nested <div>/<p>, not something that starts its own BFC). This engine
// doesn't model BFC-establishment triggers (overflow, position, etc) --
// FloatState simply chains to its creator's FloatState, so floats are
// visible to every descendant's own text wrapping, unconditionally. That
// over-includes relative to strict CSS (a real new-BFC descendant would
// ignore ancestor floats); documented here rather than silently assumed.
struct FloatBox {
  float x_left = 0, x_right = 0, y_top = 0, y_bottom = 0;
  FloatType side = FloatType::kNone;
};

class FloatState {
 public:
  explicit FloatState(const FloatState* parent = nullptr) : parent_(parent) {}

  void Add(const FloatBox& f) { floats_.push_back(f); }

  // Narrows [content_x, content_x+content_width) to the space actually
  // free of floats (this level's own, and every ancestor level's) that
  // overlap the vertical span [y_top, y_bottom).
  void AvailableRect(float content_x, float content_width, float y_top, float y_bottom,
                     float* out_x, float* out_width) const {
    float left = content_x;
    float right = content_x + content_width;
    for (const FloatState* fs = this; fs; fs = fs->parent_) {
      for (const FloatBox& f : fs->floats_) {
        if (f.y_bottom <= y_top || f.y_top >= y_bottom) continue;
        if (f.side == FloatType::kLeft) left = std::max(left, f.x_right);
        else if (f.side == FloatType::kRight) right = std::min(right, f.x_left);
      }
    }
    *out_x = left;
    *out_width = std::max(0.0f, right - left);
  }

  float ClearY(float y, ClearType clear) const {
    if (clear == ClearType::kNone) return y;
    float result = y;
    for (const FloatState* fs = this; fs; fs = fs->parent_) {
      for (const FloatBox& f : fs->floats_) {
        bool matches = clear == ClearType::kBoth ||
                      (clear == ClearType::kLeft && f.side == FloatType::kLeft) ||
                      (clear == ClearType::kRight && f.side == FloatType::kRight);
        if (matches) result = std::max(result, f.y_bottom);
      }
    }
    return result;
  }

  // Only this level's own floats contribute to the containing block's own
  // height -- an ancestor's float bottom doesn't belong to *this* block.
  float MaxBottom() const {
    float m = 0;
    for (const FloatBox& f : floats_) m = std::max(m, f.y_bottom);
    return m;
  }

 private:
  const FloatState* parent_;
  std::vector<FloatBox> floats_;
};

// CSS Sizing 3 intrinsic contributions of a box.
struct IntrinsicWidths {
  float min_content = 0;
  float max_content = 0;
};

// An out-of-flow box, held until the ancestor that is actually its
// containing block has been laid out. CSS 2.1 10.1: that ancestor is the
// nearest *positioned* one, which is not in general the parent -- so an
// absolutely positioned box cannot be resolved at the point it is found.
struct PendingAbsolute {
  const Node* node = nullptr;
  ComputedStyle style;
  // Where the box would have sat in normal flow, used when its offsets are
  // auto (CSS 2.1 10.3.7's "static position").
  float static_x = 0;
  float static_y = 0;
};

struct CounterState {
  std::unordered_map<std::string, int> values;
  std::vector<std::unordered_map<std::string, int>> reset_stack;

  void OnEnter(const ComputedStyle& s) {
    if (!s.counter_reset_name.empty()) {
      reset_stack.push_back(values);
      values[s.counter_reset_name] = s.counter_reset_value;
    }
    if (!s.counter_increment_name.empty()) {
      values[s.counter_increment_name] += s.counter_increment_value;
    }
  }

  void OnExit(const ComputedStyle& s) {
    if (!s.counter_reset_name.empty() && !reset_stack.empty()) {
      values = reset_stack.back();
      reset_stack.pop_back();
    }
  }
};

struct CounterGuard {
  CounterState* state;
  const ComputedStyle& style;
  CounterGuard(CounterState* s, const ComputedStyle& st) : state(s), style(st) {
    if (state) state->OnEnter(style);
  }
  ~CounterGuard() {
    if (state) state->OnExit(style);
  }
};

struct LayoutCtx {
  const Stylesheet* sheet = nullptr;
  const FontSet* fonts = nullptr;
  const HtmlDocument* document = nullptr;
  CounterState* counters = nullptr;
  float viewport_w = 0;
  float viewport_h = 0;
  float scroll_x = 0;
  float scroll_y = 0;
  // Out-of-flow boxes found so far, oldest first. Owned by ComputeLayout.
  std::vector<PendingAbsolute>* abs_sink = nullptr;
  // Intrinsic sizing is recursive over the subtree and is asked for per
  // float, per inline-block and per table cell, so without memoization a
  // wide document costs a pass per candidate. Styles do not change during
  // one layout, so the node alone is a sound key.
  std::unordered_map<const Node*, IntrinsicWidths>* intrinsic_cache = nullptr;

  // Measurement face for code that has no per-run style of its own.
  // Real runs resolve their own face.
  const wasmskia::Font* font() const { return fonts ? fonts->regular : nullptr; }
};

bool IsOutOfFlow(const ComputedStyle& s) {
  return s.position == PositionType::kAbsolute || s.position == PositionType::kFixed;
}

bool IsInlineLevel(const ComputedStyle& style);

LayoutBox LayoutBlockChild(const Node& node, const ComputedStyle& style, float x,
                          float avail_width, float y, const LayoutCtx& ctx,
                          const FloatState* inherited_floats = nullptr);

// True for lengths an intrinsic pass can evaluate. Percentages have no
// base to resolve against here, so CSS Sizing 3 treats them as auto.
bool IsDefiniteLength(const Length& len) {
  return len.unit == Length::Unit::kPx || len.unit == Length::Unit::kEm ||
         len.unit == Length::Unit::kRem;
}

IntrinsicWidths ComputeIntrinsicWidths(const Node& node, const ComputedStyle& style,
                                       const LayoutCtx& ctx);

// CSS 2.1 10.3.5: min(max(min-content, available), max-content). This is
// the correct width for a float, an inline-block, an absolutely positioned
// box and a table column -- everything whose width is auto but whose
// containing block does not hand it a width.
float ShrinkToFitWidth(const Node& node, const ComputedStyle& style, const LayoutCtx& ctx,
                       float available) {
  const IntrinsicWidths w = ComputeIntrinsicWidths(node, style, ctx);
  return std::min(std::max(w.min_content, available), w.max_content);
}

IntrinsicWidths ComputeIntrinsicWidthsUncached(const Node& node, const ComputedStyle& style,
                                               const LayoutCtx& ctx) {
  const float em = style.font_size > 0 ? style.font_size : 16.0f;
  IntrinsicWidths out;

  // box-sizing is border-box throughout, so a definite width is the answer
  // outright and everything else has to have the surround added at the end.
  if (IsDefiniteLength(style.width)) {
    out.min_content = out.max_content = style.width.Resolve(0, 0, em);
    return out;
  }

  float surround = style.border_left + style.border_right;
  if (IsDefiniteLength(style.padding_left)) surround += style.padding_left.Resolve(0, 0, em);
  if (IsDefiniteLength(style.padding_right)) surround += style.padding_right.Resolve(0, 0, em);
  if (style.display == DisplayType::kListItem && style.list_style_type != ListStyleType::kNone) {
    surround += std::max(14.0f, em * 0.9f);  // marker gutter, as in LayoutBlockChild
  }

  // Replaced content contributes its own intrinsic width, not its text.
  if (node.tag_name == "img") {
    int iw = 0;
    if (ctx.document) {
      auto it = ctx.document->images.find(&node);
      if (it != ctx.document->images.end()) iw = it->second.width;
    }
    if (iw == 0) iw = std::atoi(node.GetAttribute("width").c_str());
    out.min_content = out.max_content = static_cast<float>(iw) + surround;
    return out;
  }
  if (node.tag_name == "input" || node.tag_name == "textarea") {
    std::string value = node.GetAttribute("value");
    if (value.empty()) value = node.GetAttribute("placeholder");
    if (value.empty()) value = "xxxxxxxxxx";
    float tw = 0;
    if (ctx.fonts) {
      if (const wasmskia::Font* face = ctx.fonts->Resolve(style)) {
        tw = wasmskia::MeasureText(*face, value, em);
      }
    }
    out.min_content = out.max_content = tw + surround + 8.0f;
    return out;
  }

  // Along a row axis the children sit side by side, so their contributions
  // add; along a block axis they stack, so the widest one wins. A table and
  // a row group hold rows, which stack -- only a row holds cells.
  const bool sums =
      (style.display == DisplayType::kFlex && style.flex_direction == FlexDirection::kRow) ||
      style.display == DisplayType::kTableRow;

  std::vector<InlineItem> pending;
  float sum_min = 0, sum_max = 0;
  int in_row = 0;

  // Consecutive inline-level children are measured as one inline
  // formatting context, so `a <b>b</b> c` is one run and not three.
  auto flush_inline = [&]() {
    if (pending.empty()) return;
    if (ctx.fonts) {
      AtomicWidthFn atomic = [&ctx](const Node& n, const ComputedStyle& s) {
        return ComputeIntrinsicWidths(n, s, ctx).max_content;
      };
      const InlineIntrinsicWidths w = InlineIntrinsicSizes(pending, *ctx.fonts, atomic);
      out.min_content = std::max(out.min_content, w.min_content);
      out.max_content = std::max(out.max_content, w.max_content);
    }
    pending.clear();
  };

  for (const auto& child : node.children) {
    if (child->type == NodeType::kText) {
      if (child->text_data.empty()) continue;
      InlineItem item;
      item.type = InlineItemType::kText;
      item.text = child->text_data;
      item.source = &node;
      item.style = style;
      pending.push_back(std::move(item));
      continue;
    }
    if (child->type != NodeType::kElement) continue;
    const std::string& tag = child->tag_name;
    if (tag == "head" || tag == "script" || tag == "style" || tag == "title") continue;
    ComputedStyle cs = ComputeStyle(*child, *ctx.sheet, &style);
    if (cs.display == DisplayType::kNone || IsOutOfFlow(cs)) continue;

    if (cs.float_type == FloatType::kNone && (IsInlineLevel(cs) || tag == "br")) {
      CollectInlineItemsForNode(*child, cs, *ctx.sheet, &pending);
      continue;
    }
    flush_inline();
    IntrinsicWidths w = ComputeIntrinsicWidths(*child, cs, ctx);
    float margins = 0;
    if (IsDefiniteLength(cs.margin_left)) margins += cs.margin_left.Resolve(0, 0, em);
    if (IsDefiniteLength(cs.margin_right)) margins += cs.margin_right.Resolve(0, 0, em);
    w.min_content += margins;
    w.max_content += margins;
    if (sums) {
      sum_min += w.min_content;
      sum_max += w.max_content;
      ++in_row;
    } else {
      out.min_content = std::max(out.min_content, w.min_content);
      out.max_content = std::max(out.max_content, w.max_content);
    }
  }
  flush_inline();

  if (sums) {
    const float gaps = in_row > 1 ? style.gap * static_cast<float>(in_row - 1) : 0.0f;
    out.min_content = std::max(out.min_content, sum_min + gaps);
    out.max_content = std::max(out.max_content, sum_max + gaps);
  }

  out.min_content += surround;
  out.max_content += surround;
  if (IsDefiniteLength(style.min_width)) {
    const float mw = style.min_width.Resolve(0, 0, em);
    out.min_content = std::max(out.min_content, mw);
    out.max_content = std::max(out.max_content, mw);
  }
  if (IsDefiniteLength(style.max_width)) {
    const float mw = style.max_width.Resolve(0, 0, em);
    out.min_content = std::min(out.min_content, mw);
    out.max_content = std::min(out.max_content, mw);
  }
  out.max_content = std::max(out.max_content, out.min_content);
  return out;
}

IntrinsicWidths ComputeIntrinsicWidths(const Node& node, const ComputedStyle& style,
                                       const LayoutCtx& ctx) {
  if (!ctx.intrinsic_cache) return ComputeIntrinsicWidthsUncached(node, style, ctx);
  auto it = ctx.intrinsic_cache->find(&node);
  if (it != ctx.intrinsic_cache->end()) return it->second;
  const IntrinsicWidths w = ComputeIntrinsicWidthsUncached(node, style, ctx);
  (*ctx.intrinsic_cache)[&node] = w;
  return w;
}

// flex-basis: auto resolves to the item's content size. Laying the item out
// at the flex container's full width made every auto-width button 1024px,
// then flex-shrink crushed the row to un-clickable slivers.
float FlexItemBaseWidth(const Node& node, const ComputedStyle& style, const LayoutCtx& ctx,
                        float cap) {
  const float em = style.font_size > 0 ? style.font_size : 13.0f;
  if (!style.flex_basis.is_auto()) return style.flex_basis.Resolve(cap, 0, em);
  if (!style.width.is_auto()) return style.width.Resolve(cap, 0, em);
  return std::min(cap, ComputeIntrinsicWidths(node, style, ctx).max_content);
}

LayoutBox LayoutPseudoTextBox(const Node& node, PseudoElement which, const ComputedStyle& container,
                              const LayoutCtx& ctx) {
  LayoutBox box;
  if (!ctx.sheet) return box;
  ComputedStyle ps = ComputePseudoStyle(node, which, *ctx.sheet, &container);

  if (ps.content_is_url && ctx.document) {
    const DecodedImage* img = ResolvePseudoContentImage(node, ps, *ctx.document);
    if (!img) return box;
    ps.display = DisplayType::kInlineBlock;
    const float em = ps.font_size > 0 ? ps.font_size : 16.0f;
    const float iw = static_cast<float>(img->width);
    const float ih = static_cast<float>(img->height);
    const float pad_l = ps.padding_left.Resolve(iw, 0, em);
    const float pad_r = ps.padding_right.Resolve(iw, 0, em);
    const float pad_t = ps.padding_top.Resolve(iw, 0, em);
    const float pad_b = ps.padding_bottom.Resolve(iw, 0, em);
    box.source = &node;
    box.style = ps;
    box.content_image = img;
    box.width = iw + ps.border_left + ps.border_right + pad_l + pad_r;
    box.height = ih + ps.border_top + ps.border_bottom + pad_t + pad_b;
    return box;
  }

  const std::unordered_map<std::string, int>* counter_map =
      ctx.counters ? &ctx.counters->values : nullptr;
  const std::string text = ResolveGeneratedContent(node, ps, ctx.document, counter_map);
  if (text.empty()) return box;
  ps.display = DisplayType::kInlineBlock;
  const float em = ps.font_size > 0 ? ps.font_size : 16.0f;
  float tw = 0.0f;
  const wasmskia::Font* face = ctx.fonts ? ctx.fonts->Resolve(ps) : nullptr;
  if (face) tw = wasmskia::MeasureText(*face, text, em);
  const FontMetrics metrics = FontMetrics::ForFace(face, em);
  const float pad_l = ps.padding_left.Resolve(tw, 0, em);
  const float pad_r = ps.padding_right.Resolve(tw, 0, em);
  const float pad_t = ps.padding_top.Resolve(tw, 0, em);
  const float pad_b = ps.padding_bottom.Resolve(tw, 0, em);
  box.source = &node;
  box.style = ps;
  box.width = tw + ps.border_left + ps.border_right + pad_l + pad_r;
  box.height = metrics.height() + ps.border_top + ps.border_bottom + pad_t + pad_b;
  InlineFragment frag;
  frag.text = text;
  frag.source = &node;
  frag.style = ps;
  frag.width = tw;
  frag.ascent = metrics.ascent;
  frag.descent = metrics.descent;
  box.fragments.push_back(std::move(frag));
  return box;
}

bool PseudoBoxHasContent(const LayoutBox& box) {
  return !box.fragments.empty() || box.content_image != nullptr;
}

float LayoutFlexChildren(const Node& node, const ComputedStyle& flex_style, const LayoutCtx& ctx,
                         float content_x, float content_y, float content_width,
                         std::vector<LayoutBox>* out_children) {
  struct Item {
    const Node* node;
    ComputedStyle style;
    PseudoElement pseudo = PseudoElement::kNone;
  };
  std::vector<Item> items;
  LayoutBox pseudo_before = LayoutPseudoTextBox(node, PseudoElement::kBefore, flex_style, ctx);
  if (PseudoBoxHasContent(pseudo_before)) {
    items.push_back({&node, pseudo_before.style, PseudoElement::kBefore});
  }
  for (const auto& child : node.children) {
    if (child->type != NodeType::kElement) continue;
    if (child->tag_name == "script" || child->tag_name == "style") continue;
    ComputedStyle cs = ComputeStyle(*child, *ctx.sheet, &flex_style);
    if (cs.display == DisplayType::kNone) continue;
    // An absolutely positioned child of a flex container is not a flex
    // item (CSS Flexbox 1 section 4.1); it is laid out against its
    // containing block like any other out-of-flow box.
    if (IsOutOfFlow(cs)) {
      if (ctx.abs_sink) ctx.abs_sink->push_back({child.get(), cs, content_x, content_y});
      continue;
    }
    items.push_back({child.get(), cs, PseudoElement::kNone});
  }
  LayoutBox pseudo_after = LayoutPseudoTextBox(node, PseudoElement::kAfter, flex_style, ctx);
  if (PseudoBoxHasContent(pseudo_after)) {
    items.push_back({&node, pseudo_after.style, PseudoElement::kAfter});
  }
  if (items.empty()) return 0.0f;

  auto flex_item_base = [&](const Item& it) -> float {
    if (it.pseudo != PseudoElement::kNone) {
      return LayoutPseudoTextBox(*it.node, it.pseudo, flex_style, ctx).width;
    }
    return FlexItemBaseWidth(*it.node, it.style, ctx, content_width);
  };

  auto place_flex_item = [&](const Item& it, const ComputedStyle& flexed, float x, float y,
                             float w) -> LayoutBox {
    if (it.pseudo != PseudoElement::kNone) {
      LayoutBox box = LayoutPseudoTextBox(*it.node, it.pseudo, flex_style, ctx);
      box.style = flexed;
      box.width = std::max(box.width, w);
      box.x = x;
      box.y = y;
      const float em = box.style.font_size > 0 ? box.style.font_size : 16.0f;
      const float pad_l = box.style.padding_left.Resolve(box.width, 0, em);
      const float pad_t = box.style.padding_top.Resolve(box.width, 0, em);
      if (!box.fragments.empty()) {
        const wasmskia::Font* face = ctx.fonts ? ctx.fonts->Resolve(box.style) : nullptr;
        const FontMetrics metrics = FontMetrics::ForFace(face, em);
        box.fragments[0].x = x + box.style.border_left + pad_l;
        box.fragments[0].y = y + box.style.border_top + pad_t + metrics.ascent;
      }
      return box;
    }
    return LayoutBlockChild(*it.node, flexed, x, w, y, ctx);
  };

  const bool row = flex_style.flex_direction == FlexDirection::kRow;
  const float gap = flex_style.gap;
  std::vector<LayoutBox> boxes;
  boxes.reserve(items.size());
  float main_total;
  float cross_size;

  if (row) {
    std::vector<float> base(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
      base[i] = flex_item_base(items[i]);
    }

    struct Line {
      std::vector<size_t> indices;
      float used = 0;
    };
    std::vector<Line> lines;
    if (flex_style.flex_wrap == FlexWrap::kWrap) {
      Line current;
      for (size_t i = 0; i < items.size(); ++i) {
        const float w = base[i];
        const float extra = current.indices.empty() ? 0.0f : gap;
        if (!current.indices.empty() && current.used + extra + w > content_width + 0.01f) {
          lines.push_back(std::move(current));
          current = {};
        }
        if (!current.indices.empty()) current.used += gap;
        current.indices.push_back(i);
        current.used += w;
      }
      if (!current.indices.empty()) lines.push_back(std::move(current));
    } else {
      Line all;
      all.indices.resize(items.size());
      for (size_t i = 0; i < items.size(); ++i) all.indices[i] = i;
      for (float b : base) all.used += b;
      all.used += gap * std::max(0, static_cast<int>(items.size()) - 1);
      lines.push_back(std::move(all));
    }

    float total_cross = 0.0f;
    float line_y = content_y;
    for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
      const Line& line = lines[line_idx];
      if (line.indices.empty()) continue;
      std::vector<float> line_base(line.indices.size());
      float line_grow = 0.0f, line_shrink = 0.0f;
      for (size_t li = 0; li < line.indices.size(); ++li) {
        const size_t i = line.indices[li];
        line_base[li] = base[i];
        line_grow += items[i].style.flex_grow;
        line_shrink += items[i].style.flex_shrink;
      }
      const float gap_total = gap * std::max(0, static_cast<int>(line.indices.size()) - 1);
      float used = gap_total;
      for (float b : line_base) used += b;
      float free_space = content_width - used;
      std::vector<float> assigned = line_base;
      if (free_space > 0.0f && line_grow > 0.0f) {
        for (size_t li = 0; li < line.indices.size(); ++li) {
          const size_t i = line.indices[li];
          assigned[li] += free_space * (items[i].style.flex_grow / line_grow);
        }
      } else if (free_space < 0.0f && line_shrink > 0.0f) {
        for (size_t li = 0; li < line.indices.size(); ++li) {
          const size_t i = line.indices[li];
          assigned[li] += free_space * (items[i].style.flex_shrink / line_shrink);
          assigned[li] = std::max(0.0f, assigned[li]);
        }
      }

      float cursor_x = content_x;
      float cross_max = 0.0f;
      std::vector<LayoutBox> line_boxes;
      line_boxes.reserve(line.indices.size());
      for (size_t li = 0; li < line.indices.size(); ++li) {
        const size_t i = line.indices[li];
        const float w = assigned[li];
        ComputedStyle flexed = items[i].style;
        flexed.width.unit = Length::Unit::kPx;
        flexed.width.value = w;
        LayoutBox box = place_flex_item(items[i], flexed, cursor_x, line_y, w);
        cross_max = std::max(cross_max, box.height);
        line_boxes.push_back(std::move(box));
        cursor_x += w + gap;
      }
      const float main_total = cursor_x - gap - content_x;

      float justify_space = std::max(0.0f, content_width - main_total);
      float leading = 0.0f, extra_gap = 0.0f;
      switch (flex_style.justify_content) {
        case JustifyContent::kFlexEnd: leading = justify_space; break;
        case JustifyContent::kCenter: leading = justify_space / 2.0f; break;
        case JustifyContent::kSpaceBetween:
          extra_gap = line.indices.size() > 1
                          ? justify_space / static_cast<float>(line.indices.size() - 1)
                          : 0.0f;
          break;
        case JustifyContent::kSpaceAround:
          leading = line.indices.empty() ? 0.0f
                                         : justify_space / static_cast<float>(line.indices.size() * 2);
          extra_gap = line.indices.empty() ? 0.0f
                                           : justify_space / static_cast<float>(line.indices.size());
          break;
        default: break;
      }
      float running = leading;
      for (size_t li = 0; li < line_boxes.size(); ++li) {
        float dx = running - (line_boxes[li].x - content_x);
        float dy = 0.0f;
        const float extra_cross = cross_max - line_boxes[li].height;
        switch (flex_style.align_items) {
          case AlignItems::kFlexEnd: dy = extra_cross; break;
          case AlignItems::kCenter: dy = extra_cross / 2.0f; break;
          default: break;
        }
        ShiftLayoutBox(&line_boxes[li], dx, dy);
        running += line_boxes[li].width + gap + extra_gap;
      }
      for (LayoutBox& b : line_boxes) boxes.push_back(std::move(b));
      total_cross += cross_max;
      if (line_idx + 1 < lines.size()) total_cross += gap;
      line_y += cross_max + gap;
    }
    main_total = line_y - gap - content_y;
    cross_size = total_cross;
  } else {
    float cursor_y = content_y;
    float cross_max = 0.0f;
    for (const Item& it : items) {
      float w = it.style.width.is_auto() ? content_width : it.style.width.Resolve(content_width, content_width);
      LayoutBox box = place_flex_item(it, it.style, content_x, cursor_y, w);
      cross_max = std::max(cross_max, box.width);
      cursor_y += box.height + gap;
      boxes.push_back(std::move(box));
    }
    main_total = cursor_y - gap - content_y;
    cross_size = cross_max;

    float free_space = std::max(0.0f, 0.0f);  // column main-axis (height) is intrinsic here; no fixed container height to justify against.
    (void)free_space;
    for (LayoutBox& box : boxes) {
      float extra_cross = content_width - box.width;
      float dx = 0.0f;
      switch (flex_style.align_items) {
        case AlignItems::kFlexEnd: dx = extra_cross; break;
        case AlignItems::kCenter: dx = extra_cross / 2.0f; break;
        default: break;
      }
      if (dx != 0.0f) ShiftLayoutBox(&box, dx, 0.0f);
    }
  }

  (void)cross_size;
  for (LayoutBox& b : boxes) out_children->push_back(std::move(b));
  return row ? cross_size : main_total;
}

// True for content that belongs on a line box rather than in its own
// block row. inline-block used to be treated as block here, which is why
// a row of buttons stacked vertically.
bool IsInlineLevel(const ComputedStyle& style) {
  return style.display == DisplayType::kInline || style.display == DisplayType::kInlineBlock;
}

// The block formatting context: walks `node`'s children in order, mixing
// inline content (handed to the inline formatting context, which wraps it
// float-aware and keeps per-run style), block children (stacked
// vertically, each its own recursive box), and floated children (taken out
// of normal flow, narrowing subsequent lines).
float LayoutBlockFormattingContext(const Node& node, const ComputedStyle& own_style,
                                   const LayoutCtx& ctx, float content_x, float content_y,
                                   float content_width, std::vector<LayoutBox>* out_children,
                                   std::vector<InlineFragment>* out_fragments,
                                   const FloatState* inherited_floats) {
  FloatState floats(inherited_floats);
  float cursor_y = content_y;
  float border_bottom_y = content_y;
  float pending_mb = 0.0f;
  bool first_in_flow_block = true;
  bool had_block_child = false;
  std::vector<InlineItem> pending;

  auto append_pseudo = [&](PseudoElement which) {
    if (!ctx.sheet) return;
    ComputedStyle ps = ComputePseudoStyle(node, which, *ctx.sheet, &own_style);
    if (ps.content_is_url && ctx.document) {
      LayoutBox probe = LayoutPseudoTextBox(node, which, own_style, ctx);
      if (!probe.content_image) return;
      InlineItem item;
      item.type = InlineItemType::kAtomicInline;
      item.source = &node;
      item.style = ps;
      item.box_node = &node;
      item.pseudo = which;
      pending.push_back(std::move(item));
      return;
    }
    const std::unordered_map<std::string, int>* counter_map =
        ctx.counters ? &ctx.counters->values : nullptr;
    const std::string text = ResolveGeneratedContent(node, ps, ctx.document, counter_map);
    if (text.empty()) return;
    InlineItem item;
    item.type = InlineItemType::kText;
    item.text = text;
    item.source = &node;
    item.style = ps;
    pending.push_back(std::move(item));
  };

  // Runs the inline formatting context over everything buffered so far.
  // Called before any block-level child so inline content keeps its
  // document order relative to blocks.
  auto flush_inline = [&]() {
    if (pending.empty()) return;
    InlineLayoutInput in;
    in.block_style = &own_style;
    in.fonts = ctx.fonts;
    in.content_x = content_x;
    in.content_width = content_width;
    in.start_y = cursor_y;
    in.available_band = [&](float y_top, float y_bottom, float* x, float* w) {
      floats.AvailableRect(content_x, content_width, y_top, y_bottom, x, w);
    };
    in.layout_atomic = [&](const Node& n, const ComputedStyle& s, float avail, PseudoElement pseudo) {
      if (pseudo != PseudoElement::kNone) {
        return LayoutPseudoTextBox(n, pseudo, own_style, ctx);
      }
      // An atomic inline is shrink-to-fit, not full-width. Replaced
      // elements size themselves from intrinsic dimensions inside
      // LayoutBlockChild, so leave their auto width alone.
      ComputedStyle sized = s;
      const bool self_sizing =
          n.tag_name == "img" || n.tag_name == "input" || n.tag_name == "textarea";
      if (sized.width.is_auto() && !self_sizing) {
        sized.width.unit = Length::Unit::kPx;
        sized.width.value = ShrinkToFitWidth(n, s, ctx, avail);
      }
      return LayoutBlockChild(n, sized, 0.0f, avail, 0.0f, ctx, &floats);
    };
    InlineLayoutOutput result = LayoutInlineItems(pending, in);
    for (InlineFragment& frag : result.fragments) out_fragments->push_back(std::move(frag));
    for (LayoutBox& box : result.atomic_boxes) out_children->push_back(std::move(box));
    cursor_y = result.end_y;
    border_bottom_y = cursor_y;
    pending_mb = 0.0f;
    pending.clear();
  };

  append_pseudo(PseudoElement::kBefore);

  for (const auto& child : node.children) {
    if (child->type == NodeType::kText) {
      if (child->text_data.empty()) continue;
      InlineItem item;
      item.type = InlineItemType::kText;
      item.text = child->text_data;
      item.source = &node;
      item.style = own_style;
      pending.push_back(std::move(item));
      continue;
    }
    if (child->type != NodeType::kElement) continue;
    const std::string& tag = child->tag_name;
    if (tag == "head" || tag == "script" || tag == "style" || tag == "title") continue;

    ComputedStyle child_style = ComputeStyle(*child, *ctx.sheet, &own_style);
    if (child_style.display == DisplayType::kNone) continue;
    if (IsOutOfFlow(child_style)) {
      // Deferred: this box's containing block is the nearest positioned
      // ancestor, which may be far above. Record the static position now,
      // because after the flow moves on it is not recoverable.
      if (ctx.abs_sink) {
        flush_inline();
        ctx.abs_sink->push_back({child.get(), child_style, content_x, cursor_y});
      }
      continue;
    }

    if (child_style.float_type != FloatType::kNone) {
      flush_inline();
      float avail_x, avail_w;
      floats.AvailableRect(content_x, content_width, cursor_y, cursor_y + 1.0f, &avail_x, &avail_w);
      // CSS 2.1 9.5/10.3.5: a float with width:auto is shrink-to-fit. The
      // fixed 200px this used to guess made every auto-width float either
      // a truncated column or a band of empty space.
      float float_width = child_style.width.is_auto()
                              ? ShrinkToFitWidth(*child, child_style, ctx, avail_w)
                              : child_style.width.Resolve(content_width, avail_w);
      float place_x = (child_style.float_type == FloatType::kLeft) ? avail_x
                                                                  : (avail_x + avail_w - float_width);
      LayoutBox float_box =
          LayoutBlockChild(*child, child_style, place_x, float_width, cursor_y, ctx, &floats);
      floats.Add({float_box.x, float_box.x + float_box.width, float_box.y,
                  float_box.y + float_box.height, child_style.float_type});
      out_children->push_back(std::move(float_box));
      continue;
    }

    if (IsInlineLevel(child_style) || tag == "br") {
      CollectInlineItemsForNode(*child, child_style, *ctx.sheet, &pending);
      continue;
    }

    flush_inline();
    if (child_style.clear != ClearType::kNone) {
      cursor_y = floats.ClearY(cursor_y, child_style.clear);
      border_bottom_y = cursor_y;
      pending_mb = 0.0f;
    }
    const float mt = child_style.margin_top.Resolve(content_width, 0);
    float place_y;
    if (first_in_flow_block && pending_mb == 0.0f &&
        CanCollapseParentTopWithFirstChild(own_style) &&
        !EstablishesBlockFormattingContext(child_style)) {
      place_y = content_y - mt;
    } else {
      const float collapsed = CollapseMargins(pending_mb, mt);
      place_y = border_bottom_y + collapsed - mt;
    }
    first_in_flow_block = false;
    had_block_child = true;
    LayoutBox child_box =
        LayoutBlockChild(*child, child_style, content_x, content_width, place_y, ctx, &floats);
    border_bottom_y = child_box.y + child_box.height;
    pending_mb = child_style.margin_bottom.Resolve(content_width, 0);
    cursor_y = border_bottom_y + pending_mb;
    out_children->push_back(std::move(child_box));
  }
  append_pseudo(PseudoElement::kAfter);
  flush_inline();
  float height = cursor_y - content_y;
  if (had_block_child && CanCollapseParentBottomWithLastChild(own_style)) {
    height = border_bottom_y - content_y;
  }
  return std::max(height, floats.MaxBottom() - content_y);
}

// True for boxes that are a containing block for absolutely positioned
// descendants (CSS 2.1 10.1). `position: static` boxes are not, which is
// exactly why out-of-flow layout has to be deferred rather than done where
// the child is found.
bool IsAbsoluteContainingBlock(const ComputedStyle& s) {
  return s.position != PositionType::kStatic;
}

// Lays out and places every pending out-of-flow box from `from` onward
// against `cb_box` as its containing block. Fixed-position boxes are left
// pending unless this is the initial containing block, since the viewport
// is their containing block no matter which ancestor is positioned.
void ResolveAbsolutes(size_t from, const ComputedStyle& cb_style, LayoutBox* cb_box,
                      const LayoutCtx& ctx, bool is_initial_cb) {
  if (!ctx.abs_sink || ctx.abs_sink->size() <= from) return;
  std::vector<PendingAbsolute>& sink = *ctx.abs_sink;
  // Snapshot: laying these out can discover further out-of-flow boxes,
  // which append to the same sink and are resolved by the nested call.
  std::vector<PendingAbsolute> batch(sink.begin() + static_cast<long>(from), sink.end());
  sink.resize(from);

  // The containing block established by a positioned box is its *padding*
  // box, not its content box, so padding is inside the area offsets and
  // percentages resolve against.
  const float pad_x = cb_box->x + cb_style.border_left;
  const float pad_y = cb_box->y + cb_style.border_top;
  const float pad_w =
      std::max(0.0f, cb_box->width - cb_style.border_left - cb_style.border_right);
  const float pad_h =
      std::max(0.0f, cb_box->height - cb_style.border_top - cb_style.border_bottom);

  std::vector<PendingAbsolute> deferred;
  for (PendingAbsolute& item : batch) {
    const bool fixed = item.style.position == PositionType::kFixed;
    if (fixed && !is_initial_cb) {
      deferred.push_back(std::move(item));
      continue;
    }
    const float cb_x = fixed ? ctx.scroll_x : pad_x;
    const float cb_y = fixed ? ctx.scroll_y : pad_y;
    const float cb_w = fixed ? ctx.viewport_w : pad_w;
    const float cb_h = fixed ? ctx.viewport_h : pad_h;

    ComputedStyle sized = item.style;
    const float em = sized.font_size > 0 ? sized.font_size : 13.0f;
    const bool has_left = !sized.left.is_auto();
    const bool has_right = !sized.right.is_auto();
    const bool has_top = !sized.top.is_auto();
    const bool has_bottom = !sized.bottom.is_auto();
    const std::string& tag = item.node->tag_name;
    const bool self_sizing = tag == "img" || tag == "input" || tag == "textarea";

    // CSS 2.1 10.3.7. With both offsets given the width is what is left
    // between them; otherwise it is shrink-to-fit. Neither is the
    // containing block's full width, which is what an in-flow block would
    // get and what this used to hand out -- the reason an absolutely
    // positioned badge spanned its whole container.
    if (sized.width.is_auto() && !self_sizing) {
      float w;
      if (has_left && has_right) {
        w = std::max(0.0f, cb_w - sized.left.Resolve(cb_w, 0, em) -
                               sized.right.Resolve(cb_w, 0, em));
      } else {
        w = ShrinkToFitWidth(*item.node, sized, ctx, cb_w);
      }
      sized.width.unit = Length::Unit::kPx;
      sized.width.value = w;
    }
    // CSS 2.1 10.6.4, the same rule in the block direction.
    if (sized.height.is_auto() && has_top && has_bottom) {
      sized.height.unit = Length::Unit::kPx;
      sized.height.value = std::max(0.0f, cb_h - sized.top.Resolve(cb_h, 0, em) -
                                              sized.bottom.Resolve(cb_h, 0, em));
    }

    LayoutBox box = LayoutBlockChild(*item.node, sized, cb_x, cb_w, cb_y, ctx);

    float x = item.static_x;
    float y = item.static_y;
    if (has_left) x = cb_x + sized.left.Resolve(cb_w, 0, em);
    else if (has_right) x = cb_x + cb_w - sized.right.Resolve(cb_w, 0, em) - box.width;
    if (has_top) y = cb_y + sized.top.Resolve(cb_h, 0, em);
    else if (has_bottom) y = cb_y + cb_h - sized.bottom.Resolve(cb_h, 0, em) - box.height;

    ShiftLayoutBox(&box, x - box.x, y - box.y);
    cb_box->children.push_back(std::move(box));
  }
  for (PendingAbsolute& item : deferred) sink.push_back(std::move(item));
}

float LayoutGridChildren(const Node& node, const ComputedStyle& grid_style, const LayoutCtx& ctx,
                         float content_x, float content_y, float content_width,
                         std::vector<LayoutBox>* out_children) {
  const int cols = grid_style.grid_tracks.empty()
                       ? std::max(1, grid_style.grid_columns)
                       : static_cast<int>(grid_style.grid_tracks.size());
  std::vector<float> col_w(static_cast<size_t>(cols), 0.0f);
  const float gap = grid_style.gap;
  if (grid_style.grid_tracks.empty()) {
    const float even =
        std::max(0.0f, (content_width - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    for (float& w : col_w) w = even;
  } else {
    float fr_sum = 0.0f;
    float fixed = 0.0f;
    int auto_count = 0;
    float min_reserve = 0.0f;
    for (const GridTrack& track : grid_style.grid_tracks) {
      if (track.sizing == GridTrackSizing::kPx) fixed += track.value;
      else if (track.sizing == GridTrackSizing::kFr) {
        fr_sum += track.value;
        if (track.has_minmax) min_reserve += track.min_px;
      } else ++auto_count;
    }
    const float free =
        content_width - fixed - min_reserve - gap * static_cast<float>(std::max(0, cols - 1));
    for (size_t i = 0; i < grid_style.grid_tracks.size() && i < col_w.size(); ++i) {
      const GridTrack& track = grid_style.grid_tracks[i];
      if (track.sizing == GridTrackSizing::kPx) {
        col_w[i] = track.value;
      } else if (track.sizing == GridTrackSizing::kFr) {
        col_w[i] = track.min_px;
        if (fr_sum > 0.0f) col_w[i] += std::max(0.0f, free) * (track.value / fr_sum);
      } else {
        col_w[i] = auto_count > 0 ? std::max(0.0f, free) / static_cast<float>(auto_count) : 0.0f;
      }
      if (track.has_minmax) {
        if (track.min_px > 0.0f) col_w[i] = std::max(col_w[i], track.min_px);
        if (track.max_px >= 0.0f) col_w[i] = std::min(col_w[i], track.max_px);
      }
    }
  }

  std::vector<const Node*> items;
  for (const auto& child : node.children) {
    if (child->type != NodeType::kElement) continue;
    ComputedStyle cs = ComputeStyle(*child, *ctx.sheet, &grid_style);
    if (cs.display == DisplayType::kNone) continue;
    if (IsOutOfFlow(cs)) {
      if (ctx.abs_sink) ctx.abs_sink->push_back({child.get(), cs, content_x, content_y});
      continue;
    }
    items.push_back(child.get());
  }
  if (items.empty()) return 0;
  float cursor_y = content_y;
  float row_h = 0;
  int col = 0;
  for (const Node* item : items) {
    ComputedStyle cs = ComputeStyle(*item, *ctx.sheet, &grid_style);
    float x = content_x;
    for (int i = 0; i < col; ++i) x += col_w[static_cast<size_t>(i)] + gap;
    const float w = col_w[static_cast<size_t>(col)];
    LayoutBox box = LayoutBlockChild(*item, cs, x, w, cursor_y, ctx);
    row_h = std::max(row_h, box.height);
    out_children->push_back(std::move(box));
    ++col;
    if (col == cols) {
      cursor_y += row_h + gap;
      row_h = 0;
      col = 0;
    }
  }
  if (col != 0) cursor_y += row_h;
  else if (!items.empty()) cursor_y -= gap;
  return std::max(0.0f, cursor_y - content_y);
}

void CollectTableRows(const Node& node, const ComputedStyle& parent_style, const LayoutCtx& ctx,
                      std::vector<const Node*>* rows) {
  for (const auto& child : node.children) {
    if (child->type != NodeType::kElement) continue;
    if (child->tag_name == "script" || child->tag_name == "style" || child->tag_name == "caption")
      continue;
    ComputedStyle cs = ComputeStyle(*child, *ctx.sheet, &parent_style);
    if (cs.display == DisplayType::kNone) continue;
    if (cs.display == DisplayType::kTableRow) {
      rows->push_back(child.get());
    } else if (cs.display == DisplayType::kTableRowGroup) {
      CollectTableRows(*child, cs, ctx, rows);
    }
  }
}

void CollectTableCells(const Node& row, const ComputedStyle& row_style, const LayoutCtx& ctx,
                       std::vector<const Node*>* cells) {
  for (const auto& child : row.children) {
    if (child->type != NodeType::kElement) continue;
    ComputedStyle cs = ComputeStyle(*child, *ctx.sheet, &row_style);
    if (cs.display == DisplayType::kNone) continue;
    if (cs.display == DisplayType::kTableCell) cells->push_back(child.get());
  }
}

int ParseTableSpan(const Node& node, const char* attr) {
  const std::string v = node.GetAttribute(attr);
  if (v.empty()) return 1;
  const int n = std::atoi(v.c_str());
  return n < 1 ? 1 : n;
}

int TableRowColumnSlots(const std::vector<const Node*>& cells) {
  int slots = 0;
  for (const Node* cell : cells) slots += ParseTableSpan(*cell, "colspan");
  return slots;
}

float SumColumnWidths(const std::vector<float>& col_w, int start, int span) {
  float total = 0.0f;
  for (int i = 0; i < span; ++i) {
    if (start + i >= static_cast<int>(col_w.size())) break;
    total += col_w[static_cast<size_t>(start + i)];
  }
  return total;
}

float CellIntrinsicWidth(const Node& cell, const ComputedStyle& style, const LayoutCtx& ctx,
                         float cap) {
  float em = style.font_size > 0 ? style.font_size : 16.0f;
  float pad = style.padding_left.Resolve(cap, 0, em) + style.padding_right.Resolve(cap, 0, em) +
              style.border_left + style.border_right;
  std::string text = cell.TextContent();
  float tw = ctx.font() ? wasmskia::MeasureText(*ctx.font(), text, em)
                      : static_cast<float>(text.size()) * 7.0f;
  return pad + tw + 4.0f;
}

float ColumnOffset(const std::vector<float>& col_w, float gap, int col) {
  float x = 0.0f;
  for (int i = 0; i < col; ++i) x += col_w[static_cast<size_t>(i)] + gap;
  return x;
}

struct TableCellMeta {
  int row = 0;
  int colspan = 1;
  int rowspan = 1;
};

// CSS 2.2 auto table: thead/tbody/tfoot flatten to rows (they must not
// become inline runs -- that was the dashboard "Name Kind Export Status
// guikit.wasm language..." blob). Column widths start from max-content
// then grow equally to fill the table's content box (width:100% case).
float LayoutTableChildren(const Node& node, const ComputedStyle& table_style, const LayoutCtx& ctx,
                          float content_x, float content_y, float content_width,
                          std::vector<LayoutBox>* out_children) {
  std::vector<const Node*> rows;
  if (table_style.display == DisplayType::kTableRow) {
    rows.push_back(&node);
  } else {
    CollectTableRows(node, table_style, ctx, &rows);
  }
  if (rows.empty()) return 0.0f;

  std::vector<std::vector<const Node*>> row_cells;
  row_cells.reserve(rows.size());
  int cols = 0;
  for (const Node* row : rows) {
    ComputedStyle rs = ComputeStyle(*row, *ctx.sheet, &table_style);
    std::vector<const Node*> cells;
    CollectTableCells(*row, rs, ctx, &cells);
    cols = std::max(cols, TableRowColumnSlots(cells));
    row_cells.push_back(std::move(cells));
  }
  if (cols <= 0) return 0.0f;

  std::vector<float> col_w(static_cast<size_t>(cols), 0.0f);
  for (size_t r = 0; r < rows.size(); ++r) {
    ComputedStyle rs = ComputeStyle(*rows[r], *ctx.sheet, &table_style);
    int col = 0;
    for (const Node* cell : row_cells[r]) {
      const int span = std::min(ParseTableSpan(*cell, "colspan"), cols - col);
      ComputedStyle cs = ComputeStyle(*cell, *ctx.sheet, &rs);
      const float iw = CellIntrinsicWidth(*cell, cs, ctx, content_width);
      const float per_col = span > 0 ? iw / static_cast<float>(span) : iw;
      for (int k = 0; k < span; ++k) {
        col_w[static_cast<size_t>(col + k)] =
            std::max(col_w[static_cast<size_t>(col + k)], per_col);
      }
      col += span;
    }
  }
  float total = 0.0f;
  for (float w : col_w) total += w;
  if (total > 0.0f && content_width > 0.0f && total != content_width) {
    const float scale = content_width / total;
    for (float& w : col_w) w *= scale;
  } else if (total <= 0.0f && cols > 0) {
    const float even = content_width / static_cast<float>(cols);
    for (float& w : col_w) w = even;
  }

  const size_t num_rows = rows.size();
  std::vector<float> row_heights(num_rows, 0.0f);
  std::vector<std::vector<TableCellMeta>> row_meta(num_rows);
  std::vector<int> rowspan_remaining(static_cast<size_t>(cols), 0);

  float cursor_y = content_y;
  for (size_t r = 0; r < num_rows; ++r) {
    for (int& rem : rowspan_remaining) {
      if (rem > 0) --rem;
    }

    ComputedStyle rs = ComputeStyle(*rows[r], *ctx.sheet, &table_style);
    LayoutBox row_box;
    row_box.source = rows[r];
    row_box.style = rs;
    row_box.x = content_x;
    row_box.y = cursor_y;
    row_box.width = content_width;

    int col = 0;
    for (const Node* cell_node : row_cells[r]) {
      while (col < cols && rowspan_remaining[static_cast<size_t>(col)] > 0) ++col;
      if (col >= cols) break;

      const int cspan = std::min(ParseTableSpan(*cell_node, "colspan"), cols - col);
      const int rspan = ParseTableSpan(*cell_node, "rowspan");
      ComputedStyle cs = ComputeStyle(*cell_node, *ctx.sheet, &rs);
      const float w = SumColumnWidths(col_w, col, cspan);
      const float x = content_x + ColumnOffset(col_w, 0.0f, col);
      LayoutBox cell = LayoutBlockChild(*cell_node, cs, x, w, cursor_y, ctx);

      if (rspan > 1) {
        const float per_row = cell.height / static_cast<float>(rspan);
        for (int k = 0; k < rspan && r + static_cast<size_t>(k) < num_rows; ++k) {
          row_heights[r + static_cast<size_t>(k)] =
              std::max(row_heights[r + static_cast<size_t>(k)], per_row);
        }
        for (int k = 0; k < cspan; ++k) {
          rowspan_remaining[static_cast<size_t>(col + k)] = rspan - 1;
        }
      } else {
        row_heights[r] = std::max(row_heights[r], cell.height);
      }

      TableCellMeta meta;
      meta.row = static_cast<int>(r);
      meta.colspan = cspan;
      meta.rowspan = rspan;
      row_meta[r].push_back(meta);
      row_box.children.push_back(std::move(cell));
      col += cspan;
    }

    if (row_heights[r] <= 0.0f) row_heights[r] = 0.0f;
    for (size_t i = 0; i < row_box.children.size(); ++i) {
      LayoutBox& cell = row_box.children[i];
      const TableCellMeta& meta = row_meta[r][i];
      if (meta.rowspan > 1) {
        float span_h = 0.0f;
        for (int k = 0; k < meta.rowspan && meta.row + k < static_cast<int>(num_rows); ++k) {
          span_h += row_heights[static_cast<size_t>(meta.row + k)];
        }
        cell.height = span_h;
      } else if (cell.height < row_heights[r]) {
        cell.height = row_heights[r];
      }
    }
    row_box.height = row_heights[r];
    cursor_y += row_heights[r];
    out_children->push_back(std::move(row_box));
  }
  return std::max(0.0f, cursor_y - content_y);
}

LayoutBox LayoutBlockChild(const Node& node, const ComputedStyle& style, float x,
                          float avail_width, float y, const LayoutCtx& ctx,
                          const FloatState* inherited_floats) {
  CounterGuard counter_guard(ctx.counters, style);
  LayoutBox box;
  box.source = &node;
  box.style = style;
  // Everything appended to the sink from here on was found in this box's
  // subtree, so this is the boundary ResolveAbsolutes claims below.
  const size_t abs_mark = ctx.abs_sink ? ctx.abs_sink->size() : 0;

  float em = style.font_size;
  float margin_left = style.margin_left.Resolve(avail_width, 0, em);
  float margin_right = style.margin_right.Resolve(avail_width, 0, em);
  float margin_top = style.margin_top.Resolve(avail_width, 0, em);

  float width = style.width.is_auto() ? std::max(0.0f, avail_width - margin_left - margin_right)
                                      : style.width.Resolve(avail_width, avail_width - margin_left - margin_right, em);
  // A table with width:auto is shrink-to-fit, not full-width (CSS 2.1
  // 17.5.2.2). Stretching it made every two-column table span the viewport
  // and its columns share the slack evenly.
  if (style.width.is_auto() && style.display == DisplayType::kTable) {
    width = ShrinkToFitWidth(node, style, ctx, std::max(0.0f, avail_width - margin_left - margin_right));
  }
  if (!style.min_width.is_auto()) width = std::max(width, style.min_width.Resolve(avail_width, 0, em));
  if (!style.max_width.is_auto()) width = std::min(width, style.max_width.Resolve(avail_width, width, em));
  if (!style.width.is_auto() && style.margin_left.is_auto() && style.margin_right.is_auto()) {
    float extra = std::max(0.0f, avail_width - width);
    margin_left = margin_right = extra / 2.0f;
  }

  box.x = x + margin_left;
  box.y = y + margin_top;
  box.width = width;

  float padding_l = style.padding_left.Resolve(width, 0, em);
  float padding_r = style.padding_right.Resolve(width, 0, em);
  float padding_t = style.padding_top.Resolve(width, 0, em);
  float padding_b = style.padding_bottom.Resolve(width, 0, em);
  if (style.display == DisplayType::kListItem && style.list_style_type != ListStyleType::kNone) {
    padding_l += std::max(14.0f, em * 0.9f);
  }

  float content_x = box.x + style.border_left + padding_l;
  float content_width =
      std::max(0.0f, width - style.border_left - style.border_right - padding_l - padding_r);
  float content_y = box.y + style.border_top + padding_t;

  float content_height = 0;
  if (node.tag_name == "img") {
    int iw = 0, ih = 0;
    if (ctx.document) {
      auto it = ctx.document->images.find(&node);
      if (it != ctx.document->images.end()) {
        iw = it->second.width;
        ih = it->second.height;
      }
    }
    if (iw == 0) iw = std::atoi(node.GetAttribute("width").c_str());
    if (ih == 0) ih = std::atoi(node.GetAttribute("height").c_str());
    if (!style.width.is_auto()) {
      width = style.width.Resolve(avail_width, static_cast<float>(iw), em);
      box.width = width;
      content_width = std::max(0.0f, width - style.border_left - style.border_right - padding_l - padding_r);
    } else if (iw > 0) {
      box.width = static_cast<float>(iw) + style.border_left + style.border_right + padding_l + padding_r;
      content_width = static_cast<float>(iw);
    }
    if (!style.height.is_auto()) {
      content_height = style.height.Resolve(0, static_cast<float>(ih), em);
    } else if (ih > 0 && iw > 0 && content_width > 0) {
      content_height = content_width * (static_cast<float>(ih) / static_cast<float>(iw));
    } else {
      content_height = static_cast<float>(ih);
    }
  } else if (node.tag_name == "input" || node.tag_name == "textarea") {
    std::string value = node.GetAttribute("value");
    if (value.empty()) value = node.GetAttribute("placeholder");
    float size = style.font_size > 0 ? style.font_size : 13.0f;
    const FontMetrics metrics = FontMetrics::ForFace(ctx.fonts ? ctx.fonts->Resolve(style) : nullptr, size);
    InlineFragment frag;
    frag.text = value;
    frag.source = &node;
    frag.style = style;
    frag.x = content_x;
    frag.y = content_y + metrics.ascent;
    frag.ascent = metrics.ascent;
    frag.descent = metrics.descent;
    if (ctx.fonts) {
      const wasmskia::Font* face = ctx.fonts->Resolve(style);
      if (face) frag.width = wasmskia::MeasureText(*face, value, size);
    }
    box.fragments.push_back(std::move(frag));
    content_height = size * 1.4f;
    if (style.width.is_auto() && node.tag_name == "input" && content_width < 40.0f) {
      float tw = ctx.font() ? wasmskia::MeasureText(*ctx.font(),
                                                    value.empty() ? "xxxxxxxxxx" : value, size)
                            : 160.0f;
      box.width = tw + style.border_left + style.border_right + padding_l + padding_r + 8.0f;
    }
  } else if (style.display == DisplayType::kFlex) {
    content_height =
        LayoutFlexChildren(node, style, ctx, content_x, content_y, content_width, &box.children);
  } else if (style.display == DisplayType::kGrid && style.grid_columns > 0) {
    content_height =
        LayoutGridChildren(node, style, ctx, content_x, content_y, content_width, &box.children);
  } else if (style.display == DisplayType::kTable ||
             style.display == DisplayType::kTableRowGroup ||
             style.display == DisplayType::kTableRow) {
    content_height =
        LayoutTableChildren(node, style, ctx, content_x, content_y, content_width, &box.children);
  } else {
    content_height = LayoutBlockFormattingContext(node, style, ctx, content_x, content_y,
                                                  content_width, &box.children, &box.fragments,
                                                  inherited_floats);
  }

  float used_height;
  if (style.height.is_auto()) {
    used_height = content_height;
  } else {
    float pct_base = ctx.viewport_h > 0 ? ctx.viewport_h : content_height;
    used_height = style.height.Resolve(pct_base, content_height, em);
  }
  if (!style.min_height.is_auto())
    used_height = std::max(used_height, style.min_height.Resolve(used_height, 0, em));
  if (!style.max_height.is_auto())
    used_height = std::min(used_height, style.max_height.Resolve(used_height, used_height, em));

  box.height = style.border_top + padding_t + used_height + padding_b + style.border_bottom;

  if (style.position == PositionType::kRelative) {
    float dx = 0, dy = 0;
    if (!style.top.is_auto()) dy = style.top.Resolve(0, 0, em);
    else if (!style.bottom.is_auto()) dy = -style.bottom.Resolve(0, 0, em);
    if (!style.left.is_auto()) dx = style.left.Resolve(box.width, 0, em);
    else if (!style.right.is_auto()) dx = -style.right.Resolve(box.width, 0, em);
    if (dx != 0 || dy != 0) ShiftLayoutBox(&box, dx, dy);
  }

  // Out-of-flow descendants found anywhere in this subtree resolve here if
  // this box is their containing block; otherwise they stay pending for an
  // ancestor that is.
  if (IsAbsoluteContainingBlock(style)) ResolveAbsolutes(abs_mark, style, &box, ctx, false);
  return box;
}

void CollectStyleText(const Node& node, std::string* out) {
  if (node.type == NodeType::kElement && node.tag_name == "style") {
    *out += node.TextContent();
    *out += "\n";
    return;
  }
  for (const auto& child : node.children) CollectStyleText(*child, out);
}

float MaxContentWidth(const LayoutBox& box) {
  float w = box.x + box.width;
  for (const InlineFragment& frag : box.fragments) {
    w = std::max(w, frag.x + frag.width);
  }
  for (const LayoutBox& child : box.children) {
    w = std::max(w, MaxContentWidth(child));
  }
  return w;
}

}  // namespace

LayoutResult ComputeLayout(const HtmlDocument& document, float viewport_width,
                          const wasmskia::Font& font, float viewport_height) {
  FontSet fonts;
  fonts.regular = &font;
  return ComputeLayout(document, viewport_width, fonts, viewport_height);
}

LayoutResult ComputeLayout(const HtmlDocument& document, float viewport_width,
                          const FontSet& fonts, float viewport_height) {
  LayoutResult result;
  if (!document.root) {
    result.error = "empty document";
    return result;
  }
  const Node* body = document.root->FindFirstElement("body");
  const Node* start = body ? body : document.root->FindFirstElement("html");
  if (!start) start = document.root.get();

  std::string style_text;
  CollectStyleText(*document.root, &style_text);
  Stylesheet sheet = ParseStylesheet(style_text);
  sheet.viewport_w = viewport_width;
  sheet.viewport_h = viewport_height > 0.0f ? viewport_height : viewport_width * 0.75f;

  ComputedStyle root_style = ComputeStyle(*start, sheet, nullptr);
  std::vector<PendingAbsolute> abs_sink;
  std::unordered_map<const Node*, IntrinsicWidths> intrinsic_cache;
  CounterState counters;
  LayoutCtx ctx;
  ctx.sheet = &sheet;
  ctx.fonts = &fonts;
  ctx.document = &document;
  ctx.counters = &counters;
  ctx.viewport_w = viewport_width;
  ctx.viewport_h = viewport_height > 0.0f ? viewport_height : viewport_width * 0.75f;
  ctx.scroll_x = document.scroll_x;
  ctx.scroll_y = document.scroll_y;
  ctx.abs_sink = &abs_sink;
  ctx.intrinsic_cache = &intrinsic_cache;
  result.root = LayoutBlockChild(*start, root_style, 0.0f, viewport_width, 0.0f, ctx);
  // The initial containing block. Anything still pending had no positioned
  // ancestor (or is fixed, whose containing block is the viewport either
  // way), so it resolves against the root box.
  ResolveAbsolutes(0, root_style, &result.root, ctx, true);
  const float scroll_y = document.scroll_y;
  const float viewport_bottom = scroll_y + ctx.viewport_h;
  ComputeScrollExtents(&result.root);
  ResolveStickyPositions(&result.root, &document, 0.0f, scroll_y, scroll_y, viewport_bottom);
  result.total_height = result.root.height;
  result.total_width = MaxContentWidth(result.root);
  result.ok = true;
  return result;
}

void ComputeScrollExtents(LayoutBox* box) {
  if (!box) return;
  for (LayoutBox& child : box->children) ComputeScrollExtents(&child);
  box->scroll_extent_x = 0.0f;
  box->scroll_extent_y = 0.0f;
  if (!ScrollsOverflow(box->style)) return;

  const PaddingBox pad =
      ComputePaddingBox(box->style, box->x, box->y, box->width, box->height);
  float max_right = pad.x;
  float max_bottom = pad.y;
  for (const LayoutBox& child : box->children) {
    max_right = std::max(max_right, child.x + child.width);
    max_bottom = std::max(max_bottom, child.y + child.height);
  }
  for (const InlineFragment& frag : box->fragments) {
    max_right = std::max(max_right, frag.x + frag.width);
    max_bottom = std::max(max_bottom, frag.y + frag.descent);
  }
  if (ScrollsOverflowX(box->style)) {
    box->scroll_extent_x = std::max(0.0f, max_right - (pad.x + pad.width));
  }
  if (ScrollsOverflowY(box->style)) {
    box->scroll_extent_y = std::max(0.0f, max_bottom - (pad.y + pad.height));
  }
}

float EffectiveScrollX(const HtmlDocument& document, const LayoutBox& box) {
  if (!box.source) return 0.0f;
  auto it = document.element_scroll.find(box.source);
  if (it == document.element_scroll.end()) return 0.0f;
  return std::min(it->second.x, box.scroll_extent_x);
}

float EffectiveScrollY(const HtmlDocument& document, const LayoutBox& box) {
  if (!box.source) return 0.0f;
  auto it = document.element_scroll.find(box.source);
  if (it == document.element_scroll.end()) return 0.0f;
  return std::min(it->second.y, box.scroll_extent_y);
}

const LayoutBox* HitTestLayout(const LayoutBox& root, float x, float y,
                               const HtmlDocument* document) {
  float child_x = x;
  float child_y = y;
  if (document && ScrollsOverflow(root.style) && root.source) {
    const PaddingBox pad =
        ComputePaddingBox(root.style, root.x, root.y, root.width, root.height);
    if (x >= pad.x && x < pad.x + pad.width && y >= pad.y && y < pad.y + pad.height) {
      if (ScrollsOverflowX(root.style)) child_x = x + EffectiveScrollX(*document, root);
      if (ScrollsOverflowY(root.style)) child_y = y + EffectiveScrollY(*document, root);
    }
  }
  for (const LayoutBox& child : root.children) {
    if (child_x >= child.x && child_x < child.x + child.width && child_y >= child.y &&
        child_y < child.y + child.height) {
      const LayoutBox* deeper = HitTestLayout(child, child_x, child_y, document);
      return deeper ? deeper : &child;
    }
  }
  if (x >= root.x && x < root.x + root.width && y >= root.y && y < root.y + root.height) {
    return &root;
  }
  return nullptr;
}

const Node* HitTestInline(const LayoutBox& root, float x, float y, const HtmlDocument* document) {
  float child_x = x;
  float child_y = y;
  if (document && ScrollsOverflow(root.style) && root.source) {
    const PaddingBox pad =
        ComputePaddingBox(root.style, root.x, root.y, root.width, root.height);
    if (x >= pad.x && x < pad.x + pad.width && y >= pad.y && y < pad.y + pad.height) {
      if (ScrollsOverflowX(root.style)) child_x = x + EffectiveScrollX(*document, root);
      if (ScrollsOverflowY(root.style)) child_y = y + EffectiveScrollY(*document, root);
    }
  }
  // Fragments are checked before the box itself: a link's text sits inside
  // its paragraph's rect, and the anchor is the more specific answer.
  // Later fragments win, matching paint order within a line.
  const Node* found = nullptr;
  for (const LayoutBox& child : root.children) {
    if (const Node* hit = HitTestInline(child, child_x, child_y, document)) found = hit;
  }
  if (found) return found;
  for (const InlineFragment& frag : root.fragments) {
    if (frag.source && frag.Contains(child_x, child_y)) found = frag.source;
  }
  if (found) return found;
  if (x >= root.x && x < root.x + root.width && y >= root.y && y < root.y + root.height) {
    return root.source;
  }
  return nullptr;
}

namespace {

const LayoutBox* FindDeepestScrollContainer(const LayoutBox& box, float x, float y,
                                            const HtmlDocument& document,
                                            const LayoutBox* best) {
  float child_x = x;
  float child_y = y;
  const LayoutBox* next = best;
  if (ScrollsOverflow(box.style) && box.source) {
    const PaddingBox pad =
        ComputePaddingBox(box.style, box.x, box.y, box.width, box.height);
    if (x >= pad.x && x < pad.x + pad.width && y >= pad.y && y < pad.y + pad.height) {
      if (ScrollsOverflowX(box.style)) child_x = x + EffectiveScrollX(document, box);
      if (ScrollsOverflowY(box.style)) child_y = y + EffectiveScrollY(document, box);
      if ((ScrollsOverflowX(box.style) && box.scroll_extent_x > 0.0f) ||
          (ScrollsOverflowY(box.style) && box.scroll_extent_y > 0.0f)) {
        next = &box;
      }
    }
  }
  for (const LayoutBox& child : box.children) {
    next = FindDeepestScrollContainer(child, child_x, child_y, document, next);
  }
  return next;
}

}  // namespace

WheelScrollResult ApplyWheelScroll(HtmlDocument* document, float viewport_width,
                                   float viewport_height, const FontSet& fonts, float x,
                                   float y, float delta_x, float delta_y) {
  WheelScrollResult result;
  if (!document) return result;
  document->viewport_height = viewport_height;
  LayoutResult layout = ComputeLayout(*document, viewport_width, fonts, viewport_height);
  if (!layout.ok) return result;

  const float doc_x = x + document->scroll_x;
  const float doc_y = y + document->scroll_y;
  const LayoutBox* target =
      FindDeepestScrollContainer(layout.root, doc_x, doc_y, *document, nullptr);
  if (target && target->source) {
    HtmlDocument::ElementScroll& scroll = document->element_scroll[target->source];
    if (ScrollsOverflowX(target->style)) {
      scroll.x = std::clamp(scroll.x + delta_x, 0.0f, target->scroll_extent_x);
    }
    if (ScrollsOverflowY(target->style)) {
      scroll.y = std::clamp(scroll.y + delta_y, 0.0f, target->scroll_extent_y);
    }
    result.consumed = (delta_x != 0.0f || delta_y != 0.0f);
    result.scroll_target = target->source;
    return result;
  }

  bool scrolled = false;
  if (viewport_width > 0.0f && delta_x != 0.0f) {
    const float max_scroll_x = std::max(0.0f, layout.total_width - viewport_width);
    document->scroll_x = std::clamp(document->scroll_x + delta_x, 0.0f, max_scroll_x);
    scrolled = true;
  }
  if (viewport_height > 0.0f && delta_y != 0.0f) {
    const float max_scroll = std::max(0.0f, layout.total_height - viewport_height);
    document->scroll_y = std::clamp(document->scroll_y + delta_y, 0.0f, max_scroll);
    scrolled = true;
  }
  if (scrolled) {
    result.consumed = true;
    result.scrolled_viewport = true;
  }
  return result;
}

}  // namespace blink

#endif  // BLINK_HAS_PAINT_PIPELINE
