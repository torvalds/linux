//===-- HostNativeThreadBase.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTNATIVETHREADBASE_H
#define LLDB_HOST_HOSTNATIVETHREADBASE_H

#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

#if defined(_WIN32)
#define THREAD_ROUTINE __stdcall
#else
#define THREAD_ROUTINE
#endif

class HostNativeThreadBase {
  friend class ThreadLauncher;
  HostNativeThreadBase(const HostNativeThreadBase &) = delete;
  const HostNativeThreadBase &operator=(const HostNativeThreadBase &) = delete;

public:
  HostNativeThreadBase() = default;
  explicit HostNativeThreadBase(lldb::thread_t thread);
  virtual ~HostNativeThreadBase() = default;

  virtual Status Join(lldb::thread_result_t *result) = 0;
  virtual Status Cancel() = 0;
  virtual bool IsJoinable() const;
  virtual void Reset();
  virtual bool EqualsThread(lldb::thread_t thread) const;
  lldb::thread_t Release();

  lldb::thread_t GetSystemHandle() const;
  lldb::thread_result_t GetResult() const;

protected:
  static lldb::thread_result_t THREAD_ROUTINE
  ThreadCreateTrampoline(lldb::thread_arg_t arg);

  lldb::thread_t m_thread = LLDB_INVALID_HOST_THREAD;
  lldb::thread_result_t m_result = 0; // NOLINT(modernize-use-nullptr)
};
}

#endif
