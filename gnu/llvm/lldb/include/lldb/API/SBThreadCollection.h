//===-- SBThreadCollection.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTHREADCOLLECTION_H
#define LLDB_API_SBTHREADCOLLECTION_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBThreadCollection {
public:
  SBThreadCollection();

  SBThreadCollection(const SBThreadCollection &rhs);

  const SBThreadCollection &operator=(const SBThreadCollection &rhs);

  ~SBThreadCollection();

  explicit operator bool() const;

  bool IsValid() const;

  size_t GetSize();

  lldb::SBThread GetThreadAtIndex(size_t idx);

protected:
  // Mimic shared pointer...
  lldb_private::ThreadCollection *get() const;

  lldb_private::ThreadCollection *operator->() const;

  lldb::ThreadCollectionSP &operator*();

  const lldb::ThreadCollectionSP &operator*() const;

  SBThreadCollection(const lldb::ThreadCollectionSP &threads);

  void SetOpaque(const lldb::ThreadCollectionSP &threads);

private:
  friend class SBProcess;
  friend class SBThread;

  lldb::ThreadCollectionSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBTHREADCOLLECTION_H
