#include "blink/display_list.h"

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "blink/block_layout.h"
#include "blink/layout.h"

#include <algorithm>
#include <vector>

namespace blink {

namespace {

bool IsPositioned(const ComputedStyle& s) { return s.position != PositionType::kStatic; }
bool IsFloating(const ComputedStyle& s) { return s.float_type != FloatType::kNone; }

uint32_t WithOpacity(uint32_t argb, float opacity) {
  if (opacity >= 1.0f) return argb;
  if (opacity <= 0.0f) return argb & 0x00FFFFFFu;
  uint32_t a = static_cast<uint32_t>((opacity * (argb >> 24)) + 0.5f);
  return (a << 24) | (argb & 0x00FFFFFFu);
}

int LayerZ(const ComputedStyle& s) { return s.z_index_auto ? 0 : s.z_index; }

struct Recorder {
  const HtmlDocument* document = nullptr;
  DisplayList* out = nullptr;
};

void CopyRadii(const ComputedStyle& s, DisplayItem* item) {
  item->radius_tl = s.radius_tl;
  item->radius_tr = s.radius_tr;
  item->radius_br = s.radius_br;
  item->radius_bl = s.radius_bl;
}

// Background, replaced content, border and list marker for one box. The
// order within a box is fixed by CSS Backgrounds and Borders: background
// first, then content, then border on top.
void AppendBoxDecorations(const LayoutBox& box, Recorder& r) {
  const ComputedStyle& s = box.style;
  const bool rounded = s.has_radius();

  if (s.has_box_shadow && !s.box_shadow.inset) {
    DisplayItem sh;
    sh.kind = DisplayItemKind::kBoxShadow;
    sh.source = box.source;
    sh.x = box.x;
    sh.y = box.y;
    sh.width = box.width;
    sh.height = box.height;
    CopyRadii(s, &sh);
    sh.shadow = s.box_shadow;
    sh.shadow_inset = false;
    sh.color = WithOpacity(s.box_shadow.color, s.opacity);
    r.out->Append(std::move(sh));
  }

  if (s.bg_gradient != BgGradientKind::kNone && !s.bg_stops.empty()) {
    DisplayItem item;
    item.kind = DisplayItemKind::kGradient;
    item.source = box.source;
    item.x = box.x;
    item.y = box.y;
    item.width = box.width;
    item.height = box.height;
    CopyRadii(s, &item);
    item.gradient = s.bg_gradient;
    item.grad_angle = s.bg_grad_angle;
    item.grad_cx = s.bg_grad_cx;
    item.grad_cy = s.bg_grad_cy;
    item.grad_radius = s.bg_grad_radius;
    item.stops = s.bg_stops;
    for (BgGradientStop& stop : item.stops) stop.color = WithOpacity(stop.color, s.opacity);
    r.out->Append(std::move(item));
  } else if (s.has_background) {
    uint32_t bg = WithOpacity(s.background_color, s.opacity);
    if ((bg >> 24) != 0) {
      DisplayItem item;
      item.kind = rounded ? DisplayItemKind::kFillRoundRect : DisplayItemKind::kFillRect;
      item.source = box.source;
      item.x = box.x;
      item.y = box.y;
      item.width = box.width;
      item.height = box.height;
      CopyRadii(s, &item);
      item.color = bg;
      r.out->Append(std::move(item));
    }
  }

  if (s.has_box_shadow && s.box_shadow.inset) {
    const PaddingBox pad = ComputePaddingBox(s, box.x, box.y, box.width, box.height);
    if (pad.width > 0.0f && pad.height > 0.0f) {
      DisplayItem sh;
      sh.kind = DisplayItemKind::kBoxShadow;
      sh.source = box.source;
      sh.x = pad.x;
      sh.y = pad.y;
      sh.width = pad.width;
      sh.height = pad.height;
      CopyRadii(s, &sh);
      sh.shadow = s.box_shadow;
      sh.shadow_inset = true;
      sh.color = WithOpacity(s.box_shadow.color, s.opacity);
      r.out->Append(std::move(sh));
    }
  }

  if (box.source && box.source->tag_name == "img" && r.document) {
    auto it = r.document->images.find(box.source);
    if (it != r.document->images.end() && it->second.width > 0 && it->second.height > 0) {
      const float em = s.font_size;
      const float pad_l = s.padding_left.Resolve(box.width, 0, em);
      const float pad_t = s.padding_top.Resolve(box.width, 0, em);
      const float dw = std::max(1.0f, box.width - s.border_left - s.border_right - pad_l -
                                          s.padding_right.Resolve(box.width, 0, em));
      const float dh = std::max(1.0f, box.height - s.border_top - s.border_bottom - pad_t -
                                          s.padding_bottom.Resolve(box.width, 0, em));
      if (rounded) {
        DisplayItem clip;
        clip.kind = DisplayItemKind::kPushClipRoundRect;
        clip.source = box.source;
        clip.x = box.x;
        clip.y = box.y;
        clip.width = box.width;
        clip.height = box.height;
        CopyRadii(s, &clip);
        r.out->Append(std::move(clip));
      }
      DisplayItem item;
      item.kind = DisplayItemKind::kImage;
      item.source = box.source;
      item.x = box.x + s.border_left + pad_l;
      item.y = box.y + s.border_top + pad_t;
      item.width = dw;
      item.height = dh;
      item.image = &it->second;
      r.out->Append(std::move(item));
      if (rounded) {
        DisplayItem pop;
        pop.kind = DisplayItemKind::kPopClip;
        pop.source = box.source;
        r.out->Append(std::move(pop));
      }
    }
  }

  if (box.content_image) {
    const DecodedImage& img = *box.content_image;
    const float em = s.font_size;
    const float pad_l = s.padding_left.Resolve(box.width, 0, em);
    const float pad_t = s.padding_top.Resolve(box.width, 0, em);
    const float dw = std::max(1.0f, box.width - s.border_left - s.border_right - pad_l -
                                        s.padding_right.Resolve(box.width, 0, em));
    const float dh = std::max(1.0f, box.height - s.border_top - s.border_bottom - pad_t -
                                        s.padding_bottom.Resolve(box.width, 0, em));
    if (rounded) {
      DisplayItem clip;
      clip.kind = DisplayItemKind::kPushClipRoundRect;
      clip.source = box.source;
      clip.x = box.x;
      clip.y = box.y;
      clip.width = box.width;
      clip.height = box.height;
      CopyRadii(s, &clip);
      r.out->Append(std::move(clip));
    }
    DisplayItem item;
    item.kind = DisplayItemKind::kImage;
    item.source = box.source;
    item.x = box.x + s.border_left + pad_l;
    item.y = box.y + s.border_top + pad_t;
    item.width = dw;
    item.height = dh;
    item.image = &img;
    r.out->Append(std::move(item));
    if (rounded) {
      DisplayItem pop;
      pop.kind = DisplayItemKind::kPopClip;
      pop.source = box.source;
      r.out->Append(std::move(pop));
    }
  }

  const float bw = std::max(std::max(s.border_top, s.border_right),
                            std::max(s.border_bottom, s.border_left));
  const uint32_t border = WithOpacity(s.border_color, s.opacity);
  if (rounded && bw > 0 && box.width > 2.0f && box.height > 2.0f) {
    DisplayItem item;
    item.kind = DisplayItemKind::kStrokeRoundRect;
    item.source = box.source;
    item.x = box.x;
    item.y = box.y;
    item.width = box.width;
    item.height = box.height;
    CopyRadii(s, &item);
    item.stroke_width = bw;
    item.color = border;
    r.out->Append(std::move(item));
  } else if (!rounded) {
    auto edge = [&](float ex, float ey, float ew, float eh) {
      if (ew <= 0 || eh <= 0) return;
      DisplayItem item;
      item.kind = DisplayItemKind::kFillRect;
      item.source = box.source;
      item.x = ex;
      item.y = ey;
      item.width = ew;
      item.height = eh;
      item.color = border;
      r.out->Append(std::move(item));
    };
    edge(box.x, box.y, box.width, s.border_top);
    edge(box.x, box.y + box.height - s.border_bottom, box.width, s.border_bottom);
    edge(box.x, box.y, s.border_left, box.height);
    edge(box.x + box.width - s.border_right, box.y, s.border_right, box.height);
  }
}

void AppendListMarker(const LayoutBox& box, Recorder& r) {
  const ComputedStyle& s = box.style;
  if (s.display != DisplayType::kListItem || s.list_style_type == ListStyleType::kNone) return;
  const float size = s.font_size > 0 ? s.font_size : 14.0f;
  const float cy =
      box.y + s.border_top + s.padding_top.Resolve(box.width, 0, size) + size * 0.45f;
  if (s.list_style_type == ListStyleType::kDecimal && box.source && box.source->parent) {
    int index = 0;
    for (const auto& sib : box.source->parent->children) {
      if (sib->type != NodeType::kElement || sib->tag_name != "li") continue;
      ++index;
      if (sib.get() == box.source) break;
    }
    DisplayItem item;
    item.kind = DisplayItemKind::kText;
    item.source = box.source;
    item.text = std::to_string(index) + ".";
    item.x = box.x + s.border_left + 2.0f;
    item.y = cy + size * 0.35f;
    item.font_size = size * 0.85f;
    item.color = WithOpacity(s.color, s.opacity);
    r.out->Append(std::move(item));
    return;
  }
  DisplayItem item;
  item.kind = DisplayItemKind::kCircle;
  item.source = box.source;
  item.x = box.x + s.border_left + std::max(6.0f, size * 0.4f);  // center x
  item.y = cy;                                                    // center y
  item.width = std::max(2.0f, size * 0.16f);                      // radius
  item.color = WithOpacity(s.color, s.opacity);
  r.out->Append(std::move(item));
}

// Inline content: one text item per fragment, each carrying its own face
// and color. Decoration lines are separate fills so raster does not need
// to know anything about text styling.
void AppendInlineContent(const LayoutBox& box, Recorder& r) {
  if (box.style.visibility != Visibility::kHidden) AppendListMarker(box, r);
  for (const InlineFragment& frag : box.fragments) {
    const ComputedStyle& s = frag.style;
    if (s.visibility == Visibility::kHidden) continue;
    const uint32_t color = WithOpacity(s.color, s.opacity);
    // An inline box with its own background (<mark>) paints it behind just
    // its own run, not across the whole line.
    if (s.has_background && (s.background_color >> 24) != 0 && frag.width > 0 && frag.source &&
        frag.source->tag_name != "body" && frag.source->tag_name != "html") {
      DisplayItem bg;
      bg.kind = DisplayItemKind::kFillRect;
      bg.source = frag.source;
      bg.x = frag.x;
      bg.y = frag.top();
      bg.width = frag.width;
      bg.height = frag.height();
      bg.color = WithOpacity(s.background_color, s.opacity);
      r.out->Append(std::move(bg));
    }
    if (!frag.text.empty()) {
      DisplayItem item;
      item.kind = DisplayItemKind::kText;
      item.source = frag.source;
      item.text = frag.text;
      item.x = frag.x;
      item.y = frag.y;
      item.width = frag.width;
      item.font_size = s.font_size;
      item.family = s.font_family;
      item.bold = s.font_bold;
      item.italic = s.font_italic;
      item.underline = s.text_underline;
      item.line_through = s.text_line_through;
      item.color = color;
      r.out->Append(std::move(item));
    }
    const float thickness = std::max(1.0f, s.font_size * 0.07f);
    auto line = [&](float ly) {
      DisplayItem item;
      item.kind = DisplayItemKind::kFillRect;
      item.source = frag.source;
      item.x = frag.x;
      item.y = ly;
      item.width = frag.width;
      item.height = thickness;
      item.color = color;
      r.out->Append(std::move(item));
    };
    if (s.text_underline) line(frag.y + s.font_size * 0.12f);
    if (s.text_line_through) line(frag.y - s.font_size * 0.28f);
  }

  // Text caret for the focused field. A UI affordance rather than CSS, but
  // it belongs in the foreground phase so nothing paints over it.
  if (r.document && r.document->focused && box.source == r.document->focused &&
      (box.source->tag_name == "input" || box.source->tag_name == "textarea")) {
    const ComputedStyle& s = box.style;
    const float size = s.font_size > 0 ? s.font_size : 13.0f;
    float caret_x = box.x + s.border_left + s.padding_left.Resolve(box.width, 0, size);
    if (!box.fragments.empty()) {
      caret_x = box.fragments.front().x + box.fragments.front().width;
    }
    DisplayItem item;
    item.kind = DisplayItemKind::kFillRect;
    item.source = box.source;
    item.x = caret_x;
    item.y = box.y + s.border_top + 2.0f;
    item.width = 1.0f;
    item.height = std::max(2.0f, box.height - s.border_top - s.border_bottom - 4.0f);
    item.color = 0xFF4A9EFFu;
    r.out->Append(std::move(item));
  }
}

void PopClip(Recorder& r) {
  DisplayItem item;
  item.kind = DisplayItemKind::kPopClip;
  r.out->Append(std::move(item));
}

void PushScroll(const LayoutBox& box, Recorder& r) {
  if (!ScrollsOverflow(box.style)) return;
  float sx = 0.0f;
  float sy = 0.0f;
  if (r.document) {
    sx = EffectiveScrollX(*r.document, box);
    sy = EffectiveScrollY(*r.document, box);
  }
  DisplayItem item;
  item.kind = DisplayItemKind::kPushScroll;
  item.source = box.source;
  item.x = sx;
  item.y = sy;
  r.out->Append(std::move(item));
}

void PopScroll(Recorder& r) {
  DisplayItem item;
  item.kind = DisplayItemKind::kPopScroll;
  r.out->Append(std::move(item));
}

void PushOverflowClip(const LayoutBox& box, Recorder& r) {
  const PaddingBox pad = ComputePaddingBox(box.style, box.x, box.y, box.width, box.height);
  if (pad.width <= 0.0f || pad.height <= 0.0f) return;
  DisplayItem item;
  item.kind = DisplayItemKind::kPushClipRect;
  item.source = box.source;
  item.x = pad.x;
  item.y = pad.y;
  item.width = pad.width;
  item.height = pad.height;
  r.out->Append(std::move(item));
}

void PushTransform(const LayoutBox& box, Recorder& r) {
  const Transform& t = box.style.transform;
  if (!t.has()) return;
  DisplayItem item;
  item.kind = DisplayItemKind::kPushTransform;
  item.source = box.source;
  item.x = box.x + box.width * 0.5f;
  item.y = box.y + box.height * 0.5f;
  item.a = t.a;
  item.b = t.b;
  item.c = t.c;
  item.d = t.d;
  item.e = t.e;
  item.f = t.f;
  r.out->Append(std::move(item));
}

void PopTransform(Recorder& r) {
  DisplayItem item;
  item.kind = DisplayItemKind::kPopTransform;
  r.out->Append(std::move(item));
}

void PaintStackingContext(const LayoutBox& box, Recorder& r, bool is_root);

// One phase over the part of the tree this stacking context paints
// directly. Descendants that own their own z-order (stacking contexts,
// positioned boxes) and floats are skipped here and handled by the caller.
void PaintSubtree(const LayoutBox& box, PaintPhase phase, Recorder& r, bool sc_root) {
  const bool hidden = box.style.visibility == Visibility::kHidden;
  const bool clip = ClipsOverflow(box.style) && !sc_root;
  const bool scrolls = ScrollsOverflow(box.style);
  if (clip && phase == PaintPhase::kBlockBackground) PushOverflowClip(box, r);
  if (scrolls && clip) PushScroll(box, r);
  if (!sc_root) {
    if (IsStackingContext(box.style, false)) return;
    if (IsPositioned(box.style)) return;
    if (IsFloating(box.style)) {
      // A float paints atomically, all of its own phases together.
      if (phase == PaintPhase::kFloat) PaintStackingContext(box, r, false);
      return;
    }
    if (!hidden && phase == PaintPhase::kBlockBackground) AppendBoxDecorations(box, r);
  }
  if (phase == PaintPhase::kForeground) AppendInlineContent(box, r);
  for (const LayoutBox& child : box.children) PaintSubtree(child, phase, r, false);
  if (scrolls && clip) PopScroll(r);
  if (clip && phase == PaintPhase::kForeground) PopClip(r);
}

// Descendants whose paint order this stacking context owns but whose
// content it does not paint inline. Nested stacking contexts terminate the
// walk: their own descendants belong to them.
void CollectLayers(const LayoutBox& box, std::vector<const LayoutBox*>* negative,
                   std::vector<const LayoutBox*>* auto_z,
                   std::vector<const LayoutBox*>* positive) {
  for (const LayoutBox& child : box.children) {
    if (IsStackingContext(child.style, false)) {
      const int z = LayerZ(child.style);
      if (z < 0) negative->push_back(&child);
      else if (z > 0) positive->push_back(&child);
      else auto_z->push_back(&child);
      continue;
    }
    if (IsPositioned(child.style)) {
      auto_z->push_back(&child);
      continue;
    }
    // Floats paint atomically in the float phase; do not lift layers out
    // of them or their contents would paint twice.
    if (IsFloating(child.style)) continue;
    CollectLayers(child, negative, auto_z, positive);
  }
}

// CSS 2.1 Appendix E.
void PaintStackingContext(const LayoutBox& box, Recorder& r, bool is_root) {
  const bool hidden = box.style.visibility == Visibility::kHidden;
  const bool xform = box.style.transform.has();
  if (!hidden) AppendBoxDecorations(box, r);

  const bool clip = ClipsOverflow(box.style);
  const bool scrolls = ScrollsOverflow(box.style);
  if (clip) PushOverflowClip(box, r);
  if (scrolls) PushScroll(box, r);
  if (xform) PushTransform(box, r);

  std::vector<const LayoutBox*> negative, auto_z, positive;
  CollectLayers(box, &negative, &auto_z, &positive);
  auto by_z = [](const LayoutBox* a, const LayoutBox* b) {
    return LayerZ(a->style) < LayerZ(b->style);
  };
  std::stable_sort(negative.begin(), negative.end(), by_z);
  std::stable_sort(positive.begin(), positive.end(), by_z);

  for (const LayoutBox* layer : negative) PaintStackingContext(*layer, r, false);
  PaintSubtree(box, PaintPhase::kBlockBackground, r, true);
  PaintSubtree(box, PaintPhase::kFloat, r, true);
  PaintSubtree(box, PaintPhase::kForeground, r, true);
  for (const LayoutBox* layer : auto_z) PaintStackingContext(*layer, r, false);
  for (const LayoutBox* layer : positive) PaintStackingContext(*layer, r, false);
  if (xform) PopTransform(r);
  if (scrolls) PopScroll(r);
  if (clip) PopClip(r);
  (void)is_root;
}

}  // namespace

bool IsStackingContext(const ComputedStyle& style, bool is_root) {
  if (is_root) return true;
  if (style.opacity < 1.0f) return true;
  if (style.transform.has()) return true;
  if (style.position != PositionType::kStatic && !style.z_index_auto) return true;
  return false;
}

DisplayList BuildDisplayList(const LayoutBox& root, const HtmlDocument& document) {
  DisplayList list;
  Recorder r;
  r.document = &document;
  r.out = &list;
  PaintStackingContext(root, r, true);
  return list;
}

}  // namespace blink

#endif  // BLINK_HAS_PAINT_PIPELINE
