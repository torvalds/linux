//===-- TCPSocket.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(_MSC_VER)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "lldb/Host/common/TCPSocket.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/MainLoop.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/WindowsError.h"
#include "llvm/Support/raw_ostream.h"

#if LLDB_ENABLE_POSIX
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#endif

#ifdef _WIN32
#define CLOSE_SOCKET closesocket
typedef const char *set_socket_option_arg_type;
#else
#include <unistd.h>
#define CLOSE_SOCKET ::close
typedef const void *set_socket_option_arg_type;
#endif

using namespace lldb;
using namespace lldb_private;

static Status GetLastSocketError() {
  std::error_code EC;
#ifdef _WIN32
  EC = llvm::mapWindowsError(WSAGetLastError());
#else
  EC = std::error_code(errno, std::generic_category());
#endif
  return EC;
}

static const int kType = SOCK_STREAM;

TCPSocket::TCPSocket(bool should_close, bool child_processes_inherit)
    : Socket(ProtocolTcp, should_close, child_processes_inherit) {}

TCPSocket::TCPSocket(NativeSocket socket, const TCPSocket &listen_socket)
    : Socket(ProtocolTcp, listen_socket.m_should_close_fd,
             listen_socket.m_child_processes_inherit) {
  m_socket = socket;
}

TCPSocket::TCPSocket(NativeSocket socket, bool should_close,
                     bool child_processes_inherit)
    : Socket(ProtocolTcp, should_close, child_processes_inherit) {
  m_socket = socket;
}

TCPSocket::~TCPSocket() { CloseListenSockets(); }

bool TCPSocket::IsValid() const {
  return m_socket != kInvalidSocketValue || m_listen_sockets.size() != 0;
}

// Return the port number that is being used by the socket.
uint16_t TCPSocket::GetLocalPortNumber() const {
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetPort();
  } else if (!m_listen_sockets.empty()) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_listen_sockets.begin()->first, sock_addr,
                      &sock_addr_len) == 0)
      return sock_addr.GetPort();
  }
  return 0;
}

std::string TCPSocket::GetLocalIPAddress() const {
  // We bound to port zero, so we need to figure out which port we actually
  // bound to
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetIPAddress();
  }
  return "";
}

uint16_t TCPSocket::GetRemotePortNumber() const {
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getpeername(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetPort();
  }
  return 0;
}

std::string TCPSocket::GetRemoteIPAddress() const {
  // We bound to port zero, so we need to figure out which port we actually
  // bound to
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getpeername(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetIPAddress();
  }
  return "";
}

std::string TCPSocket::GetRemoteConnectionURI() const {
  if (m_socket != kInvalidSocketValue) {
    return std::string(llvm::formatv(
        "connect://[{0}]:{1}", GetRemoteIPAddress(), GetRemotePortNumber()));
  }
  return "";
}

Status TCPSocket::CreateSocket(int domain) {
  Status error;
  if (IsValid())
    error = Close();
  if (error.Fail())
    return error;
  m_socket = Socket::CreateSocket(domain, kType, IPPROTO_TCP,
                                  m_child_processes_inherit, error);
  return error;
}

Status TCPSocket::Connect(llvm::StringRef name) {

  Log *log = GetLog(LLDBLog::Communication);
  LLDB_LOG(log, "Connect to host/port {0}", name);

  Status error;
  llvm::Expected<HostAndPort> host_port = DecodeHostAndPort(name);
  if (!host_port)
    return Status(host_port.takeError());

  std::vector<SocketAddress> addresses =
      SocketAddress::GetAddressInfo(host_port->hostname.c_str(), nullptr,
                                    AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
  for (SocketAddress &address : addresses) {
    error = CreateSocket(address.GetFamily());
    if (error.Fail())
      continue;

    address.SetPort(host_port->port);

    if (llvm::sys::RetryAfterSignal(-1, ::connect, GetNativeSocket(),
                                    &address.sockaddr(),
                                    address.GetLength()) == -1) {
      Close();
      continue;
    }

    if (SetOptionNoDelay() == -1) {
      Close();
      continue;
    }

    error.Clear();
    return error;
  }

  error.SetErrorString("Failed to connect port");
  return error;
}

Status TCPSocket::Listen(llvm::StringRef name, int backlog) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "Listen to {0}", name);

  Status error;
  llvm::Expected<HostAndPort> host_port = DecodeHostAndPort(name);
  if (!host_port)
    return Status(host_port.takeError());

  if (host_port->hostname == "*")
    host_port->hostname = "0.0.0.0";
  std::vector<SocketAddress> addresses = SocketAddress::GetAddressInfo(
      host_port->hostname.c_str(), nullptr, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
  for (SocketAddress &address : addresses) {
    int fd = Socket::CreateSocket(address.GetFamily(), kType, IPPROTO_TCP,
                                  m_child_processes_inherit, error);
    if (error.Fail() || fd < 0)
      continue;

    // enable local address reuse
    int option_value = 1;
    set_socket_option_arg_type option_value_p =
        reinterpret_cast<set_socket_option_arg_type>(&option_value);
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, option_value_p,
                     sizeof(option_value)) == -1) {
      CLOSE_SOCKET(fd);
      continue;
    }

    SocketAddress listen_address = address;
    if(!listen_address.IsLocalhost())
      listen_address.SetToAnyAddress(address.GetFamily(), host_port->port);
    else
      listen_address.SetPort(host_port->port);

    int err =
        ::bind(fd, &listen_address.sockaddr(), listen_address.GetLength());
    if (err != -1)
      err = ::listen(fd, backlog);

    if (err == -1) {
      error = GetLastSocketError();
      CLOSE_SOCKET(fd);
      continue;
    }

    if (host_port->port == 0) {
      socklen_t sa_len = address.GetLength();
      if (getsockname(fd, &address.sockaddr(), &sa_len) == 0)
        host_port->port = address.GetPort();
    }
    m_listen_sockets[fd] = address;
  }

  if (m_listen_sockets.empty()) {
    assert(error.Fail());
    return error;
  }
  return Status();
}

void TCPSocket::CloseListenSockets() {
  for (auto socket : m_listen_sockets)
    CLOSE_SOCKET(socket.first);
  m_listen_sockets.clear();
}

Status TCPSocket::Accept(Socket *&conn_socket) {
  Status error;
  if (m_listen_sockets.size() == 0) {
    error.SetErrorString("No open listening sockets!");
    return error;
  }

  NativeSocket sock = kInvalidSocketValue;
  NativeSocket listen_sock = kInvalidSocketValue;
  lldb_private::SocketAddress AcceptAddr;
  MainLoop accept_loop;
  std::vector<MainLoopBase::ReadHandleUP> handles;
  for (auto socket : m_listen_sockets) {
    auto fd = socket.first;
    auto inherit = this->m_child_processes_inherit;
    auto io_sp = IOObjectSP(new TCPSocket(socket.first, false, inherit));
    handles.emplace_back(accept_loop.RegisterReadObject(
        io_sp, [fd, inherit, &sock, &AcceptAddr, &error,
                        &listen_sock](MainLoopBase &loop) {
          socklen_t sa_len = AcceptAddr.GetMaxLength();
          sock = AcceptSocket(fd, &AcceptAddr.sockaddr(), &sa_len, inherit,
                              error);
          listen_sock = fd;
          loop.RequestTermination();
        }, error));
    if (error.Fail())
      return error;
  }

  bool accept_connection = false;
  std::unique_ptr<TCPSocket> accepted_socket;
  // Loop until we are happy with our connection
  while (!accept_connection) {
    accept_loop.Run();

    if (error.Fail())
        return error;

    lldb_private::SocketAddress &AddrIn = m_listen_sockets[listen_sock];
    if (!AddrIn.IsAnyAddr() && AcceptAddr != AddrIn) {
      if (sock != kInvalidSocketValue) {
        CLOSE_SOCKET(sock);
        sock = kInvalidSocketValue;
      }
      llvm::errs() << llvm::formatv(
          "error: rejecting incoming connection from {0} (expecting {1})",
          AcceptAddr.GetIPAddress(), AddrIn.GetIPAddress());
      continue;
    }
    accept_connection = true;
    accepted_socket.reset(new TCPSocket(sock, *this));
  }

  if (!accepted_socket)
    return error;

  // Keep our TCP packets coming without any delays.
  accepted_socket->SetOptionNoDelay();
  error.Clear();
  conn_socket = accepted_socket.release();
  return error;
}

int TCPSocket::SetOptionNoDelay() {
  return SetOption(IPPROTO_TCP, TCP_NODELAY, 1);
}

int TCPSocket::SetOptionReuseAddress() {
  return SetOption(SOL_SOCKET, SO_REUSEADDR, 1);
}
