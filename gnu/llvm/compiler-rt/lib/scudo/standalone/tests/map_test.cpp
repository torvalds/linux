//===-- map_test.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "common.h"
#include "mem_map.h"

#include <string.h>
#include <unistd.h>

static const char *MappingName = "scudo:test";

TEST(ScudoMapTest, PageSize) {
  EXPECT_EQ(scudo::getPageSizeCached(),
            static_cast<scudo::uptr>(sysconf(_SC_PAGESIZE)));
}

TEST(ScudoMapDeathTest, MapNoAccessUnmap) {
  const scudo::uptr Size = 4 * scudo::getPageSizeCached();
  scudo::ReservedMemoryT ReservedMemory;

  ASSERT_TRUE(ReservedMemory.create(/*Addr=*/0U, Size, MappingName));
  EXPECT_NE(ReservedMemory.getBase(), 0U);
  EXPECT_DEATH(
      memset(reinterpret_cast<void *>(ReservedMemory.getBase()), 0xaa, Size),
      "");

  ReservedMemory.release();
}

TEST(ScudoMapDeathTest, MapUnmap) {
  const scudo::uptr Size = 4 * scudo::getPageSizeCached();
  EXPECT_DEATH(
      {
        // Repeat few time to avoid missing crash if it's mmaped by unrelated
        // code.
        for (int i = 0; i < 10; ++i) {
          scudo::MemMapT MemMap;
          MemMap.map(/*Addr=*/0U, Size, MappingName);
          scudo::uptr P = MemMap.getBase();
          if (P == 0U)
            continue;
          MemMap.unmap(MemMap.getBase(), Size);
          memset(reinterpret_cast<void *>(P), 0xbb, Size);
        }
      },
      "");
}

TEST(ScudoMapDeathTest, MapWithGuardUnmap) {
  const scudo::uptr PageSize = scudo::getPageSizeCached();
  const scudo::uptr Size = 4 * PageSize;
  scudo::ReservedMemoryT ReservedMemory;
  ASSERT_TRUE(
      ReservedMemory.create(/*Addr=*/0U, Size + 2 * PageSize, MappingName));
  ASSERT_NE(ReservedMemory.getBase(), 0U);

  scudo::MemMapT MemMap =
      ReservedMemory.dispatch(ReservedMemory.getBase(), Size + 2 * PageSize);
  ASSERT_TRUE(MemMap.isAllocated());
  scudo::uptr Q = MemMap.getBase() + PageSize;
  ASSERT_TRUE(MemMap.remap(Q, Size, MappingName));
  memset(reinterpret_cast<void *>(Q), 0xaa, Size);
  EXPECT_DEATH(memset(reinterpret_cast<void *>(Q), 0xaa, Size + 1), "");
  MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
}

TEST(ScudoMapTest, MapGrowUnmap) {
  const scudo::uptr PageSize = scudo::getPageSizeCached();
  const scudo::uptr Size = 4 * PageSize;
  scudo::ReservedMemoryT ReservedMemory;
  ReservedMemory.create(/*Addr=*/0U, Size, MappingName);
  ASSERT_TRUE(ReservedMemory.isCreated());

  scudo::MemMapT MemMap =
      ReservedMemory.dispatch(ReservedMemory.getBase(), Size);
  ASSERT_TRUE(MemMap.isAllocated());
  scudo::uptr Q = MemMap.getBase() + PageSize;
  ASSERT_TRUE(MemMap.remap(Q, PageSize, MappingName));
  memset(reinterpret_cast<void *>(Q), 0xaa, PageSize);
  Q += PageSize;
  ASSERT_TRUE(MemMap.remap(Q, PageSize, MappingName));
  memset(reinterpret_cast<void *>(Q), 0xbb, PageSize);
  MemMap.unmap(MemMap.getBase(), MemMap.getCapacity());
}
