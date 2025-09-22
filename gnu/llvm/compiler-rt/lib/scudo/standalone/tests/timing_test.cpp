//===-- timing_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "timing.h"

#include <cstdlib>
#include <string>

class ScudoTimingTest : public Test {
public:
  void testFunc1() { scudo::ScopedTimer ST(Manager, __func__); }

  void testFunc2() {
    scudo::ScopedTimer ST(Manager, __func__);
    testFunc1();
  }

  void testChainedCalls() {
    scudo::ScopedTimer ST(Manager, __func__);
    testFunc2();
  }

  void testIgnoredTimer() {
    scudo::ScopedTimer ST(Manager, __func__);
    ST.ignore();
  }

  void printAllTimersStats() { Manager.printAll(); }

  void getAllTimersStats(scudo::ScopedString &Str) { Manager.getAll(Str); }

  scudo::TimingManager &getTimingManager() { return Manager; }

  void testCallTimers() {
    scudo::ScopedTimer Outer(getTimingManager(), "Level1");
    {
      scudo::ScopedTimer Inner1(getTimingManager(), Outer, "Level2");
      { scudo::ScopedTimer Inner2(getTimingManager(), Inner1, "Level3"); }
    }
  }

private:
  scudo::TimingManager Manager;
};

TEST_F(ScudoTimingTest, SimpleTimer) {
  testIgnoredTimer();
  testChainedCalls();
  scudo::ScopedString Str;
  getAllTimersStats(Str);

  std::string Output(Str.data());
  EXPECT_TRUE(Output.find("testIgnoredTimer (1)") == std::string::npos);
  EXPECT_TRUE(Output.find("testChainedCalls (1)") != std::string::npos);
  EXPECT_TRUE(Output.find("testFunc2 (1)") != std::string::npos);
  EXPECT_TRUE(Output.find("testFunc1 (1)") != std::string::npos);
}

TEST_F(ScudoTimingTest, NestedTimer) {
  {
    scudo::ScopedTimer Outer(getTimingManager(), "Outer");
    {
      scudo::ScopedTimer Inner1(getTimingManager(), Outer, "Inner1");
      { scudo::ScopedTimer Inner2(getTimingManager(), Inner1, "Inner2"); }
    }
  }
  scudo::ScopedString Str;
  getAllTimersStats(Str);

  std::string Output(Str.data());
  EXPECT_TRUE(Output.find("Outer (1)") != std::string::npos);
  EXPECT_TRUE(Output.find("Inner1 (1)") != std::string::npos);
  EXPECT_TRUE(Output.find("Inner2 (1)") != std::string::npos);
}

TEST_F(ScudoTimingTest, VerifyChainedTimerCalculations) {
  {
    scudo::ScopedTimer Outer(getTimingManager(), "Level1");
    sleep(1);
    {
      scudo::ScopedTimer Inner1(getTimingManager(), Outer, "Level2");
      sleep(2);
      {
        scudo::ScopedTimer Inner2(getTimingManager(), Inner1, "Level3");
        sleep(3);
      }
    }
  }
  scudo::ScopedString Str;
  getAllTimersStats(Str);
  std::string Output(Str.data());

  // Get the individual timer values for the average and maximum, then
  // verify that the timer values are being calculated properly.
  Output = Output.substr(Output.find('\n') + 1);
  char *end;
  unsigned long long Level1AvgNs = std::strtoull(Output.c_str(), &end, 10);
  ASSERT_TRUE(end != nullptr);
  unsigned long long Level1MaxNs = std::strtoull(&end[6], &end, 10);
  ASSERT_TRUE(end != nullptr);
  EXPECT_EQ(Level1AvgNs, Level1MaxNs);

  Output = Output.substr(Output.find('\n') + 1);
  unsigned long long Level2AvgNs = std::strtoull(Output.c_str(), &end, 10);
  ASSERT_TRUE(end != nullptr);
  unsigned long long Level2MaxNs = std::strtoull(&end[6], &end, 10);
  ASSERT_TRUE(end != nullptr);
  EXPECT_EQ(Level2AvgNs, Level2MaxNs);

  Output = Output.substr(Output.find('\n') + 1);
  unsigned long long Level3AvgNs = std::strtoull(Output.c_str(), &end, 10);
  ASSERT_TRUE(end != nullptr);
  unsigned long long Level3MaxNs = std::strtoull(&end[6], &end, 10);
  ASSERT_TRUE(end != nullptr);
  EXPECT_EQ(Level3AvgNs, Level3MaxNs);

  EXPECT_GT(Level1AvgNs, Level2AvgNs);
  EXPECT_GT(Level2AvgNs, Level3AvgNs);

  // The time for the first timer needs to be at least six seconds.
  EXPECT_GT(Level1AvgNs, 6000000000U);
  // The time for the second timer needs to be at least five seconds.
  EXPECT_GT(Level2AvgNs, 5000000000U);
  // The time for the third timer needs to be at least three seconds.
  EXPECT_GT(Level3AvgNs, 3000000000U);
  // The time between the first and second timer needs to be at least one
  // second.
  EXPECT_GT(Level1AvgNs - Level2AvgNs, 1000000000U);
  // The time between the second and third timer needs to be at least two
  // second.
  EXPECT_GT(Level2AvgNs - Level3AvgNs, 2000000000U);
}

TEST_F(ScudoTimingTest, VerifyMax) {
  for (size_t i = 0; i < 3; i++) {
    scudo::ScopedTimer Outer(getTimingManager(), "Level1");
    sleep(1);
  }
  scudo::ScopedString Str;
  getAllTimersStats(Str);
  std::string Output(Str.data());

  Output = Output.substr(Output.find('\n') + 1);
  char *end;
  unsigned long long AvgNs = std::strtoull(Output.c_str(), &end, 10);
  ASSERT_TRUE(end != nullptr);
  unsigned long long MaxNs = std::strtoull(&end[6], &end, 10);
  ASSERT_TRUE(end != nullptr);

  EXPECT_GT(MaxNs, AvgNs);
}

TEST_F(ScudoTimingTest, VerifyMultipleTimerCalls) {
  for (size_t i = 0; i < 5; i++)
    testCallTimers();

  scudo::ScopedString Str;
  getAllTimersStats(Str);
  std::string Output(Str.data());
  EXPECT_TRUE(Output.find("Level1 (5)") != std::string::npos);
  EXPECT_TRUE(Output.find("Level2 (5)") != std::string::npos);
  EXPECT_TRUE(Output.find("Level3 (5)") != std::string::npos);
}

TEST_F(ScudoTimingTest, VerifyHeader) {
  { scudo::ScopedTimer Outer(getTimingManager(), "Timer"); }
  scudo::ScopedString Str;
  getAllTimersStats(Str);

  std::string Output(Str.data());
  std::string Header(Output.substr(0, Output.find('\n')));
  EXPECT_EQ(Header, "-- Average Operation Time -- -- Maximum Operation Time -- "
                    "-- Name (# of Calls) --");
}

TEST_F(ScudoTimingTest, VerifyTimerFormat) {
  testCallTimers();
  scudo::ScopedString Str;
  getAllTimersStats(Str);
  std::string Output(Str.data());

  // Check the top level line, should look similar to:
  //          11718.0(ns)                    11718(ns)            Level1 (1)
  Output = Output.substr(Output.find('\n') + 1);

  // Verify that the Average Operation Time is in the correct location.
  EXPECT_EQ(".0(ns) ", Output.substr(14, 7));

  // Verify that the Maximum Operation Time is in the correct location.
  EXPECT_EQ("(ns) ", Output.substr(45, 5));

  // Verify that the first timer name is in the correct location.
  EXPECT_EQ("Level1 (1)\n", Output.substr(61, 11));

  // Check a chained timer, should look similar to:
  //           5331.0(ns)                     5331(ns)              Level2 (1)
  Output = Output.substr(Output.find('\n') + 1);

  // Verify that the Average Operation Time is in the correct location.
  EXPECT_EQ(".0(ns) ", Output.substr(14, 7));

  // Verify that the Maximum Operation Time is in the correct location.
  EXPECT_EQ("(ns) ", Output.substr(45, 5));

  // Verify that the first timer name is in the correct location.
  EXPECT_EQ("  Level2 (1)\n", Output.substr(61, 13));

  // Check a secondary chained timer, should look similar to:
  //            814.0(ns)                      814(ns)                Level3 (1)
  Output = Output.substr(Output.find('\n') + 1);

  // Verify that the Average Operation Time is in the correct location.
  EXPECT_EQ(".0(ns) ", Output.substr(14, 7));

  // Verify that the Maximum Operation Time is in the correct location.
  EXPECT_EQ("(ns) ", Output.substr(45, 5));

  // Verify that the first timer name is in the correct location.
  EXPECT_EQ("    Level3 (1)\n", Output.substr(61, 15));
}

#if SCUDO_LINUX
TEST_F(ScudoTimingTest, VerifyPrintMatchesGet) {
  testing::internal::LogToStderr();
  testing::internal::CaptureStderr();
  testCallTimers();

  { scudo::ScopedTimer Outer(getTimingManager(), "Timer"); }
  printAllTimersStats();
  std::string PrintOutput = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(PrintOutput.size() != 0);

  scudo::ScopedString Str;
  getAllTimersStats(Str);
  std::string GetOutput(Str.data());
  EXPECT_TRUE(GetOutput.size() != 0);

  EXPECT_EQ(PrintOutput, GetOutput);
}
#endif

#if SCUDO_LINUX
TEST_F(ScudoTimingTest, VerifyReporting) {
  testing::internal::LogToStderr();
  testing::internal::CaptureStderr();
  // Every 100 calls generates a report, but run a few extra to verify the
  // report happened at call 100.
  for (size_t i = 0; i < 110; i++)
    scudo::ScopedTimer Outer(getTimingManager(), "VerifyReportTimer");

  std::string Output = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(Output.find("VerifyReportTimer (100)") != std::string::npos);
}
#endif
