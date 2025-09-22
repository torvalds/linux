//===-- asan_internal_interface_test.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
//===----------------------------------------------------------------------===//
#include "asan_interface_internal.h"
#include "asan_test_utils.h"
#include <vector>

TEST(AddressSanitizerInternalInterface, SetShadow) {
  std::vector<char> buffer(17, 0xff);

  __asan_set_shadow_00((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x00), buffer);

  __asan_set_shadow_01((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x01), buffer);

  __asan_set_shadow_02((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x02), buffer);

  __asan_set_shadow_03((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x03), buffer);

  __asan_set_shadow_04((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x04), buffer);

  __asan_set_shadow_05((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x05), buffer);

  __asan_set_shadow_06((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x06), buffer);

  __asan_set_shadow_07((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0x07), buffer);

  __asan_set_shadow_f1((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0xf1), buffer);

  __asan_set_shadow_f2((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0xf2), buffer);

  __asan_set_shadow_f3((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0xf3), buffer);

  __asan_set_shadow_f5((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0xf5), buffer);

  __asan_set_shadow_f8((uptr)buffer.data(), buffer.size());
  EXPECT_EQ(std::vector<char>(buffer.size(), 0xf8), buffer);
}
