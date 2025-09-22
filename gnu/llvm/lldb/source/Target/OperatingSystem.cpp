//===-- OperatingSystem.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/OperatingSystem.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

OperatingSystem *OperatingSystem::FindPlugin(Process *process,
                                             const char *plugin_name) {
  OperatingSystemCreateInstance create_callback = nullptr;
  if (plugin_name) {
    create_callback =
        PluginManager::GetOperatingSystemCreateCallbackForPluginName(
            plugin_name);
    if (create_callback) {
      std::unique_ptr<OperatingSystem> instance_up(
          create_callback(process, true));
      if (instance_up)
        return instance_up.release();
    }
  } else {
    for (uint32_t idx = 0;
         (create_callback =
              PluginManager::GetOperatingSystemCreateCallbackAtIndex(idx)) !=
         nullptr;
         ++idx) {
      std::unique_ptr<OperatingSystem> instance_up(
          create_callback(process, false));
      if (instance_up)
        return instance_up.release();
    }
  }
  return nullptr;
}

OperatingSystem::OperatingSystem(Process *process) : m_process(process) {}

bool OperatingSystem::IsOperatingSystemPluginThread(
    const lldb::ThreadSP &thread_sp) {
  if (thread_sp)
    return thread_sp->IsOperatingSystemPluginThread();
  return false;
}
