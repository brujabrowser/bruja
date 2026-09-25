// paint_demo <out.png> [html] -- exercises the real, native (no
// loki-closure) LocalFrameImpl pipeline end to end: LoadHTML parses the
// given source (or a small built-in sample), CaptureSnapshot paints it
// with a real TrueType font onto a real wasmskia::Canvas and PNG-encodes
// it via WASMRasta, and this just writes the bytes to disk. Only produces
// real pixels when built with the optional WASMSkia+WASMRasta siblings
// present (BLINK_HAS_PAINT_PIPELINE) -- otherwise CaptureSnapshot's
// honest stub makes this print an error and exit nonzero.
#include "blink/local_frame_impl.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace {
void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) whp::Executor::Current().RunUntilIdle();
}
}  // namespace

int main(int argc, char** argv) {
  // Without this, mojo::Receiver/Remote pipe setup below segfaults
  // immediately (before any output) -- tests/test_main.cc calls this too.
  MojoInitialize(nullptr);

  if (argc < 2) {
    std::fprintf(stderr, "usage: paint_demo <out.png> [html]\n");
    return 2;
  }
  std::string out_path = argv[1];
  std::string html =
      argc > 2 ? argv[2]
               : "<html><head><title>WASMBlinker</title></head><body>"
                 "<h1>Hello from native WASMBlinker 😀</h1>"
                 "<p>This paragraph is real TrueType text, rendered by "
                 "wasmskia::Font/DrawText, with zero loki-closure involved. "
                 "Color emoji: ❤️ 👍 🔥 🌍</p>"
                 "<p>Second paragraph, second line.</p>"
                 "</body></html>";

  blink::LocalFrameImpl frame("unused", 0, "paint-demo");
  mojo::Receiver<blink::LocalFrame> receiver(&frame);
  mojo::Remote<blink::LocalFrame> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool load_ok = false;
  std::string title;
  remote->LoadHTML(html, [&](bool ok, std::string t, uint32_t, uint32_t, std::string err) {
    load_ok = ok;
    title = t;
    if (!ok) std::fprintf(stderr, "LoadHTML failed: %s\n", err.c_str());
  });
  Pump();
  if (!load_ok) return 1;
  std::printf("title: %s\n", title.c_str());

  bool snap_ok = false;
  std::string png, error;
  remote->CaptureSnapshot([&](bool ok, std::string p, std::string e) {
    snap_ok = ok;
    png = p;
    error = e;
  });
  Pump();
  if (!snap_ok) {
    std::fprintf(stderr, "CaptureSnapshot failed: %s\n", error.c_str());
    return 1;
  }

  std::ofstream out(out_path, std::ios::binary);
  out.write(png.data(), static_cast<std::streamsize>(png.size()));
  std::printf("wrote %zu bytes to %s\n", png.size(), out_path.c_str());
  MojoShutdown(nullptr);
  return 0;
}
