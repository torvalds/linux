//===-- iterate.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

#include <algorithm>
#include <set>
#include <vector>

TEST_F(CustomGuardedPoolAllocator, Iterate) {
  InitNumSlots(7);
  std::vector<std::pair<void *, size_t>> Allocated;
  auto alloc = [&](size_t size) {
    Allocated.push_back({GPA.allocate(size), size});
  };

  void *Ptr = GPA.allocate(5);
  alloc(2);
  alloc(1);
  alloc(100);
  GPA.deallocate(Ptr);
  alloc(42);
  std::sort(Allocated.begin(), Allocated.end());

  GPA.disable();
  void *Base = Allocated[0].first;
  size_t Size = reinterpret_cast<size_t>(Allocated.back().first) -
                reinterpret_cast<size_t>(Base) + 1;
  std::vector<std::pair<void *, size_t>> Found;
  GPA.iterate(
      Base, Size,
      [](uintptr_t Addr, size_t Size, void *Arg) {
        reinterpret_cast<std::vector<std::pair<void *, size_t>> *>(Arg)
            ->push_back({(void *)Addr, Size});
      },
      reinterpret_cast<void *>(&Found));
  GPA.enable();

  std::sort(Found.begin(), Found.end());
  EXPECT_EQ(Allocated, Found);

  // Now without the last allocation.
  GPA.disable();
  Size = reinterpret_cast<size_t>(Allocated.back().first) -
         reinterpret_cast<size_t>(Base); // Allocated.back() is out of range.
  Found.clear();
  GPA.iterate(
      Base, Size,
      [](uintptr_t Addr, size_t Size, void *Arg) {
        reinterpret_cast<std::vector<std::pair<void *, size_t>> *>(Arg)
            ->push_back({(void *)Addr, Size});
      },
      reinterpret_cast<void *>(&Found));
  GPA.enable();

  // We should have found every allocation but the last.
  // Remove it and compare the rest.
  std::sort(Found.begin(), Found.end());
  GPA.deallocate(Allocated.back().first);
  Allocated.pop_back();
  EXPECT_EQ(Allocated, Found);

  for (auto PS : Allocated)
    GPA.deallocate(PS.first);
}
