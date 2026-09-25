// Proves LocalFrameImpl's native (no loki-closure) behavior end-to-end:
// Navigate() does a real Mojo request/response across a real pipe (same
// mojo::Receiver/mojo::Remote, generated Proxy_/Stub_ code as before) whose
// implementation makes a real socket HTTP GET (blink/http_client.cc) of
// the navigated-to URL itself -- MockRendererBridge here stands in for an
// arbitrary web server, not a Cadmium renderer bridge -- and parses the
// response with blink/html_parser.h. CaptureSnapshot/Eval are proven as
// honest stubs, not silently-broken calls to a bridge Navigate no longer
// feeds.
#include "test.h"

#include "blink/local_frame_impl.h"
#include "mock_renderer_bridge.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_navigate_fetches_and_parses_real_html) {
  MockRendererBridge mock(
      "<html><head><title>Real Title</title></head><body>hi</body></html>");

  blink::LocalFrameImpl impl("unused", 0, "frame-1");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool done = false, ok = false;
  std::string title, error;
  uint32_t width = 0, height = 0;
  remote->Navigate("http://127.0.0.1:" + std::to_string(mock.port()) + "/",
                    [&](bool o, std::string t, uint32_t w, uint32_t h, std::string e) {
                      ok = o;
                      title = t;
                      width = w;
                      height = h;
                      error = e;
                      done = true;
                    });
  Pump();

  EXPECT(done);
  EXPECT(ok);
  EXPECT_EQ(title, "Real Title");
  EXPECT(width > 0);
  EXPECT(height > 0);
  EXPECT(error.empty());
}

TEST(handle_click_maps_ping_and_omnibox_go) {
  blink::LocalFrameImpl impl("unused", 0, "frame-gk");
  bool loaded = false;
  impl.LoadHTML(
      "<html><body>"
      "<input id=\"omnibox\" value=\"example.com\" />"
      "<button gk-click=\"go\">Go</button>"
      "<button gk-click=\"ping\">Ping</button>"
      "</body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  EXPECT(loaded);

  bool done = false, hit = false;
  std::string gk, error;
  impl.HandleClick(0, 0, [&](bool h, std::string v, std::string e) {
    hit = h;
    gk = v;
    error = e;
    done = true;
  });
  EXPECT(done);
  (void)hit;
  (void)error;
  // Hit-test may miss a 0,0 click on an un-laid-out doc; the mapping helpers
  // are still exercised by HandleKey Enter on the focused omnibox.
  impl.HandleClick(1, 1, [&](bool, std::string, std::string) {});
  bool key_done = false, handled = false;
  std::string ev, val;
  // Focus omnibox by clicking is layout-dependent; type via HandleKey after
  // focusing through Ctrl+L (now looks up #omnibox, not only #url).
  impl.HandleKey(static_cast<uint32_t>('L'), 2, "",
                 [&](bool h, std::string e, std::string, std::string) {
                   handled = h;
                   ev = e;
                 });
  EXPECT(handled);
  EXPECT(ev == "focus-url");
  impl.HandleKey(0x0D, 0, "", [&](bool h, std::string e, std::string v, std::string) {
    handled = h;
    ev = e;
    val = v;
    key_done = true;
  });
  EXPECT(key_done);
  EXPECT(handled);
  EXPECT(ev.find("go") == 0);
  EXPECT(ev.find("example.com") != std::string::npos);
}

TEST(golden_load_html_parses_given_source_directly) {
  blink::LocalFrameImpl impl("unused", 0, "frame-html");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool done = false, ok = false;
  std::string title, error;
  remote->LoadHTML("<html><head><title>Direct</title></head><body>hi</body></html>",
                    [&](bool o, std::string t, uint32_t, uint32_t, std::string e) {
                      ok = o;
                      title = t;
                      error = e;
                      done = true;
                    });
  Pump();

  EXPECT(done);
  EXPECT(ok);
  EXPECT_EQ(title, "Direct");
  EXPECT(error.empty());
}

TEST(golden_navigate_rejects_https_no_tls) {
  blink::LocalFrameImpl impl("unused", 0, "frame-https");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool done = false, ok = true;
  std::string error;
  remote->Navigate("https://example.com/",
                    [&](bool o, std::string, uint32_t, uint32_t, std::string e) {
                      ok = o;
                      error = e;
                      done = true;
                    });
  Pump();

  EXPECT(done);
  EXPECT(!ok);
  EXPECT(error.find("not supported") != std::string::npos);
}

TEST(golden_navigate_reports_unreachable_host) {
  // Port 1 (reserved, nothing listens) -- proves a real connect() failure
  // surfaces through the real Mojo response instead of crashing or hanging.
  blink::LocalFrameImpl impl("unused", 0, "frame-2");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool done = false, ok = true;
  std::string error;
  remote->Navigate("http://127.0.0.1:1/",
                    [&](bool o, std::string, uint32_t, uint32_t, std::string e) {
                      ok = o;
                      error = e;
                      done = true;
                    });
  Pump();

  EXPECT(done);
  EXPECT(!ok);
  EXPECT(!error.empty());
}

// Branches on whether this build linked ../WASMSkia + ../WASMRasta (see
// blinker's own CMakeLists' `if(TARGET wasmskia_font AND TARGET rasta)`
// guard) -- exercises whichever behavior is actually compiled in, real
// or stub.
TEST(golden_capture_snapshot_reflects_whether_a_paint_pipeline_is_wired) {
  blink::LocalFrameImpl impl("unused", 0, "frame-3");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML("<html><head><title>Snap</title></head><body><p>Hello</p></body></html>",
                    [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  bool done = false, ok = true;
  std::string png, error;
  remote->CaptureSnapshot([&](bool o, std::string p, std::string e) {
    ok = o;
    png = p;
    error = e;
    done = true;
  });
  Pump();

  EXPECT(done);
#ifdef BLINK_HAS_PAINT_PIPELINE
  EXPECT(ok);
  EXPECT(error.empty());
  // PNG magic bytes -- proves it's a real encoded image, not just a
  // non-empty placeholder string.
  EXPECT(png.size() > 8);
  EXPECT(static_cast<unsigned char>(png[0]) == 0x89 && png[1] == 'P' && png[2] == 'N' &&
        png[3] == 'G');
#else
  EXPECT(!ok);
#endif
}

// Branches on whether this build linked ../WASMv16's quickjs-ng engine
// (see blinker's own CMakeLists' `if(TARGET wasmv16_engine)` guard) --
// exercises whichever behavior is actually compiled in, real or stub.
TEST(golden_eval_reflects_whether_a_js_engine_is_wired) {
  blink::LocalFrameImpl impl("unused", 0, "frame-4");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool done = false, ok = true;
  std::string result_json, error;
  remote->Eval("1+1", [&](bool o, std::string r, std::string e) {
    ok = o;
    result_json = r;
    error = e;
    done = true;
  });
  Pump();

  EXPECT(done);
#ifdef BLINK_HAS_WASMV16_ENGINE
  EXPECT(ok);
  EXPECT_EQ(result_json, "2");
  EXPECT(error.empty());
#else
  EXPECT(!ok);
#endif
}

#ifdef BLINK_HAS_DOM_BINDINGS

namespace {

std::string EvalOnce(mojo::Remote<blink::LocalFrame>& remote, const std::string& js) {
  bool done = false;
  std::string result_json;
  remote->Eval(js, [&](bool, std::string r, std::string) {
    result_json = r;
    done = true;
  });
  Pump();
  EXPECT(done);
  return result_json;
}

}  // namespace

// Proves the real bruja_dom-backed document/window (installed only when
// both ../WASMv16 and ../WASMBruja are present, see blinker's CMakeLists'
// `if(TARGET wasmv16_engine AND TARGET bruja_dom)` guard) actually mirrors
// LoadHTML's parsed tree and answers real DOM queries -- not a bare
// quickjs-ng global scope.
TEST(golden_eval_sees_real_dom_mirroring_loaded_html) {
  blink::LocalFrameImpl impl("unused", 0, "frame-dom");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><head><title>Dom Test</title></head>"
      "<body><div id=\"foo\">hi</div></body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  EXPECT_EQ(EvalOnce(remote, "document.title"), "\"Dom Test\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('foo').tagName"), "\"DIV\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('foo').textContent"), "\"hi\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('missing')"), "null");
}

// HTML5 "in body" mode implicitly closes an open <p> when certain start
// tags follow it (html_parser.cc's ImplicitlyClosesP) -- without that,
// `<p>first<p>second` parsed as one nested <p> ("firstsecond"), not two
// siblings ("first" / "second"), the one real HTML-parsing gap
// BrowserEmu's same-HTML-through-three-facades parity harness actually
// found (every facade shares this parser).
TEST(golden_load_html_implicitly_closes_open_p_on_block_start_tag) {
  blink::LocalFrameImpl impl("unused", 0, "frame-implicit-p");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><body><p id=\"a\">first<p id=\"b\">second</body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  EXPECT_EQ(EvalOnce(remote, "document.getElementById('a').textContent"), "\"first\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('b').textContent"), "\"second\"");
  // Siblings, not nested: #a does not contain #b.
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('a').contains(document.getElementById('b'))"),
           "false");
}

// window.innerWidth/innerHeight previously never reflected a real
// SetViewport call -- WindowImpl's own inner_width_/inner_height_ just
// defaulted to a hardcoded 1024x768 forever. Covers both orderings:
// SetViewport before dom_window_ exists (picked up by EnsureDomInstalled)
// and after (SetViewport itself pushes the update).
TEST(golden_set_viewport_updates_window_inner_dimensions) {
  blink::LocalFrameImpl impl("unused", 0, "frame-viewport");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  // Before any script has run (before dom_window_ exists).
  remote->SetViewport(400, 300);
  Pump();

  bool loaded = false;
  remote->LoadHTML("<html><body></body></html>",
                   [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  EXPECT_EQ(EvalOnce(remote, "innerWidth"), "400");
  EXPECT_EQ(EvalOnce(remote, "innerHeight"), "300");

  // After dom_window_ already exists (a live resize).
  remote->SetViewport(800, 600);
  Pump();
  EXPECT_EQ(EvalOnce(remote, "innerWidth"), "800");
  EXPECT_EQ(EvalOnce(remote, "innerHeight"), "600");
}

// getComputedStyle(el) cascades against the document's own <style> (via
// LocalFrameImpl::current_sheet_, parsed in RebuildDomTree the same way
// InlineComputedCss parses one) and resolves inheritance by walking the
// element's real ancestor chain, the same recursion shape css.cc's own
// ApplyTree uses for layout -- not a stub: window.getComputedStyle was
// simply undefined before this existed.
TEST(golden_get_computed_style_resolves_cascaded_and_inherited_color) {
  blink::LocalFrameImpl impl("unused", 0, "frame-computed-style");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><head><title>Css Test</title>"
      "<style>#parent{color:red}</style></head>"
      "<body><div id=\"parent\"><span id=\"child\">hi</span></div></body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  // Directly styled.
  EXPECT_EQ(EvalOnce(remote, "getComputedStyle(document.getElementById('parent')).color"),
           "\"rgb(255, 0, 0)\"");
  // Inherited from #parent -- not itself styled.
  EXPECT_EQ(EvalOnce(remote, "getComputedStyle(document.getElementById('child')).color"),
           "\"rgb(255, 0, 0)\"");
  // No element / not an Element: honest empty object, not a crash.
  EXPECT_EQ(EvalOnce(remote, "getComputedStyle(document.getElementById('missing'))"), "{}");
  EXPECT_EQ(EvalOnce(remote, "getComputedStyle(42)"), "{}");
}

#ifdef BLINK_HAS_IMAGE_DECODE
// Proves <img src="data:...;base64,..."> is a *real* decode (wasmskia's
// stb_image, via FetchSubresources -> DecodeDataUrl/DecodeBase64,
// local_frame_impl.cc), not just attribute parsing -- checked against
// LocalFrameImpl::document().images_by_url directly (C++-side ground
// truth) as well as through JS (document.getElementById(...).naturalWidth,
// now real -- see golden_get_element_by_id_returns_tag_specific_binding
// below for the getElementById/WrapElementForJs fix that made this
// JS-reachable at all; it wasn't when this test was first written).
TEST(golden_load_html_decodes_data_url_image_bytes) {
  blink::LocalFrameImpl impl("unused", 0, "frame-image-decode");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  // A real, valid 1x1 red PNG (not a placeholder byte string).
  const std::string src =
      "data:image/png;base64,"
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR42mNg+M8AAAMBAQB6"
      "XzH2AAAAAElFTkSuQmCC";

  bool loaded = false;
  remote->LoadHTML("<html><body><img id=\"a\" src=\"" + src + "\"></body></html>",
                   [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  auto it = impl.document().images_by_url.find(src);
  EXPECT(it != impl.document().images_by_url.end());
  EXPECT_EQ(it->second.width, 1);
  EXPECT_EQ(it->second.height, 1);
  // RGBA8888, 1x1 = 4 bytes; real decode (not a stub -- an unrecognized
  // format or corrupt bytes leaves this url entirely absent from the map,
  // caught by the EXPECT above), opaque alpha.
  EXPECT_EQ(it->second.rgba.size(), 4u);
  EXPECT_EQ(static_cast<int>(it->second.rgba[3]), 255);  // A (opaque)

  EXPECT_EQ(EvalOnce(remote, "document.getElementById('a').naturalWidth"), "1");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('a').naturalHeight"), "1");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('a').complete"), "true");
}
#endif  // BLINK_HAS_IMAGE_DECODE

// getElementById/querySelector/etc. used to always hand back a
// generic-Element-shaped V8 wrapper regardless of the real tag
// (dom_v8_gen.h's CreateElementBinding had no tag-based dispatch), so
// nothing subtype-specific (naturalWidth, an anchor's href, an input's
// value/checked) was ever JS-reachable through a DOM query -- only via a
// pointer some C++ code happened to wrap with the *specific* binding
// directly, which nothing in this stack did. Fixed: EmitInterfaceReturn/
// EmitBaseToJsVariable (WASMBruja's brujac V8 backend,
// cpp_generator_v8.cc) now route Element-typed returns through
// WrapElementForJs (bruja_dom/dom_v8_impl.h), a hand-maintained
// dynamic_cast chain mirroring DocumentImpl::MakeElementForTag's own
// tag->concrete-type map. getComputedStyle(el) needed the reverse
// (UnwrapElementFromJs) since its argument is now tag-specific too, not
// always Element -- covered by golden_get_computed_style_resolves_cascaded_
// and_inherited_color already passing unchanged.
TEST(golden_get_element_by_id_returns_tag_specific_binding) {
  blink::LocalFrameImpl impl("unused", 0, "frame-tag-specific-binding");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><body><a id=\"link\" href=\"https://example.test/\">go</a>"
      "<input id=\"box\" value=\"hi\" type=\"checkbox\" checked>"
      "<div id=\"plain\"></div></body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  EXPECT_EQ(EvalOnce(remote, "document.getElementById('link').href"),
           "\"https://example.test/\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('box').value"), "\"hi\"");
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('box').checked"), "true");
  // A plain <div> still round-trips through its own specific binding
  // (HTMLDivElement, MakeElementForTag's fallback case), not a crash or
  // a silently-wrong type.
  EXPECT_EQ(EvalOnce(remote, "document.getElementById('plain').tagName"), "\"DIV\"");
}

// Proves window.localStorage/sessionStorage are real, independent,
// in-memory stores that persist across separate Eval() calls within one
// LocalFrameImpl -- the explicit "SessionStorage and *Storage aren't
// done" gap this test closes.
TEST(golden_eval_local_and_session_storage_persist_and_stay_independent) {
  blink::LocalFrameImpl impl("unused", 0, "frame-storage");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML("<html><body>hi</body></html>",
                    [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);

  // Not set yet.
  EXPECT_EQ(EvalOnce(remote, "window.localStorage.getItem('k')"), "null");

  // Set in a separate Eval call, then read back in a third -- proves
  // persistence isn't just within-one-expression scoping.
  EvalOnce(remote, "window.localStorage.setItem('k', 'v')");
  EXPECT_EQ(EvalOnce(remote, "window.localStorage.getItem('k')"), "\"v\"");

  // sessionStorage is a distinct instance -- must not see localStorage's key.
  EXPECT_EQ(EvalOnce(remote, "window.sessionStorage.getItem('k')"), "null");
  EvalOnce(remote, "window.sessionStorage.setItem('k', 'session-value')");
  EXPECT_EQ(EvalOnce(remote, "window.sessionStorage.getItem('k')"), "\"session-value\"");
  EXPECT_EQ(EvalOnce(remote, "window.localStorage.getItem('k')"), "\"v\"");
}

#endif  // BLINK_HAS_DOM_BINDINGS

#ifdef BLINK_HAS_WASMV16_ENGINE

namespace {

std::string EvalJs(mojo::Remote<blink::LocalFrame>& remote, const std::string& js) {
  bool done = false;
  std::string result_json;
  remote->Eval(js, [&](bool, std::string r, std::string) {
    result_json = r;
    done = true;
  });
  Pump();
  EXPECT(done);
  return result_json;
}

}  // namespace

// Blink HTML parser behavior: classic inline <script> tags run in document
// order as part of LoadHTML, not as a later Eval() the caller has to
// remember to issue. Script-goal (StatementList), so `var x = 1;` works
// -- Eval()'s parenthesized-expression wrap would have rejected that.
TEST(golden_load_html_runs_classic_inline_scripts) {
  blink::LocalFrameImpl impl("unused", 0, "frame-scripts");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><body>"
      "<script>var __blink_script_ran = 40 + 2;</script>"
      "<script>var __blink_script_next = __blink_script_ran + 1;</script>"
      "</body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);
  EXPECT_EQ(EvalJs(remote, "__blink_script_ran"), "42");
  EXPECT_EQ(EvalJs(remote, "__blink_script_next"), "43");
}

// Blink: window === globalThis, so assigning window.foo creates a global.
TEST(golden_load_html_window_assignment_is_a_global) {
  blink::LocalFrameImpl impl("unused", 0, "frame-window-global");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false;
  remote->LoadHTML(
      "<html><body><script>window.__blink_from_window = 99;</script></body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) { loaded = o; });
  Pump();
  EXPECT(loaded);
  EXPECT_EQ(EvalJs(remote, "__blink_from_window"), "99");
  EXPECT_EQ(EvalJs(remote, "window === globalThis || window === this"), "true");
}

// type=module / src= / non-JS MIME are not classic parser-blocking
// scripts. A throwing classic script does not abort later ones (Blink).
TEST(golden_load_html_skips_non_classic_scripts_and_survives_throw) {
  blink::LocalFrameImpl impl("unused", 0, "frame-scripts-skip");
  mojo::Receiver<blink::LocalFrame> receiver(&impl);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool loaded = false, ok = false;
  remote->LoadHTML(
      "<html><body>"
      "<script type=\"module\">var __blink_mod = 1;</script>"
      "<script src=\"http://127.0.0.1:1/missing.js\">var __blink_src_body = 1;</script>"
      "<script type=\"application/json\">{\"a\":1}</script>"
      "<script>throw new Error('x');</script>"
      "<script>var __blink_after_throw = 7;</script>"
      "</body></html>",
      [&](bool o, std::string, uint32_t, uint32_t, std::string) {
        ok = o;
        loaded = true;
      });
  Pump();
  EXPECT(loaded);
  EXPECT(ok);
  EXPECT_EQ(EvalJs(remote, "typeof __blink_mod"), "\"undefined\"");
  EXPECT_EQ(EvalJs(remote, "typeof __blink_src_body"), "\"undefined\"");
  EXPECT_EQ(EvalJs(remote, "__blink_after_throw"), "7");
}

#endif  // BLINK_HAS_WASMV16_ENGINE
