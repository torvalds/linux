//===-- SBCommunication.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBCommunication.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/Core/ThreadedCommunication.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBCommunication::SBCommunication() { LLDB_INSTRUMENT_VA(this); }

SBCommunication::SBCommunication(const char *broadcaster_name)
    : m_opaque(new ThreadedCommunication(broadcaster_name)),
      m_opaque_owned(true) {
  LLDB_INSTRUMENT_VA(this, broadcaster_name);
}

SBCommunication::~SBCommunication() {
  if (m_opaque && m_opaque_owned)
    delete m_opaque;
  m_opaque = nullptr;
  m_opaque_owned = false;
}

bool SBCommunication::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBCommunication::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque != nullptr;
}

bool SBCommunication::GetCloseOnEOF() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque)
    return m_opaque->GetCloseOnEOF();
  return false;
}

void SBCommunication::SetCloseOnEOF(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  if (m_opaque)
    m_opaque->SetCloseOnEOF(b);
}

ConnectionStatus SBCommunication::Connect(const char *url) {
  LLDB_INSTRUMENT_VA(this, url);

  if (m_opaque) {
    if (!m_opaque->HasConnection())
      m_opaque->SetConnection(Host::CreateDefaultConnection(url));
    return m_opaque->Connect(url, nullptr);
  }
  return eConnectionStatusNoConnection;
}

ConnectionStatus SBCommunication::AdoptFileDesriptor(int fd, bool owns_fd) {
  LLDB_INSTRUMENT_VA(this, fd, owns_fd);

  ConnectionStatus status = eConnectionStatusNoConnection;
  if (m_opaque) {
    if (m_opaque->HasConnection()) {
      if (m_opaque->IsConnected())
        m_opaque->Disconnect();
    }
    m_opaque->SetConnection(
        std::make_unique<ConnectionFileDescriptor>(fd, owns_fd));
    if (m_opaque->IsConnected())
      status = eConnectionStatusSuccess;
    else
      status = eConnectionStatusLostConnection;
  }
  return status;
}

ConnectionStatus SBCommunication::Disconnect() {
  LLDB_INSTRUMENT_VA(this);

  ConnectionStatus status = eConnectionStatusNoConnection;
  if (m_opaque)
    status = m_opaque->Disconnect();
  return status;
}

bool SBCommunication::IsConnected() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque ? m_opaque->IsConnected() : false;
}

size_t SBCommunication::Read(void *dst, size_t dst_len, uint32_t timeout_usec,
                             ConnectionStatus &status) {
  LLDB_INSTRUMENT_VA(this, dst, dst_len, timeout_usec, status);

  size_t bytes_read = 0;
  Timeout<std::micro> timeout = timeout_usec == UINT32_MAX
                                    ? Timeout<std::micro>(std::nullopt)
                                    : std::chrono::microseconds(timeout_usec);
  if (m_opaque)
    bytes_read = m_opaque->Read(dst, dst_len, timeout, status, nullptr);
  else
    status = eConnectionStatusNoConnection;

  return bytes_read;
}

size_t SBCommunication::Write(const void *src, size_t src_len,
                              ConnectionStatus &status) {
  LLDB_INSTRUMENT_VA(this, src, src_len, status);

  size_t bytes_written = 0;
  if (m_opaque)
    bytes_written = m_opaque->Write(src, src_len, status, nullptr);
  else
    status = eConnectionStatusNoConnection;

  return bytes_written;
}

bool SBCommunication::ReadThreadStart() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque ? m_opaque->StartReadThread() : false;
}

bool SBCommunication::ReadThreadStop() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque ? m_opaque->StopReadThread() : false;
}

bool SBCommunication::ReadThreadIsRunning() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque ? m_opaque->ReadThreadIsRunning() : false;
}

bool SBCommunication::SetReadThreadBytesReceivedCallback(
    ReadThreadBytesReceived callback, void *callback_baton) {
  LLDB_INSTRUMENT_VA(this, callback, callback_baton);

  bool result = false;
  if (m_opaque) {
    m_opaque->SetReadThreadBytesReceivedCallback(callback, callback_baton);
    result = true;
  }
  return result;
}

SBBroadcaster SBCommunication::GetBroadcaster() {
  LLDB_INSTRUMENT_VA(this);

  SBBroadcaster broadcaster(m_opaque, false);
  return broadcaster;
}

const char *SBCommunication::GetBroadcasterClass() {
  LLDB_INSTRUMENT();

  return ConstString(ThreadedCommunication::GetStaticBroadcasterClass())
      .AsCString();
}
