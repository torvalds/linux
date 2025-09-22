//===-- Checksum.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Checksum.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"

using namespace lldb_private;

Checksum::Checksum(llvm::MD5::MD5Result md5) { SetMD5(md5); }

Checksum::Checksum(const Checksum &checksum) { SetMD5(checksum.m_checksum); }

Checksum &Checksum::operator=(const Checksum &checksum) {
  SetMD5(checksum.m_checksum);
  return *this;
}

void Checksum::SetMD5(llvm::MD5::MD5Result md5) {
  const constexpr size_t md5_length = 16;
  std::uninitialized_copy_n(md5.begin(), md5_length, m_checksum.begin());
}

Checksum::operator bool() const { return !llvm::equal(m_checksum, g_sentinel); }

bool Checksum::operator==(const Checksum &checksum) const {
  return llvm::equal(m_checksum, checksum.m_checksum);
}

bool Checksum::operator!=(const Checksum &checksum) const {
  return !(*this == checksum);
}

std::string Checksum::digest() const {
  return std::string(m_checksum.digest());
}

llvm::MD5::MD5Result Checksum::g_sentinel = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
