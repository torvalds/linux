//===-- flags_test.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "flags.h"
#include "flags_parser.h"

#include <string.h>

static const char FlagName[] = "flag_name";
static const char FlagDesc[] = "flag description";

template <typename T>
static void testFlag(scudo::FlagType Type, T StartValue, const char *Env,
                     T FinalValue) {
  scudo::FlagParser Parser;
  T Flag = StartValue;
  Parser.registerFlag(FlagName, FlagDesc, Type, &Flag);
  Parser.parseString(Env);
  EXPECT_EQ(FinalValue, Flag);
  // Reporting unrecognized flags is needed to reset them.
  scudo::reportUnrecognizedFlags();
}

TEST(ScudoFlagsTest, BooleanFlags) {
  testFlag(scudo::FlagType::FT_bool, false, "flag_name=1", true);
  testFlag(scudo::FlagType::FT_bool, false, "flag_name=yes", true);
  testFlag(scudo::FlagType::FT_bool, false, "flag_name='yes'", true);
  testFlag(scudo::FlagType::FT_bool, false, "flag_name=true", true);
  testFlag(scudo::FlagType::FT_bool, true, "flag_name=0", false);
  testFlag(scudo::FlagType::FT_bool, true, "flag_name=\"0\"", false);
  testFlag(scudo::FlagType::FT_bool, true, "flag_name=no", false);
  testFlag(scudo::FlagType::FT_bool, true, "flag_name=false", false);
  testFlag(scudo::FlagType::FT_bool, true, "flag_name='false'", false);
}

TEST(ScudoFlagsDeathTest, BooleanFlags) {
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_bool, false, "flag_name", true),
               "expected '='");
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_bool, false, "flag_name=", true),
               "invalid value for bool option: ''");
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_bool, false, "flag_name=2", true),
               "invalid value for bool option: '2'");
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_bool, false, "flag_name=-1", true),
               "invalid value for bool option: '-1'");
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_bool, false, "flag_name=on", true),
               "invalid value for bool option: 'on'");
}

TEST(ScudoFlagsTest, IntFlags) {
  testFlag(scudo::FlagType::FT_int, -11, nullptr, -11);
  testFlag(scudo::FlagType::FT_int, -11, "flag_name=0", 0);
  testFlag(scudo::FlagType::FT_int, -11, "flag_name='0'", 0);
  testFlag(scudo::FlagType::FT_int, -11, "flag_name=42", 42);
  testFlag(scudo::FlagType::FT_int, -11, "flag_name=-42", -42);
  testFlag(scudo::FlagType::FT_int, -11, "flag_name=\"-42\"", -42);

  // Unrecognized flags are ignored.
  testFlag(scudo::FlagType::FT_int, -11, "--flag_name=42", -11);
  testFlag(scudo::FlagType::FT_int, -11, "zzzzzzz=42", -11);
}

TEST(ScudoFlagsDeathTest, IntFlags) {
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_int, -11, "flag_name", 0),
               "expected '='");
  EXPECT_DEATH(testFlag(scudo::FlagType::FT_int, -11, "flag_name=42U", 0),
               "invalid value for int option");
}

static void testTwoFlags(const char *Env, bool ExpectedFlag1,
                         const int ExpectedFlag2, const char *Name1 = "flag1",
                         const char *Name2 = "flag2") {
  scudo::FlagParser Parser;
  bool Flag1 = !ExpectedFlag1;
  int Flag2;
  Parser.registerFlag(Name1, FlagDesc, scudo::FlagType::FT_bool, &Flag1);
  Parser.registerFlag(Name2, FlagDesc, scudo::FlagType::FT_int, &Flag2);
  Parser.parseString(Env);
  EXPECT_EQ(ExpectedFlag1, Flag1);
  EXPECT_EQ(Flag2, ExpectedFlag2);
  // Reporting unrecognized flags is needed to reset them.
  scudo::reportUnrecognizedFlags();
}

TEST(ScudoFlagsTest, MultipleFlags) {
  testTwoFlags("flag1=1 flag2=42", true, 42);
  testTwoFlags("flag2=-1 flag1=0", false, -1);
  testTwoFlags("flag1=false:flag2=1337", false, 1337);
  testTwoFlags("flag2=42:flag1=yes", true, 42);
  testTwoFlags("flag2=42\nflag1=yes", true, 42);
  testTwoFlags("flag2=42\r\nflag1=yes", true, 42);
  testTwoFlags("flag2=42\tflag1=yes", true, 42);
}

TEST(ScudoFlagsTest, CommonSuffixFlags) {
  testTwoFlags("flag=1 other_flag=42", true, 42, "flag", "other_flag");
  testTwoFlags("other_flag=42 flag=1", true, 42, "flag", "other_flag");
}

TEST(ScudoFlagsTest, AllocatorFlags) {
  scudo::FlagParser Parser;
  scudo::Flags Flags;
  scudo::registerFlags(&Parser, &Flags);
  Flags.setDefaults();
  Flags.dealloc_type_mismatch = false;
  Flags.delete_size_mismatch = false;
  Flags.quarantine_max_chunk_size = 1024;
  Parser.parseString("dealloc_type_mismatch=true:delete_size_mismatch=true:"
                     "quarantine_max_chunk_size=2048");
  EXPECT_TRUE(Flags.dealloc_type_mismatch);
  EXPECT_TRUE(Flags.delete_size_mismatch);
  EXPECT_EQ(2048, Flags.quarantine_max_chunk_size);
}

#ifdef GWP_ASAN_HOOKS
TEST(ScudoFlagsTest, GWPASanFlags) {
  scudo::FlagParser Parser;
  scudo::Flags Flags;
  scudo::registerFlags(&Parser, &Flags);
  Flags.setDefaults();
  Flags.GWP_ASAN_Enabled = false;
  Parser.parseString("GWP_ASAN_Enabled=true:GWP_ASAN_SampleRate=1:"
                     "GWP_ASAN_InstallSignalHandlers=false");
  EXPECT_TRUE(Flags.GWP_ASAN_Enabled);
  EXPECT_FALSE(Flags.GWP_ASAN_InstallSignalHandlers);
  EXPECT_EQ(1, Flags.GWP_ASAN_SampleRate);
}
#endif // GWP_ASAN_HOOKS
