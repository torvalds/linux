//===-- PipeWindows.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_windows_PipeWindows_h_
#define liblldb_Host_windows_PipeWindows_h_

#include "lldb/Host/PipeBase.h"
#include "lldb/Host/windows/windows.h"

namespace lldb_private {

/// \class Pipe PipeWindows.h "lldb/Host/windows/PipeWindows.h"
/// A windows-based implementation of Pipe, a class that abtracts
///        unix style pipes.
///
/// A class that abstracts the LLDB core from host pipe functionality.
class PipeWindows : public PipeBase {
public:
  static const int kInvalidDescriptor = -1;

public:
  PipeWindows();
  PipeWindows(lldb::pipe_t read, lldb::pipe_t write);
  ~PipeWindows() override;

  // Create an unnamed pipe.
  Status CreateNew(bool child_process_inherit) override;

  // Create a named pipe.
  Status CreateNewNamed(bool child_process_inherit);
  Status CreateNew(llvm::StringRef name, bool child_process_inherit) override;
  Status CreateWithUniqueName(llvm::StringRef prefix,
                              bool child_process_inherit,
                              llvm::SmallVectorImpl<char> &name) override;
  Status OpenAsReader(llvm::StringRef name,
                      bool child_process_inherit) override;
  Status
  OpenAsWriterWithTimeout(llvm::StringRef name, bool child_process_inherit,
                          const std::chrono::microseconds &timeout) override;

  bool CanRead() const override;
  bool CanWrite() const override;

  lldb::pipe_t GetReadPipe() const override { return lldb::pipe_t(m_read); }
  lldb::pipe_t GetWritePipe() const override { return lldb::pipe_t(m_write); }

  int GetReadFileDescriptor() const override;
  int GetWriteFileDescriptor() const override;
  int ReleaseReadFileDescriptor() override;
  int ReleaseWriteFileDescriptor() override;
  void CloseReadFileDescriptor() override;
  void CloseWriteFileDescriptor() override;

  void Close() override;

  Status Delete(llvm::StringRef name) override;

  Status Write(const void *buf, size_t size, size_t &bytes_written) override;
  Status ReadWithTimeout(void *buf, size_t size,
                         const std::chrono::microseconds &timeout,
                         size_t &bytes_read) override;

  // PipeWindows specific methods.  These allow access to the underlying OS
  // handle.
  HANDLE GetReadNativeHandle();
  HANDLE GetWriteNativeHandle();

private:
  Status OpenNamedPipe(llvm::StringRef name, bool child_process_inherit,
                       bool is_read);

  HANDLE m_read;
  HANDLE m_write;

  int m_read_fd;
  int m_write_fd;

  OVERLAPPED m_read_overlapped;
  OVERLAPPED m_write_overlapped;
};

} // namespace lldb_private

#endif // liblldb_Host_posix_PipePosix_h_
