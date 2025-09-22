//===-- simple_packed_serialization_utils.h -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_TEST_SIMPLE_PACKED_SERIALIZATION_UTILS_H
#define ORC_RT_TEST_SIMPLE_PACKED_SERIALIZATION_UTILS_H

#include "simple_packed_serialization.h"
#include "gtest/gtest.h"

template <typename SPSTagT, typename T>
static void blobSerializationRoundTrip(const T &Value) {
  using BST = __orc_rt::SPSSerializationTraits<SPSTagT, T>;

  size_t Size = BST::size(Value);
  auto Buffer = std::make_unique<char[]>(Size);
  __orc_rt::SPSOutputBuffer OB(Buffer.get(), Size);

  EXPECT_TRUE(BST::serialize(OB, Value));

  __orc_rt::SPSInputBuffer IB(Buffer.get(), Size);

  T DSValue;
  EXPECT_TRUE(BST::deserialize(IB, DSValue));

  EXPECT_EQ(Value, DSValue)
      << "Incorrect value after serialization/deserialization round-trip";
}

#endif // ORC_RT_TEST_SIMPLE_PACKED_SERIALIZATION_UTILS_H