//===-- DYLDRendezvous.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/Path.h"

#include "DYLDRendezvous.h"

using namespace lldb;
using namespace lldb_private;

const char *DYLDRendezvous::StateToCStr(RendezvousState state) {
  switch (state) {
    case DYLDRendezvous::eConsistent:
      return "eConsistent";
    case DYLDRendezvous::eAdd:
      return "eAdd";
    case DYLDRendezvous::eDelete:
      return "eDelete";
  }
  return "<invalid RendezvousState>";
}

const char *DYLDRendezvous::ActionToCStr(RendezvousAction action) {
  switch (action) {
  case DYLDRendezvous::RendezvousAction::eTakeSnapshot:
    return "eTakeSnapshot";
  case DYLDRendezvous::RendezvousAction::eAddModules:
    return "eAddModules";
  case DYLDRendezvous::RendezvousAction::eRemoveModules:
    return "eRemoveModules";
  case DYLDRendezvous::RendezvousAction::eNoAction:
    return "eNoAction";
  }
  return "<invalid RendezvousAction>";
}

DYLDRendezvous::DYLDRendezvous(Process *process)
    : m_process(process), m_rendezvous_addr(LLDB_INVALID_ADDRESS),
      m_executable_interpreter(false), m_current(), m_previous(),
      m_loaded_modules(), m_soentries(), m_added_soentries(),
      m_removed_soentries() {
  m_thread_info.valid = false;
  UpdateExecutablePath();
}

addr_t DYLDRendezvous::ResolveRendezvousAddress() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  addr_t info_location;
  addr_t info_addr;
  Status error;

  if (!m_process) {
    LLDB_LOGF(log, "%s null process provided", __FUNCTION__);
    return LLDB_INVALID_ADDRESS;
  }

  // Try to get it from our process.  This might be a remote process and might
  // grab it via some remote-specific mechanism.
  info_location = m_process->GetImageInfoAddress();
  LLDB_LOGF(log, "%s info_location = 0x%" PRIx64, __FUNCTION__, info_location);

  // If the process fails to return an address, fall back to seeing if the
  // local object file can help us find it.
  if (info_location == LLDB_INVALID_ADDRESS) {
    Target *target = &m_process->GetTarget();
    if (target) {
      ObjectFile *obj_file = target->GetExecutableModule()->GetObjectFile();
      Address addr = obj_file->GetImageInfoAddress(target);

      if (addr.IsValid()) {
        info_location = addr.GetLoadAddress(target);
        LLDB_LOGF(log,
                  "%s resolved via direct object file approach to 0x%" PRIx64,
                  __FUNCTION__, info_location);
      } else {
        const Symbol *_r_debug =
            target->GetExecutableModule()->FindFirstSymbolWithNameAndType(
                ConstString("_r_debug"));
        if (_r_debug) {
          info_addr = _r_debug->GetAddress().GetLoadAddress(target);
          if (info_addr != LLDB_INVALID_ADDRESS) {
            LLDB_LOGF(log,
                      "%s resolved by finding symbol '_r_debug' whose value is "
                      "0x%" PRIx64,
                      __FUNCTION__, info_addr);
            m_executable_interpreter = true;
            return info_addr;
          }
        }
        LLDB_LOGF(log,
                  "%s FAILED - direct object file approach did not yield a "
                  "valid address",
                  __FUNCTION__);
      }
    }
  }

  if (info_location == LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log, "%s FAILED - invalid info address", __FUNCTION__);
    return LLDB_INVALID_ADDRESS;
  }

  LLDB_LOGF(log, "%s reading pointer (%" PRIu32 " bytes) from 0x%" PRIx64,
            __FUNCTION__, m_process->GetAddressByteSize(), info_location);

  info_addr = m_process->ReadPointerFromMemory(info_location, error);
  if (error.Fail()) {
    LLDB_LOGF(log, "%s FAILED - could not read from the info location: %s",
              __FUNCTION__, error.AsCString());
    return LLDB_INVALID_ADDRESS;
  }

  if (info_addr == 0) {
    LLDB_LOGF(log,
              "%s FAILED - the rendezvous address contained at 0x%" PRIx64
              " returned a null value",
              __FUNCTION__, info_location);
    return LLDB_INVALID_ADDRESS;
  }

  return info_addr;
}

void DYLDRendezvous::UpdateExecutablePath() {
  if (m_process) {
    Log *log = GetLog(LLDBLog::DynamicLoader);
    Module *exe_mod = m_process->GetTarget().GetExecutableModulePointer();
    if (exe_mod) {
      m_exe_file_spec = exe_mod->GetPlatformFileSpec();
      LLDB_LOGF(log, "DYLDRendezvous::%s exe module executable path set: '%s'",
                __FUNCTION__, m_exe_file_spec.GetPath().c_str());
    } else {
      LLDB_LOGF(log,
                "DYLDRendezvous::%s cannot cache exe module path: null "
                "executable module pointer",
                __FUNCTION__);
    }
  }
}

void DYLDRendezvous::Rendezvous::DumpToLog(Log *log, const char *label) {
  LLDB_LOGF(log, "%s Rendezvous: version = %" PRIu64 ", map_addr = 0x%16.16"
            PRIx64 ", brk = 0x%16.16" PRIx64 ", state = %" PRIu64
            " (%s), ldbase = 0x%16.16" PRIx64, label ? label : "", version,
            map_addr, brk, state, StateToCStr((RendezvousState)state), ldbase);
}

bool DYLDRendezvous::Resolve() {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  const size_t word_size = 4;
  Rendezvous info;
  size_t address_size;
  size_t padding;
  addr_t info_addr;
  addr_t cursor;

  address_size = m_process->GetAddressByteSize();
  padding = address_size - word_size;
  LLDB_LOGF(log,
            "DYLDRendezvous::%s address size: %" PRIu64 ", padding %" PRIu64,
            __FUNCTION__, uint64_t(address_size), uint64_t(padding));

  if (m_rendezvous_addr == LLDB_INVALID_ADDRESS)
    cursor = info_addr =
        ResolveRendezvousAddress();
  else
    cursor = info_addr = m_rendezvous_addr;
  LLDB_LOGF(log, "DYLDRendezvous::%s cursor = 0x%" PRIx64, __FUNCTION__,
            cursor);

  if (cursor == LLDB_INVALID_ADDRESS)
    return false;

  if (!(cursor = ReadWord(cursor, &info.version, word_size)))
    return false;

  if (!(cursor = ReadPointer(cursor + padding, &info.map_addr)))
    return false;

  if (!(cursor = ReadPointer(cursor, &info.brk)))
    return false;

  if (!(cursor = ReadWord(cursor, &info.state, word_size)))
    return false;

  if (!(cursor = ReadPointer(cursor + padding, &info.ldbase)))
    return false;

  // The rendezvous was successfully read.  Update our internal state.
  m_rendezvous_addr = info_addr;
  m_previous = m_current;
  m_current = info;

  m_previous.DumpToLog(log, "m_previous");
  m_current.DumpToLog(log, "m_current ");

  if (m_current.map_addr == 0)
    return false;

  if (UpdateSOEntriesFromRemote())
    return true;

  return UpdateSOEntries();
}

bool DYLDRendezvous::IsValid() {
  return m_rendezvous_addr != LLDB_INVALID_ADDRESS;
}

DYLDRendezvous::RendezvousAction DYLDRendezvous::GetAction() const {
  // If we have a core file, we will read the current rendezvous state
  // from the core file's memory into m_current which can be in an inconsistent
  // state, so we can't rely on its state to determine what we should do. We
  // always need it to load all of the shared libraries one time when we attach
  // to a core file.
  if (IsCoreFile())
    return eTakeSnapshot;

  switch (m_current.state) {

  case eConsistent:
    switch (m_previous.state) {
    // When the previous and current states are consistent this is the first
    // time we have been asked to update.  Just take a snapshot of the
    // currently loaded modules.
    case eConsistent:
      return eTakeSnapshot;
    // If we are about to add or remove a shared object clear out the current
    // state and take a snapshot of the currently loaded images.
    case eAdd:
      return eAddModules;
    case eDelete:
      return eRemoveModules;
    }
    break;

  case eAdd:
    // If the main executable or a shared library defines a publicly visible
    // symbol named "_r_debug", then it will cause problems once the executable
    // that contains the symbol is loaded into the process. The correct
    // "_r_debug" structure is currently found by LLDB by looking through
    // the .dynamic section in the main executable and finding the DT_DEBUG tag
    // entry.
    //
    // An issue comes up if someone defines another publicly visible "_r_debug"
    // struct in their program. Sample code looks like:
    //
    //    #include <link.h>
    //    r_debug _r_debug;
    //
    // If code like this is in an executable or shared library, this creates a
    // new "_r_debug" structure and it causes problems once the executable is
    // loaded due to the way symbol lookups happen in linux: the shared library
    // list from _r_debug.r_map will be searched for a symbol named "_r_debug"
    // and the first match will be the new version that is used. The dynamic
    // loader is always last in this list. So at some point the dynamic loader
    // will start updating the copy of "_r_debug" that gets found first. The
    // issue is that LLDB will only look at the copy that is pointed to by the
    // DT_DEBUG entry, or the initial version from the ld.so binary.
    //
    // Steps that show the problem are:
    //
    // - LLDB finds the "_r_debug" structure via the DT_DEBUG entry in the
    //   .dynamic section and this points to the "_r_debug" in ld.so
    // - ld.so uodates its copy of "_r_debug" with "state = eAdd" before it
    //   loads the dependent shared libraries for the main executable and
    //   any dependencies of all shared libraries from the executable's list
    //   and ld.so code calls the debugger notification function
    //   that LLDB has set a breakpoint on.
    // - LLDB hits the breakpoint and the breakpoint has a callback function
    //   where we read the _r_debug.state (eAdd) state and we do nothing as the
    //   "eAdd" state indicates that the shared libraries are about to be added.
    // - ld.so finishes loading the main executable and any dependent shared
    //   libraries and it will update the "_r_debug.state" member with a
    //   "eConsistent", but it now updates the "_r_debug" in the a.out program
    //   and it calls the debugger notification function.
    // - lldb hits the notification breakpoint and checks the ld.so copy of
    //   "_r_debug.state" which still has a state of "eAdd", but LLDB needs to see a
    //   "eConsistent" state to trigger the shared libraries to get loaded into
    //   the debug session, but LLDB the ld.so _r_debug.state which still
    //   contains "eAdd" and doesn't do anyhing and library load is missed.
    //   The "_r_debug" in a.out has the state set correctly to "eConsistent"
    //   but LLDB is still looking at the "_r_debug" from ld.so.
    //
    // So if we detect two "eAdd" states in a row, we assume this is the issue
    // and we now load shared libraries correctly and will emit a log message
    // in the "log enable lldb dyld" log channel which states there might be
    // multiple "_r_debug" structs causing problems.
    //
    // The correct solution is that no one should be adding a duplicate
    // publicly visible "_r_debug" symbols to their binaries, but we have
    // programs that are doing this already and since it can be done, we should
    // be able to work with this and keep debug sessions working as expected.
    //
    // If a user includes the <link.h> file, they can just use the existing
    // "_r_debug" structure as it is defined in this header file as "extern
    // struct r_debug _r_debug;" and no local copies need to be made.
    if (m_previous.state == eAdd) {
      Log *log = GetLog(LLDBLog::DynamicLoader);
      LLDB_LOG(log, "DYLDRendezvous::GetAction() found two eAdd states in a "
               "row, check process for multiple \"_r_debug\" symbols. "
               "Returning eAddModules to ensure shared libraries get loaded "
               "correctly");
      return eAddModules;
    }
    return eNoAction;
  case eDelete:
    return eNoAction;
  }

  return eNoAction;
}

bool DYLDRendezvous::UpdateSOEntriesFromRemote() {
  const auto action = GetAction();
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOG(log, "{0} action = {1}", LLVM_PRETTY_FUNCTION, ActionToCStr(action));

  if (action == eNoAction)
    return false;

  m_added_soentries.clear();
  m_removed_soentries.clear();
  if (action == eTakeSnapshot) {
    // We already have the loaded list from the previous update so no need to
    // find all the modules again.
    if (!m_loaded_modules.m_list.empty())
      return true;
  }

  llvm::Expected<LoadedModuleInfoList> module_list =
      m_process->GetLoadedModuleList();
  if (!module_list) {
    llvm::consumeError(module_list.takeError());
    return false;
  }

  switch (action) {
  case eTakeSnapshot:
    m_soentries.clear();
    return SaveSOEntriesFromRemote(*module_list);
  case eAddModules:
    return AddSOEntriesFromRemote(*module_list);
  case eRemoveModules:
    return RemoveSOEntriesFromRemote(*module_list);
  case eNoAction:
    return false;
  }
  llvm_unreachable("Fully covered switch above!");
}

bool DYLDRendezvous::UpdateSOEntries() {
  m_added_soentries.clear();
  m_removed_soentries.clear();
  const auto action = GetAction();
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOG(log, "{0} action = {1}", LLVM_PRETTY_FUNCTION, ActionToCStr(action));
  switch (action) {
  case eTakeSnapshot:
    m_soentries.clear();
    return TakeSnapshot(m_soentries);
  case eAddModules:
    return AddSOEntries();
  case eRemoveModules:
    return RemoveSOEntries();
  case eNoAction:
    return false;
  }
  llvm_unreachable("Fully covered switch above!");
}

bool DYLDRendezvous::FillSOEntryFromModuleInfo(
    LoadedModuleInfoList::LoadedModuleInfo const &modInfo, SOEntry &entry) {
  addr_t link_map_addr;
  addr_t base_addr;
  addr_t dyn_addr;
  std::string name;

  if (!modInfo.get_link_map(link_map_addr) || !modInfo.get_base(base_addr) ||
      !modInfo.get_dynamic(dyn_addr) || !modInfo.get_name(name))
    return false;

  entry.link_addr = link_map_addr;
  entry.base_addr = base_addr;
  entry.dyn_addr = dyn_addr;

  entry.file_spec.SetFile(name, FileSpec::Style::native);

  UpdateBaseAddrIfNecessary(entry, name);

  // not needed if we're using ModuleInfos
  entry.next = 0;
  entry.prev = 0;
  entry.path_addr = 0;

  return true;
}

bool DYLDRendezvous::SaveSOEntriesFromRemote(
    const LoadedModuleInfoList &module_list) {
  for (auto const &modInfo : module_list.m_list) {
    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(modInfo, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry)) {
      UpdateFileSpecIfNecessary(entry);
      m_soentries.push_back(entry);
    }
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::AddSOEntriesFromRemote(
    const LoadedModuleInfoList &module_list) {
  for (auto const &modInfo : module_list.m_list) {
    bool found = false;
    for (auto const &existing : m_loaded_modules.m_list) {
      if (modInfo == existing) {
        found = true;
        break;
      }
    }

    if (found)
      continue;

    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(modInfo, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry)) {
      UpdateFileSpecIfNecessary(entry);
      m_soentries.push_back(entry);
      m_added_soentries.push_back(entry);
    }
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::RemoveSOEntriesFromRemote(
    const LoadedModuleInfoList &module_list) {
  for (auto const &existing : m_loaded_modules.m_list) {
    bool found = false;
    for (auto const &modInfo : module_list.m_list) {
      if (modInfo == existing) {
        found = true;
        break;
      }
    }

    if (found)
      continue;

    SOEntry entry;
    if (!FillSOEntryFromModuleInfo(existing, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (!SOEntryIsMainExecutable(entry)) {
      auto pos = llvm::find(m_soentries, entry);
      if (pos == m_soentries.end())
        return false;

      m_soentries.erase(pos);
      m_removed_soentries.push_back(entry);
    }
  }

  m_loaded_modules = module_list;
  return true;
}

bool DYLDRendezvous::AddSOEntries() {
  SOEntry entry;
  iterator pos;

  assert(m_previous.state == eAdd);

  if (m_current.map_addr == 0)
    return false;

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (SOEntryIsMainExecutable(entry))
      continue;

    UpdateFileSpecIfNecessary(entry);

    if (!llvm::is_contained(m_soentries, entry)) {
      m_soentries.push_back(entry);
      m_added_soentries.push_back(entry);
    }
  }

  return true;
}

bool DYLDRendezvous::RemoveSOEntries() {
  SOEntryList entry_list;
  iterator pos;

  assert(m_previous.state == eDelete);

  if (!TakeSnapshot(entry_list))
    return false;

  for (iterator I = begin(); I != end(); ++I) {
    if (!llvm::is_contained(entry_list, *I))
      m_removed_soentries.push_back(*I);
  }

  m_soentries = entry_list;
  return true;
}

bool DYLDRendezvous::SOEntryIsMainExecutable(const SOEntry &entry) {
  // On some systes the executable is indicated by an empty path in the entry.
  // On others it is the full path to the executable.

  auto triple = m_process->GetTarget().GetArchitecture().GetTriple();
  switch (triple.getOS()) {
  case llvm::Triple::FreeBSD:
  case llvm::Triple::NetBSD:
  case llvm::Triple::OpenBSD:
    return entry.file_spec == m_exe_file_spec;
  case llvm::Triple::Linux:
    if (triple.isAndroid())
      return entry.file_spec == m_exe_file_spec;
    // If we are debugging ld.so, then all SOEntries should be treated as
    // libraries, including the "main" one (denoted by an empty string).
    if (!entry.file_spec && m_executable_interpreter)
      return false;
    return !entry.file_spec;
  default:
    return false;
  }
}

bool DYLDRendezvous::TakeSnapshot(SOEntryList &entry_list) {
  SOEntry entry;

  if (m_current.map_addr == 0)
    return false;

  // Clear previous entries since we are about to obtain an up to date list.
  entry_list.clear();

  for (addr_t cursor = m_current.map_addr; cursor != 0; cursor = entry.next) {
    if (!ReadSOEntryFromMemory(cursor, entry))
      return false;

    // Only add shared libraries and not the executable.
    if (SOEntryIsMainExecutable(entry))
      continue;

    UpdateFileSpecIfNecessary(entry);

    entry_list.push_back(entry);
  }

  return true;
}

addr_t DYLDRendezvous::ReadWord(addr_t addr, uint64_t *dst, size_t size) {
  Status error;

  *dst = m_process->ReadUnsignedIntegerFromMemory(addr, size, 0, error);
  if (error.Fail())
    return 0;

  return addr + size;
}

addr_t DYLDRendezvous::ReadPointer(addr_t addr, addr_t *dst) {
  Status error;

  *dst = m_process->ReadPointerFromMemory(addr, error);
  if (error.Fail())
    return 0;

  return addr + m_process->GetAddressByteSize();
}

std::string DYLDRendezvous::ReadStringFromMemory(addr_t addr) {
  std::string str;
  Status error;

  if (addr == LLDB_INVALID_ADDRESS)
    return std::string();

  m_process->ReadCStringFromMemory(addr, str, error);

  return str;
}

// Returns true if the load bias reported by the linker is incorrect for the
// given entry. This function is used to handle cases where we want to work
// around a bug in the system linker.
static bool isLoadBiasIncorrect(Target &target, const std::string &file_path) {
  // On Android L (API 21, 22) the load address of the "/system/bin/linker"
  // isn't filled in correctly.
  unsigned os_major = target.GetPlatform()->GetOSVersion().getMajor();
  return target.GetArchitecture().GetTriple().isAndroid() &&
         (os_major == 21 || os_major == 22) &&
         (file_path == "/system/bin/linker" ||
          file_path == "/system/bin/linker64");
}

void DYLDRendezvous::UpdateBaseAddrIfNecessary(SOEntry &entry,
                                               std::string const &file_path) {
  // If the load bias reported by the linker is incorrect then fetch the load
  // address of the file from the proc file system.
  if (isLoadBiasIncorrect(m_process->GetTarget(), file_path)) {
    lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
    bool is_loaded = false;
    Status error =
        m_process->GetFileLoadAddress(entry.file_spec, is_loaded, load_addr);
    if (error.Success() && is_loaded)
      entry.base_addr = load_addr;
  }
}

void DYLDRendezvous::UpdateFileSpecIfNecessary(SOEntry &entry) {
  // Updates filename if empty. It is useful while debugging ld.so,
  // when the link map returns empty string for the main executable.
  if (!entry.file_spec) {
    MemoryRegionInfo region;
    Status region_status =
        m_process->GetMemoryRegionInfo(entry.dyn_addr, region);
    if (!region.GetName().IsEmpty())
      entry.file_spec.SetFile(region.GetName().AsCString(),
                              FileSpec::Style::native);
  }
}

bool DYLDRendezvous::ReadSOEntryFromMemory(lldb::addr_t addr, SOEntry &entry) {
  entry.clear();

  entry.link_addr = addr;

  if (!(addr = ReadPointer(addr, &entry.base_addr)))
    return false;

  // mips adds an extra load offset field to the link map struct on FreeBSD and
  // NetBSD (need to validate other OSes).
  // http://svnweb.freebsd.org/base/head/sys/sys/link_elf.h?revision=217153&view=markup#l57
  const ArchSpec &arch = m_process->GetTarget().GetArchitecture();
  if ((arch.GetTriple().getOS() == llvm::Triple::FreeBSD ||
       arch.GetTriple().getOS() == llvm::Triple::NetBSD) &&
      arch.IsMIPS()) {
    addr_t mips_l_offs;
    if (!(addr = ReadPointer(addr, &mips_l_offs)))
      return false;
    if (mips_l_offs != 0 && mips_l_offs != entry.base_addr)
      return false;
  }

  if (!(addr = ReadPointer(addr, &entry.path_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.dyn_addr)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.next)))
    return false;

  if (!(addr = ReadPointer(addr, &entry.prev)))
    return false;

  std::string file_path = ReadStringFromMemory(entry.path_addr);
  entry.file_spec.SetFile(file_path, FileSpec::Style::native);

  UpdateBaseAddrIfNecessary(entry, file_path);

  return true;
}

bool DYLDRendezvous::FindMetadata(const char *name, PThreadField field,
                                  uint32_t &value) {
  Target &target = m_process->GetTarget();

  SymbolContextList list;
  target.GetImages().FindSymbolsWithNameAndType(ConstString(name),
                                                eSymbolTypeAny, list);
  if (list.IsEmpty())
    return false;

  Address address = list[0].symbol->GetAddress();
  address.SetOffset(address.GetOffset() + field * sizeof(uint32_t));

  // Read from target memory as this allows us to try process memory and
  // fallback to reading from read only sections from the object files. Here we
  // are reading read only data from libpthread.so to find data in the thread
  // specific area for the data we want and this won't be saved into process
  // memory due to it being read only.
  Status error;
  value =
      target.ReadUnsignedIntegerFromMemory(address, sizeof(uint32_t), 0, error);
  if (error.Fail())
    return false;

  if (field == eSize)
    value /= 8; // convert bits to bytes

  return true;
}

const DYLDRendezvous::ThreadInfo &DYLDRendezvous::GetThreadInfo() {
  if (!m_thread_info.valid) {
    bool ok = true;

    ok &= FindMetadata("_thread_db_pthread_dtvp", eOffset,
                       m_thread_info.dtv_offset);
    ok &=
        FindMetadata("_thread_db_dtv_dtv", eSize, m_thread_info.dtv_slot_size);
    ok &= FindMetadata("_thread_db_link_map_l_tls_modid", eOffset,
                       m_thread_info.modid_offset);
    ok &= FindMetadata("_thread_db_dtv_t_pointer_val", eOffset,
                       m_thread_info.tls_offset);

    if (ok)
      m_thread_info.valid = true;
  }

  return m_thread_info;
}

void DYLDRendezvous::DumpToLog(Log *log) const {
  int state = GetState();

  if (!log)
    return;

  log->PutCString("DYLDRendezvous:");
  LLDB_LOGF(log, "   Address: %" PRIx64, GetRendezvousAddress());
  LLDB_LOGF(log, "   Version: %" PRIu64, GetVersion());
  LLDB_LOGF(log, "   Link   : %" PRIx64, GetLinkMapAddress());
  LLDB_LOGF(log, "   Break  : %" PRIx64, GetBreakAddress());
  LLDB_LOGF(log, "   LDBase : %" PRIx64, GetLDBase());
  LLDB_LOGF(log, "   State  : %s",
            (state == eConsistent)
                ? "consistent"
                : (state == eAdd) ? "add"
                                  : (state == eDelete) ? "delete" : "unknown");

  iterator I = begin();
  iterator E = end();

  if (I != E)
    log->PutCString("DYLDRendezvous SOEntries:");

  for (int i = 1; I != E; ++I, ++i) {
    LLDB_LOGF(log, "\n   SOEntry [%d] %s", i, I->file_spec.GetPath().c_str());
    LLDB_LOGF(log, "      Base : %" PRIx64, I->base_addr);
    LLDB_LOGF(log, "      Path : %" PRIx64, I->path_addr);
    LLDB_LOGF(log, "      Dyn  : %" PRIx64, I->dyn_addr);
    LLDB_LOGF(log, "      Next : %" PRIx64, I->next);
    LLDB_LOGF(log, "      Prev : %" PRIx64, I->prev);
  }
}

bool DYLDRendezvous::IsCoreFile() const {
  return !m_process->IsLiveDebugSession();
}
