//===---------- string_pool_test.cpp - Unit tests for StringPool ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "string_pool.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

namespace {

TEST(StringPool, UniquingAndComparisons) {
  StringPool SP;
  auto P1 = SP.intern("hello");

  std::string S("hel");
  S += "lo";
  auto P2 = SP.intern(S);

  auto P3 = SP.intern("goodbye");

  EXPECT_EQ(P1, P2) << "Failed to unique entries";
  EXPECT_NE(P1, P3) << "Unequal pooled strings comparing equal";

  // We want to test that less-than comparison of PooledStringPtrs compiles,
  // however we can't test the actual result as this is a pointer comparison and
  // PooledStringPtr doesn't expose the underlying address of the string.
  (void)(P1 < P3);
}

TEST(StringPool, Dereference) {
  StringPool SP;
  auto Foo = SP.intern("foo");
  EXPECT_EQ(*Foo, "foo") << "Equality on dereferenced string failed";
}

TEST(StringPool, ClearDeadEntries) {
  StringPool SP;
  {
    auto P1 = SP.intern("s1");
    SP.clearDeadEntries();
    EXPECT_FALSE(SP.empty()) << "\"s1\" entry in pool should still be retained";
  }
  SP.clearDeadEntries();
  EXPECT_TRUE(SP.empty()) << "pool should be empty";
}

TEST(StringPool, NullPtr) {
  // Make sure that we can default construct and then destroy a null
  // PooledStringPtr.
  PooledStringPtr Null;
}

TEST(StringPool, Hashable) {
  StringPool SP;
  PooledStringPtr P1 = SP.intern("s1");
  PooledStringPtr Null;
  EXPECT_NE(std::hash<PooledStringPtr>()(P1),
            std::hash<PooledStringPtr>()(Null));
}

} // end anonymous namespace
