//===-- DynamicLoaderPOSIXDYLD.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Main header include
#include "DynamicLoaderPOSIXDYLD.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/ProcessInfo.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(DynamicLoaderPOSIXDYLD, DynamicLoaderPosixDYLD)

void DynamicLoaderPOSIXDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderPOSIXDYLD::Terminate() {}

llvm::StringRef DynamicLoaderPOSIXDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in POSIX processes.";
}

DynamicLoader *DynamicLoaderPOSIXDYLD::CreateInstance(Process *process,
                                                      bool force) {
  bool create = force;
  if (!create) {
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (triple_ref.getOS() == llvm::Triple::FreeBSD ||
        triple_ref.getOS() == llvm::Triple::Linux ||
        triple_ref.getOS() == llvm::Triple::NetBSD ||
        triple_ref.getOS() == llvm::Triple::OpenBSD)
      create = true;
  }

  if (create)
    return new DynamicLoaderPOSIXDYLD(process);
  return nullptr;
}

DynamicLoaderPOSIXDYLD::DynamicLoaderPOSIXDYLD(Process *process)
    : DynamicLoader(process), m_rendezvous(process),
      m_load_offset(LLDB_INVALID_ADDRESS), m_entry_point(LLDB_INVALID_ADDRESS),
      m_auxv(), m_dyld_bid(LLDB_INVALID_BREAK_ID),
      m_vdso_base(LLDB_INVALID_ADDRESS),
      m_interpreter_base(LLDB_INVALID_ADDRESS), m_initial_modules_added(false) {
}

DynamicLoaderPOSIXDYLD::~DynamicLoaderPOSIXDYLD() {
  if (m_dyld_bid != LLDB_INVALID_BREAK_ID) {
    m_process->GetTarget().RemoveBreakpointByID(m_dyld_bid);
    m_dyld_bid = LLDB_INVALID_BREAK_ID;
  }
}

void DynamicLoaderPOSIXDYLD::DidAttach() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderPOSIXDYLD::%s() pid %" PRIu64, __FUNCTION__,
            m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);
  m_auxv = std::make_unique<AuxVector>(m_process->GetAuxvData());

  LLDB_LOGF(
      log, "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64 " reloaded auxv data",
      __FUNCTION__, m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);

  ModuleSP executable_sp = GetTargetExecutable();
  ResolveExecutableModule(executable_sp);
  m_rendezvous.UpdateExecutablePath();

  // find the main process load offset
  addr_t load_offset = ComputeLoadOffset();
  LLDB_LOGF(log,
            "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
            " executable '%s', load_offset 0x%" PRIx64,
            __FUNCTION__,
            m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID,
            executable_sp ? executable_sp->GetFileSpec().GetPath().c_str()
                          : "<null executable>",
            load_offset);

  EvalSpecialModulesStatus();

  // if we dont have a load address we cant re-base
  bool rebase_exec = load_offset != LLDB_INVALID_ADDRESS;

  // if we have a valid executable
  if (executable_sp.get()) {
    lldb_private::ObjectFile *obj = executable_sp->GetObjectFile();
    if (obj) {
      // don't rebase if the module already has a load address
      Target &target = m_process->GetTarget();
      Address addr = obj->GetImageInfoAddress(&target);
      if (addr.GetLoadAddress(&target) != LLDB_INVALID_ADDRESS)
        rebase_exec = false;
    }
  } else {
    // no executable, nothing to re-base
    rebase_exec = false;
  }

  // if the target executable should be re-based
  if (rebase_exec) {
    ModuleList module_list;

    module_list.Append(executable_sp);
    LLDB_LOGF(log,
              "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
              " added executable '%s' to module load list",
              __FUNCTION__,
              m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID,
              executable_sp->GetFileSpec().GetPath().c_str());

    UpdateLoadedSections(executable_sp, LLDB_INVALID_ADDRESS, load_offset,
                         true);

    LoadAllCurrentModules();

    m_process->GetTarget().ModulesDidLoad(module_list);
    if (log) {
      LLDB_LOGF(log,
                "DynamicLoaderPOSIXDYLD::%s told the target about the "
                "modules that loaded:",
                __FUNCTION__);
      for (auto module_sp : module_list.Modules()) {
        LLDB_LOGF(log, "-- [module] %s (pid %" PRIu64 ")",
                  module_sp ? module_sp->GetFileSpec().GetPath().c_str()
                            : "<null>",
                  m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);
      }
    }
  }

  if (executable_sp.get()) {
    if (!SetRendezvousBreakpoint()) {
      // If we cannot establish rendezvous breakpoint right now we'll try again
      // at entry point.
      ProbeEntry();
    }
  }
}

void DynamicLoaderPOSIXDYLD::DidLaunch() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderPOSIXDYLD::%s()", __FUNCTION__);

  ModuleSP executable;
  addr_t load_offset;

  m_auxv = std::make_unique<AuxVector>(m_process->GetAuxvData());

  executable = GetTargetExecutable();
  load_offset = ComputeLoadOffset();
  EvalSpecialModulesStatus();

  if (executable.get() && load_offset != LLDB_INVALID_ADDRESS) {
    ModuleList module_list;
    module_list.Append(executable);
    UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_offset, true);

    LLDB_LOGF(log, "DynamicLoaderPOSIXDYLD::%s about to call ProbeEntry()",
              __FUNCTION__);

    if (!SetRendezvousBreakpoint()) {
      // If we cannot establish rendezvous breakpoint right now we'll try again
      // at entry point.
      ProbeEntry();
    }

    LoadVDSO();
    m_process->GetTarget().ModulesDidLoad(module_list);
  }
}

Status DynamicLoaderPOSIXDYLD::CanLoadImage() { return Status(); }

void DynamicLoaderPOSIXDYLD::UpdateLoadedSections(ModuleSP module,
                                                  addr_t link_map_addr,
                                                  addr_t base_addr,
                                                  bool base_addr_is_offset) {
  m_loaded_modules[module] = link_map_addr;
  UpdateLoadedSectionsCommon(module, base_addr, base_addr_is_offset);
}

void DynamicLoaderPOSIXDYLD::UnloadSections(const ModuleSP module) {
  m_loaded_modules.erase(module);

  UnloadSectionsCommon(module);
}

void DynamicLoaderPOSIXDYLD::ProbeEntry() {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  // If we have a core file, we don't need any breakpoints.
  if (IsCoreFile())
    return;

  const addr_t entry = GetEntryPoint();
  if (entry == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(
        log,
        "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
        " GetEntryPoint() returned no address, not setting entry breakpoint",
        __FUNCTION__, m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);
    return;
  }

  LLDB_LOGF(log,
            "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
            " GetEntryPoint() returned address 0x%" PRIx64
            ", setting entry breakpoint",
            __FUNCTION__,
            m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID, entry);

  if (m_process) {
    Breakpoint *const entry_break =
        m_process->GetTarget().CreateBreakpoint(entry, true, false).get();
    entry_break->SetCallback(EntryBreakpointHit, this, true);
    entry_break->SetBreakpointKind("shared-library-event");

    // Shoudn't hit this more than once.
    entry_break->SetOneShot(true);
  }
}

// The runtime linker has run and initialized the rendezvous structure once the
// process has hit its entry point.  When we hit the corresponding breakpoint
// we interrogate the rendezvous structure to get the load addresses of all
// dependent modules for the process.  Similarly, we can discover the runtime
// linker function and setup a breakpoint to notify us of any dynamically
// loaded modules (via dlopen).
bool DynamicLoaderPOSIXDYLD::EntryBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  Log *log = GetLog(LLDBLog::DynamicLoader);
  DynamicLoaderPOSIXDYLD *const dyld_instance =
      static_cast<DynamicLoaderPOSIXDYLD *>(baton);
  LLDB_LOGF(log, "DynamicLoaderPOSIXDYLD::%s called for pid %" PRIu64,
            __FUNCTION__,
            dyld_instance->m_process ? dyld_instance->m_process->GetID()
                                     : LLDB_INVALID_PROCESS_ID);

  // Disable the breakpoint --- if a stop happens right after this, which we've
  // seen on occasion, we don't want the breakpoint stepping thread-plan logic
  // to show a breakpoint instruction at the disassembled entry point to the
  // program.  Disabling it prevents it.  (One-shot is not enough - one-shot
  // removal logic only happens after the breakpoint goes public, which wasn't
  // happening in our scenario).
  if (dyld_instance->m_process) {
    BreakpointSP breakpoint_sp =
        dyld_instance->m_process->GetTarget().GetBreakpointByID(break_id);
    if (breakpoint_sp) {
      LLDB_LOGF(log,
                "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
                " disabling breakpoint id %" PRIu64,
                __FUNCTION__, dyld_instance->m_process->GetID(), break_id);
      breakpoint_sp->SetEnabled(false);
    } else {
      LLDB_LOGF(log,
                "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
                " failed to find breakpoint for breakpoint id %" PRIu64,
                __FUNCTION__, dyld_instance->m_process->GetID(), break_id);
    }
  } else {
    LLDB_LOGF(log,
              "DynamicLoaderPOSIXDYLD::%s breakpoint id %" PRIu64
              " no Process instance!  Cannot disable breakpoint",
              __FUNCTION__, break_id);
  }

  dyld_instance->LoadAllCurrentModules();
  dyld_instance->SetRendezvousBreakpoint();
  return false; // Continue running.
}

bool DynamicLoaderPOSIXDYLD::SetRendezvousBreakpoint() {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  // If we have a core file, we don't need any breakpoints.
  if (IsCoreFile())
    return false;

  if (m_dyld_bid != LLDB_INVALID_BREAK_ID) {
    LLDB_LOG(log,
             "Rendezvous breakpoint breakpoint id {0} for pid {1}"
             "is already set.",
             m_dyld_bid,
             m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);
    return true;
  }

  addr_t break_addr;
  Target &target = m_process->GetTarget();
  BreakpointSP dyld_break;
  if (m_rendezvous.IsValid() && m_rendezvous.GetBreakAddress() != 0) {
    break_addr = m_rendezvous.GetBreakAddress();
    LLDB_LOG(log, "Setting rendezvous break address for pid {0} at {1:x}",
             m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID,
             break_addr);
    dyld_break = target.CreateBreakpoint(break_addr, true, false);
  } else {
    LLDB_LOG(log, "Rendezvous structure is not set up yet. "
                  "Trying to locate rendezvous breakpoint in the interpreter "
                  "by symbol name.");
    // Function names from different dynamic loaders that are known to be
    // used as rendezvous between the loader and debuggers.
    static std::vector<std::string> DebugStateCandidates{
        "_dl_debug_state", "rtld_db_dlactivity", "__dl_rtld_db_dlactivity",
        "r_debug_state",   "_r_debug_state",     "_rtld_debug_state",
    };

    ModuleSP interpreter = LoadInterpreterModule();
    FileSpecList containingModules;
    if (interpreter)
      containingModules.Append(interpreter->GetFileSpec());
    else
      containingModules.Append(
          m_process->GetTarget().GetExecutableModulePointer()->GetFileSpec());

    dyld_break = target.CreateBreakpoint(
        &containingModules, /*containingSourceFiles=*/nullptr,
        DebugStateCandidates, eFunctionNameTypeFull, eLanguageTypeC,
        /*m_offset=*/0,
        /*skip_prologue=*/eLazyBoolNo,
        /*internal=*/true,
        /*request_hardware=*/false);
  }

  if (dyld_break->GetNumResolvedLocations() != 1) {
    LLDB_LOG(
        log,
        "Rendezvous breakpoint has abnormal number of"
        " resolved locations ({0}) in pid {1}. It's supposed to be exactly 1.",
        dyld_break->GetNumResolvedLocations(),
        m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);

    target.RemoveBreakpointByID(dyld_break->GetID());
    return false;
  }

  BreakpointLocationSP location = dyld_break->GetLocationAtIndex(0);
  LLDB_LOG(log,
           "Successfully set rendezvous breakpoint at address {0:x} "
           "for pid {1}",
           location->GetLoadAddress(),
           m_process ? m_process->GetID() : LLDB_INVALID_PROCESS_ID);

  dyld_break->SetCallback(RendezvousBreakpointHit, this, true);
  dyld_break->SetBreakpointKind("shared-library-event");
  m_dyld_bid = dyld_break->GetID();
  return true;
}

bool DynamicLoaderPOSIXDYLD::RendezvousBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  Log *log = GetLog(LLDBLog::DynamicLoader);
  DynamicLoaderPOSIXDYLD *const dyld_instance =
      static_cast<DynamicLoaderPOSIXDYLD *>(baton);
  LLDB_LOGF(log, "DynamicLoaderPOSIXDYLD::%s called for pid %" PRIu64,
            __FUNCTION__,
            dyld_instance->m_process ? dyld_instance->m_process->GetID()
                                     : LLDB_INVALID_PROCESS_ID);

  dyld_instance->RefreshModules();

  // Return true to stop the target, false to just let the target run.
  const bool stop_when_images_change = dyld_instance->GetStopWhenImagesChange();
  LLDB_LOGF(log,
            "DynamicLoaderPOSIXDYLD::%s pid %" PRIu64
            " stop_when_images_change=%s",
            __FUNCTION__,
            dyld_instance->m_process ? dyld_instance->m_process->GetID()
                                     : LLDB_INVALID_PROCESS_ID,
            stop_when_images_change ? "true" : "false");
  return stop_when_images_change;
}

void DynamicLoaderPOSIXDYLD::RefreshModules() {
  if (!m_rendezvous.Resolve())
    return;

  // The rendezvous class doesn't enumerate the main module, so track that
  // ourselves here.
  ModuleSP executable = GetTargetExecutable();
  m_loaded_modules[executable] = m_rendezvous.GetLinkMapAddress();

  DYLDRendezvous::iterator I;
  DYLDRendezvous::iterator E;

  ModuleList &loaded_modules = m_process->GetTarget().GetImages();

  if (m_rendezvous.ModulesDidLoad() || !m_initial_modules_added) {
    ModuleList new_modules;

    // If this is the first time rendezvous breakpoint fires, we need
    // to take care of adding all the initial modules reported by
    // the loader.  This is necessary to list ld-linux.so on Linux,
    // and all DT_NEEDED entries on *BSD.
    if (m_initial_modules_added) {
      I = m_rendezvous.loaded_begin();
      E = m_rendezvous.loaded_end();
    } else {
      I = m_rendezvous.begin();
      E = m_rendezvous.end();
      m_initial_modules_added = true;
    }
    for (; I != E; ++I) {
      // Don't load a duplicate copy of ld.so if we have already loaded it
      // earlier in LoadInterpreterModule. If we instead loaded then unloaded it
      // later, the section information for ld.so would be removed. That
      // information is required for placing breakpoints on Arm/Thumb systems.
      if ((m_interpreter_module.lock() != nullptr) &&
          (I->base_addr == m_interpreter_base))
        continue;

      ModuleSP module_sp =
          LoadModuleAtAddress(I->file_spec, I->link_addr, I->base_addr, true);
      if (!module_sp.get())
        continue;

      if (module_sp->GetObjectFile()->GetBaseAddress().GetLoadAddress(
              &m_process->GetTarget()) == m_interpreter_base) {
        ModuleSP interpreter_sp = m_interpreter_module.lock();
        if (m_interpreter_module.lock() == nullptr) {
          m_interpreter_module = module_sp;
        } else if (module_sp == interpreter_sp) {
          // Module already loaded.
          continue;
        }
      }

      loaded_modules.AppendIfNeeded(module_sp);
      new_modules.Append(module_sp);
    }
    m_process->GetTarget().ModulesDidLoad(new_modules);
  }

  if (m_rendezvous.ModulesDidUnload()) {
    ModuleList old_modules;

    E = m_rendezvous.unloaded_end();
    for (I = m_rendezvous.unloaded_begin(); I != E; ++I) {
      ModuleSpec module_spec{I->file_spec};
      ModuleSP module_sp = loaded_modules.FindFirstModule(module_spec);

      if (module_sp.get()) {
        old_modules.Append(module_sp);
        UnloadSections(module_sp);
      }
    }
    loaded_modules.Remove(old_modules);
    m_process->GetTarget().ModulesDidUnload(old_modules, false);
  }
}

ThreadPlanSP
DynamicLoaderPOSIXDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                     bool stop) {
  ThreadPlanSP thread_plan_sp;

  StackFrame *frame = thread.GetStackFrameAtIndex(0).get();
  const SymbolContext &context = frame->GetSymbolContext(eSymbolContextSymbol);
  Symbol *sym = context.symbol;

  if (sym == nullptr || !sym->IsTrampoline())
    return thread_plan_sp;

  ConstString sym_name = sym->GetMangled().GetName(Mangled::ePreferMangled);
  if (!sym_name)
    return thread_plan_sp;

  SymbolContextList target_symbols;
  Target &target = thread.GetProcess()->GetTarget();
  const ModuleList &images = target.GetImages();

  llvm::StringRef target_name = sym_name.GetStringRef();
  // On AArch64, the trampoline name has a prefix (__AArch64ADRPThunk_ or
  // __AArch64AbsLongThunk_) added to the function name. If we detect a
  // trampoline with the prefix, we need to remove the prefix to find the
  // function symbol.
  if (target_name.consume_front("__AArch64ADRPThunk_") ||
      target_name.consume_front("__AArch64AbsLongThunk_")) {
    // An empty target name can happen for trampolines generated for
    // section-referencing relocations.
    if (!target_name.empty()) {
      sym_name = ConstString(target_name);
    }
  }
  images.FindSymbolsWithNameAndType(sym_name, eSymbolTypeCode, target_symbols);
  if (!target_symbols.GetSize())
    return thread_plan_sp;

  typedef std::vector<lldb::addr_t> AddressVector;
  AddressVector addrs;
  for (const SymbolContext &context : target_symbols) {
    AddressRange range;
    context.GetAddressRange(eSymbolContextEverything, 0, false, range);
    lldb::addr_t addr = range.GetBaseAddress().GetLoadAddress(&target);
    if (addr != LLDB_INVALID_ADDRESS)
      addrs.push_back(addr);
  }

  if (addrs.size() > 0) {
    AddressVector::iterator start = addrs.begin();
    AddressVector::iterator end = addrs.end();

    llvm::sort(start, end);
    addrs.erase(std::unique(start, end), end);
    thread_plan_sp =
        std::make_shared<ThreadPlanRunToAddress>(thread, addrs, stop);
  }

  return thread_plan_sp;
}

void DynamicLoaderPOSIXDYLD::LoadVDSO() {
  if (m_vdso_base == LLDB_INVALID_ADDRESS)
    return;

  FileSpec file("[vdso]");

  MemoryRegionInfo info;
  Status status = m_process->GetMemoryRegionInfo(m_vdso_base, info);
  if (status.Fail()) {
    Log *log = GetLog(LLDBLog::DynamicLoader);
    LLDB_LOG(log, "Failed to get vdso region info: {0}", status);
    return;
  }

  if (ModuleSP module_sp = m_process->ReadModuleFromMemory(
          file, m_vdso_base, info.GetRange().GetByteSize())) {
    UpdateLoadedSections(module_sp, LLDB_INVALID_ADDRESS, m_vdso_base, false);
    m_process->GetTarget().GetImages().AppendIfNeeded(module_sp);
  }
}

ModuleSP DynamicLoaderPOSIXDYLD::LoadInterpreterModule() {
  if (m_interpreter_base == LLDB_INVALID_ADDRESS)
    return nullptr;

  MemoryRegionInfo info;
  Target &target = m_process->GetTarget();
  Status status = m_process->GetMemoryRegionInfo(m_interpreter_base, info);
  if (status.Fail() || info.GetMapped() != MemoryRegionInfo::eYes ||
      info.GetName().IsEmpty()) {
    Log *log = GetLog(LLDBLog::DynamicLoader);
    LLDB_LOG(log, "Failed to get interpreter region info: {0}", status);
    return nullptr;
  }

  FileSpec file(info.GetName().GetCString());
  ModuleSpec module_spec(file, target.GetArchitecture());

  // Don't notify that module is added here because its loading section
  // addresses are not updated yet. We manually notify it below.
  if (ModuleSP module_sp =
          target.GetOrCreateModule(module_spec, /*notify=*/false)) {
    UpdateLoadedSections(module_sp, LLDB_INVALID_ADDRESS, m_interpreter_base,
                         false);
    // Manually notify that dynamic linker is loaded after updating load section
    // addersses so that breakpoints can be resolved.
    ModuleList module_list;
    module_list.Append(module_sp);
    target.ModulesDidLoad(module_list);
    m_interpreter_module = module_sp;
    return module_sp;
  }
  return nullptr;
}

ModuleSP DynamicLoaderPOSIXDYLD::LoadModuleAtAddress(const FileSpec &file,
                                                     addr_t link_map_addr,
                                                     addr_t base_addr,
                                                     bool base_addr_is_offset) {
  if (ModuleSP module_sp = DynamicLoader::LoadModuleAtAddress(
          file, link_map_addr, base_addr, base_addr_is_offset))
    return module_sp;

  // This works around an dynamic linker "bug" on android <= 23, where the
  // dynamic linker would report the application name
  // (e.g. com.example.myapplication) instead of the main process binary
  // (/system/bin/app_process(32)). The logic is not sound in general (it
  // assumes base_addr is the real address, even though it actually is a load
  // bias), but it happens to work on android because app_process has a file
  // address of zero.
  // This should be removed after we drop support for android-23.
  if (m_process->GetTarget().GetArchitecture().GetTriple().isAndroid()) {
    MemoryRegionInfo memory_info;
    Status error = m_process->GetMemoryRegionInfo(base_addr, memory_info);
    if (error.Success() && memory_info.GetMapped() &&
        memory_info.GetRange().GetRangeBase() == base_addr &&
        !(memory_info.GetName().IsEmpty())) {
      if (ModuleSP module_sp = DynamicLoader::LoadModuleAtAddress(
              FileSpec(memory_info.GetName().GetStringRef()), link_map_addr,
              base_addr, base_addr_is_offset))
        return module_sp;
    }
  }

  return nullptr;
}

void DynamicLoaderPOSIXDYLD::LoadAllCurrentModules() {
  DYLDRendezvous::iterator I;
  DYLDRendezvous::iterator E;
  ModuleList module_list;
  Log *log = GetLog(LLDBLog::DynamicLoader);

  LoadVDSO();

  if (!m_rendezvous.Resolve()) {
    LLDB_LOGF(log,
              "DynamicLoaderPOSIXDYLD::%s unable to resolve POSIX DYLD "
              "rendezvous address",
              __FUNCTION__);
    return;
  }

  // The rendezvous class doesn't enumerate the main module, so track that
  // ourselves here.
  ModuleSP executable = GetTargetExecutable();
  m_loaded_modules[executable] = m_rendezvous.GetLinkMapAddress();

  std::vector<FileSpec> module_names;
  for (I = m_rendezvous.begin(), E = m_rendezvous.end(); I != E; ++I)
    module_names.push_back(I->file_spec);
  m_process->PrefetchModuleSpecs(
      module_names, m_process->GetTarget().GetArchitecture().GetTriple());

  for (I = m_rendezvous.begin(), E = m_rendezvous.end(); I != E; ++I) {
    ModuleSP module_sp =
        LoadModuleAtAddress(I->file_spec, I->link_addr, I->base_addr, true);
    if (module_sp.get()) {
      LLDB_LOG(log, "LoadAllCurrentModules loading module: {0}",
               I->file_spec.GetFilename());
      module_list.Append(module_sp);
    } else {
      Log *log = GetLog(LLDBLog::DynamicLoader);
      LLDB_LOGF(
          log,
          "DynamicLoaderPOSIXDYLD::%s failed loading module %s at 0x%" PRIx64,
          __FUNCTION__, I->file_spec.GetPath().c_str(), I->base_addr);
    }
  }

  m_process->GetTarget().ModulesDidLoad(module_list);
  m_initial_modules_added = true;
}

addr_t DynamicLoaderPOSIXDYLD::ComputeLoadOffset() {
  addr_t virt_entry;

  if (m_load_offset != LLDB_INVALID_ADDRESS)
    return m_load_offset;

  if ((virt_entry = GetEntryPoint()) == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  ModuleSP module = m_process->GetTarget().GetExecutableModule();
  if (!module)
    return LLDB_INVALID_ADDRESS;

  ObjectFile *exe = module->GetObjectFile();
  if (!exe)
    return LLDB_INVALID_ADDRESS;

  Address file_entry = exe->GetEntryPointAddress();

  if (!file_entry.IsValid())
    return LLDB_INVALID_ADDRESS;

  m_load_offset = virt_entry - file_entry.GetFileAddress();
  return m_load_offset;
}

void DynamicLoaderPOSIXDYLD::EvalSpecialModulesStatus() {
  if (std::optional<uint64_t> vdso_base =
          m_auxv->GetAuxValue(AuxVector::AUXV_AT_SYSINFO_EHDR))
    m_vdso_base = *vdso_base;

  if (std::optional<uint64_t> interpreter_base =
          m_auxv->GetAuxValue(AuxVector::AUXV_AT_BASE))
    m_interpreter_base = *interpreter_base;
}

addr_t DynamicLoaderPOSIXDYLD::GetEntryPoint() {
  if (m_entry_point != LLDB_INVALID_ADDRESS)
    return m_entry_point;

  if (m_auxv == nullptr)
    return LLDB_INVALID_ADDRESS;

  std::optional<uint64_t> entry_point =
      m_auxv->GetAuxValue(AuxVector::AUXV_AT_ENTRY);
  if (!entry_point)
    return LLDB_INVALID_ADDRESS;

  m_entry_point = static_cast<addr_t>(*entry_point);

  const ArchSpec &arch = m_process->GetTarget().GetArchitecture();

  // On ppc64, the entry point is actually a descriptor.  Dereference it.
  if (arch.GetMachine() == llvm::Triple::ppc64)
    m_entry_point = ReadUnsignedIntWithSizeInBytes(m_entry_point, 8);

  return m_entry_point;
}

lldb::addr_t
DynamicLoaderPOSIXDYLD::GetThreadLocalData(const lldb::ModuleSP module_sp,
                                           const lldb::ThreadSP thread,
                                           lldb::addr_t tls_file_addr) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  auto it = m_loaded_modules.find(module_sp);
  if (it == m_loaded_modules.end()) {
    LLDB_LOGF(
        log, "GetThreadLocalData error: module(%s) not found in loaded modules",
        module_sp->GetObjectName().AsCString());
    return LLDB_INVALID_ADDRESS;
  }

  addr_t link_map = it->second;
  if (link_map == LLDB_INVALID_ADDRESS || link_map == 0) {
    LLDB_LOGF(log,
              "GetThreadLocalData error: invalid link map address=0x%" PRIx64,
              link_map);
    return LLDB_INVALID_ADDRESS;
  }

  const DYLDRendezvous::ThreadInfo &metadata = m_rendezvous.GetThreadInfo();
  if (!metadata.valid) {
    LLDB_LOGF(log,
              "GetThreadLocalData error: fail to read thread info metadata");
    return LLDB_INVALID_ADDRESS;
  }

  LLDB_LOGF(log,
            "GetThreadLocalData info: link_map=0x%" PRIx64
            ", thread info metadata: "
            "modid_offset=0x%" PRIx32 ", dtv_offset=0x%" PRIx32
            ", tls_offset=0x%" PRIx32 ", dtv_slot_size=%" PRIx32 "\n",
            link_map, metadata.modid_offset, metadata.dtv_offset,
            metadata.tls_offset, metadata.dtv_slot_size);

  // Get the thread pointer.
  addr_t tp = thread->GetThreadPointer();
  if (tp == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log, "GetThreadLocalData error: fail to read thread pointer");
    return LLDB_INVALID_ADDRESS;
  }

  // Find the module's modid.
  int modid_size = 4; // FIXME(spucci): This isn't right for big-endian 64-bit
  int64_t modid = ReadUnsignedIntWithSizeInBytes(
      link_map + metadata.modid_offset, modid_size);
  if (modid == -1) {
    LLDB_LOGF(log, "GetThreadLocalData error: fail to read modid");
    return LLDB_INVALID_ADDRESS;
  }

  // Lookup the DTV structure for this thread.
  addr_t dtv_ptr = tp + metadata.dtv_offset;
  addr_t dtv = ReadPointer(dtv_ptr);
  if (dtv == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log, "GetThreadLocalData error: fail to read dtv");
    return LLDB_INVALID_ADDRESS;
  }

  // Find the TLS block for this module.
  addr_t dtv_slot = dtv + metadata.dtv_slot_size * modid;
  addr_t tls_block = ReadPointer(dtv_slot + metadata.tls_offset);

  LLDB_LOGF(log,
            "DynamicLoaderPOSIXDYLD::Performed TLS lookup: "
            "module=%s, link_map=0x%" PRIx64 ", tp=0x%" PRIx64
            ", modid=%" PRId64 ", tls_block=0x%" PRIx64 "\n",
            module_sp->GetObjectName().AsCString(""), link_map, tp,
            (int64_t)modid, tls_block);

  if (tls_block == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log, "GetThreadLocalData error: fail to read tls_block");
    return LLDB_INVALID_ADDRESS;
  } else
    return tls_block + tls_file_addr;
}

void DynamicLoaderPOSIXDYLD::ResolveExecutableModule(
    lldb::ModuleSP &module_sp) {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  if (m_process == nullptr)
    return;

  auto &target = m_process->GetTarget();
  const auto platform_sp = target.GetPlatform();

  ProcessInstanceInfo process_info;
  if (!m_process->GetProcessInfo(process_info)) {
    LLDB_LOGF(log,
              "DynamicLoaderPOSIXDYLD::%s - failed to get process info for "
              "pid %" PRIu64,
              __FUNCTION__, m_process->GetID());
    return;
  }

  LLDB_LOGF(
      log, "DynamicLoaderPOSIXDYLD::%s - got executable by pid %" PRIu64 ": %s",
      __FUNCTION__, m_process->GetID(),
      process_info.GetExecutableFile().GetPath().c_str());

  ModuleSpec module_spec(process_info.GetExecutableFile(),
                         process_info.GetArchitecture());
  if (module_sp && module_sp->MatchesModuleSpec(module_spec))
    return;

  const auto executable_search_paths(Target::GetDefaultExecutableSearchPaths());
  auto error = platform_sp->ResolveExecutable(
      module_spec, module_sp,
      !executable_search_paths.IsEmpty() ? &executable_search_paths : nullptr);
  if (error.Fail()) {
    StreamString stream;
    module_spec.Dump(stream);

    LLDB_LOGF(log,
              "DynamicLoaderPOSIXDYLD::%s - failed to resolve executable "
              "with module spec \"%s\": %s",
              __FUNCTION__, stream.GetData(), error.AsCString());
    return;
  }

  target.SetExecutableModule(module_sp, eLoadDependentsNo);
}

bool DynamicLoaderPOSIXDYLD::AlwaysRelyOnEHUnwindInfo(
    lldb_private::SymbolContext &sym_ctx) {
  ModuleSP module_sp;
  if (sym_ctx.symbol)
    module_sp = sym_ctx.symbol->GetAddressRef().GetModule();
  if (!module_sp && sym_ctx.function)
    module_sp =
        sym_ctx.function->GetAddressRange().GetBaseAddress().GetModule();
  if (!module_sp)
    return false;

  return module_sp->GetFileSpec().GetPath() == "[vdso]";
}

bool DynamicLoaderPOSIXDYLD::IsCoreFile() const {
  return !m_process->IsLiveDebugSession();
}
