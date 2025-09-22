//===-- ProcessElfCore.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <memory>
#include <mutex>

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Threading.h"

#include "Plugins/DynamicLoader/POSIX-DYLD/DynamicLoaderPOSIXDYLD.h"
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"
#include "ProcessElfCore.h"
#include "ThreadElfCore.h"

using namespace lldb_private;
namespace ELF = llvm::ELF;

LLDB_PLUGIN_DEFINE(ProcessElfCore)

llvm::StringRef ProcessElfCore::GetPluginDescriptionStatic() {
  return "ELF core dump plug-in.";
}

void ProcessElfCore::Terminate() {
  PluginManager::UnregisterPlugin(ProcessElfCore::CreateInstance);
}

lldb::ProcessSP ProcessElfCore::CreateInstance(lldb::TargetSP target_sp,
                                               lldb::ListenerSP listener_sp,
                                               const FileSpec *crash_file,
                                               bool can_connect) {
  lldb::ProcessSP process_sp;
  if (crash_file && !can_connect) {
    // Read enough data for an ELF32 header or ELF64 header Note: Here we care
    // about e_type field only, so it is safe to ignore possible presence of
    // the header extension.
    const size_t header_size = sizeof(llvm::ELF::Elf64_Ehdr);

    auto data_sp = FileSystem::Instance().CreateDataBuffer(
        crash_file->GetPath(), header_size, 0);
    if (data_sp && data_sp->GetByteSize() == header_size &&
        elf::ELFHeader::MagicBytesMatch(data_sp->GetBytes())) {
      elf::ELFHeader elf_header;
      DataExtractor data(data_sp, lldb::eByteOrderLittle, 4);
      lldb::offset_t data_offset = 0;
      if (elf_header.Parse(data, &data_offset)) {
        // Check whether we're dealing with a raw FreeBSD "full memory dump"
        // ELF vmcore that needs to be handled via FreeBSDKernel plugin instead.
        if (elf_header.e_ident[7] == 0xFF && elf_header.e_version == 0)
          return process_sp;
        if (elf_header.e_type == llvm::ELF::ET_CORE)
          process_sp = std::make_shared<ProcessElfCore>(target_sp, listener_sp,
                                                        *crash_file);
      }
    }
  }
  return process_sp;
}

bool ProcessElfCore::CanDebug(lldb::TargetSP target_sp,
                              bool plugin_specified_by_name) {
  // For now we are just making sure the file exists for a given module
  if (!m_core_module_sp && FileSystem::Instance().Exists(m_core_file)) {
    ModuleSpec core_module_spec(m_core_file, target_sp->GetArchitecture());
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

// ProcessElfCore constructor
ProcessElfCore::ProcessElfCore(lldb::TargetSP target_sp,
                               lldb::ListenerSP listener_sp,
                               const FileSpec &core_file)
    : PostMortemProcess(target_sp, listener_sp, core_file) {}

// Destructor
ProcessElfCore::~ProcessElfCore() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);
}

lldb::addr_t ProcessElfCore::AddAddressRangeFromLoadSegment(
    const elf::ELFProgramHeader &header) {
  const lldb::addr_t addr = header.p_vaddr;
  FileRange file_range(header.p_offset, header.p_filesz);
  VMRangeToFileOffset::Entry range_entry(addr, header.p_memsz, file_range);

  // Only add to m_core_aranges if the file size is non zero. Some core files
  // have PT_LOAD segments for all address ranges, but set f_filesz to zero for
  // the .text sections since they can be retrieved from the object files.
  if (header.p_filesz > 0) {
    VMRangeToFileOffset::Entry *last_entry = m_core_aranges.Back();
    if (last_entry && last_entry->GetRangeEnd() == range_entry.GetRangeBase() &&
        last_entry->data.GetRangeEnd() == range_entry.data.GetRangeBase() &&
        last_entry->GetByteSize() == last_entry->data.GetByteSize()) {
      last_entry->SetRangeEnd(range_entry.GetRangeEnd());
      last_entry->data.SetRangeEnd(range_entry.data.GetRangeEnd());
    } else {
      m_core_aranges.Append(range_entry);
    }
  }
  // Keep a separate map of permissions that isn't coalesced so all ranges
  // are maintained.
  const uint32_t permissions =
      ((header.p_flags & llvm::ELF::PF_R) ? lldb::ePermissionsReadable : 0u) |
      ((header.p_flags & llvm::ELF::PF_W) ? lldb::ePermissionsWritable : 0u) |
      ((header.p_flags & llvm::ELF::PF_X) ? lldb::ePermissionsExecutable : 0u);

  m_core_range_infos.Append(
      VMRangeToPermissions::Entry(addr, header.p_memsz, permissions));

  return addr;
}

lldb::addr_t ProcessElfCore::AddAddressRangeFromMemoryTagSegment(
    const elf::ELFProgramHeader &header) {
  // If lldb understood multiple kinds of tag segments we would record the type
  // of the segment here also. As long as there is only 1 type lldb looks for,
  // there is no need.
  FileRange file_range(header.p_offset, header.p_filesz);
  m_core_tag_ranges.Append(
      VMRangeToFileOffset::Entry(header.p_vaddr, header.p_memsz, file_range));

  return header.p_vaddr;
}

// Process Control
Status ProcessElfCore::DoLoadCore() {
  Status error;
  if (!m_core_module_sp) {
    error.SetErrorString("invalid core module");
    return error;
  }

  ObjectFileELF *core = (ObjectFileELF *)(m_core_module_sp->GetObjectFile());
  if (core == nullptr) {
    error.SetErrorString("invalid core object file");
    return error;
  }

  llvm::ArrayRef<elf::ELFProgramHeader> segments = core->ProgramHeaders();
  if (segments.size() == 0) {
    error.SetErrorString("core file has no segments");
    return error;
  }

  SetCanJIT(false);

  m_thread_data_valid = true;

  bool ranges_are_sorted = true;
  lldb::addr_t vm_addr = 0;
  lldb::addr_t tag_addr = 0;
  /// Walk through segments and Thread and Address Map information.
  /// PT_NOTE - Contains Thread and Register information
  /// PT_LOAD - Contains a contiguous range of Process Address Space
  /// PT_AARCH64_MEMTAG_MTE - Contains AArch64 MTE memory tags for a range of
  ///                         Process Address Space.
  for (const elf::ELFProgramHeader &H : segments) {
    DataExtractor data = core->GetSegmentData(H);

    // Parse thread contexts and auxv structure
    if (H.p_type == llvm::ELF::PT_NOTE) {
      if (llvm::Error error = ParseThreadContextsFromNoteSegment(H, data))
        return Status(std::move(error));
    }
    // PT_LOAD segments contains address map
    if (H.p_type == llvm::ELF::PT_LOAD) {
      lldb::addr_t last_addr = AddAddressRangeFromLoadSegment(H);
      if (vm_addr > last_addr)
        ranges_are_sorted = false;
      vm_addr = last_addr;
    } else if (H.p_type == llvm::ELF::PT_AARCH64_MEMTAG_MTE) {
      lldb::addr_t last_addr = AddAddressRangeFromMemoryTagSegment(H);
      if (tag_addr > last_addr)
        ranges_are_sorted = false;
      tag_addr = last_addr;
    }
  }

  if (!ranges_are_sorted) {
    m_core_aranges.Sort();
    m_core_range_infos.Sort();
    m_core_tag_ranges.Sort();
  }

  // Even if the architecture is set in the target, we need to override it to
  // match the core file which is always single arch.
  ArchSpec arch(m_core_module_sp->GetArchitecture());

  ArchSpec target_arch = GetTarget().GetArchitecture();
  ArchSpec core_arch(m_core_module_sp->GetArchitecture());
  target_arch.MergeFrom(core_arch);
  GetTarget().SetArchitecture(target_arch);
 
  SetUnixSignals(UnixSignals::Create(GetArchitecture()));

  // Ensure we found at least one thread that was stopped on a signal.
  bool siginfo_signal_found = false;
  bool prstatus_signal_found = false;
  // Check we found a signal in a SIGINFO note.
  for (const auto &thread_data : m_thread_data) {
    if (thread_data.signo != 0)
      siginfo_signal_found = true;
    if (thread_data.prstatus_sig != 0)
      prstatus_signal_found = true;
  }
  if (!siginfo_signal_found) {
    // If we don't have signal from SIGINFO use the signal from each threads
    // PRSTATUS note.
    if (prstatus_signal_found) {
      for (auto &thread_data : m_thread_data)
        thread_data.signo = thread_data.prstatus_sig;
    } else if (m_thread_data.size() > 0) {
      // If all else fails force the first thread to be SIGSTOP
      m_thread_data.begin()->signo =
          GetUnixSignals()->GetSignalNumberFromName("SIGSTOP");
    }
  }

  // Try to find gnu build id before we load the executable.
  UpdateBuildIdForNTFileEntries();

  // Core files are useless without the main executable. See if we can locate
  // the main executable using data we found in the core file notes.
  lldb::ModuleSP exe_module_sp = GetTarget().GetExecutableModule();
  if (!exe_module_sp) {
    // The first entry in the NT_FILE might be our executable
    if (!m_nt_file_entries.empty()) {
      ModuleSpec exe_module_spec;
      exe_module_spec.GetArchitecture() = arch;
      exe_module_spec.GetUUID() = m_nt_file_entries[0].uuid;
      exe_module_spec.GetFileSpec().SetFile(m_nt_file_entries[0].path,
                                            FileSpec::Style::native);
      if (exe_module_spec.GetFileSpec()) {
        exe_module_sp =
            GetTarget().GetOrCreateModule(exe_module_spec, true /* notify */);
        if (exe_module_sp)
          GetTarget().SetExecutableModule(exe_module_sp, eLoadDependentsNo);
      }
    }
  }
  return error;
}

void ProcessElfCore::UpdateBuildIdForNTFileEntries() {
  for (NT_FILE_Entry &entry : m_nt_file_entries) {
    entry.uuid = FindBuidIdInCoreMemory(entry.start);
  }
}

lldb_private::DynamicLoader *ProcessElfCore::GetDynamicLoader() {
  if (m_dyld_up.get() == nullptr)
    m_dyld_up.reset(DynamicLoader::FindPlugin(
        this, DynamicLoaderPOSIXDYLD::GetPluginNameStatic()));
  return m_dyld_up.get();
}

bool ProcessElfCore::DoUpdateThreadList(ThreadList &old_thread_list,
                                        ThreadList &new_thread_list) {
  const uint32_t num_threads = GetNumThreadContexts();
  if (!m_thread_data_valid)
    return false;

  for (lldb::tid_t tid = 0; tid < num_threads; ++tid) {
    const ThreadData &td = m_thread_data[tid];
    lldb::ThreadSP thread_sp(new ThreadElfCore(*this, td));
    new_thread_list.AddThread(thread_sp);
  }
  return new_thread_list.GetSize(false) > 0;
}

void ProcessElfCore::RefreshStateAfterStop() {}

Status ProcessElfCore::DoDestroy() { return Status(); }

// Process Queries

bool ProcessElfCore::IsAlive() { return true; }

// Process Memory
size_t ProcessElfCore::ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                  Status &error) {
  if (lldb::ABISP abi_sp = GetABI())
    addr = abi_sp->FixAnyAddress(addr);

  // Don't allow the caching that lldb_private::Process::ReadMemory does since
  // in core files we have it all cached our our core file anyway.
  return DoReadMemory(addr, buf, size, error);
}

Status ProcessElfCore::DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                                             MemoryRegionInfo &region_info) {
  region_info.Clear();
  const VMRangeToPermissions::Entry *permission_entry =
      m_core_range_infos.FindEntryThatContainsOrFollows(load_addr);
  if (permission_entry) {
    if (permission_entry->Contains(load_addr)) {
      region_info.GetRange().SetRangeBase(permission_entry->GetRangeBase());
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeEnd());
      const Flags permissions(permission_entry->data);
      region_info.SetReadable(permissions.Test(lldb::ePermissionsReadable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetWritable(permissions.Test(lldb::ePermissionsWritable)
                                  ? MemoryRegionInfo::eYes
                                  : MemoryRegionInfo::eNo);
      region_info.SetExecutable(permissions.Test(lldb::ePermissionsExecutable)
                                    ? MemoryRegionInfo::eYes
                                    : MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eYes);

      // A region is memory tagged if there is a memory tag segment that covers
      // the exact same range.
      region_info.SetMemoryTagged(MemoryRegionInfo::eNo);
      const VMRangeToFileOffset::Entry *tag_entry =
          m_core_tag_ranges.FindEntryStartsAt(permission_entry->GetRangeBase());
      if (tag_entry &&
          tag_entry->GetRangeEnd() == permission_entry->GetRangeEnd())
        region_info.SetMemoryTagged(MemoryRegionInfo::eYes);
    } else if (load_addr < permission_entry->GetRangeBase()) {
      region_info.GetRange().SetRangeBase(load_addr);
      region_info.GetRange().SetRangeEnd(permission_entry->GetRangeBase());
      region_info.SetReadable(MemoryRegionInfo::eNo);
      region_info.SetWritable(MemoryRegionInfo::eNo);
      region_info.SetExecutable(MemoryRegionInfo::eNo);
      region_info.SetMapped(MemoryRegionInfo::eNo);
      region_info.SetMemoryTagged(MemoryRegionInfo::eNo);
    }
    return Status();
  }

  region_info.GetRange().SetRangeBase(load_addr);
  region_info.GetRange().SetRangeEnd(LLDB_INVALID_ADDRESS);
  region_info.SetReadable(MemoryRegionInfo::eNo);
  region_info.SetWritable(MemoryRegionInfo::eNo);
  region_info.SetExecutable(MemoryRegionInfo::eNo);
  region_info.SetMapped(MemoryRegionInfo::eNo);
  region_info.SetMemoryTagged(MemoryRegionInfo::eNo);
  return Status();
}

size_t ProcessElfCore::DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                    Status &error) {
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();

  if (core_objfile == nullptr)
    return 0;

  // Get the address range
  const VMRangeToFileOffset::Entry *address_range =
      m_core_aranges.FindEntryThatContains(addr);
  if (address_range == nullptr || address_range->GetRangeEnd() < addr) {
    error.SetErrorStringWithFormat("core file does not contain 0x%" PRIx64,
                                   addr);
    return 0;
  }

  // Convert the address into core file offset
  const lldb::addr_t offset = addr - address_range->GetRangeBase();
  const lldb::addr_t file_start = address_range->data.GetRangeBase();
  const lldb::addr_t file_end = address_range->data.GetRangeEnd();
  size_t bytes_to_read = size; // Number of bytes to read from the core file
  size_t bytes_copied = 0;   // Number of bytes actually read from the core file
  lldb::addr_t bytes_left =
      0; // Number of bytes available in the core file from the given address

  // Don't proceed if core file doesn't contain the actual data for this
  // address range.
  if (file_start == file_end)
    return 0;

  // Figure out how many on-disk bytes remain in this segment starting at the
  // given offset
  if (file_end > file_start + offset)
    bytes_left = file_end - (file_start + offset);

  if (bytes_to_read > bytes_left)
    bytes_to_read = bytes_left;

  // If there is data available on the core file read it
  if (bytes_to_read)
    bytes_copied =
        core_objfile->CopyData(offset + file_start, bytes_to_read, buf);

  return bytes_copied;
}

llvm::Expected<std::vector<lldb::addr_t>>
ProcessElfCore::ReadMemoryTags(lldb::addr_t addr, size_t len) {
  ObjectFile *core_objfile = m_core_module_sp->GetObjectFile();
  if (core_objfile == nullptr)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "No core object file.");

  llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
      GetMemoryTagManager();
  if (!tag_manager_or_err)
    return tag_manager_or_err.takeError();

  // LLDB only supports AArch64 MTE tag segments so we do not need to worry
  // about the segment type here. If you got here then you must have a tag
  // manager (meaning you are debugging AArch64) and all the segments in this
  // list will have had type PT_AARCH64_MEMTAG_MTE.
  const VMRangeToFileOffset::Entry *tag_entry =
      m_core_tag_ranges.FindEntryThatContains(addr);
  // If we don't have a tag segment or the range asked for extends outside the
  // segment.
  if (!tag_entry || (addr + len) >= tag_entry->GetRangeEnd())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "No tag segment that covers this range.");

  const MemoryTagManager *tag_manager = *tag_manager_or_err;
  return tag_manager->UnpackTagsFromCoreFileSegment(
      [core_objfile](lldb::offset_t offset, size_t length, void *dst) {
        return core_objfile->CopyData(offset, length, dst);
      },
      tag_entry->GetRangeBase(), tag_entry->data.GetRangeBase(), addr, len);
}

void ProcessElfCore::Clear() {
  m_thread_list.Clear();

  SetUnixSignals(std::make_shared<UnixSignals>());
}

void ProcessElfCore::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance);
  });
}

lldb::addr_t ProcessElfCore::GetImageInfoAddress() {
  ObjectFile *obj_file = GetTarget().GetExecutableModule()->GetObjectFile();
  Address addr = obj_file->GetImageInfoAddress(&GetTarget());

  if (addr.IsValid())
    return addr.GetLoadAddress(&GetTarget());
  return LLDB_INVALID_ADDRESS;
}

// Parse a FreeBSD NT_PRSTATUS note - see FreeBSD sys/procfs.h for details.
static void ParseFreeBSDPrStatus(ThreadData &thread_data,
                                 const DataExtractor &data,
                                 bool lp64) {
  lldb::offset_t offset = 0;
  int pr_version = data.GetU32(&offset);

  Log *log = GetLog(LLDBLog::Process);
  if (log) {
    if (pr_version > 1)
      LLDB_LOGF(log, "FreeBSD PRSTATUS unexpected version %d", pr_version);
  }

  // Skip padding, pr_statussz, pr_gregsetsz, pr_fpregsetsz, pr_osreldate
  if (lp64)
    offset += 32;
  else
    offset += 16;

  thread_data.signo = data.GetU32(&offset); // pr_cursig
  thread_data.tid = data.GetU32(&offset);   // pr_pid
  if (lp64)
    offset += 4;

  size_t len = data.GetByteSize() - offset;
  thread_data.gpregset = DataExtractor(data, offset, len);
}

// Parse a FreeBSD NT_PRPSINFO note - see FreeBSD sys/procfs.h for details.
static void ParseFreeBSDPrPsInfo(ProcessElfCore &process,
                                 const DataExtractor &data,
                                 bool lp64) {
  lldb::offset_t offset = 0;
  int pr_version = data.GetU32(&offset);

  Log *log = GetLog(LLDBLog::Process);
  if (log) {
    if (pr_version > 1)
      LLDB_LOGF(log, "FreeBSD PRPSINFO unexpected version %d", pr_version);
  }

  // Skip pr_psinfosz, pr_fname, pr_psargs
  offset += 108;
  if (lp64)
    offset += 4;

  process.SetID(data.GetU32(&offset)); // pr_pid
}

static llvm::Error ParseNetBSDProcInfo(const DataExtractor &data,
                                       uint32_t &cpi_nlwps,
                                       uint32_t &cpi_signo,
                                       uint32_t &cpi_siglwp,
                                       uint32_t &cpi_pid) {
  lldb::offset_t offset = 0;

  uint32_t version = data.GetU32(&offset);
  if (version != 1)
    return llvm::make_error<llvm::StringError>(
        "Error parsing NetBSD core(5) notes: Unsupported procinfo version",
        llvm::inconvertibleErrorCode());

  uint32_t cpisize = data.GetU32(&offset);
  if (cpisize != NETBSD::NT_PROCINFO_SIZE)
    return llvm::make_error<llvm::StringError>(
        "Error parsing NetBSD core(5) notes: Unsupported procinfo size",
        llvm::inconvertibleErrorCode());

  cpi_signo = data.GetU32(&offset); /* killing signal */

  offset += NETBSD::NT_PROCINFO_CPI_SIGCODE_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SIGPEND_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SIGMASK_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SIGIGNORE_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SIGCATCH_SIZE;
  cpi_pid = data.GetU32(&offset);
  offset += NETBSD::NT_PROCINFO_CPI_PPID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_PGRP_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_RUID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_EUID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SVUID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_RGID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_EGID_SIZE;
  offset += NETBSD::NT_PROCINFO_CPI_SVGID_SIZE;
  cpi_nlwps = data.GetU32(&offset); /* number of LWPs */

  offset += NETBSD::NT_PROCINFO_CPI_NAME_SIZE;
  cpi_siglwp = data.GetU32(&offset); /* LWP target of killing signal */

  return llvm::Error::success();
}

static void ParseOpenBSDProcInfo(ThreadData &thread_data,
                                 const DataExtractor &data) {
  lldb::offset_t offset = 0;

  int version = data.GetU32(&offset);
  if (version != 1)
    return;

  offset += 4;
  thread_data.signo = data.GetU32(&offset);
}

llvm::Expected<std::vector<CoreNote>>
ProcessElfCore::parseSegment(const DataExtractor &segment) {
  lldb::offset_t offset = 0;
  std::vector<CoreNote> result;

  while (offset < segment.GetByteSize()) {
    ELFNote note = ELFNote();
    if (!note.Parse(segment, &offset))
      return llvm::make_error<llvm::StringError>(
          "Unable to parse note segment", llvm::inconvertibleErrorCode());

    size_t note_start = offset;
    size_t note_size = llvm::alignTo(note.n_descsz, 4);

    result.push_back({note, DataExtractor(segment, note_start, note_size)});
    offset += note_size;
  }

  return std::move(result);
}

llvm::Error ProcessElfCore::parseFreeBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  ArchSpec arch = GetArchitecture();
  bool lp64 = (arch.GetMachine() == llvm::Triple::aarch64 ||
               arch.GetMachine() == llvm::Triple::mips64 ||
               arch.GetMachine() == llvm::Triple::ppc64 ||
               arch.GetMachine() == llvm::Triple::x86_64);
  bool have_prstatus = false;
  bool have_prpsinfo = false;
  ThreadData thread_data;
  for (const auto &note : notes) {
    if (note.info.n_name != "FreeBSD")
      continue;

    if ((note.info.n_type == ELF::NT_PRSTATUS && have_prstatus) ||
        (note.info.n_type == ELF::NT_PRPSINFO && have_prpsinfo)) {
      assert(thread_data.gpregset.GetByteSize() > 0);
      // Add the new thread to thread list
      m_thread_data.push_back(thread_data);
      thread_data = ThreadData();
      have_prstatus = false;
      have_prpsinfo = false;
    }

    switch (note.info.n_type) {
    case ELF::NT_PRSTATUS:
      have_prstatus = true;
      ParseFreeBSDPrStatus(thread_data, note.data, lp64);
      break;
    case ELF::NT_PRPSINFO:
      have_prpsinfo = true;
      ParseFreeBSDPrPsInfo(*this, note.data, lp64);
      break;
    case ELF::NT_FREEBSD_THRMISC: {
      lldb::offset_t offset = 0;
      thread_data.name = note.data.GetCStr(&offset, 20);
      break;
    }
    case ELF::NT_FREEBSD_PROCSTAT_AUXV:
      // FIXME: FreeBSD sticks an int at the beginning of the note
      m_auxv = DataExtractor(note.data, 4, note.data.GetByteSize() - 4);
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  if (!have_prstatus) {
    return llvm::make_error<llvm::StringError>(
        "Could not find NT_PRSTATUS note in core file.",
        llvm::inconvertibleErrorCode());
  }
  m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

/// NetBSD specific Thread context from PT_NOTE segment
///
/// NetBSD ELF core files use notes to provide information about
/// the process's state.  The note name is "NetBSD-CORE" for
/// information that is global to the process, and "NetBSD-CORE@nn",
/// where "nn" is the lwpid of the LWP that the information belongs
/// to (such as register state).
///
/// NetBSD uses the following note identifiers:
///
///      ELF_NOTE_NETBSD_CORE_PROCINFO (value 1)
///             Note is a "netbsd_elfcore_procinfo" structure.
///      ELF_NOTE_NETBSD_CORE_AUXV     (value 2; since NetBSD 8.0)
///             Note is an array of AuxInfo structures.
///
/// NetBSD also uses ptrace(2) request numbers (the ones that exist in
/// machine-dependent space) to identify register info notes.  The
/// info in such notes is in the same format that ptrace(2) would
/// export that information.
///
/// For more information see /usr/include/sys/exec_elf.h
///
llvm::Error ProcessElfCore::parseNetBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  ThreadData thread_data;
  bool had_nt_regs = false;

  // To be extracted from struct netbsd_elfcore_procinfo
  // Used to sanity check of the LWPs of the process
  uint32_t nlwps = 0;
  uint32_t signo = 0;  // killing signal
  uint32_t siglwp = 0; // LWP target of killing signal
  uint32_t pr_pid = 0;

  for (const auto &note : notes) {
    llvm::StringRef name = note.info.n_name;

    if (name == "NetBSD-CORE") {
      if (note.info.n_type == NETBSD::NT_PROCINFO) {
        llvm::Error error = ParseNetBSDProcInfo(note.data, nlwps, signo,
                                                siglwp, pr_pid);
        if (error)
          return error;
        SetID(pr_pid);
      } else if (note.info.n_type == NETBSD::NT_AUXV) {
        m_auxv = note.data;
      }
    } else if (name.consume_front("NetBSD-CORE@")) {
      lldb::tid_t tid;
      if (name.getAsInteger(10, tid))
        return llvm::make_error<llvm::StringError>(
            "Error parsing NetBSD core(5) notes: Cannot convert LWP ID "
            "to integer",
            llvm::inconvertibleErrorCode());

      switch (GetArchitecture().GetMachine()) {
      case llvm::Triple::aarch64: {
        // Assume order PT_GETREGS, PT_GETFPREGS
        if (note.info.n_type == NETBSD::AARCH64::NT_REGS) {
          // If this is the next thread, push the previous one first.
          if (had_nt_regs) {
            m_thread_data.push_back(thread_data);
            thread_data = ThreadData();
            had_nt_regs = false;
          }

          thread_data.gpregset = note.data;
          thread_data.tid = tid;
          if (thread_data.gpregset.GetByteSize() == 0)
            return llvm::make_error<llvm::StringError>(
                "Could not find general purpose registers note in core file.",
                llvm::inconvertibleErrorCode());
          had_nt_regs = true;
        } else if (note.info.n_type == NETBSD::AARCH64::NT_FPREGS) {
          if (!had_nt_regs || tid != thread_data.tid)
            return llvm::make_error<llvm::StringError>(
                "Error parsing NetBSD core(5) notes: Unexpected order "
                "of NOTEs PT_GETFPREG before PT_GETREG",
                llvm::inconvertibleErrorCode());
          thread_data.notes.push_back(note);
        }
      } break;
      case llvm::Triple::x86: {
        // Assume order PT_GETREGS, PT_GETFPREGS
        if (note.info.n_type == NETBSD::I386::NT_REGS) {
          // If this is the next thread, push the previous one first.
          if (had_nt_regs) {
            m_thread_data.push_back(thread_data);
            thread_data = ThreadData();
            had_nt_regs = false;
          }

          thread_data.gpregset = note.data;
          thread_data.tid = tid;
          if (thread_data.gpregset.GetByteSize() == 0)
            return llvm::make_error<llvm::StringError>(
                "Could not find general purpose registers note in core file.",
                llvm::inconvertibleErrorCode());
          had_nt_regs = true;
        } else if (note.info.n_type == NETBSD::I386::NT_FPREGS) {
          if (!had_nt_regs || tid != thread_data.tid)
            return llvm::make_error<llvm::StringError>(
                "Error parsing NetBSD core(5) notes: Unexpected order "
                "of NOTEs PT_GETFPREG before PT_GETREG",
                llvm::inconvertibleErrorCode());
          thread_data.notes.push_back(note);
        }
      } break;
      case llvm::Triple::x86_64: {
        // Assume order PT_GETREGS, PT_GETFPREGS
        if (note.info.n_type == NETBSD::AMD64::NT_REGS) {
          // If this is the next thread, push the previous one first.
          if (had_nt_regs) {
            m_thread_data.push_back(thread_data);
            thread_data = ThreadData();
            had_nt_regs = false;
          }

          thread_data.gpregset = note.data;
          thread_data.tid = tid;
          if (thread_data.gpregset.GetByteSize() == 0)
            return llvm::make_error<llvm::StringError>(
                "Could not find general purpose registers note in core file.",
                llvm::inconvertibleErrorCode());
          had_nt_regs = true;
        } else if (note.info.n_type == NETBSD::AMD64::NT_FPREGS) {
          if (!had_nt_regs || tid != thread_data.tid)
            return llvm::make_error<llvm::StringError>(
                "Error parsing NetBSD core(5) notes: Unexpected order "
                "of NOTEs PT_GETFPREG before PT_GETREG",
                llvm::inconvertibleErrorCode());
          thread_data.notes.push_back(note);
        }
      } break;
      default:
        break;
      }
    }
  }

  // Push the last thread.
  if (had_nt_regs)
    m_thread_data.push_back(thread_data);

  if (m_thread_data.empty())
    return llvm::make_error<llvm::StringError>(
        "Error parsing NetBSD core(5) notes: No threads information "
        "specified in notes",
        llvm::inconvertibleErrorCode());

  if (m_thread_data.size() != nlwps)
    return llvm::make_error<llvm::StringError>(
        "Error parsing NetBSD core(5) notes: Mismatch between the number "
        "of LWPs in netbsd_elfcore_procinfo and the number of LWPs specified "
        "by MD notes",
        llvm::inconvertibleErrorCode());

  // Signal targeted at the whole process.
  if (siglwp == 0) {
    for (auto &data : m_thread_data)
      data.signo = signo;
  }
  // Signal destined for a particular LWP.
  else {
    bool passed = false;

    for (auto &data : m_thread_data) {
      if (data.tid == siglwp) {
        data.signo = signo;
        passed = true;
        break;
      }
    }

    if (!passed)
      return llvm::make_error<llvm::StringError>(
          "Error parsing NetBSD core(5) notes: Signal passed to unknown LWP",
          llvm::inconvertibleErrorCode());
  }

  return llvm::Error::success();
}

llvm::Error ProcessElfCore::parseOpenBSDNotes(llvm::ArrayRef<CoreNote> notes) {
  ThreadData thread_data = {};
  for (const auto &note : notes) {
    // OpenBSD per-thread information is stored in notes named "OpenBSD@nnn" so
    // match on the initial part of the string.
    if (!llvm::StringRef(note.info.n_name).starts_with("OpenBSD"))
      continue;

    switch (note.info.n_type) {
    case OPENBSD::NT_PROCINFO:
      ParseOpenBSDProcInfo(thread_data, note.data);
      break;
    case OPENBSD::NT_AUXV:
      m_auxv = note.data;
      break;
    case OPENBSD::NT_REGS:
      thread_data.gpregset = note.data;
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  if (thread_data.gpregset.GetByteSize() == 0) {
    return llvm::make_error<llvm::StringError>(
        "Could not find general purpose registers note in core file.",
        llvm::inconvertibleErrorCode());
  }
  m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

/// A description of a linux process usually contains the following NOTE
/// entries:
/// - NT_PRPSINFO - General process information like pid, uid, name, ...
/// - NT_SIGINFO - Information about the signal that terminated the process
/// - NT_AUXV - Process auxiliary vector
/// - NT_FILE - Files mapped into memory
/// 
/// Additionally, for each thread in the process the core file will contain at
/// least the NT_PRSTATUS note, containing the thread id and general purpose
/// registers. It may include additional notes for other register sets (floating
/// point and vector registers, ...). The tricky part here is that some of these
/// notes have "CORE" in their owner fields, while other set it to "LINUX".
llvm::Error ProcessElfCore::parseLinuxNotes(llvm::ArrayRef<CoreNote> notes) {
  const ArchSpec &arch = GetArchitecture();
  bool have_prstatus = false;
  bool have_prpsinfo = false;
  ThreadData thread_data;
  for (const auto &note : notes) {
    if (note.info.n_name != "CORE" && note.info.n_name != "LINUX")
      continue;

    if ((note.info.n_type == ELF::NT_PRSTATUS && have_prstatus) ||
        (note.info.n_type == ELF::NT_PRPSINFO && have_prpsinfo)) {
      assert(thread_data.gpregset.GetByteSize() > 0);
      // Add the new thread to thread list
      m_thread_data.push_back(thread_data);
      thread_data = ThreadData();
      have_prstatus = false;
      have_prpsinfo = false;
    }

    switch (note.info.n_type) {
    case ELF::NT_PRSTATUS: {
      have_prstatus = true;
      ELFLinuxPrStatus prstatus;
      Status status = prstatus.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.prstatus_sig = prstatus.pr_cursig;
      thread_data.tid = prstatus.pr_pid;
      uint32_t header_size = ELFLinuxPrStatus::GetSize(arch);
      size_t len = note.data.GetByteSize() - header_size;
      thread_data.gpregset = DataExtractor(note.data, header_size, len);
      break;
    }
    case ELF::NT_PRPSINFO: {
      have_prpsinfo = true;
      ELFLinuxPrPsInfo prpsinfo;
      Status status = prpsinfo.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.name.assign (prpsinfo.pr_fname, strnlen (prpsinfo.pr_fname, sizeof (prpsinfo.pr_fname)));
      SetID(prpsinfo.pr_pid);
      break;
    }
    case ELF::NT_SIGINFO: {
      ELFLinuxSigInfo siginfo;
      Status status = siginfo.Parse(note.data, arch);
      if (status.Fail())
        return status.ToError();
      thread_data.signo = siginfo.si_signo;
      thread_data.code = siginfo.si_code;
      break;
    }
    case ELF::NT_FILE: {
      m_nt_file_entries.clear();
      lldb::offset_t offset = 0;
      const uint64_t count = note.data.GetAddress(&offset);
      note.data.GetAddress(&offset); // Skip page size
      for (uint64_t i = 0; i < count; ++i) {
        NT_FILE_Entry entry;
        entry.start = note.data.GetAddress(&offset);
        entry.end = note.data.GetAddress(&offset);
        entry.file_ofs = note.data.GetAddress(&offset);
        m_nt_file_entries.push_back(entry);
      }
      for (uint64_t i = 0; i < count; ++i) {
        const char *path = note.data.GetCStr(&offset);
        if (path && path[0])
          m_nt_file_entries[i].path.assign(path);
      }
      break;
    }
    case ELF::NT_AUXV:
      m_auxv = note.data;
      break;
    default:
      thread_data.notes.push_back(note);
      break;
    }
  }
  // Add last entry in the note section
  if (have_prstatus)
    m_thread_data.push_back(thread_data);
  return llvm::Error::success();
}

/// Parse Thread context from PT_NOTE segment and store it in the thread list
/// A note segment consists of one or more NOTE entries, but their types and
/// meaning differ depending on the OS.
llvm::Error ProcessElfCore::ParseThreadContextsFromNoteSegment(
    const elf::ELFProgramHeader &segment_header,
    const DataExtractor &segment_data) {
  assert(segment_header.p_type == llvm::ELF::PT_NOTE);

  auto notes_or_error = parseSegment(segment_data);
  if(!notes_or_error)
    return notes_or_error.takeError();
  switch (GetArchitecture().GetTriple().getOS()) {
  case llvm::Triple::FreeBSD:
    return parseFreeBSDNotes(*notes_or_error);
  case llvm::Triple::Linux:
    return parseLinuxNotes(*notes_or_error);
  case llvm::Triple::NetBSD:
    return parseNetBSDNotes(*notes_or_error);
  case llvm::Triple::OpenBSD:
    return parseOpenBSDNotes(*notes_or_error);
  default:
    return llvm::make_error<llvm::StringError>(
        "Don't know how to parse core file. Unsupported OS.",
        llvm::inconvertibleErrorCode());
  }
}

UUID ProcessElfCore::FindBuidIdInCoreMemory(lldb::addr_t address) {
  UUID invalid_uuid;
  const uint32_t addr_size = GetAddressByteSize();
  const size_t elf_header_size = addr_size == 4 ? sizeof(llvm::ELF::Elf32_Ehdr)
                                                : sizeof(llvm::ELF::Elf64_Ehdr);

  std::vector<uint8_t> elf_header_bytes;
  elf_header_bytes.resize(elf_header_size);
  Status error;
  size_t byte_read =
      ReadMemory(address, elf_header_bytes.data(), elf_header_size, error);
  if (byte_read != elf_header_size ||
      !elf::ELFHeader::MagicBytesMatch(elf_header_bytes.data()))
    return invalid_uuid;
  DataExtractor elf_header_data(elf_header_bytes.data(), elf_header_size,
                                GetByteOrder(), addr_size);
  lldb::offset_t offset = 0;

  elf::ELFHeader elf_header;
  elf_header.Parse(elf_header_data, &offset);

  const lldb::addr_t ph_addr = address + elf_header.e_phoff;

  std::vector<uint8_t> ph_bytes;
  ph_bytes.resize(elf_header.e_phentsize);
  for (unsigned int i = 0; i < elf_header.e_phnum; ++i) {
    byte_read = ReadMemory(ph_addr + i * elf_header.e_phentsize,
                           ph_bytes.data(), elf_header.e_phentsize, error);
    if (byte_read != elf_header.e_phentsize)
      break;
    DataExtractor program_header_data(ph_bytes.data(), elf_header.e_phentsize,
                                      GetByteOrder(), addr_size);
    offset = 0;
    elf::ELFProgramHeader program_header;
    program_header.Parse(program_header_data, &offset);
    if (program_header.p_type != llvm::ELF::PT_NOTE)
      continue;

    std::vector<uint8_t> note_bytes;
    note_bytes.resize(program_header.p_memsz);

    byte_read = ReadMemory(program_header.p_vaddr, note_bytes.data(),
                           program_header.p_memsz, error);
    if (byte_read != program_header.p_memsz)
      continue;
    DataExtractor segment_data(note_bytes.data(), note_bytes.size(),
                               GetByteOrder(), addr_size);
    auto notes_or_error = parseSegment(segment_data);
    if (!notes_or_error)
      return invalid_uuid;
    for (const CoreNote &note : *notes_or_error) {
      if (note.info.n_namesz == 4 &&
          note.info.n_type == llvm::ELF::NT_GNU_BUILD_ID &&
          "GNU" == note.info.n_name &&
          note.data.ValidOffsetForDataOfSize(0, note.info.n_descsz))
        return UUID(note.data.GetData().take_front(note.info.n_descsz));
    }
  }
  return invalid_uuid;
}

uint32_t ProcessElfCore::GetNumThreadContexts() {
  if (!m_thread_data_valid)
    DoLoadCore();
  return m_thread_data.size();
}

ArchSpec ProcessElfCore::GetArchitecture() {
  ArchSpec arch = m_core_module_sp->GetObjectFile()->GetArchitecture();

  ArchSpec target_arch = GetTarget().GetArchitecture();
  arch.MergeFrom(target_arch);

  // On MIPS there is no way to differentiate betwenn 32bit and 64bit core
  // files and this information can't be merged in from the target arch so we
  // fail back to unconditionally returning the target arch in this config.
  if (target_arch.IsMIPS()) {
    return target_arch;
  }

  return arch;
}

DataExtractor ProcessElfCore::GetAuxvData() {
  const uint8_t *start = m_auxv.GetDataStart();
  size_t len = m_auxv.GetByteSize();
  lldb::DataBufferSP buffer(new lldb_private::DataBufferHeap(start, len));
  return DataExtractor(buffer, GetByteOrder(), GetAddressByteSize());
}

bool ProcessElfCore::GetProcessInfo(ProcessInstanceInfo &info) {
  info.Clear();
  info.SetProcessID(GetID());
  info.SetArchitecture(GetArchitecture());
  lldb::ModuleSP module_sp = GetTarget().GetExecutableModule();
  if (module_sp) {
    const bool add_exe_file_as_first_arg = false;
    info.SetExecutableFile(GetTarget().GetExecutableModule()->GetFileSpec(),
                           add_exe_file_as_first_arg);
  }
  return true;
}
