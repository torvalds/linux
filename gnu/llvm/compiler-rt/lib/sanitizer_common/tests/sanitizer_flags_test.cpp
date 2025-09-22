//===-- sanitizer_flags_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "gtest/gtest.h"

#include <stdint.h>
#include <string.h>

namespace __sanitizer {

static const char kFlagName[] = "flag_name";
static const char kFlagDesc[] = "flag description";

template <typename T>
static void TestFlag(T start_value, const char *env, T final_value) {
  T flag = start_value;

  FlagParser parser;
  RegisterFlag(&parser, kFlagName, kFlagDesc, &flag);

  parser.ParseString(env);

  EXPECT_EQ(final_value, flag);

  // Reporting unrecognized flags is needed to reset them.
  ReportUnrecognizedFlags();
}

template <>
void TestFlag(const char *start_value, const char *env,
                     const char *final_value) {
  const char *flag = start_value;

  FlagParser parser;
  RegisterFlag(&parser, kFlagName, kFlagDesc, &flag);

  parser.ParseString(env);

  EXPECT_EQ(0, internal_strcmp(final_value, flag));

  // Reporting unrecognized flags is needed to reset them.
  ReportUnrecognizedFlags();
}

TEST(SanitizerCommon, BooleanFlags) {
  TestFlag(false, "flag_name=1", true);
  TestFlag(false, "flag_name=yes", true);
  TestFlag(false, "flag_name=true", true);
  TestFlag(true, "flag_name=0", false);
  TestFlag(true, "flag_name=no", false);
  TestFlag(true, "flag_name=false", false);

  EXPECT_DEATH(TestFlag(false, "flag_name", true), "expected '='");
  EXPECT_DEATH(TestFlag(false, "flag_name=", true),
               "Invalid value for bool option: ''");
  EXPECT_DEATH(TestFlag(false, "flag_name=2", true),
               "Invalid value for bool option: '2'");
  EXPECT_DEATH(TestFlag(false, "flag_name=-1", true),
               "Invalid value for bool option: '-1'");
  EXPECT_DEATH(TestFlag(false, "flag_name=on", true),
               "Invalid value for bool option: 'on'");
}

TEST(SanitizerCommon, HandleSignalMode) {
  TestFlag(kHandleSignalNo, "flag_name=1", kHandleSignalYes);
  TestFlag(kHandleSignalNo, "flag_name=yes", kHandleSignalYes);
  TestFlag(kHandleSignalNo, "flag_name=true", kHandleSignalYes);
  TestFlag(kHandleSignalYes, "flag_name=0", kHandleSignalNo);
  TestFlag(kHandleSignalYes, "flag_name=no", kHandleSignalNo);
  TestFlag(kHandleSignalYes, "flag_name=false", kHandleSignalNo);
  TestFlag(kHandleSignalNo, "flag_name=2", kHandleSignalExclusive);
  TestFlag(kHandleSignalYes, "flag_name=exclusive", kHandleSignalExclusive);

  EXPECT_DEATH(TestFlag(kHandleSignalNo, "flag_name", kHandleSignalNo),
               "expected '='");
  EXPECT_DEATH(TestFlag(kHandleSignalNo, "flag_name=", kHandleSignalNo),
               "Invalid value for signal handler option: ''");
  EXPECT_DEATH(TestFlag(kHandleSignalNo, "flag_name=3", kHandleSignalNo),
               "Invalid value for signal handler option: '3'");
  EXPECT_DEATH(TestFlag(kHandleSignalNo, "flag_name=-1", kHandleSignalNo),
               "Invalid value for signal handler option: '-1'");
  EXPECT_DEATH(TestFlag(kHandleSignalNo, "flag_name=on", kHandleSignalNo),
               "Invalid value for signal handler option: 'on'");
}

TEST(SanitizerCommon, IntFlags) {
  TestFlag(-11, 0, -11);
  TestFlag(-11, "flag_name=0", 0);
  TestFlag(-11, "flag_name=42", 42);
  TestFlag(-11, "flag_name=-42", -42);

  // Unrecognized flags are ignored.
  TestFlag(-11, "--flag_name=42", -11);
  TestFlag(-11, "zzzzzzz=42", -11);

  EXPECT_DEATH(TestFlag(-11, "flag_name", 0), "expected '='");
  EXPECT_DEATH(TestFlag(-11, "flag_name=42U", 0),
               "Invalid value for int option");
}

TEST(SanitizerCommon, LongLongIntFlags) {
  s64 InitValue = -5;
  s64 IntMin = INT64_MIN;
  s64 IntMax = INT64_MAX;
  TestFlag(InitValue, "flag_name=0", 0ll);
  TestFlag(InitValue, "flag_name=42", 42ll);
  TestFlag(InitValue, "flag_name=-42", -42ll);

  TestFlag(InitValue, "flag_name=-9223372036854775808", IntMin);
  TestFlag(InitValue, "flag_name=9223372036854775807", IntMax);

  TestFlag(InitValue, "flag_name=-92233720368547758080000", IntMin);
  TestFlag(InitValue, "flag_name=92233720368547758070000", IntMax);
}

TEST(SanitizerCommon, StrFlags) {
  TestFlag("zzz", 0, "zzz");
  TestFlag("zzz", "flag_name=", "");
  TestFlag("zzz", "flag_name=abc", "abc");
  TestFlag("", "flag_name=abc", "abc");
  TestFlag("", "flag_name='abc zxc'", "abc zxc");
  // TestStrFlag("", "flag_name=\"abc qwe\" asd", "abc qwe");
}

static void TestTwoFlags(const char *env, bool expected_flag1,
                         const char *expected_flag2,
                         const char *name1 = "flag1",
                         const char *name2 = "flag2") {
  bool flag1 = !expected_flag1;
  const char *flag2 = "";

  FlagParser parser;
  RegisterFlag(&parser, name1, kFlagDesc, &flag1);
  RegisterFlag(&parser, name2, kFlagDesc, &flag2);

  parser.ParseString(env);

  EXPECT_EQ(expected_flag1, flag1);
  EXPECT_EQ(0, internal_strcmp(flag2, expected_flag2));

  // Reporting unrecognized flags is needed to reset them.
  ReportUnrecognizedFlags();
}

TEST(SanitizerCommon, MultipleFlags) {
  TestTwoFlags("flag1=1 flag2='zzz'", true, "zzz");
  TestTwoFlags("flag2='qxx' flag1=0", false, "qxx");
  TestTwoFlags("flag1=false:flag2='zzz'", false, "zzz");
  TestTwoFlags("flag2=qxx:flag1=yes", true, "qxx");
  TestTwoFlags("flag2=qxx\nflag1=yes", true, "qxx");
  TestTwoFlags("flag2=qxx\r\nflag1=yes", true, "qxx");
  TestTwoFlags("flag2=qxx\tflag1=yes", true, "qxx");
}

TEST(SanitizerCommon, CommonSuffixFlags) {
  TestTwoFlags("flag=1 other_flag='zzz'", true, "zzz", "flag", "other_flag");
  TestTwoFlags("other_flag='zzz' flag=1", true, "zzz", "flag", "other_flag");
  TestTwoFlags("other_flag=' flag=0 ' flag=1", true, " flag=0 ", "flag",
               "other_flag");
  TestTwoFlags("flag=1 other_flag=' flag=0 '", true, " flag=0 ", "flag",
               "other_flag");
}

TEST(SanitizerCommon, CommonFlags) {
  CommonFlags cf;
  FlagParser parser;
  RegisterCommonFlags(&parser, &cf);

  cf.SetDefaults();
  EXPECT_TRUE(cf.symbolize);
  EXPECT_STREQ(".", cf.coverage_dir);

  cf.symbolize = false;
  cf.coverage = true;
  cf.heap_profile = true;
  cf.log_path = "path/one";

  parser.ParseString("symbolize=1:heap_profile=false log_path='path/two'");
  EXPECT_TRUE(cf.symbolize);
  EXPECT_TRUE(cf.coverage);
  EXPECT_FALSE(cf.heap_profile);
  EXPECT_STREQ("path/two", cf.log_path);
}

}  // namespace __sanitizer
