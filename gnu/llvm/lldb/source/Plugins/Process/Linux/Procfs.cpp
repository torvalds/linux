//===-- Procfs.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Procfs.h"
#include "lldb/Host/linux/Support.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

Expected<ArrayRef<uint8_t>> lldb_private::process_linux::GetProcfsCpuInfo() {
  static ErrorOr<std::unique_ptr<MemoryBuffer>> cpu_info_or_err =
      getProcFile("cpuinfo");

  if (!*cpu_info_or_err)
    cpu_info_or_err.getError();

  MemoryBuffer &buffer = **cpu_info_or_err;
  return arrayRefFromStringRef(buffer.getBuffer());
}

Expected<std::vector<cpu_id_t>>
lldb_private::process_linux::GetAvailableLogicalCoreIDs(StringRef cpuinfo) {
  SmallVector<StringRef, 8> lines;
  cpuinfo.split(lines, "\n", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  std::vector<cpu_id_t> logical_cores;

  for (StringRef line : lines) {
    std::pair<StringRef, StringRef> key_value = line.split(':');
    auto key = key_value.first.trim();
    auto val = key_value.second.trim();
    if (key == "processor") {
      cpu_id_t processor;
      if (val.getAsInteger(10, processor))
        return createStringError(
            inconvertibleErrorCode(),
            "Failed parsing the /proc/cpuinfo line entry: %s", line.data());
      logical_cores.push_back(processor);
    }
  }
  return logical_cores;
}

llvm::Expected<llvm::ArrayRef<cpu_id_t>>
lldb_private::process_linux::GetAvailableLogicalCoreIDs() {
  static std::optional<std::vector<cpu_id_t>> logical_cores_ids;
  if (!logical_cores_ids) {
    // We find the actual list of core ids by parsing /proc/cpuinfo
    Expected<ArrayRef<uint8_t>> cpuinfo = GetProcfsCpuInfo();
    if (!cpuinfo)
      return cpuinfo.takeError();

    Expected<std::vector<cpu_id_t>> cpu_ids = GetAvailableLogicalCoreIDs(
        StringRef(reinterpret_cast<const char *>(cpuinfo->data())));
    if (!cpu_ids)
      return cpu_ids.takeError();

    logical_cores_ids.emplace(std::move(*cpu_ids));
  }
  return *logical_cores_ids;
}

llvm::Expected<int> lldb_private::process_linux::GetPtraceScope() {
  ErrorOr<std::unique_ptr<MemoryBuffer>> ptrace_scope_file =
      getProcFile("sys/kernel/yama/ptrace_scope");
  if (!*ptrace_scope_file)
    return errorCodeToError(ptrace_scope_file.getError());
  // The contents should be something like "1\n". Trim it so we get "1".
  StringRef buffer = (*ptrace_scope_file)->getBuffer().trim();
  int ptrace_scope_value;
  if (buffer.getAsInteger(10, ptrace_scope_value)) {
    return createStringError(inconvertibleErrorCode(),
                             "Invalid ptrace_scope value: '%s'", buffer.data());
  }
  return ptrace_scope_value;
}
