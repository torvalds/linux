//===-- Checksum.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_CHECKSUM_H
#define LLDB_UTILITY_CHECKSUM_H

#include "llvm/Support/MD5.h"

namespace lldb_private {
class Checksum {
public:
  static llvm::MD5::MD5Result g_sentinel;

  Checksum(llvm::MD5::MD5Result md5 = g_sentinel);
  Checksum(const Checksum &checksum);
  Checksum &operator=(const Checksum &checksum);

  explicit operator bool() const;
  bool operator==(const Checksum &checksum) const;
  bool operator!=(const Checksum &checksum) const;

  std::string digest() const;

private:
  void SetMD5(llvm::MD5::MD5Result);

  llvm::MD5::MD5Result m_checksum;
};
} // namespace lldb_private

#endif // LLDB_UTILITY_CHECKSUM_H
