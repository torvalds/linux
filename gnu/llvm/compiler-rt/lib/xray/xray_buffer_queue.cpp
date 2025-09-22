//===-- xray_buffer_queue.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Defines the interface for a buffer queue implementation.
//
//===----------------------------------------------------------------------===//
#include "xray_buffer_queue.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#if !SANITIZER_FUCHSIA
#include "sanitizer_common/sanitizer_posix.h"
#endif
#include "xray_allocator.h"
#include "xray_defs.h"
#include <memory>
#include <sys/mman.h>

using namespace __xray;

namespace {

BufferQueue::ControlBlock *allocControlBlock(size_t Size, size_t Count) {
  auto B =
      allocateBuffer((sizeof(BufferQueue::ControlBlock) - 1) + (Size * Count));
  return B == nullptr ? nullptr
                      : reinterpret_cast<BufferQueue::ControlBlock *>(B);
}

void deallocControlBlock(BufferQueue::ControlBlock *C, size_t Size,
                         size_t Count) {
  deallocateBuffer(reinterpret_cast<unsigned char *>(C),
                   (sizeof(BufferQueue::ControlBlock) - 1) + (Size * Count));
}

void decRefCount(BufferQueue::ControlBlock *C, size_t Size, size_t Count) {
  if (C == nullptr)
    return;
  if (atomic_fetch_sub(&C->RefCount, 1, memory_order_acq_rel) == 1)
    deallocControlBlock(C, Size, Count);
}

void incRefCount(BufferQueue::ControlBlock *C) {
  if (C == nullptr)
    return;
  atomic_fetch_add(&C->RefCount, 1, memory_order_acq_rel);
}

// We use a struct to ensure that we are allocating one atomic_uint64_t per
// cache line. This allows us to not worry about false-sharing among atomic
// objects being updated (constantly) by different threads.
struct ExtentsPadded {
  union {
    atomic_uint64_t Extents;
    unsigned char Storage[kCacheLineSize];
  };
};

constexpr size_t kExtentsSize = sizeof(ExtentsPadded);

} // namespace

BufferQueue::ErrorCode BufferQueue::init(size_t BS, size_t BC) {
  SpinMutexLock Guard(&Mutex);

  if (!finalizing())
    return BufferQueue::ErrorCode::AlreadyInitialized;

  cleanupBuffers();

  bool Success = false;
  BufferSize = BS;
  BufferCount = BC;

  BackingStore = allocControlBlock(BufferSize, BufferCount);
  if (BackingStore == nullptr)
    return BufferQueue::ErrorCode::NotEnoughMemory;

  auto CleanupBackingStore = at_scope_exit([&, this] {
    if (Success)
      return;
    deallocControlBlock(BackingStore, BufferSize, BufferCount);
    BackingStore = nullptr;
  });

  // Initialize enough atomic_uint64_t instances, each
  ExtentsBackingStore = allocControlBlock(kExtentsSize, BufferCount);
  if (ExtentsBackingStore == nullptr)
    return BufferQueue::ErrorCode::NotEnoughMemory;

  auto CleanupExtentsBackingStore = at_scope_exit([&, this] {
    if (Success)
      return;
    deallocControlBlock(ExtentsBackingStore, kExtentsSize, BufferCount);
    ExtentsBackingStore = nullptr;
  });

  Buffers = initArray<BufferRep>(BufferCount);
  if (Buffers == nullptr)
    return BufferQueue::ErrorCode::NotEnoughMemory;

  // At this point we increment the generation number to associate the buffers
  // to the new generation.
  atomic_fetch_add(&Generation, 1, memory_order_acq_rel);

  // First, we initialize the refcount in the ControlBlock, which we treat as
  // being at the start of the BackingStore pointer.
  atomic_store(&BackingStore->RefCount, 1, memory_order_release);
  atomic_store(&ExtentsBackingStore->RefCount, 1, memory_order_release);

  // Then we initialise the individual buffers that sub-divide the whole backing
  // store. Each buffer will start at the `Data` member of the ControlBlock, and
  // will be offsets from these locations.
  for (size_t i = 0; i < BufferCount; ++i) {
    auto &T = Buffers[i];
    auto &Buf = T.Buff;
    auto *E = reinterpret_cast<ExtentsPadded *>(&ExtentsBackingStore->Data +
                                                (kExtentsSize * i));
    Buf.Extents = &E->Extents;
    atomic_store(Buf.Extents, 0, memory_order_release);
    Buf.Generation = generation();
    Buf.Data = &BackingStore->Data + (BufferSize * i);
    Buf.Size = BufferSize;
    Buf.BackingStore = BackingStore;
    Buf.ExtentsBackingStore = ExtentsBackingStore;
    Buf.Count = BufferCount;
    T.Used = false;
  }

  Next = Buffers;
  First = Buffers;
  LiveBuffers = 0;
  atomic_store(&Finalizing, 0, memory_order_release);
  Success = true;
  return BufferQueue::ErrorCode::Ok;
}

BufferQueue::BufferQueue(size_t B, size_t N,
                         bool &Success) XRAY_NEVER_INSTRUMENT
    : BufferSize(B),
      BufferCount(N),
      Mutex(),
      Finalizing{1},
      BackingStore(nullptr),
      ExtentsBackingStore(nullptr),
      Buffers(nullptr),
      Next(Buffers),
      First(Buffers),
      LiveBuffers(0),
      Generation{0} {
  Success = init(B, N) == BufferQueue::ErrorCode::Ok;
}

BufferQueue::ErrorCode BufferQueue::getBuffer(Buffer &Buf) {
  if (atomic_load(&Finalizing, memory_order_acquire))
    return ErrorCode::QueueFinalizing;

  BufferRep *B = nullptr;
  {
    SpinMutexLock Guard(&Mutex);
    if (LiveBuffers == BufferCount)
      return ErrorCode::NotEnoughMemory;
    B = Next++;
    if (Next == (Buffers + BufferCount))
      Next = Buffers;
    ++LiveBuffers;
  }

  incRefCount(BackingStore);
  incRefCount(ExtentsBackingStore);
  Buf = B->Buff;
  Buf.Generation = generation();
  B->Used = true;
  return ErrorCode::Ok;
}

BufferQueue::ErrorCode BufferQueue::releaseBuffer(Buffer &Buf) {
  // Check whether the buffer being referred to is within the bounds of the
  // backing store's range.
  BufferRep *B = nullptr;
  {
    SpinMutexLock Guard(&Mutex);
    if (Buf.Generation != generation() || LiveBuffers == 0) {
      Buf = {};
      decRefCount(Buf.BackingStore, Buf.Size, Buf.Count);
      decRefCount(Buf.ExtentsBackingStore, kExtentsSize, Buf.Count);
      return BufferQueue::ErrorCode::Ok;
    }

    if (Buf.Data < &BackingStore->Data ||
        Buf.Data > &BackingStore->Data + (BufferCount * BufferSize))
      return BufferQueue::ErrorCode::UnrecognizedBuffer;

    --LiveBuffers;
    B = First++;
    if (First == (Buffers + BufferCount))
      First = Buffers;
  }

  // Now that the buffer has been released, we mark it as "used".
  B->Buff = Buf;
  B->Used = true;
  decRefCount(Buf.BackingStore, Buf.Size, Buf.Count);
  decRefCount(Buf.ExtentsBackingStore, kExtentsSize, Buf.Count);
  atomic_store(B->Buff.Extents, atomic_load(Buf.Extents, memory_order_acquire),
               memory_order_release);
  Buf = {};
  return ErrorCode::Ok;
}

BufferQueue::ErrorCode BufferQueue::finalize() {
  if (atomic_exchange(&Finalizing, 1, memory_order_acq_rel))
    return ErrorCode::QueueFinalizing;
  return ErrorCode::Ok;
}

void BufferQueue::cleanupBuffers() {
  for (auto B = Buffers, E = Buffers + BufferCount; B != E; ++B)
    B->~BufferRep();
  deallocateBuffer(Buffers, BufferCount);
  decRefCount(BackingStore, BufferSize, BufferCount);
  decRefCount(ExtentsBackingStore, kExtentsSize, BufferCount);
  BackingStore = nullptr;
  ExtentsBackingStore = nullptr;
  Buffers = nullptr;
  BufferCount = 0;
  BufferSize = 0;
}

BufferQueue::~BufferQueue() { cleanupBuffers(); }
