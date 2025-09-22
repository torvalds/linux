//===-- Communication.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Communication.h"

#include "lldb/Utility/Connection.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/Compiler.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <cerrno>
#include <cinttypes>
#include <cstdio>

using namespace lldb;
using namespace lldb_private;

Communication::Communication()
    : m_connection_sp(), m_write_mutex(), m_close_on_eof(true) {
}

Communication::~Communication() {
  Clear();
}

void Communication::Clear() {
  Disconnect(nullptr);
}

ConnectionStatus Communication::Connect(const char *url, Status *error_ptr) {
  Clear();

  LLDB_LOG(GetLog(LLDBLog::Communication),
           "{0} Communication::Connect (url = {1})", this, url);

  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp)
    return connection_sp->Connect(url, error_ptr);
  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  return eConnectionStatusNoConnection;
}

ConnectionStatus Communication::Disconnect(Status *error_ptr) {
  LLDB_LOG(GetLog(LLDBLog::Communication), "{0} Communication::Disconnect ()",
           this);

  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp) {
    ConnectionStatus status = connection_sp->Disconnect(error_ptr);
    // We currently don't protect connection_sp with any mutex for multi-
    // threaded environments. So lets not nuke our connection class without
    // putting some multi-threaded protections in. We also probably don't want
    // to pay for the overhead it might cause if every time we access the
    // connection we have to take a lock.
    //
    // This unique pointer will cleanup after itself when this object goes
    // away, so there is no need to currently have it destroy itself
    // immediately upon disconnect.
    // connection_sp.reset();
    return status;
  }
  return eConnectionStatusNoConnection;
}

bool Communication::IsConnected() const {
  lldb::ConnectionSP connection_sp(m_connection_sp);
  return (connection_sp ? connection_sp->IsConnected() : false);
}

bool Communication::HasConnection() const {
  return m_connection_sp.get() != nullptr;
}

size_t Communication::Read(void *dst, size_t dst_len,
                           const Timeout<std::micro> &timeout,
                           ConnectionStatus &status, Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Communication);
  LLDB_LOG(
      log,
      "this = {0}, dst = {1}, dst_len = {2}, timeout = {3}, connection = {4}",
      this, dst, dst_len, timeout, m_connection_sp.get());

  return ReadFromConnection(dst, dst_len, timeout, status, error_ptr);
}

size_t Communication::Write(const void *src, size_t src_len,
                            ConnectionStatus &status, Status *error_ptr) {
  lldb::ConnectionSP connection_sp(m_connection_sp);

  std::lock_guard<std::mutex> guard(m_write_mutex);
  LLDB_LOG(GetLog(LLDBLog::Communication),
           "{0} Communication::Write (src = {1}, src_len = {2}"
           ") connection = {3}",
           this, src, (uint64_t)src_len, connection_sp.get());

  if (connection_sp)
    return connection_sp->Write(src, src_len, status, error_ptr);

  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  status = eConnectionStatusNoConnection;
  return 0;
}

size_t Communication::WriteAll(const void *src, size_t src_len,
                               ConnectionStatus &status, Status *error_ptr) {
  size_t total_written = 0;
  do
    total_written += Write(static_cast<const char *>(src) + total_written,
                           src_len - total_written, status, error_ptr);
  while (status == eConnectionStatusSuccess && total_written < src_len);
  return total_written;
}

size_t Communication::ReadFromConnection(void *dst, size_t dst_len,
                                         const Timeout<std::micro> &timeout,
                                         ConnectionStatus &status,
                                         Status *error_ptr) {
  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp)
    return connection_sp->Read(dst, dst_len, timeout, status, error_ptr);

  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  status = eConnectionStatusNoConnection;
  return 0;
}

void Communication::SetConnection(std::unique_ptr<Connection> connection) {
  Disconnect(nullptr);
  m_connection_sp = std::move(connection);
}

std::string
Communication::ConnectionStatusAsString(lldb::ConnectionStatus status) {
  switch (status) {
  case eConnectionStatusSuccess:
    return "success";
  case eConnectionStatusError:
    return "error";
  case eConnectionStatusTimedOut:
    return "timed out";
  case eConnectionStatusNoConnection:
    return "no connection";
  case eConnectionStatusLostConnection:
    return "lost connection";
  case eConnectionStatusEndOfFile:
    return "end of file";
  case eConnectionStatusInterrupted:
    return "interrupted";
  }

  return "@" + std::to_string(status);
}
