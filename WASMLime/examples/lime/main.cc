// Real end-to-end demo: ContextProviderImpl::Create -> Context::CreateFrame
// (Renderer include hop) -> Frame::GetNavigationController ->
// NavigationController::LoadUrl -- LocalFrameImpl Navigate, not Loki.
// ExecuteJavaScript/CreateView2 are exercised too, but both are still
// honest stubs (no native JS engine or paint pipeline wired up yet -- see
// WASMBlinker's README) -- expect ok=false from ExecuteJavaScript and a
// blank CreateView2 window for now.
//
//   wlm_example.exe [url]     (default: http://example.com/)
//
// https:// URLs aren't supported yet (no TLS in this stack).
#include "lime/context_provider_main.h"

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "whp/base/executor.h"

#if defined(__wasi__)
#include "win32/wasigocvm_windows_shim.hpp"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void Pump(int iterations = 8) {
  for (int i = 0; i < iterations; ++i) whp::Executor::Current().RunUntilIdle();
}

class ListenerImpl : public lime::NavigationEventListener {
 public:
  void OnNavigationStateChanged(const lime::NavigationState& change,
                                base::OnceCallback<void()> callback) override {
    std::printf("navigation state changed: url=%s title=%s loaded=%d\n", change.url.c_str(),
               change.title.c_str(), change.is_main_document_loaded);
    callback();
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string url = argc > 1 ? argv[1] : "http://example.com/";

  // host/port are vestigial today (Navigate parses the target URL itself,
  // see WASMBlinker's LocalFrameImpl) -- kept only for constructor
  // compatibility across the family.
  lime::ContextProviderHandle provider = lime::ContextProviderMain("unused", 0);
  mojo::AssociatedGroup group = provider.remote.associated_group();

  lime::CreateContextParams params;
  mojo::PendingAssociatedRemote<lime::Context> context_remote;
  provider.remote->Create(params, context_remote.InitWithNewEndpointAndPassReceiver(group));
  mojo::AssociatedRemote<lime::Context> context;
  context.Bind(std::move(context_remote));
  Pump();

  mojo::PendingAssociatedRemote<lime::Frame> frame_remote;
  context->CreateFrame(frame_remote.InitWithNewEndpointAndPassReceiver(group));
  mojo::AssociatedRemote<lime::Frame> frame;
  frame.Bind(std::move(frame_remote));
  Pump();

  mojo::PendingAssociatedRemote<lime::NavigationController> nav_remote;
  frame->GetNavigationController(nav_remote.InitWithNewEndpointAndPassReceiver(group));
  mojo::AssociatedRemote<lime::NavigationController> nav;
  nav.Bind(std::move(nav_remote));

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<lime::NavigationEventListener> listener_receiver(&listener_impl);
  mojo::PendingAssociatedRemote<lime::NavigationEventListener> listener_remote;
  listener_receiver.Bind(listener_remote.InitWithNewEndpointAndPassReceiver(group));
  frame->SetNavigationEventListener(std::move(listener_remote));
  Pump();

  bool ok = false;
  std::string error;
  nav->LoadUrl(url, [&](bool o, std::string e) {
    ok = o;
    error = e;
  });
  Pump();
  if (!ok) {
    std::fprintf(stderr, "LoadUrl failed: %s\n", error.c_str());
    return 1;
  }

  frame->ExecuteJavaScript("1+1", [](bool js_ok, std::string result_json, std::string js_error) {
    std::printf("ExecuteJavaScript: ok=%d result=%s error=%s\n", js_ok, result_json.c_str(),
               js_error.c_str());
  });
  Pump();

  frame->CreateView2();
  Pump();

  std::printf("wlm_example done -- close the window to exit.\n");
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
