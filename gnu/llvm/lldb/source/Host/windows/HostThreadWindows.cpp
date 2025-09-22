//===-- HostThreadWindows.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Status.h"

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

static void __stdcall ExitThreadProxy(ULONG_PTR dwExitCode) {
  ::ExitThread(dwExitCode);
}

HostThreadWindows::HostThreadWindows()
    : HostNativeThreadBase(), m_owns_handle(true) {}

HostThreadWindows::HostThreadWindows(lldb::thread_t thread)
    : HostNativeThreadBase(thread), m_owns_handle(true) {}

HostThreadWindows::~HostThreadWindows() { Reset(); }

void HostThreadWindows::SetOwnsHandle(bool owns) { m_owns_handle = owns; }

Status HostThreadWindows::Join(lldb::thread_result_t *result) {
  Status error;
  if (IsJoinable()) {
    DWORD wait_result = ::WaitForSingleObject(m_thread, INFINITE);
    if (WAIT_OBJECT_0 == wait_result && result) {
      DWORD exit_code = 0;
      if (!::GetExitCodeThread(m_thread, &exit_code))
        *result = 0;
      *result = exit_code;
    } else if (WAIT_OBJECT_0 != wait_result)
      error.SetError(::GetLastError(), eErrorTypeWin32);
  } else
    error.SetError(ERROR_INVALID_HANDLE, eErrorTypeWin32);

  Reset();
  return error;
}

Status HostThreadWindows::Cancel() {
  Status error;

  DWORD result = ::QueueUserAPC(::ExitThreadProxy, m_thread, 0);
  error.SetError(result, eErrorTypeWin32);
  return error;
}

lldb::tid_t HostThreadWindows::GetThreadId() const {
  return ::GetThreadId(m_thread);
}

void HostThreadWindows::Reset() {
  if (m_owns_handle && m_thread != LLDB_INVALID_HOST_THREAD)
    ::CloseHandle(m_thread);

  HostNativeThreadBase::Reset();
}

bool HostThreadWindows::EqualsThread(lldb::thread_t thread) const {
  return GetThreadId() == ::GetThreadId(thread);
}
