//===-- ThreadCollection.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADCOLLECTION_H
#define LLDB_TARGET_THREADCOLLECTION_H

#include <mutex>
#include <vector>

#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ThreadCollection {
public:
  typedef std::vector<lldb::ThreadSP> collection;
  typedef LockingAdaptedIterable<collection, lldb::ThreadSP, vector_adapter,
                                 std::recursive_mutex>
      ThreadIterable;

  ThreadCollection();

  ThreadCollection(collection threads);

  virtual ~ThreadCollection() = default;

  uint32_t GetSize();

  void AddThread(const lldb::ThreadSP &thread_sp);

  void AddThreadSortedByIndexID(const lldb::ThreadSP &thread_sp);

  void InsertThread(const lldb::ThreadSP &thread_sp, uint32_t idx);

  // Note that "idx" is not the same as the "thread_index". It is a zero based
  // index to accessing the current threads, whereas "thread_index" is a unique
  // index assigned
  lldb::ThreadSP GetThreadAtIndex(uint32_t idx);

  virtual ThreadIterable Threads() {
    return ThreadIterable(m_threads, GetMutex());
  }

  virtual std::recursive_mutex &GetMutex() const { return m_mutex; }

protected:
  collection m_threads;
  mutable std::recursive_mutex m_mutex;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADCOLLECTION_H
