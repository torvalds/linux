//===-- HostThread.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTTHREAD_H
#define LLDB_HOST_HOSTTHREAD_H

#include "lldb/Host/HostNativeThreadForward.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-types.h"

#include <memory>

namespace lldb_private {

class HostNativeThreadBase;

/// \class HostInfo HostInfo.h "lldb/Host/HostThread.h"
/// A class that represents a thread running inside of a process on the
///        local machine.
///
/// HostThread allows querying and manipulation of threads running on the host
/// machine.
///
class HostThread {
public:
  HostThread();
  HostThread(lldb::thread_t thread);

  Status Join(lldb::thread_result_t *result);
  Status Cancel();
  void Reset();
  lldb::thread_t Release();

  bool IsJoinable() const;
  HostNativeThread &GetNativeThread();
  const HostNativeThread &GetNativeThread() const;
  lldb::thread_result_t GetResult() const;

  bool EqualsThread(lldb::thread_t thread) const;

private:
  std::shared_ptr<HostNativeThreadBase> m_native_thread;
};
}

#endif
