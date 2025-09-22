//===-- InferiorCallPOSIX.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_INFERIORCALLPOSIX_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_INFERIORCALLPOSIX_H

// Inferior execution of POSIX functions.

#include "lldb/lldb-types.h"

namespace lldb_private {

class Process;

enum MmapProt {
  eMmapProtNone = 0,
  eMmapProtExec = 1,
  eMmapProtRead = 2,
  eMmapProtWrite = 4
};

bool InferiorCallMmap(Process *proc, lldb::addr_t &allocated_addr,
                      lldb::addr_t addr, lldb::addr_t length, unsigned prot,
                      unsigned flags, lldb::addr_t fd, lldb::addr_t offset);

bool InferiorCallMunmap(Process *proc, lldb::addr_t addr, lldb::addr_t length);

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_INFERIORCALLPOSIX_H
