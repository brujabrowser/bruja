#include "blink/html_parser.h"

#include <cctype>
#include <unordered_set>

namespace blink {

namespace {

const std::unordered_set<std::string>& VoidElements() {
  static const std::unordered_set<std::string> kVoid = {
      "area", "base", "br",   "col",  "embed",  "hr",    "img",
      "input", "link", "meta", "param", "source", "track", "wbr"};
  return kVoid;
}

const std::unordered_set<std::string>& RawTextElements() {
  static const std::unordered_set<std::string> kRaw = {"script", "style"};
  return kRaw;
}

// HTML5 "in body" insertion mode: these start tags implicitly close an
// open <p> first (WHATWG "A start tag whose tag name is one of..." rule)
// so e.g. `<p>a<p>b` parses as two sibling <p>s, not nested -- without
// this, this parser was one of the very few real gaps a same-HTML-through-
// three-facades parity run (BrowserEmu's) actually found, since it
// exercises this parser identically underneath v8/mothman/yeti. Simplified
// vs. the real spec's "has a p element in button scope" check: this stack
// has no scope tracking, so it only looks at the immediately-open element,
// not any ancestor -- covers the common adjacent-siblings case tested,
// not an open <p> several levels up with an unrelated element between.
const std::unordered_set<std::string>& ImplicitlyClosesP() {
  static const std::unordered_set<std::string> kClosesP = {
      "address", "article",    "aside",  "blockquote", "center",  "details",
      "dialog",  "dir",        "div",    "dl",         "fieldset", "figcaption",
      "figure",  "footer",     "form",   "h1",         "h2",       "h3",
      "h4",      "h5",         "h6",     "header",     "hgroup",   "hr",
      "main",    "menu",       "nav",    "ol",         "p",        "pre",
      "section", "summary",    "table",  "ul"};
  return kClosesP;
}

std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string DecodeEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '&') {
      out += s[i++];
      continue;
    }
    auto semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out += s[i++];
      continue;
    }
    std::string name = s.substr(i + 1, semi - i - 1);
    if (name == "amp") {
      out += '&';
    } else if (name == "lt") {
      out += '<';
    } else if (name == "gt") {
      out += '>';
    } else if (name == "quot") {
      out += '"';
    } else if (name == "apos" || name == "#39") {
      out += '\'';
    } else if (name == "nbsp") {
      out += ' ';
    } else {
      out += s.substr(i, semi - i + 1);
      i = semi + 1;
      continue;
    }
    i = semi + 1;
  }
  return out;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

class Parser {
 public:
  explicit Parser(const std::string& source) : src_(source) {}

  HtmlDocument Parse() {
    HtmlDocument doc;
    doc.root = std::make_unique<Node>(NodeType::kDocument);
    stack_.push_back(doc.root.get());

    while (pos_ < src_.size()) {
      if (Peek(0) == '<') {
        if (Peek(1) == '!') {
          SkipDeclarationOrComment();
        } else if (Peek(1) == '/') {
          ParseEndTag();
        } else if (std::isalpha(static_cast<unsigned char>(Peek(1)))) {
          ParseStartTag();
        } else {
          ConsumeTextRun();
        }
      } else {
        ConsumeTextRun();
      }
    }
    return doc;
  }

 private:
  char Peek(size_t ahead) const {
    size_t p = pos_ + ahead;
    return p < src_.size() ? src_[p] : '\0';
  }

  void SkipDeclarationOrComment() {
    if (src_.compare(pos_, 4, "<!--") == 0) {
      auto end = src_.find("-->", pos_ + 4);
      pos_ = end == std::string::npos ? src_.size() : end + 3;
      return;
    }
    auto end = src_.find('>', pos_);
    pos_ = end == std::string::npos ? src_.size() : end + 1;
  }

  void ConsumeTextRun() {
    size_t start = pos_;
    while (pos_ < src_.size() && src_[pos_] != '<') ++pos_;
    std::string text = DecodeEntities(src_.substr(start, pos_ - start));
    bool blank = true;
    for (char c : text) {
      if (!IsSpace(c)) {
        blank = false;
        break;
      }
    }
    if (!blank) {
      auto node = std::make_unique<Node>(NodeType::kText);
      node->text_data = text;
      stack_.back()->AppendChild(std::move(node));
    }
  }

  std::string ConsumeTagName() {
    size_t start = pos_;
    while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) ||
                                  src_[pos_] == '-')) {
      ++pos_;
    }
    return ToLower(src_.substr(start, pos_ - start));
  }

  void SkipWhitespace() {
    while (pos_ < src_.size() && IsSpace(src_[pos_])) ++pos_;
  }

  std::unordered_map<std::string, std::string> ConsumeAttributes() {
    std::unordered_map<std::string, std::string> attrs;
    for (;;) {
      SkipWhitespace();
      if (pos_ >= src_.size() || Peek(0) == '>' || (Peek(0) == '/' && Peek(1) == '>')) break;
      size_t name_start = pos_;
      while (pos_ < src_.size() && !IsSpace(src_[pos_]) && src_[pos_] != '=' &&
             src_[pos_] != '>' && src_[pos_] != '/') {
        ++pos_;
      }
      std::string name = ToLower(src_.substr(name_start, pos_ - name_start));
      if (name.empty()) {
        ++pos_;  // Malformed input (stray '=' or similar); don't spin forever.
        continue;
      }
      SkipWhitespace();
      std::string value = "true";
      if (Peek(0) == '=') {
        ++pos_;
        SkipWhitespace();
        if (Peek(0) == '"' || Peek(0) == '\'') {
          char quote = src_[pos_++];
          size_t val_start = pos_;
          while (pos_ < src_.size() && src_[pos_] != quote) ++pos_;
          value = DecodeEntities(src_.substr(val_start, pos_ - val_start));
          if (pos_ < src_.size()) ++pos_;  // closing quote
        } else {
          size_t val_start = pos_;
          while (pos_ < src_.size() && !IsSpace(src_[pos_]) && src_[pos_] != '>') ++pos_;
          value = DecodeEntities(src_.substr(val_start, pos_ - val_start));
        }
      }
      attrs[name] = value;
    }
    return attrs;
  }

  void ParseStartTag() {
    pos_ += 1;  // '<'
    std::string tag = ConsumeTagName();
    auto attrs = ConsumeAttributes();
    bool self_closing = false;
    if (Peek(0) == '/' && Peek(1) == '>') {
      self_closing = true;
      pos_ += 2;
    } else if (Peek(0) == '>') {
      pos_ += 1;
    }

    // `tag` is already lowercase (ConsumeTagName), matching tag_name's
    // own storage convention.
    if (stack_.back()->tag_name == "p" && ImplicitlyClosesP().count(tag)) {
      stack_.pop_back();
    }

    auto node = std::make_unique<Node>(NodeType::kElement);
    node->tag_name = tag;
    node->attributes = std::move(attrs);
    Node* raw = stack_.back()->AppendChild(std::move(node));

    if (RawTextElements().count(tag)) {
      std::string close = "</" + tag;
      auto end = FindCaseInsensitive(close, pos_);
      std::string content =
          end == std::string::npos ? src_.substr(pos_) : src_.substr(pos_, end - pos_);
      if (!content.empty()) {
        auto text_node = std::make_unique<Node>(NodeType::kText);
        text_node->text_data = content;
        raw->AppendChild(std::move(text_node));
      }
      pos_ = end == std::string::npos ? src_.size() : end;
      if (pos_ < src_.size()) {
        auto gt = src_.find('>', pos_);
        pos_ = gt == std::string::npos ? src_.size() : gt + 1;
      }
      return;
    }

    if (!self_closing && !VoidElements().count(tag)) {
      stack_.push_back(raw);
    }
  }

  size_t FindCaseInsensitive(const std::string& needle, size_t from) const {
    std::string lower_src = ToLower(src_.substr(from));
    std::string lower_needle = ToLower(needle);
    auto pos = lower_src.find(lower_needle);
    return pos == std::string::npos ? std::string::npos : from + pos;
  }

  void ParseEndTag() {
    pos_ += 2;  // "</"
    std::string tag = ConsumeTagName();
    auto gt = src_.find('>', pos_);
    pos_ = gt == std::string::npos ? src_.size() : gt + 1;

    // Walk up the open-element stack for a match (document root never
    // pops); mismatched/unopened end tags are ignored rather than erroring.
    for (size_t i = stack_.size(); i-- > 1;) {
      if (stack_[i]->tag_name == tag) {
        stack_.resize(i);
        return;
      }
    }
  }

  const std::string& src_;
  size_t pos_ = 0;
  std::vector<Node*> stack_;
};

}  // namespace

HtmlDocument ParseHtml(const std::string& source) { return Parser(source).Parse(); }

}  // namespace blink
