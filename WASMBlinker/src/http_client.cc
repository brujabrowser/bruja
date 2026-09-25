#include "blink/http_client.h"

#include <sstream>

#if defined(__wasi__)
// WASI preview1 has no getaddrinfo/connect. Same honest-error shape
// Navigate() already uses for https:// -- connected=false, error set.
namespace blink {

namespace {

HttpResult WasiNoSockets(const std::string& host, int port) {
  HttpResult result;
  result.error = "WASI networking not wired (no sockets in this guest) for " +
                 host + ":" + std::to_string(port);
  return result;
}

}  // namespace

HttpResult HttpPostJSONTimeout(const std::string& host, int port, const std::string&,
                               const std::string&, int) {
  return WasiNoSockets(host, port);
}

HttpResult HttpPostJSON(const std::string& host, int port, const std::string&,
                        const std::string&) {
  return WasiNoSockets(host, port);
}

HttpResult HttpGet(const std::string& host, int port, const std::string&) {
  return WasiNoSockets(host, port);
}

}  // namespace blink

#else  // !__wasi__

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

namespace blink {

namespace {

#ifdef _WIN32
class WinsockInit {
 public:
  WinsockInit() {
    WSADATA wsa;
    ok_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  }
  ~WinsockInit() {
    if (ok_) WSACleanup();
  }
  bool ok_ = false;
};
// Constructed once, before main() -- WSAStartup/WSACleanup bracket every
// socket call any translation unit in this binary makes.
WinsockInit g_winsock_init;
#endif

bool SendAll(SOCKET s, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int n = send(s, data + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

// Splits the raw HTTP/1.1 response into status code + body, trusting
// Content-Length (present on every renderer_bridge.go response) over
// connection-close framing so a keep-alive server response still parses.
bool ParseHttpResponse(const std::string& raw, int* status, std::string* body) {
  size_t header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) return false;
  std::string status_line = raw.substr(0, raw.find("\r\n"));
  size_t sp1 = status_line.find(' ');
  if (sp1 == std::string::npos) return false;
  *status = std::atoi(status_line.c_str() + sp1 + 1);

  std::string headers = raw.substr(0, header_end);
  size_t content_length = raw.size() - (header_end + 4);
  size_t cl_pos = headers.find("Content-Length:");
  if (cl_pos == std::string::npos) cl_pos = headers.find("content-length:");
  if (cl_pos != std::string::npos) {
    content_length = static_cast<size_t>(std::atoi(headers.c_str() + cl_pos + 15));
  }
  size_t body_start = header_end + 4;
  size_t available = raw.size() - body_start;
  *body = raw.substr(body_start, content_length < available ? content_length : available);
  return true;
}

// Connects to host:port, sends the fully-framed `request` verbatim, reads
// the response to EOF (every request here sends "Connection: close", so
// the server closing the socket is the normal end-of-response signal), and
// parses it. Shared by HttpPostJSON and HttpGet -- everything past framing
// the request line/headers is identical.
HttpResult SendRequestAndReadResponse(const std::string& host, int port,
                                       const std::string& request, int timeout_ms) {
  HttpResult result;
  if (timeout_ms < 1) timeout_ms = 2000;

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* addr = nullptr;
  std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addr) != 0 || !addr) {
    result.error = "getaddrinfo failed for " + host + ":" + port_str;
    return result;
  }

  SOCKET s = INVALID_SOCKET;
  struct addrinfo* p = addr;
  for (; p != nullptr; p = p->ai_next) {
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
    closesocket(s);
    s = INVALID_SOCKET;
  }
  freeaddrinfo(addr);
  if (s == INVALID_SOCKET) {
    result.error = "connect failed to " + host + ":" + port_str;
    return result;
  }
#ifdef _WIN32
  DWORD timeout_dword = static_cast<DWORD>(timeout_ms);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_dword),
             sizeof(timeout_dword));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_dword),
             sizeof(timeout_dword));
#else
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
  result.connected = true;

  if (!SendAll(s, request.data(), request.size())) {
    closesocket(s);
    result.connected = false;
    result.error = "send failed";
    return result;
  }

  std::string raw;
  char buf[4096];
  for (;;) {
    int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
  }
  closesocket(s);

  if (!ParseHttpResponse(raw, &result.status, &result.body)) {
    result.error = "malformed HTTP response";
    return result;
  }
  return result;
}

}  // namespace

HttpResult HttpPostJSONTimeout(const std::string& host, int port, const std::string& path,
                               const std::string& json_body, int timeout_ms) {
  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << json_body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << json_body;
  return SendRequestAndReadResponse(host, port, req.str(), timeout_ms);
}

HttpResult HttpPostJSON(const std::string& host, int port,
                        const std::string& path, const std::string& json_body) {
  return HttpPostJSONTimeout(host, port, path, json_body, 2000);
}

HttpResult HttpGet(const std::string& host, int port, const std::string& path) {
  std::ostringstream req;
  req << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Connection: close\r\n\r\n";
  return SendRequestAndReadResponse(host, port, req.str(), 2000);
}

}  // namespace blink

#endif  // !__wasi__
