#include "test.h"

#include "lime/command_line.h"

TEST(BareSwitchHasNoValue) {
  const char* argv[] = {"prog", "--offerer"};
  lime::CommandLine cmd(2, argv);
  EXPECT(cmd.HasSwitch("offerer"));
  EXPECT(cmd.GetSwitchValueASCII("offerer") == "");
}

TEST(SwitchWithValue) {
  const char* argv[] = {"prog", "--role=offerer", "--bind=127.0.0.1:0"};
  lime::CommandLine cmd(3, argv);
  EXPECT(cmd.HasSwitch("role"));
  EXPECT(cmd.GetSwitchValueASCII("role") == "offerer");
  EXPECT(cmd.GetSwitchValueASCII("bind") == "127.0.0.1:0");
}

TEST(SingleDashAlsoRecognized) {
  const char* argv[] = {"prog", "-h"};
  lime::CommandLine cmd(2, argv);
  EXPECT(cmd.HasSwitch("h"));
}

TEST(MissingSwitchIsAbsent) {
  const char* argv[] = {"prog"};
  lime::CommandLine cmd(1, argv);
  EXPECT(!cmd.HasSwitch("role"));
  EXPECT(cmd.GetSwitchValueASCII("role") == "");
}

TEST(PositionalArgsPreserved) {
  const char* argv[] = {"prog", "--role=peer", "extra"};
  lime::CommandLine cmd(3, argv);
  EXPECT_EQ(cmd.args().size(), 1u);
  EXPECT(cmd.args()[0] == "extra");
}

TEST(ForCurrentProcessNeverNull) {
  EXPECT(lime::CommandLine::ForCurrentProcess() != nullptr);
}
