//===- endian_test.cpp ------------------------- swap byte order test -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime.
//
// Adapted from the llvm/unittests/Support/SwapByteOrderTest.cpp LLVM unit test.
//
//===----------------------------------------------------------------------===//

#include "endianness.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

TEST(Endian, ByteSwap_32) {
  EXPECT_EQ(0x44332211u, ByteSwap_32(0x11223344));
  EXPECT_EQ(0xDDCCBBAAu, ByteSwap_32(0xAABBCCDD));
}

TEST(Endian, ByteSwap_64) {
  EXPECT_EQ(0x8877665544332211ULL, ByteSwap_64(0x1122334455667788LL));
  EXPECT_EQ(0x1100FFEEDDCCBBAAULL, ByteSwap_64(0xAABBCCDDEEFF0011LL));
}

// In these first two tests all of the original_uintx values are truncated
// except for 64. We could avoid this, but there's really no point.
TEST(Endian, getSwappedBytes_UnsignedRoundTrip) {
  // The point of the bit twiddling of magic is to test with and without bits
  // in every byte.
  uint64_t value = 1;
  for (std::size_t i = 0; i <= sizeof(value); ++i) {
    uint8_t original_uint8 = static_cast<uint8_t>(value);
    EXPECT_EQ(original_uint8, getSwappedBytes(getSwappedBytes(original_uint8)));

    uint16_t original_uint16 = static_cast<uint16_t>(value);
    EXPECT_EQ(original_uint16,
              getSwappedBytes(getSwappedBytes(original_uint16)));

    uint32_t original_uint32 = static_cast<uint32_t>(value);
    EXPECT_EQ(original_uint32,
              getSwappedBytes(getSwappedBytes(original_uint32)));

    uint64_t original_uint64 = static_cast<uint64_t>(value);
    EXPECT_EQ(original_uint64,
              getSwappedBytes(getSwappedBytes(original_uint64)));

    value = (value << 8) | 0x55; // binary 0101 0101.
  }
}

TEST(Endian, getSwappedBytes_SignedRoundTrip) {
  // The point of the bit twiddling of magic is to test with and without bits
  // in every byte.
  uint64_t value = 1;
  for (std::size_t i = 0; i <= sizeof(value); ++i) {
    int8_t original_int8 = static_cast<int8_t>(value);
    EXPECT_EQ(original_int8, getSwappedBytes(getSwappedBytes(original_int8)));

    int16_t original_int16 = static_cast<int16_t>(value);
    EXPECT_EQ(original_int16, getSwappedBytes(getSwappedBytes(original_int16)));

    int32_t original_int32 = static_cast<int32_t>(value);
    EXPECT_EQ(original_int32, getSwappedBytes(getSwappedBytes(original_int32)));

    int64_t original_int64 = static_cast<int64_t>(value);
    EXPECT_EQ(original_int64, getSwappedBytes(getSwappedBytes(original_int64)));

    // Test other sign.
    value *= -1;

    original_int8 = static_cast<int8_t>(value);
    EXPECT_EQ(original_int8, getSwappedBytes(getSwappedBytes(original_int8)));

    original_int16 = static_cast<int16_t>(value);
    EXPECT_EQ(original_int16, getSwappedBytes(getSwappedBytes(original_int16)));

    original_int32 = static_cast<int32_t>(value);
    EXPECT_EQ(original_int32, getSwappedBytes(getSwappedBytes(original_int32)));

    original_int64 = static_cast<int64_t>(value);
    EXPECT_EQ(original_int64, getSwappedBytes(getSwappedBytes(original_int64)));

    // Return to normal sign and twiddle.
    value *= -1;
    value = (value << 8) | 0x55; // binary 0101 0101.
  }
}

TEST(Endian, getSwappedBytes_uint8_t) {
  EXPECT_EQ(uint8_t(0x11), getSwappedBytes(uint8_t(0x11)));
}

TEST(Endian, getSwappedBytes_uint16_t) {
  EXPECT_EQ(uint16_t(0x1122), getSwappedBytes(uint16_t(0x2211)));
}

TEST(Endian, getSwappedBytes_uint32_t) {
  EXPECT_EQ(uint32_t(0x11223344), getSwappedBytes(uint32_t(0x44332211)));
}

TEST(Endian, getSwappedBytes_uint64_t) {
  EXPECT_EQ(uint64_t(0x1122334455667788ULL),
            getSwappedBytes(uint64_t(0x8877665544332211ULL)));
}

TEST(Endian, getSwappedBytes_int8_t) {
  EXPECT_EQ(int8_t(0x11), getSwappedBytes(int8_t(0x11)));
}

TEST(Endian, getSwappedBytes_int16_t) {
  EXPECT_EQ(int16_t(0x1122), getSwappedBytes(int16_t(0x2211)));
}

TEST(Endian, getSwappedBytes_int32_t) {
  EXPECT_EQ(int32_t(0x11223344), getSwappedBytes(int32_t(0x44332211)));
}

TEST(Endian, getSwappedBytes_int64_t) {
  EXPECT_EQ(int64_t(0x1122334455667788LL),
            getSwappedBytes(int64_t(0x8877665544332211LL)));
}

TEST(Endian, swapByteOrder_uint8_t) {
  uint8_t value = 0x11;
  swapByteOrder(value);
  EXPECT_EQ(uint8_t(0x11), value);
}

TEST(Endian, swapByteOrder_uint16_t) {
  uint16_t value = 0x2211;
  swapByteOrder(value);
  EXPECT_EQ(uint16_t(0x1122), value);
}

TEST(Endian, swapByteOrder_uint32_t) {
  uint32_t value = 0x44332211;
  swapByteOrder(value);
  EXPECT_EQ(uint32_t(0x11223344), value);
}

TEST(Endian, swapByteOrder_uint64_t) {
  uint64_t value = 0x8877665544332211ULL;
  swapByteOrder(value);
  EXPECT_EQ(uint64_t(0x1122334455667788ULL), value);
}

TEST(Endian, swapByteOrder_int8_t) {
  int8_t value = 0x11;
  swapByteOrder(value);
  EXPECT_EQ(int8_t(0x11), value);
}

TEST(Endian, swapByteOrder_int16_t) {
  int16_t value = 0x2211;
  swapByteOrder(value);
  EXPECT_EQ(int16_t(0x1122), value);
}

TEST(Endian, swapByteOrder_int32_t) {
  int32_t value = 0x44332211;
  swapByteOrder(value);
  EXPECT_EQ(int32_t(0x11223344), value);
}

TEST(Endian, swapByteOrder_int64_t) {
  int64_t value = 0x8877665544332211LL;
  swapByteOrder(value);
  EXPECT_EQ(int64_t(0x1122334455667788LL), value);
}
