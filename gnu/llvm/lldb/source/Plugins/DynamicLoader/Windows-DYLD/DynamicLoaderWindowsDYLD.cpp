//===-- DynamicLoaderWindowsDYLD.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DynamicLoaderWindowsDYLD.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(DynamicLoaderWindowsDYLD)

DynamicLoaderWindowsDYLD::DynamicLoaderWindowsDYLD(Process *process)
    : DynamicLoader(process) {}

DynamicLoaderWindowsDYLD::~DynamicLoaderWindowsDYLD() = default;

void DynamicLoaderWindowsDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderWindowsDYLD::Terminate() {}

llvm::StringRef DynamicLoaderWindowsDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in Windows processes.";
}

DynamicLoader *DynamicLoaderWindowsDYLD::CreateInstance(Process *process,
                                                        bool force) {
  bool should_create = force;
  if (!should_create) {
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (triple_ref.getOS() == llvm::Triple::Win32)
      should_create = true;
  }

  if (should_create)
    return new DynamicLoaderWindowsDYLD(process);

  return nullptr;
}

void DynamicLoaderWindowsDYLD::OnLoadModule(lldb::ModuleSP module_sp,
                                            const ModuleSpec module_spec,
                                            lldb::addr_t module_addr) {

  // Resolve the module unless we already have one.
  if (!module_sp) {
    Status error;
    module_sp = m_process->GetTarget().GetOrCreateModule(module_spec, 
                                             true /* notify */, &error);
    if (error.Fail())
      return;
  }

  m_loaded_modules[module_sp] = module_addr;
  UpdateLoadedSectionsCommon(module_sp, module_addr, false);
  ModuleList module_list;
  module_list.Append(module_sp);
  m_process->GetTarget().ModulesDidLoad(module_list);
}

void DynamicLoaderWindowsDYLD::OnUnloadModule(lldb::addr_t module_addr) {
  Address resolved_addr;
  if (!m_process->GetTarget().ResolveLoadAddress(module_addr, resolved_addr))
    return;

  ModuleSP module_sp = resolved_addr.GetModule();
  if (module_sp) {
    m_loaded_modules.erase(module_sp);
    UnloadSectionsCommon(module_sp);
    ModuleList module_list;
    module_list.Append(module_sp);
    m_process->GetTarget().ModulesDidUnload(module_list, false);
  }
}

lldb::addr_t DynamicLoaderWindowsDYLD::GetLoadAddress(ModuleSP executable) {
  // First, see if the load address is already cached.
  auto it = m_loaded_modules.find(executable);
  if (it != m_loaded_modules.end() && it->second != LLDB_INVALID_ADDRESS)
    return it->second;

  lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;

  // Second, try to get it through the process plugins.  For a remote process,
  // the remote platform will be responsible for providing it.
  FileSpec file_spec(executable->GetPlatformFileSpec());
  bool is_loaded = false;
  Status status =
      m_process->GetFileLoadAddress(file_spec, is_loaded, load_addr);
  // Servers other than lldb server could respond with a bogus address.
  if (status.Success() && is_loaded && load_addr != LLDB_INVALID_ADDRESS) {
    m_loaded_modules[executable] = load_addr;
    return load_addr;
  }

  return LLDB_INVALID_ADDRESS;
}

void DynamicLoaderWindowsDYLD::DidAttach() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderWindowsDYLD::%s()", __FUNCTION__);

  ModuleSP executable = GetTargetExecutable();

  if (!executable.get())
    return;

  // Try to fetch the load address of the file from the process, since there
  // could be randomization of the load address.
  lldb::addr_t load_addr = GetLoadAddress(executable);
  if (load_addr == LLDB_INVALID_ADDRESS)
    return;

  // Request the process base address.
  lldb::addr_t image_base = m_process->GetImageInfoAddress();
  if (image_base == load_addr)
    return;

  // Rebase the process's modules if there is a mismatch.
  UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_addr, false);

  ModuleList module_list;
  module_list.Append(executable);
  m_process->GetTarget().ModulesDidLoad(module_list);
  auto error = m_process->LoadModules();
  LLDB_LOG_ERROR(log, std::move(error), "failed to load modules: {0}");
}

void DynamicLoaderWindowsDYLD::DidLaunch() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderWindowsDYLD::%s()", __FUNCTION__);

  ModuleSP executable = GetTargetExecutable();
  if (!executable.get())
    return;

  lldb::addr_t load_addr = GetLoadAddress(executable);
  if (load_addr != LLDB_INVALID_ADDRESS) {
    // Update the loaded sections so that the breakpoints can be resolved.
    UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_addr, false);

    ModuleList module_list;
    module_list.Append(executable);
    m_process->GetTarget().ModulesDidLoad(module_list);
    auto error = m_process->LoadModules();
    LLDB_LOG_ERROR(log, std::move(error), "failed to load modules: {0}");
  }
}

Status DynamicLoaderWindowsDYLD::CanLoadImage() { return Status(); }

ThreadPlanSP
DynamicLoaderWindowsDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                       bool stop) {
  auto arch = m_process->GetTarget().GetArchitecture();
  if (arch.GetMachine() != llvm::Triple::x86) {
    return ThreadPlanSP();
  }

  uint64_t pc = thread.GetRegisterContext()->GetPC();
  // Max size of an instruction in x86 is 15 bytes.
  AddressRange range(pc, 2 * 15);

  DisassemblerSP disassembler_sp = Disassembler::DisassembleRange(
      arch, nullptr, nullptr, m_process->GetTarget(), range);
  if (!disassembler_sp) {
    return ThreadPlanSP();
  }

  InstructionList *insn_list = &disassembler_sp->GetInstructionList();
  if (insn_list == nullptr) {
    return ThreadPlanSP();
  }

  // First instruction in a x86 Windows trampoline is going to be an indirect
  // jump through the IAT and the next one will be a nop (usually there for
  // alignment purposes). e.g.:
  //     0x70ff4cfc <+956>: jmpl   *0x7100c2a8
  //     0x70ff4d02 <+962>: nop

  auto first_insn = insn_list->GetInstructionAtIndex(0);
  auto second_insn = insn_list->GetInstructionAtIndex(1);

  ExecutionContext exe_ctx(m_process->GetTarget());
  if (first_insn == nullptr || second_insn == nullptr ||
      strcmp(first_insn->GetMnemonic(&exe_ctx), "jmpl") != 0 ||
      strcmp(second_insn->GetMnemonic(&exe_ctx), "nop") != 0) {
    return ThreadPlanSP();
  }

  assert(first_insn->DoesBranch() && !second_insn->DoesBranch());

  return ThreadPlanSP(new ThreadPlanStepInstruction(
      thread, false, false, eVoteNoOpinion, eVoteNoOpinion));
}
