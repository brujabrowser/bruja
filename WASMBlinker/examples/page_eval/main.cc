// page_eval <html> [eval-expr] -- exercises the real, native (no
// loki-closure) LocalFrameImpl pipeline: LoadHTML (real HTML parser,
// real CSS/layout, and -- when built with BLINK_HAS_DOM_BINDINGS -- a
// real window/document DOM that classic <script> tags see) followed by
// an optional Eval. This is BrowserEmu's "v8" runtime binary: unlike
// v8_example (WASMv8Bindings, a bare isolate with no HTML/DOM at all),
// this is the actual Chromium-shaped content pipeline -- built as part
// of ../../WASMv16's CMake project, which is what wires the
// BLINK_HAS_WASMV16_ENGINE/BLINK_HAS_DOM_BINDINGS siblings in; built
// standalone from WASMBlinker's own CMakeLists, Eval() is an honest
// stub instead.
#include "blink/local_frame_impl.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"
#include "wmp/mojo.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) whp::Executor::Current().RunUntilIdle();
}

// This binary's own small blink::LocalFrame method-id space (LoadHTML=1,
// Eval=2), same numbering mothman's PMothmanContent actor pair uses for
// the same two messages -- but note this is real Chromium-shaped Mojo
// transport (mojo::Receiver/Remote<blink::LocalFrame>, ../../WASMv16's
// own bindings), not a hand-rolled IPDL actor: it's synthetic only
// because blink::LocalFrame is BrowserEmu's own test interface with no
// shipped .mojom, so no real mojom-generated ordinal exists for it the
// way OrdinalCatalog::DefaultBrowser2Mojo's 23 rows do for real Chrome
// interfaces.
constexpr uint32_t kLoadHTMLMsgId = 1;
constexpr uint32_t kEvalMsgId = 2;

// --name=value, returns true and sets *out if argv[i] matches `name`.
bool MatchFlag(const std::string& arg, const char* name, std::string* out) {
  std::string prefix = std::string("--") + name + "=";
  if (arg.rfind(prefix, 0) != 0) return false;
  *out = arg.substr(prefix.size());
  return true;
}

// A JSON array of pre-quoted string fragments, e.g. Args({"\"html\"", "3"}).
std::string JsonArgs(std::initializer_list<std::string> parts) {
  std::string out = "[";
  bool first = true;
  for (const auto& p : parts) {
    if (!first) out += ",";
    first = false;
    out += p;
  }
  out += "]";
  return out;
}

// Same real MojoInvoke/OrdinalCatalog/MojoSink shape project_lovelace's
// own WMP ordinal bus produces (see wmp/mojo.h,
// project_lovelace/docs/ORDINAL-CHAINING.md); mirrors mothman's and
// yeti's own TraceFire helpers for a consistent trace format across all
// three engines.
void TraceFire(wmp::MojoSink* sink, uint32_t ordinal, const std::string& name,
               const std::string& args_json) {
  sink->Fire(ordinal, name, args_json,
            "browseremu-v8 synthetic blink::LocalFrame method id (real Mojo "
            "transport, no shipped .mojom ordinal for this test interface)",
            "v8");
}
}  // namespace

int main(int argc, char** argv) {
  // Unbuffered so title=/eval= survive the process-exit crash below (a
  // real bug: bruja_dom_generated's per-isolate V8 template caches, e.g.
  // ConsoleV8TemplatesByIsolate, are function-local statics that outlive
  // the isolates they cache -- destroying them at atexit touches already
  // -disposed QuickJS memory. Out of scope to fix here (it's generated
  // code, in WASMBruja's brujac V8 backend, not this repo); tests/test_main.cc
  // hits the same crash at the very end of a full ctest run, past its
  // last printed result, for the same reason).
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // Without this, mojo::Receiver/Remote pipe setup below segfaults
  // immediately (before any output) -- tests/test_main.cc calls this too;
  // examples/paint_demo/main.cc never did, so it has the same latent bug.
  MojoInitialize(nullptr);

  std::string html, eval_expr, trace_flag, fire_ordinal_flag;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (MatchFlag(arg, "trace", &trace_flag)) continue;
    if (MatchFlag(arg, "fire-ordinal", &fire_ordinal_flag)) continue;
    positional.push_back(arg);
  }
  if (!positional.empty()) html = positional[0];
  if (positional.size() > 1) eval_expr = positional[1];
  if (html.empty()) {
    html = "<html><head><title>WASMBlinker</title></head><body></body></html>";
  }

  // Real MojoInvoke/OrdinalCatalog/MojoSink (../../WASMMsgProxy) -- same
  // JSON shape project_lovelace's own WMP ordinal bus produces, loaded
  // with the same real, harvested Chrome ordinals
  // (OrdinalCatalog::DefaultBrowser2Mojo) its tooling ships. --fire-ordinal
  // lets a caller fire an arbitrary named or numeric ordinal (real-
  // cataloged or made up) directly into the same sink -- the same
  // "does this number get accepted" probe project_lovelace/docs/
  // ORDINAL-CHAINING.md runs against WMP, now against this engine's own
  // trace instead.
  wmp::MojoSink trace;
  trace.set_catalog(wmp::OrdinalCatalog::DefaultBrowser2Mojo());

  if (!fire_ordinal_flag.empty()) {
    uint32_t ord = 0;
    std::string iface;
    if (trace.catalog().Lookup(fire_ordinal_flag, &ord, &iface)) {
      TraceFire(&trace, ord, fire_ordinal_flag, "[]");
      std::printf("fire-ordinal=%u (real Chrome ordinal, %s)\n", ord, iface.c_str());
    } else {
      char* end = nullptr;
      unsigned long n = std::strtoul(fire_ordinal_flag.c_str(), &end, 0);
      if (end != fire_ordinal_flag.c_str() && n > 0) {
        TraceFire(&trace, static_cast<uint32_t>(n), "fire-ordinal", "[]");
        std::printf("fire-ordinal=%lu (not in the real Chrome catalog -- accepted anyway, "
                    "same finding as project_lovelace/docs/ORDINAL-CHAINING.md)\n",
                    n);
      } else {
        std::fprintf(stderr, "--fire-ordinal: %s is neither a cataloged name nor a number\n",
                     fire_ordinal_flag.c_str());
      }
    }
  }

  blink::LocalFrameImpl frame("unused", 0, "page-eval");
  mojo::Receiver<blink::LocalFrame> receiver(&frame);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool load_ok = false;
  std::string title, load_err;
  TraceFire(&trace, kLoadHTMLMsgId, "LoadHTML",
           JsonArgs({"\"<html, " + std::to_string(html.size()) + " bytes>\""}));
  remote->LoadHTML(html, [&](bool ok, std::string t, uint32_t, uint32_t, std::string err) {
    load_ok = ok;
    title = t;
    load_err = err;
  });
  Pump();
  if (!load_ok) {
    std::fprintf(stderr, "LoadHTML failed: %s\n", load_err.c_str());
    return 1;
  }
  std::printf("title=%s\n", title.c_str());

  if (!eval_expr.empty()) {
    TraceFire(&trace, kEvalMsgId, "Eval", JsonArgs({"\"" + eval_expr + "\""}));
    bool eval_ok = false;
    std::string result, eval_err;
    remote->Eval(eval_expr, [&](bool ok, std::string r, std::string err) {
      eval_ok = ok;
      result = r;
      eval_err = err;
    });
    Pump();
    if (eval_ok) {
      std::printf("eval=%s\n", result.c_str());
    } else {
      std::printf("eval=ERROR: %s\n", eval_err.c_str());
    }
  }

  if (!trace_flag.empty()) {
    std::string trace_json = trace.QueueJson();
    if (trace_flag == "-") {
      std::printf("trace=%s\n", trace_json.c_str());
    } else {
      std::ofstream out(trace_flag);
      out << trace_json;
      std::printf("trace=%s (%zu messages)\n", trace_flag.c_str(), trace.size());
    }
  }

  MojoShutdown(nullptr);
  return 0;
}
