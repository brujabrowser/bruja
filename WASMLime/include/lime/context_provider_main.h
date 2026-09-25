#ifndef LIME_CONTEXT_PROVIDER_MAIN_H_
#define LIME_CONTEXT_PROVIDER_MAIN_H_

#include "context_provider_gen.h"

#include "lime/context_provider_impl.h"

#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

#include <string>

namespace lime {

// The real boot-layer entry point when a consumer's main() is invoked with
// --context-provider: constructs one ContextProviderImpl and binds a
// Remote<ContextProvider> to it, in-process. Real Fuchsia's own
// ContextProviderMain never returns except when killed by the component
// framework; there is no such framework here, so this analog just hands
// back a working, bound Remote<ContextProvider> and lets the caller
// (RunProcess or an example) drive the run loop -- see README's Known
// Simplifications for why this repo has no cross-process transport for
// these interfaces yet.
struct ContextProviderHandle {
  mojo::Remote<ContextProvider> remote;
};

ContextProviderHandle ContextProviderMain(std::string renderer_host, int renderer_port);

}  // namespace lime

#endif  // LIME_CONTEXT_PROVIDER_MAIN_H_
