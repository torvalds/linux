//===-- sanitizer_suppressions_test.cpp -----------------------------------===//
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
#include "sanitizer_common/sanitizer_suppressions.h"
#include "gtest/gtest.h"

#include <string.h>

namespace __sanitizer {

static bool MyMatch(const char *templ, const char *func) {
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", templ);
  return TemplateMatch(tmp, func);
}

TEST(Suppressions, Match) {
  EXPECT_TRUE(MyMatch("foobar$", "foobar"));

  EXPECT_TRUE(MyMatch("foobar", "foobar"));
  EXPECT_TRUE(MyMatch("*foobar*", "foobar"));
  EXPECT_TRUE(MyMatch("foobar", "prefix_foobar_postfix"));
  EXPECT_TRUE(MyMatch("*foobar*", "prefix_foobar_postfix"));
  EXPECT_TRUE(MyMatch("foo*bar", "foo_middle_bar"));
  EXPECT_TRUE(MyMatch("foo*bar", "foobar"));
  EXPECT_TRUE(MyMatch("foo*bar*baz", "foo_middle_bar_another_baz"));
  EXPECT_TRUE(MyMatch("foo*bar*baz", "foo_middle_barbaz"));
  EXPECT_TRUE(MyMatch("^foobar", "foobar"));
  EXPECT_TRUE(MyMatch("^foobar", "foobar_postfix"));
  EXPECT_TRUE(MyMatch("^*foobar", "foobar"));
  EXPECT_TRUE(MyMatch("^*foobar", "prefix_foobar"));
  EXPECT_TRUE(MyMatch("foobar$", "foobar"));
  EXPECT_TRUE(MyMatch("foobar$", "prefix_foobar"));
  EXPECT_TRUE(MyMatch("*foobar*$", "foobar"));
  EXPECT_TRUE(MyMatch("*foobar*$", "foobar_postfix"));
  EXPECT_TRUE(MyMatch("^foobar$", "foobar"));

  EXPECT_FALSE(MyMatch("foo", "baz"));
  EXPECT_FALSE(MyMatch("foobarbaz", "foobar"));
  EXPECT_FALSE(MyMatch("foobarbaz", "barbaz"));
  EXPECT_FALSE(MyMatch("foo*bar", "foobaz"));
  EXPECT_FALSE(MyMatch("foo*bar", "foo_baz"));
  EXPECT_FALSE(MyMatch("^foobar", "prefix_foobar"));
  EXPECT_FALSE(MyMatch("foobar$", "foobar_postfix"));
  EXPECT_FALSE(MyMatch("^foobar$", "prefix_foobar"));
  EXPECT_FALSE(MyMatch("^foobar$", "foobar_postfix"));
  EXPECT_FALSE(MyMatch("foo^bar", "foobar"));
  EXPECT_FALSE(MyMatch("foo$bar", "foobar"));
  EXPECT_FALSE(MyMatch("foo$^bar", "foobar"));
}

static const char *kTestSuppressionTypes[] = {"race", "thread", "mutex",
                                              "signal"};

class SuppressionContextTest : public ::testing::Test {
 public:
  SuppressionContextTest()
      : ctx_(kTestSuppressionTypes, ARRAY_SIZE(kTestSuppressionTypes)) {}

 protected:
  SuppressionContext ctx_;

  void CheckSuppressions(unsigned count, std::vector<const char *> types,
                         std::vector<const char *> templs) const {
    EXPECT_EQ(count, ctx_.SuppressionCount());
    for (unsigned i = 0; i < count; i++) {
      const Suppression *s = ctx_.SuppressionAt(i);
      EXPECT_STREQ(types[i], s->type);
      EXPECT_STREQ(templs[i], s->templ);
    }
  }
};

TEST_F(SuppressionContextTest, Parse) {
  ctx_.Parse(
      "race:foo\n"
      " \trace:bar\n"
      "race:baz\t \n"
      "# a comment\n"
      "race:quz\n");
  CheckSuppressions(4, {"race", "race", "race", "race"},
                    {"foo", "bar", "baz", "quz"});
}

TEST_F(SuppressionContextTest, Parse2) {
  ctx_.Parse(
      "  \t# first line comment\n"
      " \trace:bar \t\n"
      "race:baz* *baz\n"
      "# a comment\n"
      "# last line comment\n");
  CheckSuppressions(2, {"race", "race"}, {"bar", "baz* *baz"});
}

TEST_F(SuppressionContextTest, Parse3) {
  ctx_.Parse(
      "# last suppression w/o line-feed\n"
      "race:foo\n"
      "race:bar\r\n"
      "race:baz");
  CheckSuppressions(3, {"race", "race", "race"}, {"foo", "bar", "baz"});
}

TEST_F(SuppressionContextTest, ParseType) {
  ctx_.Parse(
      "race:foo\n"
      "thread:bar\n"
      "mutex:baz\n"
      "signal:quz\n");
  CheckSuppressions(4, {"race", "thread", "mutex", "signal"},
                    {"foo", "bar", "baz", "quz"});
}

TEST_F(SuppressionContextTest, HasSuppressionType) {
  ctx_.Parse(
    "race:foo\n"
    "thread:bar\n");
  EXPECT_TRUE(ctx_.HasSuppressionType("race"));
  EXPECT_TRUE(ctx_.HasSuppressionType("thread"));
  EXPECT_FALSE(ctx_.HasSuppressionType("mutex"));
  EXPECT_FALSE(ctx_.HasSuppressionType("signal"));
}

TEST_F(SuppressionContextTest, RegressionTestForBufferOverflowInSuppressions) {
  const char *expected_output =
      "failed to parse suppressions.\n"
      "Supported suppression types are:\n"
      "- race\n"
      "- thread\n"
      "- mutex\n"
      "- signal\n";
  EXPECT_DEATH(ctx_.Parse("race"), expected_output);
  EXPECT_DEATH(ctx_.Parse("foo"), expected_output);
}

}  // namespace __sanitizer
