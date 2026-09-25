#include "lime/frame_window.h"

#include "wasmskia/canvas_bitmap.h"
#include "whp/base/executor.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#if defined(__wasi__)
// wasigocvm: no real <windows.h> in the wasm32-wasip2 sysroot -- FrameWindow
// occupancy is WASMWin32's simulated Win32/GDI session (see WASMLime's own
// README "WASM (wasigocvm)" section and wasigocvm_windows_shim.hpp's file
// comment for exactly what that does and doesn't give you).
#include "win32/wasigocvm_windows_shim.hpp"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#endif

#include <vector>

namespace lime {

namespace {

constexpr wchar_t kWindowClassName[] = L"WASMLimeFrameWindow";

// Tracks how many FrameWindows are currently alive so WM_DESTROY only
// quits the process' message loop once the *last* one closes -- a single
// consumer opening several FrameWindows (e.g. one browser window per tab)
// must be able to close any one of them without tearing down every other
// window's event pump too.
int g_live_windows = 0;

}  // namespace

namespace detail {

class FrameWindowImpl {
 public:
  FrameWindowImpl(blink::LocalFrame* local_frame, std::function<void(std::string)> on_gk_click,
                  FrameWindowOptions options)
      : local_frame_(local_frame),
        on_gk_click_(std::move(on_gk_click)),
        options_(options),
        alive_(std::make_shared<bool>(true)) {
    EnsureWindow();
  }

  ~FrameWindowImpl() {
    *alive_ = false;
    if (hwnd_ && IsWindow(hwnd_)) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
      HWND h = hwnd_;
      hwnd_ = nullptr;
      DestroyWindow(h);
    }
    Uncount();
  }

  void Uncount() {
    if (!counted_) return;
    counted_ = false;
    --g_live_windows;
    // UniShell's chrome HWND is not a FrameWindow. PostQuitMessage here
    // killed the shell when the first https tab's CreateView2 window closed.
  }

  void* native_handle() const { return hwnd_; }

  void SetBounds(int x, int y, int width, int height) {
    if (!hwnd_) return;
    if (width < 64) width = 64;
    if (height < 64) height = 64;
    const bool moved = x != options_.x || y != options_.y;
    const bool resized = width != options_.width || height != options_.height;
    if (!moved && !resized) return;
    options_.x = x;
    options_.y = y;
    options_.width = width;
    options_.height = height;
    MoveWindow(hwnd_, x, y, width, height, TRUE);
    if (resized) {
      SyncViewport();
      Repaint();
    }
  }

  void SetVisible(bool visible) {
    if (!hwnd_) return;
    const bool was = options_.visible;
    options_.visible = visible;
    if (visible) {
      // HWND_TOP without activating/focus — SetFocus here stole the omnibox
      // caret on every FrameManager Show (ping/DML/nav) and raced chrome
      // RmlUi input with the page child.
      SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
      if (!was) Repaint();
    } else if (was) {
      ShowWindow(hwnd_, SW_HIDE);
    }
  }

  // FrameImpl LoadGml/LoadHtml/Eval share this Mojo LocalFrame Remote.
  // While those are in flight, CaptureFrame/HandleClick must not run — nested
  // calls on the same remote corrupt delivery and crash on viewport clicks.
  void NoteExternalRemoteOp(bool on) {
    if (on) {
      ++remote_depth_;
      return;
    }
    if (remote_depth_ > 0) --remote_depth_;
    if (remote_depth_ == 0 && need_repaint_) {
      need_repaint_ = false;
      Repaint();
    }
  }

  // Real live-rendering repaint: raw RGBA pixels straight from
  // LocalFrame::CaptureFrame into wasmskia::Canvas -- no PNG encode on
  // the sending end, no decode here. Posted rather than done inline:
  // Repaint() is normally called *from inside* a response callback on
  // this same local_frame_ remote (LoadHTML/Eval's own callback, right
  // after getting their result) -- issuing a new call (CaptureFrame) on
  // that same remote while still nested inside dispatching its own
  // previous response corrupted delivery in a way that didn't crash but
  // silently never resolved (CaptureFrame's response never arrived even
  // after several RunUntilIdle() passes -- the window kept showing its
  // previous, now-stale frame). Posting lets whatever response callback
  // triggered this Repaint() fully return and unwind first; DoRepaint()
  // then runs from the idle-pump timer's shallow, non-nested
  // RunUntilIdle() instead (see browser_main.cc's IdlePumpTimerProc).
  void Repaint() {
    if (repaint_pending_) return;
    repaint_pending_ = true;
    std::shared_ptr<bool> alive = alive_;
    whp::Executor::Current().PostTask(whp::OnceClosure([this, alive]() {
      if (!*alive) return;
      repaint_pending_ = false;
      DoRepaint();
    }));
  }

  void DoRepaint() {
    // Never CaptureFrame while HandleClick/LoadGml/Eval owns the remote —
    // that nesting is what crashed on viewport button clicks.
    if (remote_depth_ > 0 || input_depth_ > 0 || capturing_) {
      need_repaint_ = true;
      return;
    }
    capturing_ = true;
    ++remote_depth_;
    bool ok = false;
    bool done = false;
    std::string rgba;
    uint32_t w = 0, h = 0;
    std::string error;
    local_frame_->CaptureFrame(
        [&](bool o, std::string data, uint32_t width, uint32_t height, std::string e) {
          ok = o;
          rgba = std::move(data);
          w = width;
          h = height;
          error = std::move(e);
          done = true;
        });
    for (int i = 0; i < 8 && !done; ++i) {
      whp::Executor::Current().RunUntilIdle();
    }
    if (remote_depth_ > 0) --remote_depth_;
    capturing_ = false;
    if (ok && w > 0 && h > 0 && rgba.size() == static_cast<size_t>(w) * h * 4) {
      skia::voodoo::BitmapN32 bitmap;
      bitmap.image_info.alpha_type = skia::voodoo::AlphaType::UNPREMUL;
      bitmap.image_info.width = w;
      bitmap.image_info.height = h;
      bitmap.pixel_data.assign(rgba.begin(), rgba.end());
      wasmskia::LoadBitmapN32(canvas_, bitmap);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    if (need_repaint_) {
      need_repaint_ = false;
      Repaint();
    }
  }

  RECT Client() const {
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    return rc;
  }

  void MapClick(int* x, int* y) const {
    RECT rc = Client();
    int cw = rc.right > 0 ? rc.right : 1;
    int bw = canvas_.width() > 0 ? canvas_.width() : cw;
    if (cw != bw && cw > 0) {
      *x = static_cast<int>(static_cast<long long>(*x) * bw / cw);
    }
    (void)rc;
  }

  void OnWheel(int x, int y, float delta_x, float delta_y) {
    MapClick(&x, &y);
    if (x < 0 || y < 0) return;
    if (remote_depth_ > 0 || input_depth_ > 0) return;
    ++input_depth_;
    ++remote_depth_;
    bool handled = false, done = false;
    local_frame_->HandleWheel(
        static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        static_cast<int32_t>(delta_x), static_cast<int32_t>(delta_y),
        [&](bool h, std::string /*error*/) {
          handled = h;
          done = true;
        });
    for (int i = 0; i < 8 && !done; ++i) {
      whp::Executor::Current().RunUntilIdle();
    }
    if (remote_depth_ > 0) --remote_depth_;
    --input_depth_;
    if (handled) Repaint();
    else if (need_repaint_) {
      need_repaint_ = false;
      Repaint();
    }
  }

  void SyncViewport() {
    if (!hwnd_ || !local_frame_) return;
    RECT rc = Client();
    uint32_t w = rc.right > 64 ? static_cast<uint32_t>(rc.right) : 64;
    uint32_t h = rc.bottom > 64 ? static_cast<uint32_t>(rc.bottom) : 64;
    local_frame_->SetViewport(w, h);
    for (int i = 0; i < 4; ++i) whp::Executor::Current().RunUntilIdle();
  }

  void OnClick(int x, int y) {
    MapClick(&x, &y);
    if (x < 0 || y < 0) return;
    // Drop click while LoadGml/CaptureFrame owns the remote — overlapping
    // HandleClick was the viewport-button crash.
    if (remote_depth_ > 0 || input_depth_ > 0) return;
    ++input_depth_;
    ++remote_depth_;
    bool hit = false, done = false;
    std::string gk_click, error;
    local_frame_->HandleClick(
        static_cast<uint32_t>(x), static_cast<uint32_t>(y),
        [&](bool h, std::string value, std::string e) {
          hit = h;
          gk_click = std::move(value);
          error = std::move(e);
          done = true;
        });
    for (int i = 0; i < 8 && !done; ++i) {
      whp::Executor::Current().RunUntilIdle();
    }
    if (remote_depth_ > 0) --remote_depth_;
    --input_depth_;
    if (hit && !gk_click.empty() && on_gk_click_) {
      auto on_gk_click = on_gk_click_;
      std::string value = gk_click;
      // InvokeEvent → LoadGml will Repaint; do not CaptureFrame here.
      whp::Executor::Current().PostTask(
          whp::OnceClosure([on_gk_click, value]() { on_gk_click(value); }));
    } else {
      Repaint();
    }
  }

  static std::string Utf16ToUtf8(wchar_t ch) {
    unsigned int cp = static_cast<unsigned int>(ch);
    std::string out;
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
  }

  static uint32_t KeyMods() {
    uint32_t mods = 0;
    if (GetKeyState(VK_MENU) < 0) mods |= 1;
    if (GetKeyState(VK_CONTROL) < 0) mods |= 2;
    if (GetKeyState(VK_SHIFT) < 0) mods |= 4;
    return mods;
  }

  void OnKey(uint32_t vk, uint32_t mods, const std::string& text) {
    if (remote_depth_ > 0 || input_depth_ > 0) return;
    ++input_depth_;
    ++remote_depth_;
    bool handled = false, done = false;
    std::string gk_event, value, error;
    local_frame_->HandleKey(vk, mods, text,
                            [&](bool h, std::string ev, std::string val, std::string e) {
                              handled = h;
                              gk_event = std::move(ev);
                              value = std::move(val);
                              error = std::move(e);
                              done = true;
                            });
    for (int i = 0; i < 8 && !done; ++i) {
      whp::Executor::Current().RunUntilIdle();
    }
    if (remote_depth_ > 0) --remote_depth_;
    --input_depth_;
    if (handled && !gk_event.empty() && on_gk_click_) {
      auto on_gk_click = on_gk_click_;
      std::string ev = gk_event;
      whp::Executor::Current().PostTask(
          whp::OnceClosure([on_gk_click, ev]() { on_gk_click(ev); }));
    } else if (handled) {
      Repaint();
    }
  }

  void OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc = Client();
    FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    int cw = rc.right;
    int ch = rc.bottom;
    int w = canvas_.width();
    int h = canvas_.height();
    if (w > 0 && h > 0 && cw > 0 && ch > 0) {
      const uint8_t* rgba = canvas_.pixels();
      std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
      for (size_t i = 0, n = bgra.size(); i < n; i += 4) {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
      }
      BITMAPINFO bmi{};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = w;
      bmi.bmiHeader.biHeight = -h;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;
      StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, w, h, bgra.data(), &bmi, DIB_RGB_COLORS,
                    SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<FrameWindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
      case WM_PAINT:
        if (self) {
          self->OnPaint();
          return 0;
        }
        break;
      case WM_LBUTTONDOWN:
        if (self && self->hwnd_) SetFocus(self->hwnd_);
        break;
      case WM_SIZE:
        if (self && wp != SIZE_MINIMIZED) {
          self->SyncViewport();
          self->Repaint();
          return 0;
        }
        break;
      case WM_MOUSEWHEEL:
        if (self) {
          const int delta = GET_WHEEL_DELTA_WPARAM(wp);
          POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
          ScreenToClient(hwnd, &pt);
          // Win32: positive delta = scroll up; Blinker positive delta_y = scroll down.
          self->OnWheel(pt.x, pt.y, 0.0f, -static_cast<float>(delta) / 2.0f);
          return 0;
        }
        break;
      case WM_LBUTTONUP:
        if (self) {
          if (self->hwnd_) SetFocus(self->hwnd_);
          self->OnClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
          return 0;
        }
        break;
      case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTALLKEYS;
      case WM_KEYDOWN:
        if (self) {
          uint32_t mods = FrameWindowImpl::KeyMods();
          WPARAM vk = wp;
          bool accel = (mods & 2) != 0 || (mods & 1) != 0 || vk == VK_BACK || vk == VK_RETURN ||
                       vk == VK_DELETE || vk == VK_F5 || vk == VK_LEFT || vk == VK_RIGHT;
          if (accel) {
            self->OnKey(static_cast<uint32_t>(vk), mods, "");
            return 0;
          }
        }
        break;
      case WM_CHAR:
        if (self) {
          wchar_t ch = static_cast<wchar_t>(wp);
          if (ch >= 32) self->OnKey(0, FrameWindowImpl::KeyMods(), FrameWindowImpl::Utf16ToUtf8(ch));
          return 0;
        }
        break;
      case WM_DESTROY:
        if (self) {
          self->hwnd_ = nullptr;
          self->Uncount();
        }
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  void EnsureWindow() {
    static bool class_registered = false;
    if (!class_registered) {
      WNDCLASSW wc{};
      wc.lpfnWndProc = &FrameWindowImpl::WndProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = kWindowClassName;
      wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      wc.hbrBackground = CreateSolidBrush(RGB(11, 14, 19));
      RegisterClassW(&wc);
      class_registered = true;
    }
    HWND parent = options_.child ? static_cast<HWND>(options_.parent) : nullptr;
    DWORD style = options_.child ? (WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                                 : WS_OVERLAPPEDWINDOW;
    int x = options_.child ? options_.x : CW_USEDEFAULT;
    int y = options_.child ? options_.y : CW_USEDEFAULT;
    int w = options_.width > 0 ? options_.width : 1100;
    int h = options_.height > 0 ? options_.height : 800;
    hwnd_ = CreateWindowExW(0, kWindowClassName, L"", style, x, y, w, h, parent, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (hwnd_ && !options_.child) {
      ++g_live_windows;
      counted_ = true;
    }
    if (hwnd_ && options_.visible) ShowWindow(hwnd_, SW_SHOW);
    SyncViewport();
  }

  blink::LocalFrame* local_frame_;
  std::function<void(std::string)> on_gk_click_;
  FrameWindowOptions options_;
  HWND hwnd_ = nullptr;
  bool counted_ = false;
  wasmskia::Canvas canvas_{1, 1};
  // Set false in the destructor and checked before a posted Repaint()
  // task touches `this` -- guards against a tab closing while its own
  // repaint is still queued (see Repaint()'s own comment for why
  // repainting is posted rather than done inline).
  std::shared_ptr<bool> alive_;
  bool repaint_pending_ = false;
  bool capturing_ = false;
  bool need_repaint_ = false;
  int remote_depth_ = 0;
  int input_depth_ = 0;
};

}  // namespace detail

FrameWindow::FrameWindow(blink::LocalFrame* local_frame, std::function<void(std::string)> on_gk_click)
    : FrameWindow(local_frame, std::move(on_gk_click), {}) {}

FrameWindow::FrameWindow(blink::LocalFrame* local_frame, std::function<void(std::string)> on_gk_click,
                         FrameWindowOptions options)
    : impl_(std::make_unique<detail::FrameWindowImpl>(local_frame, std::move(on_gk_click),
                                                      options)) {}

FrameWindow::~FrameWindow() = default;

void FrameWindow::Repaint() { impl_->Repaint(); }

void FrameWindow::NoteExternalRemoteOp(bool on) { impl_->NoteExternalRemoteOp(on); }

void* FrameWindow::native_handle() const { return impl_->native_handle(); }

void FrameWindow::SetBounds(int x, int y, int width, int height) {
  impl_->SetBounds(x, y, width, height);
}

void FrameWindow::SetVisible(bool visible) { impl_->SetVisible(visible); }

}  // namespace lime
