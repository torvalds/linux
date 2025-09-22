//===-- ProcessMachCore.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdlib>

#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Threading.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/AppleUuidCompatibility.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/UUID.h"

#include "ProcessMachCore.h"
#include "Plugins/Process/Utility/StopInfoMachException.h"
#include "ThreadMachCore.h"

// Needed for the plug-in names for the dynamic loaders.
#include "lldb/Host/SafeMachO.h"

#include "Plugins/DynamicLoader/Darwin-Kernel/DynamicLoaderDarwinKernel.h"
#include "Plugins/DynamicLoader/MacOSX-DYLD/DynamicLoaderMacOSXDYLD.h"
#include "Plugins/DynamicLoader/Static/DynamicLoaderStatic.h"
#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
#include "Plugins/Platform/MacOSX/PlatformDarwinKernel.h"

#include <memory>
#include <mutex>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ProcessMachCore)

llvm::StringRef ProcessMachCore::GetPluginDescriptionStatic() {
  return "Mach-O core file debugging plug-in.";
}

void ProcessMachCore::Terminate() {
  PluginManager::UnregisterPlugin(ProcessMachCore::CreateInstance);
}

lldb::ProcessSP ProcessMachCore::CreateInstance(lldb::TargetSP target_sp,
                                                ListenerSP listener_sp,
                                                const FileSpec *crash_file,
                                                bool can_connect) {
  lldb::ProcessSP process_sp;
  if (crash_file && !can_connect) {
    const size_t header_size = sizeof(llvm::MachO::mach_header);
    auto data_sp = FileSystem::Instance().CreateDataBuffer(
        crash_file->GetPath(), header_size, 0);
    if (data_sp && data_sp->GetByteSize() == header_size) {
      DataExtractor data(data_sp, lldb::eByteOrderLittle, 4);

      lldb::offset_t data_offset = 0;
      llvm::MachO::mach_header mach_header;
      if (ObjectFileMachO::ParseHeader(data, &data_offset, mach_header)) {
        if (mach_header.filetype == llvm::MachO::MH_CORE)
          process_sp = std::make_shared<ProcessMachCore>(target_sp, listener_sp,
                                                         *crash_file);
      }
    }
  }
  return process_sp;
}

bool ProcessMachCore::CanDebug(lldb::TargetSP target_sp,
                               bool plugin_specified_by_name) {
  if (plugin_specified_by_name)
    return true;

  // For now we are just making sure the file exists for a given module
  if (!m_core_module_sp && FileSystem::Instance().Exists(m_core_file)) {
    // Don't add the Target's architecture to the ModuleSpec - we may be
    // working with a core file that doesn't have the correct cpusubtype in the
    // header but we should still try to use it -
    // ModuleSpecList::FindMatchingModuleSpec enforces a strict arch mach.
    ModuleSpec core_module_spec(m_core_file);
    Status error(ModuleList::GetSharedModule(core_module_spec, m_core_module_sp,
                                             nullptr, nullptr, nullptr));

    if (m_core_module_sp) {
      ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
      if (core_objfile && core_objfile->GetType() == ObjectFile::eTypeCoreFile)
        return true;
    }
  }
  return false;
}

// ProcessMachCore constructor
ProcessMachCore::ProcessMachCore(lldb::TargetSP target_sp,
                                 ListenerSP listener_sp,
                                 const FileSpec &core_file)
    : PostMortemProcess(target_sp, listener_sp, core_file), m_core_aranges(),
      m_core_range_infos(), m_core_module_sp(),
      m_dyld_addr(LLDB_INVALID_ADDRESS),
      m_mach_kernel_addr(LLDB_INVALID_ADDRESS) {}

// Destructor
ProcessMachCore::~ProcessMachCore() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);
}

bool ProcessMachCore::CheckAddressForDyldOrKernel(lldb::addr_t addr,
                                                  addr_t &dyld,
                                                  addr_t &kernel) {
  Log *log(GetLog(LLDBLog::DynamicLoader | LLDBLog::Process));
  llvm::MachO::mach_header header;
  Status error;
  dyld = kernel = LLDB_INVALID_ADDRESS;
  if (DoReadMemory(addr, &header, sizeof(header), error) != sizeof(header))
    return false;
  if (header.magic == llvm::MachO::MH_CIGAM ||
      header.magic == llvm::MachO::MH_CIGAM_64) {
    header.magic = llvm::byteswap<uint32_t>(header.magic);
    header.cputype = llvm::byteswap<uint32_t>(header.cputype);
    header.cpusubtype = llvm::byteswap<uint32_t>(header.cpusubtype);
    header.filetype = llvm::byteswap<uint32_t>(header.filetype);
    header.ncmds = llvm::byteswap<uint32_t>(header.ncmds);
    header.sizeofcmds = llvm::byteswap<uint32_t>(header.sizeofcmds);
    header.flags = llvm::byteswap<uint32_t>(header.flags);
  }

  if (header.magic == llvm::MachO::MH_MAGIC ||
      header.magic == llvm::MachO::MH_MAGIC_64) {
    // Check MH_EXECUTABLE to see if we can find the mach image that contains
    // the shared library list. The dynamic loader (dyld) is what contains the
    // list for user applications, and the mach kernel contains a global that
    // has the list of kexts to load
    switch (header.filetype) {
    case llvm::MachO::MH_DYLINKER:
      LLDB_LOGF(log,
                "ProcessMachCore::%s found a user "
                "process dyld binary image at 0x%" PRIx64,
                __FUNCTION__, addr);
      dyld = addr;
      return true;

    case llvm::MachO::MH_EXECUTE:
      // Check MH_EXECUTABLE file types to see if the dynamic link object flag
      // is NOT set. If it isn't, then we have a mach_kernel.
      if ((header.flags & llvm::MachO::MH_DYLDLINK) == 0) {
        LLDB_LOGF(log,
                  "ProcessMachCore::%s found a mach "
                  "kernel binary image at 0x%" PRIx64,
                  __FUNCTION__, addr);
        // Address of the mach kernel "struct mach_header" in the core file.
        kernel = addr;
        return true;
      }
      break;
    }
  }
  return false;
}

void ProcessMachCore::CreateMemoryRegions() {
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
  SectionList *section_list = core_objfile->GetSectionList();
  const uint32_t num_sections = section_list->GetNumSections(0);

  bool ranges_are_sorted = true;
  addr_t vm_addr = 0;
  for (uint32_t i = 0; i < num_sections; ++i) {
    Section *section = section_list->GetSectionAtIndex(i).get();
    if (section && section->GetFileSize() > 0) {
      lldb::addr_t section_vm_addr = section->GetFileAddress();
      FileRange file_range(section->GetFileOffset(), section->GetFileSize());
      VMRangeToFileOffset::Entry range_entry(
          section_vm_addr, section->GetByteSize(), file_range);

      if (vm_addr > section_vm_addr)
        ranges_are_sorted = false;
      vm_addr = section->GetFileAddress();
      VMRangeToFileOffset::Entry *last_entry = m_core_aranges.Back();

      if (last_entry &&
          last_entry->GetRangeEnd() == range_entry.GetRangeBase() &&
          last_entry->data.GetRangeEnd() == range_entry.data.GetRangeBase()) {
        last_entry->SetRangeEnd(range_entry.GetRangeEnd());
        last_entry->data.SetRangeEnd(range_entry.data.GetRangeEnd());
      } else {
        m_core_aranges.Append(range_entry);
      }
      // Some core files don't fill in the permissions correctly. If that is
      // the case assume read + execute so clients don't think the memory is
      // not readable, or executable. The memory isn't writable since this
      // plug-in doesn't implement DoWriteMemory.
      uint32_t permissions = section->GetPermissions();
      if (permissions == 0)
        permissions = lldb::ePermissionsReadable | lldb::ePermissionsExecutable;
      m_core_range_infos.Append(VMRangeToPermissions::Entry(
          section_vm_addr, section->GetByteSize(), permissions));
    }
  }
  if (!ranges_are_sorted) {
    m_core_aranges.Sort();
    m_core_range_infos.Sort();
  }
}

// Some corefiles have a UUID stored in a low memory
// address.  We inspect a set list of addresses for
// the characters 'uuid' and 16 bytes later there will
// be a uuid_t UUID.  If we can find a binary that
// matches the UUID, it is loaded with no slide in the target.
bool ProcessMachCore::LoadBinaryViaLowmemUUID() {
  Log *log(GetLog(LLDBLog::DynamicLoader | LLDBLog::Process));
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();

  uint64_t lowmem_uuid_addresses[] = {0x2000204, 0x1000204, 0x1000020, 0x4204,
                                      0x1204,    0x1020,    0x4020,    0xc00,
                                      0xC0,      0};

  for (uint64_t addr : lowmem_uuid_addresses) {
    const VMRangeToFileOffset::Entry *core_memory_entry =
        m_core_aranges.FindEntryThatContains(addr);
    if (core_memory_entry) {
      const addr_t offset = addr - core_memory_entry->GetRangeBase();
      const addr_t bytes_left = core_memory_entry->GetRangeEnd() - addr;
      // (4-bytes 'uuid' + 12 bytes pad for align + 16 bytes uuid_t) == 32 bytes
      if (bytes_left >= 32) {
        char strbuf[4];
        if (core_objfile->CopyData(
                core_memory_entry->data.GetRangeBase() + offset, 4, &strbuf) &&
            strncmp("uuid", (char *)&strbuf, 4) == 0) {
          uuid_t uuid_bytes;
          if (core_objfile->CopyData(core_memory_entry->data.GetRangeBase() +
                                         offset + 16,
                                     sizeof(uuid_t), uuid_bytes)) {
            UUID uuid(uuid_bytes, sizeof(uuid_t));
            if (uuid.IsValid()) {
              LLDB_LOGF(log,
                        "ProcessMachCore::LoadBinaryViaLowmemUUID: found "
                        "binary uuid %s at low memory address 0x%" PRIx64,
                        uuid.GetAsString().c_str(), addr);
              // We have no address specified, only a UUID.  Load it at the file
              // address.
              const bool value_is_offset = true;
              const bool force_symbol_search = true;
              const bool notify = true;
              const bool set_address_in_target = true;
              const bool allow_memory_image_last_resort = false;
              if (DynamicLoader::LoadBinaryWithUUIDAndAddress(
                      this, llvm::StringRef(), uuid, 0, value_is_offset,
                      force_symbol_search, notify, set_address_in_target,
                      allow_memory_image_last_resort)) {
                m_dyld_plugin_name = DynamicLoaderStatic::GetPluginNameStatic();
              }
              // We found metadata saying which binary should be loaded; don't
              // try an exhaustive search.
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool ProcessMachCore::LoadBinariesViaMetadata() {
  Log *log(GetLog(LLDBLog::DynamicLoader | LLDBLog::Process));
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();

  addr_t objfile_binary_value;
  bool objfile_binary_value_is_offset;
  UUID objfile_binary_uuid;
  ObjectFile::BinaryType type;

  // This will be set to true if we had a metadata hint
  // specifying a UUID or address -- and we should not fall back
  // to doing an exhaustive search.
  bool found_binary_spec_in_metadata = false;

  if (core_objfile->GetCorefileMainBinaryInfo(objfile_binary_value,
                                              objfile_binary_value_is_offset,
                                              objfile_binary_uuid, type)) {
    if (log) {
      log->Printf("ProcessMachCore::LoadBinariesViaMetadata: using binary hint "
                  "from 'main bin spec' "
                  "LC_NOTE with UUID %s value 0x%" PRIx64
                  " value is offset %d and type %d",
                  objfile_binary_uuid.GetAsString().c_str(),
                  objfile_binary_value, objfile_binary_value_is_offset, type);
    }
    found_binary_spec_in_metadata = true;

    // If this is the xnu kernel, don't load it now.  Note the correct
    // DynamicLoader plugin to use, and the address of the kernel, and
    // let the DynamicLoader handle the finding & loading of the binary.
    if (type == ObjectFile::eBinaryTypeKernel) {
      m_mach_kernel_addr = objfile_binary_value;
      m_dyld_plugin_name = DynamicLoaderDarwinKernel::GetPluginNameStatic();
    } else if (type == ObjectFile::eBinaryTypeUser) {
      m_dyld_addr = objfile_binary_value;
      m_dyld_plugin_name = DynamicLoaderMacOSXDYLD::GetPluginNameStatic();
    } else {
      const bool force_symbol_search = true;
      const bool notify = true;
      const bool set_address_in_target = true;
      const bool allow_memory_image_last_resort = false;
      if (DynamicLoader::LoadBinaryWithUUIDAndAddress(
              this, llvm::StringRef(), objfile_binary_uuid,
              objfile_binary_value, objfile_binary_value_is_offset,
              force_symbol_search, notify, set_address_in_target,
              allow_memory_image_last_resort)) {
        m_dyld_plugin_name = DynamicLoaderStatic::GetPluginNameStatic();
      }
    }
  }

  // This checks for the presence of an LC_IDENT string in a core file;
  // LC_IDENT is very obsolete and should not be used in new code, but if the
  // load command is present, let's use the contents.
  UUID ident_uuid;
  addr_t ident_binary_addr = LLDB_INVALID_ADDRESS;
    std::string corefile_identifier = core_objfile->GetIdentifierString();

    // Search for UUID= and stext= strings in the identifier str.
    if (corefile_identifier.find("UUID=") != std::string::npos) {
      size_t p = corefile_identifier.find("UUID=") + strlen("UUID=");
      std::string uuid_str = corefile_identifier.substr(p, 36);
      ident_uuid.SetFromStringRef(uuid_str);
      if (log)
        log->Printf("Got a UUID from LC_IDENT/kern ver str LC_NOTE: %s",
                    ident_uuid.GetAsString().c_str());
      found_binary_spec_in_metadata = true;
    }
    if (corefile_identifier.find("stext=") != std::string::npos) {
      size_t p = corefile_identifier.find("stext=") + strlen("stext=");
      if (corefile_identifier[p] == '0' && corefile_identifier[p + 1] == 'x') {
        ident_binary_addr =
            ::strtoul(corefile_identifier.c_str() + p, nullptr, 16);
        if (log)
          log->Printf("Got a load address from LC_IDENT/kern ver str "
                      "LC_NOTE: 0x%" PRIx64,
                      ident_binary_addr);
        found_binary_spec_in_metadata = true;
      }
    }

    // Search for a "Darwin Kernel" str indicating kernel; else treat as
    // standalone
    if (corefile_identifier.find("Darwin Kernel") != std::string::npos &&
        ident_uuid.IsValid() && ident_binary_addr != LLDB_INVALID_ADDRESS) {
      if (log)
        log->Printf(
            "ProcessMachCore::LoadBinariesViaMetadata: Found kernel binary via "
            "LC_IDENT/kern ver str LC_NOTE");
      m_mach_kernel_addr = ident_binary_addr;
      found_binary_spec_in_metadata = true;
    } else if (ident_uuid.IsValid()) {
      // We have no address specified, only a UUID.  Load it at the file
      // address.
      const bool value_is_offset = false;
      const bool force_symbol_search = true;
      const bool notify = true;
      const bool set_address_in_target = true;
      const bool allow_memory_image_last_resort = false;
      if (DynamicLoader::LoadBinaryWithUUIDAndAddress(
              this, llvm::StringRef(), ident_uuid, ident_binary_addr,
              value_is_offset, force_symbol_search, notify,
              set_address_in_target, allow_memory_image_last_resort)) {
        found_binary_spec_in_metadata = true;
        m_dyld_plugin_name = DynamicLoaderStatic::GetPluginNameStatic();
      }
    }

  // Finally, load any binaries noted by "load binary" LC_NOTEs in the
  // corefile
  if (core_objfile->LoadCoreFileImages(*this)) {
    found_binary_spec_in_metadata = true;
    m_dyld_plugin_name = DynamicLoaderStatic::GetPluginNameStatic();
  }

  if (!found_binary_spec_in_metadata && LoadBinaryViaLowmemUUID())
    found_binary_spec_in_metadata = true;

  // LoadCoreFileImges may have set the dynamic loader, e.g. in
  // PlatformDarwinKernel::LoadPlatformBinaryAndSetup().
  // If we now have a dynamic loader, save its name so we don't 
  // un-set it later.
  if (m_dyld_up)
    m_dyld_plugin_name = GetDynamicLoader()->GetPluginName();

  return found_binary_spec_in_metadata;
}

void ProcessMachCore::LoadBinariesViaExhaustiveSearch() {
  Log *log(GetLog(LLDBLog::DynamicLoader | LLDBLog::Process));

  // Search the pages of the corefile for dyld or mach kernel
  // binaries.  There may be multiple things that look like a kernel
  // in the corefile; disambiguating to the correct one can be difficult.

  std::vector<addr_t> dylds_found;
  std::vector<addr_t> kernels_found;

  const size_t num_core_aranges = m_core_aranges.GetSize();
  for (size_t i = 0; i < num_core_aranges; ++i) {
    const VMRangeToFileOffset::Entry *entry = m_core_aranges.GetEntryAtIndex(i);
    lldb::addr_t section_vm_addr_start = entry->GetRangeBase();
    lldb::addr_t section_vm_addr_end = entry->GetRangeEnd();
    for (lldb::addr_t section_vm_addr = section_vm_addr_start;
         section_vm_addr < section_vm_addr_end; section_vm_addr += 0x1000) {
      addr_t dyld, kernel;
      if (CheckAddressForDyldOrKernel(section_vm_addr, dyld, kernel)) {
        if (dyld != LLDB_INVALID_ADDRESS)
          dylds_found.push_back(dyld);
        if (kernel != LLDB_INVALID_ADDRESS)
          kernels_found.push_back(kernel);
      }
    }
  }

  // If we found more than one dyld mach-o header in the corefile,
  // pick the first one.
  if (dylds_found.size() > 0)
    m_dyld_addr = dylds_found[0];
  if (kernels_found.size() > 0)
    m_mach_kernel_addr = kernels_found[0];

  // Zero or one kernels found, we're done.
  if (kernels_found.size() < 2)
    return;

  // In the case of multiple kernel images found in the core file via
  // exhaustive search, we may not pick the correct one.  See if the
  // DynamicLoaderDarwinKernel's search heuristics might identify the correct
  // one.

  // SearchForDarwinKernel will call this class' GetImageInfoAddress method
  // which will give it the addresses we already have.
  // Save those aside and set
  // m_mach_kernel_addr/m_dyld_addr to an invalid address temporarily so
  // DynamicLoaderDarwinKernel does a real search for the kernel using its
  // own heuristics.

  addr_t saved_mach_kernel_addr = m_mach_kernel_addr;
  addr_t saved_user_dyld_addr = m_dyld_addr;
  m_mach_kernel_addr = LLDB_INVALID_ADDRESS;
  m_dyld_addr = LLDB_INVALID_ADDRESS;

  addr_t better_kernel_address =
      DynamicLoaderDarwinKernel::SearchForDarwinKernel(this);

  m_mach_kernel_addr = saved_mach_kernel_addr;
  m_dyld_addr = saved_user_dyld_addr;

  if (better_kernel_address != LLDB_INVALID_ADDRESS) {
    LLDB_LOGF(log,
              "ProcessMachCore::%s: Using "
              "the kernel address "
              "from DynamicLoaderDarwinKernel",
              __FUNCTION__);
    m_mach_kernel_addr = better_kernel_address;
  }
}

void ProcessMachCore::LoadBinariesAndSetDYLD() {
  Log *log(GetLog(LLDBLog::DynamicLoader | LLDBLog::Process));

  bool found_binary_spec_in_metadata = LoadBinariesViaMetadata();
  if (!found_binary_spec_in_metadata)
    LoadBinariesViaExhaustiveSearch();

  if (m_dyld_plugin_name.empty()) {
    // If we found both a user-process dyld and a kernel binary, we need to
    // decide which to prefer.
    if (GetCorefilePreference() == eKernelCorefile) {
      if (m_mach_kernel_addr != LLDB_INVALID_ADDRESS) {
        LLDB_LOGF(log,
                  "ProcessMachCore::%s: Using kernel "
                  "corefile image "
                  "at 0x%" PRIx64,
                  __FUNCTION__, m_mach_kernel_addr);
        m_dyld_plugin_name = DynamicLoaderDarwinKernel::GetPluginNameStatic();
      } else if (m_dyld_addr != LLDB_INVALID_ADDRESS) {
        LLDB_LOGF(log,
                  "ProcessMachCore::%s: Using user process dyld "
                  "image at 0x%" PRIx64,
                  __FUNCTION__, m_dyld_addr);
        m_dyld_plugin_name = DynamicLoaderMacOSXDYLD::GetPluginNameStatic();
      }
    } else {
      if (m_dyld_addr != LLDB_INVALID_ADDRESS) {
        LLDB_LOGF(log,
                  "ProcessMachCore::%s: Using user process dyld "
                  "image at 0x%" PRIx64,
                  __FUNCTION__, m_dyld_addr);
        m_dyld_plugin_name = DynamicLoaderMacOSXDYLD::GetPluginNameStatic();
      } else if (m_mach_kernel_addr != LLDB_INVALID_ADDRESS) {
        LLDB_LOGF(log,
                  "ProcessMachCore::%s: Using kernel "
                  "corefile image "
                  "at 0x%" PRIx64,
                  __FUNCTION__, m_mach_kernel_addr);
        m_dyld_plugin_name = DynamicLoaderDarwinKernel::GetPluginNameStatic();
      }
    }
  }
}

void ProcessMachCore::CleanupMemoryRegionPermissions() {
  if (m_dyld_plugin_name != DynamicLoaderMacOSXDYLD::GetPluginNameStatic()) {
    // For non-user process core files, the permissions on the core file
    // segments are usually meaningless, they may be just "read", because we're
    // dealing with kernel coredumps or early startup coredumps and the dumper
    // is grabbing pages of memory without knowing what they are.  If they
    // aren't marked as "executable", that can break the unwinder which will
    // check a pc value to see if it is in an executable segment and stop the
    // backtrace early if it is not ("executable" and "unknown" would both be
    // fine, but "not executable" will break the unwinder).
    size_t core_range_infos_size = m_core_range_infos.GetSize();
    for (size_t i = 0; i < core_range_infos_size; i++) {
      VMRangeToPermissions::Entry *ent =
          m_core_range_infos.GetMutableEntryAtIndex(i);
      ent->data = lldb::ePermissionsReadable | lldb::ePermissionsExecutable;
    }
  }
}

// Process Control
Status ProcessMachCore::DoLoadCore() {
  Status error;
  if (!m_core_module_sp) {
    error.SetErrorString("invalid core module");
    return error;
  }

  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
  if (core_objfile == nullptr) {
    error.SetErrorString("invalid core object file");
    return error;
  }

  SetCanJIT(false);

  // The corefile's architecture is our best starting point.
  ArchSpec arch(m_core_module_sp->GetArchitecture());
  if (arch.IsValid())
    GetTarget().SetArchitecture(arch);

  CreateMemoryRegions();

  LoadBinariesAndSetDYLD();

  CleanupMemoryRegionPermissions();

  AddressableBits addressable_bits = core_objfile->GetAddressableBits();
  SetAddressableBitMasks(addressable_bits);

  return error;
}

lldb_private::DynamicLoader *ProcessMachCore::GetDynamicLoader() {
  if (m_dyld_up.get() == nullptr)
    m_dyld_up.reset(DynamicLoader::FindPlugin(this, m_dyld_plugin_name));
  return m_dyld_up.get();
}

bool ProcessMachCore::DoUpdateThreadList(ThreadList &old_thread_list,
                                         ThreadList &new_thread_list) {
  if (old_thread_list.GetSize(false) == 0) {
    // Make up the thread the first time this is called so we can setup our one
    // and only core thread state.
    ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();

    if (core_objfile) {
      std::set<tid_t> used_tids;
      const uint32_t num_threads = core_objfile->GetNumThreadContexts();
      std::vector<tid_t> tids;
      if (core_objfile->GetCorefileThreadExtraInfos(tids)) {
        assert(tids.size() == num_threads);

        // Find highest tid value.
        tid_t highest_tid = 0;
        for (uint32_t i = 0; i < num_threads; i++) {
          if (tids[i] != LLDB_INVALID_THREAD_ID && tids[i] > highest_tid)
            highest_tid = tids[i];
        }
        tid_t current_unused_tid = highest_tid + 1;
        for (uint32_t i = 0; i < num_threads; i++) {
          if (tids[i] == LLDB_INVALID_THREAD_ID) {
            tids[i] = current_unused_tid++;
          }
        }
      } else {
        // No metadata, insert numbers sequentially from 0.
        for (uint32_t i = 0; i < num_threads; i++) {
          tids.push_back(i);
        }
      }

      for (uint32_t i = 0; i < num_threads; i++) {
        ThreadSP thread_sp =
            std::make_shared<ThreadMachCore>(*this, tids[i], i);
        new_thread_list.AddThread(thread_sp);
      }
    }
  } else {
    const uint32_t num_threads = old_thread_list.GetSize(false);
    for (uint32_t i = 0; i < num_threads; ++i)
      new_thread_list.AddThread(old_thread_list.GetThreadAtIndex(i, false));
  }
  return new_thread_list.GetSize(false) > 0;
}

void ProcessMachCore::RefreshStateAfterStop() {
  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list.RefreshStateAfterStop();
  // SetThreadStopInfo (m_last_stop_packet);
}

Status ProcessMachCore::DoDestroy() { return Status(); }

// Process Queries

bool ProcessMachCore::IsAlive() { return true; }

bool ProcessMachCore::WarnBeforeDetach() const { return false; }

// Process Memory
size_t ProcessMachCore::ReadMemory(addr_t addr, void *buf, size_t size,
                                   Status &error) {
  // Don't allow the caching that lldb_private::Process::ReadMemory does since
  // in core files we have it all cached our our core file anyway.
  return DoReadMemory(FixAnyAddress(addr), buf, size, error);
}

size_t ProcessMachCore::DoReadMemory(addr_t addr, void *buf, size_t size,
                                     Status &error) {
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
  size_t bytes_read = 0;

  if (core_objfile) {
    // Segments are not always contiguous in mach-o core files. We have core
    // files that have segments like:
    //            Address    Size       File off   File size
    //            ---------- ---------- ---------- ----------
    // LC_SEGMENT 0x000f6000 0x00001000 0x1d509ee8 0x00001000 --- ---   0
    // 0x00000000 __TEXT LC_SEGMENT 0x0f600000 0x00100000 0x1d50aee8 0x00100000
    // --- ---   0 0x00000000 __TEXT LC_SEGMENT 0x000f7000 0x00001000
    // 0x1d60aee8 0x00001000 --- ---   0 0x00000000 __TEXT
    //
    // Any if the user executes the following command:
    //
    // (lldb) mem read 0xf6ff0
    //
    // We would attempt to read 32 bytes from 0xf6ff0 but would only get 16
    // unless we loop through consecutive memory ranges that are contiguous in
    // the address space, but not in the file data.
    while (bytes_read < size) {
      const addr_t curr_addr = addr + bytes_read;
      const VMRangeToFileOffset::Entry *core_memory_entry =
          m_core_aranges.FindEntryThatContains(curr_addr);

      if (core_memory_entry) {
        const addr_t offset = curr_addr - core_memory_entry->GetRangeBase();
        const addr_t bytes_left = core_memory_entry->GetRangeEnd() - curr_addr;
        const size_t bytes_to_read =
            std::min(size - bytes_read, (size_t)bytes_left);
        const size_t curr_bytes_read = core_objfile->CopyData(
            core_memory_entry->data.GetRangeBase() + offset, bytes_to_read,
            (char *)buf + bytes_read);
        if (curr_bytes_read == 0)
          break;
        bytes_read += curr_bytes_read;
      } else {
        // Only set the error if we didn't read any bytes
        if (bytes_read == 0)
          error.SetErrorStringWithFormat(
              "core file does not contain 0x%" PRIx64, curr_addr);
        break;
      }
    }
  }

  return bytes_read;
}

Status ProcessMachCore::DoGetMemoryRegionInfo(addr_t load_addr,
                                              MemoryRegionInfo &region_info) {
  region_info.Clear();
  const VMRangeToPermissions::Entry *permission_entry =
      m_core_range_infos.FindEntryThatContainsOrFollows(load_addr);
  if (permission_entry) {
    if (permission_entry->Contains(load_addr)) {
      region_info.GetRange().SetRangeBase(permission_entry->GetRangeBase());
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeEnd());
      const Flags permissions(permission_entry->data);
      region_info.SetReadable(permissions.Test(ePermissionsReadable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetWritable(permissions.Test(ePermissionsWritable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetExecutable(permissions.Test(ePermissionsExecutable)
                                    ? MemoryRegionInfo::eYes
                                    : MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eYes);
    } else if (load_addr < permission_entry->GetRangeBase()) {
      region_info.GetRange().SetRangeBase(load_addr);
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeBase());
      region_info.SetReadable(MemoryRegionInfo::eNo);
      region_info.SetWritable(MemoryRegionInfo::eNo);
      region_info.SetExecutable(MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eNo);
    }
    return Status();
  }

  region_info.GetRange().SetRangeBase(load_addr);
  region_info.GetRange().SetRangeEnd(LLDB_INVALID_ADDRESS);
  region_info.SetReadable(MemoryRegionInfo::eNo);
  region_info.SetWritable(MemoryRegionInfo::eNo);
  region_info.SetExecutable(MemoryRegionInfo::eNo);
  region_info.SetMapped(MemoryRegionInfo::eNo);
  return Status();
}

void ProcessMachCore::Clear() { m_thread_list.Clear(); }

void ProcessMachCore::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance);
  });
}

addr_t ProcessMachCore::GetImageInfoAddress() {
  // If we found both a user-process dyld and a kernel binary, we need to
  // decide which to prefer.
  if (GetCorefilePreference() == eKernelCorefile) {
    if (m_mach_kernel_addr != LLDB_INVALID_ADDRESS) {
      return m_mach_kernel_addr;
    }
    return m_dyld_addr;
  } else {
    if (m_dyld_addr != LLDB_INVALID_ADDRESS) {
      return m_dyld_addr;
    }
    return m_mach_kernel_addr;
  }
}

lldb_private::ObjectFile *ProcessMachCore::GetCoreObjectFile() {
  return m_core_module_sp->GetObjectFile();
}
