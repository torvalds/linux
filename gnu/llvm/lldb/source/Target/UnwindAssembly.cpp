//===-- UnwindAssembly.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/UnwindAssembly.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

UnwindAssemblySP UnwindAssembly::FindPlugin(const ArchSpec &arch) {
  UnwindAssemblyCreateInstance create_callback;

  for (uint32_t idx = 0;
       (create_callback = PluginManager::GetUnwindAssemblyCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    UnwindAssemblySP assembly_profiler_up(create_callback(arch));
    if (assembly_profiler_up)
      return assembly_profiler_up;
  }
  return nullptr;
}

UnwindAssembly::UnwindAssembly(const ArchSpec &arch) : m_arch(arch) {}
