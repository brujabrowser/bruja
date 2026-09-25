#include "blink/dml.h"

#include "blink/html_parser.h"

#include <cctype>
#include <string>
#include <vector>

namespace blink {
namespace {

struct DmlOp {
  std::string action;
  std::string target_id;
  std::string value;
};

std::string JsonUnescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      switch (c) {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case '"':
        case '\\':
        case '/':
          out.push_back(c);
          break;
        case 'u':
          if (i + 4 < s.size()) i += 4;  // skip BMP escape
          break;
        default:
          out.push_back(c);
          break;
      }
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

bool ExtractJsonString(const std::string& obj, const char* key, std::string* out) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t pos = obj.find(needle);
  if (pos == std::string::npos) return false;
  pos = obj.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  ++pos;
  while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t' || obj[pos] == '\n')) ++pos;
  if (pos >= obj.size() || obj[pos] != '"') return false;
  ++pos;
  std::string raw;
  while (pos < obj.size()) {
    if (obj[pos] == '\\' && pos + 1 < obj.size()) {
      raw.push_back(obj[pos++]);
      raw.push_back(obj[pos++]);
      continue;
    }
    if (obj[pos] == '"') break;
    raw.push_back(obj[pos++]);
  }
  *out = JsonUnescape(raw);
  return true;
}

std::vector<DmlOp> ParseDmlArray(const std::string& json) {
  std::vector<DmlOp> out;
  size_t i = 0;
  while (i < json.size()) {
    if (json[i] != '{') {
      ++i;
      continue;
    }
    size_t start = i;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (; i < json.size(); ++i) {
      char c = json[i];
      if (in_str) {
        if (esc)
          esc = false;
        else if (c == '\\')
          esc = true;
        else if (c == '"')
          in_str = false;
        continue;
      }
      if (c == '"') {
        in_str = true;
        continue;
      }
      if (c == '{')
        ++depth;
      else if (c == '}') {
        --depth;
        if (depth == 0) {
          ++i;
          break;
        }
      }
    }
    std::string obj = json.substr(start, i - start);
    DmlOp op;
    ExtractJsonString(obj, "action", &op.action);
    if (!ExtractJsonString(obj, "target_id", &op.target_id))
      ExtractJsonString(obj, "targetId", &op.target_id);
    ExtractJsonString(obj, "value", &op.value);
    if (!op.action.empty() && !op.target_id.empty()) out.push_back(std::move(op));
  }
  return out;
}

void ReplaceChildrenWithFragment(Node* el, const std::string& html) {
  if (!el) return;
  el->children.clear();
  if (html.empty()) return;
  // Prefer text when the value has no tags (KPI counters).
  if (html.find('<') == std::string::npos) {
    auto text = std::make_unique<Node>(NodeType::kText);
    text->text_data = html;
    el->AppendChild(std::move(text));
    return;
  }
  HtmlDocument frag = ParseHtml(html);
  if (!frag.root) return;
  Node* root = frag.root.get();
  Node* body = root->FindFirstElement("body");
  Node* src = body ? body : root;
  for (auto& child : src->children) {
    if (!child) continue;
    el->AppendChild(std::move(child));
  }
  src->children.clear();
}

void AppendFragment(Node* el, const std::string& html) {
  if (!el || html.empty()) return;
  HtmlDocument frag = ParseHtml(html);
  if (!frag.root) return;
  Node* root = frag.root.get();
  Node* body = root->FindFirstElement("body");
  Node* src = body ? body : root;
  for (auto& child : src->children) {
    if (!child) continue;
    el->AppendChild(std::move(child));
  }
  src->children.clear();
}

void MutateClass(Node* el, const std::string& cls, bool add) {
  if (!el || cls.empty()) return;
  std::string cur = el->GetAttribute("class");
  std::vector<std::string> parts;
  std::string tok;
  for (char c : cur) {
    if (c == ' ' || c == '\t' || c == '\n') {
      if (!tok.empty()) {
        parts.push_back(tok);
        tok.clear();
      }
    } else {
      tok.push_back(c);
    }
  }
  if (!tok.empty()) parts.push_back(tok);
  bool found = false;
  std::vector<std::string> next;
  for (const auto& p : parts) {
    if (p == cls) {
      found = true;
      if (add) next.push_back(p);
    } else {
      next.push_back(p);
    }
  }
  if (add && !found) next.push_back(cls);
  std::string joined;
  for (size_t i = 0; i < next.size(); ++i) {
    if (i) joined.push_back(' ');
    joined += next[i];
  }
  el->attributes["class"] = joined;
}

}  // namespace

std::string SplitLoadGmlData(const std::string& data_json, std::string* dml_json) {
  if (dml_json) dml_json->clear();
  if (data_json.empty()) return "{}";
  const std::string key = "\"__dml\"";
  size_t pos = data_json.find(key);
  if (pos == std::string::npos) return data_json;
  size_t colon = data_json.find(':', pos + key.size());
  if (colon == std::string::npos) return data_json;
  size_t arr = data_json.find('[', colon);
  if (arr == std::string::npos) return data_json;
  int depth = 0;
  size_t end = arr;
  bool in_str = false;
  bool esc = false;
  for (; end < data_json.size(); ++end) {
    char c = data_json[end];
    if (in_str) {
      if (esc)
        esc = false;
      else if (c == '\\')
        esc = true;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '[')
      ++depth;
    else if (c == ']') {
      --depth;
      if (depth == 0) {
        ++end;
        break;
      }
    }
  }
  if (dml_json) *dml_json = data_json.substr(arr, end - arr);
  // Drop "__dml": [...] including a preceding comma when present.
  size_t cut_start = pos;
  while (cut_start > 0 && (data_json[cut_start - 1] == ' ' || data_json[cut_start - 1] == '\n' ||
                           data_json[cut_start - 1] == '\t'))
    --cut_start;
  if (cut_start > 0 && data_json[cut_start - 1] == ',') {
    --cut_start;
  } else {
    // Leading field: also drop trailing comma after the array.
    size_t after = end;
    while (after < data_json.size() &&
           (data_json[after] == ' ' || data_json[after] == '\n' || data_json[after] == '\t'))
      ++after;
    if (after < data_json.size() && data_json[after] == ',') ++after;
    end = after;
  }
  return data_json.substr(0, cut_start) + data_json.substr(end);
}

void ApplyHtmlDml(HtmlDocument* doc, const std::string& dml_json) {
  if (!doc || !doc->root || dml_json.empty()) return;
  for (const DmlOp& op : ParseDmlArray(dml_json)) {
    Node* el = doc->root->FindById(op.target_id);
    if (!el) continue;
    if (op.action == "setValue" || op.action == "remove") {
      ReplaceChildrenWithFragment(el, op.action == "remove" ? std::string() : op.value);
    } else if (op.action == "append") {
      AppendFragment(el, op.value);
    } else if (op.action == "addClass") {
      MutateClass(el, op.value, true);
    } else if (op.action == "removeClass") {
      MutateClass(el, op.value, false);
    }
  }
}

}  // namespace blink
