// Proves the real CSS cascade (blink/css.h) and the real block/inline/
// float/flex layout engine (blink/layout.h) built to replace the old
// one-line-per-block placeholder -- cascade/specificity tests need no
// font at all; box-model/float/flex geometry tests need a real font for
// word-wrap measurement (wasmskia::MeasureText), so they're gated the
// same way blinker's own paint pipeline is (BLINK_HAS_PAINT_PIPELINE).
#include "test.h"

#include "blink/css.h"
#include "blink/html_parser.h"

using blink::AlignItems;
using blink::BgGradientKind;
using blink::ClearType;
using blink::ComputedStyle;
using blink::ComputeStyle;
using blink::DisplayType;
using blink::FloatType;
using blink::GenericFontFamily;
using blink::JustifyContent;
using blink::ListStyleType;
using blink::ParseHtml;
using blink::ParseStylesheet;
using blink::Stylesheet;
using blink::TextAlign;

TEST(golden_css_tag_selector_applies) {
  Stylesheet sheet = ParseStylesheet("p { color: red; }");
  blink::HtmlDocument doc = ParseHtml("<p>hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle style = ComputeStyle(*p, sheet, nullptr);
  EXPECT_EQ(style.color, 0xFFFF0000u);
}

TEST(golden_css_id_beats_class_beats_tag_specificity) {
  Stylesheet sheet = ParseStylesheet(
      "p { color: black; } .warn { color: orange; } #critical { color: red; }");
  blink::HtmlDocument doc = ParseHtml("<p id=\"critical\" class=\"warn\">hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle style = ComputeStyle(*p, sheet, nullptr);
  EXPECT_EQ(style.color, 0xFFFF0000u);  // #critical wins over .warn and tag p.
}

TEST(golden_css_later_rule_wins_equal_specificity) {
  Stylesheet sheet = ParseStylesheet(".a { color: red; } .b { color: blue; }");
  blink::HtmlDocument doc = ParseHtml("<p class=\"a b\">hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle style = ComputeStyle(*p, sheet, nullptr);
  EXPECT_EQ(style.color, 0xFF0000FFu);  // .b declared later, same specificity as .a.
}

TEST(golden_css_inline_style_beats_any_selector) {
  Stylesheet sheet = ParseStylesheet("#x { color: red; }");
  blink::HtmlDocument doc = ParseHtml("<p id=\"x\" style=\"color: green\">hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle style = ComputeStyle(*p, sheet, nullptr);
  EXPECT_EQ(style.color, 0xFF008000u);
}

TEST(golden_css_descendant_combinator_requires_ancestor) {
  Stylesheet sheet = ParseStylesheet("div p { color: red; }");
  blink::HtmlDocument doc_match = ParseHtml("<div><p>hi</p></div>");
  blink::Node* p_match = doc_match.root->FindFirstElement("p");
  EXPECT_EQ(ComputeStyle(*p_match, sheet, nullptr).color, 0xFFFF0000u);

  blink::HtmlDocument doc_no_match = ParseHtml("<section><p>hi</p></section>");
  blink::Node* p_no_match = doc_no_match.root->FindFirstElement("p");
  EXPECT_EQ(ComputeStyle(*p_no_match, sheet, nullptr).color, 0xFF111111u);  // default text color, no div ancestor.
}

TEST(golden_css_color_and_font_size_inherit_but_margin_does_not) {
  ComputedStyle parent_style;
  parent_style.color = 0xFFAABBCCu;
  parent_style.font_size = 22.0f;
  Stylesheet sheet;  // empty -- purely testing inheritance defaults.
  blink::HtmlDocument doc = ParseHtml("<span>hi</span>");
  blink::Node* span = doc.root->FindFirstElement("span");
  ComputedStyle child = ComputeStyle(*span, sheet, &parent_style);
  EXPECT_EQ(child.color, 0xFFAABBCCu);
  EXPECT(child.font_size == 22.0f);
  EXPECT(child.margin_top.is_auto());  // margin never inherits, regardless of parent.
}

TEST(golden_css_display_none_and_block_defaults) {
  Stylesheet sheet;
  blink::HtmlDocument doc = ParseHtml("<div><script>x</script><p>hi</p><span>hi</span></div>");
  blink::Node* script = doc.root->FindFirstElement("script");
  blink::Node* p = doc.root->FindFirstElement("p");
  blink::Node* span = doc.root->FindFirstElement("span");
  EXPECT(ComputeStyle(*script, sheet, nullptr).display == DisplayType::kNone);
  EXPECT(ComputeStyle(*p, sheet, nullptr).display == DisplayType::kBlock);
  EXPECT(ComputeStyle(*span, sheet, nullptr).display == DisplayType::kInline);
}

TEST(golden_css_margin_shorthand_four_values) {
  Stylesheet sheet = ParseStylesheet("div { margin: 1px 2px 3px 4px; }");
  blink::HtmlDocument doc = ParseHtml("<div>hi</div>");
  blink::Node* div = doc.root->FindFirstElement("div");
  ComputedStyle style = ComputeStyle(*div, sheet, nullptr);
  EXPECT(style.margin_top.value == 1.0f);
  EXPECT(style.margin_right.value == 2.0f);
  EXPECT(style.margin_bottom.value == 3.0f);
  EXPECT(style.margin_left.value == 4.0f);
}

TEST(golden_css_border_radius_parses_one_and_four_values) {
  Stylesheet one = ParseStylesheet(".kpi { border-radius: 10px; }");
  blink::HtmlDocument doc = ParseHtml("<div class=\"kpi\">hi</div>");
  blink::Node* div = doc.root->FindFirstElement("div");
  ComputedStyle a = ComputeStyle(*div, one, nullptr);
  EXPECT(a.radius_tl == 10.0f);
  EXPECT(a.radius_tr == 10.0f);
  EXPECT(a.radius_br == 10.0f);
  EXPECT(a.radius_bl == 10.0f);
  EXPECT(a.has_radius());

  Stylesheet four = ParseStylesheet("div { border-radius: 1px 2px 3px 4px; }");
  ComputedStyle b = ComputeStyle(*div, four, nullptr);
  EXPECT(b.radius_tl == 1.0f);
  EXPECT(b.radius_tr == 2.0f);
  EXPECT(b.radius_br == 3.0f);
  EXPECT(b.radius_bl == 4.0f);
}

TEST(golden_css_border_shorthand_parses_width_and_color) {
  Stylesheet sheet = ParseStylesheet("div { border: 2px solid #ff0000; }");
  blink::HtmlDocument doc = ParseHtml("<div>hi</div>");
  blink::Node* div = doc.root->FindFirstElement("div");
  ComputedStyle style = ComputeStyle(*div, sheet, nullptr);
  EXPECT(style.border_top == 2.0f);
  EXPECT(style.border_left == 2.0f);
  EXPECT_EQ(style.border_color, 0xFFFF0000u);
}

TEST(golden_css_float_and_clear_and_flex_properties_parse) {
  Stylesheet sheet = ParseStylesheet(
      "img { float: left; } .c { clear: both; } "
      ".row { display: flex; justify-content: space-between; align-items: center; gap: 10px; }");
  blink::HtmlDocument doc = ParseHtml("<div class=\"row\"><img><p class=\"c\">hi</p></div>");
  blink::Node* img = doc.root->FindFirstElement("img");
  blink::Node* p = doc.root->FindFirstElement("p");
  blink::Node* row = doc.root->FindFirstElement("div");
  EXPECT(ComputeStyle(*img, sheet, nullptr).float_type == FloatType::kLeft);
  EXPECT(ComputeStyle(*p, sheet, nullptr).clear == ClearType::kBoth);
  ComputedStyle row_style = ComputeStyle(*row, sheet, nullptr);
  EXPECT(row_style.display == DisplayType::kFlex);
  EXPECT(row_style.justify_content == JustifyContent::kSpaceBetween);
  EXPECT(row_style.align_items == AlignItems::kCenter);
  EXPECT(row_style.gap == 10.0f);
}

TEST(golden_css_child_combinator_does_not_match_nested_descendant) {
  Stylesheet sheet = ParseStylesheet("div > p { color: red; }");
  blink::HtmlDocument match = ParseHtml("<div><p>hi</p></div>");
  blink::HtmlDocument nested = ParseHtml("<div><span><p>hi</p></span></div>");
  EXPECT_EQ(ComputeStyle(*match.root->FindFirstElement("p"), sheet, nullptr).color, 0xFFFF0000u);
  EXPECT_EQ(ComputeStyle(*nested.root->FindFirstElement("p"), sheet, nullptr).color, 0xFF111111u);
}

TEST(golden_css_nth_child_and_attribute_selector) {
  Stylesheet sheet = ParseStylesheet(
      "p:nth-child(2) { color: blue; } input[type=text] { color: green; }");
  blink::HtmlDocument doc = ParseHtml(
      "<div><p>one</p><p>two</p><input type=\"text\"></div>");
  blink::Node* div = doc.root->FindFirstElement("div");
  blink::Node* first = nullptr;
  blink::Node* second = nullptr;
  blink::Node* input = nullptr;
  for (auto& c : div->children) {
    if (c->type != blink::NodeType::kElement) continue;
    if (c->tag_name == "p" && !first) first = c.get();
    else if (c->tag_name == "p") second = c.get();
    if (c->tag_name == "input") input = c.get();
  }
  EXPECT(first && second && input);
  EXPECT_EQ(ComputeStyle(*first, sheet, nullptr).color, 0xFF111111u);
  EXPECT_EQ(ComputeStyle(*second, sheet, nullptr).color, 0xFF0000FFu);
  EXPECT_EQ(ComputeStyle(*input, sheet, nullptr).color, 0xFF008000u);
}

TEST(golden_css_important_beats_later_non_important) {
  Stylesheet sheet = ParseStylesheet("p { color: red !important; } p { color: blue; }");
  blink::HtmlDocument doc = ParseHtml("<p>hi</p>");
  EXPECT_EQ(ComputeStyle(*doc.root->FindFirstElement("p"), sheet, nullptr).color, 0xFFFF0000u);
}

TEST(golden_css_em_font_size_is_relative_to_parent) {
  Stylesheet sheet = ParseStylesheet("div { font-size: 20px; } p { font-size: 2em; }");
  blink::HtmlDocument doc = ParseHtml("<div><p>hi</p></div>");
  blink::Node* div = doc.root->FindFirstElement("div");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle ds = ComputeStyle(*div, sheet, nullptr);
  ComputedStyle ps = ComputeStyle(*p, sheet, &ds);
  EXPECT(ps.font_size == 40.0f);
}

TEST(golden_css_position_and_flex_grow_parse) {
  Stylesheet sheet = ParseStylesheet(
      "#abs { position: absolute; top: 10px; left: 5px; z-index: 3; } "
      "span { flex-grow: 1; }");
  blink::HtmlDocument doc = ParseHtml("<div id=\"abs\"></div><span></span>");
  ComputedStyle abs = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(abs.position == blink::PositionType::kAbsolute);
  EXPECT(abs.top.value == 10.0f);
  EXPECT(abs.z_index == 3);
  ComputedStyle span = ComputeStyle(*doc.root->FindFirstElement("span"), sheet, nullptr);
  EXPECT(span.flex_grow == 1.0f);
}

TEST(golden_css_at_rule_bodies_are_skipped_not_misparsed) {
  Stylesheet sheet = ParseStylesheet(
      "@media (min-width: 600px) { p { color: red; } } p { color: blue; }");
  blink::HtmlDocument doc = ParseHtml("<p>hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  // The @media body (including its own "p { color: red; }") must not
  // leak out as a top-level rule -- only the real top-level "p { color:
  // blue; }" should apply.
  EXPECT_EQ(ComputeStyle(*p, sheet, nullptr).color, 0xFF0000FFu);
}

TEST(golden_css_table_and_list_ua_display) {
  Stylesheet sheet;
  blink::HtmlDocument doc = ParseHtml(
      "<table><thead><tr><th>h</th></tr></thead>"
      "<tbody><tr><td class=\"ok\">d</td></tr></tbody></table>"
      "<ul><li>x</li></ul>");
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("table"), sheet, nullptr).display ==
         DisplayType::kTable);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("thead"), sheet, nullptr).display ==
         DisplayType::kTableRowGroup);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("tbody"), sheet, nullptr).display ==
         DisplayType::kTableRowGroup);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("tr"), sheet, nullptr).display ==
         DisplayType::kTableRow);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("th"), sheet, nullptr).display ==
         DisplayType::kTableCell);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("td"), sheet, nullptr).display ==
         DisplayType::kTableCell);
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("li"), sheet, nullptr).display ==
         DisplayType::kListItem);
  ComputedStyle ul = ComputeStyle(*doc.root->FindFirstElement("ul"), sheet, nullptr);
  EXPECT(ul.list_style_type == ListStyleType::kDisc);
}

TEST(golden_css_display_table_and_list_item_keywords_parse) {
  Stylesheet sheet = ParseStylesheet(
      "div { display: table; } span { display: list-item; list-style-type: decimal; }");
  blink::HtmlDocument doc = ParseHtml("<div></div><span></span>");
  EXPECT(ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr).display ==
         DisplayType::kTable);
  ComputedStyle span = ComputeStyle(*doc.root->FindFirstElement("span"), sheet, nullptr);
  EXPECT(span.display == DisplayType::kListItem);
  EXPECT(span.list_style_type == ListStyleType::kDecimal);
}

TEST(golden_css_vw_vh_resolve_against_stylesheet_viewport) {
  Stylesheet sheet = ParseStylesheet("div { width: 50vw; margin-top: 10vh; }");
  sheet.viewport_w = 800;
  sheet.viewport_h = 600;
  blink::HtmlDocument doc = ParseHtml("<div>x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.width.unit == blink::Length::Unit::kVw);
  EXPECT(style.width.value == 50.0f);
  EXPECT(style.margin_top.unit == blink::Length::Unit::kVh);
  EXPECT(style.margin_top.value == 10.0f);
  EXPECT(style.width.Resolve(0, 0, 16.0f, 16.0f, 800.0f, 600.0f) == 400.0f);
  EXPECT(style.margin_top.Resolve(0, 0, 16.0f, 16.0f, 800.0f, 600.0f) == 60.0f);
}

TEST(golden_css_media_inner_rules_are_kept) {
  Stylesheet sheet = ParseStylesheet("@media (max-width: 700px) { p { color: red; } }");
  blink::HtmlDocument doc = ParseHtml("<p>hi</p>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("p"), sheet, nullptr);
  EXPECT_EQ(style.color, 0xFFFF0000u);
}

TEST(golden_css_inline_computed_works_with_rmlui) {
  std::string html =
      "<style>#shell{display:flex}</style>"
      "<div id=\"shell\"><div id=\"viewport\">"
      "<style>#page-root{background:#eee;width:60vw;color:#111}h1{font-size:24px}</style>"
      "<div id=\"page-root\"><h1>Example Domain</h1></div>"
      "</div></div>";
  std::string out = blink::InlineComputedCss(html, 800.0f, 600.0f);
  EXPECT(out.find("Example Domain") != std::string::npos);
  EXPECT(out.find("#page-root{background") != std::string::npos);  // sheet kept for RmlUi
  EXPECT(out.find("#shell{display:flex}") != std::string::npos);   // chrome sheet kept
  EXPECT(out.find("id=\"shell\"") != std::string::npos);
  EXPECT(out.find("display: flex") != std::string::npos);  // chrome #shell author RCSS
  // Colors stay on the sheet so :hover/:focus can override — not style="".
  EXPECT(out.find("background-color: #EEEEEE") == std::string::npos &&
         out.find("background-color: #eeeeee") == std::string::npos);
  EXPECT(out.find("60vw") != std::string::npos);  // RmlUi native unit, not 480px
  EXPECT(out.find("width: 480px") == std::string::npos);
  EXPECT(out.find("style=\"") != std::string::npos);  // Blinker still cascaded onto style=""
}

TEST(golden_css_inline_computed_emits_justify_content) {
  std::string html =
      "<div id=\"viewport\">"
      "<style>.row{display:flex;justify-content:center;align-items:flex-end}</style>"
      "<div class=\"row\"><span>a</span><span>b</span></div>"
      "</div>";
  std::string out = blink::InlineComputedCss(html, 800.0f, 600.0f);
  EXPECT(out.find("justify-content: center") != std::string::npos);
  EXPECT(out.find("align-items: flex-end") != std::string::npos);
  EXPECT(out.find("display: flex") != std::string::npos);
}

TEST(golden_css_inline_computed_resolves_vmin_vmax_to_px) {
  // 50vmin of 800x600 = 300px; 25vmax = 200px. AuthorRcss must not map to vw/vh.
  std::string html =
      "<div id=\"viewport\">"
      "<style>#box{width:50vmin;height:25vmax}</style>"
      "<div id=\"box\">x</div>"
      "</div>";
  std::string out = blink::InlineComputedCss(html, 800.0f, 600.0f);
  // AppendLen (AuthorRcss's own vmin/vmax branch) writes "300px"; if the
  // optional paint pipeline is present, InjectComputedWidths's later
  // measured-layout bake (ReplaceStyleWidth) overwrites it with
  // "300.00px" instead -- same two-format situation
  // golden_css_inline_computed_bakes_flex_grow_widths already tolerates
  // below, just not previously applied here.
  EXPECT(out.find("width: 300px") != std::string::npos ||
         out.find("width: 300.00px") != std::string::npos);
  EXPECT(out.find("height: 200px") != std::string::npos ||
         out.find("height: 200.00px") != std::string::npos);
  EXPECT(out.find("width: 50vw") == std::string::npos);
  EXPECT(out.find("height: 25vh") == std::string::npos);
}

TEST(golden_css_inline_computed_bakes_flex_grow_widths) {
  // Dashboard shape: fixed sidebar + flex-grow main. Without baking, RmlUi
  // leaves #main at 0px (black void). Percent row width must resolve against
  // the containing block; main gets the leftover px. Sheet may still say %.
  std::string html =
      "<div id=\"viewport\">"
      "<style>"
      "#dash{display:flex;flex-direction:row;width:100%}"
      "#side{width:220px;flex-grow:0;flex-shrink:0}"
      "#main{display:flex;flex-direction:column;flex-grow:1;flex-shrink:1;min-width:0}"
      "#body{width:100%}"
      "</style>"
      "<div id=\"dash\"><nav id=\"side\">s</nav><div id=\"main\"><div id=\"body\">b</div></div></div>"
      "</div>";
  std::string out = blink::InlineComputedCss(html, 1084.0f, 761.0f);
  auto style_near = [&](const char* id) -> std::string {
    std::string needle = std::string("id=\"") + id + "\"";
    size_t pos = out.find(needle);
    if (pos == std::string::npos) return {};
    size_t tag = out.rfind('<', pos);
    size_t end = out.find('>', pos);
    if (tag == std::string::npos || end == std::string::npos) return {};
    return out.substr(tag, end - tag + 1);
  };
  std::string dash = style_near("dash");
  std::string main = style_near("main");
  std::string body = style_near("body");
  EXPECT(!dash.empty() && !main.empty() && !body.empty());
  // 1084 row, 220 side → 864 main; body 100% of main → 864.
  EXPECT(dash.find("width: 1084.00px") != std::string::npos ||
         dash.find("width: 1084px") != std::string::npos);
  EXPECT(main.find("width: 864.00px") != std::string::npos ||
         main.find("width: 864px") != std::string::npos);
  EXPECT(body.find("width: 864.00px") != std::string::npos ||
         body.find("width: 864px") != std::string::npos);
}

TEST(golden_css_linear_gradient_background) {
  Stylesheet sheet = ParseStylesheet(
      "div { background: linear-gradient(135deg, #22d3ee, #34d399); }");
  blink::HtmlDocument doc = ParseHtml("<div>x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.bg_gradient == BgGradientKind::kLinear);
  EXPECT(style.bg_stops.size() >= 2);
  EXPECT(style.has_background);
  EXPECT(style.bg_grad_angle == 135.0f);
}

TEST(golden_css_radial_gradient_at_position) {
  Stylesheet sheet = ParseStylesheet(
      ".hero { background: radial-gradient(ellipse at 50% 0%, rgba(14,165,233,0.16), transparent 55%); }");
  blink::HtmlDocument doc = ParseHtml("<div class=\"hero\">x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.bg_gradient == BgGradientKind::kRadial);
  EXPECT(style.bg_stops.size() >= 2);
  EXPECT(style.bg_grad_cx == 0.5f);
  EXPECT(style.bg_grad_cy == 0.0f);
}

TEST(golden_css_font_family_generic_and_named_aliases) {
  Stylesheet sheet = ParseStylesheet(
      "#m { font-family: Consolas, monospace; } "
      "#s { font-family: Georgia, serif; } "
      "#a { font-family: \"Segoe UI\", sans-serif; }");
  blink::HtmlDocument doc = ParseHtml(
      "<p id=\"m\">m</p><p id=\"s\">s</p><p id=\"a\">a</p>");
  blink::Node* m = doc.root->FindById("m");
  blink::Node* s = doc.root->FindById("s");
  blink::Node* a = doc.root->FindById("a");
  EXPECT(m && s && a);
  if (!m || !s || !a) return;
  EXPECT(ComputeStyle(*m, sheet, nullptr).font_family == GenericFontFamily::kMonospace);
  EXPECT(ComputeStyle(*s, sheet, nullptr).font_family == GenericFontFamily::kSerif);
  EXPECT(ComputeStyle(*a, sheet, nullptr).font_family == GenericFontFamily::kSansSerif);
}

TEST(golden_css_font_shorthand_sets_italic_weight_size_and_family) {
  Stylesheet sheet = ParseStylesheet("code { font: italic bold 13px/1.4 monospace; }");
  blink::HtmlDocument doc = ParseHtml("<code>x</code>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("code"), sheet, nullptr);
  EXPECT(style.font_italic);
  EXPECT(style.font_bold);
  EXPECT(style.font_family == GenericFontFamily::kMonospace);
  EXPECT(style.font_size > 12.0f && style.font_size < 14.0f);
}

TEST(golden_css_ua_pre_and_code_are_monospace) {
  Stylesheet sheet;
  blink::HtmlDocument doc = ParseHtml("<body><pre>x</pre><code>y</code><p>z</p></body>");
  ComputedStyle pre = ComputeStyle(*doc.root->FindFirstElement("pre"), sheet, nullptr);
  ComputedStyle code = ComputeStyle(*doc.root->FindFirstElement("code"), sheet, nullptr);
  ComputedStyle p = ComputeStyle(*doc.root->FindFirstElement("p"), sheet, nullptr);
  EXPECT(pre.font_family == GenericFontFamily::kMonospace);
  EXPECT(code.font_family == GenericFontFamily::kMonospace);
  EXPECT(p.font_family == GenericFontFamily::kSansSerif);
}

TEST(golden_css_overflow_hidden) {
  Stylesheet sheet = ParseStylesheet("div { overflow: hidden; }");
  blink::HtmlDocument doc = ParseHtml("<div>x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.overflow_x == blink::Overflow::kHidden);
  EXPECT(style.overflow_y == blink::Overflow::kHidden);
}

TEST(golden_css_overflow_x_y_parse_separately) {
  Stylesheet sheet = ParseStylesheet("div { overflow-x: hidden; overflow-y: scroll; }");
  blink::HtmlDocument doc = ParseHtml("<div>x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.overflow_x == blink::Overflow::kHidden);
  EXPECT(style.overflow_y == blink::Overflow::kScroll);
}

TEST(golden_css_overflow_visible_axis_computes_to_auto) {
  Stylesheet sheet = ParseStylesheet("div { overflow-x: hidden; overflow-y: visible; }");
  blink::HtmlDocument doc = ParseHtml("<div>x</div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.overflow_x == blink::Overflow::kHidden);
  EXPECT(style.overflow_y == blink::Overflow::kAuto);
}

#include "blink/block_layout.h"

TEST(golden_block_layout_margin_collapse_math) {
  EXPECT(blink::CollapseMargins(30.0f, 20.0f) == 30.0f);
  EXPECT(blink::CollapseMargins(-10.0f, -20.0f) == -20.0f);
  EXPECT(blink::CollapseMargins(10.0f, -5.0f) == 5.0f);
  EXPECT(blink::EstablishesBlockFormattingContext(
      ComputeStyle(*ParseHtml("<div style=\"overflow:hidden\"></div>").root->FindFirstElement("div"),
                   Stylesheet(), nullptr)));
}

TEST(golden_css_visibility_hidden) {
  Stylesheet sheet = ParseStylesheet(".x { visibility: hidden; }");
  blink::HtmlDocument doc = ParseHtml("<p class=\"x\">x</p>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("p"), sheet, nullptr);
  EXPECT(style.visibility == blink::Visibility::kHidden);
}

TEST(golden_css_grid_template_columns_fr_parses) {
  Stylesheet sheet = ParseStylesheet(".g { display: grid; grid-template-columns: 100px 1fr 2fr; }");
  blink::HtmlDocument doc = ParseHtml("<div class=\"g\"></div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.display == DisplayType::kGrid);
  EXPECT_EQ(style.grid_tracks.size(), static_cast<size_t>(3));
  EXPECT(style.grid_tracks[0].sizing == blink::GridTrackSizing::kPx);
  EXPECT(style.grid_tracks[1].sizing == blink::GridTrackSizing::kFr);
  EXPECT(style.grid_tracks[2].sizing == blink::GridTrackSizing::kFr);
  EXPECT(style.grid_tracks[2].value == 2.0f);
}

TEST(golden_css_box_shadow_parses) {
  Stylesheet sheet = ParseStylesheet("div { box-shadow: 4px 6px 2px #800000; }");
  blink::HtmlDocument doc = ParseHtml("<div></div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.has_box_shadow);
  EXPECT(style.box_shadow.offset_x == 4.0f);
  EXPECT(style.box_shadow.offset_y == 6.0f);
  EXPECT(style.box_shadow.color == 0xFF800000u);
}

TEST(golden_css_font_family_preferred_records_first_name) {
  Stylesheet sheet = ParseStylesheet("p { font-family: Georgia, serif; }");
  blink::HtmlDocument doc = ParseHtml("<p>x</p>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("p"), sheet, nullptr);
  EXPECT(style.font_family_preferred == "georgia");
  EXPECT(style.font_family == GenericFontFamily::kSerif);
}

#ifdef BLINK_HAS_PAINT_PIPELINE

#include "blink/block_layout.h"
#include "blink/display_list.h"
#include "blink/font_set.h"
#include "blink/layout.h"
#include "blink/paint.h"
#include "wasmskia/font.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> ReadWholeFile(const std::string& path) {
  std::vector<uint8_t> data;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return data;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size > 0) {
    data.resize(static_cast<size_t>(size));
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
  }
  std::fclose(f);
  return data;
}

std::vector<uint8_t> LoadSystemFontBytes() {
  for (const char* path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
    std::vector<uint8_t> data = ReadWholeFile(path);
    if (!data.empty()) return data;
  }
  return {};
}

}  // namespace

TEST(golden_layout_box_model_margin_padding_border) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  EXPECT(!ttf.empty());
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  EXPECT(font.valid());

  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"margin: 10px; padding: 5px; border: 2px solid black; "
      "width: 200px;\">hi</div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox& body = result.root;
  EXPECT_EQ(body.children.size(), static_cast<size_t>(1));
  const blink::LayoutBox& div = body.children[0];

  // body itself has a 16px UA padding (see layout.cc's ApplyUserAgentDefaults),
  // so div's margin box starts at body's content origin + its own 10px margin.
  float expected_x = 16.0f + 10.0f;
  float expected_y = 16.0f + 10.0f;
  EXPECT(div.x == expected_x);
  EXPECT(div.y == expected_y);
  EXPECT(div.width == 200.0f);  // explicit width -- border-box, so this is the full border box width.
  EXPECT(div.style.border_top == 2.0f);
  EXPECT(div.style.padding_top.value == 5.0f);
}

TEST(golden_layout_display_none_produces_no_box_and_no_space) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());

  blink::HtmlDocument doc =
      ParseHtml("<body><div style=\"display:none\">hidden</div><p>shown</p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  // Only <p> should have produced a child box -- the display:none div
  // contributes neither a box nor vertical space.
  EXPECT_EQ(result.root.children.size(), static_cast<size_t>(1));
  EXPECT_EQ(result.root.children[0].source->tag_name, std::string("p"));
}

TEST(golden_layout_float_narrows_subsequent_line_width) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());

  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"float:left; width:100px; height:50px;\"></div>"
      "<p>word word word word word word word word word word word word word word word "
      "word word word word word</p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  // Find the <p>'s box (the second child after the float).
  const blink::LayoutBox* p_box = nullptr;
  for (const auto& child : result.root.children) {
    if (child.source->tag_name == "p") p_box = &child;
  }
  EXPECT(p_box != nullptr);
  if (!p_box) return;
  EXPECT(!p_box->fragments.empty());
  // The float occupies x in [16, 116) (body's 16px padding + 100px
  // width) at y in [16, 66). p's first line, at the same y range, must
  // start to the right of the float, not at the plain content edge.
  if (!p_box->fragments.empty()) {
    EXPECT(p_box->fragments.front().x >= 116.0f);
  }
}

TEST(golden_layout_flex_row_justify_space_between) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());

  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"display:flex; justify-content:space-between; width:auto;\">"
      "<div style=\"width:50px;\">a</div><div style=\"width:50px;\">b</div>"
      "<div style=\"width:50px;\">c</div></div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  EXPECT_EQ(result.root.children.size(), static_cast<size_t>(1));
  const blink::LayoutBox& flex_container = result.root.children[0];
  EXPECT_EQ(flex_container.children.size(), static_cast<size_t>(3));
  // Content width is 400 - 2*16 (body padding) = 368. Three 50px items,
  // space-between: first at content_x, last flush against content_x+368-50.
  float content_x = 16.0f;
  float content_width = 400.0f - 32.0f;
  EXPECT(flex_container.children[0].x == content_x);
  float last_expected = content_x + content_width - 50.0f;
  EXPECT(flex_container.children[2].x == last_expected);
  // Middle item strictly between the other two.
  EXPECT(flex_container.children[1].x > flex_container.children[0].x);
  EXPECT(flex_container.children[1].x < flex_container.children[2].x);
}

TEST(golden_layout_flex_column_stacks_with_gap) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());

  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"display:flex; flex-direction:column; gap:20px;\">"
      "<div style=\"height:30px;\">a</div><div style=\"height:30px;\">b</div>"
      "</div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox& flex_container = result.root.children[0];
  EXPECT_EQ(flex_container.children.size(), static_cast<size_t>(2));
  float gap_between = flex_container.children[1].y -
                      (flex_container.children[0].y + flex_container.children[0].height);
  EXPECT(gap_between == 20.0f);
}

TEST(golden_layout_absolute_is_taken_out_of_flow) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"position:relative; width:200px; height:100px;\">"
      "<div style=\"position:absolute; left:20px; top:10px; width:30px; height:30px;\"></div>"
      "<p>after</p></div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox& rel = result.root.children[0];
  EXPECT(rel.children.size() >= 2);
  const blink::LayoutBox* abs = nullptr;
  const blink::LayoutBox* p = nullptr;
  for (const auto& c : rel.children) {
    if (c.style.position == blink::PositionType::kAbsolute) abs = &c;
    if (c.source && c.source->tag_name == "p") p = &c;
  }
  EXPECT(abs != nullptr && p != nullptr);
  if (!abs || !p) return;
  EXPECT(abs->x == rel.x + 20.0f);
  EXPECT(abs->y == rel.y + 10.0f);
  EXPECT(p->y < abs->y + abs->height + 50.0f);
}

TEST(golden_layout_flex_grow_shares_free_space) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><div style=\"display:flex; width:300px;\">"
      "<div style=\"width:50px; flex-grow:1;\"></div>"
      "<div style=\"width:50px; flex-grow:1;\"></div>"
      "</div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox& flex = result.root.children[0];
  EXPECT_EQ(flex.children.size(), static_cast<size_t>(2));
  EXPECT(flex.children[0].width == 150.0f);
  EXPECT(flex.children[1].width == 150.0f);
}

const blink::LayoutBox* FindBoxByTag(const blink::LayoutBox& box, const char* tag) {
  if (box.source && box.source->tag_name == tag) return &box;
  for (const auto& child : box.children) {
    if (const blink::LayoutBox* found = FindBoxByTag(child, tag)) return found;
  }
  return nullptr;
}

TEST(golden_layout_table_cells_are_columns_not_inline_blob) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><table style=\"width:400px;\">"
      "<thead><tr><th>Name</th><th>Kind</th><th>Export</th><th>Status</th></tr></thead>"
      "<tbody><tr><td>guikit.wasm</td><td>language</td><td>compileGML</td><td>live</td></tr>"
      "<tr><td>hello.wasm</td><td>wasi</td><td>_start</td><td>idle</td></tr></tbody>"
      "</table></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 500.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox* table = FindBoxByTag(result.root, "table");
  EXPECT(table != nullptr);
  if (!table) return;
  // thead/tbody must not collapse into the table's own inline lines --
  // that was the catalog painting as one concatenated paragraph.
  EXPECT(table->fragments.empty());
  EXPECT(table->children.size() >= static_cast<size_t>(2));
  const blink::LayoutBox& header = table->children[0];
  const blink::LayoutBox& first_data = table->children[1];
  EXPECT_EQ(header.source->tag_name, std::string("tr"));
  EXPECT_EQ(header.children.size(), static_cast<size_t>(4));
  EXPECT_EQ(first_data.children.size(), static_cast<size_t>(4));
  // Four columns, same row: later cells sit to the right, not below.
  EXPECT(header.children[1].x > header.children[0].x + 8.0f);
  EXPECT(header.children[2].x > header.children[1].x + 8.0f);
  EXPECT(header.children[3].x > header.children[2].x + 8.0f);
  EXPECT(header.children[0].y == header.children[3].y);
  EXPECT(first_data.y >= header.y + header.height - 0.5f);
  EXPECT(first_data.children[0].source->tag_name == std::string("td"));
}

TEST(golden_layout_list_item_is_a_box_not_inline) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml("<body><ul><li>one</li><li>two</li></ul></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox* ul = FindBoxByTag(result.root, "ul");
  EXPECT(ul != nullptr);
  if (!ul) return;
  EXPECT_EQ(ul->children.size(), static_cast<size_t>(2));
  EXPECT(ul->children[0].style.display == DisplayType::kListItem);
  EXPECT(ul->children[1].y > ul->children[0].y);
}

// The inline formatting context (blink/inline_layout.h). Before it, every
// inline run was flattened into one string painted with the *block's*
// style, so none of the following could be represented at all.
namespace {

const blink::InlineFragment* FindFragmentWithText(const blink::LayoutBox& box,
                                                  const std::string& needle) {
  for (const blink::InlineFragment& frag : box.fragments) {
    if (frag.text.find(needle) != std::string::npos) return &frag;
  }
  for (const blink::LayoutBox& child : box.children) {
    if (const blink::InlineFragment* hit = FindFragmentWithText(child, needle)) return hit;
  }
  return nullptr;
}

}  // namespace

TEST(golden_inline_run_keeps_its_own_style_not_the_blocks) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><p style=\"color:#000000;font-size:16px\">plain "
      "<b>bolded</b> <span style=\"color:#ff0000;font-size:24px\">big red</span></p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);

  const blink::InlineFragment* plain = FindFragmentWithText(result.root, "plain");
  const blink::InlineFragment* bolded = FindFragmentWithText(result.root, "bolded");
  const blink::InlineFragment* big = FindFragmentWithText(result.root, "big red");
  EXPECT(plain != nullptr);
  EXPECT(bolded != nullptr);
  EXPECT(big != nullptr);
  if (!plain || !bolded || !big) return;
  EXPECT(!plain->style.font_bold);
  EXPECT(bolded->style.font_bold);         // <b> is its own run now.
  EXPECT(big->style.color == 0xFFFF0000u);  // and keeps its own color...
  EXPECT(big->style.font_size == 24.0f);    // ...and its own size.
}

TEST(golden_inline_runs_on_one_line_share_a_baseline) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><p style=\"font-size:12px\">small "
      "<span style=\"font-size:32px\">large</span></p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);
  const blink::InlineFragment* small = FindFragmentWithText(result.root, "small");
  const blink::InlineFragment* large = FindFragmentWithText(result.root, "large");
  EXPECT(small != nullptr);
  EXPECT(large != nullptr);
  if (!small || !large) return;
  // Mixed sizes on one line sit on a common baseline. The old engine put
  // every run at cursor_y + the *block's* font size.
  EXPECT(std::abs(small->y - large->y) < 0.5f);
  EXPECT(large->x > small->x);
}

TEST(golden_anchor_text_hit_tests_to_the_anchor) {
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0;font-size:16px\">"
      "<p>before <a href=\"https://example.com/\">click here</a> after</p></body>");
  const blink::InlineFragment* link = nullptr;
  {
    std::vector<uint8_t> ttf = LoadSystemFontBytes();
    if (ttf.empty()) return;
    wasmskia::Font font(ttf.data(), ttf.size());
    blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
    EXPECT(result.ok);
    link = FindFragmentWithText(result.root, "click here");
    EXPECT(link != nullptr);
    if (!link) return;
    EXPECT(link->source != nullptr);
    EXPECT_EQ(link->source->tag_name, std::string("a"));
    EXPECT(link->style.text_underline);  // UA sheet underlines links.

    // A point inside the link's own fragment resolves to the <a>, not to
    // the enclosing <p>. Inline elements own no box, so box-only
    // hit-testing structurally could not answer this.
    float px = link->x + link->width * 0.5f;
    float py = link->y - link->ascent * 0.5f;
    const blink::Node* hit = blink::HitTestInline(result.root, px, py);
    EXPECT(hit != nullptr);
    if (hit) EXPECT_EQ(hit->tag_name, std::string("a"));
  }
}

TEST(golden_br_forces_a_line_break) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml("<body><p>first<br>second</p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);
  const blink::InlineFragment* first = FindFragmentWithText(result.root, "first");
  const blink::InlineFragment* second = FindFragmentWithText(result.root, "second");
  EXPECT(first != nullptr);
  EXPECT(second != nullptr);
  if (!first || !second) return;
  EXPECT(second->y > first->y);  // <br> used to contribute nothing at all.
}

TEST(golden_inline_block_shares_a_line_instead_of_stacking) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body><div>"
      "<span style=\"display:inline-block;width:40px;height:20px\"></span>"
      "<span style=\"display:inline-block;width:40px;height:20px\"></span>"
      "</div></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox* div = FindBoxByTag(result.root, "div");
  EXPECT(div != nullptr);
  if (!div) return;
  EXPECT_EQ(div->children.size(), static_cast<size_t>(2));
  if (div->children.size() < 2) return;
  // inline-block was treated as block before, so these stacked.
  EXPECT(div->children[1].x > div->children[0].x);
  EXPECT(std::abs(div->children[1].y - div->children[0].y) < 0.5f);
}

TEST(golden_collapsed_whitespace_does_not_double_between_inlines) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc =
      ParseHtml("<body><p>   leading   and\n\ninner   </p></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);
  std::string joined;
  const blink::LayoutBox* p = FindBoxByTag(result.root, "p");
  EXPECT(p != nullptr);
  if (!p) return;
  for (const blink::InlineFragment& frag : p->fragments) joined += frag.text;
  // Leading and trailing collapsible space is dropped; runs collapse to one.
  EXPECT_EQ(joined, std::string("leading and inner"));
}

TEST(golden_pre_preserves_spaces_and_newlines) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml("<body><pre>a   b\nc</pre></body>");
  blink::LayoutResult result = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(result.ok);
  const blink::LayoutBox* pre = FindBoxByTag(result.root, "pre");
  EXPECT(pre != nullptr);
  if (!pre) return;
  const blink::InlineFragment* a = FindFragmentWithText(*pre, "a   b");
  EXPECT(a != nullptr);  // interior spaces survive under white-space:pre
  const blink::InlineFragment* c = FindFragmentWithText(*pre, "c");
  EXPECT(c != nullptr);
  if (a && c) EXPECT(c->y > a->y);  // the newline broke the line
}

TEST(golden_emoji_paints_color_glyphs_not_tofu_squares) {
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:8px;font-size:48px;background:#ffffff\">😀❤️👍</body>");
  blink::RawFrameResult frame = blink::CaptureRawFrame(doc, 320);
  EXPECT(frame.ok);
  EXPECT(frame.rgba.size() >= static_cast<size_t>(frame.width) * frame.height * 4);
  bool colored = false;
  for (size_t i = 0; i + 4 <= frame.rgba.size(); i += 4) {
    int r = frame.rgba[i], g = frame.rgba[i + 1], b = frame.rgba[i + 2], a = frame.rgba[i + 3];
    if (a > 32 && (std::abs(r - g) > 20 || std::abs(g - b) > 20 || std::abs(r - b) > 20)) {
      colored = true;
      break;
    }
  }
  EXPECT(colored);
}

namespace {

// Paint order is a property of the recorded display list, so these assert
// on item indices rather than on pixels: an ordering bug shows up as
// "green recorded after blue" instead of as a subtly wrong screenshot.
int IndexOfFill(const blink::DisplayList& list, uint32_t color) {
  for (size_t i = 0; i < list.items.size(); ++i) {
    const blink::DisplayItem& item = list.items[i];
    const bool fill = item.kind == blink::DisplayItemKind::kFillRect ||
                      item.kind == blink::DisplayItemKind::kFillRoundRect;
    if (fill && item.color == color) return static_cast<int>(i);
  }
  return -1;
}

int IndexOfText(const blink::DisplayList& list, const std::string& needle) {
  for (size_t i = 0; i < list.items.size(); ++i) {
    const blink::DisplayItem& item = list.items[i];
    if (item.kind == blink::DisplayItemKind::kText &&
        item.text.find(needle) != std::string::npos) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int IndexOfShadow(const blink::DisplayList& list, uint32_t color) {
  for (size_t i = 0; i < list.items.size(); ++i) {
    const blink::DisplayItem& item = list.items[i];
    if (item.kind == blink::DisplayItemKind::kBoxShadow && item.color == color) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int IndexOfKind(const blink::DisplayList& list, blink::DisplayItemKind kind) {
  for (size_t i = 0; i < list.items.size(); ++i) {
    if (list.items[i].kind == kind) return static_cast<int>(i);
  }
  return -1;
}

const blink::DisplayItem* TextItem(const blink::DisplayList& list, const std::string& needle) {
  int at = IndexOfText(list, needle);
  return at < 0 ? nullptr : &list.items[static_cast<size_t>(at)];
}

}  // namespace

TEST(golden_paint_order_positioned_paints_over_later_sibling) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0\">"
      "<div style=\"position:relative;background:#ff0000;width:50px;height:50px\"></div>"
      "<div style=\"background:#00ff00;width:50px;height:50px\"></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int red = IndexOfFill(list, 0xFFFF0000u);
  int green = IndexOfFill(list, 0xFF00FF00u);
  EXPECT(red >= 0);
  EXPECT(green >= 0);
  // Appendix E step 8: positioned descendants paint after all in-flow
  // ones, even ones earlier in the tree. A painter that recursed in tree
  // order and only z-sorted siblings put red first and let green cover it.
  if (red >= 0 && green >= 0) EXPECT(red > green);
}

TEST(golden_paint_order_descendant_background_under_ancestor_text) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\">ancestor"
      "<div style=\"background:#00ff00;height:20px\"></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int green = IndexOfFill(list, 0xFF00FF00u);
  int text = IndexOfText(list, "ancestor");
  EXPECT(green >= 0);
  EXPECT(text >= 0);
  // The phase split: every block background in a stacking context is
  // recorded before any inline content in it, so a descendant's background
  // cannot hide an ancestor's text.
  if (green >= 0 && text >= 0) EXPECT(green < text);
}

TEST(golden_paint_order_float_between_backgrounds_and_text) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\">"
      "<div style=\"float:left;background:#00ff00;width:30px;height:30px\"></div>"
      "<div style=\"background:#0000ff;height:10px\"></div>tail</body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int blue = IndexOfFill(list, 0xFF0000FFu);
  int floated = IndexOfFill(list, 0xFF00FF00u);
  int text = IndexOfText(list, "tail");
  EXPECT(blue >= 0);
  EXPECT(floated >= 0);
  EXPECT(text >= 0);
  if (blue >= 0 && floated >= 0) EXPECT(blue < floated);   // step 4 after step 3
  if (floated >= 0 && text >= 0) EXPECT(floated < text);   // step 5 after step 4
}

TEST(golden_paint_order_negative_z_index_under_its_stacking_contexts_content) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\">"
      "<div style=\"position:relative;z-index:0;background:#0000ff;width:100px;height:100px\">over"
      "<div style=\"position:absolute;z-index:-1;background:#00ff00;width:40px;height:40px\">"
      "</div></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int blue = IndexOfFill(list, 0xFF0000FFu);
  int green = IndexOfFill(list, 0xFF00FF00u);
  int text = IndexOfText(list, "over");
  EXPECT(blue >= 0);
  EXPECT(green >= 0);
  EXPECT(text >= 0);
  // Steps 1-3: above the stacking context's own background, below its content.
  if (blue >= 0 && green >= 0) EXPECT(blue < green);
  if (green >= 0 && text >= 0) EXPECT(green < text);
}

TEST(golden_paint_order_positive_z_index_last) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0\">"
      "<div style=\"position:absolute;z-index:5;background:#ff0000;width:10px;height:10px\"></div>"
      "<div style=\"position:absolute;z-index:1;background:#00ff00;width:10px;height:10px\"></div>"
      "<div style=\"position:absolute;z-index:-2;background:#0000ff;width:10px;height:10px\">"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int red = IndexOfFill(list, 0xFFFF0000u);
  int green = IndexOfFill(list, 0xFF00FF00u);
  int blue = IndexOfFill(list, 0xFF0000FFu);
  EXPECT(blue >= 0);
  EXPECT(green >= 0);
  EXPECT(red >= 0);
  // Sorted by z-index across the whole stacking context, not by tree order.
  if (blue >= 0 && green >= 0) EXPECT(blue < green);
  if (green >= 0 && red >= 0) EXPECT(green < red);
}

TEST(golden_display_list_text_carries_its_own_face_and_decorations) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\"><p>plain <b>bolded</b> "
      "<a href=\"https://example.com/\">linked</a></p></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 600.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  const blink::DisplayItem* plain = TextItem(list, "plain");
  const blink::DisplayItem* bolded = TextItem(list, "bolded");
  const blink::DisplayItem* linked = TextItem(list, "linked");
  EXPECT(plain != nullptr);
  EXPECT(bolded != nullptr);
  EXPECT(linked != nullptr);
  if (!plain || !bolded || !linked) return;
  // Face selection travels with the recorded item, so raster resolves the
  // same FontSet entry that layout measured with.
  EXPECT(!plain->bold);
  EXPECT(bolded->bold);
  EXPECT(linked->underline);
  EXPECT(!plain->underline);
  // The underline itself is a separate fill so raster needs no text-style
  // knowledge at all.
  bool underline_fill = false;
  for (const blink::DisplayItem& item : list.items) {
    if (item.kind != blink::DisplayItemKind::kFillRect) continue;
    if (item.source == linked->source && item.height <= 3.0f && item.width > 1.0f) {
      underline_fill = true;
      break;
    }
  }
  EXPECT(underline_fill);
}

TEST(golden_display_list_rounded_image_is_clipped_and_balanced) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0\"><img src=\"x.png\" width=\"40\" height=\"40\" "
      "style=\"border-radius:20px\"></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int pushes = 0, pops = 0;
  for (const blink::DisplayItem& item : list.items) {
    if (item.kind == blink::DisplayItemKind::kPushClipRoundRect) ++pushes;
    if (item.kind == blink::DisplayItemKind::kPopClip) ++pops;
  }
  // No decoded pixels for x.png here, so no image and no clip pair -- but
  // whatever is recorded must nest, or replay leaves the canvas clipped.
  EXPECT_EQ(pushes, pops);
}

TEST(golden_display_list_pre_records_monospace_family) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\"><p>plain</p><pre>codey</pre></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  const blink::DisplayItem* plain = TextItem(list, "plain");
  const blink::DisplayItem* codey = TextItem(list, "codey");
  EXPECT(plain != nullptr);
  EXPECT(codey != nullptr);
  if (!plain || !codey) return;
  EXPECT(plain->family == blink::GenericFontFamily::kSansSerif);
  EXPECT(codey->family == blink::GenericFontFamily::kMonospace);
}

TEST(golden_layout_margin_collapsing_adjacent_blocks) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"margin:0;margin-bottom:30px;height:10px\"></div>"
      "<div style=\"margin:0;margin-top:20px;height:10px\"></div>"
      "</body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  EXPECT_EQ(layout.root.children.size(), static_cast<size_t>(2));
  const blink::LayoutBox& first = layout.root.children[0];
  const blink::LayoutBox& second = layout.root.children[1];
  const float gap = second.y - (first.y + first.height);
  // CSS 2.1 8.3.1: adjoining margins collapse to the maximum (30px), not the sum (50px).
  EXPECT(std::abs(gap - 30.0f) < 0.5f);
}

TEST(golden_display_list_overflow_hidden_pushes_clip) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"overflow:hidden;width:50px;height:50px;background:#ff0000\">"
      "<div style=\"width:80px;height:80px;background:#00ff00\"></div></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  bool push = false;
  bool pop = false;
  for (const blink::DisplayItem& item : list.items) {
    if (item.kind == blink::DisplayItemKind::kPushClipRect) push = true;
    if (item.kind == blink::DisplayItemKind::kPopClip) pop = true;
  }
  EXPECT(push);
  EXPECT(pop);
}

TEST(golden_display_list_visibility_hidden_skips_own_text) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;font-size:16px\">"
      "<p style=\"visibility:hidden\">gone "
      "<span style=\"visibility:visible\">seen</span></p></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  EXPECT(IndexOfText(list, "gone") < 0);
  EXPECT(IndexOfText(list, "seen") >= 0);
}

TEST(golden_layout_parent_child_margin_top_collapses) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"margin:0;padding:0\">"
      "<div style=\"margin:0;margin-top:30px;height:10px\"></div>"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* outer = FindBoxByTag(layout.root, "div");
  EXPECT(outer != nullptr);
  if (!outer || outer->children.empty()) return;
  const blink::LayoutBox& inner = outer->children[0];
  // Parent top and first-child top margins collapse: the child hugs the
  // parent's content edge instead of sitting 30px below it.
  EXPECT(inner.y < outer->y + outer->style.border_top + 5.0f);
}

TEST(golden_layout_parent_child_margin_bottom_collapses) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"margin:0;padding:0;margin-bottom:40px\">"
      "<div style=\"margin:0;margin-bottom:30px;height:10px\"></div>"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* outer = FindBoxByTag(layout.root, "div");
  EXPECT(outer != nullptr);
  if (!outer || outer->children.empty()) return;
  // Collapsed bottom margin must not inflate the parent's border box.
  EXPECT(std::abs(outer->height - 10.0f) < 0.5f);
}

TEST(golden_layout_flex_wrap_breaks_to_next_line) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"display:flex;flex-wrap:wrap;width:100px\">"
      "<div style=\"width:40px;height:10px\"></div>"
      "<div style=\"width:40px;height:10px\"></div>"
      "<div style=\"width:40px;height:10px\"></div>"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* flex = FindBoxByTag(layout.root, "div");
  EXPECT(flex != nullptr);
  if (!flex || flex->children.size() < 3) return;
  EXPECT(flex->children[2].y > flex->children[0].y + 1.0f);
}

TEST(golden_layout_table_colspan_spans_columns) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<table style=\"width:200px\"><tr><td colspan=\"2\">wide</td></tr>"
      "<tr><td>a</td><td>b</td></tr></table></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* table = FindBoxByTag(layout.root, "table");
  EXPECT(table != nullptr);
  if (!table || table->children.size() < 2) return;
  const blink::LayoutBox& header_row = table->children[0];
  const blink::LayoutBox& data_row = table->children[1];
  EXPECT_EQ(header_row.children.size(), static_cast<size_t>(1));
  EXPECT_EQ(data_row.children.size(), static_cast<size_t>(2));
  EXPECT(std::abs(header_row.children[0].width - 200.0f) < 2.0f);
  EXPECT(std::abs(data_row.children[0].width - 100.0f) < 2.0f);
  EXPECT(std::abs(data_row.children[1].width - 100.0f) < 2.0f);
}

TEST(golden_layout_grid_fr_track_sizes) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"display:grid;grid-template-columns:100px 1fr;width:300px\">"
      "<div></div><div></div></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* grid = FindBoxByTag(layout.root, "div");
  EXPECT(grid != nullptr);
  if (!grid || grid->children.size() < 2) return;
  EXPECT(std::abs(grid->children[0].width - 100.0f) < 2.0f);
  EXPECT(std::abs(grid->children[1].width - 200.0f) < 2.0f);
}

TEST(golden_layout_table_rowspan_spans_rows) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<table style=\"width:200px\"><tr>"
      "<td rowspan=\"2\" style=\"height:60px\">A</td><td style=\"height:20px\">B</td>"
      "</tr><tr><td style=\"height:30px\">C</td></tr></table></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* table = FindBoxByTag(layout.root, "table");
  EXPECT(table != nullptr);
  if (!table || table->children.size() < 2) return;
  const blink::LayoutBox& row0 = table->children[0];
  const blink::LayoutBox& row1 = table->children[1];
  EXPECT(!row0.children.empty());
  const float rowspan_cell_h = row0.children[0].height;
  EXPECT(rowspan_cell_h >= row0.height + row1.height - 1.0f);
}

TEST(golden_display_list_box_shadow_before_background) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"box-shadow:4px 4px 0 #800000;background:#ff0000;width:40px;height:40px\">"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int shadow = IndexOfShadow(list, 0xFF800000u);
  int red = IndexOfFill(list, 0xFFFF0000u);
  EXPECT(shadow >= 0);
  EXPECT(red >= 0);
  if (shadow >= 0 && red >= 0) EXPECT(shadow < red);
}

TEST(golden_display_list_transform_establishes_stacking_context) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"transform:rotate(15deg);background:#00ff00;width:40px;height:40px\">"
      "</div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  EXPECT(IndexOfKind(list, blink::DisplayItemKind::kPushTransform) >= 0);
  EXPECT(IndexOfKind(list, blink::DisplayItemKind::kPopTransform) >= 0);
}

TEST(golden_layout_sticky_top_sticks_on_scroll) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"height:200px\"></div>"
      "<div id=\"stick\" style=\"position:sticky;top:10px;height:20px\"></div>"
      "<div style=\"height:400px\"></div></body>");
  doc.scroll_y = 250.0f;
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font, 600.0f);
  EXPECT(layout.ok);
  const blink::LayoutBox* stick = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "stick") stick = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(stick != nullptr);
  if (!stick) return;
  // Flow position is ~200px; with scroll_y=250 and top:10px the box sticks at 260px.
  EXPECT(stick->y >= 255.0f);
}

TEST(golden_inline_rtl_places_runs_from_the_right) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0;font-size:16px\">"
      "<p dir=\"rtl\" style=\"margin:0;width:200px\">alpha beta</p></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* p = FindBoxByTag(layout.root, "p");
  EXPECT(p != nullptr);
  if (!p || p->fragments.size() < 2) return;
  const blink::InlineFragment* alpha = nullptr;
  const blink::InlineFragment* beta = nullptr;
  for (const blink::InlineFragment& frag : p->fragments) {
    if (frag.text.find("alpha") != std::string::npos) alpha = &frag;
    if (frag.text.find("beta") != std::string::npos) beta = &frag;
  }
  EXPECT(alpha != nullptr);
  EXPECT(beta != nullptr);
  if (!alpha || !beta) return;
  EXPECT(alpha->x > beta->x);
}

TEST(golden_display_list_inset_shadow_after_background) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"box-shadow:inset 0 0 0 2px #008000;background:#ff0000;"
      "width:40px;height:40px\"></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  int red = IndexOfFill(list, 0xFFFF0000u);
  int inset = -1;
  for (size_t i = 0; i < list.items.size(); ++i) {
    if (list.items[i].kind == blink::DisplayItemKind::kBoxShadow && list.items[i].shadow_inset &&
        list.items[i].color == 0xFF008000u) {
      inset = static_cast<int>(i);
      break;
    }
  }
  EXPECT(red >= 0);
  EXPECT(inset >= 0);
  if (red >= 0 && inset >= 0) EXPECT(red < inset);
}

uint32_t RgbaAt(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y) {
  const size_t i = (static_cast<size_t>(y) * width + x) * 4;
  if (i + 3 >= rgba.size()) return 0;
  return static_cast<uint32_t>(rgba[i]) | (static_cast<uint32_t>(rgba[i + 1]) << 8) |
         (static_cast<uint32_t>(rgba[i + 2]) << 16) | (static_cast<uint32_t>(rgba[i + 3]) << 24);
}

TEST(golden_layout_fixed_top_follows_viewport_scroll) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"height:400px\"></div>"
      "<div id=\"fx\" style=\"position:fixed;top:0;left:0;width:10px;height:10px\"></div>"
      "</body>");
  doc.scroll_y = 100.0f;
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font, 300.0f);
  EXPECT(layout.ok);
  const blink::LayoutBox* fx = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "fx") fx = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(fx != nullptr);
  if (!fx) return;
  EXPECT(fx->y >= 95.0f);
  EXPECT(fx->y <= 105.0f);
}

TEST(golden_paint_viewport_scroll_shows_lower_content) {
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div style=\"height:100px;background:#ff0000\"></div>"
      "<div style=\"height:100px;background:#00ff00\"></div></body></html>");
  doc.viewport_height = 100.0f;
  doc.scroll_y = 0.0f;
  blink::RawFrameResult unscroll = blink::CaptureRawFrame(doc, 200, 100);
  doc.scroll_y = 80.0f;
  blink::RawFrameResult scrolled = blink::CaptureRawFrame(doc, 200, 100);
  EXPECT(unscroll.ok);
  EXPECT(scrolled.ok);
  auto red = [](uint32_t px) { return px & 0xFF; };
  auto green = [](uint32_t px) { return (px >> 8) & 0xFF; };
  const uint32_t px_unscroll = RgbaAt(unscroll.rgba, unscroll.width, 10, 30);
  const uint32_t px_scrolled = RgbaAt(scrolled.rgba, scrolled.width, 10, 30);
  EXPECT(red(px_unscroll) > green(px_unscroll));
  EXPECT(green(px_scrolled) > red(px_scrolled));
}

TEST(golden_hit_test_scroll_offsets_viewport_coordinates) {
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"target\" style=\"position:absolute;top:250px;left:10px;width:40px;height:40px\">"
      "</div></body></html>");
  doc.scroll_y = 200.0f;
  doc.viewport_height = 300.0f;
  const float view_y = 60.0f;  // document y = 250 + half height
  const blink::Node* hit = blink::HitTestDocument(doc, 400, 15.0f, view_y);
  EXPECT(hit != nullptr);
  if (hit) EXPECT(hit->GetAttribute("id") == "target");
}

TEST(golden_dynamic_font_family_loads_system_face) {
  const wasmskia::Font* calibri = blink::DynamicFontFamily("calibri");
  if (!calibri || !calibri->valid()) return;
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:8px;font-family:Calibri;font-size:16px\">Sample</body>");
  blink::RawFrameResult frame = blink::CaptureRawFrame(doc, 320);
  EXPECT(frame.ok);
  EXPECT(frame.rgba.size() >= static_cast<size_t>(frame.width) * frame.height * 4);
}

TEST(golden_css_grid_minmax_parses) {
  Stylesheet sheet = ParseStylesheet("div { grid-template-columns: minmax(100px, 1fr) 50px; }");
  blink::HtmlDocument doc = ParseHtml("<div></div>");
  ComputedStyle style = ComputeStyle(*doc.root->FindFirstElement("div"), sheet, nullptr);
  EXPECT(style.grid_tracks.size() == 2u);
  EXPECT(style.grid_tracks[0].has_minmax);
  EXPECT(style.grid_tracks[0].min_px == 100.0f);
  EXPECT(style.grid_tracks[0].sizing == blink::GridTrackSizing::kFr);
  EXPECT(style.grid_tracks[1].sizing == blink::GridTrackSizing::kPx);
}

TEST(golden_layout_grid_minmax_respects_minimum) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div style=\"display:grid;grid-template-columns:minmax(100px,1fr) 100px;width:250px\">"
      "<div></div><div></div></div></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* grid = FindBoxByTag(layout.root, "div");
  EXPECT(grid != nullptr);
  if (!grid || grid->children.size() < 2) return;
  EXPECT(grid->children[0].width >= 99.0f);
  EXPECT(std::abs(grid->children[0].width - 150.0f) < 2.0f);
  EXPECT(std::abs(grid->children[1].width - 100.0f) < 2.0f);
}

TEST(golden_display_list_overflow_scroll_pushes_scroll) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;width:50px;height:50px\">"
      "<div style=\"height:200px;background:#ff0000\"></div></div></body>");
  blink::Node* scroller = doc.root->FindById("scroller");
  EXPECT(scroller != nullptr);
  if (!scroller) return;
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  doc.element_scroll[scroller].y = 40.0f;
  blink::DisplayList list = blink::BuildDisplayList(layout.root, doc);
  EXPECT(IndexOfKind(list, blink::DisplayItemKind::kPushScroll) >= 0);
  EXPECT(IndexOfKind(list, blink::DisplayItemKind::kPopScroll) >= 0);
}

TEST(golden_paint_element_scroll_shows_lower_content) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;width:100px;height:100px\">"
      "<div style=\"height:100px;background:#ff0000\"></div>"
      "<div style=\"height:100px;background:#00ff00\"></div></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  blink::Node* scroller = doc.root->FindById("scroller");
  EXPECT(scroller != nullptr);
  if (!scroller) return;
  const blink::LayoutBox* scroller_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source == scroller) scroller_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  if (!scroller_box) return;
  const blink::PaddingBox pad = blink::ComputePaddingBox(scroller_box->style, scroller_box->x,
                                                         scroller_box->y, scroller_box->width,
                                                         scroller_box->height);
  doc.element_scroll[scroller].y = 0.0f;
  blink::RawFrameResult unscroll = blink::CaptureRawFrame(doc, 200, 300);
  doc.element_scroll[scroller].y = 80.0f;
  blink::RawFrameResult scrolled = blink::CaptureRawFrame(doc, 200, 300);
  EXPECT(unscroll.ok);
  EXPECT(scrolled.ok);
  const uint32_t px = static_cast<uint32_t>(pad.x + 10.0f);
  const uint32_t py = static_cast<uint32_t>(pad.y + 30.0f);
  auto red = [](uint32_t c) { return c & 0xFF; };
  auto green = [](uint32_t c) { return (c >> 8) & 0xFF; };
  const uint32_t before = RgbaAt(unscroll.rgba, unscroll.width, px, py);
  const uint32_t after = RgbaAt(scrolled.rgba, scrolled.width, px, py);
  EXPECT(red(before) > green(before));
  EXPECT(green(after) > red(after));
}

TEST(golden_hit_test_element_scroll_offsets) {
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:scroll;position:absolute;top:0;left:0;"
      "width:120px;height:100px\">"
      "<div id=\"target\" style=\"position:absolute;top:200px;left:10px;width:40px;height:40px\">"
      "</div></div></body></html>");
  blink::Node* scroller = doc.root->FindById("scroller");
  EXPECT(scroller != nullptr);
  if (!scroller) return;
  doc.element_scroll[scroller].y = 5000.0f;  // clamps to scroll extent
  const blink::Node* hit = blink::HitTestDocument(doc, 400, 20.0f, 65.0f);
  EXPECT(hit != nullptr);
  if (hit) EXPECT(hit->GetAttribute("id") == "target");
}

TEST(golden_layout_scroll_extent_computed) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;width:100px;height:80px\">"
      "<div style=\"height:200px\"></div></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* scroller_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "scroller") scroller_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  if (!scroller_box) return;
  EXPECT(scroller_box->scroll_extent_y >= 118.0f);
  EXPECT(scroller_box->scroll_extent_y <= 122.0f);
}

TEST(golden_scroll_offset_clamps_to_extent) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;width:100px;height:80px\">"
      "<div style=\"height:200px\"></div></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* scroller_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "scroller") scroller_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  if (!scroller_box) return;
  blink::Node* scroller = doc.root->FindById("scroller");
  doc.element_scroll[scroller].y = 5000.0f;
  EXPECT(blink::EffectiveScrollY(doc, *scroller_box) == scroller_box->scroll_extent_y);
}

TEST(golden_inline_bidi_places_rtl_and_ltr_runs_on_rtl_lines) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0;font-size:16px\">"
      "<p dir=\"rtl\" style=\"margin:0;width:300px\">"
      "<span id=\"num\">123</span> <span id=\"heb\">עב</span></p></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* p = FindBoxByTag(layout.root, "p");
  EXPECT(p != nullptr);
  if (!p) return;
  const blink::InlineFragment* num = nullptr;
  const blink::InlineFragment* heb = nullptr;
  for (const blink::InlineFragment& frag : p->fragments) {
    if (frag.text.find("123") != std::string::npos) num = &frag;
    if (frag.text.find("עב") != std::string::npos) heb = &frag;
  }
  EXPECT(num != nullptr);
  EXPECT(heb != nullptr);
  if (!num || !heb) return;
  EXPECT(heb->x > num->x);
}

TEST(golden_layout_overflow_x_scroll_extent) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow-x:auto;overflow-y:hidden;width:100px;height:50px\">"
      "<div style=\"width:300px;height:40px\"></div></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* scroller_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "scroller") scroller_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  if (!scroller_box) return;
  EXPECT(scroller_box->scroll_extent_x >= 198.0f);
  EXPECT(scroller_box->scroll_extent_y == 0.0f);
}

TEST(golden_wheel_scroll_viewport) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::FontSet fonts;
  fonts.regular = &font;
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div style=\"height:400px\"></div></body></html>");
  doc.viewport_height = 100.0f;
  doc.scroll_y = 0.0f;
  blink::WheelScrollResult wheel = blink::ApplyWheelScroll(&doc, 200.0f, 100.0f, fonts, 10.0f,
                                                           10.0f, 0.0f, 50.0f);
  EXPECT(wheel.consumed);
  EXPECT(wheel.scrolled_viewport);
  EXPECT(wheel.scroll_target == nullptr);
  EXPECT(doc.scroll_y > 0.0f);
}

TEST(golden_wheel_scroll_element) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::FontSet fonts;
  fonts.regular = &font;
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;width:100px;height:80px\">"
      "<div style=\"height:200px\"></div></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* scroller_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "scroller") scroller_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  if (!scroller_box) return;
  const float hit_x = scroller_box->x + 10.0f;
  const float hit_y = scroller_box->y + 10.0f;
  blink::WheelScrollResult wheel = blink::ApplyWheelScroll(&doc, 200.0f, 300.0f, fonts, hit_x,
                                                           hit_y - doc.scroll_y, 0.0f, 40.0f);
  EXPECT(wheel.consumed);
  EXPECT(!wheel.scrolled_viewport);
  blink::Node* scroller = doc.root->FindById("scroller");
  EXPECT(wheel.scroll_target == scroller);
  EXPECT(doc.element_scroll[scroller].y > 0.0f);
}

TEST(golden_paint_box_shadow_blur_spreads) {
  auto luminance = [](const blink::RawFrameResult& frame, uint32_t x, uint32_t y) {
    const uint32_t c = RgbaAt(frame.rgba, frame.width, x, y);
    return static_cast<int>((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF));
  };
  blink::HtmlDocument sharp = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div style=\"width:40px;height:40px;background:#ffffff;"
      "box-shadow:0 0 0 4px rgba(0,0,0,0.8)\"></div></body></html>");
  blink::HtmlDocument soft = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div style=\"width:40px;height:40px;background:#ffffff;"
      "box-shadow:0 0 12px 4px rgba(0,0,0,0.8)\"></div></body></html>");
  blink::RawFrameResult sharp_frame = blink::CaptureRawFrame(sharp, 120, 80);
  blink::RawFrameResult soft_frame = blink::CaptureRawFrame(soft, 120, 80);
  EXPECT(sharp_frame.ok);
  EXPECT(soft_frame.ok);
  bool blur_reaches_further = false;
  for (uint32_t x = 0; x < 100; ++x) {
    if (luminance(soft_frame, x, 20) + 4 < luminance(sharp_frame, x, 20)) {
      blur_reaches_further = true;
      break;
    }
  }
  EXPECT(blur_reaches_further);
}

TEST(golden_css_pseudo_before_content_parses) {
  Stylesheet sheet = ParseStylesheet("p::before { content: \"*\"; color: red; }");
  blink::HtmlDocument doc = ParseHtml("<p>hi</p>");
  blink::Node* p = doc.root->FindFirstElement("p");
  ComputedStyle elem = ComputeStyle(*p, sheet, nullptr);
  ComputedStyle pseudo = blink::ComputePseudoStyle(*p, blink::PseudoElement::kBefore, sheet, &elem);
  EXPECT(pseudo.has_generated_content);
  EXPECT(pseudo.generated_content == "*");
  EXPECT(pseudo.color == 0xFFFF0000u);
}

TEST(golden_layout_pseudo_before_and_after_wrap_text) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html><head><style>"
      "p::before { content: \"[\"; } p::after { content: \"]\"; }"
      "</style></head><body style=\"margin:0;padding:0\">"
      "<p id=\"p\" style=\"margin:0\">hi</p></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* p_box = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "p") p_box = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(p_box != nullptr);
  if (!p_box || p_box->fragments.empty()) return;
  std::string line;
  for (const blink::InlineFragment& frag : p_box->fragments) line += frag.text;
  EXPECT(line.find("[hi]") != std::string::npos);
}

TEST(golden_layout_pseudo_before_style_overrides) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html><head><style>li::before { content: \"•\"; color: rgb(255,0,0); }</style></head>"
      "<body style=\"margin:0;padding:0\"><ul style=\"margin:0;padding:0;list-style:none\">"
      "<li id=\"item\" style=\"margin:0;color:blue\">x</li></ul></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* li = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "item") li = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(li != nullptr);
  if (!li) return;
  const blink::InlineFragment* bullet = nullptr;
  for (const blink::InlineFragment& frag : li->fragments) {
    if (frag.text.find("•") != std::string::npos) bullet = &frag;
  }
  EXPECT(bullet != nullptr);
  if (bullet) EXPECT(bullet->style.color == 0xFFFF0000u);
}

TEST(golden_css_content_attr_parses) {
  Stylesheet sheet = ParseStylesheet("span::before { content: attr(data-label); }");
  blink::HtmlDocument doc = ParseHtml("<span data-label=\"OK\">x</span>");
  blink::Node* span = doc.root->FindFirstElement("span");
  ComputedStyle elem = ComputeStyle(*span, sheet, nullptr);
  ComputedStyle pseudo = blink::ComputePseudoStyle(*span, blink::PseudoElement::kBefore, sheet, &elem);
  EXPECT(pseudo.has_generated_content);
  EXPECT(pseudo.content_is_attr);
  EXPECT(pseudo.generated_content == "data-label");
  EXPECT(blink::ResolveGeneratedContent(*span, pseudo) == "OK");
}

TEST(golden_layout_sticky_inside_scroll_container) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<div id=\"scroller\" style=\"overflow:auto;height:150px;margin-top:100px\">"
      "<div style=\"height:40px\"></div>"
      "<div id=\"stick\" style=\"position:sticky;top:5px;height:20px\"></div>"
      "<div style=\"height:300px\"></div></div></body></html>");
  blink::Node* scroller = doc.root->FindById("scroller");
  EXPECT(scroller != nullptr);
  if (!scroller) return;
  doc.element_scroll[scroller].y = 80.0f;
  blink::LayoutResult layout = blink::ComputeLayout(doc, 400.0f, font, 600.0f);
  EXPECT(layout.ok);
  const blink::LayoutBox* scroller_box = nullptr;
  const blink::LayoutBox* stick = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "scroller") scroller_box = &box;
    if (box.source && box.source->GetAttribute("id") == "stick") stick = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(scroller_box != nullptr);
  EXPECT(stick != nullptr);
  if (!scroller_box || !stick) return;
  const blink::PaddingBox pad = blink::ComputePaddingBox(scroller_box->style, scroller_box->x,
                                                         scroller_box->y, scroller_box->width,
                                                         scroller_box->height);
  const float visible_y = stick->y - doc.element_scroll[scroller].y;
  EXPECT(visible_y >= pad.y + 4.0f);
  EXPECT(visible_y <= pad.y + 8.0f);
}

TEST(golden_layout_flex_before_inserts_item) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html><head><style>"
      "div::before { content: \"[\"; }"
      "</style></head><body style=\"margin:0;padding:0\">"
      "<div id=\"flex\" style=\"display:flex;margin:0\">"
      "<span id=\"a\">A</span><span>B</span></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 300.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* flex = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "flex") flex = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(flex != nullptr);
  if (!flex || flex->children.size() < 3) return;
  EXPECT(flex->children[0].fragments.size() == 1);
  EXPECT(flex->children[0].fragments[0].text == "[");
  const blink::LayoutBox* a = nullptr;
  for (const blink::LayoutBox& child : flex->children) {
    if (child.source && child.source->GetAttribute("id") == "a") a = &child;
  }
  EXPECT(a != nullptr);
  if (a) EXPECT(flex->children[0].x < a->x);
}

TEST(golden_layout_text_overflow_ellipsis_truncates) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<body style=\"margin:0;padding:0\">"
      "<p id=\"p\" style=\"margin:0;width:80px;overflow:hidden;white-space:nowrap;"
      "text-overflow:ellipsis\">abcdefghijklmnopqrstuvwxyz</p></body>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* p = FindBoxByTag(layout.root, "p");
  EXPECT(p != nullptr);
  if (!p || p->fragments.empty()) return;
  std::string line;
  for (const blink::InlineFragment& frag : p->fragments) line += frag.text;
  EXPECT(line.find("\xE2\x80\xA6") != std::string::npos);
  EXPECT(line.size() < 26u);
}

TEST(golden_css_content_counter_parses_and_layouts) {
  const char* css =
      "ol { counter-reset: item; list-style: none; margin: 0; padding: 0; }"
      "li { counter-increment: item; }"
      "li::before { content: counter(item); }";
  Stylesheet sheet = ParseStylesheet(css);
  blink::HtmlDocument doc = ParseHtml(
      std::string("<html><head><style>") + css +
      "</style></head><body><ol><li id=\"one\">a</li><li id=\"two\">b</li></ol></body></html>");
  blink::Node* one = doc.root->FindById("one");
  blink::Node* two = doc.root->FindById("two");
  EXPECT(one != nullptr);
  EXPECT(two != nullptr);
  if (!one || !two) return;
  ComputedStyle elem = ComputeStyle(*one, sheet, nullptr);
  ComputedStyle pseudo = blink::ComputePseudoStyle(*one, blink::PseudoElement::kBefore, sheet, &elem);
  EXPECT(pseudo.content_is_counter);
  EXPECT(pseudo.generated_content == "item");
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::LayoutResult layout = blink::ComputeLayout(doc, 200.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* li1 = nullptr;
  const blink::LayoutBox* li2 = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source == one) li1 = &box;
    if (box.source == two) li2 = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(li1 != nullptr);
  EXPECT(li2 != nullptr);
  if (!li1 || !li2 || li1->fragments.empty() || li2->fragments.empty()) return;
  EXPECT(li1->fragments[0].text == "1");
  EXPECT(li2->fragments[0].text == "2");
}

TEST(golden_layout_content_url_pseudo_flex) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html><head><style>"
      "div::before { content: url(\"dot.png\"); display: inline-block; }"
      "</style></head><body style=\"margin:0;padding:0\">"
      "<div id=\"flex\" style=\"display:flex;margin:0\">"
      "<span>A</span></div></body></html>");
  blink::DecodedImage dot;
  dot.width = 8;
  dot.height = 8;
  dot.rgba.assign(8 * 8 * 4, 0);
  for (int i = 0; i < 8 * 8; ++i) {
    dot.rgba[static_cast<size_t>(i) * 4 + 0] = 255;
    dot.rgba[static_cast<size_t>(i) * 4 + 3] = 255;
  }
  doc.images_by_url["dot.png"] = dot;
  blink::LayoutResult layout = blink::ComputeLayout(doc, 300.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* flex = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "flex") flex = &box;
    for (const blink::LayoutBox& child : box.children) walk(child);
  };
  walk(layout.root);
  EXPECT(flex != nullptr);
  if (!flex || flex->children.empty()) return;
  EXPECT(flex->children[0].content_image != nullptr);
  EXPECT(flex->children[0].width >= 8.0f);
}

TEST(golden_wheel_scroll_viewport_horizontal) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::FontSet fonts;
  fonts.regular = &font;
  blink::HtmlDocument doc = ParseHtml(
      "<html style=\"margin:0;padding:0\"><body style=\"margin:0;padding:0\">"
      "<div style=\"width:600px;height:20px\"></div></body></html>");
  doc.viewport_height = 100.0f;
  doc.scroll_x = 0.0f;
  blink::WheelScrollResult wheel = blink::ApplyWheelScroll(&doc, 200.0f, 100.0f, fonts, 10.0f,
                                                           10.0f, 80.0f, 0.0f);
  EXPECT(wheel.consumed);
  EXPECT(wheel.scrolled_viewport);
  EXPECT(doc.scroll_x > 0.0f);
}

TEST(golden_layout_flex_after_justify_end) {
  std::vector<uint8_t> ttf = LoadSystemFontBytes();
  if (ttf.empty()) return;
  wasmskia::Font font(ttf.data(), ttf.size());
  blink::HtmlDocument doc = ParseHtml(
      "<html><head><style>"
      "div::after { content: \"]\"; }"
      "</style></head><body style=\"margin:0;padding:0\">"
      "<div id=\"flex\" style=\"display:flex;justify-content:flex-end;width:200px;margin:0\">"
      "<span id=\"a\">A</span></div></body></html>");
  blink::LayoutResult layout = blink::ComputeLayout(doc, 300.0f, font);
  EXPECT(layout.ok);
  const blink::LayoutBox* flex = nullptr;
  const blink::LayoutBox* a = nullptr;
  const blink::LayoutBox* after = nullptr;
  std::function<void(const blink::LayoutBox&)> walk = [&](const blink::LayoutBox& box) {
    if (box.source && box.source->GetAttribute("id") == "flex") flex = &box;
    if (box.source && box.source->GetAttribute("id") == "a") a = &box;
    for (const blink::LayoutBox& child : box.children) {
      if (child.fragments.size() == 1 && child.fragments[0].text == "]") after = &child;
      walk(child);
    }
  };
  walk(layout.root);
  EXPECT(flex != nullptr);
  EXPECT(a != nullptr);
  EXPECT(after != nullptr);
  if (!flex || !a || !after) return;
  EXPECT(after->x > a->x);
  EXPECT(after->x + after->width <= flex->x + flex->width + 1.0f);
}

#endif  // BLINK_HAS_PAINT_PIPELINE
