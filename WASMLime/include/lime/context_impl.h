#ifndef LIME_CONTEXT_IMPL_H_
#define LIME_CONTEXT_IMPL_H_

#include "context_gen.h"

#include "content/renderer_impl.h"

#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/unique_associated_receiver_set.h"

#include "wpr/store.h"

#include <memory>
#include <string>

namespace lime {

class FrameImpl;

// One Context = one wpr::Process plus one content::Renderer include hop
// (WASMRenderer RendererImpl). CreateFrame() returns pending_remote<
// blink::LocalFrame> onto LocalFrameImpl. Not Loki. Each Frame attaches
// as a task (tab) via task_manager_->AttachTask.
class ContextImpl : public Context {
 public:
  ContextImpl(wpr::TaskManager* task_manager, int process_id,
             std::string renderer_host, int renderer_port);
  ~ContextImpl() override;

  void CreateFrame(mojo::PendingAssociatedReceiver<Frame> frame) override;
  void CreateFrameWithParams(const CreateFrameParams& params,
                             mojo::PendingAssociatedReceiver<Frame> frame) override;

  int process_id() const { return process_id_; }

 private:
  void CreateFrameInternal(const std::string& debug_name,
                           mojo::PendingAssociatedReceiver<Frame> frame);

  wpr::TaskManager* task_manager_;
  int process_id_;
  int next_tab_id_ = 1;

  content::RendererImpl renderer_impl_;
  mojo::ReceiverSet<content::Renderer> renderer_receivers_;
  mojo::Remote<content::Renderer> renderer_remote_;

  mojo::UniqueAssociatedReceiverSet<Frame> frame_receivers_;
};

}  // namespace lime

#endif  // LIME_CONTEXT_IMPL_H_
