#ifndef BLINK_LOCAL_FRAME_IMPL_H_
#define BLINK_LOCAL_FRAME_IMPL_H_

#include <cstdint>
#include <functional>
#include <string>

#include "base/callback.h"
#include "blink/css.h"
#include "blink/dom.h"
#include "local_frame_interface_gen.h"
#include "blink/html_raster.h"

#ifdef BLINK_HAS_WASMV16_ENGINE
#include "wasmv16/engine.h"
#endif

#ifdef BLINK_HAS_DOM_BINDINGS
// V8-backend-generated DOM (bruja_dom_v8, ../WASMBruja's dom_v8_impl.h --
// same bruja_dom_generated:: classes as the original quickjs-backend
// dom_impl.h, compiling unchanged against dom_v8_gen.h; see
// WASMBruja/README.md's "V8 backend" section) -- unified onto the same
// WASMv8bindings facade as js_engine_ below, rather than the raw
// JSContext*/JSValue the quickjs-backend bruja_dom used.
#include "bruja_dom/dom_v8_impl.h"

#include <memory>
#include <unordered_map>
#include <vector>
#endif

#ifdef BLINK_HAS_WASMTTY_BINDINGS
#include "wasmtty/bindings.h"
#endif

namespace blink {

// The real LocalFrame -- fully native, no loki-closure/Cadmium dependency
// anywhere in this class. Navigate() does a real native HTTP GET of the
// target URL (blink/http_client.cc, no TLS -- see Navigate's own comment)
// and parses the response with blink/html_parser.h; LoadHTML() parses the
// given source directly. Both then run classic inline <script> tags in
// document order (Blink HTML parser behavior for parser-blocking scripts
// -- see RunClassicScripts), report `title` for real (the parsed <title>
// element's text), and report a fixed 1024x768 viewport (the layout
// engine in blink/layout.h is real; the viewport size itself is still a
// placeholder until a caller can resize the frame). CaptureSnapshot and
// Eval are each real when built with their respective optional siblings
// present: CaptureSnapshot needs ../WASMSkia + ../WASMRasta
// (BLINK_HAS_PAINT_PIPELINE, see blink/paint.h); Eval needs ../WASMv16
// (BLINK_HAS_WASMV16_ENGINE) -- a genuine quickjs-ng evaluation,
// JSON-stringifying the result. Without the respective sibling, each is
// an honest stub (ok=false).
//
// When built with BOTH ../WASMv16 and ../WASMBruja present
// (BLINK_HAS_DOM_BINDINGS), Eval() also gets a real `window`/`document`
// DOM (../WASMBruja's bruja_dom library -- see its dom_impl.h) mirroring
// whatever Navigate/LoadHTML most recently parsed, with real
// window.localStorage/sessionStorage that persist across navigations
// within this LocalFrameImpl's lifetime (in-memory only -- see
// StorageImpl's own doc comment for the persistence gap). Classic
// <script> tags see that same DOM. Without WASMBruja, Eval and inline
// scripts still run (given WASMv16 alone) but see no DOM at all -- bare
// quickjs-ng globals only.
class LocalFrameImpl : public LocalFrame {
 public:
  LocalFrameImpl(std::string renderer_host, int renderer_port, std::string frame_id);

#ifdef BLINK_HAS_WASMV16_ENGINE
  // Chrome-only opt-in. When set, this process's LocalFrameImpls eval on
  // the embedder's privileged JSContext (Mojo.createLocalFramePipe,
  // processes, rmlui). Page frames must NOT set this — they keep a private
  // engine with the page JS layer (Mojo echo, not the chrome global).
  static void SetSharedEngine(wasmv16::Engine* engine);
#endif

  void Navigate(
      const std::string& url,
      base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback)
      override;

  void CaptureSnapshot(
      base::OnceCallback<void(bool, std::string, std::string)> callback) override;

  void LoadHTML(
      const std::string& html,
      base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback)
      override;

  void LoadGML(const std::string& gml, const std::string& data_json,
               base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback)
      override;

  void Eval(
      const std::string& js,
      base::OnceCallback<void(bool, std::string, std::string)> callback) override;

  void CaptureFrame(
      base::OnceCallback<void(bool, std::string, uint32_t, uint32_t, std::string)> callback) override;

  void HandleClick(
      uint32_t x, uint32_t y,
      base::OnceCallback<void(bool, std::string, std::string)> callback) override;

  void HandleKey(uint32_t vk, uint32_t mods, const std::string& text,
                 base::OnceCallback<void(bool, std::string, std::string, std::string)> callback)
      override;

  void HandleWheel(uint32_t x, uint32_t y, int32_t delta_x, int32_t delta_y,
                   base::OnceCallback<void(bool, std::string)> callback) override;

  void SetViewport(uint32_t width, uint32_t height) override;

  const HtmlDocument& document() const { return document_; }

#ifdef BLINK_HAS_DOM_BINDINGS
  // HTML after classic scripts ran against bruja_dom (not the pre-JS parse tree).
  std::string LiveHtml();
#endif

 private:
  // Commits a freshly parsed document_: fetches <img> / <script src>,
  // mirrors it into bruja_dom when that sibling is present, then runs
  // classic scripts (inline and src=, then defer, then async).
  void CommitDocument();
  void FetchSubresources();
#ifdef BLINK_HAS_WASMV16_ENGINE
  // Blink HTML parser script execution: document-order parser-blocking
  // classic scripts (inline or src=), then defer, then async. Skips
  // type=module and non-JS MIME. A throwing script does not abort the
  // rest of the document. Uses the engine's script goal, not Eval()'s
  // parenthesized-expression wrap.
  void RunClassicScripts();
  void RunOneScript(const Node& script);
#endif
  // host_/port_ kept so the constructor signature stays compatible with
  // existing call sites (WASMRenderer CreateFrame, WASMv16, WASMWebviewKernel,
  // WASMLime). Unused by Navigate/LoadHTML/Eval/paint.
  std::string host_;
  int port_;
  std::string frame_id_;
  uint32_t viewport_w_ = 1024;
  uint32_t viewport_h_ = 768;
  HtmlDocument document_;
  // HTML uses HtmlRaster (Ultralight WebCore when linked). GML stays on
  // WASMSkia layout — UniLoader's Electron split.
  bool html_kind_ = true;
  std::string html_src_;
#ifdef BLINK_HAS_DOM_BINDINGS
  void EnsureDomInstalled();
  void RebuildDomTree();
  // window.getComputedStyle(el) binding (installed in EnsureDomInstalled).
  // A member (not an anonymous-namespace free function like the .cc file's
  // other JS callbacks, e.g. WKWebViewImpl.cc's JsPostMessage) because it
  // needs reverse_node_map_/current_sheet_, both private below -- reached
  // through the thread_local t_frame_for_js the same way those callbacks
  // reach their instance, see local_frame_impl.cc's top comment on it.
  static void JsGetComputedStyle(const v8::FunctionCallbackInfo<v8::Value>& info);

  // These must be declared (and therefore destroyed, in reverse
  // declaration order) BEFORE js_engine_ below: EnsureDomInstalled()
  // installs `window`/`document` JS globals that hold opaque pointers
  // back into dom_document_/dom_window_/etc, and quickjs-ng runs
  // finalizers on those bindings when js_engine_'s JSRuntime is torn
  // down. If js_engine_ destructed first (as it did when this member
  // used to be declared above this block), those finalizers would fire
  // after the pointed-to C++ objects were already destroyed -- a
  // use-after-free that only crashed nondeterministically depending on
  // heap layout/ASLR.
  bruja_dom_generated::DocumentImpl dom_document_;
  bruja_dom_generated::LocationImpl dom_location_;
  bruja_dom_generated::CustomElementRegistryImpl dom_custom_elements_;
  bruja_dom_generated::StorageImpl dom_local_storage_;
  bruja_dom_generated::StorageImpl dom_session_storage_;
  bruja_dom_generated::NavigatorImpl dom_navigator_;
  bruja_dom_generated::ConsoleImpl dom_console_;
  std::unique_ptr<bruja_dom_generated::WindowImpl> dom_window_;
  bool dom_installed_ = false;
  std::unordered_map<const Node*, bruja_dom_generated::Node*> node_map_;
  // Reverse of node_map_ -- getComputedStyle(el) needs to go from a DOM
  // Element JS wraps back to the parsed blink::Node ComputeStyle (css.h)
  // actually understands; the two trees are otherwise unconnected.
  std::unordered_map<bruja_dom_generated::Node*, const Node*> reverse_node_map_;
  // Parsed once per CommitDocument (RebuildDomTree) from this document's
  // own <style> text, same as InlineComputedCss's own sheet -- getComputedStyle
  // cascades against it instead of re-parsing on every call.
  Stylesheet current_sheet_;
#endif
#ifdef BLINK_HAS_WASMTTY_BINDINGS
  // Installs `TTYListener`/`TTYReceiver` (see ../../WASMv16/wasmtty) --
  // needs js_engine_ below, so like EnsureDomInstalled it only runs from
  // RunClassicScripts/Eval, never from the constructor.
  void EnsureWasmttyInstalled();
  bool wasmtty_installed_ = false;
#endif
#ifdef BLINK_HAS_WASMV16_ENGINE
  wasmv16::Engine* JsEngine();
  static wasmv16::Engine* shared_engine_;
  wasmv16::Engine js_engine_;
  bool page_js_installed_ = false;
#endif
};

}  // namespace blink

#endif  // BLINK_LOCAL_FRAME_IMPL_H_
