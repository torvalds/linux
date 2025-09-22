//===-- common_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "internal_defs.h"
#include "tests/scudo_unit_test.h"

#include "common.h"
#include "mem_map.h"
#include <algorithm>
#include <fstream>

namespace scudo {

static uptr getResidentMemorySize() {
  if (!SCUDO_LINUX)
    UNREACHABLE("Not implemented!");
  uptr Size;
  uptr Resident;
  std::ifstream IFS("/proc/self/statm");
  IFS >> Size;
  IFS >> Resident;
  return Resident * getPageSizeCached();
}

// Fuchsia needs getResidentMemorySize implementation.
TEST(ScudoCommonTest, SKIP_ON_FUCHSIA(ResidentMemorySize)) {
  uptr OnStart = getResidentMemorySize();
  EXPECT_GT(OnStart, 0UL);

  const uptr Size = 1ull << 30;
  const uptr Threshold = Size >> 3;

  MemMapT MemMap;
  ASSERT_TRUE(MemMap.map(/*Addr=*/0U, Size, "ResidentMemorySize"));
  ASSERT_NE(MemMap.getBase(), 0U);
  void *P = reinterpret_cast<void *>(MemMap.getBase());
  EXPECT_LT(getResidentMemorySize(), OnStart + Threshold);

  memset(P, 1, Size);
  EXPECT_GT(getResidentMemorySize(), OnStart + Size - Threshold);

  MemMap.releasePagesToOS(MemMap.getBase(), Size);
  EXPECT_LT(getResidentMemorySize(), OnStart + Threshold);

  memset(P, 1, Size);
  EXPECT_GT(getResidentMemorySize(), OnStart + Size - Threshold);

  MemMap.unmap(MemMap.getBase(), Size);
}

TEST(ScudoCommonTest, Zeros) {
  const uptr Size = 1ull << 20;

  MemMapT MemMap;
  ASSERT_TRUE(MemMap.map(/*Addr=*/0U, Size, "Zeros"));
  ASSERT_NE(MemMap.getBase(), 0U);
  uptr *P = reinterpret_cast<uptr *>(MemMap.getBase());
  const ptrdiff_t N = Size / sizeof(uptr);
  EXPECT_EQ(std::count(P, P + N, 0), N);

  memset(P, 1, Size);
  EXPECT_EQ(std::count(P, P + N, 0), 0);

  MemMap.releasePagesToOS(MemMap.getBase(), Size);
  EXPECT_EQ(std::count(P, P + N, 0), N);

  MemMap.unmap(MemMap.getBase(), Size);
}

} // namespace scudo
