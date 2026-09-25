#include "lime/context_provider_impl.h"

#include "lime/context_impl.h"

#include "wpr/process.h"

#include <utility>

namespace lime {

ContextProviderImpl::ContextProviderImpl(std::string renderer_host, int renderer_port)
    : renderer_host_(std::move(renderer_host)), renderer_port_(renderer_port) {}

ContextProviderImpl::~ContextProviderImpl() = default;

void ContextProviderImpl::Create(const CreateContextParams& params,
                                 mojo::PendingAssociatedReceiver<Context> context) {
  (void)params;  // cors_exempt_headers/remote_debugging_port: accepted,
                 // not yet applied -- no network stack/devtools to wire
                 // them into (see README's Known Simplifications).
  wpr::Process process = task_manager_.Spawn(wpr::ProcessType::kRenderer, "Context");

  auto impl = std::make_unique<ContextImpl>(&task_manager_, process.id, renderer_host_,
                                            renderer_port_);
  auto receiver = std::make_unique<mojo::AssociatedReceiver<Context>>(impl.get());
  receiver->Bind(std::move(context));

  contexts_.push_back(std::move(impl));
  context_receivers_.push_back(std::move(receiver));
}

}  // namespace lime
