/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "utils/Range.h"

#include <gtest/gtest.h>
#include <string>

using namespace pzstd;

// Range is directly copied from folly.
// Just some sanity tests to make sure everything seems to work.

TEST(Range, Constructors) {
  StringPiece empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0, empty.size());

  std::string str = "hello";
  {
    Range<std::string::const_iterator> piece(str.begin(), str.end());
    EXPECT_EQ(5, piece.size());
    EXPECT_EQ('h', *piece.data());
    EXPECT_EQ('o', *(piece.end() - 1));
  }

  {
    StringPiece piece(str.data(), str.size());
    EXPECT_EQ(5, piece.size());
    EXPECT_EQ('h', *piece.data());
    EXPECT_EQ('o', *(piece.end() - 1));
  }

  {
    StringPiece piece(str);
    EXPECT_EQ(5, piece.size());
    EXPECT_EQ('h', *piece.data());
    EXPECT_EQ('o', *(piece.end() - 1));
  }

  {
    StringPiece piece(str.c_str());
    EXPECT_EQ(5, piece.size());
    EXPECT_EQ('h', *piece.data());
    EXPECT_EQ('o', *(piece.end() - 1));
  }
}

TEST(Range, Modifiers) {
  StringPiece range("hello world");
  ASSERT_EQ(11, range.size());

  {
    auto hello = range.subpiece(0, 5);
    EXPECT_EQ(5, hello.size());
    EXPECT_EQ('h', *hello.data());
    EXPECT_EQ('o', *(hello.end() - 1));
  }
  {
    auto hello = range;
    hello.subtract(6);
    EXPECT_EQ(5, hello.size());
    EXPECT_EQ('h', *hello.data());
    EXPECT_EQ('o', *(hello.end() - 1));
  }
  {
    auto world = range;
    world.advance(6);
    EXPECT_EQ(5, world.size());
    EXPECT_EQ('w', *world.data());
    EXPECT_EQ('d', *(world.end() - 1));
  }

  std::string expected = "hello world";
  EXPECT_EQ(expected, std::string(range.begin(), range.end()));
  EXPECT_EQ(expected, std::string(range.data(), range.size()));
}
