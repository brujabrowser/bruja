#include "lime/main.h"

#include "lime/command_line.h"
#include "lime/main_delegate.h"
#include "lime/switches.h"

#include "whp/base/executor.h"

namespace lime {

int Main(MainParams params) {
  MainDelegate* delegate = params.delegate;

  if (auto exit_code = delegate->BasicStartupComplete()) {
    return *exit_code;
  }

  delegate->PreTransportStartup();

  if (auto exit_code = delegate->PreRunLoop()) {
    delegate->ProcessExiting();
    return *exit_code;
  }

  std::string role =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(switches::kRole);
  if (auto exit_code = delegate->RunProcess(role)) {
    delegate->ProcessExiting();
    return *exit_code;
  }

  whp::Executor::Current().Run();
  delegate->ProcessExiting();
  return 0;
}

}  // namespace lime
