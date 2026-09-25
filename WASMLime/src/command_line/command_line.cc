#include "lime/command_line.h"

namespace lime {

namespace {

CommandLine* g_instance = nullptr;

}  // namespace

CommandLine::CommandLine(int argc, const char* const* argv) {
  ParseFrom(argc, argv);
}

void CommandLine::Init(int argc, const char* const* argv) {
  if (g_instance) {
    // Repeated Init() is ignored, matching the base::CommandLine behavior
    // web_engine_main.cc itself relies on.
    return;
  }
  g_instance = new CommandLine();
  g_instance->ParseFrom(argc, argv);
}

CommandLine* CommandLine::ForCurrentProcess() {
  if (!g_instance) {
    g_instance = new CommandLine();
  }
  return g_instance;
}

void CommandLine::ParseFrom(int argc, const char* const* argv) {
  if (argc <= 1 || !argv) {
    return;
  }
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) {
      continue;
    }
    std::string token = argv[i];
    size_t dashes = 0;
    while (dashes < token.size() && token[dashes] == '-') {
      ++dashes;
    }
    if (dashes == 0 || dashes == token.size()) {
      args_.push_back(token);
      continue;
    }
    std::string body = token.substr(dashes);
    auto eq = body.find('=');
    if (eq == std::string::npos) {
      switches_[body] = "";
    } else {
      switches_[body.substr(0, eq)] = body.substr(eq + 1);
    }
  }
}

bool CommandLine::HasSwitch(const std::string& name) const {
  return switches_.find(name) != switches_.end();
}

std::string CommandLine::GetSwitchValueASCII(const std::string& name) const {
  auto it = switches_.find(name);
  return it == switches_.end() ? std::string() : it->second;
}

}  // namespace lime
