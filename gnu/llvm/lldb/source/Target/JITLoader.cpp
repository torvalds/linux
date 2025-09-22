//===-- JITLoader.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/JITLoader.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/JITLoaderList.h"
#include "lldb/Target/Process.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

void JITLoader::LoadPlugins(Process *process, JITLoaderList &list) {
  JITLoaderCreateInstance create_callback = nullptr;
  for (uint32_t idx = 0;
       (create_callback =
            PluginManager::GetJITLoaderCreateCallbackAtIndex(idx)) != nullptr;
       ++idx) {
    JITLoaderSP instance_sp(create_callback(process, false));
    if (instance_sp)
      list.Append(std::move(instance_sp));
  }
}

JITLoader::JITLoader(Process *process) : m_process(process) {}

JITLoader::~JITLoader() = default;
