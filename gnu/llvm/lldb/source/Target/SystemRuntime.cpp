//===-- SystemRuntime.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/SystemRuntime.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Process.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

SystemRuntime *SystemRuntime::FindPlugin(Process *process) {
  SystemRuntimeCreateInstance create_callback = nullptr;
  for (uint32_t idx = 0;
       (create_callback = PluginManager::GetSystemRuntimeCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    std::unique_ptr<SystemRuntime> instance_up(create_callback(process));
    if (instance_up)
      return instance_up.release();
  }
  return nullptr;
}

SystemRuntime::SystemRuntime(Process *process) : Runtime(process), m_types() {}

SystemRuntime::~SystemRuntime() = default;

void SystemRuntime::DidAttach() {}

void SystemRuntime::DidLaunch() {}

void SystemRuntime::Detach() {}

void SystemRuntime::ModulesDidLoad(const ModuleList &module_list) {}

const std::vector<ConstString> &SystemRuntime::GetExtendedBacktraceTypes() {
  return m_types;
}

ThreadSP SystemRuntime::GetExtendedBacktraceThread(ThreadSP thread,
                                                   ConstString type) {
  return ThreadSP();
}
