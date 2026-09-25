// Host-callable C ABI over the voodoomc-generated LocalFrame Proxy_/Stub_.
// Does not reimplement Navigate/LoadHTML/Eval -- it binds LocalFrameImpl
// the same way tests/local_frame_roundtrip_test.cc does and forwards.

#include "blink/c/frame.h"

#include "blink/local_frame_impl.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

#if defined(__wasi__)
#define BLINKER_EXPORT(name) __attribute__((export_name(name)))
#else
#define BLINKER_EXPORT(name)
#endif

void Pump(int iterations = 8) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

struct FrameSlot {
  blink::LocalFrameImpl impl;
  mojo::Receiver<blink::LocalFrame> receiver;
  mojo::Remote<blink::LocalFrame> remote;
  std::string title;
  std::string error;
  std::string eval_json;
  uint32_t width = 0;
  uint32_t height = 0;
  bool last_ok = false;

  FrameSlot() : impl("wasm", 0, "frame"), receiver(&impl) {
    receiver.Bind(remote.BindNewPipeAndPassReceiver());
  }
};

std::vector<std::unique_ptr<FrameSlot>> g_frames;
bool g_inited = false;

FrameSlot* Slot(BlinkerHandle frame) {
  if (frame == BLINKER_HANDLE_INVALID) return nullptr;
  const size_t i = static_cast<size_t>(frame - 1);
  if (i >= g_frames.size() || !g_frames[i]) return nullptr;
  return g_frames[i].get();
}

BlinkerResult CopyOut(const std::string& src, char* buf, uint32_t buf_len) {
  if (!buf || buf_len == 0) {
    return src.empty() ? BLINKER_RESULT_OK : BLINKER_RESULT_RESOURCE_EXHAUSTED;
  }
  const size_t n = src.size();
  if (n + 1 > buf_len) {
    std::memcpy(buf, src.data(), buf_len - 1);
    buf[buf_len - 1] = '\0';
    return BLINKER_RESULT_RESOURCE_EXHAUSTED;
  }
  std::memcpy(buf, src.data(), n);
  buf[n] = '\0';
  return BLINKER_RESULT_OK;
}

std::string FromGuest(const char* p, uint32_t n) {
  if (!p || n == 0) return {};
  return std::string(p, p + n);
}

}  // namespace

extern "C" {

BLINKER_EXPORT("BlinkerInit")
BlinkerResult BlinkerInit(void) {
  if (!g_inited) {
    MojoInitialize(nullptr);
    g_inited = true;
  }
  return BLINKER_RESULT_OK;
}

BLINKER_EXPORT("BlinkerShutdown")
void BlinkerShutdown(void) {
  g_frames.clear();
  if (g_inited) {
    MojoShutdown(nullptr);
    g_inited = false;
  }
}

BLINKER_EXPORT("BlinkerAlloc")
void* BlinkerAlloc(uint32_t n) {
  if (n == 0) return nullptr;
  return std::malloc(n);
}

BLINKER_EXPORT("BlinkerFree")
void BlinkerFree(void* p) { std::free(p); }

BLINKER_EXPORT("BlinkerCreateFrame")
BlinkerHandle BlinkerCreateFrame(void) {
  if (!g_inited) return BLINKER_HANDLE_INVALID;
  auto slot = std::make_unique<FrameSlot>();
  for (size_t i = 0; i < g_frames.size(); ++i) {
    if (!g_frames[i]) {
      g_frames[i] = std::move(slot);
      return static_cast<BlinkerHandle>(i + 1);
    }
  }
  g_frames.push_back(std::move(slot));
  return static_cast<BlinkerHandle>(g_frames.size());
}

BLINKER_EXPORT("BlinkerDestroyFrame")
void BlinkerDestroyFrame(BlinkerHandle frame) {
  FrameSlot* s = Slot(frame);
  if (!s) return;
  g_frames[static_cast<size_t>(frame - 1)].reset();
}

BLINKER_EXPORT("BlinkerLoadHTML")
BlinkerResult BlinkerLoadHTML(BlinkerHandle frame, const char* html, uint32_t html_len) {
  FrameSlot* s = Slot(frame);
  if (!s || !s->remote.is_bound()) return BLINKER_RESULT_INVALID_ARGUMENT;
  bool done = false;
  s->remote->LoadHTML(FromGuest(html, html_len),
                      [&](bool ok, std::string title, uint32_t w, uint32_t h, std::string err) {
                        s->last_ok = ok;
                        s->title = std::move(title);
                        s->width = w;
                        s->height = h;
                        s->error = std::move(err);
                        done = true;
                      });
  Pump();
  return done ? BLINKER_RESULT_OK : BLINKER_RESULT_INTERNAL;
}

BLINKER_EXPORT("BlinkerNavigate")
BlinkerResult BlinkerNavigate(BlinkerHandle frame, const char* url, uint32_t url_len) {
  FrameSlot* s = Slot(frame);
  if (!s || !s->remote.is_bound()) return BLINKER_RESULT_INVALID_ARGUMENT;
  bool done = false;
  s->remote->Navigate(FromGuest(url, url_len),
                      [&](bool ok, std::string title, uint32_t w, uint32_t h, std::string err) {
                        s->last_ok = ok;
                        s->title = std::move(title);
                        s->width = w;
                        s->height = h;
                        s->error = std::move(err);
                        done = true;
                      });
  Pump();
  return done ? BLINKER_RESULT_OK : BLINKER_RESULT_INTERNAL;
}

BLINKER_EXPORT("BlinkerEval")
BlinkerResult BlinkerEval(BlinkerHandle frame, const char* js, uint32_t js_len) {
  FrameSlot* s = Slot(frame);
  if (!s || !s->remote.is_bound()) return BLINKER_RESULT_INVALID_ARGUMENT;
  bool done = false;
  s->remote->Eval(FromGuest(js, js_len),
                  [&](bool ok, std::string json, std::string err) {
                    s->last_ok = ok;
                    s->eval_json = std::move(json);
                    s->error = std::move(err);
                    done = true;
                  });
  Pump();
  return done ? BLINKER_RESULT_OK : BLINKER_RESULT_INTERNAL;
}

BLINKER_EXPORT("BlinkerTitle")
BlinkerResult BlinkerTitle(BlinkerHandle frame, char* buf, uint32_t buf_len) {
  FrameSlot* s = Slot(frame);
  if (!s) return BLINKER_RESULT_INVALID_ARGUMENT;
  return CopyOut(s->title, buf, buf_len);
}

BLINKER_EXPORT("BlinkerLastError")
BlinkerResult BlinkerLastError(BlinkerHandle frame, char* buf, uint32_t buf_len) {
  FrameSlot* s = Slot(frame);
  if (!s) return BLINKER_RESULT_INVALID_ARGUMENT;
  return CopyOut(s->error, buf, buf_len);
}

BLINKER_EXPORT("BlinkerEvalResult")
BlinkerResult BlinkerEvalResult(BlinkerHandle frame, char* buf, uint32_t buf_len) {
  FrameSlot* s = Slot(frame);
  if (!s) return BLINKER_RESULT_INVALID_ARGUMENT;
  return CopyOut(s->eval_json, buf, buf_len);
}

BLINKER_EXPORT("BlinkerWidth")
uint32_t BlinkerWidth(BlinkerHandle frame) {
  FrameSlot* s = Slot(frame);
  return s ? s->width : 0;
}

BLINKER_EXPORT("BlinkerHeight")
uint32_t BlinkerHeight(BlinkerHandle frame) {
  FrameSlot* s = Slot(frame);
  return s ? s->height : 0;
}

BLINKER_EXPORT("BlinkerLastOk")
int BlinkerLastOk(BlinkerHandle frame) {
  FrameSlot* s = Slot(frame);
  return s && s->last_ok ? 1 : 0;
}

BLINKER_EXPORT("BlinkerSmokeLoadHTML")
int BlinkerSmokeLoadHTML(void) {
  if (BlinkerInit() != BLINKER_RESULT_OK) return 0;
  BlinkerHandle frame = BlinkerCreateFrame();
  if (frame == BLINKER_HANDLE_INVALID) return 0;
  const char kHtml[] =
      "<html><head><title>Wasm Bindings</title></head><body>hi</body></html>";
  if (BlinkerLoadHTML(frame, kHtml, static_cast<uint32_t>(sizeof(kHtml) - 1)) !=
      BLINKER_RESULT_OK) {
    return 0;
  }
  if (BlinkerLastOk(frame) != 1) return 0;
  char title[32];
  if (BlinkerTitle(frame, title, sizeof(title)) != BLINKER_RESULT_OK) return 0;
  return std::strcmp(title, "Wasm Bindings") == 0 ? 1 : 0;
}

}  // extern "C"
