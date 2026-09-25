#ifndef LIME_CONTEXT_PROVIDER_IMPL_H_
#define LIME_CONTEXT_PROVIDER_IMPL_H_

#include "context_provider_gen.h"

#include "mojo/public/cpp/bindings/associated_receiver.h"

#include "wpr/store.h"

#include <memory>
#include <string>
#include <vector>

namespace lime {

class ContextImpl;

// Real Fuchsia's Create() spawns a fresh sandboxed OS process per Context.
// Nothing in this stack spawns processes -- but WASMProcess's
// wpr::TaskManager already gives us the exact real, tested analog used
// elsewhere in this family (chrome.processes without a host TaskManager):
// each Context becomes one wpr::Process entry (ProcessType::kRenderer,
// since a Context hosts renderable web content), and each Frame it later
// creates (see ContextImpl::CreateFrame) becomes one attached task on that
// same process entry. Closing a Frame's last tab tears the whole process
// entry down automatically (TaskManager::CloseTab's existing "last task on
// a renderer-like process removes the process" behavior) -- a real,
// already-tested "Context dies when its last Frame closes" analog, not a
// new mechanism invented for this repo.
class ContextProviderImpl : public ContextProvider {
 public:
  ContextProviderImpl(std::string renderer_host, int renderer_port);
  ~ContextProviderImpl() override;

  void Create(const CreateContextParams& params,
             mojo::PendingAssociatedReceiver<Context> context) override;

  const wpr::TaskManager& task_manager() const { return task_manager_; }

 private:
  std::string renderer_host_;
  int renderer_port_;
  wpr::TaskManager task_manager_;
  std::vector<std::unique_ptr<ContextImpl>> contexts_;
  std::vector<std::unique_ptr<mojo::AssociatedReceiver<Context>>> context_receivers_;
};

}  // namespace lime

#endif  // LIME_CONTEXT_PROVIDER_IMPL_H_
