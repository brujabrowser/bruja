#ifndef BLINK_CSS_H_
#define BLINK_CSS_H_

#include "blink/dom.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace blink {

enum class DisplayType {
  kBlock,
  kInline,
  kInlineBlock,
  kFlex,
  kGrid,
  kNone,
  kTable,
  kTableRow,
  kTableCell,
  kTableRowGroup,
  kListItem
};
enum class Overflow { kVisible, kHidden, kScroll, kAuto };
enum class Visibility { kVisible, kHidden, kCollapse };
enum class Direction { kLtr, kRtl };
enum class FloatType { kNone, kLeft, kRight };
enum class ClearType { kNone, kLeft, kRight, kBoth };
enum class TextAlign { kLeft, kCenter, kRight };
enum class FlexDirection { kRow, kColumn };
enum class JustifyContent { kFlexStart, kFlexEnd, kCenter, kSpaceBetween, kSpaceAround };
enum class AlignItems { kFlexStart, kFlexEnd, kCenter, kStretch };
enum class PositionType { kStatic, kRelative, kAbsolute, kFixed, kSticky };
enum class FlexWrap { kNowrap, kWrap };
enum class ListStyleType { kNone, kDisc, kDecimal };
enum class PseudoElement { kNone, kBefore, kAfter };
enum class BgGradientKind { kNone, kLinear, kRadial };
// CSS Text `white-space`: controls whether the inline formatting context
// may collapse runs of spaces and whether it may break lines at them.
enum class WhiteSpace { kNormal, kNowrap, kPre, kPrewrap, kPreline };
// CSS Inline `vertical-align`, as far as an inline formatting context that
// has no real font metrics can honor it (see FontMetrics in font_set.h).
enum class VerticalAlign { kBaseline, kMiddle, kTop, kBottom, kSub, kSuper };

// Only the generic families CSS Fonts 4 guarantees. Resolving a real
// family list would need a system font enumerator; a generic keyword is
// what `<pre>`, `<code>` and most page CSS actually depend on, and it is
// enough for layout and paint to agree on a face (see FontSet).
enum class GenericFontFamily { kSansSerif, kSerif, kMonospace };

enum class GridTrackSizing { kAuto, kPx, kFr };

struct GridTrack {
  GridTrackSizing sizing = GridTrackSizing::kAuto;
  float value = 1.0f;
  // When set by `minmax()`, column width is clamped to [min_px, max_px] after
  // the fr/px distribution pass. max_px < 0 means no maximum.
  float min_px = 0.0f;
  float max_px = -1.0f;
  bool has_minmax = false;
};

// CSS Transforms 1 as a 2x3 matrix (a c e / b d f), CSS `matrix()` order.
// Each function post-multiplies, so `translate(10px) rotate(45deg)` rotates
// in local space then translates. Raster decomposes to Canvas
// Translate/Rotate/Scale; WASMSkia has no general Concat.
struct Transform {
  bool none = true;
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  bool has() const { return !none; }
};

struct BoxShadow {
  float offset_x = 0, offset_y = 0, blur = 0, spread = 0;
  uint32_t color = 0x40000000u;
  bool inset = false;
};

struct BgGradientStop {
  float offset = -1.0f;  // 0..1; negative = unspecified (filled after parse)
  uint32_t color = 0xFF000000u;
};

// A CSS length: auto, px, %, em, rem, and viewport units (vw/vh/vmin/vmax).
// Cascade keeps vw/vh as units. Blinker layout calls Length::Resolve.
// InlineComputedCss emits those units as RCSS so RmlUi (Unit::VW/VH) can
// paint the same cascade — not a neutralize pass, not a remote paint.
struct Length {
  enum class Unit { kAuto, kPx, kPercent, kEm, kRem, kVw, kVh, kVmin, kVmax } unit = Unit::kAuto;
  float value = 0.0f;
  bool is_auto() const { return unit == Unit::kAuto; }
  float Resolve(float percent_base, float auto_value, float em_base = 16.0f,
                float rem_base = 16.0f, float viewport_w = 800.0f,
                float viewport_h = 600.0f) const {
    switch (unit) {
      case Unit::kPx: return value;
      case Unit::kPercent: return percent_base * (value / 100.0f);
      case Unit::kEm: return value * em_base;
      case Unit::kRem: return value * rem_base;
      case Unit::kVw: return viewport_w * (value / 100.0f);
      case Unit::kVh: return viewport_h * (value / 100.0f);
      case Unit::kVmin:
        return (viewport_w < viewport_h ? viewport_w : viewport_h) * (value / 100.0f);
      case Unit::kVmax:
        return (viewport_w > viewport_h ? viewport_w : viewport_h) * (value / 100.0f);
      case Unit::kAuto:
      default: return auto_value;
    }
  }
};

// Cascaded style for one element. box-sizing:border-box throughout.
struct ComputedStyle {
  DisplayType display = DisplayType::kInline;
  PositionType position = PositionType::kStatic;
  FloatType float_type = FloatType::kNone;
  ClearType clear = ClearType::kNone;
  Overflow overflow_x = Overflow::kVisible;
  Overflow overflow_y = Overflow::kVisible;
  Visibility visibility = Visibility::kVisible;
  Direction direction = Direction::kLtr;
  Length width;
  Length height;
  Length min_width, max_width, min_height, max_height;
  Length top, right, bottom, left;
  int z_index = 0;
  bool z_index_auto = true;
  Length margin_top, margin_right, margin_bottom, margin_left;
  Length padding_top, padding_right, padding_bottom, padding_left;
  float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;
  uint32_t border_color = 0xFF000000u;
  // CSS border-radius: top-left, top-right, bottom-right, bottom-left.
  float radius_tl = 0, radius_tr = 0, radius_br = 0, radius_bl = 0;
  bool has_radius() const {
    return radius_tl > 0.5f || radius_tr > 0.5f || radius_br > 0.5f || radius_bl > 0.5f;
  }
  bool has_background = false;
  uint32_t background_color = 0xFFFFFFFFu;
  // CSS linear-gradient / radial-gradient — painted by WASMSkia, not RmlUi.
  BgGradientKind bg_gradient = BgGradientKind::kNone;
  float bg_grad_angle = 180.0f;  // CSS degrees (0 = to top)
  float bg_grad_cx = 0.5f;       // radial center as a fraction of the box
  float bg_grad_cy = 0.5f;
  float bg_grad_radius = 0.7f;   // fraction of hypot(width, height)
  std::vector<BgGradientStop> bg_stops;
  uint32_t color = 0xFF111111u;
  float font_size = 16.0f;
  bool font_bold = false;
  bool font_italic = false;
  GenericFontFamily font_family = GenericFontFamily::kSansSerif;
  // First family name in the cascade list (lowercase), used to pick a
  // named system face when one is wired in FontSet (e.g. Georgia vs Times).
  std::string font_family_preferred;
  // text-decoration. Real CSS propagates a decoration from the box that
  // set it to its in-flow descendants rather than inheriting it; treating
  // it as inherited gives the same result for the cases that matter
  // (<a>, <u>, <s>) and keeps it on one bool per fragment.
  bool text_underline = false;
  bool text_line_through = false;
  WhiteSpace white_space = WhiteSpace::kNormal;
  VerticalAlign vertical_align = VerticalAlign::kBaseline;
  TextAlign text_align = TextAlign::kLeft;
  float line_height = 1.35f;  // multiplier of font-size
  float opacity = 1.0f;
  float outline_width = 0;
  uint32_t outline_color = 0xFF000000u;
  bool has_box_shadow = false;
  BoxShadow box_shadow;
  Transform transform;
  FlexDirection flex_direction = FlexDirection::kRow;
  FlexWrap flex_wrap = FlexWrap::kNowrap;
  JustifyContent justify_content = JustifyContent::kFlexStart;
  AlignItems align_items = AlignItems::kStretch;
  float gap = 0.0f;
  float flex_grow = 0.0f;
  float flex_shrink = 1.0f;
  Length flex_basis;  // auto = use width/height
  int grid_columns = 0;  // 0 = not a grid; otherwise a simple equal-column track count
  std::vector<GridTrack> grid_tracks;  // when non-empty, overrides equal-column sizing
  ListStyleType list_style_type = ListStyleType::kNone;  // inherited; UA disc on ul, decimal on ol
  // CSS `content` for ::before / ::after.
  bool has_generated_content = false;
  bool content_is_attr = false;
  bool content_is_url = false;
  bool content_is_counter = false;
  std::string generated_content;
  std::string counter_reset_name;
  int counter_reset_value = 0;
  std::string counter_increment_name;
  int counter_increment_value = 1;
  bool text_overflow_ellipsis = false;
};

struct StyleRule {
  std::vector<std::string> selectors;
  std::unordered_map<std::string, std::string> declarations;
  std::unordered_map<std::string, std::string> important;
  int source_order = 0;
};

struct Stylesheet {
  std::vector<StyleRule> rules;
  float viewport_w = 800.0f;
  float viewport_h = 600.0f;
};

// Selector grammar: tag / #id / .class / [attr] / [attr=value] /
// :first-child / :last-child / :nth-child(an+b|odd|even), combined by
// descendant (whitespace), child `>`, adjacent sibling `+`, and general
// sibling `~`. @media/@supports/@layer bodies are flattened into top-level
// rules (inner CSS is kept). Other @-rules are brace-matched and skipped.
Stylesheet ParseStylesheet(const std::string& css_text);

std::unordered_map<std::string, std::string> ParseInlineStyle(const std::string& style_attr);

// Cascaded style: UA defaults, then matching rules by specificity + source
// order (attribute/pseudo-class selectors count as classes, matching real
// CSS), then inline style="", with !important beating non-important at
// each step. color/font-size/font-weight/text-align inherit when unset.
ComputedStyle ComputeStyle(const Node& node, const Stylesheet& sheet,
                           const ComputedStyle* parent_style);

// Cascaded style for a ::before or ::after pseudo on `node`.
ComputedStyle ComputePseudoStyle(const Node& node, PseudoElement pseudo, const Stylesheet& sheet,
                                 const ComputedStyle* element_style);

// Resolves `content` on a pseudo style to the string that should be laid out.
std::string ResolveGeneratedContent(
    const Node& node, const ComputedStyle& style, const HtmlDocument* document = nullptr,
    const std::unordered_map<std::string, int>* counters = nullptr);

// Decoded image for `content: url(...)`, if available.
const DecodedImage* ResolvePseudoContentImage(const Node& node, const ComputedStyle& style,
                                              const HtmlDocument& document);

// Cascade CSS with Blinker and write RCSS onto inline style="" of
// #viewport descendants (vw/vh stay vw/vh). Page <style> is kept so
// RmlUi parses the same sheet. Chrome stylesheets stay. after-Lime
// paints that document — Blinker cascade + RmlUi layout, together.
std::string InlineComputedCss(const std::string& html, float viewport_w, float viewport_h);

// Serialize a parsed document back to HTML (preserves attributes/style="" after cascade).
std::string SerializeHtml(const HtmlDocument& doc);

}  // namespace blink

#endif  // BLINK_CSS_H_
