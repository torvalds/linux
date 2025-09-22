//===-- LockFileWindows.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_posix_LockFileWindows_h_
#define liblldb_Host_posix_LockFileWindows_h_

#include "lldb/Host/LockFileBase.h"
#include "lldb/Host/windows/windows.h"

namespace lldb_private {

class LockFileWindows : public LockFileBase {
public:
  explicit LockFileWindows(int fd);
  ~LockFileWindows();

protected:
  Status DoWriteLock(const uint64_t start, const uint64_t len) override;

  Status DoTryWriteLock(const uint64_t start, const uint64_t len) override;

  Status DoReadLock(const uint64_t start, const uint64_t len) override;

  Status DoTryReadLock(const uint64_t start, const uint64_t len) override;

  Status DoUnlock() override;

  bool IsValidFile() const override;

private:
  HANDLE m_file;
};

} // namespace lldb_private

#endif // liblldb_Host_posix_LockFileWindows_h_
