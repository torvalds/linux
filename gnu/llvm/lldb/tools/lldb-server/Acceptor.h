//===-- Acceptor.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLDB_TOOLS_LLDB_SERVER_ACCEPTOR_H
#define LLDB_TOOLS_LLDB_SERVER_ACCEPTOR_H

#include "lldb/Host/Socket.h"
#include "lldb/Utility/Connection.h"
#include "lldb/Utility/Status.h"

#include <functional>
#include <memory>
#include <string>

namespace llvm {
class StringRef;
}

namespace lldb_private {
namespace lldb_server {

class Acceptor {
public:
  virtual ~Acceptor() = default;

  Status Listen(int backlog);

  Status Accept(const bool child_processes_inherit, Connection *&conn);

  static std::unique_ptr<Acceptor> Create(llvm::StringRef name,
                                          const bool child_processes_inherit,
                                          Status &error);

  Socket::SocketProtocol GetSocketProtocol() const;

  const char *GetSocketScheme() const;

  // Returns either TCP port number as string or domain socket path.
  // Empty string is returned in case of error.
  std::string GetLocalSocketId() const;

private:
  typedef std::function<std::string()> LocalSocketIdFunc;

  Acceptor(std::unique_ptr<Socket> &&listener_socket, llvm::StringRef name,
           const LocalSocketIdFunc &local_socket_id);

  const std::unique_ptr<Socket> m_listener_socket_up;
  const std::string m_name;
  const LocalSocketIdFunc m_local_socket_id;
};

} // namespace lldb_server
} // namespace lldb_private

#endif // LLDB_TOOLS_LLDB_SERVER_ACCEPTOR_H
