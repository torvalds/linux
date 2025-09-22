//===-- Socket.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Socket.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/SocketAddress.h"
#include "lldb/Host/common/TCPSocket.h"
#include "lldb/Host/common/UDPSocket.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/WindowsError.h"

#if LLDB_ENABLE_POSIX
#include "lldb/Host/posix/DomainSocket.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include "lldb/Host/linux/AbstractSocket.h"
#endif

#ifdef __ANDROID__
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <cerrno>
#include <fcntl.h>
#include <linux/tcp.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif // __ANDROID__

using namespace lldb;
using namespace lldb_private;

#if defined(_WIN32)
typedef const char *set_socket_option_arg_type;
typedef char *get_socket_option_arg_type;
const NativeSocket Socket::kInvalidSocketValue = INVALID_SOCKET;
#else  // #if defined(_WIN32)
typedef const void *set_socket_option_arg_type;
typedef void *get_socket_option_arg_type;
const NativeSocket Socket::kInvalidSocketValue = -1;
#endif // #if defined(_WIN32)

static bool IsInterrupted() {
#if defined(_WIN32)
  return ::WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

Socket::Socket(SocketProtocol protocol, bool should_close,
               bool child_processes_inherit)
    : IOObject(eFDTypeSocket), m_protocol(protocol),
      m_socket(kInvalidSocketValue),
      m_child_processes_inherit(child_processes_inherit),
      m_should_close_fd(should_close) {}

Socket::~Socket() { Close(); }

llvm::Error Socket::Initialize() {
#if defined(_WIN32)
  auto wVersion = WINSOCK_VERSION;
  WSADATA wsaData;
  int err = ::WSAStartup(wVersion, &wsaData);
  if (err == 0) {
    if (wsaData.wVersion < wVersion) {
      WSACleanup();
      return llvm::createStringError("WSASock version is not expected.");
    }
  } else {
    return llvm::errorCodeToError(llvm::mapWindowsError(::WSAGetLastError()));
  }
#endif

  return llvm::Error::success();
}

void Socket::Terminate() {
#if defined(_WIN32)
  ::WSACleanup();
#endif
}

std::unique_ptr<Socket> Socket::Create(const SocketProtocol protocol,
                                       bool child_processes_inherit,
                                       Status &error) {
  error.Clear();

  std::unique_ptr<Socket> socket_up;
  switch (protocol) {
  case ProtocolTcp:
    socket_up =
        std::make_unique<TCPSocket>(true, child_processes_inherit);
    break;
  case ProtocolUdp:
    socket_up =
        std::make_unique<UDPSocket>(true, child_processes_inherit);
    break;
  case ProtocolUnixDomain:
#if LLDB_ENABLE_POSIX
    socket_up =
        std::make_unique<DomainSocket>(true, child_processes_inherit);
#else
    error.SetErrorString(
        "Unix domain sockets are not supported on this platform.");
#endif
    break;
  case ProtocolUnixAbstract:
#ifdef __linux__
    socket_up =
        std::make_unique<AbstractSocket>(child_processes_inherit);
#else
    error.SetErrorString(
        "Abstract domain sockets are not supported on this platform.");
#endif
    break;
  }

  if (error.Fail())
    socket_up.reset();

  return socket_up;
}

llvm::Expected<std::unique_ptr<Socket>>
Socket::TcpConnect(llvm::StringRef host_and_port,
                   bool child_processes_inherit) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "host_and_port = {0}", host_and_port);

  Status error;
  std::unique_ptr<Socket> connect_socket(
      Create(ProtocolTcp, child_processes_inherit, error));
  if (error.Fail())
    return error.ToError();

  error = connect_socket->Connect(host_and_port);
  if (error.Success())
    return std::move(connect_socket);

  return error.ToError();
}

llvm::Expected<std::unique_ptr<TCPSocket>>
Socket::TcpListen(llvm::StringRef host_and_port, bool child_processes_inherit,
                  int backlog) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "host_and_port = {0}", host_and_port);

  std::unique_ptr<TCPSocket> listen_socket(
      new TCPSocket(true, child_processes_inherit));

  Status error = listen_socket->Listen(host_and_port, backlog);
  if (error.Fail())
    return error.ToError();

  return std::move(listen_socket);
}

llvm::Expected<std::unique_ptr<UDPSocket>>
Socket::UdpConnect(llvm::StringRef host_and_port,
                   bool child_processes_inherit) {
  return UDPSocket::Connect(host_and_port, child_processes_inherit);
}

llvm::Expected<Socket::HostAndPort> Socket::DecodeHostAndPort(llvm::StringRef host_and_port) {
  static llvm::Regex g_regex("([^:]+|\\[[0-9a-fA-F:]+.*\\]):([0-9]+)");
  HostAndPort ret;
  llvm::SmallVector<llvm::StringRef, 3> matches;
  if (g_regex.match(host_and_port, &matches)) {
    ret.hostname = matches[1].str();
    // IPv6 addresses are wrapped in [] when specified with ports
    if (ret.hostname.front() == '[' && ret.hostname.back() == ']')
      ret.hostname = ret.hostname.substr(1, ret.hostname.size() - 2);
    if (to_integer(matches[2], ret.port, 10))
      return ret;
  } else {
    // If this was unsuccessful, then check if it's simply an unsigned 16-bit
    // integer, representing a port with an empty host.
    if (to_integer(host_and_port, ret.port, 10))
      return ret;
  }

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid host:port specification: '%s'",
                                 host_and_port.str().c_str());
}

IOObject::WaitableHandle Socket::GetWaitableHandle() {
  // TODO: On Windows, use WSAEventSelect
  return m_socket;
}

Status Socket::Read(void *buf, size_t &num_bytes) {
  Status error;
  int bytes_received = 0;
  do {
    bytes_received = ::recv(m_socket, static_cast<char *>(buf), num_bytes, 0);
  } while (bytes_received < 0 && IsInterrupted());

  if (bytes_received < 0) {
    SetLastError(error);
    num_bytes = 0;
  } else
    num_bytes = bytes_received;

  Log *log = GetLog(LLDBLog::Communication);
  if (log) {
    LLDB_LOGF(log,
              "%p Socket::Read() (socket = %" PRIu64
              ", src = %p, src_len = %" PRIu64 ", flags = 0) => %" PRIi64
              " (error = %s)",
              static_cast<void *>(this), static_cast<uint64_t>(m_socket), buf,
              static_cast<uint64_t>(num_bytes),
              static_cast<int64_t>(bytes_received), error.AsCString());
  }

  return error;
}

Status Socket::Write(const void *buf, size_t &num_bytes) {
  const size_t src_len = num_bytes;
  Status error;
  int bytes_sent = 0;
  do {
    bytes_sent = Send(buf, num_bytes);
  } while (bytes_sent < 0 && IsInterrupted());

  if (bytes_sent < 0) {
    SetLastError(error);
    num_bytes = 0;
  } else
    num_bytes = bytes_sent;

  Log *log = GetLog(LLDBLog::Communication);
  if (log) {
    LLDB_LOGF(log,
              "%p Socket::Write() (socket = %" PRIu64
              ", src = %p, src_len = %" PRIu64 ", flags = 0) => %" PRIi64
              " (error = %s)",
              static_cast<void *>(this), static_cast<uint64_t>(m_socket), buf,
              static_cast<uint64_t>(src_len),
              static_cast<int64_t>(bytes_sent), error.AsCString());
  }

  return error;
}

Status Socket::Close() {
  Status error;
  if (!IsValid() || !m_should_close_fd)
    return error;

  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p Socket::Close (fd = %" PRIu64 ")",
            static_cast<void *>(this), static_cast<uint64_t>(m_socket));

#if defined(_WIN32)
  bool success = closesocket(m_socket) == 0;
#else
  bool success = ::close(m_socket) == 0;
#endif
  // A reference to a FD was passed in, set it to an invalid value
  m_socket = kInvalidSocketValue;
  if (!success) {
    SetLastError(error);
  }

  return error;
}

int Socket::GetOption(int level, int option_name, int &option_value) {
  get_socket_option_arg_type option_value_p =
      reinterpret_cast<get_socket_option_arg_type>(&option_value);
  socklen_t option_value_size = sizeof(int);
  return ::getsockopt(m_socket, level, option_name, option_value_p,
                      &option_value_size);
}

int Socket::SetOption(int level, int option_name, int option_value) {
  set_socket_option_arg_type option_value_p =
      reinterpret_cast<get_socket_option_arg_type>(&option_value);
  return ::setsockopt(m_socket, level, option_name, option_value_p,
                      sizeof(option_value));
}

size_t Socket::Send(const void *buf, const size_t num_bytes) {
  return ::send(m_socket, static_cast<const char *>(buf), num_bytes, 0);
}

void Socket::SetLastError(Status &error) {
#if defined(_WIN32)
  error.SetError(::WSAGetLastError(), lldb::eErrorTypeWin32);
#else
  error.SetErrorToErrno();
#endif
}

NativeSocket Socket::CreateSocket(const int domain, const int type,
                                  const int protocol,
                                  bool child_processes_inherit, Status &error) {
  error.Clear();
  auto socket_type = type;
#ifdef SOCK_CLOEXEC
  if (!child_processes_inherit)
    socket_type |= SOCK_CLOEXEC;
#endif
  auto sock = ::socket(domain, socket_type, protocol);
  if (sock == kInvalidSocketValue)
    SetLastError(error);

  return sock;
}

NativeSocket Socket::AcceptSocket(NativeSocket sockfd, struct sockaddr *addr,
                                  socklen_t *addrlen,
                                  bool child_processes_inherit, Status &error) {
  error.Clear();
#if defined(ANDROID_USE_ACCEPT_WORKAROUND)
  // Hack:
  // This enables static linking lldb-server to an API 21 libc, but still
  // having it run on older devices. It is necessary because API 21 libc's
  // implementation of accept() uses the accept4 syscall(), which is not
  // available in older kernels. Using an older libc would fix this issue, but
  // introduce other ones, as the old libraries were quite buggy.
  int fd = syscall(__NR_accept, sockfd, addr, addrlen);
  if (fd >= 0 && !child_processes_inherit) {
    int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1)
      return fd;
    SetLastError(error);
    close(fd);
  }
  return fd;
#elif defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
  int flags = 0;
  if (!child_processes_inherit) {
    flags |= SOCK_CLOEXEC;
  }
  NativeSocket fd = llvm::sys::RetryAfterSignal(
      static_cast<NativeSocket>(-1), ::accept4, sockfd, addr, addrlen, flags);
#else
  NativeSocket fd = llvm::sys::RetryAfterSignal(
      static_cast<NativeSocket>(-1), ::accept, sockfd, addr, addrlen);
#endif
  if (fd == kInvalidSocketValue)
    SetLastError(error);
  return fd;
}

llvm::raw_ostream &lldb_private::operator<<(llvm::raw_ostream &OS,
                                            const Socket::HostAndPort &HP) {
  return OS << '[' << HP.hostname << ']' << ':' << HP.port;
}
