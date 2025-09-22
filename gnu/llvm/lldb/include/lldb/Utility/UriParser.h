//===-- UriParser.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_URIPARSER_H
#define LLDB_UTILITY_URIPARSER_H

#include "llvm/ADT/StringRef.h"
#include <optional>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace lldb_private {

struct URI {
  llvm::StringRef scheme;
  llvm::StringRef hostname;
  std::optional<uint16_t> port;
  llvm::StringRef path;

  bool operator==(const URI &R) const {
    return port == R.port && scheme == R.scheme && hostname == R.hostname &&
           path == R.path;
  }

  static std::optional<URI> Parse(llvm::StringRef uri);
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const URI &U);

} // namespace lldb_private

#endif // LLDB_UTILITY_URIPARSER_H
