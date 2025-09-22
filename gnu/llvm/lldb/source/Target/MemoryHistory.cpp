//===-- MemoryHistory.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/MemoryHistory.h"
#include "lldb/Core/PluginManager.h"

using namespace lldb;
using namespace lldb_private;

lldb::MemoryHistorySP MemoryHistory::FindPlugin(const ProcessSP process) {
  MemoryHistoryCreateInstance create_callback = nullptr;

  for (uint32_t idx = 0;
       (create_callback = PluginManager::GetMemoryHistoryCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    MemoryHistorySP memory_history_sp(create_callback(process));
    if (memory_history_sp)
      return memory_history_sp;
  }

  return MemoryHistorySP();
}
