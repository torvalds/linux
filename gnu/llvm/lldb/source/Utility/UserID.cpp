//===-- UserID.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UserID.h"
#include "lldb/Utility/Stream.h"

#include <cinttypes>

using namespace lldb;
using namespace lldb_private;

Stream &lldb_private::operator<<(Stream &strm, const UserID &uid) {
  strm.Printf("{0x%8.8" PRIx64 "}", uid.GetID());
  return strm;
}
