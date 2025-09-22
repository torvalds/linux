//===-- asan_memory_profile.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file implements __sanitizer_print_memory_profile.
//===----------------------------------------------------------------------===//

#include "asan/asan_allocator.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

#if CAN_SANITIZE_LEAKS

namespace __asan {

struct AllocationSite {
  u32 id;
  uptr total_size;
  uptr count;
};

class HeapProfile {
 public:
  HeapProfile() { allocations_.reserve(1024); }

  void ProcessChunk(const AsanChunkView &cv) {
    if (cv.IsAllocated()) {
      total_allocated_user_size_ += cv.UsedSize();
      total_allocated_count_++;
      u32 id = cv.GetAllocStackId();
      if (id)
        Insert(id, cv.UsedSize());
    } else if (cv.IsQuarantined()) {
      total_quarantined_user_size_ += cv.UsedSize();
      total_quarantined_count_++;
    } else {
      total_other_count_++;
    }
  }

  void Print(uptr top_percent, uptr max_number_of_contexts) {
    Sort(allocations_.data(), allocations_.size(),
         [](const AllocationSite &a, const AllocationSite &b) {
           return a.total_size > b.total_size;
         });
    CHECK(total_allocated_user_size_);
    uptr total_shown = 0;
    Printf("Live Heap Allocations: %zd bytes in %zd chunks; quarantined: "
           "%zd bytes in %zd chunks; %zd other chunks; total chunks: %zd; "
           "showing top %zd%% (at most %zd unique contexts)\n",
           total_allocated_user_size_, total_allocated_count_,
           total_quarantined_user_size_, total_quarantined_count_,
           total_other_count_, total_allocated_count_ +
           total_quarantined_count_ + total_other_count_, top_percent,
           max_number_of_contexts);
    for (uptr i = 0; i < Min(allocations_.size(), max_number_of_contexts);
         i++) {
      auto &a = allocations_[i];
      Printf("%zd byte(s) (%zd%%) in %zd allocation(s)\n", a.total_size,
             a.total_size * 100 / total_allocated_user_size_, a.count);
      StackDepotGet(a.id).Print();
      total_shown += a.total_size;
      if (total_shown * 100 / total_allocated_user_size_ > top_percent)
        break;
    }
  }

 private:
  uptr total_allocated_user_size_ = 0;
  uptr total_allocated_count_ = 0;
  uptr total_quarantined_user_size_ = 0;
  uptr total_quarantined_count_ = 0;
  uptr total_other_count_ = 0;
  InternalMmapVector<AllocationSite> allocations_;

  void Insert(u32 id, uptr size) {
    // Linear lookup will be good enough for most cases (although not all).
    for (uptr i = 0; i < allocations_.size(); i++) {
      if (allocations_[i].id == id) {
        allocations_[i].total_size += size;
        allocations_[i].count++;
        return;
      }
    }
    allocations_.push_back({id, size, 1});
  }
};

static void ChunkCallback(uptr chunk, void *arg) {
  reinterpret_cast<HeapProfile*>(arg)->ProcessChunk(
      FindHeapChunkByAllocBeg(chunk));
}

static void MemoryProfileCB(uptr top_percent, uptr max_number_of_contexts) {
  HeapProfile hp;
  __lsan::LockAllocator();
  __lsan::ForEachChunk(ChunkCallback, &hp);
  __lsan::UnlockAllocator();
  hp.Print(top_percent, max_number_of_contexts);

  if (Verbosity())
    __asan_print_accumulated_stats();
}
}  // namespace __asan

#endif  // CAN_SANITIZE_LEAKS

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_memory_profile(uptr top_percent,
                                      uptr max_number_of_contexts) {
#if CAN_SANITIZE_LEAKS
  __asan::MemoryProfileCB(top_percent, max_number_of_contexts);
#endif  // CAN_SANITIZE_LEAKS
}
}  // extern "C"
