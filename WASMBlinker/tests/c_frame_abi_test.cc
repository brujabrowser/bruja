// Proves the C ABI talks through the voodoomc-generated LocalFrame
// Proxy_/Stub_ (LoadHTML round-trip), not a bypass of generated bindings.
#include "test.h"

#include "blink/c/frame.h"

#include <string>

TEST(c_abi_load_html_through_generated_stub) {
  EXPECT(BlinkerInit() == BLINKER_RESULT_OK);
  BlinkerHandle frame = BlinkerCreateFrame();
  EXPECT(frame != BLINKER_HANDLE_INVALID);

  const char kHtml[] = "<html><head><title>Wasm Bindings</title></head><body>hi</body></html>";
  EXPECT(BlinkerLoadHTML(frame, kHtml, static_cast<uint32_t>(sizeof(kHtml) - 1)) ==
         BLINKER_RESULT_OK);
  EXPECT(BlinkerLastOk(frame) == 1);

  char title[64];
  EXPECT(BlinkerTitle(frame, title, sizeof(title)) == BLINKER_RESULT_OK);
  EXPECT_EQ(std::string(title), std::string("Wasm Bindings"));
  EXPECT(BlinkerWidth(frame) > 0);
  EXPECT(BlinkerHeight(frame) > 0);

  char err[8];
  EXPECT(BlinkerLastError(frame, err, sizeof(err)) == BLINKER_RESULT_OK);
  EXPECT(err[0] == '\0');

  BlinkerDestroyFrame(frame);
  BlinkerShutdown();
}

TEST(c_abi_navigate_https_is_honest_error) {
  EXPECT(BlinkerInit() == BLINKER_RESULT_OK);
  BlinkerHandle frame = BlinkerCreateFrame();
  EXPECT(frame != BLINKER_HANDLE_INVALID);

  const char kUrl[] = "https://example.com/";
  EXPECT(BlinkerNavigate(frame, kUrl, static_cast<uint32_t>(sizeof(kUrl) - 1)) ==
         BLINKER_RESULT_OK);
  EXPECT(BlinkerLastOk(frame) == 0);
  char err[128];
  EXPECT(BlinkerLastError(frame, err, sizeof(err)) == BLINKER_RESULT_OK);
  EXPECT(std::string(err).find("not supported") != std::string::npos);

  BlinkerDestroyFrame(frame);
  BlinkerShutdown();
}
