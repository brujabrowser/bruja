#include "blink/css.h"
#include "blink/html_parser.h"
#include "blink/layout.h"
#include "blink/paint.h"
#include "blink/url.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <tuple>
#include <vector>

namespace blink {

namespace {

std::string Trim(const std::string& s) {
  size_t start = 0, end = s.size();
  while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(start, end - start);
}

std::string ToLowerAscii(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

Overflow ParseOverflowKeyword(const std::string& ov) {
  if (ov == "hidden" || ov == "clip") return Overflow::kHidden;
  if (ov == "scroll") return Overflow::kScroll;
  if (ov == "auto") return Overflow::kAuto;
  return Overflow::kVisible;
}

void NormalizeOverflowAxes(Overflow* x, Overflow* y) {
  if (*x == Overflow::kVisible && *y != Overflow::kVisible) *x = Overflow::kAuto;
  if (*y == Overflow::kVisible && *x != Overflow::kVisible) *y = Overflow::kAuto;
}

std::vector<std::string> SplitTopLevel(const std::string& s, char delim) {
  std::vector<std::string> parts;
  std::string current;
  int paren_depth = 0;
  for (char c : s) {
    if (c == '(') ++paren_depth;
    if (c == ')') --paren_depth;
    if (c == delim && paren_depth == 0) {
      parts.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!Trim(current).empty()) parts.push_back(current);
  return parts;
}

std::vector<std::string> SplitTopLevelWhitespace(const std::string& s) {
  std::vector<std::string> parts;
  std::string current;
  int paren_depth = 0;
  for (char c : s) {
    if (c == '(') ++paren_depth;
    if (c == ')') --paren_depth;
    if (std::isspace(static_cast<unsigned char>(c)) && paren_depth == 0) {
      const std::string trimmed = Trim(current);
      if (!trimmed.empty()) parts.push_back(trimmed);
      current.clear();
    } else {
      current += c;
    }
  }
  const std::string trimmed = Trim(current);
  if (!trimmed.empty()) parts.push_back(trimmed);
  return parts;
}

// Strips /* ... */ comments (real CSS comments, not GML/JS) -- done as a
// first pass so the rest of the parser never has to think about them.
std::string StripComments(const std::string& css) {
  std::string out;
  out.reserve(css.size());
  for (size_t i = 0; i < css.size();) {
    if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
      size_t end = css.find("*/", i + 2);
      i = (end == std::string::npos) ? css.size() : end + 2;
      continue;
    }
    out += css[i];
    ++i;
  }
  return out;
}

// Finds the index of the '}' matching the '{' at `open`, honoring nested
// braces (needed for skipping @media/@keyframes bodies, which themselves
// contain nested rule blocks).
size_t MatchingBrace(const std::string& css, size_t open) {
  int depth = 0;
  for (size_t i = open; i < css.size(); ++i) {
    if (css[i] == '{') ++depth;
    else if (css[i] == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

}  // namespace

namespace {

struct DeclarationBlock {
  std::unordered_map<std::string, std::string> normal;
  std::unordered_map<std::string, std::string> important;
};

DeclarationBlock ParseDeclarationBlock(const std::string& style_attr) {
  DeclarationBlock out;
  for (const std::string& decl : SplitTopLevel(style_attr, ';')) {
    size_t colon = decl.find(':');
    if (colon == std::string::npos) continue;
    std::string prop = ToLowerAscii(Trim(decl.substr(0, colon)));
    std::string value = Trim(decl.substr(colon + 1));
    if (prop.empty() || value.empty()) continue;
    if (prop.size() > 2 && prop[0] == '-') {
      size_t second = prop.find('-', 1);
      if (second != std::string::npos && second + 1 < prop.size()) prop = prop.substr(second + 1);
    }
    bool important = false;
    std::string lower = ToLowerAscii(value);
    size_t bang = lower.rfind("!important");
    if (bang != std::string::npos) {
      important = true;
      value = Trim(value.substr(0, bang));
    }
    if (value.empty()) continue;
    (important ? out.important : out.normal)[prop] = value;
  }
  return out;
}

}  // namespace

std::unordered_map<std::string, std::string> ParseInlineStyle(const std::string& style_attr) {
  DeclarationBlock block = ParseDeclarationBlock(style_attr);
  std::unordered_map<std::string, std::string> out = block.normal;
  for (const auto& kv : block.important) out[kv.first] = kv.second;
  return out;
}

Stylesheet ParseStylesheet(const std::string& css_text) {
  Stylesheet sheet;
  std::string css = StripComments(css_text);
  int source_order = 0;
  size_t i = 0;
  while (i < css.size()) {
    // Skip whitespace between rules.
    while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i]))) ++i;
    if (i >= css.size()) break;

    if (css[i] == '@') {
      size_t ident_end = i + 1;
      while (ident_end < css.size() &&
             (std::isalnum(static_cast<unsigned char>(css[ident_end])) || css[ident_end] == '-'))
        ++ident_end;
      std::string at = ToLowerAscii(css.substr(i + 1, ident_end - (i + 1)));
      size_t brace = css.find('{', i);
      size_t semi = css.find(';', i);
      if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
        size_t end = MatchingBrace(css, brace);
        if (end == std::string::npos) {
          i = css.size();
          continue;
        }
        // Keep inner CSS. RmlUi chokes on @media queries; flattening lets
        // Blinker cascade the declarations instead of dropping the block.
        if (at == "media" || at == "supports" || at == "layer") {
          std::string inner = css.substr(brace + 1, end - brace - 1);
          Stylesheet nested = ParseStylesheet(inner);
          for (StyleRule& r : nested.rules) {
            r.source_order += source_order;
            sheet.rules.push_back(std::move(r));
          }
          source_order += static_cast<int>(nested.rules.size());
        }
        i = end + 1;
      } else {
        i = (semi == std::string::npos) ? css.size() : semi + 1;
      }
      continue;
    }

    size_t brace = css.find('{', i);
    if (brace == std::string::npos) break;
    size_t end = MatchingBrace(css, brace);
    if (end == std::string::npos) break;

    std::string selector_text = Trim(css.substr(i, brace - i));
    std::string body = css.substr(brace + 1, end - brace - 1);
    i = end + 1;

    if (selector_text.empty()) continue;
    StyleRule rule;
    rule.source_order = source_order++;
    for (const std::string& sel : SplitTopLevel(selector_text, ',')) {
      std::string trimmed = Trim(sel);
      if (!trimmed.empty()) rule.selectors.push_back(trimmed);
    }
    DeclarationBlock block = ParseDeclarationBlock(body);
    rule.declarations = std::move(block.normal);
    rule.important = std::move(block.important);
    if (!rule.selectors.empty() && (!rule.declarations.empty() || !rule.important.empty())) {
      sheet.rules.push_back(std::move(rule));
    }
  }
  return sheet;
}

namespace {

enum class Combinator { kDescendant, kChild, kAdjacent, kSibling };

struct Nth {
  bool active = false;
  int a = 0, b = 0;  // an+b
};

struct Compound {
  std::string tag;
  std::string id;
  std::vector<std::string> classes;
  std::vector<std::pair<std::string, std::string>> attrs;  // empty value = [attr] presence
  bool first_child = false;
  bool last_child = false;
  Nth nth;
  PseudoElement pseudo = PseudoElement::kNone;
};

const Node* PreviousElementSibling(const Node* node) {
  if (!node || !node->parent) return nullptr;
  const Node* prev = nullptr;
  for (const auto& c : node->parent->children) {
    if (c.get() == node) return prev;
    if (c->type == NodeType::kElement) prev = c.get();
  }
  return nullptr;
}

int ElementIndex1(const Node* node) {
  if (!node || !node->parent) return 1;
  int i = 0;
  for (const auto& c : node->parent->children) {
    if (c->type != NodeType::kElement) continue;
    ++i;
    if (c.get() == node) return i;
  }
  return 1;
}

int ElementCount(const Node* node) {
  if (!node || !node->parent) return 1;
  int n = 0;
  for (const auto& c : node->parent->children) {
    if (c->type == NodeType::kElement) ++n;
  }
  return n;
}

bool NthMatchesIndex(int index, int a, int b) {
  if (a == 0) return index == b;
  int n = index - b;
  if (n % a != 0) return false;
  return (n / a) >= 0;
}

Nth ParseNth(const std::string& raw) {
  Nth n;
  n.active = true;
  std::string v = ToLowerAscii(Trim(raw));
  if (v == "odd") {
    n.a = 2;
    n.b = 1;
    return n;
  }
  if (v == "even") {
    n.a = 2;
    n.b = 0;
    return n;
  }
  // an+b / n+b / -n+b / b
  size_t npos = v.find('n');
  if (npos == std::string::npos) {
    n.a = 0;
    n.b = std::atoi(v.c_str());
    return n;
  }
  std::string a_s = Trim(v.substr(0, npos));
  if (a_s.empty() || a_s == "+") n.a = 1;
  else if (a_s == "-") n.a = -1;
  else n.a = std::atoi(a_s.c_str());
  std::string rest = Trim(v.substr(npos + 1));
  n.b = rest.empty() ? 0 : std::atoi(rest.c_str());
  return n;
}

Compound ParseCompound(const std::string& text) {
  Compound c;
  size_t i = 0;
  std::string tag;
  while (i < text.size() && text[i] != '#' && text[i] != '.' && text[i] != '[' && text[i] != ':') {
    tag += text[i++];
  }
  c.tag = ToLowerAscii(Trim(tag));
  while (i < text.size()) {
    if (text[i] == '#') {
      size_t start = ++i;
      while (i < text.size() && text[i] != '.' && text[i] != '#' && text[i] != '[' && text[i] != ':')
        ++i;
      c.id = text.substr(start, i - start);
    } else if (text[i] == '.') {
      size_t start = ++i;
      while (i < text.size() && text[i] != '.' && text[i] != '#' && text[i] != '[' && text[i] != ':')
        ++i;
      c.classes.push_back(text.substr(start, i - start));
    } else if (text[i] == '[') {
      ++i;
      size_t name_start = i;
      while (i < text.size() && text[i] != ']' && text[i] != '=' && text[i] != '^' && text[i] != '*' &&
             text[i] != '$')
        ++i;
      std::string name = ToLowerAscii(Trim(text.substr(name_start, i - name_start)));
      std::string value;
      if (i < text.size() && text[i] == '=') {
        ++i;
        if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
          char q = text[i++];
          size_t vs = i;
          while (i < text.size() && text[i] != q) ++i;
          value = text.substr(vs, i - vs);
          if (i < text.size()) ++i;
        } else {
          size_t vs = i;
          while (i < text.size() && text[i] != ']') ++i;
          value = Trim(text.substr(vs, i - vs));
        }
      }
      while (i < text.size() && text[i] != ']') ++i;
      if (i < text.size()) ++i;
      if (!name.empty()) c.attrs.push_back({name, value});
    } else if (text[i] == ':') {
      const bool pseudo_elem = (i + 1 < text.size() && text[i + 1] == ':');
      if (pseudo_elem) ++i;
      ++i;
      size_t start = i;
      while (i < text.size() && text[i] != '(' && text[i] != ':' && text[i] != '.' && text[i] != '#' &&
             text[i] != '[')
        ++i;
      std::string pseudo = ToLowerAscii(text.substr(start, i - start));
      std::string arg;
      if (i < text.size() && text[i] == '(') {
        ++i;
        size_t as = i;
        int depth = 1;
        while (i < text.size() && depth > 0) {
          if (text[i] == '(') ++depth;
          else if (text[i] == ')') --depth;
          if (depth > 0) ++i;
        }
        arg = text.substr(as, i - as);
        if (i < text.size() && text[i] == ')') ++i;
      }
      if (pseudo_elem) {
        if (pseudo == "before") c.pseudo = PseudoElement::kBefore;
        else if (pseudo == "after") c.pseudo = PseudoElement::kAfter;
      } else if (pseudo == "first-child") {
        c.first_child = true;
      } else if (pseudo == "last-child") {
        c.last_child = true;
      } else if (pseudo == "nth-child") {
        c.nth = ParseNth(arg);
      }
    } else {
      ++i;
    }
  }
  return c;
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok) out.push_back(tok);
  return out;
}

std::vector<std::string> ClassListOf(const Node& node) {
  std::string class_attr = node.GetAttribute("class");
  return SplitWhitespace(class_attr);
}

bool CompoundMatches(const Node& node, const Compound& c) {
  if (node.type != NodeType::kElement) return false;
  if (!c.tag.empty() && c.tag != "*" && c.tag != node.tag_name) return false;
  if (!c.id.empty() && c.id != node.GetAttribute("id")) return false;
  if (!c.classes.empty()) {
    std::vector<std::string> node_classes = ClassListOf(node);
    for (const std::string& want : c.classes) {
      if (std::find(node_classes.begin(), node_classes.end(), want) == node_classes.end()) {
        return false;
      }
    }
  }
  for (const auto& attr : c.attrs) {
    auto it = node.attributes.find(attr.first);
    if (it == node.attributes.end()) return false;
    if (!attr.second.empty() && it->second != attr.second) return false;
  }
  if (c.first_child && ElementIndex1(&node) != 1) return false;
  if (c.last_child && ElementIndex1(&node) != ElementCount(&node)) return false;
  if (c.nth.active && !NthMatchesIndex(ElementIndex1(&node), c.nth.a, c.nth.b)) return false;
  return true;
}

struct ComplexSelector {
  std::vector<Compound> compounds;
  std::vector<Combinator> combinators;
};

ComplexSelector ParseComplexSelector(const std::string& selector_text) {
  ComplexSelector sel;
  size_t i = 0;
  auto skip_ws = [&] {
    while (i < selector_text.size() && std::isspace(static_cast<unsigned char>(selector_text[i])))
      ++i;
  };
  skip_ws();
  while (i < selector_text.size()) {
    size_t start = i;
    while (i < selector_text.size()) {
      char ch = selector_text[i];
      if (std::isspace(static_cast<unsigned char>(ch)) || ch == '>' || ch == '+' || ch == '~') break;
      if (ch == '[') {
        while (i < selector_text.size() && selector_text[i] != ']') ++i;
        if (i < selector_text.size()) ++i;
        continue;
      }
      if (ch == '(') {
        int depth = 0;
        while (i < selector_text.size()) {
          if (selector_text[i] == '(') ++depth;
          else if (selector_text[i] == ')') {
            --depth;
            ++i;
            if (depth == 0) break;
            continue;
          }
          ++i;
        }
        continue;
      }
      ++i;
    }
    std::string compound = Trim(selector_text.substr(start, i - start));
    if (!compound.empty()) sel.compounds.push_back(ParseCompound(compound));
    skip_ws();
    if (i >= selector_text.size()) break;
    Combinator comb = Combinator::kDescendant;
    if (selector_text[i] == '>') {
      comb = Combinator::kChild;
      ++i;
    } else if (selector_text[i] == '+') {
      comb = Combinator::kAdjacent;
      ++i;
    } else if (selector_text[i] == '~') {
      comb = Combinator::kSibling;
      ++i;
    }
    skip_ws();
    if (!sel.compounds.empty()) sel.combinators.push_back(comb);
  }
  return sel;
}

bool SelectorMatches(const Node& node, const std::string& selector_text) {
  ComplexSelector sel = ParseComplexSelector(selector_text);
  if (sel.compounds.empty()) return false;
  if (!CompoundMatches(node, sel.compounds.back())) return false;
  const Node* cursor = &node;
  for (size_t ci = sel.compounds.size() - 1; ci-- > 0;) {
    Combinator comb = sel.combinators[ci];
    const Node* found = nullptr;
    if (comb == Combinator::kChild) {
      if (cursor->parent && CompoundMatches(*cursor->parent, sel.compounds[ci])) {
        found = cursor->parent;
      }
    } else if (comb == Combinator::kAdjacent) {
      const Node* prev = PreviousElementSibling(cursor);
      if (prev && CompoundMatches(*prev, sel.compounds[ci])) found = prev;
    } else if (comb == Combinator::kSibling) {
      for (const Node* prev = PreviousElementSibling(cursor); prev;
           prev = PreviousElementSibling(prev)) {
        if (CompoundMatches(*prev, sel.compounds[ci])) {
          found = prev;
          break;
        }
      }
    } else {
      for (const Node* anc = cursor->parent; anc; anc = anc->parent) {
        if (CompoundMatches(*anc, sel.compounds[ci])) {
          found = anc;
          break;
        }
      }
    }
    if (!found) return false;
    cursor = found;
  }
  return true;
}

std::tuple<int, int, int> Specificity(const std::string& selector_text) {
  int ids = 0, classes = 0, tags = 0;
  ComplexSelector sel = ParseComplexSelector(selector_text);
  for (const Compound& c : sel.compounds) {
    if (!c.id.empty()) ++ids;
    classes += static_cast<int>(c.classes.size());
    classes += static_cast<int>(c.attrs.size());
    if (c.first_child) ++classes;
    if (c.last_child) ++classes;
    if (c.nth.active) ++classes;
    if (!c.tag.empty() && c.tag != "*") ++tags;
  }
  return {ids, classes, tags};
}

float ParsePx(const std::string& raw) { return static_cast<float>(std::atof(raw.c_str())); }

Length ParseLength(const std::string& raw) {
  Length len;
  std::string v = ToLowerAscii(Trim(raw));
  if (v.empty() || v == "auto") return len;
  if (!v.empty() && v.back() == '%') {
    len.unit = Length::Unit::kPercent;
    len.value = ParsePx(v.substr(0, v.size() - 1));
    return len;
  }
  auto strip_unit = [&](const char* unit, Length::Unit u) {
    size_t n = std::strlen(unit);
    if (v.size() > n && v.compare(v.size() - n, n, unit) == 0) {
      len.unit = u;
      len.value = ParsePx(v.substr(0, v.size() - n));
      return true;
    }
    return false;
  };
  if (strip_unit("rem", Length::Unit::kRem)) return len;
  if (strip_unit("em", Length::Unit::kEm)) return len;
  if (strip_unit("vmin", Length::Unit::kVmin)) return len;
  if (strip_unit("vmax", Length::Unit::kVmax)) return len;
  if (strip_unit("dvh", Length::Unit::kVh) || strip_unit("svh", Length::Unit::kVh) ||
      strip_unit("lvh", Length::Unit::kVh))
    return len;
  if (strip_unit("dvw", Length::Unit::kVw) || strip_unit("svw", Length::Unit::kVw) ||
      strip_unit("lvw", Length::Unit::kVw))
    return len;
  if (strip_unit("vw", Length::Unit::kVw)) return len;
  if (strip_unit("vh", Length::Unit::kVh)) return len;
  if (strip_unit("px", Length::Unit::kPx)) return len;
  len.unit = Length::Unit::kPx;
  len.value = ParsePx(v);
  return len;
}

// A modest named-color table (not the full CSS Color Module Level 4 list)
// plus #hex and rgb()/rgba() -- covers what real pages/tests plausibly
// use without vendoring a 150+ entry table.
bool ParseColor(const std::string& raw, uint32_t* out) {
  std::string v = ToLowerAscii(Trim(raw));
  if (v.empty()) return false;

  static const std::unordered_map<std::string, uint32_t> kNamed = {
      {"transparent", 0x00000000u}, {"black", 0xFF000000u},   {"white", 0xFFFFFFFFu},
      {"red", 0xFFFF0000u},         {"green", 0xFF008000u},   {"blue", 0xFF0000FFu},
      {"yellow", 0xFFFFFF00u},      {"orange", 0xFFFFA500u},  {"gray", 0xFF808080u},
      {"grey", 0xFF808080u},        {"lightgray", 0xFFD3D3D3u}, {"lightgrey", 0xFFD3D3D3u},
      {"darkgray", 0xFFA9A9A9u},    {"darkgrey", 0xFFA9A9A9u}, {"silver", 0xFFC0C0C0u},
      {"purple", 0xFF800080u},      {"pink", 0xFFFFC0CBu},    {"brown", 0xFFA52A2Au},
      {"navy", 0xFF000080u},        {"teal", 0xFF008080u},    {"cyan", 0xFF00FFFFu},
      {"magenta", 0xFFFF00FFu},     {"lime", 0xFF00FF00u},    {"maroon", 0xFF800000u},
      {"olive", 0xFF808000u},       {"indigo", 0xFF4B0082u},  {"gold", 0xFFFFD700u},
      {"whitesmoke", 0xFFF5F5F5u}, {"crimson", 0xFFDC143Cu}, {"coral", 0xFFFF7F50u},
      {"salmon", 0xFFFA8072u},     {"khaki", 0xFFF0E68Cu},   {"beige", 0xFFF5F5DCu},
      {"ivory", 0xFFFFFFF0u},      {"lavender", 0xFFE6E6FAu},
  };
  auto named = kNamed.find(v);
  if (named != kNamed.end()) {
    *out = named->second;
    return true;
  }

  if (v[0] == '#') {
    std::string hex = v.substr(1);
    if (hex.size() == 3) {
      std::string expanded;
      for (char c : hex) { expanded += c; expanded += c; }
      hex = expanded;
    }
    if (hex.size() != 6) return false;
    for (char c : hex) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    unsigned long rgb = std::strtoul(hex.c_str(), nullptr, 16);
    *out = 0xFF000000u | static_cast<uint32_t>(rgb);
    return true;
  }

  if (v.compare(0, 4, "rgb(") == 0 || v.compare(0, 5, "rgba(") == 0) {
    size_t open = v.find('(');
    size_t close = v.find(')', open);
    if (open == std::string::npos || close == std::string::npos) return false;
    std::vector<std::string> parts = SplitTopLevel(v.substr(open + 1, close - open - 1), ',');
    if (parts.size() < 3) return false;
    int r = std::atoi(Trim(parts[0]).c_str());
    int g = std::atoi(Trim(parts[1]).c_str());
    int b = std::atoi(Trim(parts[2]).c_str());
    int a = 255;
    if (parts.size() >= 4) a = static_cast<int>(std::atof(Trim(parts[3]).c_str()) * 255.0f);
    auto clamp = [](int x) { return static_cast<uint32_t>(x < 0 ? 0 : (x > 255 ? 255 : x)); };
    *out = (clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
    return true;
  }

  return false;
}

void NormalizeGradientStops(std::vector<BgGradientStop>* stops) {
  if (!stops || stops->empty()) return;
  if (stops->size() == 1) {
    if ((*stops)[0].offset < 0) (*stops)[0].offset = 0;
    BgGradientStop dup = (*stops)[0];
    dup.offset = 1;
    stops->push_back(dup);
    return;
  }
  if (stops->front().offset < 0) stops->front().offset = 0;
  if (stops->back().offset < 0) stops->back().offset = 1;
  for (size_t i = 1; i + 1 < stops->size(); ++i) {
    if ((*stops)[i].offset >= 0) continue;
    size_t j = i + 1;
    while (j < stops->size() && (*stops)[j].offset < 0) ++j;
    float a = (*stops)[i - 1].offset;
    float b = j < stops->size() ? (*stops)[j].offset : 1.0f;
    size_t n = j - (i - 1);
    for (size_t k = 1; k < n; ++k) {
      (*stops)[i - 1 + k].offset = a + (b - a) * (static_cast<float>(k) / static_cast<float>(n));
    }
  }
  std::sort(stops->begin(), stops->end(), [](const BgGradientStop& a, const BgGradientStop& b) {
    return a.offset < b.offset;
  });
}

bool ParseColorStop(const std::string& raw, BgGradientStop* stop) {
  std::string s = Trim(raw);
  if (s.empty() || !stop) return false;
  std::vector<std::string> parts = SplitWhitespace(s);
  auto looks_pos = [](const std::string& t) {
    return t.find('%') != std::string::npos || t.find("px") != std::string::npos;
  };
  uint32_t color = 0;
  if (parts.size() >= 2 && looks_pos(parts.back())) {
    std::string pos = parts.back();
    std::string color_s = Trim(s.substr(0, s.size() - pos.size()));
    if (!ParseColor(color_s, &color)) return false;
    stop->color = color;
    if (pos.find('%') != std::string::npos)
      stop->offset = static_cast<float>(std::atof(pos.c_str())) / 100.0f;
    else
      stop->offset = -1;
    return true;
  }
  if (ParseColor(s, &color)) {
    stop->color = color;
    stop->offset = -1;
    return true;
  }
  return false;
}

float ParseLinearAngle(const std::string& dir) {
  std::string d = ToLowerAscii(Trim(dir));
  if (d.size() > 3 && d.compare(d.size() - 3, 3, "deg") == 0)
    return static_cast<float>(std::atof(d.c_str()));
  if (d.compare(0, 3, "to ") == 0) {
    bool t = d.find("top") != std::string::npos;
    bool b = d.find("bottom") != std::string::npos;
    bool l = d.find("left") != std::string::npos;
    bool r = d.find("right") != std::string::npos;
    if (t && r) return 45;
    if (b && r) return 135;
    if (b && l) return 225;
    if (t && l) return 315;
    if (t) return 0;
    if (r) return 90;
    if (b) return 180;
    if (l) return 270;
  }
  return 180;
}

bool ParseGradient(const std::string& v, ComputedStyle* style) {
  if (!style) return false;
  std::string low = ToLowerAscii(v);
  bool radial = false;
  size_t p = low.find("linear-gradient(");
  if (p == std::string::npos) {
    p = low.find("radial-gradient(");
    if (p == std::string::npos) return false;
    radial = true;
  }
  size_t open = v.find('(', p);
  if (open == std::string::npos) return false;
  int depth = 0;
  size_t close = std::string::npos;
  for (size_t i = open; i < v.size(); ++i) {
    if (v[i] == '(') ++depth;
    else if (v[i] == ')') {
      --depth;
      if (depth == 0) {
        close = i;
        break;
      }
    }
  }
  if (close == std::string::npos) return false;
  std::vector<std::string> parts = SplitTopLevel(v.substr(open + 1, close - open - 1), ',');
  if (parts.empty()) return false;

  style->bg_gradient = radial ? BgGradientKind::kRadial : BgGradientKind::kLinear;
  style->bg_grad_angle = 180.0f;
  style->bg_grad_cx = 0.5f;
  style->bg_grad_cy = 0.5f;
  style->bg_grad_radius = 0.7f;
  style->bg_stops.clear();

  size_t i = 0;
  std::string first = ToLowerAscii(Trim(parts[0]));
  uint32_t dummy = 0;
  bool first_is_color = ParseColor(Trim(parts[0]), &dummy);
  if (!radial) {
    bool is_dir = !first_is_color &&
                  (first.compare(0, 3, "to ") == 0 || first.find("deg") != std::string::npos);
    if (is_dir) {
      style->bg_grad_angle = ParseLinearAngle(parts[0]);
      i = 1;
    }
  } else {
    bool geom = !first_is_color && (first.find("circle") != std::string::npos ||
                                    first.find("ellipse") != std::string::npos ||
                                    first.find(" at ") != std::string::npos ||
                                    first.compare(0, 3, "at ") == 0);
    if (geom) {
      size_t at = first.find("at ");
      if (at != std::string::npos) {
        std::vector<std::string> xy = SplitWhitespace(Trim(first.substr(at + 3)));
        if (!xy.empty() && xy[0].find('%') != std::string::npos)
          style->bg_grad_cx = static_cast<float>(std::atof(xy[0].c_str())) / 100.0f;
        if (xy.size() >= 2 && xy[1].find('%') != std::string::npos)
          style->bg_grad_cy = static_cast<float>(std::atof(xy[1].c_str())) / 100.0f;
      }
      i = 1;
    }
  }
  for (; i < parts.size(); ++i) {
    BgGradientStop st;
    if (ParseColorStop(parts[i], &st)) style->bg_stops.push_back(st);
  }
  NormalizeGradientStops(&style->bg_stops);
  if (style->bg_stops.empty()) {
    style->bg_gradient = BgGradientKind::kNone;
    return false;
  }
  style->has_background = true;
  return true;
}

void ApplyBackgroundValue(const std::string& v, ComputedStyle* style) {
  ParseGradient(v, style);
  for (const std::string& layer : SplitTopLevel(v, ',')) {
    std::string t = Trim(layer);
    if (ToLowerAscii(t).find("gradient(") != std::string::npos) continue;
    uint32_t color = 0;
    if (ParseColor(t, &color)) {
      style->background_color = color;
      if ((color >> 24) != 0) style->has_background = true;
      continue;
    }
    for (const std::string& tok : SplitWhitespace(t)) {
      if (ParseColor(tok, &color)) {
        style->background_color = color;
        if ((color >> 24) != 0) style->has_background = true;
        break;
      }
    }
  }
  if (style->bg_gradient != BgGradientKind::kNone) style->has_background = true;
}

void MultiplyTransform(Transform* t, float a, float b, float c, float d, float e, float f) {
  const float na = t->a * a + t->c * b;
  const float nb = t->b * a + t->d * b;
  const float nc = t->a * c + t->c * d;
  const float nd = t->b * c + t->d * d;
  const float ne = t->a * e + t->c * f + t->e;
  const float nf = t->b * e + t->d * f + t->f;
  t->a = na;
  t->b = nb;
  t->c = nc;
  t->d = nd;
  t->e = ne;
  t->f = nf;
  t->none = false;
}

void ParseTransform(const std::string& value, Transform* t, float em, float rem, float vw,
                    float vh) {
  std::string v = ToLowerAscii(Trim(value));
  if (v == "none" || v.empty()) {
    *t = Transform{};
    return;
  }
  *t = Transform{};
  size_t i = 0;
  while (i < v.size()) {
    while (i < v.size() && (std::isspace(static_cast<unsigned char>(v[i])) || v[i] == ',')) ++i;
    if (i >= v.size()) break;
    size_t name_end = i;
    while (name_end < v.size() && std::isalpha(static_cast<unsigned char>(v[name_end]))) ++name_end;
    std::string fn = v.substr(i, name_end - i);
    size_t open = v.find('(', name_end);
    if (open == std::string::npos) break;
    size_t close = v.find(')', open);
    if (close == std::string::npos) break;
    std::string args = v.substr(open + 1, close - open - 1);
    std::vector<std::string> parts;
    {
      std::string cur;
      for (char ch : args) {
        if (ch == ',') {
          parts.push_back(Trim(cur));
          cur.clear();
        } else {
          cur += ch;
        }
      }
      if (!Trim(cur).empty()) parts.push_back(Trim(cur));
    }
    auto len_px = [&](const std::string& raw) {
      return ParseLength(raw).Resolve(0, 0, em, rem, vw, vh);
    };
    if (fn == "translate" || fn == "translate3d") {
      float x = parts.empty() ? 0 : len_px(parts[0]);
      float y = parts.size() < 2 ? 0 : len_px(parts[1]);
      MultiplyTransform(t, 1, 0, 0, 1, x, y);
    } else if (fn == "translatex") {
      MultiplyTransform(t, 1, 0, 0, 1, parts.empty() ? 0 : len_px(parts[0]), 0);
    } else if (fn == "translatey") {
      MultiplyTransform(t, 1, 0, 0, 1, 0, parts.empty() ? 0 : len_px(parts[0]));
    } else if (fn == "scale" || fn == "scalex" || fn == "scaley") {
      float sx = 1, sy = 1;
      if (fn == "scalex") sx = parts.empty() ? 1 : static_cast<float>(std::atof(parts[0].c_str()));
      else if (fn == "scaley")
        sy = parts.empty() ? 1 : static_cast<float>(std::atof(parts[0].c_str()));
      else {
        sx = parts.empty() ? 1 : static_cast<float>(std::atof(parts[0].c_str()));
        sy = parts.size() < 2 ? sx : static_cast<float>(std::atof(parts[1].c_str()));
      }
      MultiplyTransform(t, sx, 0, 0, sy, 0, 0);
    } else if (fn == "rotate") {
      float deg = 0;
      if (!parts.empty()) {
        std::string p = parts[0];
        if (p.size() > 3 && p.substr(p.size() - 3) == "rad") {
          deg = static_cast<float>(std::atof(p.c_str()) * 180.0 / 3.14159265358979323846);
        } else {
          if (p.size() > 3 && p.substr(p.size() - 3) == "deg") p = p.substr(0, p.size() - 3);
          deg = static_cast<float>(std::atof(p.c_str()));
        }
      }
      const float rad = deg * 3.14159265358979323846f / 180.0f;
      const float c = std::cos(rad);
      const float s = std::sin(rad);
      MultiplyTransform(t, c, s, -s, c, 0, 0);
    } else if (fn == "matrix" && parts.size() >= 6) {
      MultiplyTransform(t, static_cast<float>(std::atof(parts[0].c_str())),
                        static_cast<float>(std::atof(parts[1].c_str())),
                        static_cast<float>(std::atof(parts[2].c_str())),
                        static_cast<float>(std::atof(parts[3].c_str())),
                        static_cast<float>(std::atof(parts[4].c_str())),
                        static_cast<float>(std::atof(parts[5].c_str())));
    }
    i = close + 1;
  }
}

GridTrack ParseGridTrackToken(const std::string& raw) {
  std::string tok = ToLowerAscii(Trim(raw));
  GridTrack track;
  if (tok.empty()) return track;
  if (tok.rfind("minmax(", 0) == 0) {
    const size_t comma = tok.find(',');
    const std::string min_part =
        comma == std::string::npos ? std::string() : Trim(tok.substr(7, comma - 7));
    const std::string max_part =
        comma == std::string::npos ? std::string()
                                   : Trim(tok.substr(comma + 1, tok.size() - comma - 2));
    const GridTrack max_track = ParseGridTrackToken(max_part);
    track = max_track;
    track.has_minmax = true;
    if (min_part == "auto" || min_part.empty()) {
      track.min_px = 0.0f;
    } else {
      const GridTrack min_track = ParseGridTrackToken(min_part);
      if (min_track.sizing == GridTrackSizing::kPx) track.min_px = min_track.value;
    }
    if (max_track.sizing == GridTrackSizing::kPx) track.max_px = max_track.value;
    return track;
  }
  if (tok.size() >= 2 && tok.compare(tok.size() - 2, 2, "fr") == 0) {
    track.sizing = GridTrackSizing::kFr;
    track.value = static_cast<float>(std::atof(tok.c_str()));
    if (track.value <= 0.0f) track.value = 1.0f;
  } else if (tok.size() >= 2 && tok.compare(tok.size() - 2, 2, "px") == 0) {
    track.sizing = GridTrackSizing::kPx;
    track.value = static_cast<float>(std::atof(tok.c_str()));
  } else {
    track.sizing = GridTrackSizing::kAuto;
    track.value = 1.0f;
  }
  return track;
}

void ParseBoxShadow(const std::string& value, ComputedStyle* style) {
  std::string v = Trim(value);
  if (ToLowerAscii(v) == "none" || v.empty()) {
    style->has_box_shadow = false;
    return;
  }
  BoxShadow sh;
  std::vector<std::string> toks = SplitWhitespace(v);
  std::vector<float> nums;
  for (const std::string& tok : toks) {
    std::string tl = ToLowerAscii(tok);
    if (tl == "inset") {
      sh.inset = true;
      continue;
    }
    uint32_t color;
    if (ParseColor(tok, &color)) {
      sh.color = color;
      continue;
    }
    nums.push_back(ParseLength(tok).Resolve(0, 0, style->font_size));
  }
  if (!nums.empty()) sh.offset_x = nums[0];
  if (nums.size() > 1) sh.offset_y = nums[1];
  if (nums.size() > 2) sh.blur = std::max(0.0f, nums[2]);
  if (nums.size() > 3) sh.spread = nums[3];
  style->box_shadow = sh;
  style->has_box_shadow = true;
}

// A family list is scanned for a generic keyword or a well-known family
// name, in list order. There is no system font enumerator here, so
// `font-family: Consolas, monospace` and `font-family: monospace` have to
// reach the same face. Shared by `font-family` and the `font` shorthand.
void ApplyGenericFamily(const std::string& value, ComputedStyle* style) {
  std::string fv = ToLowerAscii(value);
  auto strip_quotes = [](std::string name) {
    while (!name.empty() && (name.front() == '"' || name.front() == '\'')) name.erase(0, 1);
    while (!name.empty() && (name.back() == '"' || name.back() == '\'')) name.pop_back();
    return Trim(name);
  };
  size_t first_comma = fv.find(',');
  style->font_family_preferred = strip_quotes(fv.substr(0, first_comma));
  for (size_t start = 0; start <= fv.size();) {
    size_t comma = fv.find(',', start);
    std::string name = strip_quotes(fv.substr(start, comma == std::string::npos ? std::string::npos
                                                                               : comma - start));
    bool matched = true;
    if (name == "monospace" || name == "consolas" || name == "courier" ||
        name == "courier new" || name == "menlo" || name == "monaco" ||
        name == "ui-monospace" || name == "sf mono" || name == "dejavu sans mono" ||
        name == "liberation mono" || name == "lucida console" || name == "cascadia code" ||
        name == "cascadia mono" || name == "source code pro" || name == "roboto mono" ||
        name == "fira code" || name == "jetbrains mono") {
      style->font_family = GenericFontFamily::kMonospace;
    } else if (name == "serif" || name == "times" || name == "times new roman" ||
               name == "georgia" || name == "garamond" || name == "cambria" ||
               name == "palatino" || name == "book antiqua" || name == "ui-serif" ||
               name == "dejavu serif" || name == "liberation serif") {
      style->font_family = GenericFontFamily::kSerif;
    } else if (name == "sans-serif" || name == "system-ui" || name == "ui-sans-serif" ||
               name == "arial" || name == "helvetica" || name == "segoe ui" ||
               name == "roboto" || name == "verdana" || name == "tahoma" ||
               name == "calibri" || name == "inter" || name == "-apple-system") {
      style->font_family = GenericFontFamily::kSansSerif;
    } else {
      matched = false;
    }
    if (matched || comma == std::string::npos) break;
    start = comma + 1;
  }
}

// Applies one already-cascade-ordered declaration map onto `style` --
// shared by both the selector-matched rules and the final inline
// style="" pass (ComputeStyle just calls this twice, in order).
void ApplyDeclarations(const std::unordered_map<std::string, std::string>& decls,
                       ComputedStyle* style, float em_base, float rem_base, float viewport_w,
                       float viewport_h) {
  auto get = [&](const char* prop, std::string* out) {
    auto it = decls.find(prop);
    if (it == decls.end()) return false;
    *out = it->second;
    return true;
  };
  auto len = [&](const std::string& raw) {
    Length l = ParseLength(raw);
    // em/rem resolve for inheritance. vw/vh stay as units so RmlUi can
    // parse them natively (Unit::VW / Unit::VH). Blinker layout still
    // calls Length::Resolve at paint time.
    if (l.unit == Length::Unit::kEm || l.unit == Length::Unit::kRem) {
      Length px;
      px.unit = Length::Unit::kPx;
      px.value = l.Resolve(0, 0, style->font_size, rem_base, viewport_w, viewport_h);
      return px;
    }
    return l;
  };
  std::string v;

  if (get("font", &v)) {
    // CSS font shorthand: [style] [weight] size[/line-height] family
    std::string size_tok;
    std::string rest = Trim(v);
    std::vector<std::string> parts = SplitWhitespace(rest);
    for (size_t pi = 0; pi < parts.size(); ++pi) {
      std::string p = ToLowerAscii(parts[pi]);
      if (p == "italic" || p == "oblique") {
        style->font_italic = true;
        continue;
      }
      if (p == "bold" || p == "bolder") {
        style->font_bold = true;
        continue;
      }
      if (p == "normal" || p == "lighter" || p == "small-caps") continue;
      size_t slash = parts[pi].find('/');
      std::string size_s = slash == std::string::npos ? parts[pi] : parts[pi].substr(0, slash);
      Length sl = ParseLength(size_s);
      style->font_size = sl.Resolve(em_base, em_base, em_base, rem_base, viewport_w, viewport_h);
      if (style->font_size <= 0.0f) style->font_size = em_base;
      if (slash != std::string::npos) {
        std::string lh = parts[pi].substr(slash + 1);
        Length ll = ParseLength(lh);
        float px = ll.Resolve(style->font_size, style->font_size, style->font_size, rem_base,
                              viewport_w, viewport_h);
        style->line_height = style->font_size > 0 ? px / style->font_size : 1.35f;
      }
      if (pi + 1 < parts.size()) {
        std::string family;
        for (size_t fi = pi + 1; fi < parts.size(); ++fi) {
          if (!family.empty()) family += ' ';
          family += parts[fi];
        }
        ApplyGenericFamily(family, style);
      }
      break;
    }
  }

  if (get("border", &v)) {
    std::string lv = ToLowerAscii(Trim(v));
    if (lv == "none" || lv == "0") {
      style->border_top = style->border_right = style->border_bottom = style->border_left = 0;
    } else {
      for (const std::string& tok : SplitWhitespace(v)) {
        uint32_t color;
        if (ParseColor(tok, &color)) {
          style->border_color = color;
          continue;
        }
        std::string t = ToLowerAscii(tok);
        if (t == "solid" || t == "none" || t == "dashed" || t == "dotted" || t == "hidden" ||
            t == "double") {
          if (t == "none" || t == "hidden") {
            style->border_top = style->border_right = style->border_bottom = style->border_left = 0;
          }
          continue;
        }
        float w = ParseLength(tok).Resolve(0, 0, style->font_size, rem_base, viewport_w, viewport_h);
        style->border_top = style->border_right = style->border_bottom = style->border_left = w;
      }
    }
  }

  if (get("display", &v)) {
    std::string dv = ToLowerAscii(Trim(v));
    if (dv == "block") style->display = DisplayType::kBlock;
    else if (dv == "inline") style->display = DisplayType::kInline;
    else if (dv == "inline-block") style->display = DisplayType::kInlineBlock;
    else if (dv == "flex") style->display = DisplayType::kFlex;
    else if (dv == "grid") style->display = DisplayType::kGrid;
    else if (dv == "none") style->display = DisplayType::kNone;
    else if (dv == "table") style->display = DisplayType::kTable;
    else if (dv == "table-row") style->display = DisplayType::kTableRow;
    else if (dv == "table-cell") style->display = DisplayType::kTableCell;
    else if (dv == "table-row-group" || dv == "table-header-group" ||
             dv == "table-footer-group")
      style->display = DisplayType::kTableRowGroup;
    else if (dv == "list-item") style->display = DisplayType::kListItem;
  }
  if (get("position", &v)) {
    std::string pv = ToLowerAscii(Trim(v));
    if (pv == "relative") style->position = PositionType::kRelative;
    else if (pv == "absolute") style->position = PositionType::kAbsolute;
    else if (pv == "fixed") style->position = PositionType::kFixed;
    else if (pv == "sticky") style->position = PositionType::kSticky;
    else style->position = PositionType::kStatic;
  }
  if (get("float", &v)) {
    std::string fv = ToLowerAscii(Trim(v));
    if (fv == "left") style->float_type = FloatType::kLeft;
    else if (fv == "right") style->float_type = FloatType::kRight;
    else style->float_type = FloatType::kNone;
  }
  if (get("clear", &v)) {
    std::string cv = ToLowerAscii(Trim(v));
    if (cv == "left") style->clear = ClearType::kLeft;
    else if (cv == "right") style->clear = ClearType::kRight;
    else if (cv == "both") style->clear = ClearType::kBoth;
    else style->clear = ClearType::kNone;
  }
  if (get("width", &v)) style->width = len(v);
  if (get("height", &v)) style->height = len(v);
  if (get("min-width", &v)) style->min_width = len(v);
  if (get("max-width", &v)) style->max_width = len(v);
  if (get("min-height", &v)) style->min_height = len(v);
  if (get("max-height", &v)) style->max_height = len(v);
  if (get("top", &v)) style->top = len(v);
  if (get("right", &v)) style->right = len(v);
  if (get("bottom", &v)) style->bottom = len(v);
  if (get("left", &v)) style->left = len(v);
  if (get("z-index", &v)) {
    std::string zv = ToLowerAscii(Trim(v));
    if (zv == "auto") {
      style->z_index_auto = true;
      style->z_index = 0;
    } else {
      style->z_index_auto = false;
      style->z_index = std::atoi(zv.c_str());
    }
  }

  if (get("margin", &v)) {
    std::vector<std::string> parts = SplitWhitespace(v);
    if (parts.size() == 1) {
      style->margin_top = style->margin_right = style->margin_bottom = style->margin_left =
          len(parts[0]);
    } else if (parts.size() == 2) {
      style->margin_top = style->margin_bottom = len(parts[0]);
      style->margin_left = style->margin_right = len(parts[1]);
    } else if (parts.size() == 3) {
      style->margin_top = len(parts[0]);
      style->margin_left = style->margin_right = len(parts[1]);
      style->margin_bottom = len(parts[2]);
    } else if (parts.size() >= 4) {
      style->margin_top = len(parts[0]);
      style->margin_right = len(parts[1]);
      style->margin_bottom = len(parts[2]);
      style->margin_left = len(parts[3]);
    }
  }
  if (get("margin-top", &v)) style->margin_top = len(v);
  if (get("margin-right", &v)) style->margin_right = len(v);
  if (get("margin-bottom", &v)) style->margin_bottom = len(v);
  if (get("margin-left", &v)) style->margin_left = len(v);

  if (get("padding", &v)) {
    std::vector<std::string> parts = SplitWhitespace(v);
    if (parts.size() == 1) {
      style->padding_top = style->padding_right = style->padding_bottom = style->padding_left =
          len(parts[0]);
    } else if (parts.size() == 2) {
      style->padding_top = style->padding_bottom = len(parts[0]);
      style->padding_left = style->padding_right = len(parts[1]);
    } else if (parts.size() == 3) {
      style->padding_top = len(parts[0]);
      style->padding_left = style->padding_right = len(parts[1]);
      style->padding_bottom = len(parts[2]);
    } else if (parts.size() >= 4) {
      style->padding_top = len(parts[0]);
      style->padding_right = len(parts[1]);
      style->padding_bottom = len(parts[2]);
      style->padding_left = len(parts[3]);
    }
  }
  if (get("padding-top", &v)) style->padding_top = len(v);
  if (get("padding-right", &v)) style->padding_right = len(v);
  if (get("padding-bottom", &v)) style->padding_bottom = len(v);
  if (get("padding-left", &v)) style->padding_left = len(v);

  // border shorthand: "<width> <style> <color>", any subset/order of the
  // three -- style keyword (solid/dashed/...) is parsed (so it doesn't
  // get mistaken for a color) but ignored, since painting always strokes
  // a solid line (see paint.cc).
  auto apply_border = [&](const std::string& value, float* w_top, float* w_right, float* w_bottom,
                          float* w_left) {
    float width = 1.0f;
    bool have_width = false;
    for (const std::string& tok : SplitWhitespace(value)) {
      uint32_t color;
      if (ParseColor(tok, &color)) {
        style->border_color = color;
      } else if (tok == "solid" || tok == "dashed" || tok == "dotted" || tok == "none") {
        if (tok == "none") width = 0.0f, have_width = true;
      } else {
        width = ParsePx(tok);
        have_width = true;
      }
    }
    if (have_width) *w_top = *w_right = *w_bottom = *w_left = width;
  };
  if (get("border", &v)) {
    apply_border(v, &style->border_top, &style->border_right, &style->border_bottom,
                 &style->border_left);
  }
  if (get("border-width", &v)) {
    float w = ParsePx(v);
    style->border_top = style->border_right = style->border_bottom = style->border_left = w;
  }
  if (get("border-color", &v)) {
    uint32_t color;
    if (ParseColor(v, &color)) style->border_color = color;
  }
  if (get("border-top-width", &v)) style->border_top = ParsePx(v);
  if (get("border-right-width", &v)) style->border_right = ParsePx(v);
  if (get("border-bottom-width", &v)) style->border_bottom = ParsePx(v);
  if (get("border-left-width", &v)) style->border_left = ParsePx(v);

  auto apply_radius = [&](const std::string& value) {
    // CSS allows "10px / 5px" elliptical radii; Blinker paints circular
    // corners, so only the first (horizontal) list is used.
    std::string horiz = value;
    size_t slash = horiz.find('/');
    if (slash != std::string::npos) horiz = horiz.substr(0, slash);
    std::vector<std::string> parts;
    for (const std::string& tok : SplitWhitespace(horiz)) parts.push_back(tok);
    if (parts.empty()) return;
    float a = ParsePx(parts[0]);
    float b = parts.size() > 1 ? ParsePx(parts[1]) : a;
    float c = parts.size() > 2 ? ParsePx(parts[2]) : a;
    float d = parts.size() > 3 ? ParsePx(parts[3]) : b;
    if (parts.size() == 1) {
      style->radius_tl = style->radius_tr = style->radius_br = style->radius_bl = a;
    } else if (parts.size() == 2) {
      style->radius_tl = style->radius_br = a;
      style->radius_tr = style->radius_bl = b;
    } else if (parts.size() == 3) {
      style->radius_tl = a;
      style->radius_tr = style->radius_bl = b;
      style->radius_br = c;
    } else {
      style->radius_tl = a;
      style->radius_tr = b;
      style->radius_br = c;
      style->radius_bl = d;
    }
  };
  if (get("border-radius", &v)) apply_radius(v);
  if (get("border-top-left-radius", &v)) style->radius_tl = ParsePx(v);
  if (get("border-top-right-radius", &v)) style->radius_tr = ParsePx(v);
  if (get("border-bottom-right-radius", &v)) style->radius_br = ParsePx(v);
  if (get("border-bottom-left-radius", &v)) style->radius_bl = ParsePx(v);

  if (get("background", &v)) ApplyBackgroundValue(v, style);
  if (get("background-image", &v)) ParseGradient(v, style);
  if (get("background-color", &v)) {
    uint32_t color;
    if (ParseColor(Trim(v), &color)) {
      style->background_color = color;
      if ((color >> 24) != 0 || style->bg_gradient != BgGradientKind::kNone)
        style->has_background = true;
    }
  }
  if (get("color", &v)) {
    uint32_t color;
    if (ParseColor(v, &color)) style->color = color;
  }
  if (get("font-size", &v)) {
    Length l = ParseLength(v);
    style->font_size = l.Resolve(em_base, em_base, em_base, rem_base, viewport_w, viewport_h);
    if (style->font_size <= 0.0f) style->font_size = em_base;
  }
  if (get("line-height", &v)) {
    std::string lv = Trim(v);
    if (lv != "normal") {
      if (lv.find("px") != std::string::npos || lv.find("em") != std::string::npos ||
          lv.find('%') != std::string::npos) {
        float px = ParseLength(lv).Resolve(style->font_size, style->font_size, style->font_size,
                                           rem_base, viewport_w, viewport_h);
        style->line_height = style->font_size > 0 ? px / style->font_size : 1.35f;
      } else {
        style->line_height = static_cast<float>(std::atof(lv.c_str()));
      }
    }
  }
  if (get("opacity", &v)) {
    style->opacity = static_cast<float>(std::atof(Trim(v).c_str()));
    if (style->opacity < 0) style->opacity = 0;
    if (style->opacity > 1) style->opacity = 1;
  }
  if (get("visibility", &v)) {
    std::string vv = ToLowerAscii(Trim(v));
    if (vv == "hidden") style->visibility = Visibility::kHidden;
    else if (vv == "collapse") style->visibility = Visibility::kCollapse;
    else style->visibility = Visibility::kVisible;
  }
  if (get("direction", &v)) {
    style->direction = (ToLowerAscii(Trim(v)) == "rtl") ? Direction::kRtl : Direction::kLtr;
  }
  if (get("outline", &v)) {
    std::string ov = ToLowerAscii(Trim(v));
    if (ov == "none" || ov == "0") {
      style->outline_width = 0;
    } else {
      for (const std::string& tok : SplitWhitespace(v)) {
        uint32_t color;
        if (ParseColor(tok, &color)) {
          style->outline_color = color;
          continue;
        }
        std::string t = ToLowerAscii(tok);
        if (t == "solid" || t == "none" || t == "dashed" || t == "dotted") {
          if (t == "none") style->outline_width = 0;
          continue;
        }
        float w = ParseLength(tok).Resolve(0, 0, style->font_size, rem_base, viewport_w, viewport_h);
        if (w > 0) style->outline_width = w;
      }
    }
  }
  if (get("outline-width", &v)) {
    style->outline_width =
        ParseLength(v).Resolve(0, 0, style->font_size, rem_base, viewport_w, viewport_h);
  }
  if (get("outline-color", &v)) {
    uint32_t color;
    if (ParseColor(v, &color)) style->outline_color = color;
  }
  if (get("box-shadow", &v)) ParseBoxShadow(v, style);
  if (get("transform", &v)) {
    ParseTransform(v, &style->transform, style->font_size, rem_base, viewport_w, viewport_h);
  }
  if (get("font-weight", &v)) {
    std::string fv = ToLowerAscii(Trim(v));
    if (fv == "bold" || fv == "bolder") {
      style->font_bold = true;
    } else if (fv == "normal" || fv == "lighter") {
      style->font_bold = false;
    } else {
      int n = std::atoi(fv.c_str());
      if (n > 0) style->font_bold = n >= 700;
    }
  }
  if (get("font-style", &v)) {
    std::string fv = ToLowerAscii(Trim(v));
    style->font_italic = (fv == "italic" || fv == "oblique");
  }
  if (get("font-family", &v)) ApplyGenericFamily(v, style);
  if (get("overflow", &v)) {
    const Overflow o = ParseOverflowKeyword(ToLowerAscii(Trim(v)));
    style->overflow_x = style->overflow_y = o;
  }
  if (get("overflow-x", &v)) style->overflow_x = ParseOverflowKeyword(ToLowerAscii(Trim(v)));
  if (get("overflow-y", &v)) style->overflow_y = ParseOverflowKeyword(ToLowerAscii(Trim(v)));
  NormalizeOverflowAxes(&style->overflow_x, &style->overflow_y);
  if (get("content", &v)) {
    std::string cv = Trim(v);
    const std::string low = ToLowerAscii(cv);
    if (low == "none") {
      style->has_generated_content = false;
      style->content_is_attr = false;
      style->content_is_url = false;
      style->content_is_counter = false;
      style->generated_content.clear();
    } else if (low.size() > 5 && low.compare(0, 5, "attr(") == 0 && cv.back() == ')') {
      style->has_generated_content = true;
      style->content_is_attr = true;
      style->content_is_url = false;
      style->content_is_counter = false;
      style->generated_content = Trim(cv.substr(5, cv.size() - 6));
    } else if (low.size() > 8 && low.compare(0, 8, "counter(") == 0 && cv.back() == ')') {
      style->has_generated_content = true;
      style->content_is_counter = true;
      style->content_is_attr = false;
      style->content_is_url = false;
      style->generated_content = Trim(cv.substr(8, cv.size() - 9));
    } else if (low.size() > 4 && low.compare(0, 4, "url(") == 0 && cv.back() == ')') {
      std::string raw = Trim(cv.substr(4, cv.size() - 5));
      if (raw.size() >= 2 &&
          ((raw.front() == '"' && raw.back() == '"') || (raw.front() == '\'' && raw.back() == '\''))) {
        raw = raw.substr(1, raw.size() - 2);
      }
      style->has_generated_content = true;
      style->content_is_url = true;
      style->content_is_attr = false;
      style->content_is_counter = false;
      style->generated_content = raw;
    } else if (cv.size() >= 2 &&
               ((cv.front() == '"' && cv.back() == '"') || (cv.front() == '\'' && cv.back() == '\''))) {
      style->has_generated_content = true;
      style->content_is_attr = false;
      style->content_is_url = false;
      style->content_is_counter = false;
      style->generated_content = cv.substr(1, cv.size() - 2);
    } else if (!cv.empty()) {
      style->has_generated_content = true;
      style->content_is_attr = false;
      style->content_is_url = false;
      style->content_is_counter = false;
      style->generated_content = cv;
    }
  }
  if (get("counter-reset", &v)) {
    const std::vector<std::string> parts = SplitWhitespace(Trim(v));
    if (!parts.empty()) {
      style->counter_reset_name = parts[0];
      style->counter_reset_value = parts.size() > 1 ? std::atoi(parts[1].c_str()) : 0;
    }
  }
  if (get("counter-increment", &v)) {
    const std::vector<std::string> parts = SplitWhitespace(Trim(v));
    if (!parts.empty()) {
      style->counter_increment_name = parts[0];
      style->counter_increment_value = parts.size() > 1 ? std::atoi(parts[1].c_str()) : 1;
    }
  }
  if (get("text-overflow", &v)) {
    if (ToLowerAscii(Trim(v)).find("ellipsis") != std::string::npos) {
      style->text_overflow_ellipsis = true;
    }
  }
  // text-decoration is a shorthand; the line keyword can sit anywhere in
  // it (`underline`, `red wavy underline`), so scan rather than compare.
  for (const char* prop : {"text-decoration", "text-decoration-line"}) {
    if (!get(prop, &v)) continue;
    std::string dv = ToLowerAscii(Trim(v));
    if (dv.find("none") != std::string::npos) {
      style->text_underline = false;
      style->text_line_through = false;
      continue;
    }
    if (dv.find("underline") != std::string::npos) style->text_underline = true;
    if (dv.find("line-through") != std::string::npos) style->text_line_through = true;
  }
  if (get("white-space", &v)) {
    std::string wv = ToLowerAscii(Trim(v));
    if (wv == "nowrap") style->white_space = WhiteSpace::kNowrap;
    else if (wv == "pre") style->white_space = WhiteSpace::kPre;
    else if (wv == "pre-wrap") style->white_space = WhiteSpace::kPrewrap;
    else if (wv == "pre-line") style->white_space = WhiteSpace::kPreline;
    else style->white_space = WhiteSpace::kNormal;
  }
  if (get("vertical-align", &v)) {
    std::string av = ToLowerAscii(Trim(v));
    if (av == "middle") style->vertical_align = VerticalAlign::kMiddle;
    else if (av == "top" || av == "text-top") style->vertical_align = VerticalAlign::kTop;
    else if (av == "bottom" || av == "text-bottom") style->vertical_align = VerticalAlign::kBottom;
    else if (av == "sub") style->vertical_align = VerticalAlign::kSub;
    else if (av == "super") style->vertical_align = VerticalAlign::kSuper;
    else style->vertical_align = VerticalAlign::kBaseline;
  }
  if (get("text-align", &v)) {
    std::string tv = ToLowerAscii(Trim(v));
    if (tv == "center") style->text_align = TextAlign::kCenter;
    else if (tv == "right") style->text_align = TextAlign::kRight;
    else style->text_align = TextAlign::kLeft;
  }
  if (get("flex-direction", &v)) {
    style->flex_direction =
        (ToLowerAscii(Trim(v)) == "column") ? FlexDirection::kColumn : FlexDirection::kRow;
  }
  if (get("justify-content", &v)) {
    std::string jv = ToLowerAscii(Trim(v));
    if (jv == "flex-end" || jv == "end") style->justify_content = JustifyContent::kFlexEnd;
    else if (jv == "center") style->justify_content = JustifyContent::kCenter;
    else if (jv == "space-between") style->justify_content = JustifyContent::kSpaceBetween;
    else if (jv == "space-around") style->justify_content = JustifyContent::kSpaceAround;
    else style->justify_content = JustifyContent::kFlexStart;
  }
  if (get("align-items", &v)) {
    std::string av = ToLowerAscii(Trim(v));
    if (av == "flex-end" || av == "end") style->align_items = AlignItems::kFlexEnd;
    else if (av == "center") style->align_items = AlignItems::kCenter;
    else if (av == "flex-start" || av == "start") style->align_items = AlignItems::kFlexStart;
    else style->align_items = AlignItems::kStretch;
  }
  if (get("gap", &v))
    style->gap = ParseLength(v).Resolve(style->font_size, 0, style->font_size, rem_base, viewport_w,
                                        viewport_h);
  if (get("flex-grow", &v)) style->flex_grow = static_cast<float>(std::atof(Trim(v).c_str()));
  if (get("flex-shrink", &v)) style->flex_shrink = static_cast<float>(std::atof(Trim(v).c_str()));
  if (get("flex-basis", &v)) style->flex_basis = len(v);
  if (get("flex-wrap", &v)) {
    style->flex_wrap = (ToLowerAscii(Trim(v)) == "wrap") ? FlexWrap::kWrap : FlexWrap::kNowrap;
  }
  if (get("flex", &v)) {
    std::vector<std::string> parts = SplitWhitespace(v);
    if (parts.size() == 1) {
      std::string p = ToLowerAscii(parts[0]);
      if (p == "auto") {
        style->flex_grow = 1;
        style->flex_shrink = 1;
        style->flex_basis = Length{};  // auto
      } else if (p == "none") {
        style->flex_grow = 0;
        style->flex_shrink = 0;
        style->flex_basis = Length{};
        style->flex_basis.unit = Length::Unit::kAuto;
      } else if (p.find("px") != std::string::npos || p.find('%') != std::string::npos ||
                 p.find("em") != std::string::npos || p == "auto") {
        style->flex_basis = len(parts[0]);
      } else {
        style->flex_grow = static_cast<float>(std::atof(p.c_str()));
      }
    } else if (parts.size() >= 2) {
      style->flex_grow = static_cast<float>(std::atof(parts[0].c_str()));
      style->flex_shrink = static_cast<float>(std::atof(parts[1].c_str()));
      if (parts.size() >= 3) style->flex_basis = len(parts[2]);
    }
  }
  if (get("grid-template-columns", &v)) {
    style->grid_tracks.clear();
    std::string lv = ToLowerAscii(Trim(v));
    auto push_track = [&](const std::string& raw) {
      GridTrack track = ParseGridTrackToken(raw);
      if (Trim(raw).empty()) return;
      style->grid_tracks.push_back(track);
    };
    if (lv.rfind("repeat(", 0) == 0) {
      const size_t comma = lv.find(',');
      if (comma != std::string::npos) {
        const int count = std::max(1, std::atoi(lv.c_str() + 7));
        const std::string inner = Trim(lv.substr(comma + 1, lv.size() - comma - 2));
        for (int i = 0; i < count; ++i) push_track(inner);
      }
    } else {
      for (const std::string& tok : SplitTopLevelWhitespace(v)) push_track(tok);
    }
    style->grid_columns = std::max(1, static_cast<int>(style->grid_tracks.size()));
  }
  if (get("list-style-type", &v) || get("list-style", &v)) {
    std::string lv = ToLowerAscii(Trim(v));
    if (lv.find("none") != std::string::npos) style->list_style_type = ListStyleType::kNone;
    else if (lv.find("decimal") != std::string::npos)
      style->list_style_type = ListStyleType::kDecimal;
    else if (lv.find("disc") != std::string::npos || lv.find("circle") != std::string::npos ||
             lv.find("square") != std::string::npos)
      style->list_style_type = ListStyleType::kDisc;
  }
}

DisplayType DefaultDisplayFor(const std::string& tag) {
  static const std::unordered_map<std::string, DisplayType> kDefaults = {
      {"html", DisplayType::kBlock},   {"body", DisplayType::kBlock},
      {"div", DisplayType::kBlock},    {"p", DisplayType::kBlock},
      {"h1", DisplayType::kBlock},     {"h2", DisplayType::kBlock},
      {"h3", DisplayType::kBlock},     {"h4", DisplayType::kBlock},
      {"h5", DisplayType::kBlock},     {"h6", DisplayType::kBlock},
      {"li", DisplayType::kListItem},  {"ul", DisplayType::kBlock},
      {"ol", DisplayType::kBlock},     {"section", DisplayType::kBlock},
      {"article", DisplayType::kBlock}, {"header", DisplayType::kBlock},
      {"footer", DisplayType::kBlock}, {"nav", DisplayType::kBlock},
      {"main", DisplayType::kBlock},   {"table", DisplayType::kTable},
      {"thead", DisplayType::kTableRowGroup}, {"tbody", DisplayType::kTableRowGroup},
      {"tfoot", DisplayType::kTableRowGroup},
      {"tr", DisplayType::kTableRow},  {"td", DisplayType::kTableCell},
      {"th", DisplayType::kTableCell}, {"blockquote", DisplayType::kBlock},
      {"pre", DisplayType::kBlock},    {"form", DisplayType::kBlock},
      {"center", DisplayType::kBlock},
      {"figure", DisplayType::kBlock}, {"figcaption", DisplayType::kBlock},
      {"address", DisplayType::kBlock}, {"details", DisplayType::kBlock},
      {"summary", DisplayType::kBlock}, {"dialog", DisplayType::kBlock},
      {"fieldset", DisplayType::kBlock}, {"button", DisplayType::kInlineBlock},
      {"img", DisplayType::kInlineBlock}, {"input", DisplayType::kInlineBlock},
      {"head", DisplayType::kNone},    {"script", DisplayType::kNone},
      {"style", DisplayType::kNone},   {"title", DisplayType::kNone},
  };
  auto it = kDefaults.find(tag);
  return it == kDefaults.end() ? DisplayType::kInline : it->second;
}

// Per-tag initial-value overrides beyond `display` -- the closest thing
// this engine has to a real user-agent stylesheet (real browsers ship one
// too, e.g. `h1 { font-size: 2em; font-weight: bold; }`).
void ApplyUserAgentDefaults(const std::string& tag, ComputedStyle* style) {
  static const std::unordered_map<std::string, float> kHeadingSizes = {
      {"h1", 28.0f}, {"h2", 24.0f}, {"h3", 20.0f}, {"h4", 18.0f}, {"h5", 16.0f}, {"h6", 15.0f},
  };
  auto it = kHeadingSizes.find(tag);
  if (it != kHeadingSizes.end()) {
    style->font_size = it->second;
    style->font_bold = true;
  }
  if (tag == "button") {
    style->padding_top = style->padding_bottom = Length{Length::Unit::kPx, 6.0f};
    style->padding_left = style->padding_right = Length{Length::Unit::kPx, 12.0f};
    style->has_background = true;
    style->background_color = 0xFFE0E0E0u;
    style->border_top = style->border_right = style->border_bottom = style->border_left = 1.0f;
    style->border_color = 0xFF888888u;
    style->flex_shrink = 0.0f;
    style->min_width = Length{Length::Unit::kPx, 36.0f};
  }
  if (tag == "input" || tag == "textarea") {
    style->padding_top = style->padding_bottom = Length{Length::Unit::kPx, 6.0f};
    style->padding_left = style->padding_right = Length{Length::Unit::kPx, 8.0f};
    style->has_background = true;
    style->background_color = 0xFFFFFFFFu;
    style->border_top = style->border_right = style->border_bottom = style->border_left = 1.0f;
    style->border_color = 0xFF666666u;
    style->min_height = Length{Length::Unit::kPx, 28.0f};
  }
  if (tag == "a") {
    style->color = 0xFF2255CCu;
    style->text_underline = true;
  }
  // The inline half of the UA sheet. Before the inline formatting context
  // existed these could not have been honored: inline runs were flattened
  // into the enclosing block's single style, so bold/italic/underline had
  // nowhere to live.
  if (tag == "b" || tag == "strong") style->font_bold = true;
  if (tag == "i" || tag == "em" || tag == "cite" || tag == "var" || tag == "address" ||
      tag == "dfn") {
    style->font_italic = true;
  }
  if (tag == "u" || tag == "ins") style->text_underline = true;
  if (tag == "s" || tag == "strike" || tag == "del") style->text_line_through = true;
  if (tag == "small") style->font_size *= 0.8f;
  if (tag == "big") style->font_size *= 1.2f;
  if (tag == "sub") {
    style->font_size *= 0.75f;
    style->vertical_align = VerticalAlign::kSub;
  }
  if (tag == "sup") {
    style->font_size *= 0.75f;
    style->vertical_align = VerticalAlign::kSuper;
  }
  if (tag == "mark") {
    style->has_background = true;
    style->background_color = 0xFFFFF176u;
    style->color = 0xFF111111u;
  }
  if (tag == "pre" || tag == "textarea") style->white_space = WhiteSpace::kPre;
  if (tag == "nobr") style->white_space = WhiteSpace::kNowrap;
  // The UA sheet's monospace set. Its whole point is that column alignment
  // in code survives, which a proportional face silently destroys.
  if (tag == "pre" || tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp" ||
      tag == "textarea") {
    style->font_family = GenericFontFamily::kMonospace;
    style->font_size *= 0.95f;
  }
  if (tag == "body") {
    style->padding_top = style->padding_right = style->padding_bottom = style->padding_left =
        Length{Length::Unit::kPx, 16.0f};
  }
  if (tag == "th") {
    style->font_bold = true;
  }
  if (tag == "ul") {
    style->list_style_type = ListStyleType::kDisc;
    if (style->padding_left.is_auto()) style->padding_left = Length{Length::Unit::kPx, 24.0f};
  }
  if (tag == "ol") {
    style->list_style_type = ListStyleType::kDecimal;
    if (style->padding_left.is_auto()) style->padding_left = Length{Length::Unit::kPx, 24.0f};
  }
}

}  // namespace

ComputedStyle ComputeStyle(const Node& node, const Stylesheet& sheet,
                           const ComputedStyle* parent_style) {
  ComputedStyle style;
  if (parent_style) {
    style.color = parent_style->color;
    style.font_size = parent_style->font_size;
    style.font_bold = parent_style->font_bold;
    style.font_italic = parent_style->font_italic;
    style.font_family = parent_style->font_family;
    if (!parent_style->font_family_preferred.empty()) {
      style.font_family_preferred = parent_style->font_family_preferred;
    }
    style.text_underline = parent_style->text_underline;
    style.text_line_through = parent_style->text_line_through;
    style.white_space = parent_style->white_space;
    style.text_align = parent_style->text_align;
    style.line_height = parent_style->line_height;
    style.list_style_type = parent_style->list_style_type;
    style.visibility = parent_style->visibility;
    style.direction = parent_style->direction;
  }
  style.display = DefaultDisplayFor(node.tag_name);
  ApplyUserAgentDefaults(node.tag_name, &style);
  if (ToLowerAscii(node.GetAttribute("dir")) == "rtl") style.direction = Direction::kRtl;

  const float em_base = parent_style ? parent_style->font_size : 16.0f;
  const float rem_base = 16.0f;

  struct Candidate {
    std::tuple<int, int, int> specificity;
    int source_order;
    const std::unordered_map<std::string, std::string>* declarations;
    const std::unordered_map<std::string, std::string>* important;
  };
  std::vector<Candidate> candidates;
  for (const StyleRule& rule : sheet.rules) {
    for (const std::string& selector : rule.selectors) {
      ComplexSelector sel = ParseComplexSelector(selector);
      if (!sel.compounds.empty() && sel.compounds.back().pseudo != PseudoElement::kNone) continue;
      if (SelectorMatches(node, selector)) {
        candidates.push_back(
            {Specificity(selector), rule.source_order, &rule.declarations, &rule.important});
        break;
      }
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.specificity != b.specificity) return a.specificity < b.specificity;
    return a.source_order < b.source_order;
  });
  const float vw = sheet.viewport_w > 1.0f ? sheet.viewport_w : 800.0f;
  const float vh = sheet.viewport_h > 1.0f ? sheet.viewport_h : 600.0f;
  for (const Candidate& c : candidates)
    ApplyDeclarations(*c.declarations, &style, em_base, rem_base, vw, vh);
  for (const Candidate& c : candidates)
    ApplyDeclarations(*c.important, &style, em_base, rem_base, vw, vh);

  std::string inline_style = node.GetAttribute("style");
  if (!inline_style.empty()) {
    DeclarationBlock block = ParseDeclarationBlock(inline_style);
    ApplyDeclarations(block.normal, &style, em_base, rem_base, vw, vh);
    ApplyDeclarations(block.important, &style, em_base, rem_base, vw, vh);
  }

  return style;
}

std::string ResolveGeneratedContent(const Node& node, const ComputedStyle& style,
                                    const HtmlDocument* document,
                                    const std::unordered_map<std::string, int>* counters) {
  if (!style.has_generated_content) return {};
  if (style.content_is_attr) return node.GetAttribute(style.generated_content);
  if (style.content_is_counter) {
    if (!counters) return "0";
    auto it = counters->find(style.generated_content);
    return std::to_string(it == counters->end() ? 0 : it->second);
  }
  if (style.content_is_url) return {};
  (void)document;
  return style.generated_content;
}

const DecodedImage* ResolvePseudoContentImage(const Node& node, const ComputedStyle& style,
                                              const HtmlDocument& document) {
  if (!style.has_generated_content || !style.content_is_url) return nullptr;
  const std::string url =
      document.base_url.empty() ? style.generated_content
                                : ResolveUrl(document.base_url, style.generated_content);
  auto it = document.images_by_url.find(url);
  if (it != document.images_by_url.end() && it->second.width > 0 && it->second.height > 0) {
    return &it->second;
  }
  (void)node;
  return nullptr;
}

ComputedStyle ComputePseudoStyle(const Node& node, PseudoElement pseudo, const Stylesheet& sheet,
                                 const ComputedStyle* element_style) {
  ComputedStyle style;
  if (element_style) {
    style.color = element_style->color;
    style.font_size = element_style->font_size;
    style.font_bold = element_style->font_bold;
    style.font_italic = element_style->font_italic;
    style.font_family = element_style->font_family;
    if (!element_style->font_family_preferred.empty()) {
      style.font_family_preferred = element_style->font_family_preferred;
    }
    style.text_underline = element_style->text_underline;
    style.text_line_through = element_style->text_line_through;
    style.white_space = element_style->white_space;
    style.text_align = element_style->text_align;
    style.line_height = element_style->line_height;
    style.visibility = element_style->visibility;
    style.direction = element_style->direction;
  }
  style.display = DisplayType::kInline;

  const float em_base = element_style ? element_style->font_size : 16.0f;
  const float rem_base = 16.0f;

  struct Candidate {
    std::tuple<int, int, int> specificity;
    int source_order;
    const std::unordered_map<std::string, std::string>* declarations;
    const std::unordered_map<std::string, std::string>* important;
  };
  std::vector<Candidate> candidates;
  for (const StyleRule& rule : sheet.rules) {
    for (const std::string& selector : rule.selectors) {
      ComplexSelector sel = ParseComplexSelector(selector);
      if (sel.compounds.empty() || sel.compounds.back().pseudo != pseudo) continue;
      if (SelectorMatches(node, selector)) {
        candidates.push_back(
            {Specificity(selector), rule.source_order, &rule.declarations, &rule.important});
        break;
      }
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.specificity != b.specificity) return a.specificity < b.specificity;
    return a.source_order < b.source_order;
  });
  const float vw = sheet.viewport_w > 1.0f ? sheet.viewport_w : 800.0f;
  const float vh = sheet.viewport_h > 1.0f ? sheet.viewport_h : 600.0f;
  for (const Candidate& c : candidates)
    ApplyDeclarations(*c.declarations, &style, em_base, rem_base, vw, vh);
  for (const Candidate& c : candidates)
    ApplyDeclarations(*c.important, &style, em_base, rem_base, vw, vh);
  return style;
}

namespace {

void CollectStyleText(const Node& node, std::string* out) {
  if (node.type == NodeType::kElement && node.tag_name == "style") {
    *out += node.TextContent();
    *out += "\n";
    return;
  }
  for (const auto& child : node.children) CollectStyleText(*child, out);
}

const char* DisplayCss(DisplayType d) {
  switch (d) {
    case DisplayType::kBlock: return "block";
    case DisplayType::kInline: return "inline";
    case DisplayType::kInlineBlock: return "inline-block";
    case DisplayType::kFlex:
    case DisplayType::kGrid: return "flex";
    case DisplayType::kNone: return "none";
    case DisplayType::kTable: return "table";
    case DisplayType::kTableRow: return "table-row";
    case DisplayType::kTableCell: return "table-cell";
    case DisplayType::kTableRowGroup: return "table-row-group";
    case DisplayType::kListItem: return "block";
  }
  return "block";
}

void AppendColor(std::string* out, const char* prop, uint32_t c) {
  char buf[64];
  unsigned a = (c >> 24) & 0xffu;
  unsigned r = (c >> 16) & 0xffu;
  unsigned g = (c >> 8) & 0xffu;
  unsigned b = c & 0xffu;
  if (a >= 255) {
    std::snprintf(buf, sizeof(buf), "%s: #%02X%02X%02X;", prop, r, g, b);
  } else {
    std::snprintf(buf, sizeof(buf), "%s: rgba(%u,%u,%u,%.3f);", prop, r, g, b, a / 255.0f);
  }
  *out += buf;
}

void AppendLen(std::string* out, const char* prop, const Length& l, float viewport_w = 800.0f,
               float viewport_h = 600.0f) {
  char buf[64];
  if (l.is_auto()) {
    std::snprintf(buf, sizeof(buf), "%s: auto;", prop);
    *out += buf;
    return;
  }
  // vmin/vmax are not RmlUi Unit::VW/VH. Resolve against the cascade sheet
  // viewport so AuthorRcss does not lie (mapping vmin→vw was wrong off-square).
  if (l.unit == Length::Unit::kVmin || l.unit == Length::Unit::kVmax) {
    float px = l.Resolve(0, 0, 16.0f, 16.0f, viewport_w, viewport_h);
    std::snprintf(buf, sizeof(buf), "%s: %.0fpx;", prop, px);
    *out += buf;
    return;
  }
  const char* unit = "px";
  switch (l.unit) {
    case Length::Unit::kPercent:
      // Keep % here; BakeFlexRowWidths resolves against the real containing
      // block (viewport % would over-size nested #dash-body / .panel).
      std::snprintf(buf, sizeof(buf), "%s: %.0f%%;", prop, l.value);
      *out += buf;
      return;
    case Length::Unit::kEm:
      unit = "em";
      break;
    case Length::Unit::kRem:
      unit = "rem";
      break;
    case Length::Unit::kVw:
      unit = "vw";
      break;
    case Length::Unit::kVh:
      unit = "vh";
      break;
    default:
      unit = "px";
      break;
  }
  std::snprintf(buf, sizeof(buf), "%s: %.0f%s;", prop, l.value, unit);
  *out += buf;
}

bool LenEq(const Length& a, const Length& b) {
  return a.unit == b.unit && a.value == b.value && a.is_auto() == b.is_auto();
}

  // Author-origin only: UA defaults stay on RmlUi. Colors and borders stay
  // on the live stylesheet so :hover / :focus can override them — inlining
  // background-color on chrome buttons made those states dead.
  // Grid still maps to flex+wrap (RmlUi has no track list); keep that hack.
  std::string AuthorRcss(const ComputedStyle& ua, const ComputedStyle& s, float viewport_w,
                         float viewport_h) {
  std::string out;
  if (s.display != ua.display) {
    out += "display: ";
    out += DisplayCss(s.display);
    out += ";";
    if (s.display == DisplayType::kGrid) out += "flex-wrap: wrap;";
  }
  if (!LenEq(s.width, ua.width) && !s.width.is_auto())
    AppendLen(&out, "width", s.width, viewport_w, viewport_h);
  if (!LenEq(s.height, ua.height) && !s.height.is_auto())
    AppendLen(&out, "height", s.height, viewport_w, viewport_h);
  if (!LenEq(s.min_width, ua.min_width) && !s.min_width.is_auto() && s.min_width.value != 0)
    AppendLen(&out, "min-width", s.min_width, viewport_w, viewport_h);
  if (!LenEq(s.max_width, ua.max_width) && !s.max_width.is_auto())
    AppendLen(&out, "max-width", s.max_width, viewport_w, viewport_h);
  if (!LenEq(s.min_height, ua.min_height) && !s.min_height.is_auto() && s.min_height.value != 0)
    AppendLen(&out, "min-height", s.min_height, viewport_w, viewport_h);
  if (!LenEq(s.max_height, ua.max_height) && !s.max_height.is_auto())
    AppendLen(&out, "max-height", s.max_height, viewport_w, viewport_h);
  if (!LenEq(s.margin_top, ua.margin_top) || !LenEq(s.margin_right, ua.margin_right) ||
      !LenEq(s.margin_bottom, ua.margin_bottom) || !LenEq(s.margin_left, ua.margin_left)) {
    if (!s.margin_top.is_auto() || !s.margin_right.is_auto() || !s.margin_bottom.is_auto() ||
        !s.margin_left.is_auto()) {
      AppendLen(&out, "margin-top", s.margin_top, viewport_w, viewport_h);
      AppendLen(&out, "margin-right", s.margin_right, viewport_w, viewport_h);
      AppendLen(&out, "margin-bottom", s.margin_bottom, viewport_w, viewport_h);
      AppendLen(&out, "margin-left", s.margin_left, viewport_w, viewport_h);
    }
  }
  if (!LenEq(s.padding_top, ua.padding_top) || !LenEq(s.padding_right, ua.padding_right) ||
      !LenEq(s.padding_bottom, ua.padding_bottom) || !LenEq(s.padding_left, ua.padding_left)) {
    AppendLen(&out, "padding-top", s.padding_top, viewport_w, viewport_h);
    AppendLen(&out, "padding-right", s.padding_right, viewport_w, viewport_h);
    AppendLen(&out, "padding-bottom", s.padding_bottom, viewport_w, viewport_h);
    AppendLen(&out, "padding-left", s.padding_left, viewport_w, viewport_h);
  }
  if (s.font_size != ua.font_size) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "font-size: %.0fpx;", s.font_size);
    out += buf;
  }
  if (s.font_bold != ua.font_bold && s.font_bold) out += "font-weight: 700;";
  if (s.opacity != ua.opacity && s.opacity < 0.999f) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "opacity: %.3f;", s.opacity);
    out += buf;
  }
  if (s.text_align != ua.text_align) {
    if (s.text_align == TextAlign::kCenter) out += "text-align: center;";
    else if (s.text_align == TextAlign::kRight) out += "text-align: right;";
  }
  if ((s.display == DisplayType::kFlex || s.display == DisplayType::kGrid) &&
      (s.display != ua.display || s.flex_direction != ua.flex_direction ||
       s.flex_wrap != ua.flex_wrap || s.gap != ua.gap ||
       s.justify_content != ua.justify_content || s.align_items != ua.align_items)) {
    out += s.flex_direction == FlexDirection::kColumn ? "flex-direction: column;"
                                                      : "flex-direction: row;";
    if (s.flex_wrap == FlexWrap::kWrap) out += "flex-wrap: wrap;";
    if (s.gap > 0.01f) {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "column-gap: %.0fpx;row-gap: %.0fpx;", s.gap, s.gap);
      out += buf;
    }
    if (s.justify_content != ua.justify_content || s.display != ua.display) {
      if (s.justify_content == JustifyContent::kFlexEnd)
        out += "justify-content: flex-end;";
      else if (s.justify_content == JustifyContent::kCenter)
        out += "justify-content: center;";
      else if (s.justify_content == JustifyContent::kSpaceBetween)
        out += "justify-content: space-between;";
      else if (s.justify_content == JustifyContent::kSpaceAround)
        out += "justify-content: space-around;";
      else if (s.display != ua.display)
        out += "justify-content: flex-start;";
    }
    if (s.align_items != ua.align_items || s.display != ua.display) {
      if (s.align_items == AlignItems::kCenter)
        out += "align-items: center;";
      else if (s.align_items == AlignItems::kFlexEnd)
        out += "align-items: flex-end;";
      else if (s.align_items == AlignItems::kFlexStart)
        out += "align-items: flex-start;";
      else if (s.display != ua.display)
        out += "align-items: stretch;";
    }
  }
  if (s.flex_grow != ua.flex_grow && s.flex_grow > 0.01f) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "flex-grow: %.3f;", s.flex_grow);
    out += buf;
  }
  if (s.flex_shrink != ua.flex_shrink) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "flex-shrink: %.3f;", s.flex_shrink);
    out += buf;
  }
  if (s.has_radius() && (s.radius_tl != ua.radius_tl || s.radius_tr != ua.radius_tr ||
                         s.radius_br != ua.radius_br || s.radius_bl != ua.radius_bl)) {
    char buf[80];
    if (s.radius_tl == s.radius_tr && s.radius_tr == s.radius_br && s.radius_br == s.radius_bl)
      std::snprintf(buf, sizeof(buf), "border-radius: %.0fpx;", s.radius_tl);
    else
      std::snprintf(buf, sizeof(buf), "border-radius: %.0fpx %.0fpx %.0fpx %.0fpx;", s.radius_tl,
                    s.radius_tr, s.radius_br, s.radius_bl);
    out += buf;
  }
  return out;
}

void ApplyTree(Node* node, const Stylesheet& sheet, const ComputedStyle* parent) {
  if (!node) return;
  if (node->type == NodeType::kDocument) {
    for (auto& child : node->children) ApplyTree(child.get(), sheet, parent);
    return;
  }
  if (node->type != NodeType::kElement) return;
  if (node->tag_name == "style" || node->tag_name == "script") return;
  Stylesheet ua_sheet;
  ComputedStyle ua = ComputeStyle(*node, ua_sheet, parent);
  ComputedStyle style = ComputeStyle(*node, sheet, parent);
  // Chrome (#shell / #toolbar / #omnibox) lives outside #viewport. Restricting
  // author RCSS to viewport descendants left UniShell chrome unstyled when
  // RmlUi dropped `background:` (decorator, not a color).
  std::string author = AuthorRcss(ua, style, sheet.viewport_w, sheet.viewport_h);
  if (!author.empty()) {
    std::string cur = node->GetAttribute("style");
    if (!cur.empty() && cur.back() != ';') cur += ';';
    node->attributes["style"] = cur + author;
  }
  for (auto& child : node->children) ApplyTree(child.get(), sheet, &style);
}

void HtmlEscape(std::string* out, const std::string& s, bool attr) {
  for (unsigned char c : s) {
    if (c == '&') *out += "&amp;";
    else if (c == '<') *out += "&lt;";
    else if (c == '>') *out += "&gt;";
    else if (attr && c == '"') *out += "&quot;";
    else *out += static_cast<char>(c);
  }
}

bool IsVoidTag(const std::string& tag) {
  return tag == "area" || tag == "base" || tag == "br" || tag == "col" || tag == "embed" ||
         tag == "hr" || tag == "img" || tag == "input" || tag == "link" || tag == "meta" ||
         tag == "param" || tag == "source" || tag == "track" || tag == "wbr";
}

void SerializeNode(const Node& node, std::string* out) {
  if (node.type == NodeType::kDocument) {
    for (const auto& c : node.children) SerializeNode(*c, out);
    return;
  }
  if (node.type == NodeType::kText) {
    HtmlEscape(out, node.text_data, false);
    return;
  }
  *out += '<';
  *out += node.tag_name;
  for (const auto& kv : node.attributes) {
    *out += ' ';
    *out += kv.first;
    *out += "=\"";
    HtmlEscape(out, kv.second, true);
    *out += '"';
  }
  if (IsVoidTag(node.tag_name)) {
    *out += " />";
    return;
  }
  *out += '>';
  if (node.tag_name == "style" || node.tag_name == "script") {
    for (const auto& c : node.children) {
      if (c->type == NodeType::kText)
        *out += c->text_data;
      else
        SerializeNode(*c, out);
    }
  } else {
    for (const auto& c : node.children) SerializeNode(*c, out);
  }
  *out += "</";
  *out += node.tag_name;
  *out += '>';
}

void ReplaceStyleWidth(Node* node, float width_px) {
  if (!node) return;
  char buf[48];
  std::snprintf(buf, sizeof(buf), "width: %.2fpx;", width_px);
  std::string cur = node->GetAttribute("style");
  // Drop prior width: so AuthorRcss %→px or a previous bake does not leave
  // two competing declarations (RmlUi is not guaranteed to take the last).
  std::string cleaned;
  cleaned.reserve(cur.size() + 24);
  for (const std::string& decl : SplitTopLevel(cur, ';')) {
    std::string t = Trim(decl);
    if (t.empty()) continue;
    std::string lower = ToLowerAscii(t);
    if (lower.rfind("width:", 0) == 0) continue;
    cleaned += t;
    cleaned += ';';
  }
  cleaned += buf;
  node->attributes["style"] = cleaned;
}

// Font-free flex row bake: RmlUi does not distribute flex-grow against a
// percent parent the way Blinker layout does (black void beside #side).
// After AuthorRcss has turned width:% into px on the row, assign leftover
// space to flex-grow children as definite width: on style="".
void BakeFlexRowWidths(Node* node, const Stylesheet& sheet, const ComputedStyle* parent,
                       float cb_w) {
  if (!node) return;
  if (node->type == NodeType::kDocument) {
    for (auto& child : node->children) BakeFlexRowWidths(child.get(), sheet, parent, cb_w);
    return;
  }
  if (node->type != NodeType::kElement) return;
  if (node->tag_name == "style" || node->tag_name == "script") return;

  ComputedStyle style = ComputeStyle(*node, sheet, parent);
  float used_w = cb_w;
  if (!style.width.is_auto()) {
    used_w = style.width.Resolve(cb_w, cb_w, style.font_size, 16.0f, sheet.viewport_w,
                                 sheet.viewport_h);
    // Percent widths need a definite px for RmlUi; resolve against this
    // node's containing block (not the viewport — nested 100% must shrink).
    if (style.width.unit == Length::Unit::kPercent) ReplaceStyleWidth(node, used_w);
  } else if (style.display == DisplayType::kBlock || style.display == DisplayType::kFlex ||
             style.display == DisplayType::kGrid) {
    used_w = cb_w;
  }

  if ((style.display == DisplayType::kFlex || style.display == DisplayType::kGrid) &&
      style.flex_direction != FlexDirection::kColumn && used_w > 0.0f) {
    struct Kid {
      Node* node = nullptr;
      float grow = 0.0f;
      float fixed = 0.0f;
      bool grower = false;
      float child_cb = 0.0f;
    };
    std::vector<Kid> kids;
    float fixed_sum = 0.0f;
    float grow_sum = 0.0f;
    float gap = style.gap > 0.01f ? style.gap : 0.0f;
    int gap_count = 0;
    for (auto& child : node->children) {
      if (!child || child->type != NodeType::kElement) continue;
      if (child->tag_name == "style" || child->tag_name == "script") continue;
      ComputedStyle cs = ComputeStyle(*child, sheet, &style);
      if (cs.display == DisplayType::kNone) continue;
      Kid k;
      k.node = child.get();
      k.grow = cs.flex_grow > 0.0f ? cs.flex_grow : 0.0f;
      if (!cs.width.is_auto()) {
        k.fixed = cs.width.Resolve(used_w, 0.0f, cs.font_size, 16.0f, sheet.viewport_w,
                                   sheet.viewport_h);
        k.grower = false;
        fixed_sum += k.fixed;
        k.child_cb = k.fixed;
      } else if (k.grow > 0.0f) {
        k.grower = true;
        grow_sum += k.grow;
      } else {
        // Intrinsic/auto non-grower: leave alone; do not steal leftover.
        k.child_cb = used_w;
      }
      kids.push_back(k);
      ++gap_count;
    }
    float gaps = gap_count > 1 ? gap * static_cast<float>(gap_count - 1) : 0.0f;
    float rem = used_w - fixed_sum - gaps;
    if (rem < 0.0f) rem = 0.0f;
    for (Kid& k : kids) {
      if (k.grower && grow_sum > 0.0f) {
        float w = rem * (k.grow / grow_sum);
        ReplaceStyleWidth(k.node, w);
        k.child_cb = w;
      }
    }
    for (Kid& k : kids) BakeFlexRowWidths(k.node, sheet, &style, k.child_cb > 0.0f ? k.child_cb : used_w);
    return;
  }

  for (auto& child : node->children) BakeFlexRowWidths(child.get(), sheet, &style, used_w);
}

#ifdef BLINK_HAS_PAINT_PIPELINE
// Bakes each element's real, laid-out pixel width onto its own style=""
// when Blinker's own layout pass resolved one. Blinker already runs the
// real layout for this (see blink/layout.h ComputeLayout); this hands
// RmlUi the resolved number instead of asking it to re-derive the same
// distribution itself (which is what produced the uneven header row and
// mismatched .col.panel columns this replaces).
//
// Height is deliberately left alone: containers that receive DML-appended
// content after this one-shot cascade (guest/catalog/event lists) must
// keep growing on their own via RmlUi's live reflow on each mutation --
// freezing their height here would clip everything appended later.
void InjectComputedWidths(const LayoutBox& box) {
  if (box.source && box.source->type == NodeType::kElement && box.width >= 0.0f) {
    ReplaceStyleWidth(const_cast<Node*>(box.source), box.width);
  }
  for (const LayoutBox& child : box.children) InjectComputedWidths(child);
}
#endif  // BLINK_HAS_PAINT_PIPELINE

}  // namespace

std::string InlineComputedCss(const std::string& html, float viewport_w, float viewport_h) {
  if (html.empty()) return html;
  HtmlDocument doc = ParseHtml(html);
  if (!doc.root) return html;
  std::string style_text;
  CollectStyleText(*doc.root, &style_text);
  Stylesheet sheet = ParseStylesheet(style_text);
  sheet.viewport_w = viewport_w > 1.0f ? viewport_w : 800.0f;
  sheet.viewport_h = viewport_h > 1.0f ? viewport_h : 600.0f;
  ApplyTree(doc.root.get(), sheet, nullptr);
  // Always bake flex-grow leftovers to px (no font/layout dependency). Paint
  // InjectComputedWidths below overwrites with measured widths when available.
  BakeFlexRowWidths(doc.root.get(), sheet, nullptr, sheet.viewport_w);
#ifdef BLINK_HAS_PAINT_PIPELINE
  const FontSet& fonts = GetPaintFonts();
  if (fonts.valid()) {
    LayoutResult layout = ComputeLayout(doc, sheet.viewport_w, fonts, sheet.viewport_h);
    if (layout.ok) InjectComputedWidths(layout.root);
  }
#endif
  // Keep page <style> so RmlUi can parse the same sheet (vw/vh native).
  // Blinker already wrote cascaded RCSS onto style=""; the two engines
  // share the document instead of Blinker remoting CSS away.
  return SerializeHtml(doc);
}

std::string SerializeHtml(const HtmlDocument& doc) {
  if (!doc.root) return {};
  std::string out;
  out.reserve(4096);
  SerializeNode(*doc.root, &out);
  return out;
}

}  // namespace blink
