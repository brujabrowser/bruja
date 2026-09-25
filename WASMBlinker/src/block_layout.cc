#include "blink/block_layout.h"

#include <algorithm>

namespace blink {

float CollapseMargins(float upper, float lower) {
  // CSS 2.1 8.3.1: two positive margins collapse to the maximum; two
  // negative to the minimum; mixed signs sum.
  if (upper >= 0.0f && lower >= 0.0f) return std::max(upper, lower);
  if (upper <= 0.0f && lower <= 0.0f) return std::min(upper, lower);
  return upper + lower;
}

bool EstablishesBlockFormattingContext(const ComputedStyle& style) {
  if (style.float_type != FloatType::kNone) return true;
  if (style.position == PositionType::kAbsolute || style.position == PositionType::kFixed) return true;
  if (style.display == DisplayType::kInlineBlock) return true;
  if (style.display == DisplayType::kTableCell) return true;
  if (style.overflow_x != Overflow::kVisible || style.overflow_y != Overflow::kVisible) return true;
  return false;
}

bool ClipsOverflowX(const ComputedStyle& style) {
  return style.overflow_x == Overflow::kHidden || style.overflow_x == Overflow::kScroll ||
         style.overflow_x == Overflow::kAuto;
}

bool ClipsOverflowY(const ComputedStyle& style) {
  return style.overflow_y == Overflow::kHidden || style.overflow_y == Overflow::kScroll ||
         style.overflow_y == Overflow::kAuto;
}

bool ScrollsOverflowX(const ComputedStyle& style) {
  return style.overflow_x == Overflow::kScroll || style.overflow_x == Overflow::kAuto;
}

bool ScrollsOverflowY(const ComputedStyle& style) {
  return style.overflow_y == Overflow::kScroll || style.overflow_y == Overflow::kAuto;
}

bool ClipsOverflow(const ComputedStyle& style) {
  return ClipsOverflowX(style) || ClipsOverflowY(style);
}

bool ScrollsOverflow(const ComputedStyle& style) {
  return ScrollsOverflowX(style) || ScrollsOverflowY(style);
}

namespace {

bool HasTopSeparator(const ComputedStyle& style) {
  const float em = style.font_size > 0 ? style.font_size : 16.0f;
  return style.border_top > 0.0f || style.padding_top.Resolve(0, 0, em) > 0.0f;
}

bool HasBottomSeparator(const ComputedStyle& style) {
  const float em = style.font_size > 0 ? style.font_size : 16.0f;
  return style.border_bottom > 0.0f || style.padding_bottom.Resolve(0, 0, em) > 0.0f;
}

}  // namespace

bool CanCollapseParentTopWithFirstChild(const ComputedStyle& parent) {
  if (HasTopSeparator(parent)) return false;
  if (ClipsOverflow(parent)) return false;
  return true;
}

bool CanCollapseParentBottomWithLastChild(const ComputedStyle& parent) {
  if (HasBottomSeparator(parent)) return false;
  if (ClipsOverflow(parent)) return false;
  return true;
}

PaddingBox ComputePaddingBox(const ComputedStyle& style, float x, float y, float width,
                             float height) {
  const float em = style.font_size > 0 ? style.font_size : 16.0f;
  const float pad_l = style.padding_left.Resolve(width, 0, em);
  const float pad_r = style.padding_right.Resolve(width, 0, em);
  const float pad_t = style.padding_top.Resolve(width, 0, em);
  const float pad_b = style.padding_bottom.Resolve(width, 0, em);
  PaddingBox out;
  out.x = x + style.border_left + pad_l;
  out.y = y + style.border_top + pad_t;
  out.width = std::max(0.0f, width - style.border_left - style.border_right - pad_l - pad_r);
  out.height = std::max(0.0f, height - style.border_top - style.border_bottom - pad_t - pad_b);
  return out;
}

}  // namespace blink
