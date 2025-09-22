//===-- allocator_common.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ALLOCATOR_COMMON_H_
#define SCUDO_ALLOCATOR_COMMON_H_

#include "common.h"
#include "list.h"

namespace scudo {

template <class SizeClassAllocator> struct TransferBatch {
  typedef typename SizeClassAllocator::SizeClassMap SizeClassMap;
  typedef typename SizeClassAllocator::CompactPtrT CompactPtrT;

  static const u16 MaxNumCached = SizeClassMap::MaxNumCachedHint;
  void setFromArray(CompactPtrT *Array, u16 N) {
    DCHECK_LE(N, MaxNumCached);
    Count = N;
    memcpy(Batch, Array, sizeof(Batch[0]) * Count);
  }
  void appendFromArray(CompactPtrT *Array, u16 N) {
    DCHECK_LE(N, MaxNumCached - Count);
    memcpy(Batch + Count, Array, sizeof(Batch[0]) * N);
    // u16 will be promoted to int by arithmetic type conversion.
    Count = static_cast<u16>(Count + N);
  }
  void appendFromTransferBatch(TransferBatch *B, u16 N) {
    DCHECK_LE(N, MaxNumCached - Count);
    DCHECK_GE(B->Count, N);
    // Append from the back of `B`.
    memcpy(Batch + Count, B->Batch + (B->Count - N), sizeof(Batch[0]) * N);
    // u16 will be promoted to int by arithmetic type conversion.
    Count = static_cast<u16>(Count + N);
    B->Count = static_cast<u16>(B->Count - N);
  }
  void clear() { Count = 0; }
  bool empty() { return Count == 0; }
  void add(CompactPtrT P) {
    DCHECK_LT(Count, MaxNumCached);
    Batch[Count++] = P;
  }
  void moveToArray(CompactPtrT *Array) {
    memcpy(Array, Batch, sizeof(Batch[0]) * Count);
    clear();
  }

  void moveNToArray(CompactPtrT *Array, u16 N) {
    DCHECK_LE(N, Count);
    memcpy(Array, Batch + Count - N, sizeof(Batch[0]) * N);
    Count = static_cast<u16>(Count - N);
  }
  u16 getCount() const { return Count; }
  bool isEmpty() const { return Count == 0U; }
  CompactPtrT get(u16 I) const {
    DCHECK_LE(I, Count);
    return Batch[I];
  }
  TransferBatch *Next;

private:
  CompactPtrT Batch[MaxNumCached];
  u16 Count;
};

// A BatchGroup is used to collect blocks. Each group has a group id to
// identify the group kind of contained blocks.
template <class SizeClassAllocator> struct BatchGroup {
  // `Next` is used by IntrusiveList.
  BatchGroup *Next;
  // The compact base address of each group
  uptr CompactPtrGroupBase;
  // Cache value of SizeClassAllocatorLocalCache::getMaxCached()
  u16 MaxCachedPerBatch;
  // Number of blocks pushed into this group. This is an increment-only
  // counter.
  uptr PushedBlocks;
  // This is used to track how many bytes are not in-use since last time we
  // tried to release pages.
  uptr BytesInBGAtLastCheckpoint;
  // Blocks are managed by TransferBatch in a list.
  SinglyLinkedList<TransferBatch<SizeClassAllocator>> Batches;
};

} // namespace scudo

#endif // SCUDO_ALLOCATOR_COMMON_H_
