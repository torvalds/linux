/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "utils/Buffer.h"
#include "utils/Range.h"

#include <gtest/gtest.h>
#include <memory>

using namespace pzstd;

namespace {
void deleter(const unsigned char* buf) {
  delete[] buf;
}
}

TEST(Buffer, Constructors) {
  Buffer empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0, empty.size());

  Buffer sized(5);
  EXPECT_FALSE(sized.empty());
  EXPECT_EQ(5, sized.size());

  Buffer moved(std::move(sized));
  EXPECT_FALSE(sized.empty());
  EXPECT_EQ(5, sized.size());

  Buffer assigned;
  assigned = std::move(moved);
  EXPECT_FALSE(sized.empty());
  EXPECT_EQ(5, sized.size());
}

TEST(Buffer, BufferManagement) {
  std::shared_ptr<unsigned char> buf(new unsigned char[10], deleter);
  {
    Buffer acquired(buf, MutableByteRange(buf.get(), buf.get() + 10));
    EXPECT_EQ(2, buf.use_count());
    Buffer moved(std::move(acquired));
    EXPECT_EQ(2, buf.use_count());
    Buffer assigned;
    assigned = std::move(moved);
    EXPECT_EQ(2, buf.use_count());

    Buffer split = assigned.splitAt(5);
    EXPECT_EQ(3, buf.use_count());

    split.advance(1);
    assigned.subtract(1);
    EXPECT_EQ(3, buf.use_count());
  }
  EXPECT_EQ(1, buf.use_count());
}

TEST(Buffer, Modifiers) {
  Buffer buf(10);
  {
    unsigned char i = 0;
    for (auto& byte : buf.range()) {
      byte = i++;
    }
  }

  auto prefix = buf.splitAt(2);

  ASSERT_EQ(2, prefix.size());
  EXPECT_EQ(0, *prefix.data());

  ASSERT_EQ(8, buf.size());
  EXPECT_EQ(2, *buf.data());

  buf.advance(2);
  EXPECT_EQ(4, *buf.data());

  EXPECT_EQ(9, *(buf.range().end() - 1));

  buf.subtract(2);
  EXPECT_EQ(7, *(buf.range().end() - 1));

  EXPECT_EQ(4, buf.size());
}
