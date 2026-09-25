#ifndef LIME_NAVIGATION_CONTROLLER_IMPL_H_
#define LIME_NAVIGATION_CONTROLLER_IMPL_H_

#include "navigation_gen.h"

#include "renderer_interface_gen.h"

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

#include <functional>
#include <string>
#include <vector>

namespace lime {

// Real navigation ledger: LoadUrl/GoBack/GoForward/Reload drive the
// same blink::LocalFrame this Frame's other surfaces use (Renderer
// CreateFrame include hop, LocalFrameImpl parse/layout/paint -- not Loki).
// No back-forward cache: GoBack/GoForward re-navigate to the
// stored URL rather than restoring a cached document, same tradeoff
// Stop() being a no-op already documents (navigation completes
// synchronously in this stack, so there's never anything mid-flight to
// cancel or a cached prior render to restore).
class NavigationControllerImpl : public NavigationController {
 public:
  explicit NavigationControllerImpl(blink::LocalFrame* local_frame);

  void LoadUrl(const std::string& url,
              base::OnceCallback<void(bool, std::string)> callback) override;
  void GoBack() override;
  void GoForward() override;
  void Stop() override;
  void Reload() override;
  void GetVisibleEntry(base::OnceCallback<void(NavigationState)> callback) override;

  // Binds/replaces the listener. Per real fuchsia.web semantics, the
  // listener receives the current state immediately upon being set.
  void SetListener(mojo::PendingAssociatedRemote<NavigationEventListener> listener);

  // Invoked after every state change (in addition to the listener
  // notification) -- FrameImpl uses this to repaint its CreateView2
  // window, if one is open.
  void set_state_changed_hook(std::function<void()> hook) {
    state_changed_hook_ = std::move(hook);
  }

  const NavigationState& current_state() const { return current_; }

 private:
  void ApplyNavigateResult(const std::string& url, bool ok,
                           const std::string& title, const std::string& error);
  void NotifyListener();

  blink::LocalFrame* local_frame_;
  NavigationState current_;
  std::vector<std::string> history_;
  size_t history_index_ = 0;

  mojo::AssociatedRemote<NavigationEventListener> listener_;
  bool notify_in_flight_ = false;
  bool notify_dirty_ = false;

  std::function<void()> state_changed_hook_;
};

}  // namespace lime

#endif  // LIME_NAVIGATION_CONTROLLER_IMPL_H_
