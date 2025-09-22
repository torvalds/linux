//===-- CrashReason.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CrashReason.h"

#include "lldb/Target/UnixSignals.h"

std::string GetCrashReasonString(const siginfo_t &info) {
#if defined(si_lower) && defined(si_upper)
  std::optional<lldb::addr_t> lower =
      reinterpret_cast<lldb::addr_t>(info.si_lower);
  std::optional<lldb::addr_t> upper =
      reinterpret_cast<lldb::addr_t>(info.si_upper);
#else
  std::optional<lldb::addr_t> lower;
  std::optional<lldb::addr_t> upper;
#endif

  std::string description =
      lldb_private::UnixSignals::CreateForHost()->GetSignalDescription(
          info.si_signo, info.si_code,
          reinterpret_cast<uintptr_t>(info.si_addr), lower, upper);
  assert(description.size() && "unexpected signal");

  return "signal " + description;
}
