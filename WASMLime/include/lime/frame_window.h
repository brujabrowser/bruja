#ifndef LIME_FRAME_WINDOW_H_
#define LIME_FRAME_WINDOW_H_

#include "renderer_interface_gen.h"

#include <functional>
#include <memory>
#include <string>

namespace lime {

namespace detail {
class FrameWindowImpl;
}  // namespace detail

// How to create the Win32 window behind a Frame. Default is a top-level
// overlapped window (CreateView2). Bruja Browser hosts chrome + tab
// frames as WS_CHILD windows of one browser HWND.
struct FrameWindowOptions {
  void* parent = nullptr;  // HWND, when child == true
  int x = 0;
  int y = 0;
  int width = 1100;
  int height = 800;
  bool child = false;
  bool visible = true;
};

// The real CreateView2 / CreateChildView backing: a Win32 window that
// repaints from local_frame->CaptureFrame() -- raw RGBA pixels straight
// into wasmskia::Canvas -> StretchDIBits, no PNG encode/decode round trip
// anywhere in this path (CaptureSnapshot's PNG contract is for one-off
// snapshot callers, not this live-repaint loop -- see blink/paint.h).
// Built fresh here rather than reusing webview::webview directly: that
// class constructs its own internal blink::LocalFrameImpl (its own tab),
// which would be a second session disconnected from the one this Frame's
// NavigationController is actually driving -- wrong for parity. This
// window instead repaints from the *same* LocalFrame FrameImpl already
// owns, driven by FrameImpl's Repaint() calls after each
// Navigate/LoadHTML/Eval, not by its own polling loop. Real mouse clicks
// (WM_LBUTTONUP) are forwarded through local_frame->HandleClick's real
// layout hit-test; `on_gk_click` fires when a click hits an element
// carrying a "gk-click" attribute (see FrameImpl::CreateView2).
class FrameWindow {
 public:
  explicit FrameWindow(blink::LocalFrame* local_frame,
                       std::function<void(std::string)> on_gk_click = {});
  FrameWindow(blink::LocalFrame* local_frame, std::function<void(std::string)> on_gk_click,
              FrameWindowOptions options);
  ~FrameWindow();

  FrameWindow(const FrameWindow&) = delete;
  FrameWindow& operator=(const FrameWindow&) = delete;

  // Re-fetches CaptureFrame and repaints the window. Safe to call
  // whether or not the window is currently visible/focused.
  void Repaint();

  // Hold while FrameImpl LoadGml/LoadHtml/Eval owns the Mojo LocalFrame
  // remote so CaptureFrame/HandleClick cannot nest (viewport click crash).
  void NoteExternalRemoteOp(bool on);

  void* native_handle() const;  // HWND
  void SetBounds(int x, int y, int width, int height);
  void SetVisible(bool visible);

 private:
  std::unique_ptr<detail::FrameWindowImpl> impl_;
};

}  // namespace lime

#endif  // LIME_FRAME_WINDOW_H_
