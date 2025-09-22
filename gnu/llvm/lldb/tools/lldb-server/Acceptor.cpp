//===-- Acceptor.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Acceptor.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ScopedPrinter.h"

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/common/TCPSocket.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/UriParser.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::lldb_server;
using namespace llvm;

namespace {

struct SocketScheme {
  const char *m_scheme;
  const Socket::SocketProtocol m_protocol;
};

SocketScheme socket_schemes[] = {
    {"tcp", Socket::ProtocolTcp},
    {"udp", Socket::ProtocolUdp},
    {"unix", Socket::ProtocolUnixDomain},
    {"unix-abstract", Socket::ProtocolUnixAbstract},
};

bool FindProtocolByScheme(const char *scheme,
                          Socket::SocketProtocol &protocol) {
  for (auto s : socket_schemes) {
    if (!strcmp(s.m_scheme, scheme)) {
      protocol = s.m_protocol;
      return true;
    }
  }
  return false;
}

const char *FindSchemeByProtocol(const Socket::SocketProtocol protocol) {
  for (auto s : socket_schemes) {
    if (s.m_protocol == protocol)
      return s.m_scheme;
  }
  return nullptr;
}
}

Status Acceptor::Listen(int backlog) {
  return m_listener_socket_up->Listen(StringRef(m_name), backlog);
}

Status Acceptor::Accept(const bool child_processes_inherit, Connection *&conn) {
  Socket *conn_socket = nullptr;
  auto error = m_listener_socket_up->Accept(conn_socket);
  if (error.Success())
    conn = new ConnectionFileDescriptor(conn_socket);

  return error;
}

Socket::SocketProtocol Acceptor::GetSocketProtocol() const {
  return m_listener_socket_up->GetSocketProtocol();
}

const char *Acceptor::GetSocketScheme() const {
  return FindSchemeByProtocol(GetSocketProtocol());
}

std::string Acceptor::GetLocalSocketId() const { return m_local_socket_id(); }

std::unique_ptr<Acceptor> Acceptor::Create(StringRef name,
                                           const bool child_processes_inherit,
                                           Status &error) {
  error.Clear();

  Socket::SocketProtocol socket_protocol = Socket::ProtocolUnixDomain;
  // Try to match socket name as URL - e.g., tcp://localhost:5555
  if (std::optional<URI> res = URI::Parse(name)) {
    if (!FindProtocolByScheme(res->scheme.str().c_str(), socket_protocol))
      error.SetErrorStringWithFormat("Unknown protocol scheme \"%s\"",
                                     res->scheme.str().c_str());
    else
      name = name.drop_front(res->scheme.size() + strlen("://"));
  } else {
    // Try to match socket name as $host:port - e.g., localhost:5555
    if (!llvm::errorToBool(Socket::DecodeHostAndPort(name).takeError()))
      socket_protocol = Socket::ProtocolTcp;
  }

  if (error.Fail())
    return std::unique_ptr<Acceptor>();

  std::unique_ptr<Socket> listener_socket_up =
      Socket::Create(socket_protocol, child_processes_inherit, error);

  LocalSocketIdFunc local_socket_id;
  if (error.Success()) {
    if (listener_socket_up->GetSocketProtocol() == Socket::ProtocolTcp) {
      TCPSocket *tcp_socket =
          static_cast<TCPSocket *>(listener_socket_up.get());
      local_socket_id = [tcp_socket]() {
        auto local_port = tcp_socket->GetLocalPortNumber();
        return (local_port != 0) ? llvm::to_string(local_port) : "";
      };
    } else {
      const std::string socket_name = std::string(name);
      local_socket_id = [socket_name]() { return socket_name; };
    }

    return std::unique_ptr<Acceptor>(
        new Acceptor(std::move(listener_socket_up), name, local_socket_id));
  }

  return std::unique_ptr<Acceptor>();
}

Acceptor::Acceptor(std::unique_ptr<Socket> &&listener_socket, StringRef name,
                   const LocalSocketIdFunc &local_socket_id)
    : m_listener_socket_up(std::move(listener_socket)), m_name(name.str()),
      m_local_socket_id(local_socket_id) {}
