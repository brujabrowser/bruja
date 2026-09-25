// Real end-to-end round trip: ContextProviderImpl::Create ->
// ContextImpl::CreateFrame -> FrameImpl::GetNavigationController ->
// NavigationController::LoadUrl -> NavigationEventListener::
// OnNavigationStateChanged, over real Mojo associated interfaces (no
// in-process shortcuts at the IPC layer), against a one-shot mock HTTP
// renderer bridge standing in for a real Cadmium process -- same
// technique WASMBlinker's own mock_renderer_bridge.h uses.
#include "test.h"

#include "lime/context_provider_main.h"

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "whp/base/executor.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

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
    int len = sizeof(addr);
    getsockname(listen_sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }

  ~MockRendererBridge() {
    if (thread_.joinable()) thread_.join();
    closesocket(listen_sock_);
  }

  int port() const { return port_; }

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
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                       std::to_string(response_body_.size()) +
                       "\r\nConnection: close\r\n\r\n" + response_body_;
    send(client, resp.data(), static_cast<int>(resp.size()), 0);
    closesocket(client);
  }

  SOCKET listen_sock_ = INVALID_SOCKET;
  int port_ = 0;
  std::string response_body_;
  std::thread thread_;
};

struct WinsockInit {
  WinsockInit() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  }
  ~WinsockInit() { WSACleanup(); }
} g_winsock_init;

void Pump(int iterations = 8) {
  for (int i = 0; i < iterations; ++i) whp::Executor::Current().RunUntilIdle();
}

}  // namespace

TEST(navigation_round_trip_through_mock_renderer) {
  MockRendererBridge mock("<html><head><title>Mock Page</title></head><body>hi</body></html>");

  lime::ContextProviderHandle provider = lime::ContextProviderMain("unused", 0);

  bool created = false;
  lime::CreateContextParams params;
  mojo::AssociatedGroup group = provider.remote.associated_group();
  mojo::PendingAssociatedRemote<lime::Context> context_remote;
  mojo::PendingAssociatedReceiver<lime::Context> context_receiver =
      context_remote.InitWithNewEndpointAndPassReceiver(group);
  provider.remote->Create(params, std::move(context_receiver));
  mojo::AssociatedRemote<lime::Context> context;
  context.Bind(std::move(context_remote));
  Pump();
  created = context.is_bound();
  EXPECT(created);

  mojo::PendingAssociatedRemote<lime::Frame> frame_remote;
  mojo::PendingAssociatedReceiver<lime::Frame> frame_receiver =
      frame_remote.InitWithNewEndpointAndPassReceiver(group);
  context->CreateFrame(std::move(frame_receiver));
  mojo::AssociatedRemote<lime::Frame> frame;
  frame.Bind(std::move(frame_remote));
  Pump();
  EXPECT(frame.is_bound());

  mojo::PendingAssociatedRemote<lime::NavigationController> nav_remote;
  mojo::PendingAssociatedReceiver<lime::NavigationController> nav_receiver =
      nav_remote.InitWithNewEndpointAndPassReceiver(group);
  frame->GetNavigationController(std::move(nav_receiver));
  mojo::AssociatedRemote<lime::NavigationController> nav;
  nav.Bind(std::move(nav_remote));
  Pump();
  EXPECT(nav.is_bound());

  struct ListenerImpl : lime::NavigationEventListener {
    void OnNavigationStateChanged(const lime::NavigationState& change,
                                  base::OnceCallback<void()> callback) override {
      last = change;
      ++calls;
      callback();
    }
    lime::NavigationState last;
    int calls = 0;
  } listener_impl;
  mojo::AssociatedReceiver<lime::NavigationEventListener> listener_receiver_obj(&listener_impl);
  mojo::PendingAssociatedRemote<lime::NavigationEventListener> listener_remote;
  mojo::PendingAssociatedReceiver<lime::NavigationEventListener> listener_pending_receiver =
      listener_remote.InitWithNewEndpointAndPassReceiver(group);
  frame->SetNavigationEventListener(std::move(listener_remote));
  listener_receiver_obj.Bind(std::move(listener_pending_receiver));
  Pump();

  std::string url = "http://127.0.0.1:" + std::to_string(mock.port()) + "/";
  bool load_ok = false;
  std::string load_error;
  nav->LoadUrl(url, [&](bool ok, std::string error) {
    load_ok = ok;
    load_error = error;
  });
  Pump();

  EXPECT(load_ok);
  EXPECT(load_error.empty());
  EXPECT(listener_impl.calls >= 2);  // initial empty state + the real load
  EXPECT_EQ(listener_impl.last.url, url);
  EXPECT_EQ(listener_impl.last.title, "Mock Page");
  EXPECT(listener_impl.last.is_main_document_loaded);
}
