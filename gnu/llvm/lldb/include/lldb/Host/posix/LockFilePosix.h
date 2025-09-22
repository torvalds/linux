//===-- LockFilePosix.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_LOCKFILEPOSIX_H
#define LLDB_HOST_POSIX_LOCKFILEPOSIX_H

#include "lldb/Host/LockFileBase.h"

namespace lldb_private {

class LockFilePosix : public LockFileBase {
public:
  explicit LockFilePosix(int fd);
  ~LockFilePosix() override;

protected:
  Status DoWriteLock(const uint64_t start, const uint64_t len) override;

  Status DoTryWriteLock(const uint64_t start, const uint64_t len) override;

  Status DoReadLock(const uint64_t start, const uint64_t len) override;

  Status DoTryReadLock(const uint64_t start, const uint64_t len) override;

  Status DoUnlock() override;
};

} // namespace lldb_private

#endif // LLDB_HOST_POSIX_LOCKFILEPOSIX_H
