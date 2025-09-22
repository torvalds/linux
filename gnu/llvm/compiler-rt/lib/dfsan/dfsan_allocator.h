//===-- dfsan_allocator.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataflowSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef DFSAN_ALLOCATOR_H
#define DFSAN_ALLOCATOR_H

#include "sanitizer_common/sanitizer_common.h"

namespace __dfsan {

struct DFsanThreadLocalMallocStorage {
  alignas(8) uptr allocator_cache[96 * (512 * 8 + 16)];  // Opaque.
  void CommitBack();

 private:
  // These objects are allocated via mmap() and are zero-initialized.
  DFsanThreadLocalMallocStorage() {}
};

}  // namespace __dfsan
#endif  // DFSAN_ALLOCATOR_H
