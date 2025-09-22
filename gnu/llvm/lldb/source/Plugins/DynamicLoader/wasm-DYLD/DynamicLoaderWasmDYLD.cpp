//===-- DynamicLoaderWasmDYLD.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DynamicLoaderWasmDYLD.h"

#include "Plugins/ObjectFile/wasm/ObjectFileWasm.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::wasm;

LLDB_PLUGIN_DEFINE(DynamicLoaderWasmDYLD)

DynamicLoaderWasmDYLD::DynamicLoaderWasmDYLD(Process *process)
    : DynamicLoader(process) {}

void DynamicLoaderWasmDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

llvm::StringRef DynamicLoaderWasmDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in WebAssembly engines.";
}

DynamicLoader *DynamicLoaderWasmDYLD::CreateInstance(Process *process,
                                                     bool force) {
  bool should_create = force;
  if (!should_create) {
    should_create =
        (process->GetTarget().GetArchitecture().GetTriple().getArch() ==
         llvm::Triple::wasm32);
  }

  if (should_create)
    return new DynamicLoaderWasmDYLD(process);

  return nullptr;
}

void DynamicLoaderWasmDYLD::DidAttach() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderWasmDYLD::%s()", __FUNCTION__);

  // Ask the process for the list of loaded WebAssembly modules.
  auto error = m_process->LoadModules();
  LLDB_LOG_ERROR(log, std::move(error), "Couldn't load modules: {0}");
}

ThreadPlanSP DynamicLoaderWasmDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                                 bool stop) {
  return ThreadPlanSP();
}

lldb::ModuleSP DynamicLoaderWasmDYLD::LoadModuleAtAddress(
    const lldb_private::FileSpec &file, lldb::addr_t link_map_addr,
    lldb::addr_t base_addr, bool base_addr_is_offset) {
  if (ModuleSP module_sp = DynamicLoader::LoadModuleAtAddress(
          file, link_map_addr, base_addr, base_addr_is_offset))
    return module_sp;

  if (ModuleSP module_sp = m_process->ReadModuleFromMemory(file, base_addr)) {
    UpdateLoadedSections(module_sp, link_map_addr, base_addr, false);
    m_process->GetTarget().GetImages().AppendIfNeeded(module_sp);
    return module_sp;
  }

  return nullptr;
}
