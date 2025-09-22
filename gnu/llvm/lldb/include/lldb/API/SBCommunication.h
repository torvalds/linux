//===-- SBCommunication.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBCOMMUNICATION_H
#define LLDB_API_SBCOMMUNICATION_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"

namespace lldb {

class LLDB_API SBCommunication {
public:
  FLAGS_ANONYMOUS_ENUM(){
      eBroadcastBitDisconnected =
          (1 << 0), ///< Sent when the communications connection is lost.
      eBroadcastBitReadThreadGotBytes =
          (1 << 1), ///< Sent by the read thread when bytes become available.
      eBroadcastBitReadThreadDidExit =
          (1
           << 2), ///< Sent by the read thread when it exits to inform clients.
      eBroadcastBitReadThreadShouldExit =
          (1 << 3), ///< Sent by clients that need to cancel the read thread.
      eBroadcastBitPacketAvailable =
          (1 << 4), ///< Sent when data received makes a complete packet.
      eAllEventBits = 0xffffffff};

  typedef void (*ReadThreadBytesReceived)(void *baton, const void *src,
                                          size_t src_len);

  SBCommunication();
  SBCommunication(const char *broadcaster_name);
  ~SBCommunication();

  explicit operator bool() const;

  bool IsValid() const;

  lldb::SBBroadcaster GetBroadcaster();

  static const char *GetBroadcasterClass();

  lldb::ConnectionStatus AdoptFileDesriptor(int fd, bool owns_fd);

  lldb::ConnectionStatus Connect(const char *url);

  lldb::ConnectionStatus Disconnect();

  bool IsConnected() const;

  bool GetCloseOnEOF();

  void SetCloseOnEOF(bool b);

  size_t Read(void *dst, size_t dst_len, uint32_t timeout_usec,
              lldb::ConnectionStatus &status);

  size_t Write(const void *src, size_t src_len, lldb::ConnectionStatus &status);

  bool ReadThreadStart();

  bool ReadThreadStop();

  bool ReadThreadIsRunning();

  bool SetReadThreadBytesReceivedCallback(ReadThreadBytesReceived callback,
                                          void *callback_baton);

private:
  SBCommunication(const SBCommunication &) = delete;
  const SBCommunication &operator=(const SBCommunication &) = delete;

  lldb_private::ThreadedCommunication *m_opaque = nullptr;
  bool m_opaque_owned = false;
};

} // namespace lldb

#endif // LLDB_API_SBCOMMUNICATION_H
