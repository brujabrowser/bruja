#include "blink/gml.h"

#include "blink/html_parser.h"

#include <cctype>
#include <cstring>
#include <string>

#ifdef BLINK_HAS_GUIKIT
#include "guikit/gml.h"
#include "guikit/json.h"
#include "guikit/template.h"
#endif

namespace blink {
namespace {

std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

#ifdef BLINK_HAS_GUIKIT

bool LooksLikeControlTemplate(const std::string& s) {
  return s.find("{{ range") != std::string::npos || s.find("{{range") != std::string::npos ||
         s.find("{{ if") != std::string::npos || s.find("{{if") != std::string::npos ||
         s.find("{{ with") != std::string::npos || s.find("{{with") != std::string::npos ||
         s.find("{{ end") != std::string::npos || s.find("{{end") != std::string::npos;
}

bool TreeNeedsHtmlTemplate(const guikit::gml::Node& node) {
  if (auto* text = dynamic_cast<const guikit::gml::Text*>(&node)) {
    return LooksLikeControlTemplate(text->eval([](const std::string&, std::string&) { return false; }));
  }
  auto* el = dynamic_cast<const guikit::gml::Element*>(&node);
  if (!el) return false;
  for (const auto& child : el->children) {
    if (child && TreeNeedsHtmlTemplate(*child)) return true;
  }
  return false;
}

std::unique_ptr<Node> ConvertGmlNode(const guikit::gml::Node& node, const guikit::Value& data,
                                     const guikit::gml::ReadFileFn& read);

std::unique_ptr<Node> ConvertGmlElement(const guikit::gml::Element& el, const guikit::Value& data,
                                        const guikit::gml::ReadFileFn& read) {
  const std::string tag = ToLower(el.tag);
  if (tag == "wrapper" || tag == "import" || tag == "markdown") {
    std::string html = guikit::tmpl::execute(el.eval(read), data);
    HtmlDocument frag = ParseHtml(html);
    if (frag.root && !frag.root->children.empty()) {
      return std::move(frag.root->children.front());
    }
    auto empty = std::make_unique<Node>(NodeType::kElement);
    empty->tag_name = "div";
    return empty;
  }
  if (tag == "rule") {
    auto text = std::make_unique<Node>(NodeType::kText);
    text->text_data = el.eval(read);
    return text;
  }

  auto out = std::make_unique<Node>(NodeType::kElement);
  out->tag_name = tag.empty() ? "div" : tag;
  for (const auto& [key, val] : el.attributes) {
    out->attributes[ToLower(key)] = guikit::tmpl::execute(val, data);
  }
  for (const auto& child : el.children) {
    if (!child) continue;
    auto converted = ConvertGmlNode(*child, data, read);
    if (converted) out->AppendChild(std::move(converted));
  }
  return out;
}

std::unique_ptr<Node> ConvertGmlNode(const guikit::gml::Node& node, const guikit::Value& data,
                                     const guikit::gml::ReadFileFn& read) {
  if (auto* text = dynamic_cast<const guikit::gml::Text*>(&node)) {
    auto out = std::make_unique<Node>(NodeType::kText);
    out->text_data = guikit::tmpl::execute(text->eval(read), data);
    return out;
  }
  if (auto* el = dynamic_cast<const guikit::gml::Element*>(&node)) {
    return ConvertGmlElement(*el, data, read);
  }
  return nullptr;
}

HtmlDocument LowerGmlViaHtml(const std::string& gml, const guikit::Value& data,
                             const guikit::gml::ReadFileFn& read) {
  std::string raw = guikit::gml::evalAll(gml, read);
  std::string html = guikit::tmpl::execute(raw, data);
  return ParseHtml(html);
}

#endif  // BLINK_HAS_GUIKIT

}  // namespace

HtmlDocument ParseGml(const std::string& gml, const std::string& data_json, std::string* error) {
#ifdef BLINK_HAS_GUIKIT
  guikit::gml::ReadFileFn read = [](const std::string&, std::string&) { return false; };
  guikit::Value data;
  std::string parse_err;
  if (!guikit::json::parse(data_json.empty() ? "{}" : data_json, data, &parse_err)) {
    if (error) *error = parse_err;
    return HtmlDocument{};
  }
  guikit::gml::Parser parser(gml);
  auto roots = parser.parse();
  bool control = false;
  for (const auto& n : roots) {
    if (n && TreeNeedsHtmlTemplate(*n)) {
      control = true;
      break;
    }
  }
  if (control || roots.empty()) {
    return LowerGmlViaHtml(gml, data, read);
  }
  HtmlDocument doc;
  doc.root = std::make_unique<Node>(NodeType::kDocument);
  for (const auto& n : roots) {
    if (!n) continue;
    auto converted = ConvertGmlNode(*n, data, read);
    if (converted) doc.root->AppendChild(std::move(converted));
  }
  return doc;
#else
  if (error) *error = "LoadGML requires sibling guikit/cpp";
  (void)gml;
  (void)data_json;
  return HtmlDocument{};
#endif
}

}  // namespace blink
