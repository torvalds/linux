//===-- sanitizer_stack_store.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_STACK_STORE_H
#define SANITIZER_STACK_STORE_H

#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_mutex.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

class StackStore {
  static constexpr uptr kBlockSizeFrames = 0x100000;
  static constexpr uptr kBlockCount = 0x1000;
  static constexpr uptr kBlockSizeBytes = kBlockSizeFrames * sizeof(uptr);

 public:
  enum class Compression : u8 {
    None = 0,
    Delta,
    LZW,
  };

  constexpr StackStore() = default;

  using Id = u32;  // Enough for 2^32 * sizeof(uptr) bytes of traces.
  static_assert(u64(kBlockCount) * kBlockSizeFrames == 1ull << (sizeof(Id) * 8),
                "");

  Id Store(const StackTrace &trace,
           uptr *pack /* number of blocks completed by this call */);
  StackTrace Load(Id id);
  uptr Allocated() const;

  // Packs all blocks which don't expect any more writes. A block is going to be
  // packed once. As soon trace from that block was requested, it will unpack
  // and stay unpacked after that.
  // Returns the number of released bytes.
  uptr Pack(Compression type);

  void LockAll();
  void UnlockAll();

  void TestOnlyUnmap();

 private:
  friend class StackStoreTest;
  static constexpr uptr GetBlockIdx(uptr frame_idx) {
    return frame_idx / kBlockSizeFrames;
  }

  static constexpr uptr GetInBlockIdx(uptr frame_idx) {
    return frame_idx % kBlockSizeFrames;
  }

  static constexpr uptr IdToOffset(Id id) {
    CHECK_NE(id, 0);
    return id - 1;  // Avoid zero as id.
  }

  static constexpr uptr OffsetToId(Id id) {
    // This makes UINT32_MAX to 0 and it will be retrived as and empty stack.
    // But this is not a problem as we will not be able to store anything after
    // that anyway.
    return id + 1;  // Avoid zero as id.
  }

  uptr *Alloc(uptr count, uptr *idx, uptr *pack);

  void *Map(uptr size, const char *mem_type);
  void Unmap(void *addr, uptr size);

  // Total number of allocated frames.
  atomic_uintptr_t total_frames_ = {};

  // Tracks total allocated memory in bytes.
  atomic_uintptr_t allocated_ = {};

  // Each block will hold pointer to exactly kBlockSizeFrames.
  class BlockInfo {
    atomic_uintptr_t data_;
    // Counter to track store progress to know when we can Pack() the block.
    atomic_uint32_t stored_;
    // Protects alloc of new blocks.
    mutable StaticSpinMutex mtx_;

    enum class State : u8 {
      Storing = 0,
      Packed,
      Unpacked,
    };
    State state SANITIZER_GUARDED_BY(mtx_);

    uptr *Create(StackStore *store);

   public:
    uptr *Get() const;
    uptr *GetOrCreate(StackStore *store);
    uptr *GetOrUnpack(StackStore *store);
    uptr Pack(Compression type, StackStore *store);
    void TestOnlyUnmap(StackStore *store);
    bool Stored(uptr n);
    bool IsPacked() const;
    void Lock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS { mtx_.Lock(); }
    void Unlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS { mtx_.Unlock(); }
  };

  BlockInfo blocks_[kBlockCount] = {};
};

}  // namespace __sanitizer

#endif  // SANITIZER_STACK_STORE_H
