//===-- ConnectionGenericFileWindows.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_windows_ConnectionGenericFileWindows_h_
#define liblldb_Host_windows_ConnectionGenericFileWindows_h_

#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/Connection.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class Status;

class ConnectionGenericFile : public lldb_private::Connection {
public:
  ConnectionGenericFile();

  ConnectionGenericFile(lldb::file_t file, bool owns_file);

  ~ConnectionGenericFile() override;

  bool IsConnected() const override;

  lldb::ConnectionStatus Connect(llvm::StringRef s, Status *error_ptr) override;

  lldb::ConnectionStatus Disconnect(Status *error_ptr) override;

  size_t Read(void *dst, size_t dst_len, const Timeout<std::micro> &timeout,
              lldb::ConnectionStatus &status, Status *error_ptr) override;

  size_t Write(const void *src, size_t src_len, lldb::ConnectionStatus &status,
               Status *error_ptr) override;

  std::string GetURI() override;

  bool InterruptRead() override;

protected:
  OVERLAPPED m_overlapped;
  HANDLE m_file;
  HANDLE m_event_handles[2];
  bool m_owns_file;
  LARGE_INTEGER m_file_position;

  enum { kBytesAvailableEvent, kInterruptEvent };

private:
  void InitializeEventHandles();
  void IncrementFilePointer(DWORD amount);

  std::string m_uri;

  ConnectionGenericFile(const ConnectionGenericFile &) = delete;
  const ConnectionGenericFile &
  operator=(const ConnectionGenericFile &) = delete;
};
}

#endif
