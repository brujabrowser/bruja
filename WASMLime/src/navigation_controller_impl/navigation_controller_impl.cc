#include "lime/navigation_controller_impl.h"

#include <utility>

namespace lime {

NavigationControllerImpl::NavigationControllerImpl(blink::LocalFrame* local_frame)
    : local_frame_(local_frame) {}

void NavigationControllerImpl::LoadUrl(
    const std::string& url, base::OnceCallback<void(bool, std::string)> callback) {
  if (url.empty()) {
    callback(false, "invalid url");
    return;
  }
  local_frame_->Navigate(
      url, [this, url, callback = std::move(callback)](
               bool ok, std::string title, uint32_t, uint32_t, std::string error) mutable {
        // Truncate any forward history before appending, matching how a
        // real browser drops the forward stack on a fresh navigation from
        // a back/forward state.
        if (!history_.empty()) history_.resize(history_index_ + 1);
        history_.push_back(url);
        history_index_ = history_.size() - 1;
        ApplyNavigateResult(url, ok, title, error);
        callback(ok, error);
      });
}

void NavigationControllerImpl::GoBack() {
  if (history_.empty() || history_index_ == 0) return;
  --history_index_;
  const std::string url = history_[history_index_];
  local_frame_->Navigate(url, [this, url](bool ok, std::string title, uint32_t, uint32_t,
                                          std::string error) {
    ApplyNavigateResult(url, ok, title, error);
  });
}

void NavigationControllerImpl::GoForward() {
  if (history_index_ + 1 >= history_.size()) return;
  ++history_index_;
  const std::string url = history_[history_index_];
  local_frame_->Navigate(url, [this, url](bool ok, std::string title, uint32_t, uint32_t,
                                          std::string error) {
    ApplyNavigateResult(url, ok, title, error);
  });
}

void NavigationControllerImpl::Stop() {
  // No-op by construction: navigation completes synchronously in this
  // stack (see LoadUrl above), so there is never anything mid-flight to
  // cancel.
}

void NavigationControllerImpl::Reload() {
  if (current_.url.empty()) return;
  const std::string url = current_.url;
  local_frame_->Navigate(url, [this, url](bool ok, std::string title, uint32_t, uint32_t,
                                          std::string error) {
    ApplyNavigateResult(url, ok, title, error);
  });
}

void NavigationControllerImpl::GetVisibleEntry(
    base::OnceCallback<void(NavigationState)> callback) {
  callback(current_);
}

void NavigationControllerImpl::SetListener(
    mojo::PendingAssociatedRemote<NavigationEventListener> listener) {
  listener_.Bind(std::move(listener));
  // Real fuchsia.web semantics: the listener receives the current state
  // immediately upon being set.
  NotifyListener();
}

void NavigationControllerImpl::ApplyNavigateResult(const std::string& url, bool ok,
                                                   const std::string& title,
                                                   const std::string& error) {
  (void)error;
  current_.url = url;
  current_.title = ok ? title : current_.title;
  current_.page_type = ok ? PageType::NORMAL : PageType::ERROR_PAGE;
  current_.can_go_back = history_index_ > 0;
  current_.can_go_forward = !history_.empty() && history_index_ + 1 < history_.size();
  current_.is_main_document_loaded = ok;
  NotifyListener();
  if (state_changed_hook_) state_changed_hook_();
}

void NavigationControllerImpl::NotifyListener() {
  if (!listener_.is_bound()) return;
  if (notify_in_flight_) {
    notify_dirty_ = true;
    return;
  }
  notify_in_flight_ = true;
  notify_dirty_ = false;
  listener_->OnNavigationStateChanged(current_, [this]() {
    notify_in_flight_ = false;
    if (notify_dirty_) NotifyListener();
  });
}

}  // namespace lime
