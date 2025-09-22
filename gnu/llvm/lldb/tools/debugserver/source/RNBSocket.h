//===-- RNBSocket.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 12/12/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSOCKET_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSOCKET_H

#include "DNBTimer.h"
#include "RNBDefs.h"
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef WITH_LOCKDOWN
#include "lockdown.h"
#endif

class RNBSocket {
public:
  typedef void (*PortBoundCallback)(const void *baton, uint16_t port);

  RNBSocket()
      : m_fd(-1),
#ifdef WITH_LOCKDOWN
        m_fd_from_lockdown(false), m_ld_conn(),
#endif
        m_timer(true) // Make a thread safe timer
  {
  }
  ~RNBSocket(void) { Disconnect(false); }

  rnb_err_t Listen(const char *listen_host, uint16_t port,
                   PortBoundCallback callback, const void *callback_baton);
  rnb_err_t Connect(const char *host, uint16_t port);

  rnb_err_t useFD(int fd);

#ifdef WITH_LOCKDOWN
  rnb_err_t ConnectToService();
#endif
  rnb_err_t OpenFile(const char *path);
  rnb_err_t Disconnect(bool save_errno);
  rnb_err_t Read(std::string &p);
  rnb_err_t Write(const void *buffer, size_t length);

  bool IsConnected() const { return m_fd != -1; }
  void SaveErrno(int curr_errno);
  DNBTimer &Timer() { return m_timer; }

  static int SetSocketOption(int fd, int level, int option_name,
                             int option_value);

private:
  RNBSocket(const RNBSocket &) = delete;

protected:
  rnb_err_t ClosePort(int &fd, bool save_errno);

  int m_fd; // Socket we use to communicate once conn established

#ifdef WITH_LOCKDOWN
  bool m_fd_from_lockdown;
  lockdown_connection m_ld_conn;
#endif

  DNBTimer m_timer;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSOCKET_H
