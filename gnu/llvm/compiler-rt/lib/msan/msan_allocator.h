//===-- msan_allocator.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef MSAN_ALLOCATOR_H
#define MSAN_ALLOCATOR_H

#include "sanitizer_common/sanitizer_common.h"

namespace __msan {

struct MsanThreadLocalMallocStorage {
  // Allocator cache contains atomic_uint64_t which must be 8-byte aligned.
  alignas(8) uptr allocator_cache[96 * (512 * 8 + 16)];  // Opaque.
  void Init();
  void CommitBack();

 private:
  // These objects are allocated via mmap() and are zero-initialized.
  MsanThreadLocalMallocStorage() {}
};

void LockAllocator();
void UnlockAllocator();

} // namespace __msan
#endif // MSAN_ALLOCATOR_H
