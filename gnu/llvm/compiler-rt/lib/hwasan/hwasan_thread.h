//===-- hwasan_thread.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_THREAD_H
#define HWASAN_THREAD_H

#include "hwasan_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_ring_buffer.h"

namespace __hwasan {

typedef __sanitizer::CompactRingBuffer<uptr> StackAllocationsRingBuffer;

class Thread {
 public:
  // These are optional parameters that can be passed to Init.
  struct InitState;

  void Init(uptr stack_buffer_start, uptr stack_buffer_size,
            const InitState *state = nullptr);

  void InitStackAndTls(const InitState *state = nullptr);

  // Must be called from the thread itself.
  void InitStackRingBuffer(uptr stack_buffer_start, uptr stack_buffer_size);

  inline void EnsureRandomStateInited() {
    if (UNLIKELY(!random_state_inited_))
      InitRandomState();
  }

  void Destroy();

  uptr stack_top() { return stack_top_; }
  uptr stack_bottom() { return stack_bottom_; }
  uptr stack_size() { return stack_top() - stack_bottom(); }
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }
  bool IsMainThread() { return unique_id_ == 0; }

  bool AddrIsInStack(uptr addr) {
    return addr >= stack_bottom_ && addr < stack_top_;
  }

  AllocatorCache *allocator_cache() { return &allocator_cache_; }
  HeapAllocationsRingBuffer *heap_allocations() { return heap_allocations_; }
  StackAllocationsRingBuffer *stack_allocations() { return stack_allocations_; }

  tag_t GenerateRandomTag(uptr num_bits = kTagBits);

  void DisableTagging() { tagging_disabled_++; }
  void EnableTagging() { tagging_disabled_--; }

  u32 unique_id() const { return unique_id_; }
  void Announce() {
    if (announced_) return;
    announced_ = true;
    Print("Thread: ");
  }

  tid_t os_id() const { return os_id_; }
  void set_os_id(tid_t os_id) { os_id_ = os_id; }

  uptr &vfork_spill() { return vfork_spill_; }

 private:
  // NOTE: There is no Thread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.
  void ClearShadowForThreadStackAndTLS();
  void Print(const char *prefix);
  void InitRandomState();
  uptr vfork_spill_;
  uptr stack_top_;
  uptr stack_bottom_;
  uptr tls_begin_;
  uptr tls_end_;
  DTLS *dtls_;

  u32 random_state_;
  u32 random_buffer_;

  AllocatorCache allocator_cache_;
  HeapAllocationsRingBuffer *heap_allocations_;
  StackAllocationsRingBuffer *stack_allocations_;

  u32 unique_id_;  // counting from zero.

  tid_t os_id_;

  u32 tagging_disabled_;  // if non-zero, malloc uses zero tag in this thread.

  bool announced_;

  bool random_state_inited_;  // Whether InitRandomState() has been called.

  friend struct ThreadListHead;
};

Thread *GetCurrentThread();
uptr *GetCurrentThreadLongPtr();

// Used to handle fork().
void EnsureMainThreadIDIsCorrect();

struct ScopedTaggingDisabler {
  ScopedTaggingDisabler() { GetCurrentThread()->DisableTagging(); }
  ~ScopedTaggingDisabler() { GetCurrentThread()->EnableTagging(); }
};

} // namespace __hwasan

#endif // HWASAN_THREAD_H
