#ifndef LIME_FRAME_IMPL_H_
#define LIME_FRAME_IMPL_H_

#include "frame_gen.h"

#include "lime/frame_window.h"
#include "lime/navigation_controller_impl.h"

#include "renderer_interface_gen.h"

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

#include "wpr/store.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lime {

// One FrameImpl owns one blink::LocalFrame Remote from Renderer.CreateFrame
// (WASMRenderer include hop), backed by blink::LocalFrameImpl (parse /
// script / layout / paint -- WASMSkia+WASMRasta when present). Not Loki.
// NavigationController::LoadUrl, ExecuteJavaScript, and CreateView2 share
// that same live session.
class FrameImpl : public Frame {
 public:
  FrameImpl(wpr::TaskManager* task_manager, int process_id, int tab_id,
           mojo::PendingRemote<blink::LocalFrame> pending_frame);
  ~FrameImpl() override;

  void GetNavigationController(
      mojo::PendingAssociatedReceiver<NavigationController> controller) override;
  void SetNavigationEventListener(
      mojo::PendingAssociatedRemote<NavigationEventListener> listener) override;
  void ExecuteJavaScript(const std::string& js,
                        base::OnceCallback<void(bool, std::string, std::string)> callback) override;
  void ExecuteJavaScriptNoResult(const std::string& js,
                                base::OnceCallback<void(bool, std::string)> callback) override;
  void LoadHtml(const std::string& html,
               base::OnceCallback<void(bool, std::string)> callback) override;
  void LoadGml(const std::string& gml, const std::string& data_json,
               base::OnceCallback<void(bool, std::string)> callback) override;
  void HandleClick(uint32_t x, uint32_t y,
                   base::OnceCallback<void(bool, std::string, std::string)> callback) override;
  void HandleKey(uint32_t vk, uint32_t mods, const std::string& text,
                 base::OnceCallback<void(bool, std::string, std::string, std::string)> callback)
      override;
  void SetFrameClickListener(
      mojo::PendingAssociatedRemote<FrameClickListener> listener) override;
  void CreateView2() override;
  void CreateChildView(uint64_t parent, int32_t x, int32_t y, int32_t width,
                       int32_t height) override;
  void SetViewBounds(int32_t x, int32_t y, int32_t width, int32_t height) override;
  void SetViewVisible(bool visible) override;
  void GetViewHandle(base::OnceCallback<void(uint64_t)> callback) override;
  void SetJavaScriptLogLevel(ConsoleLogLevel level) override;
  void SetPermissionState(int32_t permission_type, const std::string& web_origin,
                          bool granted) override;
  void ConfigureInputTypes(uint64_t types, AllowInputState allow) override;
  void SetContentAreaSettings(bool hide_scrollbars, double page_scale) override;
  void ResetContentAreaSettings() override;
  void AddBeforeLoadJavaScript(uint64_t id, const v8::internal::CageVector<std::string>& origins,
                              const std::string& script,
                              base::OnceCallback<void(bool, std::string)> callback) override;
  void RemoveBeforeLoadJavaScript(uint64_t id) override;
  void PostMessage(const std::string& target_origin, const std::string& data,
                   base::OnceCallback<void(bool, std::string)> callback) override;
  void Close() override;

  blink::LocalFrame* local_frame() { return local_frame_.get(); }

 private:
  void EnsureNavigationController();

  wpr::TaskManager* task_manager_;
  int process_id_;
  int tab_id_;

  mojo::Remote<blink::LocalFrame> local_frame_;
  mojo::AssociatedRemote<FrameClickListener> click_listener_;
  std::unique_ptr<NavigationControllerImpl> navigation_controller_;
  std::vector<std::unique_ptr<mojo::AssociatedReceiver<NavigationController>>>
      navigation_controller_receivers_;
  std::unique_ptr<FrameWindow> window_;

  ConsoleLogLevel log_level_ = ConsoleLogLevel::NONE;
  struct Permission {
    int32_t type;
    std::string origin;
    bool granted;
  };
  std::vector<Permission> permissions_;
  uint64_t allowed_input_types_ = ~static_cast<uint64_t>(0);
  bool hide_scrollbars_ = false;
  double page_scale_ = 1.0;
  struct BeforeLoadScript {
    std::vector<std::string> origins;
    std::string script;
  };
  std::unordered_map<uint64_t, BeforeLoadScript> before_load_scripts_;
  struct PostedMessage {
    std::string target_origin;
    std::string data;
  };
  std::vector<PostedMessage> posted_messages_;
};

}  // namespace lime

#endif  // LIME_FRAME_IMPL_H_
