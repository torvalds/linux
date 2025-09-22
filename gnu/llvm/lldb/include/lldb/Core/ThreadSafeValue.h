//===-- ThreadSafeValue.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_THREADSAFEVALUE_H
#define LLDB_CORE_THREADSAFEVALUE_H

#include <mutex>

#include "lldb/lldb-defines.h"

namespace lldb_private {

template <class T> class ThreadSafeValue {
public:
  ThreadSafeValue() = default;
  ThreadSafeValue(const T &value) : m_value(value) {}

  ~ThreadSafeValue() = default;

  T GetValue() const {
    T value;
    {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      value = m_value;
    }
    return value;
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  const T &GetValueNoLock() const { return m_value; }

  void SetValue(const T &value) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_value = value;
  }

  // Call this if you have already manually locked the mutex using the
  // GetMutex() accessor
  // coverity[missing_lock]
  void SetValueNoLock(const T &value) { m_value = value; }

  std::recursive_mutex &GetMutex() { return m_mutex; }

private:
  T m_value;
  mutable std::recursive_mutex m_mutex;

  // For ThreadSafeValue only
  ThreadSafeValue(const ThreadSafeValue &) = delete;
  const ThreadSafeValue &operator=(const ThreadSafeValue &) = delete;
};

} // namespace lldb_private
#endif // LLDB_CORE_THREADSAFEVALUE_H
