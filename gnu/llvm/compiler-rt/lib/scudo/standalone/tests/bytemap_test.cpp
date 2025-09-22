//===-- bytemap_test.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "bytemap.h"

#include <pthread.h>
#include <string.h>

template <typename T> void testMap(T &Map, scudo::uptr Size) {
  Map.init();
  for (scudo::uptr I = 0; I < Size; I += 7)
    Map.set(I, (I % 100) + 1);
  for (scudo::uptr J = 0; J < Size; J++) {
    if (J % 7)
      EXPECT_EQ(Map[J], 0);
    else
      EXPECT_EQ(Map[J], (J % 100) + 1);
  }
}

TEST(ScudoByteMapTest, FlatByteMap) {
  const scudo::uptr Size = 1U << 10;
  scudo::FlatByteMap<Size> Map;
  testMap(Map, Size);
  Map.unmapTestOnly();
}
