#ifndef BLINK_C_FRAME_H_
#define BLINK_C_FRAME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// C ABI for blinker.wasm -- a WASI reactor that hosts blink::LocalFrameImpl
// behind the voodoomc-generated Proxy_/Stub_ pair (same Remote/Receiver
// loopback the native golden tests use). LoadHTML/Eval/Navigate go through
// that generated Mojo path, not a second implementation.

typedef uint32_t BlinkerHandle;
typedef uint32_t BlinkerResult;

#define BLINKER_HANDLE_INVALID ((BlinkerHandle)0)

// Numeric values match whp/mojo result codes.
#define BLINKER_RESULT_OK 0u
#define BLINKER_RESULT_INVALID_ARGUMENT 3u
#define BLINKER_RESULT_FAILED_PRECONDITION 9u
#define BLINKER_RESULT_UNIMPLEMENTED 12u
#define BLINKER_RESULT_INTERNAL 13u
#define BLINKER_RESULT_RESOURCE_EXHAUSTED 8u

BlinkerResult BlinkerInit(void);
void BlinkerShutdown(void);

// Guest-side linear-memory helpers for a host that cannot write C++ strings
// directly. Pair with BlinkerLoadHTML/Navigate/Eval.
void* BlinkerAlloc(uint32_t n);
void BlinkerFree(void* p);

BlinkerHandle BlinkerCreateFrame(void);
void BlinkerDestroyFrame(BlinkerHandle frame);

// html/url/js are pointer+length into guest memory (not necessarily NUL
// terminated). Each call pumps whp::Executor until the generated Stub_
// delivers the LocalFrameImpl response.
BlinkerResult BlinkerLoadHTML(BlinkerHandle frame, const char* html, uint32_t html_len);
BlinkerResult BlinkerNavigate(BlinkerHandle frame, const char* url, uint32_t url_len);
BlinkerResult BlinkerEval(BlinkerHandle frame, const char* js, uint32_t js_len);

// Copies the last response's title / error / eval JSON into `buf` (NUL
// terminated when buf_len > 0). Returns BLINKER_RESULT_RESOURCE_EXHAUSTED
// if the string does not fit (still writes a truncated NUL-terminated
// prefix when buf_len > 0).
BlinkerResult BlinkerTitle(BlinkerHandle frame, char* buf, uint32_t buf_len);
BlinkerResult BlinkerLastError(BlinkerHandle frame, char* buf, uint32_t buf_len);
BlinkerResult BlinkerEvalResult(BlinkerHandle frame, char* buf, uint32_t buf_len);
uint32_t BlinkerWidth(BlinkerHandle frame);
uint32_t BlinkerHeight(BlinkerHandle frame);
int BlinkerLastOk(BlinkerHandle frame);

// One-shot reactor smoke: Init + CreateFrame + LoadHTML through the
// generated Stub_, returns 1 if title is "Wasm Bindings".
int BlinkerSmokeLoadHTML(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // BLINK_C_FRAME_H_
