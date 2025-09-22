//===-- ProcessMinidump.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProcessMinidump.h"

#include "ThreadMinidump.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Target/JITLoaderList.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"

#include "Plugins/ObjectFile/Placeholder/ObjectFilePlaceholder.h"
#include "Plugins/Process/Utility/StopInfoMachException.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace minidump;

LLDB_PLUGIN_DEFINE(ProcessMinidump)

namespace {

/// Duplicate the HashElfTextSection() from the breakpad sources.
///
/// Breakpad, a Google crash log reporting tool suite, creates minidump files
/// for many different architectures. When using Breakpad to create ELF
/// minidumps, it will check for a GNU build ID when creating a minidump file
/// and if one doesn't exist in the file, it will say the UUID of the file is a
/// checksum of up to the first 4096 bytes of the .text section. Facebook also
/// uses breakpad and modified this hash to avoid collisions so we can
/// calculate and check for this as well.
///
/// The breakpad code might end up hashing up to 15 bytes that immediately
/// follow the .text section in the file, so this code must do exactly what it
/// does so we can get an exact match for the UUID.
///
/// \param[in] module_sp The module to grab the .text section from.
///
/// \param[in,out] breakpad_uuid A vector that will receive the calculated
///                breakpad .text hash.
///
/// \param[in,out] facebook_uuid A vector that will receive the calculated
///                facebook .text hash.
///
void HashElfTextSection(ModuleSP module_sp, std::vector<uint8_t> &breakpad_uuid,
                        std::vector<uint8_t> &facebook_uuid) {
  SectionList *sect_list = module_sp->GetSectionList();
  if (sect_list == nullptr)
    return;
  SectionSP sect_sp = sect_list->FindSectionByName(ConstString(".text"));
  if (!sect_sp)
    return;
  constexpr size_t kMDGUIDSize = 16;
  constexpr size_t kBreakpadPageSize = 4096;
  // The breakpad code has a bug where it might access beyond the end of a
  // .text section by up to 15 bytes, so we must ensure we round up to the
  // next kMDGUIDSize byte boundary.
  DataExtractor data;
  const size_t text_size = sect_sp->GetFileSize();
  const size_t read_size = std::min<size_t>(
      llvm::alignTo(text_size, kMDGUIDSize), kBreakpadPageSize);
  sect_sp->GetObjectFile()->GetData(sect_sp->GetFileOffset(), read_size, data);

  breakpad_uuid.assign(kMDGUIDSize, 0);
  facebook_uuid.assign(kMDGUIDSize, 0);

  // The only difference between the breakpad hash and the facebook hash is the
  // hashing of the text section size into the hash prior to hashing the .text
  // contents.
  for (size_t i = 0; i < kMDGUIDSize; i++)
    facebook_uuid[i] ^= text_size % 255;

  // This code carefully duplicates how the hash was created in Breakpad
  // sources, including the error where it might has an extra 15 bytes past the
  // end of the .text section if the .text section is less than a page size in
  // length.
  const uint8_t *ptr = data.GetDataStart();
  const uint8_t *ptr_end = data.GetDataEnd();
  while (ptr < ptr_end) {
    for (unsigned i = 0; i < kMDGUIDSize; i++) {
      breakpad_uuid[i] ^= ptr[i];
      facebook_uuid[i] ^= ptr[i];
    }
    ptr += kMDGUIDSize;
  }
}

} // namespace

llvm::StringRef ProcessMinidump::GetPluginDescriptionStatic() {
  return "Minidump plug-in.";
}

lldb::ProcessSP ProcessMinidump::CreateInstance(lldb::TargetSP target_sp,
                                                lldb::ListenerSP listener_sp,
                                                const FileSpec *crash_file,
                                                bool can_connect) {
  if (!crash_file || can_connect)
    return nullptr;

  lldb::ProcessSP process_sp;
  // Read enough data for the Minidump header
  constexpr size_t header_size = sizeof(Header);
  auto DataPtr = FileSystem::Instance().CreateDataBuffer(crash_file->GetPath(),
                                                         header_size, 0);
  if (!DataPtr)
    return nullptr;

  lldbassert(DataPtr->GetByteSize() == header_size);
  if (identify_magic(toStringRef(DataPtr->GetData())) != llvm::file_magic::minidump)
    return nullptr;

  auto AllData =
      FileSystem::Instance().CreateDataBuffer(crash_file->GetPath(), -1, 0);
  if (!AllData)
    return nullptr;

  return std::make_shared<ProcessMinidump>(target_sp, listener_sp, *crash_file,
                                           std::move(AllData));
}

bool ProcessMinidump::CanDebug(lldb::TargetSP target_sp,
                               bool plugin_specified_by_name) {
  return true;
}

ProcessMinidump::ProcessMinidump(lldb::TargetSP target_sp,
                                 lldb::ListenerSP listener_sp,
                                 const FileSpec &core_file,
                                 DataBufferSP core_data)
    : PostMortemProcess(target_sp, listener_sp, core_file),
      m_core_data(std::move(core_data)), m_active_exception(nullptr),
      m_is_wow64(false) {}

ProcessMinidump::~ProcessMinidump() {
  Clear();
  // We need to call finalize on the process before destroying ourselves to
  // make sure all of the broadcaster cleanup goes as planned. If we destruct
  // this class, then Process::~Process() might have problems trying to fully
  // destroy the broadcaster.
  Finalize(true /* destructing */);
}

void ProcessMinidump::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(),
                                  ProcessMinidump::CreateInstance);
  });
}

void ProcessMinidump::Terminate() {
  PluginManager::UnregisterPlugin(ProcessMinidump::CreateInstance);
}

Status ProcessMinidump::DoLoadCore() {
  auto expected_parser = MinidumpParser::Create(m_core_data);
  if (!expected_parser)
    return Status(expected_parser.takeError());
  m_minidump_parser = std::move(*expected_parser);

  Status error;

  // Do we support the minidump's architecture?
  ArchSpec arch = GetArchitecture();
  switch (arch.GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
  case llvm::Triple::arm:
  case llvm::Triple::aarch64:
    // Any supported architectures must be listed here and also supported in
    // ThreadMinidump::CreateRegisterContextForFrame().
    break;
  default:
    error.SetErrorStringWithFormat("unsupported minidump architecture: %s",
                                   arch.GetArchitectureName());
    return error;
  }
  GetTarget().SetArchitecture(arch, true /*set_platform*/);

  m_thread_list = m_minidump_parser->GetThreads();
  m_active_exception = m_minidump_parser->GetExceptionStream();

  SetUnixSignals(UnixSignals::Create(GetArchitecture()));

  ReadModuleList();
  if (ModuleSP module = GetTarget().GetExecutableModule())
    GetTarget().MergeArchitecture(module->GetArchitecture());
  std::optional<lldb::pid_t> pid = m_minidump_parser->GetPid();
  if (!pid) {
    Debugger::ReportWarning("unable to retrieve process ID from minidump file, "
                            "setting process ID to 1",
                            GetTarget().GetDebugger().GetID());
    pid = 1;
  }
  SetID(*pid);

  return error;
}

Status ProcessMinidump::DoDestroy() { return Status(); }

void ProcessMinidump::RefreshStateAfterStop() {

  if (!m_active_exception)
    return;

  constexpr uint32_t BreakpadDumpRequested = 0xFFFFFFFF;
  if (m_active_exception->ExceptionRecord.ExceptionCode ==
      BreakpadDumpRequested) {
    // This "ExceptionCode" value is a sentinel that is sometimes used
    // when generating a dump for a process that hasn't crashed.

    // TODO: The definition and use of this "dump requested" constant
    // in Breakpad are actually Linux-specific, and for similar use
    // cases on Mac/Windows it defines different constants, referring
    // to them as "simulated" exceptions; consider moving this check
    // down to the OS-specific paths and checking each OS for its own
    // constant.
    return;
  }

  lldb::StopInfoSP stop_info;
  lldb::ThreadSP stop_thread;

  Process::m_thread_list.SetSelectedThreadByID(m_active_exception->ThreadId);
  stop_thread = Process::m_thread_list.GetSelectedThread();
  ArchSpec arch = GetArchitecture();

  if (arch.GetTriple().getOS() == llvm::Triple::Linux) {
    uint32_t signo = m_active_exception->ExceptionRecord.ExceptionCode;

    if (signo == 0) {
      // No stop.
      return;
    }

    stop_info = StopInfo::CreateStopReasonWithSignal(
        *stop_thread, signo);
  } else if (arch.GetTriple().getVendor() == llvm::Triple::Apple) {
    stop_info = StopInfoMachException::CreateStopReasonWithMachException(
        *stop_thread, m_active_exception->ExceptionRecord.ExceptionCode, 2,
        m_active_exception->ExceptionRecord.ExceptionFlags,
        m_active_exception->ExceptionRecord.ExceptionAddress, 0);
  } else {
    std::string desc;
    llvm::raw_string_ostream desc_stream(desc);
    desc_stream << "Exception "
                << llvm::format_hex(
                       m_active_exception->ExceptionRecord.ExceptionCode, 8)
                << " encountered at address "
                << llvm::format_hex(
                       m_active_exception->ExceptionRecord.ExceptionAddress, 8);
    stop_info = StopInfo::CreateStopReasonWithException(
        *stop_thread, desc_stream.str().c_str());
  }

  stop_thread->SetStopInfo(stop_info);
}

bool ProcessMinidump::IsAlive() { return true; }

bool ProcessMinidump::WarnBeforeDetach() const { return false; }

size_t ProcessMinidump::ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                   Status &error) {
  // Don't allow the caching that lldb_private::Process::ReadMemory does since
  // we have it all cached in our dump file anyway.
  return DoReadMemory(addr, buf, size, error);
}

size_t ProcessMinidump::DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                                     Status &error) {

  llvm::ArrayRef<uint8_t> mem = m_minidump_parser->GetMemory(addr, size);
  if (mem.empty()) {
    error.SetErrorString("could not parse memory info");
    return 0;
  }

  std::memcpy(buf, mem.data(), mem.size());
  return mem.size();
}

ArchSpec ProcessMinidump::GetArchitecture() {
  if (!m_is_wow64) {
    return m_minidump_parser->GetArchitecture();
  }

  llvm::Triple triple;
  triple.setVendor(llvm::Triple::VendorType::UnknownVendor);
  triple.setArch(llvm::Triple::ArchType::x86);
  triple.setOS(llvm::Triple::OSType::Win32);
  return ArchSpec(triple);
}

void ProcessMinidump::BuildMemoryRegions() {
  if (m_memory_regions)
    return;
  m_memory_regions.emplace();
  bool is_complete;
  std::tie(*m_memory_regions, is_complete) =
      m_minidump_parser->BuildMemoryRegions();

  if (is_complete)
    return;

  MemoryRegionInfos to_add;
  ModuleList &modules = GetTarget().GetImages();
  SectionLoadList &load_list = GetTarget().GetSectionLoadList();
  modules.ForEach([&](const ModuleSP &module_sp) {
    SectionList *sections = module_sp->GetSectionList();
    for (size_t i = 0; i < sections->GetSize(); ++i) {
      SectionSP section_sp = sections->GetSectionAtIndex(i);
      addr_t load_addr = load_list.GetSectionLoadAddress(section_sp);
      if (load_addr == LLDB_INVALID_ADDRESS)
        continue;
      MemoryRegionInfo::RangeType section_range(load_addr,
                                                section_sp->GetByteSize());
      MemoryRegionInfo region =
          MinidumpParser::GetMemoryRegionInfo(*m_memory_regions, load_addr);
      if (region.GetMapped() != MemoryRegionInfo::eYes &&
          region.GetRange().GetRangeBase() <= section_range.GetRangeBase() &&
          section_range.GetRangeEnd() <= region.GetRange().GetRangeEnd()) {
        to_add.emplace_back();
        to_add.back().GetRange() = section_range;
        to_add.back().SetLLDBPermissions(section_sp->GetPermissions());
        to_add.back().SetMapped(MemoryRegionInfo::eYes);
        to_add.back().SetName(module_sp->GetFileSpec().GetPath().c_str());
      }
    }
    return true;
  });
  m_memory_regions->insert(m_memory_regions->end(), to_add.begin(),
                           to_add.end());
  llvm::sort(*m_memory_regions);
}

Status ProcessMinidump::DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                                              MemoryRegionInfo &region) {
  BuildMemoryRegions();
  region = MinidumpParser::GetMemoryRegionInfo(*m_memory_regions, load_addr);
  return Status();
}

Status ProcessMinidump::GetMemoryRegions(MemoryRegionInfos &region_list) {
  BuildMemoryRegions();
  region_list = *m_memory_regions;
  return Status();
}

void ProcessMinidump::Clear() { Process::m_thread_list.Clear(); }

bool ProcessMinidump::DoUpdateThreadList(ThreadList &old_thread_list,
                                         ThreadList &new_thread_list) {
  for (const minidump::Thread &thread : m_thread_list) {
    LocationDescriptor context_location = thread.Context;

    // If the minidump contains an exception context, use it
    if (m_active_exception != nullptr &&
        m_active_exception->ThreadId == thread.ThreadId) {
      context_location = m_active_exception->ThreadContext;
    }

    llvm::ArrayRef<uint8_t> context;
    if (!m_is_wow64)
      context = m_minidump_parser->GetThreadContext(context_location);
    else
      context = m_minidump_parser->GetThreadContextWow64(thread);

    lldb::ThreadSP thread_sp(new ThreadMinidump(*this, thread, context));
    new_thread_list.AddThread(thread_sp);
  }
  return new_thread_list.GetSize(false) > 0;
}

ModuleSP ProcessMinidump::GetOrCreateModule(UUID minidump_uuid,
                                            llvm::StringRef name,
                                            ModuleSpec module_spec) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  Status error;

  ModuleSP module_sp =
      GetTarget().GetOrCreateModule(module_spec, true /* notify */, &error);
  if (!module_sp)
    return module_sp;
  // We consider the module to be a match if the minidump UUID is a
  // prefix of the actual UUID, or if either of the UUIDs are empty.
  const auto dmp_bytes = minidump_uuid.GetBytes();
  const auto mod_bytes = module_sp->GetUUID().GetBytes();
  const bool match = dmp_bytes.empty() || mod_bytes.empty() ||
                     mod_bytes.take_front(dmp_bytes.size()) == dmp_bytes;
  if (match) {
    LLDB_LOG(log, "Partial uuid match for {0}.", name);
    return module_sp;
  }

  // Breakpad generates minindump files, and if there is no GNU build
  // ID in the binary, it will calculate a UUID by hashing first 4096
  // bytes of the .text section and using that as the UUID for a module
  // in the minidump. Facebook uses a modified breakpad client that
  // uses a slightly modified this hash to avoid collisions. Check for
  // UUIDs from the minindump that match these cases and accept the
  // module we find if they do match.
  std::vector<uint8_t> breakpad_uuid;
  std::vector<uint8_t> facebook_uuid;
  HashElfTextSection(module_sp, breakpad_uuid, facebook_uuid);
  if (dmp_bytes == llvm::ArrayRef<uint8_t>(breakpad_uuid)) {
    LLDB_LOG(log, "Breakpad .text hash match for {0}.", name);
    return module_sp;
  }
  if (dmp_bytes == llvm::ArrayRef<uint8_t>(facebook_uuid)) {
    LLDB_LOG(log, "Facebook .text hash match for {0}.", name);
    return module_sp;
  }
  // The UUID wasn't a partial match and didn't match the .text hash
  // so remove the module from the target, we will need to create a
  // placeholder object file.
  GetTarget().GetImages().Remove(module_sp);
  module_sp.reset();
  return module_sp;
}

void ProcessMinidump::ReadModuleList() {
  std::vector<const minidump::Module *> filtered_modules =
      m_minidump_parser->GetFilteredModuleList();

  Log *log = GetLog(LLDBLog::DynamicLoader);

  for (auto module : filtered_modules) {
    std::string name = cantFail(m_minidump_parser->GetMinidumpFile().getString(
        module->ModuleNameRVA));
    const uint64_t load_addr = module->BaseOfImage;
    const uint64_t load_size = module->SizeOfImage;
    LLDB_LOG(log, "found module: name: {0} {1:x10}-{2:x10} size: {3}", name,
             load_addr, load_addr + load_size, load_size);

    // check if the process is wow64 - a 32 bit windows process running on a
    // 64 bit windows
    if (llvm::StringRef(name).ends_with_insensitive("wow64.dll")) {
      m_is_wow64 = true;
    }

    const auto uuid = m_minidump_parser->GetModuleUUID(module);
    auto file_spec = FileSpec(name, GetArchitecture().GetTriple());
    ModuleSpec module_spec(file_spec, uuid);
    module_spec.GetArchitecture() = GetArchitecture();
    Status error;
    // Try and find a module with a full UUID that matches. This function will
    // add the module to the target if it finds one.
    lldb::ModuleSP module_sp = GetTarget().GetOrCreateModule(module_spec,
                                                     true /* notify */, &error);
    if (module_sp) {
      LLDB_LOG(log, "Full uuid match for {0}.", name);
    } else {
      // We couldn't find a module with an exactly-matching UUID.  Sometimes
      // a minidump UUID is only a partial match or is a hash.  So try again
      // without specifying the UUID, then again without specifying the
      // directory if that fails.  This will allow us to find modules with
      // partial matches or hash UUIDs in user-provided sysroots or search
      // directories (target.exec-search-paths).
      ModuleSpec partial_module_spec = module_spec;
      partial_module_spec.GetUUID().Clear();
      module_sp = GetOrCreateModule(uuid, name, partial_module_spec);
      if (!module_sp) {
        partial_module_spec.GetFileSpec().ClearDirectory();
        module_sp = GetOrCreateModule(uuid, name, partial_module_spec);
      }
    }
    if (module_sp) {
      // Watch out for place holder modules that have different paths, but the
      // same UUID. If the base address is different, create a new module. If
      // we don't then we will end up setting the load address of a different
      // ObjectFilePlaceholder and an assertion will fire.
      auto *objfile = module_sp->GetObjectFile();
      if (objfile &&
          objfile->GetPluginName() ==
              ObjectFilePlaceholder::GetPluginNameStatic()) {
        if (((ObjectFilePlaceholder *)objfile)->GetBaseImageAddress() !=
            load_addr)
          module_sp.reset();
      }
    }
    if (!module_sp) {
      // We failed to locate a matching local object file. Fortunately, the
      // minidump format encodes enough information about each module's memory
      // range to allow us to create placeholder modules.
      //
      // This enables most LLDB functionality involving address-to-module
      // translations (ex. identifing the module for a stack frame PC) and
      // modules/sections commands (ex. target modules list, ...)
      LLDB_LOG(log,
               "Unable to locate the matching object file, creating a "
               "placeholder module for: {0}",
               name);

      module_sp = Module::CreateModuleFromObjectFile<ObjectFilePlaceholder>(
          module_spec, load_addr, load_size);
      GetTarget().GetImages().Append(module_sp, true /* notify */);
    }

    bool load_addr_changed = false;
    module_sp->SetLoadAddress(GetTarget(), load_addr, false,
                              load_addr_changed);
  }
}

bool ProcessMinidump::GetProcessInfo(ProcessInstanceInfo &info) {
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

// For minidumps there's no runtime generated code so we don't need JITLoader(s)
// Avoiding them will also speed up minidump loading since JITLoaders normally
// try to set up symbolic breakpoints, which in turn may force loading more
// debug information than needed.
JITLoaderList &ProcessMinidump::GetJITLoaders() {
  if (!m_jit_loaders_up) {
    m_jit_loaders_up = std::make_unique<JITLoaderList>();
  }
  return *m_jit_loaders_up;
}

#define INIT_BOOL(VAR, LONG, SHORT, DESC) \
    VAR(LLDB_OPT_SET_1, false, LONG, SHORT, DESC, false, true)
#define APPEND_OPT(VAR) \
    m_option_group.Append(&VAR, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1)

class CommandObjectProcessMinidumpDump : public CommandObjectParsed {
private:
  OptionGroupOptions m_option_group;
  OptionGroupBoolean m_dump_all;
  OptionGroupBoolean m_dump_directory;
  OptionGroupBoolean m_dump_linux_cpuinfo;
  OptionGroupBoolean m_dump_linux_proc_status;
  OptionGroupBoolean m_dump_linux_lsb_release;
  OptionGroupBoolean m_dump_linux_cmdline;
  OptionGroupBoolean m_dump_linux_environ;
  OptionGroupBoolean m_dump_linux_auxv;
  OptionGroupBoolean m_dump_linux_maps;
  OptionGroupBoolean m_dump_linux_proc_stat;
  OptionGroupBoolean m_dump_linux_proc_uptime;
  OptionGroupBoolean m_dump_linux_proc_fd;
  OptionGroupBoolean m_dump_linux_all;
  OptionGroupBoolean m_fb_app_data;
  OptionGroupBoolean m_fb_build_id;
  OptionGroupBoolean m_fb_version;
  OptionGroupBoolean m_fb_java_stack;
  OptionGroupBoolean m_fb_dalvik;
  OptionGroupBoolean m_fb_unwind;
  OptionGroupBoolean m_fb_error_log;
  OptionGroupBoolean m_fb_app_state;
  OptionGroupBoolean m_fb_abort;
  OptionGroupBoolean m_fb_thread;
  OptionGroupBoolean m_fb_logcat;
  OptionGroupBoolean m_fb_all;

  void SetDefaultOptionsIfNoneAreSet() {
    if (m_dump_all.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_all.GetOptionValue().GetCurrentValue() ||
        m_fb_all.GetOptionValue().GetCurrentValue() ||
        m_dump_directory.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_cpuinfo.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_status.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_lsb_release.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_cmdline.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_environ.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_auxv.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_maps.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_stat.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_uptime.GetOptionValue().GetCurrentValue() ||
        m_dump_linux_proc_fd.GetOptionValue().GetCurrentValue() ||
        m_fb_app_data.GetOptionValue().GetCurrentValue() ||
        m_fb_build_id.GetOptionValue().GetCurrentValue() ||
        m_fb_version.GetOptionValue().GetCurrentValue() ||
        m_fb_java_stack.GetOptionValue().GetCurrentValue() ||
        m_fb_dalvik.GetOptionValue().GetCurrentValue() ||
        m_fb_unwind.GetOptionValue().GetCurrentValue() ||
        m_fb_error_log.GetOptionValue().GetCurrentValue() ||
        m_fb_app_state.GetOptionValue().GetCurrentValue() ||
        m_fb_abort.GetOptionValue().GetCurrentValue() ||
        m_fb_thread.GetOptionValue().GetCurrentValue() ||
        m_fb_logcat.GetOptionValue().GetCurrentValue())
      return;
    // If no options were set, then dump everything
    m_dump_all.GetOptionValue().SetCurrentValue(true);
  }
  bool DumpAll() const {
    return m_dump_all.GetOptionValue().GetCurrentValue();
  }
  bool DumpDirectory() const {
    return DumpAll() ||
        m_dump_directory.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinux() const {
    return DumpAll() || m_dump_linux_all.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxCPUInfo() const {
    return DumpLinux() ||
        m_dump_linux_cpuinfo.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcStatus() const {
    return DumpLinux() ||
        m_dump_linux_proc_status.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcStat() const {
    return DumpLinux() ||
        m_dump_linux_proc_stat.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxLSBRelease() const {
    return DumpLinux() ||
        m_dump_linux_lsb_release.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxCMDLine() const {
    return DumpLinux() ||
        m_dump_linux_cmdline.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxEnviron() const {
    return DumpLinux() ||
        m_dump_linux_environ.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxAuxv() const {
    return DumpLinux() ||
        m_dump_linux_auxv.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxMaps() const {
    return DumpLinux() ||
        m_dump_linux_maps.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcUptime() const {
    return DumpLinux() ||
        m_dump_linux_proc_uptime.GetOptionValue().GetCurrentValue();
  }
  bool DumpLinuxProcFD() const {
    return DumpLinux() ||
        m_dump_linux_proc_fd.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebook() const {
    return DumpAll() || m_fb_all.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookAppData() const {
    return DumpFacebook() || m_fb_app_data.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookBuildID() const {
    return DumpFacebook() || m_fb_build_id.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookVersionName() const {
    return DumpFacebook() || m_fb_version.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookJavaStack() const {
    return DumpFacebook() || m_fb_java_stack.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookDalvikInfo() const {
    return DumpFacebook() || m_fb_dalvik.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookUnwindSymbols() const {
    return DumpFacebook() || m_fb_unwind.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookErrorLog() const {
    return DumpFacebook() || m_fb_error_log.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookAppStateLog() const {
    return DumpFacebook() || m_fb_app_state.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookAbortReason() const {
    return DumpFacebook() || m_fb_abort.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookThreadName() const {
    return DumpFacebook() || m_fb_thread.GetOptionValue().GetCurrentValue();
  }
  bool DumpFacebookLogcat() const {
    return DumpFacebook() || m_fb_logcat.GetOptionValue().GetCurrentValue();
  }
public:
  CommandObjectProcessMinidumpDump(CommandInterpreter &interpreter)
  : CommandObjectParsed(interpreter, "process plugin dump",
      "Dump information from the minidump file.", nullptr),
    m_option_group(),
    INIT_BOOL(m_dump_all, "all", 'a',
              "Dump the everything in the minidump."),
    INIT_BOOL(m_dump_directory, "directory", 'd',
              "Dump the minidump directory map."),
    INIT_BOOL(m_dump_linux_cpuinfo, "cpuinfo", 'C',
              "Dump linux /proc/cpuinfo."),
    INIT_BOOL(m_dump_linux_proc_status, "status", 's',
              "Dump linux /proc/<pid>/status."),
    INIT_BOOL(m_dump_linux_lsb_release, "lsb-release", 'r',
              "Dump linux /etc/lsb-release."),
    INIT_BOOL(m_dump_linux_cmdline, "cmdline", 'c',
              "Dump linux /proc/<pid>/cmdline."),
    INIT_BOOL(m_dump_linux_environ, "environ", 'e',
              "Dump linux /proc/<pid>/environ."),
    INIT_BOOL(m_dump_linux_auxv, "auxv", 'x',
              "Dump linux /proc/<pid>/auxv."),
    INIT_BOOL(m_dump_linux_maps, "maps", 'm',
              "Dump linux /proc/<pid>/maps."),
    INIT_BOOL(m_dump_linux_proc_stat, "stat", 'S',
              "Dump linux /proc/<pid>/stat."),
    INIT_BOOL(m_dump_linux_proc_uptime, "uptime", 'u',
              "Dump linux process uptime."),
    INIT_BOOL(m_dump_linux_proc_fd, "fd", 'f',
              "Dump linux /proc/<pid>/fd."),
    INIT_BOOL(m_dump_linux_all, "linux", 'l',
              "Dump all linux streams."),
    INIT_BOOL(m_fb_app_data, "fb-app-data", 1,
              "Dump Facebook application custom data."),
    INIT_BOOL(m_fb_build_id, "fb-build-id", 2,
              "Dump the Facebook build ID."),
    INIT_BOOL(m_fb_version, "fb-version", 3,
              "Dump Facebook application version string."),
    INIT_BOOL(m_fb_java_stack, "fb-java-stack", 4,
              "Dump Facebook java stack."),
    INIT_BOOL(m_fb_dalvik, "fb-dalvik-info", 5,
              "Dump Facebook Dalvik info."),
    INIT_BOOL(m_fb_unwind, "fb-unwind-symbols", 6,
              "Dump Facebook unwind symbols."),
    INIT_BOOL(m_fb_error_log, "fb-error-log", 7,
              "Dump Facebook error log."),
    INIT_BOOL(m_fb_app_state, "fb-app-state-log", 8,
              "Dump Facebook java stack."),
    INIT_BOOL(m_fb_abort, "fb-abort-reason", 9,
              "Dump Facebook abort reason."),
    INIT_BOOL(m_fb_thread, "fb-thread-name", 10,
              "Dump Facebook thread name."),
    INIT_BOOL(m_fb_logcat, "fb-logcat", 11,
              "Dump Facebook logcat."),
    INIT_BOOL(m_fb_all, "facebook", 12, "Dump all Facebook streams.") {
    APPEND_OPT(m_dump_all);
    APPEND_OPT(m_dump_directory);
    APPEND_OPT(m_dump_linux_cpuinfo);
    APPEND_OPT(m_dump_linux_proc_status);
    APPEND_OPT(m_dump_linux_lsb_release);
    APPEND_OPT(m_dump_linux_cmdline);
    APPEND_OPT(m_dump_linux_environ);
    APPEND_OPT(m_dump_linux_auxv);
    APPEND_OPT(m_dump_linux_maps);
    APPEND_OPT(m_dump_linux_proc_stat);
    APPEND_OPT(m_dump_linux_proc_uptime);
    APPEND_OPT(m_dump_linux_proc_fd);
    APPEND_OPT(m_dump_linux_all);
    APPEND_OPT(m_fb_app_data);
    APPEND_OPT(m_fb_build_id);
    APPEND_OPT(m_fb_version);
    APPEND_OPT(m_fb_java_stack);
    APPEND_OPT(m_fb_dalvik);
    APPEND_OPT(m_fb_unwind);
    APPEND_OPT(m_fb_error_log);
    APPEND_OPT(m_fb_app_state);
    APPEND_OPT(m_fb_abort);
    APPEND_OPT(m_fb_thread);
    APPEND_OPT(m_fb_logcat);
    APPEND_OPT(m_fb_all);
    m_option_group.Finalize();
  }

  ~CommandObjectProcessMinidumpDump() override = default;

  Options *GetOptions() override { return &m_option_group; }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc > 0) {
      result.AppendErrorWithFormat("'%s' take no arguments, only options",
                                   m_cmd_name.c_str());
      return;
    }
    SetDefaultOptionsIfNoneAreSet();

    ProcessMinidump *process = static_cast<ProcessMinidump *>(
        m_interpreter.GetExecutionContext().GetProcessPtr());
    result.SetStatus(eReturnStatusSuccessFinishResult);
    Stream &s = result.GetOutputStream();
    MinidumpParser &minidump = *process->m_minidump_parser;
    if (DumpDirectory()) {
      s.Printf("RVA        SIZE       TYPE       StreamType\n");
      s.Printf("---------- ---------- ---------- --------------------------\n");
      for (const auto &stream_desc : minidump.GetMinidumpFile().streams())
        s.Printf(
            "0x%8.8x 0x%8.8x 0x%8.8x %s\n", (uint32_t)stream_desc.Location.RVA,
            (uint32_t)stream_desc.Location.DataSize,
            (unsigned)(StreamType)stream_desc.Type,
            MinidumpParser::GetStreamTypeAsString(stream_desc.Type).data());
      s.Printf("\n");
    }
    auto DumpTextStream = [&](StreamType stream_type,
                              llvm::StringRef label) -> void {
      auto bytes = minidump.GetStream(stream_type);
      if (!bytes.empty()) {
        if (label.empty())
          label = MinidumpParser::GetStreamTypeAsString(stream_type);
        s.Printf("%s:\n%s\n\n", label.data(), bytes.data());
      }
    };
    auto DumpBinaryStream = [&](StreamType stream_type,
                                llvm::StringRef label) -> void {
      auto bytes = minidump.GetStream(stream_type);
      if (!bytes.empty()) {
        if (label.empty())
          label = MinidumpParser::GetStreamTypeAsString(stream_type);
        s.Printf("%s:\n", label.data());
        DataExtractor data(bytes.data(), bytes.size(), eByteOrderLittle,
                           process->GetAddressByteSize());
        DumpDataExtractor(data, &s, 0, lldb::eFormatBytesWithASCII, 1,
                          bytes.size(), 16, 0, 0, 0);
        s.Printf("\n\n");
      }
    };

    if (DumpLinuxCPUInfo())
      DumpTextStream(StreamType::LinuxCPUInfo, "/proc/cpuinfo");
    if (DumpLinuxProcStatus())
      DumpTextStream(StreamType::LinuxProcStatus, "/proc/PID/status");
    if (DumpLinuxLSBRelease())
      DumpTextStream(StreamType::LinuxLSBRelease, "/etc/lsb-release");
    if (DumpLinuxCMDLine())
      DumpTextStream(StreamType::LinuxCMDLine, "/proc/PID/cmdline");
    if (DumpLinuxEnviron())
      DumpTextStream(StreamType::LinuxEnviron, "/proc/PID/environ");
    if (DumpLinuxAuxv())
      DumpBinaryStream(StreamType::LinuxAuxv, "/proc/PID/auxv");
    if (DumpLinuxMaps())
      DumpTextStream(StreamType::LinuxMaps, "/proc/PID/maps");
    if (DumpLinuxProcStat())
      DumpTextStream(StreamType::LinuxProcStat, "/proc/PID/stat");
    if (DumpLinuxProcUptime())
      DumpTextStream(StreamType::LinuxProcUptime, "uptime");
    if (DumpLinuxProcFD())
      DumpTextStream(StreamType::LinuxProcFD, "/proc/PID/fd");
    if (DumpFacebookAppData())
      DumpTextStream(StreamType::FacebookAppCustomData,
                     "Facebook App Data");
    if (DumpFacebookBuildID()) {
      auto bytes = minidump.GetStream(StreamType::FacebookBuildID);
      if (bytes.size() >= 4) {
        DataExtractor data(bytes.data(), bytes.size(), eByteOrderLittle,
                           process->GetAddressByteSize());
        lldb::offset_t offset = 0;
        uint32_t build_id = data.GetU32(&offset);
        s.Printf("Facebook Build ID:\n");
        s.Printf("%u\n", build_id);
        s.Printf("\n");
      }
    }
    if (DumpFacebookVersionName())
      DumpTextStream(StreamType::FacebookAppVersionName,
                     "Facebook Version String");
    if (DumpFacebookJavaStack())
      DumpTextStream(StreamType::FacebookJavaStack,
                     "Facebook Java Stack");
    if (DumpFacebookDalvikInfo())
      DumpTextStream(StreamType::FacebookDalvikInfo,
                     "Facebook Dalvik Info");
    if (DumpFacebookUnwindSymbols())
      DumpBinaryStream(StreamType::FacebookUnwindSymbols,
                       "Facebook Unwind Symbols Bytes");
    if (DumpFacebookErrorLog())
      DumpTextStream(StreamType::FacebookDumpErrorLog,
                     "Facebook Error Log");
    if (DumpFacebookAppStateLog())
      DumpTextStream(StreamType::FacebookAppStateLog,
                     "Faceook Application State Log");
    if (DumpFacebookAbortReason())
      DumpTextStream(StreamType::FacebookAbortReason,
                     "Facebook Abort Reason");
    if (DumpFacebookThreadName())
      DumpTextStream(StreamType::FacebookThreadName,
                     "Facebook Thread Name");
    if (DumpFacebookLogcat())
      DumpTextStream(StreamType::FacebookLogcat, "Facebook Logcat");
  }
};

class CommandObjectMultiwordProcessMinidump : public CommandObjectMultiword {
public:
  CommandObjectMultiwordProcessMinidump(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "process plugin",
          "Commands for operating on a ProcessMinidump process.",
          "process plugin <subcommand> [<subcommand-options>]") {
    LoadSubCommand("dump",
        CommandObjectSP(new CommandObjectProcessMinidumpDump(interpreter)));
  }

  ~CommandObjectMultiwordProcessMinidump() override = default;
};

CommandObject *ProcessMinidump::GetPluginCommandObject() {
  if (!m_command_sp)
    m_command_sp = std::make_shared<CommandObjectMultiwordProcessMinidump>(
        GetTarget().GetDebugger().GetCommandInterpreter());
  return m_command_sp.get();
}
