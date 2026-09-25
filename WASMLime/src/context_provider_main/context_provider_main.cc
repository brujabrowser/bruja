#include "lime/context_provider_main.h"

#include <memory>
#include <utility>

namespace lime {

ContextProviderHandle ContextProviderMain(std::string renderer_host, int renderer_port) {
  ContextProviderHandle handle;
  auto impl =
      std::make_unique<ContextProviderImpl>(std::move(renderer_host), renderer_port);
  mojo::SelfOwnedReceiver<ContextProvider>::Create(
      std::move(impl), handle.remote.BindNewPipeAndPassReceiver());
  return handle;
}

}  // namespace lime
