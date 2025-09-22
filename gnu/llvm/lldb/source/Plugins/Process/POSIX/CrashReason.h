//===-- CrashReason.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CrashReason_H_
#define liblldb_CrashReason_H_

#include "lldb/lldb-types.h"

#include <csignal>

#include <string>

std::string GetCrashReasonString(const siginfo_t &info);

#endif // #ifndef liblldb_CrashReason_H_
