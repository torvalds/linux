//===-- LinuxProcMaps.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPROCMAPS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPROCMAPS_H

#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace lldb_private {

typedef std::function<bool(llvm::Expected<MemoryRegionInfo>)> LinuxMapCallback;

void ParseLinuxMapRegions(llvm::StringRef linux_map,
                          LinuxMapCallback const &callback);
void ParseLinuxSMapRegions(llvm::StringRef linux_smap,
                           LinuxMapCallback const &callback);

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPROCMAPS_H
