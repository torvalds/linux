//===-- ThreadSafeDenseSet.h ------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadSafeDenseSet_h_
#define liblldb_ThreadSafeDenseSet_h_

#include <mutex>

#include "llvm/ADT/DenseSet.h"


namespace lldb_private {

template <typename _ElementType, typename _MutexType = std::mutex>
class ThreadSafeDenseSet {
public:
  typedef llvm::DenseSet<_ElementType> LLVMSetType;

  ThreadSafeDenseSet(unsigned set_initial_capacity = 0)
      : m_set(set_initial_capacity), m_mutex() {}

  void Insert(_ElementType e) {
    std::lock_guard<_MutexType> guard(m_mutex);
    m_set.insert(e);
  }

  void Erase(_ElementType e) {
    std::lock_guard<_MutexType> guard(m_mutex);
    m_set.erase(e);
  }

  bool Lookup(_ElementType e) {
    std::lock_guard<_MutexType> guard(m_mutex);
    return (m_set.count(e) > 0);
  }

  void Clear() {
    std::lock_guard<_MutexType> guard(m_mutex);
    m_set.clear();
  }

protected:
  LLVMSetType m_set;
  _MutexType m_mutex;
};

} // namespace lldb_private

#endif // liblldb_ThreadSafeDenseSet_h_
