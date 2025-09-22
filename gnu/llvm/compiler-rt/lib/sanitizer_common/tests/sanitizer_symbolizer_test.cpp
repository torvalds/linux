//===-- sanitizer_symbolizer_test.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for sanitizer_symbolizer.h and sanitizer_symbolizer_internal.h
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_symbolizer_internal.h"
#include "gtest/gtest.h"

namespace __sanitizer {

TEST(Symbolizer, ExtractToken) {
  char *token;
  const char *rest;

  rest = ExtractToken("a;b;c", ";", &token);
  EXPECT_STREQ("a", token);
  EXPECT_STREQ("b;c", rest);
  InternalFree(token);

  rest = ExtractToken("aaa-bbb.ccc", ";.-*", &token);
  EXPECT_STREQ("aaa", token);
  EXPECT_STREQ("bbb.ccc", rest);
  InternalFree(token);
}

TEST(Symbolizer, ExtractInt) {
  int token;
  const char *rest = ExtractInt("123,456;789", ";,", &token);
  EXPECT_EQ(123, token);
  EXPECT_STREQ("456;789", rest);
}

TEST(Symbolizer, ExtractUptr) {
  uptr token;
  const char *rest = ExtractUptr("123,456;789", ";,", &token);
  EXPECT_EQ(123U, token);
  EXPECT_STREQ("456;789", rest);
}

TEST(Symbolizer, ExtractTokenUpToDelimiter) {
  char *token;
  const char *rest =
      ExtractTokenUpToDelimiter("aaa-+-bbb-+-ccc", "-+-", &token);
  EXPECT_STREQ("aaa", token);
  EXPECT_STREQ("bbb-+-ccc", rest);
  InternalFree(token);
}

#if !SANITIZER_WINDOWS
TEST(Symbolizer, DemangleSwiftAndCXX) {
  // Swift names are not demangled in default llvm build because Swift
  // runtime is not linked in.
  EXPECT_STREQ(nullptr, DemangleSwiftAndCXX("_TtSd"));
  // Check that the rest demangles properly.
  EXPECT_STREQ("f1(char*, int)", DemangleSwiftAndCXX("_Z2f1Pci"));
#if !SANITIZER_FREEBSD // QoI issue with libcxxrt on FreeBSD
  EXPECT_STREQ(nullptr, DemangleSwiftAndCXX("foo"));
#endif
  EXPECT_STREQ(nullptr, DemangleSwiftAndCXX(""));
}
#endif

}  // namespace __sanitizer
