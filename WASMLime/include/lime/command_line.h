#ifndef LIME_COMMAND_LINE_H_
#define LIME_COMMAND_LINE_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace lime {

// A small analog of base::CommandLine -- just enough for
// fuchsia_web/webengine/web_engine_main.cc's own two calls,
// base::CommandLine::Init(argc, argv) and
// command_line->HasSwitch(...)/GetSwitchValueASCII(...), to have somewhere
// to live in this stack. Recognizes "-name", "--name" and "--name=value"
// tokens; anything else is a positional arg.
class CommandLine {
 public:
  CommandLine() = default;

  // Parses immediately into a standalone instance -- doesn't touch the
  // process-wide singleton below. Mainly for tests that want to check
  // parsing behavior without the "repeated Init() is ignored" rule getting
  // in the way.
  CommandLine(int argc, const char* const* argv);

  // Repeated calls after the first are ignored, matching the documented
  // base::CommandLine::Init behavior web_engine_main.cc itself relies on
  // when it passes params.argc = 0, params.argv = nullptr down into
  // content::ContentMain.
  static void Init(int argc, const char* const* argv);

  // Never null, even if Init() was never called (an empty CommandLine is
  // returned in that case) -- matching whp::Executor::Current()'s
  // always-valid-singleton shape elsewhere in this stack.
  static CommandLine* ForCurrentProcess();

  bool HasSwitch(const std::string& name) const;

  // Empty string if the switch is absent or was given no value.
  std::string GetSwitchValueASCII(const std::string& name) const;

  const std::vector<std::string>& args() const { return args_; }

 private:
  void ParseFrom(int argc, const char* const* argv);

  std::unordered_map<std::string, std::string> switches_;
  std::vector<std::string> args_;
};

}  // namespace lime

#endif  // LIME_COMMAND_LINE_H_
