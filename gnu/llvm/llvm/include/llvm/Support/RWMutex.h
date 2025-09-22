//===- RWMutex.h - Reader/Writer Mutual Exclusion Lock ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::RWMutex class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RWMUTEX_H
#define LLVM_SUPPORT_RWMUTEX_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Threading.h"
#include <cassert>
#include <mutex>
#include <shared_mutex>

#if defined(__APPLE__)
#define LLVM_USE_RW_MUTEX_IMPL
#endif

namespace llvm {
namespace sys {

#if defined(LLVM_USE_RW_MUTEX_IMPL)
/// Platform agnostic RWMutex class.
class RWMutexImpl {
  /// @name Constructors
  /// @{
public:
  /// Initializes the lock but doesn't acquire it.
  /// Default Constructor.
  explicit RWMutexImpl();

  /// @}
  /// @name Do Not Implement
  /// @{
  RWMutexImpl(const RWMutexImpl &original) = delete;
  RWMutexImpl &operator=(const RWMutexImpl &) = delete;
  /// @}

  /// Releases and removes the lock
  /// Destructor
  ~RWMutexImpl();

  /// @}
  /// @name Methods
  /// @{
public:
  /// Attempts to unconditionally acquire the lock in reader mode. If the
  /// lock is held by a writer, this method will wait until it can acquire
  /// the lock.
  /// @returns false if any kind of error occurs, true otherwise.
  /// Unconditionally acquire the lock in reader mode.
  bool lock_shared();

  /// Attempts to release the lock in reader mode.
  /// @returns false if any kind of error occurs, true otherwise.
  /// Unconditionally release the lock in reader mode.
  bool unlock_shared();

  /// Attempts to acquire the lock in reader mode. Returns immediately.
  /// @returns true on successful lock acquisition, false otherwise.
  bool try_lock_shared();

  /// Attempts to unconditionally acquire the lock in reader mode. If the
  /// lock is held by any readers, this method will wait until it can
  /// acquire the lock.
  /// @returns false if any kind of error occurs, true otherwise.
  /// Unconditionally acquire the lock in writer mode.
  bool lock();

  /// Attempts to release the lock in writer mode.
  /// @returns false if any kind of error occurs, true otherwise.
  /// Unconditionally release the lock in write mode.
  bool unlock();

  /// Attempts to acquire the lock in writer mode. Returns immediately.
  /// @returns true on successful lock acquisition, false otherwise.
  bool try_lock();

  //@}
  /// @name Platform Dependent Data
  /// @{
private:
#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
  void *data_ = nullptr; ///< We don't know what the data will be
#endif
};
#endif

/// SmartMutex - An R/W mutex with a compile time constant parameter that
/// indicates whether this mutex should become a no-op when we're not
/// running in multithreaded mode.
template <bool mt_only> class SmartRWMutex {
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
  std::shared_mutex impl;
#else
  RWMutexImpl impl;
#endif
  unsigned readers = 0;
  unsigned writers = 0;

public:
  bool lock_shared() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.lock_shared();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    ++readers;
    return true;
  }

  bool unlock_shared() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.unlock_shared();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(readers > 0 && "Reader lock not acquired before release!");
    --readers;
    return true;
  }

  bool try_lock_shared() { return impl.try_lock_shared(); }

  bool lock() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.lock();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(writers == 0 && "Writer lock already acquired!");
    ++writers;
    return true;
  }

  bool unlock() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.unlock();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(writers == 1 && "Writer lock not acquired before release!");
    --writers;
    return true;
  }

  bool try_lock() { return impl.try_lock(); }
};

typedef SmartRWMutex<false> RWMutex;

/// ScopedReader - RAII acquisition of a reader lock
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
template <bool mt_only>
using SmartScopedReader = const std::shared_lock<SmartRWMutex<mt_only>>;
#else
template <bool mt_only> struct SmartScopedReader {
  SmartRWMutex<mt_only> &mutex;

  explicit SmartScopedReader(SmartRWMutex<mt_only> &m) : mutex(m) {
    mutex.lock_shared();
  }

  ~SmartScopedReader() { mutex.unlock_shared(); }
};
#endif
typedef SmartScopedReader<false> ScopedReader;

/// ScopedWriter - RAII acquisition of a writer lock
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
template <bool mt_only>
using SmartScopedWriter = std::lock_guard<SmartRWMutex<mt_only>>;
#else
template <bool mt_only> struct SmartScopedWriter {
  SmartRWMutex<mt_only> &mutex;

  explicit SmartScopedWriter(SmartRWMutex<mt_only> &m) : mutex(m) {
    mutex.lock();
  }

  ~SmartScopedWriter() { mutex.unlock(); }
};
#endif
typedef SmartScopedWriter<false> ScopedWriter;

} // end namespace sys
} // end namespace llvm

#endif // LLVM_SUPPORT_RWMUTEX_H
