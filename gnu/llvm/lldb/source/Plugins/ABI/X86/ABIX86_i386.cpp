//===-- ABIX86_i386.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIX86_i386.h"

uint32_t ABIX86_i386::GetGenericNum(llvm::StringRef name) {
  return llvm::StringSwitch<uint32_t>(name)
      .Case("eip", LLDB_REGNUM_GENERIC_PC)
      .Case("esp", LLDB_REGNUM_GENERIC_SP)
      .Case("ebp", LLDB_REGNUM_GENERIC_FP)
      .Case("eflags", LLDB_REGNUM_GENERIC_FLAGS)
      .Case("edi", LLDB_REGNUM_GENERIC_ARG1)
      .Case("esi", LLDB_REGNUM_GENERIC_ARG2)
      .Case("edx", LLDB_REGNUM_GENERIC_ARG3)
      .Case("ecx", LLDB_REGNUM_GENERIC_ARG4)
      .Default(LLDB_INVALID_REGNUM);
}
