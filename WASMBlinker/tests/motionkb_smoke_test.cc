#include "test.h"

#include "blink/local_frame_impl.h"
#include "blink/paint.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string InjectPaintOnlyHead(std::string html, const char* base_url) {
  if (html.find("name=\"bruja-paint-only\"") != std::string::npos) return html;
  std::string inject = std::string("<base href=\"") + base_url +
                       "\"><meta name=\"bruja-paint-only\" content=\"1\">";
  if (const size_t head = html.find("<head"); head != std::string::npos) {
    const size_t gt = html.find('>', head);
    if (gt != std::string::npos) {
      html.insert(gt + 1, inject);
      return html;
    }
  }
  return html;
}

}  // namespace

TEST(motionkb_load_and_capture) {
  const char* path = "C:/Users/grego/AppData/Local/Temp/motionkb.html";
  std::string html = ReadFile(path);
  if (html.empty()) {
    std::fprintf(stderr, "skip motionkb smoke: missing %s\n", path);
    return;
  }
  html = InjectPaintOnlyHead(std::move(html), "https://motionkb.com/");
  blink::LocalFrameImpl frame("unused", 0, "motionkb-smoke");
  frame.SetViewport(1084, 615);
  bool ok = false;
  frame.LoadHTML(html, [&](bool o, std::string, uint32_t, uint32_t, std::string) { ok = o; });
  EXPECT(ok);
  blink::RawFrameResult cap = blink::CaptureRawFrame(frame.document(), 1084, 615);
  EXPECT(cap.ok);
  EXPECT(cap.width > 0);
  EXPECT(cap.height > 0);
  EXPECT(!cap.rgba.empty());
}
