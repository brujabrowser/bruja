#ifndef BLINKER_TESTS_MOCK_RENDERER_BRIDGE_H_
#define BLINKER_TESTS_MOCK_RENDERER_BRIDGE_H_

#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

// One-shot local HTTP server standing in for Cadmium's real renderer
// bridge (loki-closure/cmd/cadmium/renderer_bridge.go): accepts exactly
// one request, records its raw JSON body, and replies with a fixed JSON
// response. Proves LocalFrameImpl::Navigate's HTTP client
// (blink/http_client.cc) really goes over a socket to a real server --
// this is the C++-side proof; renderer_bridge_test.go in loki-closure is
// the matching proof that the real Go endpoint actually renders through
// Loki. Test-only: production code never links this header.
class MockRendererBridge {
 public:
  explicit MockRendererBridge(std::string response_body)
      : response_body_(std::move(response_body)) {
    listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(listen_sock_, 1);
    socklen_t len = sizeof(addr);
    getsockname(listen_sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }

  ~MockRendererBridge() {
    if (thread_.joinable()) thread_.join();
    closesocket(listen_sock_);
  }

  int port() const { return port_; }
  const std::string& last_request_body() const { return last_request_body_; }

 private:
  void Serve() {
    SOCKET client = accept(listen_sock_, nullptr, nullptr);
    if (client == INVALID_SOCKET) return;

    std::string raw;
    char buf[4096];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
      int n = recv(client, buf, sizeof(buf), 0);
      if (n <= 0) {
        closesocket(client);
        return;
      }
      raw.append(buf, static_cast<size_t>(n));
      header_end = raw.find("\r\n\r\n");
    }

    size_t content_length = 0;
    size_t cl = raw.find("Content-Length:");
    if (cl != std::string::npos) {
      content_length = std::strtoul(raw.c_str() + cl + 15, nullptr, 10);
    }
    size_t have_body = raw.size() - (header_end + 4);
    while (have_body < content_length) {
      int n = recv(client, buf, sizeof(buf), 0);
      if (n <= 0) break;
      raw.append(buf, static_cast<size_t>(n));
      have_body += static_cast<size_t>(n);
    }
    last_request_body_ = raw.substr(header_end + 4, content_length);

    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                        std::to_string(response_body_.size()) +
                        "\r\nConnection: close\r\n\r\n" + response_body_;
    send(client, resp.data(), static_cast<int>(resp.size()), 0);
    closesocket(client);
  }

  SOCKET listen_sock_ = INVALID_SOCKET;
  int port_ = 0;
  std::string response_body_;
  std::string last_request_body_;
  std::thread thread_;
};

#endif  // BLINKER_TESTS_MOCK_RENDERER_BRIDGE_H_
