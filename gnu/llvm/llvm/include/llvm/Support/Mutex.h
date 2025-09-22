//===- llvm/Support/Mutex.h - Mutex Operating System Concept -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Mutex class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MUTEX_H
#define LLVM_SUPPORT_MUTEX_H

#include "llvm/Support/Threading.h"
#include <cassert>
#include <mutex>

namespace llvm
{
  namespace sys
  {
    /// SmartMutex - A mutex with a compile time constant parameter that
    /// indicates whether this mutex should become a no-op when we're not
    /// running in multithreaded mode.
    template<bool mt_only>
    class SmartMutex {
      std::recursive_mutex impl;
      unsigned acquired = 0;

    public:
      bool lock() {
        if (!mt_only || llvm_is_multithreaded()) {
          impl.lock();
          return true;
        }
        // Single-threaded debugging code.  This would be racy in
        // multithreaded mode, but provides not basic checks in single
        // threaded mode.
        ++acquired;
        return true;
      }

      bool unlock() {
        if (!mt_only || llvm_is_multithreaded()) {
          impl.unlock();
          return true;
        }
        // Single-threaded debugging code.  This would be racy in
        // multithreaded mode, but provides not basic checks in single
        // threaded mode.
        assert(acquired && "Lock not acquired before release!");
        --acquired;
        return true;
      }

      bool try_lock() {
        if (!mt_only || llvm_is_multithreaded())
          return impl.try_lock();
        return true;
      }
    };

    /// Mutex - A standard, always enforced mutex.
    typedef SmartMutex<false> Mutex;

    template <bool mt_only>
    using SmartScopedLock = std::lock_guard<SmartMutex<mt_only>>;

    typedef SmartScopedLock<false> ScopedLock;
  }
}

#endif
