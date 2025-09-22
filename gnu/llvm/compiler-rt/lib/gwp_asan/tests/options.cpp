//===-- options.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

#include "gwp_asan/optional/options_parser.h"
#include "gwp_asan/options.h"

#include <stdarg.h>

static char Message[1024];
void MessageRecorder(const char *Format, ...) {
  va_list Args;
  va_start(Args, Format);
  vsprintf(Message + strlen(Message), Format, Args);
  va_end(Args);
}

TEST(GwpAsanOptionsTest, Basic) {
  Message[0] = '\0';
  gwp_asan::options::initOptions("Enabled=0:SampleRate=4:"
                                 "InstallSignalHandlers=false",
                                 MessageRecorder);
  gwp_asan::options::Options Opts = gwp_asan::options::getOptions();
  EXPECT_EQ('\0', Message[0]);
  EXPECT_FALSE(Opts.Enabled);
  EXPECT_FALSE(Opts.InstallSignalHandlers);
  EXPECT_EQ(4, Opts.SampleRate);
}

void RunErrorTest(const char *OptionsStr, const char *ErrorNeedle) {
  Message[0] = '\0';
  gwp_asan::options::initOptions(OptionsStr, MessageRecorder);
  EXPECT_NE('\0', Message[0])
      << "Options string \"" << OptionsStr << "\" didn't generate an error.";
  EXPECT_NE(nullptr, strstr(Message, ErrorNeedle))
      << "Couldn't find error needle \"" << ErrorNeedle
      << "\" in haystack created by options string \"" << OptionsStr
      << "\". Error was: \"" << Message << "\".";
}

TEST(GwpAsanOptionsTest, FailureModes) {
  RunErrorTest("Enabled=2", "Invalid boolean value '2' for option 'Enabled'");
  RunErrorTest("Enabled=1:MaxSimultaneousAllocations=0",
               "MaxSimultaneousAllocations must be > 0");
  RunErrorTest("Enabled=1:MaxSimultaneousAllocations=-1",
               "MaxSimultaneousAllocations must be > 0");
  RunErrorTest("Enabled=1:SampleRate=0", "SampleRate must be > 0");
  RunErrorTest("Enabled=1:SampleRate=-1", "SampleRate must be > 0");
  RunErrorTest("Enabled=", "Invalid boolean value '' for option 'Enabled'");
  RunErrorTest("==", "Unknown option '=='");
  RunErrorTest("Enabled==0", "Invalid boolean value '=0' for option 'Enabled'");
  RunErrorTest("Enabled:", "Expected '=' when parsing option 'Enabled:'");
  RunErrorTest("Enabled:=", "Expected '=' when parsing option 'Enabled:='");
  RunErrorTest("SampleRate=NOT_A_NUMBER",
               "Invalid integer value 'NOT_A_NUMBER' for option 'SampleRate'");
  RunErrorTest("NOT_A_VALUE=0", "Unknown option 'NOT_A_VALUE");
}
