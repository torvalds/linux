//===-- HostThreadPosix.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/HostThreadPosix.h"
#include "lldb/Utility/Status.h"

#include <cerrno>
#include <pthread.h>

using namespace lldb;
using namespace lldb_private;

HostThreadPosix::HostThreadPosix() = default;

HostThreadPosix::HostThreadPosix(lldb::thread_t thread)
    : HostNativeThreadBase(thread) {}

HostThreadPosix::~HostThreadPosix() = default;

Status HostThreadPosix::Join(lldb::thread_result_t *result) {
  Status error;
  if (IsJoinable()) {
    int err = ::pthread_join(m_thread, result);
    error.SetError(err, lldb::eErrorTypePOSIX);
  } else {
    if (result)
      *result = nullptr;
    error.SetError(EINVAL, eErrorTypePOSIX);
  }

  Reset();
  return error;
}

Status HostThreadPosix::Cancel() {
  Status error;
  if (IsJoinable()) {
#ifndef __FreeBSD__
    llvm_unreachable("someone is calling HostThread::Cancel()");
#else
    int err = ::pthread_cancel(m_thread);
    error.SetError(err, eErrorTypePOSIX);
#endif
  }
  return error;
}

Status HostThreadPosix::Detach() {
  Status error;
  if (IsJoinable()) {
    int err = ::pthread_detach(m_thread);
    error.SetError(err, eErrorTypePOSIX);
  }
  Reset();
  return error;
}
