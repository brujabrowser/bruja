#include "lime/frame_impl.h"

#include <cstdint>
#include <utility>

namespace lime {

FrameImpl::FrameImpl(wpr::TaskManager* task_manager, int process_id, int tab_id,
                     mojo::PendingRemote<blink::LocalFrame> pending_frame)
    : task_manager_(task_manager), process_id_(process_id), tab_id_(tab_id) {
  local_frame_.Bind(std::move(pending_frame));
}

FrameImpl::~FrameImpl() = default;

void FrameImpl::EnsureNavigationController() {
  if (navigation_controller_) return;
  navigation_controller_ = std::make_unique<NavigationControllerImpl>(local_frame_.get());
  navigation_controller_->set_state_changed_hook([this]() {
    if (window_) window_->Repaint();
  });
}

void FrameImpl::GetNavigationController(
    mojo::PendingAssociatedReceiver<NavigationController> controller) {
  EnsureNavigationController();
  auto receiver =
      std::make_unique<mojo::AssociatedReceiver<NavigationController>>(navigation_controller_.get());
  receiver->Bind(std::move(controller));
  navigation_controller_receivers_.push_back(std::move(receiver));
}

void FrameImpl::SetNavigationEventListener(
    mojo::PendingAssociatedRemote<NavigationEventListener> listener) {
  EnsureNavigationController();
  navigation_controller_->SetListener(std::move(listener));
}

void FrameImpl::ExecuteJavaScript(
    const std::string& js,
    base::OnceCallback<void(bool, std::string, std::string)> callback) {
  if (window_) window_->NoteExternalRemoteOp(true);
  local_frame_->Eval(js, [this, callback = std::move(callback)](
                             bool ok, std::string result_json, std::string error) mutable {
    if (window_) {
      window_->NoteExternalRemoteOp(false);
      window_->Repaint();
    }
    callback(ok, result_json, error);
  });
}

void FrameImpl::ExecuteJavaScriptNoResult(
    const std::string& js, base::OnceCallback<void(bool, std::string)> callback) {
  if (window_) window_->NoteExternalRemoteOp(true);
  local_frame_->Eval(js, [this, callback = std::move(callback)](
                             bool ok, std::string, std::string error) mutable {
    if (window_) {
      window_->NoteExternalRemoteOp(false);
      window_->Repaint();
    }
    callback(ok, error);
  });
}

void FrameImpl::LoadHtml(const std::string& html,
                         base::OnceCallback<void(bool, std::string)> callback) {
  if (window_) window_->NoteExternalRemoteOp(true);
  local_frame_->LoadHTML(html, [this, callback = std::move(callback)](
                                   bool ok, std::string, uint32_t, uint32_t,
                                   std::string error) mutable {
    if (window_) {
      window_->NoteExternalRemoteOp(false);
      window_->Repaint();
    }
    callback(ok, error);
  });
}

void FrameImpl::LoadGml(const std::string& gml, const std::string& data_json,
                        base::OnceCallback<void(bool, std::string)> callback) {
  if (window_) window_->NoteExternalRemoteOp(true);
  local_frame_->LoadGML(
      gml, data_json,
      [this, callback = std::move(callback)](bool ok, std::string, uint32_t, uint32_t,
                                             std::string error) mutable {
        if (window_) {
          window_->NoteExternalRemoteOp(false);
          window_->Repaint();
        }
        callback(ok, error);
      });
}

void FrameImpl::HandleClick(
    uint32_t x, uint32_t y,
    base::OnceCallback<void(bool, std::string, std::string)> callback) {
  local_frame_->HandleClick(x, y, std::move(callback));
}

void FrameImpl::HandleKey(
    uint32_t vk, uint32_t mods, const std::string& text,
    base::OnceCallback<void(bool, std::string, std::string, std::string)> callback) {
  local_frame_->HandleKey(vk, mods, text, std::move(callback));
}

void FrameImpl::SetFrameClickListener(
    mojo::PendingAssociatedRemote<FrameClickListener> listener) {
  click_listener_.Bind(std::move(listener));
}

void FrameImpl::CreateView2() {
  if (window_) return;
  window_ = std::make_unique<FrameWindow>(local_frame_.get(), [this](std::string gk_click) {
    if (click_listener_.is_bound()) {
      click_listener_->OnGkClick(gk_click, [] {});
    }
  });
  window_->Repaint();
}

void FrameImpl::CreateChildView(uint64_t parent, int32_t x, int32_t y, int32_t width,
                               int32_t height) {
  if (window_) {
    window_->SetBounds(x, y, width, height);
    window_->SetVisible(true);
    return;
  }
  FrameWindowOptions opt;
  opt.parent = reinterpret_cast<void*>(static_cast<uintptr_t>(parent));
  opt.x = x;
  opt.y = y;
  opt.width = width;
  opt.height = height;
  opt.child = true;
  opt.visible = true;
  window_ = std::make_unique<FrameWindow>(
      local_frame_.get(),
      [this](std::string gk_click) {
        if (click_listener_.is_bound()) {
          click_listener_->OnGkClick(gk_click, [] {});
        }
      },
      opt);
  window_->Repaint();
}

void FrameImpl::SetViewBounds(int32_t x, int32_t y, int32_t width, int32_t height) {
  if (window_) window_->SetBounds(x, y, width, height);
}

void FrameImpl::SetViewVisible(bool visible) {
  if (window_) window_->SetVisible(visible);
}

void FrameImpl::GetViewHandle(base::OnceCallback<void(uint64_t)> callback) {
  uint64_t hwnd = 0;
  if (window_ && window_->native_handle()) {
    hwnd = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window_->native_handle()));
  }
  callback(hwnd);
}

void FrameImpl::SetJavaScriptLogLevel(ConsoleLogLevel level) { log_level_ = level; }

void FrameImpl::SetPermissionState(int32_t permission_type, const std::string& web_origin,
                                   bool granted) {
  for (auto& p : permissions_) {
    if (p.type == permission_type && p.origin == web_origin) {
      p.granted = granted;
      return;
    }
  }
  permissions_.push_back({permission_type, web_origin, granted});
}

void FrameImpl::ConfigureInputTypes(uint64_t types, AllowInputState allow) {
  if (allow == AllowInputState::ALLOW) {
    allowed_input_types_ |= types;
  } else {
    allowed_input_types_ &= ~types;
  }
}

void FrameImpl::SetContentAreaSettings(bool hide_scrollbars, double page_scale) {
  hide_scrollbars_ = hide_scrollbars;
  page_scale_ = page_scale;
}

void FrameImpl::ResetContentAreaSettings() {
  hide_scrollbars_ = false;
  page_scale_ = 1.0;
}

void FrameImpl::AddBeforeLoadJavaScript(uint64_t id,
                                        const v8::internal::CageVector<std::string>& origins,
                                        const std::string& script,
                                        base::OnceCallback<void(bool, std::string)> callback) {
  if (origins.empty()) {
    callback(false, "origins must not be empty");
    return;
  }
  before_load_scripts_[id] =
      BeforeLoadScript{std::vector<std::string>(origins.begin(), origins.end()), script};
  callback(true, "");
}

void FrameImpl::RemoveBeforeLoadJavaScript(uint64_t id) { before_load_scripts_.erase(id); }

void FrameImpl::PostMessage(const std::string& target_origin, const std::string& data,
                            base::OnceCallback<void(bool, std::string)> callback) {
  if (data.empty()) {
    callback(false, "no data in message");
    return;
  }
  posted_messages_.push_back({target_origin, data});
  callback(true, "");
}

void FrameImpl::Close() {
  window_.reset();
  navigation_controller_receivers_.clear();
  navigation_controller_.reset();
  task_manager_->CloseTab(tab_id_);
}

}  // namespace lime
