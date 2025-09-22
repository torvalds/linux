//===-- LockFilePosix.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/LockFilePosix.h"

#include "llvm/Support/Errno.h"

#include <fcntl.h>
#include <unistd.h>

using namespace lldb;
using namespace lldb_private;

static Status fileLock(int fd, int cmd, int lock_type, const uint64_t start,
                       const uint64_t len) {
  struct flock fl;

  fl.l_type = lock_type;
  fl.l_whence = SEEK_SET;
  fl.l_start = start;
  fl.l_len = len;
  fl.l_pid = ::getpid();

  Status error;
  if (llvm::sys::RetryAfterSignal(-1, ::fcntl, fd, cmd, &fl) == -1)
    error.SetErrorToErrno();

  return error;
}

LockFilePosix::LockFilePosix(int fd) : LockFileBase(fd) {}

LockFilePosix::~LockFilePosix() { Unlock(); }

Status LockFilePosix::DoWriteLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLKW, F_WRLCK, start, len);
}

Status LockFilePosix::DoTryWriteLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLK, F_WRLCK, start, len);
}

Status LockFilePosix::DoReadLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLKW, F_RDLCK, start, len);
}

Status LockFilePosix::DoTryReadLock(const uint64_t start, const uint64_t len) {
  return fileLock(m_fd, F_SETLK, F_RDLCK, start, len);
}

Status LockFilePosix::DoUnlock() {
  return fileLock(m_fd, F_SETLK, F_UNLCK, m_start, m_len);
}
