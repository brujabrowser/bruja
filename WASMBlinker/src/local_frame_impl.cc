#include "blink/local_frame_impl.h"
#include "blink/html_raster.h"

#include "blink/gml.h"
#include "blink/dml.h"
#include "blink/html_parser.h"
#include "blink/http_client.h"
#include "blink/layout.h"
#include "blink/paint.h"
#include "blink/url.h"

#ifdef BLINK_HAS_IMAGE_DECODE
#include "wasmskia/image_codec.h"
#endif

#ifdef BLINK_HAS_WASMV16_JS
#include "wasmv16/js_layer.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blink {

#ifdef BLINK_HAS_DOM_BINDINGS
namespace {
// getComputedStyle(el) has no `this` to reach LocalFrameImpl::reverse_node_map_
// / current_sheet_ through (this facade's FunctionTemplate::New has no
// `data` parameter to close over) -- same thread_local-current-instance
// workaround WKWebViewImpl.cc's JsPostMessage uses for the same reason.
// Set only around RunClassicScripts/Eval, the two JS entry points; declared
// here (rather than down with JsGetComputedStyle itself) so it's visible to
// both -- RunClassicScripts is defined above EnsureDomInstalled/JsGetComputedStyle.
thread_local LocalFrameImpl* t_frame_for_js = nullptr;
}  // namespace
#endif

#ifdef BLINK_HAS_WASMV16_ENGINE
wasmv16::Engine* LocalFrameImpl::shared_engine_ = nullptr;

void LocalFrameImpl::SetSharedEngine(wasmv16::Engine* engine) { shared_engine_ = engine; }

wasmv16::Engine* LocalFrameImpl::JsEngine() {
  if (shared_engine_) return shared_engine_;
  if (!page_js_installed_) {
    page_js_installed_ = true;
#ifdef BLINK_HAS_WASMV16_JS
    wasmv16::InstallPageJsLayer(&js_engine_);
#endif
  }
  return &js_engine_;
}
#endif

namespace {

std::string ToLowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

void CollectScripts(const Node& node, std::vector<const Node*>* out) {
  if (node.type == NodeType::kElement && node.tag_name == "script") {
    out->push_back(&node);
    return;
  }
  for (const auto& child : node.children) CollectScripts(*child, out);
}

bool IsJavascriptMime(const Node& script) {
  std::string type = ToLowerAscii(script.GetAttribute("type"));
  return type.empty() || type == "text/javascript" || type == "application/javascript" ||
         type == "text/ecmascript" || type == "application/ecmascript" || type == "text/jscript";
}

}  // namespace

LocalFrameImpl::LocalFrameImpl(std::string renderer_host, int renderer_port,
                               std::string frame_id)
    : host_(std::move(renderer_host)), port_(renderer_port), frame_id_(std::move(frame_id)) {}

void LocalFrameImpl::Navigate(
    const std::string& url,
    base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback) {
  ParsedUrl parsed;
  if (!ParseUrl(url, &parsed)) {
    callback(false, "", 0, 0, "invalid url: " + url);
    return;
  }
  if (parsed.scheme != "http") {
    callback(false, "", 0, 0,
             "scheme '" + parsed.scheme + "' not supported (no TLS in this stack yet)");
    return;
  }

  HttpResult http = HttpGet(parsed.host, parsed.port, parsed.path);
  if (!http.connected) {
    callback(false, "", 0, 0, "fetch failed: " + http.error);
    return;
  }
  if (http.status != 200) {
    callback(false, "", 0, 0, "fetch HTTP " + std::to_string(http.status));
    return;
  }

  document_ = ParseHtml(http.body);
  document_.base_url = url;
  document_.focused = nullptr;
  html_kind_ = true;
  html_src_ = http.body;
  CommitDocument();
  // Fixed placeholder viewport -- layout itself is real (blink/layout.h);
  // the size is still a constant until a caller can resize the frame.
  callback(true, document_.Title(), viewport_w_, viewport_h_, "");
}

void LocalFrameImpl::CaptureSnapshot(
    base::OnceCallback<void(bool, std::string, std::string)> callback) {
  if (html_kind_ && HtmlRaster::Get().HasWebCore() && !html_src_.empty()) {
    HtmlFrame f = HtmlRaster::Get().PaintHTML(html_src_, document_.base_url, viewport_w_,
                                              viewport_h_);
    callback(f.ok, f.png, f.error);
    return;
  }
  PaintResult result = PaintDocument(document_, viewport_w_);
  callback(result.ok, result.png, result.error);
}

void LocalFrameImpl::CaptureFrame(
    base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback) {
  if (html_kind_ && HtmlRaster::Get().HasWebCore() && !html_src_.empty()) {
    HtmlFrame f = HtmlRaster::Get().PaintHTML(html_src_, document_.base_url, viewport_w_,
                                              viewport_h_);
    callback(f.ok, std::string(f.rgba.begin(), f.rgba.end()), f.width, f.height, f.error);
    return;
  }
  RawFrameResult result = CaptureRawFrame(document_, viewport_w_, viewport_h_);
  callback(result.ok, std::string(result.rgba.begin(), result.rgba.end()), result.width,
          result.height, result.error);
}

namespace {

constexpr char kGkSep = '\x1f';

void PopUtf8(std::string* s) {
  if (!s || s->empty()) return;
  size_t i = s->size();
  do {
    --i;
  } while (i > 0 && (static_cast<unsigned char>((*s)[i]) & 0xC0) == 0x80);
  s->resize(i);
}

bool IsTextField(const Node* n) {
  return n && n->type == NodeType::kElement &&
         (n->tag_name == "input" || n->tag_name == "textarea");
}

std::string PackGk(const std::string& event, const std::string& payload) {
  if (payload.empty()) return event;
  return event + kGkSep + payload;
}

std::string GkEventName(const Node* n) {
  if (!n) return {};
  static const char* kKeys[] = {"gk-click", "gk-action", "gk-submit", "gk-change",
                                "gk-input", "gk-dblclick", "gk-keydown"};
  for (const char* k : kKeys) {
    std::string v = n->GetAttribute(k);
    if (!v.empty()) return v;
  }
  return {};
}

Node* UrlField(HtmlDocument* doc, const std::string& for_id) {
  if (!doc || !doc->root) return nullptr;
  if (!for_id.empty()) {
    if (Node* n = doc->root->FindById(for_id)) return n;
  }
  if (Node* n = doc->root->FindById("omnibox")) return n;
  if (Node* n = doc->root->FindById("url")) return n;
  return nullptr;
}

std::string GkPayload(const Node* n, HtmlDocument* doc, const std::string& ev) {
  if (!n) return {};
  std::string url = n->GetAttribute("data-url");
  if (url.empty()) url = n->GetAttribute("href");
  if (!url.empty()) return url;
  std::string for_id = n->GetAttribute("gk-for");
  if (Node* field = UrlField(doc, for_id)) {
    if (ev == "go" || ev == "nav" || !for_id.empty()) {
      return field->GetAttribute("value");
    }
  }
  return {};
}

bool IsUrlField(const Node* n) {
  if (!IsTextField(n)) return false;
  std::string id = n->GetAttribute("id");
  return id == "omnibox" || id == "url";
}

Node* MutableHit(HtmlDocument* doc, const Node* hit) {
  if (!doc || !doc->root || !hit) return nullptr;
  if (hit->type == NodeType::kElement && !hit->GetAttribute("id").empty()) {
    return doc->root->FindById(hit->GetAttribute("id"));
  }
  // Identity walk: HitTest returns a pointer into `doc`'s own tree.
  return const_cast<Node*>(hit);
}

// UniShell page channel (WrapPageDocument) marks lifted http(s) with
// <base href="https://…">. Paint-only: parse + layout, no bruja_dom /
// classic scripts here — wasmtty/sandbox owns JS on the page channel.
bool LooksLikeLiftedPageHtml(const std::string& html) {
  std::string low = html;
  for (char& c : low) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return low.find("<base href=\"http://") != std::string::npos ||
         low.find("<base href=\"https://") != std::string::npos ||
         low.find("name=\"bruja-paint-only\"") != std::string::npos;
}

}  // namespace

void LocalFrameImpl::HandleClick(
    uint32_t x, uint32_t y, base::OnceCallback<void(bool, std::string, std::string)> callback) {
  const Node* hit = HitTestDocument(document_, viewport_w_, static_cast<float>(x),
                                    static_cast<float>(y));
  if (!hit) {
    document_.focused = nullptr;
    callback(false, "", "");
    return;
  }
#ifdef BLINK_HAS_DOM_BINDINGS
  (void)node_map_;
#endif
  document_.focused = nullptr;
  for (const Node* n = hit; n; n = n->parent) {
    if (IsTextField(n)) {
      document_.focused = MutableHit(&document_, n);
      break;
    }
  }
  for (const Node* n = hit; n; n = n->parent) {
    if (n->type != NodeType::kElement) continue;
    std::string value = GkEventName(n);
    if (value.empty()) continue;
    callback(true, PackGk(value, GkPayload(n, &document_, value)), "");
    return;
  }
  for (const Node* n = hit; n; n = n->parent) {
    if (n->type != NodeType::kElement) continue;
    std::string href = n->GetAttribute("href");
    if (href.empty()) continue;
    callback(true, PackGk("go", href), "");
    return;
  }
  callback(true, "", "");
}

void LocalFrameImpl::HandleWheel(uint32_t x, uint32_t y, int32_t delta_x, int32_t delta_y,
                                 base::OnceCallback<void(bool, std::string)> callback) {
  document_.viewport_height = static_cast<float>(viewport_h_);
  const bool handled = WheelScrollDocument(&document_, viewport_w_, viewport_h_,
                                           static_cast<float>(x), static_cast<float>(y),
                                           static_cast<float>(delta_x),
                                           static_cast<float>(delta_y));
  callback(handled, "");
}

void LocalFrameImpl::SetViewport(uint32_t width, uint32_t height) {
  viewport_w_ = width < 64 ? 64 : width;
  viewport_h_ = height < 64 ? 64 : height;
#ifdef BLINK_HAS_DOM_BINDINGS
  // window.innerWidth/innerHeight otherwise never reflect a real
  // SetViewport call -- WindowImpl's own inner_width_/inner_height_
  // default to a hardcoded 1024x768 and nothing wired them to the real
  // viewport at all before this. Only meaningful once EnsureDomInstalled
  // has run (dom_window_ constructed); a SetViewport before the first
  // script/Eval is instead picked up by EnsureDomInstalled itself, below.
  if (dom_window_) {
    dom_window_->SetInnerWidth(static_cast<int32_t>(viewport_w_));
    dom_window_->SetInnerHeight(static_cast<int32_t>(viewport_h_));
    // EnsureDomInstalled's globalThis property copy (see its own comment)
    // is a one-time VALUE snapshot, not a live binding to win's own
    // getters -- push the updated values there too so window.innerWidth/
    // innerHeight (and bare innerWidth/innerHeight, aliased the same way)
    // reflect a *later* SetViewport, not just whatever the value was at
    // install time.
    wasmv16::Engine* eng = JsEngine();
    v8::Isolate* isolate = eng->isolate();
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(eng->context());
    v8::Local<v8::Object> global = eng->context()->Global();
    global->Set(isolate, "innerWidth", v8::Number::New(isolate, static_cast<double>(viewport_w_)));
    global->Set(isolate, "innerHeight",
               v8::Number::New(isolate, static_cast<double>(viewport_h_)));
  }
#endif
}

void LocalFrameImpl::HandleKey(
    uint32_t vk, uint32_t mods, const std::string& text,
    base::OnceCallback<void(bool, std::string, std::string, std::string)> callback) {
  const uint32_t kAlt = 1, kCtrl = 2;
  if (mods & kCtrl) {
    if (vk == 'T' || vk == 't') {
      callback(true, "newtab", "", "");
      return;
    }
    if (vk == 'W' || vk == 'w') {
      callback(true, "close-active", "", "");
      return;
    }
    if (vk == 'R' || vk == 'r') {
      callback(true, "reload", "", "");
      return;
    }
    if (vk == 'D' || vk == 'd') {
      callback(true, "bookmark", "", "");
      return;
    }
    if (vk == 'L' || vk == 'l') {
      if (document_.root) {
        Node* url = UrlField(&document_, "");
        if (!url) url = document_.root->FindFirstElement("input");
        document_.focused = url;
      }
      callback(true, "focus-url", "", "");
      return;
    }
  }
  if (mods & kAlt) {
    if (vk == 0x25) {  // VK_LEFT
      callback(true, "back", "", "");
      return;
    }
    if (vk == 0x27) {  // VK_RIGHT
      callback(true, "fwd", "", "");
      return;
    }
  }
  if (vk == 0x74) {  // F5
    callback(true, "reload", "", "");
    return;
  }

  Node* field = document_.focused;
  if (!IsTextField(field)) {
    callback(false, "", "", "");
    return;
  }
  std::string& value = field->attributes["value"];
  if (vk == 0x08) {  // VK_BACK
    PopUtf8(&value);
    callback(true, "", value, "");
    return;
  }
  if (vk == 0x2E) {  // VK_DELETE — treat as backspace (no caret index yet)
    PopUtf8(&value);
    callback(true, "", value, "");
    return;
  }
  if (vk == 0x0D) {  // VK_RETURN
    std::string submit = field->GetAttribute("gk-submit");
    if (submit.empty() && IsUrlField(field)) submit = "go";
    if (submit.empty()) submit = field->GetAttribute("gk-keydown");
    if (submit.empty()) submit = "submit";
    callback(true, PackGk(submit, value), value, "");
    return;
  }
  if (!text.empty()) {
    unsigned char c = static_cast<unsigned char>(text[0]);
    if (c >= 32 || text.size() > 1) {
      value += text;
      callback(true, "", value, "");
      return;
    }
  }
  callback(true, "", value, "");
}

void LocalFrameImpl::LoadHTML(
    const std::string& html,
    base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback) {
  document_ = ParseHtml(html);
  document_.base_url.clear();
  if (document_.root) {
    std::function<void(const Node&)> walk = [&](const Node& n) {
      if (n.type == NodeType::kElement && n.tag_name == "base") {
        std::string href = n.GetAttribute("href");
        if (!href.empty()) document_.base_url = href;
      }
      for (const auto& child : n.children) walk(*child);
    };
    walk(*document_.root);
  }
  document_.focused = nullptr;
  html_kind_ = true;
  html_src_ = html;
  if (!LooksLikeLiftedPageHtml(html)) CommitDocument();
  callback(true, document_.Title(), viewport_w_, viewport_h_, "");
}

void LocalFrameImpl::LoadGML(
    const std::string& gml, const std::string& data_json,
    base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback) {
  std::string err;
  std::string dml_json;
  const std::string clean = SplitLoadGmlData(data_json, &dml_json);
  HtmlDocument doc = ParseGml(gml, clean, &err);
  if (!doc.root) {
    callback(false, "", 0, 0, err.empty() ? "ParseGml failed" : err);
    return;
  }
  ApplyHtmlDml(&doc, dml_json);
  document_ = std::move(doc);
  document_.focused = nullptr;
  html_kind_ = false;
  html_src_.clear();
  CommitDocument();
  callback(true, document_.Title(), viewport_w_, viewport_h_, "");
}

#ifdef BLINK_HAS_IMAGE_DECODE
namespace {

// RFC 4648 base64, tolerant of embedded whitespace/newlines (real
// data: URIs are sometimes hand-wrapped) and '=' padding. Returns
// std::nullopt on any non-alphabet byte -- this stack's URL parser
// already documents "no percent-decoding" as an intentional
// simplification (blink/url.h), so an unencoded data: payload that isn't
// pure base64 is out of scope the same way.
std::optional<std::string> DecodeBase64(const std::string& in) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int val = 0, valb = -8;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    int d = value(c);
    if (d < 0) return std::nullopt;
    val = (val << 6) + d;
    valb += 6;
    if (valb >= 0) {
      out += static_cast<char>((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return out;
}

// "data:[<mediatype>][;base64],<data>" (RFC 2397). Only the ";base64"
// case actually decodes bytes -- a bare percent-encoded data: URI would
// need this stack's URL parser to percent-decode first, which it
// explicitly doesn't (blink/url.h); real image data: URIs are base64 in
// practice anyway (raw text in a URL can't hold arbitrary binary).
std::optional<std::string> DecodeDataUrl(const std::string& src) {
  if (src.rfind("data:", 0) != 0) return std::nullopt;
  auto comma = src.find(',');
  if (comma == std::string::npos) return std::nullopt;
  std::string header = src.substr(5, comma - 5);
  std::string payload = src.substr(comma + 1);
  if (header.find(";base64") == std::string::npos) return std::nullopt;
  return DecodeBase64(payload);
}

}  // namespace
#endif  // BLINK_HAS_IMAGE_DECODE

void LocalFrameImpl::FetchSubresources() {
  if (!document_.root) return;
  document_.images.clear();
  document_.images_by_url.clear();
  std::function<void(Node&)> walk = [&](Node& n) {
    if (n.type == NodeType::kElement && n.tag_name == "img") {
      std::string src = n.GetAttribute("src");
      if (!src.empty()) {
#ifdef BLINK_HAS_IMAGE_DECODE
        if (auto bytes = DecodeDataUrl(src)) {
          DecodedImage img;
          if (!bytes->empty() &&
              wasmskia::DecodeImage(reinterpret_cast<const uint8_t*>(bytes->data()),
                                    bytes->size(), &img.rgba, &img.width, &img.height)) {
            document_.images[&n] = img;
            document_.images_by_url[src] = img;
          }
          for (auto& c : n.children) walk(*c);
          return;
        }
#endif
        std::string url =
            document_.base_url.empty() ? src : ResolveUrl(document_.base_url, src);
        ParsedUrl parsed;
        if (ParseUrl(url, &parsed) && parsed.scheme == "http") {
          HttpResult http = HttpGet(parsed.host, parsed.port, parsed.path);
#ifdef BLINK_HAS_IMAGE_DECODE
          if (http.connected && http.status == 200) {
            DecodedImage img;
            if (wasmskia::DecodeImage(reinterpret_cast<const uint8_t*>(http.body.data()),
                                      http.body.size(), &img.rgba, &img.width, &img.height)) {
              document_.images[&n] = img;
              document_.images_by_url[url] = img;
            }
          }
#else
          (void)http;
#endif
        }
      }
    }
    for (auto& c : n.children) walk(*c);
  };
  walk(*document_.root);
}

void LocalFrameImpl::CommitDocument() {
  FetchSubresources();
#ifdef BLINK_HAS_DOM_BINDINGS
  RebuildDomTree();
#endif
#ifdef BLINK_HAS_WASMV16_ENGINE
  RunClassicScripts();
#endif
}

#ifdef BLINK_HAS_WASMV16_ENGINE

void LocalFrameImpl::RunClassicScripts() {
#ifdef BLINK_HAS_DOM_BINDINGS
  EnsureDomInstalled();
  t_frame_for_js = this;
#else
  // Even without bruja_dom, Blink's window is globalThis -- so classic
  // scripts and bind() glue that assign `window.foo` create a real global.
  {
    wasmv16::Engine* eng = JsEngine();
    v8::Isolate* isolate = eng->isolate();
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(eng->context());
    v8::Local<v8::Object> global = eng->context()->Global();
    global->Set(isolate, "window", global);
  }
#endif
#ifdef BLINK_HAS_WASMTTY_BINDINGS
  EnsureWasmttyInstalled();
#endif
  if (!document_.root) return;
  std::vector<const Node*> scripts;
  CollectScripts(*document_.root, &scripts);
  std::vector<const Node*> deferred, async_scripts;
  for (const Node* script : scripts) {
    if (!IsJavascriptMime(*script)) continue;
    if (ToLowerAscii(script->GetAttribute("type")) == "module") continue;
    bool has_src = script->attributes.count("src") != 0;
    if (has_src && script->attributes.count("async")) {
      async_scripts.push_back(script);
      continue;
    }
    if (has_src && script->attributes.count("defer")) {
      deferred.push_back(script);
      continue;
    }
    RunOneScript(*script);
  }
  for (const Node* script : deferred) RunOneScript(*script);
  for (const Node* script : async_scripts) RunOneScript(*script);
#ifdef BLINK_HAS_DOM_BINDINGS
  t_frame_for_js = nullptr;
#endif
}

void LocalFrameImpl::RunOneScript(const Node& script) {
  std::string source;
  if (script.attributes.count("src")) {
    std::string url = script.GetAttribute("src");
    if (!document_.base_url.empty()) url = ResolveUrl(document_.base_url, url);
    ParsedUrl parsed;
    if (!ParseUrl(url, &parsed) || parsed.scheme != "http") return;
    HttpResult http = HttpGet(parsed.host, parsed.port, parsed.path);
    if (!http.connected || http.status != 200) return;
    source = std::move(http.body);
  } else {
    source = script.TextContent();
  }
  if (source.empty()) return;
  v8::Local<v8::Value> value;
  std::string error;
  wasmv16::Engine* eng = JsEngine();
  if (!eng->Eval(source, "<script>", &value, &error)) return;
  eng->RunPendingJobs();
}

#endif  // BLINK_HAS_WASMV16_ENGINE

#ifdef BLINK_HAS_DOM_BINDINGS

namespace {

// Mirrors one parsed blink::Node into a real bruja_dom_generated node,
// owned by `doc` (see DocumentImpl::CreateElement/CreateTextNode) and
// wired into the tree being built. Parent pointers are set through
// NodeImplBase (see dom_impl.h) since we hold abstract Node*/Element*
// pointers here, not the concrete leaf types DocumentImpl's factory
// picked.
bruja_dom_generated::Node* BuildDomNode(
    bruja_dom_generated::DocumentImpl* doc, const blink::Node& parsed,
    std::unordered_map<const blink::Node*, bruja_dom_generated::Node*>* map) {
  bruja_dom_generated::Node* built;
  if (parsed.type == NodeType::kText) {
    built = doc->CreateTextNode(parsed.text_data);
  } else {
    bruja_dom_generated::ElementCreationOptions options;
    bruja_dom_generated::Element* el = doc->CreateElement(parsed.tag_name, options);
    for (const auto& kv : parsed.attributes) el->SetAttribute(kv.first, kv.second);
    built = el;
  }
  if (map) (*map)[&parsed] = built;
  for (const auto& child : parsed.children) {
    bruja_dom_generated::Node* child_built = BuildDomNode(doc, *child, map);
    built->AppendChild(child_built);
    if (auto* base = dynamic_cast<bruja_dom_generated::NodeImplBase*>(child_built)) {
      base->SetParentPtr(built);
    }
  }
  return built;
}

// Same rule as css.cc's (file-local) CollectStyleText: concatenate every
// <style> element's text, document order. getComputedStyle cascades
// against the sheet this builds, the same way InlineComputedCss does.
void CollectStyleText(const blink::Node& node, std::string* out) {
  if (node.type == NodeType::kElement && node.tag_name == "style") {
    *out += node.TextContent();
    *out += "\n";
    return;
  }
  for (const auto& child : node.children) CollectStyleText(*child, out);
}

std::string FormatComputedColor(uint32_t c) {
  unsigned a = (c >> 24) & 0xffu;
  unsigned r = (c >> 16) & 0xffu;
  unsigned g = (c >> 8) & 0xffu;
  unsigned b = c & 0xffu;
  char buf[48];
  if (a >= 255) {
    std::snprintf(buf, sizeof(buf), "rgb(%u, %u, %u)", r, g, b);
  } else {
    std::snprintf(buf, sizeof(buf), "rgba(%u, %u, %u, %.3g)", r, g, b, a / 255.0f);
  }
  return buf;
}

// getComputedStyle's width/height are supposed to be the *used* value from
// layout (percent/auto resolved against the real containing block) -- this
// resolves against the viewport instead, since no layout pass runs here
// (ComputeLayout needs BLINK_HAS_PAINT_PIPELINE's real fonts). Honest for
// what this corpus actually needs (an explicit px value); wrong for
// percent/auto until getComputedStyle has a real containing-block width to
// resolve against (same gap CaptureSnapshot's real layout pass would close).
std::string FormatComputedLength(const Length& l, float viewport_w, float viewport_h,
                                 float font_size) {
  if (l.is_auto()) return "auto";
  float px = l.Resolve(viewport_w, 0.0f, font_size, 16.0f, viewport_w, viewport_h);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.0fpx", px);
  return buf;
}

}  // namespace

// Minimal CSSStyleDeclaration stand-in: a plain object with `color`/
// `width`/`height` set, not a live view backed by getPropertyValue()/
// property indices. Cascades against current_sheet_ by walking el's
// ancestor chain root-to-target so inherited properties (color included)
// resolve the same way ApplyTree/BakeFlexRowWidths (css.cc) compute them
// for layout/paint -- getComputedStyle was previously entirely unwired:
// no binding existed at all, so `window.getComputedStyle` was `undefined`.
// More properties (display, fontSize, ...) are cheap to add the same way
// once needed; width/height are honest only for an explicit px value (see
// FormatComputedLength's own comment on the percent/auto gap).
void LocalFrameImpl::JsGetComputedStyle(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Object> result = v8::Object::New(isolate);
  info.GetReturnValue().Set(result);
  if (!t_frame_for_js || info.Length() < 1 || !info[0]->IsObject()) return;

  // getElementById/querySelector/etc. now hand back whichever tag-specific
  // wrapper WrapElementForJs picked (HTMLImageElement, HTMLDivElement,
  // ...), not always the generic Element one -- UnwrapElementFromJs tries
  // all of them (see its own comment in dom_v8_impl.h).
  auto* el = bruja_dom_generated::UnwrapElementFromJs(isolate, info[0]);
  if (!el) return;

  auto it = t_frame_for_js->reverse_node_map_.find(el);
  if (it == t_frame_for_js->reverse_node_map_.end()) return;

  std::vector<const blink::Node*> chain;
  for (const blink::Node* n = it->second; n != nullptr; n = n->parent) chain.push_back(n);
  std::reverse(chain.begin(), chain.end());

  ComputedStyle style;
  const ComputedStyle* parent = nullptr;
  for (const blink::Node* n : chain) {
    if (n->type != NodeType::kElement) continue;
    style = ComputeStyle(*n, t_frame_for_js->current_sheet_, parent);
    parent = &style;
  }

  result->Set(isolate, "color",
              v8::String::NewFromUtf8(isolate, FormatComputedColor(style.color).c_str())
                  .ToLocalChecked());
  result->Set(isolate, "width",
              v8::String::NewFromUtf8(isolate,
                                      FormatComputedLength(style.width,
                                                           t_frame_for_js->current_sheet_.viewport_w,
                                                           t_frame_for_js->current_sheet_.viewport_h,
                                                           style.font_size)
                                          .c_str())
                  .ToLocalChecked());
  result->Set(isolate, "height",
              v8::String::NewFromUtf8(isolate,
                                      FormatComputedLength(style.height,
                                                           t_frame_for_js->current_sheet_.viewport_w,
                                                           t_frame_for_js->current_sheet_.viewport_h,
                                                           style.font_size)
                                          .c_str())
                  .ToLocalChecked());
}

void LocalFrameImpl::RebuildDomTree() {
  dom_document_.Reset();
  node_map_.clear();
  reverse_node_map_.clear();
  if (!document_.root) return;
  for (const auto& top_level : document_.root->children) {
    bruja_dom_generated::Node* built = BuildDomNode(&dom_document_, *top_level, &node_map_);
    dom_document_.children.push_back(built);
    if (auto* base = dynamic_cast<bruja_dom_generated::NodeImplBase*>(built)) {
      base->SetParentPtr(&dom_document_);
    }
    if (built->NodeType() == bruja_dom_generated::Node::ELEMENT_NODE) {
      auto* el = static_cast<bruja_dom_generated::Element*>(built);
      if (dom_document_.document_element == nullptr) dom_document_.document_element = el;
      std::string tag = el->TagName();
      if (tag == "BODY") dom_document_.body = static_cast<bruja_dom_generated::HTMLElement*>(el);
      if (tag == "HEAD") dom_document_.head = static_cast<bruja_dom_generated::HTMLElement*>(el);
    }
  }
  dom_document_.SetTitle(document_.Title());
  for (const auto& kv : node_map_) reverse_node_map_[kv.second] = kv.first;

#ifdef BLINK_HAS_IMAGE_DECODE
  // FetchSubresources (CommitDocument runs it before this) already
  // decoded real bytes for any <img> whose src it could fetch/decode
  // (http:// or data:;base64) into document_.images, keyed by the same
  // parsed blink::Node* node_map_ maps to a DOM element -- mirror those
  // real dimensions onto naturalWidth/naturalHeight/complete so JS
  // (`img.naturalWidth`) sees a real decode result, not the img/width
  // HTML *attribute* (HTMLImageElementImpl::Width(), a separate field).
  for (const auto& kv : document_.images) {
    auto it = node_map_.find(kv.first);
    if (it == node_map_.end()) continue;
    if (auto* img_el = dynamic_cast<bruja_dom_generated::HTMLImageElementImpl*>(it->second)) {
      img_el->SetNaturalDimensions(static_cast<uint32_t>(kv.second.width),
                                   static_cast<uint32_t>(kv.second.height));
    }
  }
#endif

  std::string style_text;
  CollectStyleText(*document_.root, &style_text);
  current_sheet_ = ParseStylesheet(style_text);
  current_sheet_.viewport_w = viewport_w_ > 1.0f ? static_cast<float>(viewport_w_) : 800.0f;
  current_sheet_.viewport_h = viewport_h_ > 1.0f ? static_cast<float>(viewport_h_) : 600.0f;
}

void LocalFrameImpl::EnsureDomInstalled() {
  if (dom_installed_) return;
  dom_installed_ = true;
  dom_window_ = std::make_unique<bruja_dom_generated::WindowImpl>(
      &dom_document_, &dom_location_, &dom_custom_elements_, &dom_local_storage_,
      &dom_session_storage_, &dom_navigator_, &dom_console_);
  // Real initial size, not WindowImpl's own hardcoded 1024x768 default --
  // picks up any SetViewport call already made before this first
  // script/Eval runs (SetViewport itself handles the case where it's
  // called after, once dom_window_ already exists).
  dom_window_->SetInnerWidth(static_cast<int32_t>(viewport_w_));
  dom_window_->SetInnerHeight(static_cast<int32_t>(viewport_h_));
  wasmv16::Engine* eng = JsEngine();
  v8::Isolate* isolate = eng->isolate();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = eng->context();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Object> win =
      bruja_dom_generated::CreateWindowBinding(isolate, context, dom_window_.get());
  // Blink: Window is the global object (window === globalThis). Copy the
  // Window binding's own properties onto globalThis (getters run with
  // this=win, so opaque lookups succeed), then alias window to globalThis
  // so `window.foo = 1` and a bare `foo` are the same binding -- required
  // for classic <script> globals and webview::bind() glue. The facade has
  // no Object::GetOwnPropertyNames() yet, so this reaches past it into raw
  // quickjs -- the same documented escape hatch sequence<T>/Promise<T>
  // conversions use in brujac's V8-backend codegen (see
  // WASMBruja/README.md's "V8 backend" section).
  JSContext* ctx = context->context_for_wasmv8_internal();
  JSValue raw_win = win->value_for_wasmv8_internal();
  JSValue raw_global = global->value_for_wasmv8_internal();
  JSPropertyEnum* tab = nullptr;
  uint32_t len = 0;
  if (JS_GetOwnPropertyNames(ctx, &tab, &len, raw_win, JS_GPN_STRING_MASK) == 0) {
    for (uint32_t i = 0; i < len; ++i) {
      JSValue val = JS_GetProperty(ctx, raw_win, tab[i].atom);
      JS_SetProperty(ctx, raw_global, tab[i].atom, val);
    }
    JS_FreePropertyEnum(ctx, tab, len);
  }
  global->Set(isolate, "document",
             bruja_dom_generated::CreateDocumentBinding(isolate, context, &dom_document_));
  global->Set(isolate, "window", global);

  v8::Local<v8::FunctionTemplate> gcs_tmpl =
      v8::FunctionTemplate::New(isolate, JsGetComputedStyle);
  global->Set(isolate, "getComputedStyle", gcs_tmpl->GetFunction(context).ToLocalChecked());
}

namespace {

void HtmlEscapeAttr(std::string* out, const std::string& s) {
  for (char c : s) {
    switch (c) {
      case '&': *out += "&amp;"; break;
      case '"': *out += "&quot;"; break;
      case '<': *out += "&lt;"; break;
      default: *out += c; break;
    }
  }
}

void HtmlEscapeText(std::string* out, const std::string& s) {
  for (char c : s) {
    switch (c) {
      case '&': *out += "&amp;"; break;
      case '<': *out += "&lt;"; break;
      default: *out += c; break;
    }
  }
}

std::string LowerTag(std::string tag) {
  for (char& c : tag) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return tag;
}

void SerializeBrujaNode(bruja_dom_generated::Node* node, std::string* out);

void SerializeBrujaElement(bruja_dom_generated::Element* el, std::string* out) {
  std::string tag = LowerTag(el->TagName());
  *out += '<';
  *out += tag;
  for (const std::string& name : el->GetAttributeNames()) {
    auto val = el->GetAttribute(name);
    if (!val) continue;
    *out += ' ';
    *out += name;
    *out += "=\"";
    HtmlEscapeAttr(out, *val);
    *out += '"';
  }
  *out += '>';
  if (tag == "script" || tag == "style") {
    for (bruja_dom_generated::Node* child : el->ChildNodes()) {
      if (child->NodeType() == bruja_dom_generated::Node::TEXT_NODE) {
        if (auto tc = child->TextContent(); tc && !tc->empty()) *out += *tc;
      } else {
        SerializeBrujaNode(child, out);
      }
    }
    *out += "</";
    *out += tag;
    *out += '>';
    return;
  }
  if (auto tc = el->TextContent(); tc && !tc->empty()) {
    HtmlEscapeText(out, *tc);
  } else {
    for (bruja_dom_generated::Node* child : el->ChildNodes()) SerializeBrujaNode(child, out);
  }
  if (tag == "br" || tag == "img" || tag == "meta" || tag == "link" || tag == "input" ||
      tag == "hr") {
    return;
  }
  *out += "</";
  *out += tag;
  *out += '>';
}

void SerializeBrujaNode(bruja_dom_generated::Node* node, std::string* out) {
  if (!node) return;
  if (node->NodeType() == bruja_dom_generated::Node::TEXT_NODE) {
    if (auto tc = node->TextContent(); tc && !tc->empty()) HtmlEscapeText(out, *tc);
    return;
  }
  if (node->NodeType() == bruja_dom_generated::Node::ELEMENT_NODE) {
    SerializeBrujaElement(static_cast<bruja_dom_generated::Element*>(node), out);
  }
}

}  // namespace

std::string LocalFrameImpl::LiveHtml() {
  if (!dom_installed_ || !dom_document_.document_element) return {};
  std::string out = "<!DOCTYPE html>";
  SerializeBrujaElement(dom_document_.document_element, &out);
  return out;
}

#endif  // BLINK_HAS_DOM_BINDINGS

#ifdef BLINK_HAS_WASMTTY_BINDINGS

void LocalFrameImpl::EnsureWasmttyInstalled() {
  if (wasmtty_installed_) return;
  wasmtty_installed_ = true;
  wasmv16::Engine* eng = JsEngine();
  v8::Isolate* isolate = eng->isolate();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = eng->context();
  v8::Context::Scope context_scope(context);
  // wasmtty's own binding installer predates this facade and still takes a
  // raw quickjs JSContext*/JSValue global -- no other changes needed on its
  // side (see WASMv16/wasmtty), only how we hand it the global object here.
  JSContext* ctx = context->context_for_wasmv8_internal();
  JSValue global = JS_GetGlobalObject(ctx);
  wasmtty::InstallBindings(ctx, global);
  JS_FreeValue(ctx, global);
}

#endif  // BLINK_HAS_WASMTTY_BINDINGS

#ifdef BLINK_HAS_WASMV16_ENGINE

namespace {

// JSON.stringify(value) via the engine's own global, rather than a
// second string-wrapped eval -- avoids re-running (and double-executing
// any side effect of) the original expression just to serialize its
// result. Matches local_frame.voodoom's documented contract: an empty
// string for `undefined` (including when JSON.stringify itself would
// return undefined, e.g. a function or symbol value).
std::string JsonStringify(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> value) {
  if (value->IsUndefined()) return "";
  v8::Local<v8::Value> json_value;
  if (!context->Global()->Get(isolate, "JSON").ToLocal(&json_value) ||
      !json_value->IsObject()) {
    return "";
  }
  v8::Local<v8::Object> json_ns = json_value.As<v8::Object>();
  v8::Local<v8::Value> stringify_value;
  if (!json_ns->Get(isolate, "stringify").ToLocal(&stringify_value) ||
      !stringify_value->IsFunction()) {
    return "";
  }
  v8::Local<v8::Function> stringify = stringify_value.As<v8::Function>();
  v8::Local<v8::Value> args[] = {value};
  v8::Local<v8::Value> result;
  if (!stringify->Call(context, json_ns, 1, args).ToLocal(&result) ||
      result->IsUndefined()) {
    return "";
  }
  v8::String::Utf8Value text(isolate, result);
  return text.length() > 0 ? std::string(*text, static_cast<size_t>(text.length())) : "";
}

}  // namespace

void LocalFrameImpl::Eval(
    const std::string& js,
    base::OnceCallback<void(bool, std::string, std::string)> callback) {
#ifdef BLINK_HAS_DOM_BINDINGS
  EnsureDomInstalled();
  t_frame_for_js = this;
#else
  {
    wasmv16::Engine* eng = JsEngine();
    v8::Isolate* isolate = eng->isolate();
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(eng->context());
    v8::Local<v8::Object> global = eng->context()->Global();
    global->Set(isolate, "window", global);
  }
#endif
  // Wrapped in parens so a bare object-literal expression (`{a: 1}`)
  // parses as an expression rather than a block statement -- the same
  // ambiguity a d8-style REPL has to work around.
  v8::Local<v8::Value> value;
  std::string error;
  wasmv16::Engine* eng = JsEngine();
  if (!eng->Eval("(" + js + ")", "<eval>", &value, &error)) {
#ifdef BLINK_HAS_DOM_BINDINGS
    t_frame_for_js = nullptr;
#endif
    callback(false, "", error);
    return;
  }
  v8::Isolate* isolate = eng->isolate();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(eng->context());
  std::string result_json = JsonStringify(isolate, eng->context(), value);
  eng->RunPendingJobs();
#ifdef BLINK_HAS_DOM_BINDINGS
  t_frame_for_js = nullptr;
#endif
  callback(true, result_json, "");
}

#else

void LocalFrameImpl::Eval(
    const std::string& js,
    base::OnceCallback<void(bool, std::string, std::string)> callback) {
  (void)js;
  callback(false, "", "Eval: no native JS engine wired yet");
}

#endif  // BLINK_HAS_WASMV16_ENGINE

}  // namespace blink
