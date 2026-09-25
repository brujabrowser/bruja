#ifndef LIME_MAIN_DELEGATE_H_
#define LIME_MAIN_DELEGATE_H_

#include <optional>
#include <string>

namespace lime {

// Analog of content::ContentMainDelegate -- the interface an embedder
// implements so lime::Main() can drive process bring-up generically,
// exactly like every fuchsia_web/webengine-style embedder implements
// ContentMainDelegate so web_engine_main.cc's own main() can stay a few
// lines long. There is no sandbox and no browser/renderer/gpu process
// split anywhere in this stack (see README's "Known simplifications"), so
// this keeps only the hooks that have a real counterpart here:
//
//   content::ContentMainDelegate    lime::MainDelegate
//   ------------------------------  --------------------------------
//   BasicStartupComplete()          BasicStartupComplete()
//   PreSandboxStartup()             PreTransportStartup()
//   PreBrowserMain()                PreRunLoop()
//   RunProcess(type, params)        RunProcess(role)
//   (owned by ContentMainRunner,    ProcessExiting()
//    not the delegate, but every
//    embedder needs a symmetric
//    teardown hook)
class MainDelegate {
 public:
  virtual ~MainDelegate() = default;

  // Return a value to exit immediately with that code, before CommandLine
  // parsing has any other effect (a --help/--version early-out). Returning
  // std::nullopt continues startup.
  virtual std::optional<int> BasicStartupComplete() { return std::nullopt; }

  // Runs after CommandLine::Init() but before any transport
  // (WASMHolePunch's socket/Executor, WASMCadidumKernel, WASMCadidumBindings)
  // is touched -- the analog of PreSandboxStartup's role as the last chance
  // to change process-wide setup before there's a pipe to break.
  virtual void PreTransportStartup() {}

  // Runs once transport is available but before the run loop starts.
  // Returning a value exits without entering the run loop.
  virtual std::optional<int> PreRunLoop() { return std::nullopt; }

  // `role` is lime::switches::kRole's value (e.g. "offerer" or "peer" --
  // the same two roles wpr_cadmium's --offerer flag already distinguishes
  // -- or "" if the switch was never given). Return an exit code to skip
  // lime::Main()'s default run loop entirely (the delegate ran its own
  // loop, or there's simply nothing to run); return std::nullopt to fall
  // into the default whp::Executor::Current().Run().
  virtual std::optional<int> RunProcess(const std::string& role) {
    (void)role;
    return std::nullopt;
  }

  // Always runs before lime::Main() returns, whether RunProcess() handled
  // everything itself or the default run loop exited via
  // whp::Executor::Current().Quit().
  virtual void ProcessExiting() {}
};

}  // namespace lime

#endif  // LIME_MAIN_DELEGATE_H_
