#include "test.h"

#include "lime/main.h"
#include "lime/main_delegate.h"

#include <string>
#include <vector>

namespace {

class RecordingDelegate : public lime::MainDelegate {
 public:
  std::vector<std::string> calls;
  std::optional<int> basic_startup_result;
  std::optional<int> pre_run_loop_result;
  std::optional<int> run_process_result;

  std::optional<int> BasicStartupComplete() override {
    calls.push_back("BasicStartupComplete");
    return basic_startup_result;
  }
  void PreTransportStartup() override { calls.push_back("PreTransportStartup"); }
  std::optional<int> PreRunLoop() override {
    calls.push_back("PreRunLoop");
    return pre_run_loop_result;
  }
  std::optional<int> RunProcess(const std::string& role) override {
    calls.push_back("RunProcess:" + role);
    return run_process_result;
  }
  void ProcessExiting() override { calls.push_back("ProcessExiting"); }
};

}  // namespace

TEST(FullLifecycleRunsEveryHookInOrder) {
  RecordingDelegate delegate;
  int rc = lime::Main(lime::MainParams(&delegate));
  EXPECT_EQ(rc, 0);
  std::vector<std::string> expected = {"BasicStartupComplete", "PreTransportStartup",
                                       "PreRunLoop", "RunProcess:", "ProcessExiting"};
  EXPECT(delegate.calls == expected);
}

TEST(BasicStartupCompleteShortCircuits) {
  RecordingDelegate delegate;
  delegate.basic_startup_result = 42;
  int rc = lime::Main(lime::MainParams(&delegate));
  EXPECT_EQ(rc, 42);
  std::vector<std::string> expected = {"BasicStartupComplete"};
  EXPECT(delegate.calls == expected);
}

TEST(PreRunLoopShortCircuitsButStillExits) {
  RecordingDelegate delegate;
  delegate.pre_run_loop_result = 7;
  int rc = lime::Main(lime::MainParams(&delegate));
  EXPECT_EQ(rc, 7);
  std::vector<std::string> expected = {"BasicStartupComplete", "PreTransportStartup",
                                       "PreRunLoop", "ProcessExiting"};
  EXPECT(delegate.calls == expected);
}

TEST(RunProcessShortCircuitsButStillExits) {
  RecordingDelegate delegate;
  delegate.run_process_result = 3;
  int rc = lime::Main(lime::MainParams(&delegate));
  EXPECT_EQ(rc, 3);
  std::vector<std::string> expected = {"BasicStartupComplete", "PreTransportStartup",
                                       "PreRunLoop", "RunProcess:", "ProcessExiting"};
  EXPECT(delegate.calls == expected);
}
