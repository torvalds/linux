//===-- CommandOptionValidators.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandOptionValidators.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Platform.h"

using namespace lldb;
using namespace lldb_private;

bool PosixPlatformCommandOptionValidator::IsValid(
    Platform &platform, const ExecutionContext &target) const {
  llvm::Triple::OSType os =
      platform.GetSystemArchitecture().GetTriple().getOS();
  switch (os) {
  // Are there any other platforms that are not POSIX-compatible?
  case llvm::Triple::Win32:
    return false;
  default:
    return true;
  }
}

const char *PosixPlatformCommandOptionValidator::ShortConditionString() const {
  return "POSIX";
}

const char *PosixPlatformCommandOptionValidator::LongConditionString() const {
  return "Option only valid for POSIX-compliant hosts.";
}
