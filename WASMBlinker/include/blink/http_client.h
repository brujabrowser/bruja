#ifndef BLINK_HTTP_CLIENT_H_
#define BLINK_HTTP_CLIENT_H_

#include <string>

namespace blink {

// A minimal, purpose-built HTTP/1.1 client -- raw sockets, hand-rolled
// request/response framing. Not a general HTTP library: LocalFrameImpl's
// only use of it is one POST with a JSON body to Cadmium's renderer bridge
// (loki-closure/cmd/cadmium/renderer_bridge.go), so it only implements
// exactly that: a POST with a Content-Length body, and a response reader
// that trusts Content-Length (the bridge always sends one -- see
// encoding/json.Encoder there). Native sockets (WinSock2 / BSD) on host
// builds; under WASI, every call returns connected=false with an honest
// error (whp's own sockets are stubs there too -- see README.md).
struct HttpResult {
  bool connected = false;  // false = couldn't even reach host:port
  int status = 0;
  std::string body;
  std::string error;  // set when !connected
};

HttpResult HttpPostJSON(const std::string& host, int port,
                        const std::string& path, const std::string& json_body);

// Same as HttpPostJSON with an explicit socket timeout. UniShell chrome
// invoke_event fetches https pages (example.com) inside the POST, so the
// default 2s renderer-bridge timeout is too short.
HttpResult HttpPostJSONTimeout(const std::string& host, int port, const std::string& path,
                               const std::string& json_body, int timeout_ms);

// A bodyless GET -- used for CaptureSnapshot's /screenshot fetch. Unlike
// HttpPostJSON's caller, which pulls known text fields (title, error) out
// of result.body via the JSON helpers in local_frame_impl.cc, a GET like
// this one may return an arbitrary binary body (e.g. a PNG): result.body is
// byte-transparent either way (HttpResult reads/stores raw bytes, with no
// text assumptions applied until a caller chooses to treat it as JSON), so
// no separate "binary" result type is needed here.
HttpResult HttpGet(const std::string& host, int port, const std::string& path);

}  // namespace blink

#endif  // BLINK_HTTP_CLIENT_H_
