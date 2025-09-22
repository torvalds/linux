//===-- sanitizer_leb128.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_leb128.h"

#include <type_traits>

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_internal_defs.h"

namespace __sanitizer {

template <typename T>
class Leb128Test : public ::testing::Test {};

using Leb128TestTypes = ::testing::Types<u8, u16, u32, u64>;
TYPED_TEST_SUITE(Leb128Test, Leb128TestTypes, );

static uptr BitsNeeded(u64 v) {
  if (!v)
    return 1;
  uptr r = 0;
  if (sizeof(uptr) != sizeof(u64)) {
    uptr uptr_bits = 8 * sizeof(uptr);
    while (v >> uptr_bits) {
      r += uptr_bits;
      v >>= uptr_bits;
    }
  }
  return r + MostSignificantSetBitIndex(v) + 1;
}

TYPED_TEST(Leb128Test, SignedOverflow) {
  using T = typename std::make_signed<TypeParam>::type;
  u8 buffer[16] = {255};
  T v = -128;
  EXPECT_EQ(buffer + 1, EncodeSLEB128(v, buffer, buffer + 1));
  EXPECT_EQ(buffer + 1, DecodeSLEB128(buffer, buffer + 1, &v));
}

TYPED_TEST(Leb128Test, Signed) {
  using T = typename std::make_signed<TypeParam>::type;
  T v = 0;
  for (int i = 0; i < 100; ++i) {
    u8 buffer[16] = {};
    u8* p = EncodeSLEB128(v, std::begin(buffer), std::end(buffer));
    EXPECT_EQ(int(BitsNeeded(v < 0 ? (-v - 1) : v) + 6 + 1) / 7, p - buffer)
        << (int)v;
    T v2;
    u8* p2 = DecodeSLEB128(std::begin(buffer), std::end(buffer), &v2);
    EXPECT_EQ(v, v2);
    EXPECT_EQ(p, p2);
    v = -TypeParam(v) * 3u + 1u;
  }
}

TYPED_TEST(Leb128Test, UnsignedOverflow) {
  using T = TypeParam;
  u8 buffer[16] = {255};
  T v = 255;
  EXPECT_EQ(buffer + 1, EncodeULEB128(v, buffer, buffer + 1));
  EXPECT_EQ(buffer + 1, DecodeULEB128(buffer, buffer + 1, &v));
}

TYPED_TEST(Leb128Test, Unsigned) {
  using T = TypeParam;
  T v = 0;
  for (int i = 0; i < 100; ++i) {
    u8 buffer[16] = {};
    u8* p = EncodeULEB128(v, std::begin(buffer), std::end(buffer));
    EXPECT_EQ(int(BitsNeeded(v) + 6) / 7, p - buffer);
    T v2;
    u8* p2 = DecodeULEB128(std::begin(buffer), std::end(buffer), &v2);
    EXPECT_EQ(v, v2);
    EXPECT_EQ(p, p2);
    v = v * 3 + 1;
  }
}

}  // namespace __sanitizer
