//===- llvm/Support/Atomic.h - Atomic Operations -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys atomic operations.
//
// DO NOT USE IN NEW CODE!
//
// New code should always rely on the std::atomic facilities in C++11.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMIC_H
#define LLVM_SUPPORT_ATOMIC_H

#include "llvm/Support/DataTypes.h"

// Windows will at times define MemoryFence.
#ifdef MemoryFence
#undef MemoryFence
#endif

namespace llvm {
  namespace sys {
    void MemoryFence();

#ifdef _MSC_VER
    typedef long cas_flag;
#else
    typedef uint32_t cas_flag;
#endif
    cas_flag CompareAndSwap(volatile cas_flag* ptr,
                            cas_flag new_value,
                            cas_flag old_value);
  }
}

#endif
