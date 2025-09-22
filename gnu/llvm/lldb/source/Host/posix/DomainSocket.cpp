//===-- DomainSocket.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/DomainSocket.h"

#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"

#include <cstddef>
#include <sys/socket.h>
#include <sys/un.h>

using namespace lldb;
using namespace lldb_private;

#ifdef __ANDROID__
// Android does not have SUN_LEN
#ifndef SUN_LEN
#define SUN_LEN(ptr)                                                           \
  (offsetof(struct sockaddr_un, sun_path) + strlen((ptr)->sun_path))
#endif
#endif // #ifdef __ANDROID__

static const int kDomain = AF_UNIX;
static const int kType = SOCK_STREAM;

static bool SetSockAddr(llvm::StringRef name, const size_t name_offset,
                        sockaddr_un *saddr_un, socklen_t &saddr_un_len) {
  if (name.size() + name_offset > sizeof(saddr_un->sun_path))
    return false;

  memset(saddr_un, 0, sizeof(*saddr_un));
  saddr_un->sun_family = kDomain;

  memcpy(saddr_un->sun_path + name_offset, name.data(), name.size());

  // For domain sockets we can use SUN_LEN in order to calculate size of
  // sockaddr_un, but for abstract sockets we have to calculate size manually
  // because of leading null symbol.
  if (name_offset == 0)
    saddr_un_len = SUN_LEN(saddr_un);
  else
    saddr_un_len =
        offsetof(struct sockaddr_un, sun_path) + name_offset + name.size();

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||       \
    defined(__OpenBSD__)
  saddr_un->sun_len = saddr_un_len;
#endif

  return true;
}

DomainSocket::DomainSocket(bool should_close, bool child_processes_inherit)
    : Socket(ProtocolUnixDomain, should_close, child_processes_inherit) {}

DomainSocket::DomainSocket(SocketProtocol protocol,
                           bool child_processes_inherit)
    : Socket(protocol, true, child_processes_inherit) {}

DomainSocket::DomainSocket(NativeSocket socket,
                           const DomainSocket &listen_socket)
    : Socket(ProtocolUnixDomain, listen_socket.m_should_close_fd,
             listen_socket.m_child_processes_inherit) {
  m_socket = socket;
}

Status DomainSocket::Connect(llvm::StringRef name) {
  sockaddr_un saddr_un;
  socklen_t saddr_un_len;
  if (!SetSockAddr(name, GetNameOffset(), &saddr_un, saddr_un_len))
    return Status("Failed to set socket address");

  Status error;
  m_socket = CreateSocket(kDomain, kType, 0, m_child_processes_inherit, error);
  if (error.Fail())
    return error;
  if (llvm::sys::RetryAfterSignal(-1, ::connect, GetNativeSocket(),
        (struct sockaddr *)&saddr_un, saddr_un_len) < 0)
    SetLastError(error);

  return error;
}

Status DomainSocket::Listen(llvm::StringRef name, int backlog) {
  sockaddr_un saddr_un;
  socklen_t saddr_un_len;
  if (!SetSockAddr(name, GetNameOffset(), &saddr_un, saddr_un_len))
    return Status("Failed to set socket address");

  DeleteSocketFile(name);

  Status error;
  m_socket = CreateSocket(kDomain, kType, 0, m_child_processes_inherit, error);
  if (error.Fail())
    return error;
  if (::bind(GetNativeSocket(), (struct sockaddr *)&saddr_un, saddr_un_len) ==
      0)
    if (::listen(GetNativeSocket(), backlog) == 0)
      return error;

  SetLastError(error);
  return error;
}

Status DomainSocket::Accept(Socket *&socket) {
  Status error;
  auto conn_fd = AcceptSocket(GetNativeSocket(), nullptr, nullptr,
                              m_child_processes_inherit, error);
  if (error.Success())
    socket = new DomainSocket(conn_fd, *this);

  return error;
}

size_t DomainSocket::GetNameOffset() const { return 0; }

void DomainSocket::DeleteSocketFile(llvm::StringRef name) {
  llvm::sys::fs::remove(name);
}

std::string DomainSocket::GetSocketName() const {
  if (m_socket == kInvalidSocketValue)
    return "";

  struct sockaddr_un saddr_un;
  saddr_un.sun_family = AF_UNIX;
  socklen_t sock_addr_len = sizeof(struct sockaddr_un);
  if (::getpeername(m_socket, (struct sockaddr *)&saddr_un, &sock_addr_len) !=
      0)
    return "";

  if (sock_addr_len <= offsetof(struct sockaddr_un, sun_path))
    return ""; // Unnamed domain socket

  llvm::StringRef name(saddr_un.sun_path + GetNameOffset(),
                       sock_addr_len - offsetof(struct sockaddr_un, sun_path) -
                           GetNameOffset());
  name = name.rtrim('\0');

  return name.str();
}

std::string DomainSocket::GetRemoteConnectionURI() const {
  std::string name = GetSocketName();
  if (name.empty())
    return name;

  return llvm::formatv(
      "{0}://{1}",
      GetNameOffset() == 0 ? "unix-connect" : "unix-abstract-connect", name);
}
