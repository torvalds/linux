//===-- Procfs.h ---------------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace lldb_private {
namespace process_linux {

/// \return
///     The content of /proc/cpuinfo and cache it if errors didn't happen.
llvm::Expected<llvm::ArrayRef<uint8_t>> GetProcfsCpuInfo();

/// \return
///     A list of available logical core ids given the contents of
///     /proc/cpuinfo.
llvm::Expected<std::vector<lldb::cpu_id_t>>
GetAvailableLogicalCoreIDs(llvm::StringRef cpuinfo);

/// \return
///     A list with all the logical cores available in the system and cache it
///     if errors didn't happen.
llvm::Expected<llvm::ArrayRef<lldb::cpu_id_t>> GetAvailableLogicalCoreIDs();

/// \return
///     The current value of /proc/sys/kernel/yama/ptrace_scope, parsed as an
///     integer, or an error if the proc file cannot be read or has non-integer
///     contents.
llvm::Expected<int> GetPtraceScope();

} // namespace process_linux
} // namespace lldb_private
