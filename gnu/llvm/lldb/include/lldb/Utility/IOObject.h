//===-- IOObject.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_IOOBJECT_H
#define LLDB_UTILITY_IOOBJECT_H

#include <cstdarg>
#include <cstdio>
#include <sys/types.h>

#include "lldb/lldb-private.h"

namespace lldb_private {

class IOObject {
public:
  enum FDType {
    eFDTypeFile,   // Other FD requiring read/write
    eFDTypeSocket, // Socket requiring send/recv
  };

  // TODO: On Windows this should be a HANDLE, and wait should use
  // WaitForMultipleObjects
  typedef int WaitableHandle;
  static const WaitableHandle kInvalidHandleValue;

  IOObject(FDType type) : m_fd_type(type) {}
  virtual ~IOObject();

  virtual Status Read(void *buf, size_t &num_bytes) = 0;
  virtual Status Write(const void *buf, size_t &num_bytes) = 0;
  virtual bool IsValid() const = 0;
  virtual Status Close() = 0;

  FDType GetFdType() const { return m_fd_type; }

  virtual WaitableHandle GetWaitableHandle() = 0;

protected:
  FDType m_fd_type;

private:
  IOObject(const IOObject &) = delete;
  const IOObject &operator=(const IOObject &) = delete;
};
} // namespace lldb_private

#endif
