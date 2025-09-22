//===-- UriParser.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UriParser.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

#include <cstdint>
#include <optional>
#include <tuple>

using namespace lldb_private;

llvm::raw_ostream &lldb_private::operator<<(llvm::raw_ostream &OS,
                                            const URI &U) {
  OS << U.scheme << "://[" << U.hostname << ']';
  if (U.port)
    OS << ':' << *U.port;
  return OS << U.path;
}

std::optional<URI> URI::Parse(llvm::StringRef uri) {
  URI ret;

  const llvm::StringRef kSchemeSep("://");
  auto pos = uri.find(kSchemeSep);
  if (pos == std::string::npos)
    return std::nullopt;

  // Extract path.
  ret.scheme = uri.substr(0, pos);
  auto host_pos = pos + kSchemeSep.size();
  auto path_pos = uri.find('/', host_pos);
  if (path_pos != std::string::npos)
    ret.path = uri.substr(path_pos);
  else
    ret.path = "/";

  auto host_port = uri.substr(
      host_pos,
      ((path_pos != std::string::npos) ? path_pos : uri.size()) - host_pos);

  // Extract hostname
  if (host_port.starts_with('[')) {
    // hostname is enclosed with square brackets.
    pos = host_port.rfind(']');
    if (pos == std::string::npos)
      return std::nullopt;

    ret.hostname = host_port.substr(1, pos - 1);
    host_port = host_port.drop_front(pos + 1);
    if (!host_port.empty() && !host_port.consume_front(":"))
      return std::nullopt;
  } else {
    std::tie(ret.hostname, host_port) = host_port.split(':');
  }

  // Extract port
  if (!host_port.empty()) {
    uint16_t port_value = 0;
    if (host_port.getAsInteger(0, port_value))
      return std::nullopt;
    ret.port = port_value;
  } else
    ret.port = std::nullopt;

  return ret;
}
