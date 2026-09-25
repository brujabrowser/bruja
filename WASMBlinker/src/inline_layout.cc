#include "blink/inline_layout.h"

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "wasmskia/font.h"

#include "blink/block_layout.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace blink {

namespace {

constexpr char kEllipsisUtf8[] = "\xE2\x80\xA6";

void PopUtf8Char(std::string* s) {
  if (!s || s->empty()) return;
  size_t i = s->size();
  do {
    --i;
  } while (i > 0 && (static_cast<unsigned char>((*s)[i]) & 0xC0) == 0x80);
  s->resize(i);
}

}  // namespace

namespace {

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Elements whose contents this engine does not lay out as inline text.
bool IsSkippedTag(const std::string& tag) {
  return tag == "script" || tag == "style" || tag == "head" || tag == "title" || tag == "meta" ||
         tag == "link" || tag == "noscript" || tag == "template";
}

// CSS replaced elements. These are atomic on a line no matter what
// `display` says, so `display:inline` must not make the collector recurse
// into them looking for text.
bool IsReplacedTag(const std::string& tag) {
  return tag == "img" || tag == "input" || tag == "textarea" || tag == "select" ||
         tag == "button" || tag == "canvas" || tag == "video" || tag == "audio" ||
         tag == "iframe" || tag == "object" || tag == "embed" || tag == "svg";
}

bool PreservesSpaces(WhiteSpace ws) {
  return ws == WhiteSpace::kPre || ws == WhiteSpace::kPrewrap;
}

bool PreservesNewlines(WhiteSpace ws) {
  return ws == WhiteSpace::kPre || ws == WhiteSpace::kPrewrap || ws == WhiteSpace::kPreline;
}

bool AllowsWrapping(WhiteSpace ws) {
  return ws != WhiteSpace::kNowrap && ws != WhiteSpace::kPre;
}

bool DecodeUtf8(const std::string& utf8, size_t* index, uint32_t* codepoint) {
  if (*index >= utf8.size()) return false;
  const unsigned char b0 = static_cast<unsigned char>(utf8[*index]);
  if (b0 < 0x80) {
    *codepoint = b0;
    ++(*index);
    return true;
  }
  if ((b0 & 0xE0) == 0xC0 && *index + 1 < utf8.size()) {
    *codepoint = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(utf8[*index + 1]) & 0x3F);
    *index += 2;
    return true;
  }
  if ((b0 & 0xF0) == 0xE0 && *index + 2 < utf8.size()) {
    *codepoint = ((b0 & 0x0F) << 12) |
                 ((static_cast<unsigned char>(utf8[*index + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(utf8[*index + 2]) & 0x3F);
    *index += 3;
    return true;
  }
  if ((b0 & 0xF8) == 0xF0 && *index + 3 < utf8.size()) {
    *codepoint = ((b0 & 0x07) << 18) |
                 ((static_cast<unsigned char>(utf8[*index + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(utf8[*index + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(utf8[*index + 3]) & 0x3F);
    *index += 4;
    return true;
  }
  ++(*index);
  return false;
}

bool IsStrongRtl(uint32_t cp) {
  return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
         (cp >= 0xFE70 && cp <= 0xFEFF);
}

bool IsStrongLtr(uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
}

Direction FirstStrongDirection(const std::string& text, Direction paragraph) {
  size_t i = 0;
  uint32_t cp = 0;
  while (DecodeUtf8(text, &i, &cp)) {
    if (IsStrongRtl(cp)) return Direction::kRtl;
    if (IsStrongLtr(cp)) return Direction::kLtr;
  }
  return paragraph;
}

bool SameBidiDirection(const std::string& a, const std::string& b, Direction paragraph) {
  auto only_space = [](const std::string& s) {
    for (char c : s) {
      if (!IsAsciiSpace(c)) return false;
    }
    return !s.empty();
  };
  if (only_space(a) || only_space(b)) return true;
  return FirstStrongDirection(a, paragraph) == FirstStrongDirection(b, paragraph);
}

// A run of text destined for one line, still carrying the style that
// selected its face so paint can reproduce the measurement exactly.
struct LineItem {
  std::string text;
  const Node* source = nullptr;
  const ComputedStyle* style = nullptr;
  float width = 0;
  float ascent = 0;
  float descent = 0;
  bool atomic = false;
  size_t atomic_index = 0;
};

// UAX #9 rule L1 on a single line: reverse contiguous runs at each
// embedding level from the highest down to 1.
void BidiVisualOrder(std::vector<LineItem>* items, Direction base_dir) {
  if (!items || items->empty()) return;
  std::vector<int> levels(items->size(), 0);
  for (size_t i = 0; i < items->size(); ++i) {
    const Direction run = FirstStrongDirection(items->at(i).text, base_dir);
    if (base_dir == Direction::kLtr) {
      levels[i] = run == Direction::kRtl ? 1 : 0;
    } else {
      levels[i] = run == Direction::kLtr ? 1 : 0;
    }
  }
  int max_level = 0;
  for (int level : levels) max_level = std::max(max_level, level);
  for (int level = max_level; level > 0; --level) {
    size_t i = 0;
    while (i < items->size()) {
      while (i < items->size() && levels[i] < level) ++i;
      const size_t start = i;
      while (i < items->size() && levels[i] >= level) ++i;
      if (i > start) std::reverse(items->begin() + static_cast<long>(start),
                                  items->begin() + static_cast<long>(i));
    }
  }
}

// CSS inline box height: the font's own ascent/descent plus half-leading
// on each side, where the leading is `line-height` minus the font's
// natural height. This is what makes a 28px run and a 13px run on one
// line share a baseline.
void InlineBoxMetrics(const FontSet& fonts, const ComputedStyle& style, float* out_ascent,
                      float* out_descent) {
  const float size = style.font_size > 0 ? style.font_size : 16.0f;
  const FontMetrics m = FontMetrics::ForFace(fonts.Resolve(style), size);
  const float line_box = size * (style.line_height > 0 ? style.line_height : 1.35f);
  const float half_leading = (line_box - m.height()) * 0.5f;
  *out_ascent = m.ascent + half_leading;
  *out_descent = m.descent + half_leading;
}

float MeasureRun(const FontSet& fonts, const ComputedStyle& style, const std::string& text) {
  const wasmskia::Font* face = fonts.Resolve(style);
  if (!face || text.empty()) return 0.0f;
  return wasmskia::MeasureText(*face, text, style.font_size);
}

// One unit the line breaker can place: a word, or a preserved run of
// spaces, or a forced break.
struct Atom {
  std::string text;
  bool space = false;       // collapsible or preserved whitespace
  bool hard_break = false;  // newline under a white-space that keeps them
};

// Splits a text run into atoms according to its own `white-space`.
// Collapsing has to happen here rather than in the collector because
// whether a space survives depends on what the previous run left behind.
void SplitIntoAtoms(const std::string& text, WhiteSpace ws, std::vector<Atom>* out) {
  const bool keep_spaces = PreservesSpaces(ws);
  const bool keep_newlines = PreservesNewlines(ws);
  size_t i = 0;
  while (i < text.size()) {
    if (IsAsciiSpace(text[i])) {
      size_t start = i;
      bool saw_newline = false;
      while (i < text.size() && IsAsciiSpace(text[i])) {
        if (text[i] == '\n') saw_newline = true;
        ++i;
      }
      if (saw_newline && keep_newlines) {
        Atom a;
        a.hard_break = true;
        out->push_back(a);
        continue;
      }
      Atom a;
      a.space = true;
      a.text = keep_spaces ? text.substr(start, i - start) : " ";
      out->push_back(a);
      continue;
    }
    size_t start = i;
    while (i < text.size() && !IsAsciiSpace(text[i])) ++i;
    Atom a;
    a.text = text.substr(start, i - start);
    out->push_back(a);
  }
}

// Extra baseline offset for `vertical-align` values that do not need the
// finished line box. middle/top/bottom are resolved later, once the line's
// own ascent and descent are known.
float StaticBaselineShift(const ComputedStyle& style) {
  const float size = style.font_size > 0 ? style.font_size : 16.0f;
  switch (style.vertical_align) {
    case VerticalAlign::kSub: return size * 0.2f;
    case VerticalAlign::kSuper: return -size * 0.35f;
    default: return 0.0f;
  }
}

}  // namespace

void CollectInlineItemsForNode(const Node& node, const ComputedStyle& style,
                               const Stylesheet& sheet, std::vector<InlineItem>* out) {
  if (IsSkippedTag(node.tag_name)) return;
  if (style.display == DisplayType::kNone) return;

  if (node.tag_name == "br") {
    InlineItem item;
    item.type = InlineItemType::kBreak;
    item.source = &node;
    item.style = style;
    out->push_back(std::move(item));
    return;
  }

  if (style.float_type != FloatType::kNone) {
    InlineItem item;
    item.type = InlineItemType::kFloat;
    item.source = &node;
    item.box_node = &node;
    item.style = style;
    out->push_back(std::move(item));
    return;
  }

  // Non-replaced inline boxes are transparent to the line: recurse so
  // their text becomes items carrying *their* style, not the block's.
  if (style.display == DisplayType::kInline && !IsReplacedTag(node.tag_name)) {
    CollectInlineItems(node, style, sheet, out);
    return;
  }

  InlineItem item;
  item.type = InlineItemType::kAtomicInline;
  item.source = &node;
  item.box_node = &node;
  item.style = style;
  out->push_back(std::move(item));
}

void CollectInlineItems(const Node& parent, const ComputedStyle& parent_style,
                        const Stylesheet& sheet, std::vector<InlineItem>* out) {
  for (const auto& child : parent.children) {
    if (child->type == NodeType::kText) {
      if (child->text_data.empty()) continue;
      InlineItem item;
      item.type = InlineItemType::kText;
      item.text = child->text_data;
      item.source = &parent;
      item.style = parent_style;
      out->push_back(std::move(item));
      continue;
    }
    if (child->type != NodeType::kElement) continue;
    if (IsSkippedTag(child->tag_name)) continue;
    ComputedStyle cs = ComputeStyle(*child, sheet, &parent_style);
    CollectInlineItemsForNode(*child, cs, sheet, out);
  }
}

InlineLayoutOutput LayoutInlineItems(const std::vector<InlineItem>& items,
                                     const InlineLayoutInput& input) {
  InlineLayoutOutput out;
  out.end_y = input.start_y;
  if (!input.block_style || !input.fonts || !input.fonts->valid()) return out;

  const ComputedStyle& block = *input.block_style;
  const FontSet& fonts = *input.fonts;

  // The strut: every line box is at least as tall as the block's own font
  // and line-height would make it, even a line holding only small text.
  float strut_ascent = 0, strut_descent = 0;
  InlineBoxMetrics(fonts, block, &strut_ascent, &strut_descent);

  float cursor_y = input.start_y;
  std::vector<LineItem> line;
  std::vector<LayoutBox> line_atomics;
  float line_width = 0;

  auto band = [&](float y_top, float y_bottom, float* x, float* w) {
    if (input.available_band) {
      input.available_band(y_top, y_bottom, x, w);
    } else {
      *x = input.content_x;
      *w = input.content_width;
    }
  };

  // Available width for the line currently being filled. Queried against
  // the strut height because the line's real height is not known until it
  // is flushed.
  auto current_avail = [&](float* x, float* w) {
    band(cursor_y, cursor_y + strut_ascent + strut_descent, x, w);
  };

  auto flush_line = [&](bool last) {
    if (line.empty()) {
      // An empty forced break still advances by one strut.
      if (!last) cursor_y += strut_ascent + strut_descent;
      return;
    }
    // Trailing collapsible space never occupies the end of a line.
    while (!line.empty() && !line.back().atomic && !line.back().text.empty() &&
           line.back().text.back() == ' ' &&
           !PreservesSpaces(line.back().style->white_space)) {
      LineItem& back = line.back();
      std::string trimmed = back.text;
      while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
      float new_w = MeasureRun(fonts, *back.style, trimmed);
      line_width -= (back.width - new_w);
      back.text = trimmed;
      back.width = new_w;
      if (back.text.empty()) {
        line_width -= back.width;
        line.pop_back();
        continue;
      }
      break;
    }
    if (line.empty()) {
      if (!last) cursor_y += strut_ascent + strut_descent;
      return;
    }

    float avail_x_pre = 0, avail_w_pre = 0;
    band(cursor_y, cursor_y + strut_ascent + strut_descent, &avail_x_pre, &avail_w_pre);
    const bool ellipsis = block.text_overflow_ellipsis && ClipsOverflowX(block) &&
                          !AllowsWrapping(block.white_space);
    if (ellipsis && line_width > avail_w_pre + 0.01f) {
      while (!line.empty() && line_width > avail_w_pre + 0.01f) {
        LineItem& back = line.back();
        if (back.atomic) {
          line_width -= back.width;
          line.pop_back();
          continue;
        }
        const wasmskia::Font* face = fonts.Resolve(*back.style);
        const float ellipsis_w =
            face ? wasmskia::MeasureText(*face, kEllipsisUtf8, back.style->font_size) : 8.0f;
        const float budget = std::max(0.0f, avail_w_pre - ellipsis_w);
        std::string trimmed = back.text;
        while (!trimmed.empty()) {
          const float w = MeasureRun(fonts, *back.style, trimmed);
          if (w <= budget + 0.01f) break;
          PopUtf8Char(&trimmed);
        }
        back.text = trimmed + kEllipsisUtf8;
        const float new_w = MeasureRun(fonts, *back.style, back.text);
        line_width += new_w - back.width;
        back.width = new_w;
        break;
      }
    }

    float max_ascent = strut_ascent;
    float max_descent = strut_descent;
    for (const LineItem& it : line) {
      max_ascent = std::max(max_ascent, it.ascent);
      max_descent = std::max(max_descent, it.descent);
    }

    float avail_x = 0, avail_w = 0;
    band(cursor_y, cursor_y + max_ascent + max_descent, &avail_x, &avail_w);
    TextAlign align = block.text_align;
    const bool rtl = block.direction == Direction::kRtl;
    if (rtl) {
      if (align == TextAlign::kLeft) align = TextAlign::kRight;
      else if (align == TextAlign::kRight) align = TextAlign::kLeft;
    }
    float offset = 0;
    if (align == TextAlign::kCenter) {
      offset = std::max(0.0f, (avail_w - line_width) * 0.5f);
    } else if (align == TextAlign::kRight) {
      offset = std::max(0.0f, avail_w - line_width);
    }

    const float baseline = cursor_y + max_ascent;
    BidiVisualOrder(&line, block.direction);
    auto place_item = [&](LineItem& it, float x, float top_for_atomic) {
      if (it.atomic) {
        LayoutBox& box = line_atomics[it.atomic_index];
        float top = top_for_atomic;
        switch (it.style->vertical_align) {
          case VerticalAlign::kTop: top = cursor_y; break;
          case VerticalAlign::kBottom: top = cursor_y + max_ascent + max_descent - box.height; break;
          case VerticalAlign::kMiddle: top = baseline - box.height * 0.5f; break;
          default: break;
        }
        ShiftLayoutBox(&box, x - box.x, top - box.y);
        return;
      }
      InlineFragment frag;
      frag.text = it.text;
      frag.source = it.source;
      frag.style = *it.style;
      frag.width = it.width;
      frag.ascent = it.ascent;
      frag.descent = it.descent;
      frag.x = x;
      frag.y = baseline + StaticBaselineShift(*it.style);
      switch (it.style->vertical_align) {
        case VerticalAlign::kTop: frag.y = cursor_y + it.ascent; break;
        case VerticalAlign::kBottom:
          frag.y = cursor_y + max_ascent + max_descent - it.descent;
          break;
        case VerticalAlign::kMiddle: frag.y = baseline - it.ascent * 0.5f + it.descent * 0.5f; break;
        default: break;
      }
      out.fragments.push_back(std::move(frag));
    };

    if (rtl) {
      float pen = avail_x + avail_w - offset;
      for (auto it = line.rbegin(); it != line.rend(); ++it) {
        pen -= it->width;
        float top = baseline - it->ascent;
        place_item(const_cast<LineItem&>(*it), pen, top);
      }
    } else {
      float pen = avail_x + offset;
      for (LineItem& it : line) {
        float top = baseline - it.ascent;
        place_item(it, pen, top);
        pen += it.width;
      }
    }
    for (LayoutBox& box : line_atomics) out.atomic_boxes.push_back(std::move(box));

    out.max_width = std::max(out.max_width, line_width);
    cursor_y += max_ascent + max_descent;
    line.clear();
    line_atomics.clear();
    line_width = 0;
  };

  // Appends text to the line, merging into the previous run when it shares
  // style and source so a whole styled run stays one fragment (and one
  // DrawText call) instead of one fragment per word.
  auto append_text = [&](const std::string& chunk, const InlineItem& item) {
    if (chunk.empty()) return;
    if (!line.empty() && !line.back().atomic && line.back().source == item.source &&
        line.back().style == &item.style &&
        SameBidiDirection(line.back().text, chunk, block.direction)) {
      LineItem& back = line.back();
      std::string merged = back.text + chunk;
      float new_w = MeasureRun(fonts, item.style, merged);
      line_width += new_w - back.width;
      back.text = std::move(merged);
      back.width = new_w;
      return;
    }
    LineItem it;
    it.text = chunk;
    it.source = item.source;
    it.style = &item.style;
    it.width = MeasureRun(fonts, item.style, chunk);
    InlineBoxMetrics(fonts, item.style, &it.ascent, &it.descent);
    line_width += it.width;
    line.push_back(std::move(it));
  };

  auto trailing_space_on_line = [&]() {
    if (line.empty()) return false;
    const LineItem& back = line.back();
    return !back.atomic && !back.text.empty() && back.text.back() == ' ';
  };

  for (const InlineItem& item : items) {
    if (item.type == InlineItemType::kBreak) {
      flush_line(false);
      continue;
    }

    if (item.type == InlineItemType::kAtomicInline || item.type == InlineItemType::kFloat) {
      if (!input.layout_atomic || !item.box_node) continue;
      float avail_x = 0, avail_w = 0;
      current_avail(&avail_x, &avail_w);
      LayoutBox box = input.layout_atomic(*item.box_node, item.style, avail_w, item.pseudo);
      const float box_w = box.width;
      if (!line.empty() && line_width + box_w > avail_w && AllowsWrapping(item.style.white_space)) {
        flush_line(false);
        current_avail(&avail_x, &avail_w);
      }
      LineItem it;
      it.atomic = true;
      it.source = item.source;
      it.style = &item.style;
      it.width = box_w;
      // An atomic inline sits on the baseline by its bottom edge.
      it.ascent = box.height;
      it.descent = 0.0f;
      it.atomic_index = line_atomics.size();
      line_atomics.push_back(std::move(box));
      line_width += box_w;
      line.push_back(std::move(it));
      continue;
    }

    std::vector<Atom> atoms;
    SplitIntoAtoms(item.text, item.style.white_space, &atoms);
    const bool can_wrap = AllowsWrapping(item.style.white_space);

    for (const Atom& atom : atoms) {
      if (atom.hard_break) {
        flush_line(false);
        continue;
      }
      if (atom.space) {
        if (PreservesSpaces(item.style.white_space)) {
          append_text(atom.text, item);
          continue;
        }
        // Collapsible: one space, never at the start of a line, never
        // doubled after a space the previous run already contributed.
        if (line.empty() || trailing_space_on_line()) continue;
        append_text(" ", item);
        continue;
      }

      float avail_x = 0, avail_w = 0;
      current_avail(&avail_x, &avail_w);
      // Width the line would reach with this word appended, accounting for
      // the merge (kerning across the join) the same way append_text will.
      float projected;
      if (!line.empty() && !line.back().atomic && line.back().source == item.source &&
          line.back().style == &item.style) {
        projected = line_width - line.back().width +
                    MeasureRun(fonts, item.style, line.back().text + atom.text);
      } else {
        projected = line_width + MeasureRun(fonts, item.style, atom.text);
      }
      if (can_wrap && !line.empty() && projected > avail_w) {
        flush_line(false);
        // The space that justified the break does not survive it.
      }
      append_text(atom.text, item);
    }
  }

  flush_line(true);
  if (!line.empty()) flush_line(true);
  out.end_y = cursor_y;
  return out;
}

InlineIntrinsicWidths InlineIntrinsicSizes(const std::vector<InlineItem>& items,
                                           const FontSet& fonts,
                                           const AtomicWidthFn& atomic_width) {
  InlineIntrinsicWidths out;
  float line_total = 0;   // max-content accumulation for the current line
  float atom_run = 0;     // width of the current unbreakable sequence
  auto end_atom_run = [&]() {
    out.min_content = std::max(out.min_content, atom_run);
    atom_run = 0;
  };
  auto end_line = [&]() {
    end_atom_run();
    out.max_content = std::max(out.max_content, line_total);
    line_total = 0;
  };

  for (const InlineItem& item : items) {
    if (item.type == InlineItemType::kBreak) {
      end_line();
      continue;
    }
    if (item.type == InlineItemType::kFloat) continue;
    if (item.type == InlineItemType::kAtomicInline) {
      if (!atomic_width || !item.box_node) continue;
      const float w = atomic_width(*item.box_node, item.style);
      // An atomic inline is unbreakable, so it contributes to both.
      line_total += w;
      atom_run += w;
      end_atom_run();
      continue;
    }
    std::vector<Atom> atoms;
    SplitIntoAtoms(item.text, item.style.white_space, &atoms);
    const bool wraps = AllowsWrapping(item.style.white_space);
    for (const Atom& atom : atoms) {
      if (atom.hard_break) {
        end_line();
        continue;
      }
      const float w = MeasureRun(fonts, item.style, atom.space ? " " : atom.text);
      line_total += w;
      if (atom.space && wraps) {
        // A collapsible space is where a line may break, so it ends the
        // current unbreakable sequence rather than joining it.
        end_atom_run();
        continue;
      }
      atom_run += w;
    }
  }
  end_line();
  return out;
}

float InlineMaxContentWidth(const std::vector<InlineItem>& items, const FontSet& fonts) {
  return InlineIntrinsicSizes(items, fonts).max_content;
}

}  // namespace blink

#endif  // BLINK_HAS_PAINT_PIPELINE
