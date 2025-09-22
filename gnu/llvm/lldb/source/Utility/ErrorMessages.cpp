//===-- ErrorMessages.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ErrorMessages.h"
#include "llvm/Support/ErrorHandling.h"

namespace lldb_private {

std::string toString(lldb::ExpressionResults e) {
  switch (e) {
  case lldb::eExpressionSetupError:
    return "expression setup error";
  case lldb::eExpressionParseError:
    return "expression parse error";
  case lldb::eExpressionResultUnavailable:
    return "expression error";
  case lldb::eExpressionCompleted:
    return "expression completed successfully";
  case lldb::eExpressionDiscarded:
    return "expression discarded";
  case lldb::eExpressionInterrupted:
    return "expression interrupted";
  case lldb::eExpressionHitBreakpoint:
    return "expression hit breakpoint";
  case lldb::eExpressionTimedOut:
    return "expression timed out";
  case lldb::eExpressionStoppedForDebug:
    return "expression stop at entry point for debugging";
  case lldb::eExpressionThreadVanished:
    return "expression thread vanished";
  }
  llvm_unreachable("unhandled enumerator");
}

} // namespace lldb_private
