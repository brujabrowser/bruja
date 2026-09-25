#include "lime/context_impl.h"

#include "lime/frame_impl.h"

#include <utility>

namespace lime {

ContextImpl::ContextImpl(wpr::TaskManager* task_manager, int process_id,
                         std::string renderer_host, int renderer_port)
    : task_manager_(task_manager),
      process_id_(process_id),
      renderer_impl_(std::move(renderer_host), renderer_port) {
  renderer_receivers_.Add(&renderer_impl_,
                          renderer_remote_.BindNewPipeAndPassReceiver());
}

ContextImpl::~ContextImpl() = default;

void ContextImpl::CreateFrame(mojo::PendingAssociatedReceiver<Frame> frame) {
  CreateFrameInternal("", std::move(frame));
}

void ContextImpl::CreateFrameWithParams(const CreateFrameParams& params,
                                        mojo::PendingAssociatedReceiver<Frame> frame) {
  CreateFrameInternal(params.debug_name, std::move(frame));
}

void ContextImpl::CreateFrameInternal(const std::string& debug_name,
                                      mojo::PendingAssociatedReceiver<Frame> frame) {
  const int tab_id = next_tab_id_++;
  task_manager_->AttachTask(process_id_, tab_id,
                            debug_name.empty() ? "Frame" : debug_name);

  // std::function (RendererImpl::CreateFrame's callback parameter) requires
  // a copy-constructible target -- a move-only PendingAssociatedReceiver
  // capture makes the closure itself move-only, so it's boxed in a
  // shared_ptr purely to satisfy that, not for any shared-ownership reason.
  auto frame_receiver = std::make_shared<mojo::PendingAssociatedReceiver<Frame>>(std::move(frame));
  renderer_remote_->CreateFrame(
      [this, tab_id, frame_receiver](mojo::PendingRemote<blink::LocalFrame> pending_frame) {
        auto impl = std::make_unique<FrameImpl>(task_manager_, process_id_, tab_id,
                                                std::move(pending_frame));
        frame_receivers_.Add(std::unique_ptr<Frame>(impl.release()),
                             std::move(*frame_receiver));
      });
}

}  // namespace lime
