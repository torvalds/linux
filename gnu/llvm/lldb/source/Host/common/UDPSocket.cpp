//===-- UDPSocket.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/UDPSocket.h"

#include "lldb/Host/Config.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#if LLDB_ENABLE_POSIX
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <memory>

using namespace lldb;
using namespace lldb_private;

static const int kDomain = AF_INET;
static const int kType = SOCK_DGRAM;

static const char *g_not_supported_error = "Not supported";

UDPSocket::UDPSocket(NativeSocket socket) : Socket(ProtocolUdp, true, true) {
  m_socket = socket;
}

UDPSocket::UDPSocket(bool should_close, bool child_processes_inherit)
    : Socket(ProtocolUdp, should_close, child_processes_inherit) {}

size_t UDPSocket::Send(const void *buf, const size_t num_bytes) {
  return ::sendto(m_socket, static_cast<const char *>(buf), num_bytes, 0,
                  m_sockaddr, m_sockaddr.GetLength());
}

Status UDPSocket::Connect(llvm::StringRef name) {
  return Status("%s", g_not_supported_error);
}

Status UDPSocket::Listen(llvm::StringRef name, int backlog) {
  return Status("%s", g_not_supported_error);
}

Status UDPSocket::Accept(Socket *&socket) {
  return Status("%s", g_not_supported_error);
}

llvm::Expected<std::unique_ptr<UDPSocket>>
UDPSocket::Connect(llvm::StringRef name, bool child_processes_inherit) {
  std::unique_ptr<UDPSocket> socket;

  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "host/port = {0}", name);

  Status error;
  llvm::Expected<HostAndPort> host_port = DecodeHostAndPort(name);
  if (!host_port)
    return host_port.takeError();

  // At this point we have setup the receive port, now we need to setup the UDP
  // send socket

  struct addrinfo hints;
  struct addrinfo *service_info_list = nullptr;

  ::memset(&hints, 0, sizeof(hints));
  hints.ai_family = kDomain;
  hints.ai_socktype = kType;
  int err = ::getaddrinfo(host_port->hostname.c_str(), std::to_string(host_port->port).c_str(), &hints,
                          &service_info_list);
  if (err != 0) {
    error.SetErrorStringWithFormat(
#if defined(_WIN32) && defined(UNICODE)
        "getaddrinfo(%s, %d, &hints, &info) returned error %i (%S)",
#else
        "getaddrinfo(%s, %d, &hints, &info) returned error %i (%s)",
#endif
        host_port->hostname.c_str(), host_port->port, err, gai_strerror(err));
    return error.ToError();
  }

  for (struct addrinfo *service_info_ptr = service_info_list;
       service_info_ptr != nullptr;
       service_info_ptr = service_info_ptr->ai_next) {
    auto send_fd = CreateSocket(
        service_info_ptr->ai_family, service_info_ptr->ai_socktype,
        service_info_ptr->ai_protocol, child_processes_inherit, error);
    if (error.Success()) {
      socket.reset(new UDPSocket(send_fd));
      socket->m_sockaddr = service_info_ptr;
      break;
    } else
      continue;
  }

  ::freeaddrinfo(service_info_list);

  if (!socket)
    return error.ToError();

  SocketAddress bind_addr;

  // Only bind to the loopback address if we are expecting a connection from
  // localhost to avoid any firewall issues.
  const bool bind_addr_success = (host_port->hostname == "127.0.0.1" || host_port->hostname == "localhost")
                                     ? bind_addr.SetToLocalhost(kDomain, host_port->port)
                                     : bind_addr.SetToAnyAddress(kDomain, host_port->port);

  if (!bind_addr_success) {
    error.SetErrorString("Failed to get hostspec to bind for");
    return error.ToError();
  }

  bind_addr.SetPort(0); // Let the source port # be determined dynamically

  err = ::bind(socket->GetNativeSocket(), bind_addr, bind_addr.GetLength());

  struct sockaddr_in source_info;
  socklen_t address_len = sizeof (struct sockaddr_in);
  err = ::getsockname(socket->GetNativeSocket(),
                      (struct sockaddr *)&source_info, &address_len);

  return std::move(socket);
}

std::string UDPSocket::GetRemoteConnectionURI() const {
  if (m_socket != kInvalidSocketValue) {
    return std::string(llvm::formatv(
        "udp://[{0}]:{1}", m_sockaddr.GetIPAddress(), m_sockaddr.GetPort()));
  }
  return "";
}
