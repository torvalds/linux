//===-- LockFileBase.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/LockFileBase.h"

using namespace lldb;
using namespace lldb_private;

static Status AlreadyLocked() { return Status("Already locked"); }

static Status NotLocked() { return Status("Not locked"); }

LockFileBase::LockFileBase(int fd)
    : m_fd(fd), m_locked(false), m_start(0), m_len(0) {}

bool LockFileBase::IsLocked() const { return m_locked; }

Status LockFileBase::WriteLock(const uint64_t start, const uint64_t len) {
  return DoLock([&](const uint64_t start,
                    const uint64_t len) { return DoWriteLock(start, len); },
                start, len);
}

Status LockFileBase::TryWriteLock(const uint64_t start, const uint64_t len) {
  return DoLock([&](const uint64_t start,
                    const uint64_t len) { return DoTryWriteLock(start, len); },
                start, len);
}

Status LockFileBase::ReadLock(const uint64_t start, const uint64_t len) {
  return DoLock([&](const uint64_t start,
                    const uint64_t len) { return DoReadLock(start, len); },
                start, len);
}

Status LockFileBase::TryReadLock(const uint64_t start, const uint64_t len) {
  return DoLock([&](const uint64_t start,
                    const uint64_t len) { return DoTryReadLock(start, len); },
                start, len);
}

Status LockFileBase::Unlock() {
  if (!IsLocked())
    return NotLocked();

  const auto error = DoUnlock();
  if (error.Success()) {
    m_locked = false;
    m_start = 0;
    m_len = 0;
  }
  return error;
}

bool LockFileBase::IsValidFile() const { return m_fd != -1; }

Status LockFileBase::DoLock(const Locker &locker, const uint64_t start,
                            const uint64_t len) {
  if (!IsValidFile())
    return Status("File is invalid");

  if (IsLocked())
    return AlreadyLocked();

  const auto error = locker(start, len);
  if (error.Success()) {
    m_locked = true;
    m_start = start;
    m_len = len;
  }

  return error;
}
