//===-- RNBSocket.cpp -------------------------------------------*- C++ -*-===//
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

#include "RNBSocket.h"
#include "DNBError.h"
#include "DNBLog.h"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/event.h>
#include <termios.h>
#include <vector>

#include "lldb/Host/SocketAddress.h"

#ifdef WITH_LOCKDOWN
#include "lockdown.h"
#endif

rnb_err_t RNBSocket::Listen(const char *listen_host, uint16_t port,
                            PortBoundCallback callback,
                            const void *callback_baton) {
  // DNBLogThreadedIf(LOG_RNB_COMM, "%8u RNBSocket::%s called",
  // (uint32_t)m_timer.ElapsedMicroSeconds(true), __FUNCTION__);
  // Disconnect without saving errno
  Disconnect(false);

  DNBError err;
  int queue_id = kqueue();
  if (queue_id < 0) {
    err.SetError(errno, DNBError::MachKernel);
    err.LogThreaded("error: failed to create kqueue.");
    return rnb_err;
  }

  bool any_addr = (strcmp(listen_host, "*") == 0);

  // If the user wants to allow connections from any address we should create
  // sockets on all families that can resolve localhost. This will allow us to
  // listen for IPv6 and IPv4 connections from all addresses if those interfaces
  // are available.
  const char *local_addr = any_addr ? "localhost" : listen_host;

  std::map<int, lldb_private::SocketAddress> sockets;
  auto addresses = lldb_private::SocketAddress::GetAddressInfo(
      local_addr, NULL, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);

  for (auto address : addresses) {
    int sock_fd = ::socket(address.GetFamily(), SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd == -1)
      continue;

    SetSocketOption(sock_fd, SOL_SOCKET, SO_REUSEADDR, 1);

    lldb_private::SocketAddress bind_address = address;

    if(any_addr || !bind_address.IsLocalhost())
      bind_address.SetToAnyAddress(bind_address.GetFamily(), port);
    else
      bind_address.SetPort(port);

    int error =
        ::bind(sock_fd, &bind_address.sockaddr(), bind_address.GetLength());
    if (error == -1) {
      ClosePort(sock_fd, false);
      continue;
    }

    error = ::listen(sock_fd, 5);
    if (error == -1) {
      ClosePort(sock_fd, false);
      continue;
    }

    // We were asked to listen on port zero which means we must now read the
    // actual port that was given to us as port zero is a special code for "find
    // an open port for me". This will only execute on the first socket created,
    // subesquent sockets will reuse this port number.
    if (port == 0) {
      socklen_t sa_len = address.GetLength();
      if (getsockname(sock_fd, &address.sockaddr(), &sa_len) == 0)
        port = address.GetPort();
    }

    sockets[sock_fd] = address;
  }

  if (sockets.size() == 0) {
    err.SetError(errno, DNBError::POSIX);
    err.LogThreaded("::listen or ::bind failed");
    return rnb_err;
  }

  if (callback)
    callback(callback_baton, port);

  std::vector<struct kevent> events;
  events.resize(sockets.size());
  int i = 0;
  for (auto socket : sockets) {
    EV_SET(&events[i++], socket.first, EVFILT_READ, EV_ADD, 0, 0, 0);
  }

  bool accept_connection = false;

  // Loop until we are happy with our connection
  while (!accept_connection) {

    struct kevent event_list[4];
    int num_events;
    do {
      errno = 0;
      num_events =
          kevent(queue_id, events.data(), events.size(), event_list, 4, NULL);
    } while (num_events == -1 &&
             (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));

    if (num_events < 0) {
      err.SetError(errno, DNBError::MachKernel);
      err.LogThreaded("error: kevent() failed.");
    }

    for (int i = 0; i < num_events; ++i) {
      auto sock_fd = event_list[i].ident;
      auto socket_pair = sockets.find(sock_fd);
      if (socket_pair == sockets.end())
        continue;

      lldb_private::SocketAddress &addr_in = socket_pair->second;
      lldb_private::SocketAddress accept_addr;
      socklen_t sa_len = accept_addr.GetMaxLength();
      m_fd = ::accept(sock_fd, &accept_addr.sockaddr(), &sa_len);

      if (m_fd == -1) {
        err.SetError(errno, DNBError::POSIX);
        err.LogThreaded("error: Socket accept failed.");
      }

      if (addr_in.IsAnyAddr())
        accept_connection = true;
      else {
        if (accept_addr == addr_in)
          accept_connection = true;
        else {
          ::close(m_fd);
          m_fd = -1;
          ::fprintf(
              stderr,
              "error: rejecting incoming connection from %s (expecting %s)\n",
              accept_addr.GetIPAddress().c_str(),
              addr_in.GetIPAddress().c_str());
          DNBLogThreaded("error: rejecting connection from %s (expecting %s)\n",
                         accept_addr.GetIPAddress().c_str(),
                         addr_in.GetIPAddress().c_str());
          err.Clear();
        }
      }
    }
    if (err.Fail())
      break;
  }
  for (auto socket : sockets) {
    int ListenFd = socket.first;
    ClosePort(ListenFd, false);
  }

  if (err.Fail())
    return rnb_err;

  // Keep our TCP packets coming without any delays.
  SetSocketOption(m_fd, IPPROTO_TCP, TCP_NODELAY, 1);

  return rnb_success;
}

rnb_err_t RNBSocket::Connect(const char *host, uint16_t port) {
  auto result = rnb_err;
  Disconnect(false);

  auto addresses = lldb_private::SocketAddress::GetAddressInfo(
      host, NULL, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);

  for (auto address : addresses) {
    m_fd = ::socket(address.GetFamily(), SOCK_STREAM, IPPROTO_TCP);
    if (m_fd == -1)
      continue;

    // Enable local address reuse
    SetSocketOption(m_fd, SOL_SOCKET, SO_REUSEADDR, 1);

    address.SetPort(port);

    if (-1 == ::connect(m_fd, &address.sockaddr(), address.GetLength())) {
      Disconnect(false);
      continue;
    }
    SetSocketOption(m_fd, IPPROTO_TCP, TCP_NODELAY, 1);

    result = rnb_success;
    break;
  }
  return result;
}

rnb_err_t RNBSocket::useFD(int fd) {
  if (fd < 0) {
    DNBLogThreadedIf(LOG_RNB_COMM, "Bad file descriptor passed in.");
    return rnb_err;
  }

  m_fd = fd;
  return rnb_success;
}

#ifdef WITH_LOCKDOWN
rnb_err_t RNBSocket::ConnectToService() {
  DNBLog("Connecting to com.apple.%s service...", DEBUGSERVER_PROGRAM_NAME);
  // Disconnect from any previous connections
  Disconnect(false);
  if (::secure_lockdown_checkin(&m_ld_conn, NULL, NULL) != kLDESuccess) {
    DNBLogThreadedIf(LOG_RNB_COMM,
                     "::secure_lockdown_checkin(&m_fd, NULL, NULL) failed");
    m_fd = -1;
    return rnb_not_connected;
  }
  m_fd = ::lockdown_get_socket(m_ld_conn);
  if (m_fd == -1) {
    DNBLogThreadedIf(LOG_RNB_COMM, "::lockdown_get_socket() failed");
    return rnb_not_connected;
  }
  m_fd_from_lockdown = true;
  return rnb_success;
}
#endif

rnb_err_t RNBSocket::OpenFile(const char *path) {
  DNBError err;
  m_fd = open(path, O_RDWR);
  if (m_fd == -1) {
    err.SetError(errno, DNBError::POSIX);
    err.LogThreaded("can't open file '%s'", path);
    return rnb_not_connected;
  } else {
    struct termios stdin_termios;

    if (::tcgetattr(m_fd, &stdin_termios) == 0) {
      stdin_termios.c_lflag &= ~ECHO;   // Turn off echoing
      stdin_termios.c_lflag &= ~ICANON; // Get one char at a time
      ::tcsetattr(m_fd, TCSANOW, &stdin_termios);
    }
  }
  return rnb_success;
}

int RNBSocket::SetSocketOption(int fd, int level, int option_name,
                               int option_value) {
  return ::setsockopt(fd, level, option_name, &option_value,
                      sizeof(option_value));
}

rnb_err_t RNBSocket::Disconnect(bool save_errno) {
#ifdef WITH_LOCKDOWN
  if (m_fd_from_lockdown) {
    m_fd_from_lockdown = false;
    m_fd = -1;
    lockdown_disconnect(m_ld_conn);
    return rnb_success;
  }
#endif
  return ClosePort(m_fd, save_errno);
}

rnb_err_t RNBSocket::Read(std::string &p) {
  char buf[1024];
  p.clear();

  // Note that BUF is on the stack so we must be careful to keep any
  // writes to BUF from overflowing or we'll have security issues.

  if (m_fd == -1)
    return rnb_err;

  // DNBLogThreadedIf(LOG_RNB_COMM, "%8u RNBSocket::%s calling read()",
  // (uint32_t)m_timer.ElapsedMicroSeconds(true), __FUNCTION__);
  DNBError err;
  ssize_t bytesread;
  do {
    errno = 0;
    bytesread = read(m_fd, buf, sizeof(buf));
  } while (bytesread == -1 &&
           (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
  if (bytesread <= 0)
    err.SetError(errno, DNBError::POSIX);
  else
    p.append(buf, bytesread);

  if (err.Fail() || DNBLogCheckLogBit(LOG_RNB_COMM))
    err.LogThreaded("::read ( %i, %p, %llu ) => %i", m_fd, buf, sizeof(buf),
                    (uint64_t)bytesread);

  // Our port went away - we have to mark this so IsConnected will return the
  // truth.
  if (bytesread == 0) {
    m_fd = -1;
    return rnb_not_connected;
  } else if (bytesread == -1) {
    m_fd = -1;
    return rnb_err;
  }
  // Strip spaces from the end of the buffer
  while (!p.empty() && isspace(p[p.size() - 1]))
    p.erase(p.size() - 1);

  // Most data in the debugserver packets valid printable characters...
  DNBLogThreadedIf(LOG_RNB_COMM, "read: %s", p.c_str());
  return rnb_success;
}

rnb_err_t RNBSocket::Write(const void *buffer, size_t length) {
  if (m_fd == -1)
    return rnb_err;

  DNBError err;
  ssize_t bytessent = write(m_fd, buffer, length);
  if (bytessent < 0)
    err.SetError(errno, DNBError::POSIX);

  if (err.Fail() || DNBLogCheckLogBit(LOG_RNB_COMM))
    err.LogThreaded("::write ( socket = %i, buffer = %p, length = %llu) => %i",
                    m_fd, buffer, length, (uint64_t)bytessent);

  if (bytessent < 0)
    return rnb_err;

  if ((size_t)bytessent != length)
    return rnb_err;

  DNBLogThreadedIf(
      LOG_RNB_PACKETS, "putpkt: %*s", (int)length,
      (const char *)
          buffer); // All data is string based in debugserver, so this is safe
  DNBLogThreadedIf(LOG_RNB_COMM, "sent: %*s", (int)length,
                   (const char *)buffer);

  return rnb_success;
}

rnb_err_t RNBSocket::ClosePort(int &fd, bool save_errno) {
  int close_err = 0;
  if (fd > 0) {
    errno = 0;
    close_err = close(fd);
    fd = -1;
  }
  return close_err != 0 ? rnb_err : rnb_success;
}
