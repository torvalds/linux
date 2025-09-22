//===-- LockFileWindows.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/LockFileWindows.h"

#include <io.h>

using namespace lldb;
using namespace lldb_private;

static Status fileLock(HANDLE file_handle, DWORD flags, const uint64_t start,
                       const uint64_t len) {
  if (start != 0)
    return Status("Non-zero start lock regions are not supported");

  OVERLAPPED overlapped = {};

  if (!::LockFileEx(file_handle, flags, 0, len, 0, &overlapped) &&
      ::GetLastError() != ERROR_IO_PENDING)
    return Status(::GetLastError(), eErrorTypeWin32);

  DWORD bytes;
  if (!::GetOverlappedResult(file_handle, &overlapped, &bytes, TRUE))
    return Status(::GetLastError(), eErrorTypeWin32);

  return Status();
}

LockFileWindows::LockFileWindows(int fd)
    : LockFileBase(fd), m_file(reinterpret_cast<HANDLE>(_get_osfhandle(fd))) {}

LockFileWindows::~LockFileWindows() { Unlock(); }

bool LockFileWindows::IsValidFile() const {
  return LockFileBase::IsValidFile() && m_file != INVALID_HANDLE_VALUE;
}

Status LockFileWindows::DoWriteLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_file, LOCKFILE_EXCLUSIVE_LOCK, start, len);
}

Status LockFileWindows::DoTryWriteLock(const uint64_t start,
                                       const uint64_t len) {
  return fileLock(m_file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                  start, len);
}

Status LockFileWindows::DoReadLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_file, 0, start, len);
}

Status LockFileWindows::DoTryReadLock(const uint64_t start,
                                      const uint64_t len) {
  return fileLock(m_file, LOCKFILE_FAIL_IMMEDIATELY, start, len);
}

Status LockFileWindows::DoUnlock() {
  OVERLAPPED overlapped = {};

  if (!::UnlockFileEx(m_file, 0, m_len, 0, &overlapped) &&
      ::GetLastError() != ERROR_IO_PENDING)
    return Status(::GetLastError(), eErrorTypeWin32);

  DWORD bytes;
  if (!::GetOverlappedResult(m_file, &overlapped, &bytes, TRUE))
    return Status(::GetLastError(), eErrorTypeWin32);

  return Status();
}
