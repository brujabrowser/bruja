#ifndef BLINK_DOM_H_
#define BLINK_DOM_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace blink {

enum class NodeType {
  kDocument,
  kElement,
  kText,
};

// A minimal DOM tree -- tag name, attributes, children, text content.
// No mutation observers, no live collections, no event machinery: this is
// the first rung of a native replacement for loki-closure's dom.go, scoped
// to exactly what HtmlParser and (eventually) a layout engine need.
struct Node {
  NodeType type;
  std::string tag_name;   // lowercase; empty for kDocument/kText
  std::string text_data;  // only meaningful for kText
  std::unordered_map<std::string, std::string> attributes;
  std::vector<std::unique_ptr<Node>> children;
  Node* parent = nullptr;

  explicit Node(NodeType t) : type(t) {}

  Node* AppendChild(std::unique_ptr<Node> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
  }

  std::string GetAttribute(const std::string& name) const {
    auto it = attributes.find(name);
    return it == attributes.end() ? std::string() : it->second;
  }

  // Concatenates this node's own and every descendant's text content, in
  // document order -- the same "flatten to plain text" operation
  // loki-closure's PaintText does, minus any layout/visibility awareness
  // (a kDocument's <title>/<script>/<style> subtrees are excluded by the
  // caller, not by this generic walk -- see HtmlDocument::FindTitle).
  std::string TextContent() const {
    if (type == NodeType::kText) return text_data;
    std::string out;
    for (const auto& child : children) out += child->TextContent();
    return out;
  }

  // Depth-first search for the first descendant element with this tag
  // name (case-sensitive; callers pass already-lowercased names, matching
  // how the parser stores them).
  Node* FindFirstElement(const std::string& tag) {
    if (type == NodeType::kElement && tag_name == tag) return this;
    for (auto& child : children) {
      if (Node* found = child->FindFirstElement(tag)) return found;
    }
    return nullptr;
  }

  Node* FindById(const std::string& id) {
    if (type == NodeType::kElement && GetAttribute("id") == id) return this;
    for (auto& child : children) {
      if (Node* found = child->FindById(id)) return found;
    }
    return nullptr;
  }
};

// A parsed document: owns the root node (a kDocument node whose children
// are the top-level parsed elements, normally just <html>).
struct DecodedImage {
  std::vector<uint8_t> rgba;
  int width = 0;
  int height = 0;
};

struct HtmlDocument {
  std::unique_ptr<Node> root;
  std::string base_url;
  std::unordered_map<const Node*, DecodedImage> images;
  // Absolute URLs of fetched images (for `content: url(...)` on pseudos).
  std::unordered_map<std::string, DecodedImage> images_by_url;
  // Live text-input focus (an <input> / <textarea> in `root`). Null after
  // Navigate/LoadHTML until HandleClick lands on a field. HandleKey types
  // into this node's `value` attribute.
  Node* focused = nullptr;

  // Viewport scroll offset in document coordinates. Layout uses this for
  // `position: sticky` / `position: fixed`; paint translates content by
  // this amount when rendering a viewport-sized surface.
  float scroll_y = 0.0f;
  float scroll_x = 0.0f;
  // When non-zero, CaptureRawFrame paints a viewport this tall instead of
  // the full document height (paired with scroll_y for scrolled views).
  float viewport_height = 0.0f;

  // Per-element scroll offsets for `overflow: auto|scroll` containers.
  struct ElementScroll {
    float x = 0.0f;
    float y = 0.0f;
  };
  std::unordered_map<const Node*, ElementScroll> element_scroll;

  // Returns the text content of the first <title> element, or "" if none.
  std::string Title() const {
    if (!root) return "";
    Node* title = root->FindFirstElement("title");
    return title ? title->TextContent() : "";
  }
};

}  // namespace blink

#endif  // BLINK_DOM_H_
