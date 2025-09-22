//===-- DynamicLoaderDarwinKernel.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
#include "Plugins/Platform/MacOSX/PlatformDarwinKernel.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/Section.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Utility/AddressableBits.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

#include "DynamicLoaderDarwinKernel.h"

#include <algorithm>
#include <memory>

//#define ENABLE_DEBUG_PRINTF // COMMENT THIS LINE OUT PRIOR TO CHECKIN
#ifdef ENABLE_DEBUG_PRINTF
#include <cstdio>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(DynamicLoaderDarwinKernel)

// Progressively greater amounts of scanning we will allow For some targets
// very early in startup, we can't do any random reads of memory or we can
// crash the device so a setting is needed that can completely disable the
// KASLR scans.

enum KASLRScanType {
  eKASLRScanNone = 0,        // No reading into the inferior at all
  eKASLRScanLowgloAddresses, // Check one word of memory for a possible kernel
                             // addr, then see if a kernel is there
  eKASLRScanNearPC, // Scan backwards from the current $pc looking for kernel;
                    // checking at 96 locations total
  eKASLRScanExhaustiveScan // Scan through the entire possible kernel address
                           // range looking for a kernel
};

static constexpr OptionEnumValueElement g_kaslr_kernel_scan_enum_values[] = {
    {
        eKASLRScanNone,
        "none",
        "Do not read memory looking for a Darwin kernel when attaching.",
    },
    {
        eKASLRScanLowgloAddresses,
        "basic",
        "Check for the Darwin kernel's load addr in the lowglo page "
        "(boot-args=debug) only.",
    },
    {
        eKASLRScanNearPC,
        "fast-scan",
        "Scan near the pc value on attach to find the Darwin kernel's load "
        "address.",
    },
    {
        eKASLRScanExhaustiveScan,
        "exhaustive-scan",
        "Scan through the entire potential address range of Darwin kernel "
        "(only on 32-bit targets).",
    },
};

#define LLDB_PROPERTIES_dynamicloaderdarwinkernel
#include "DynamicLoaderDarwinKernelProperties.inc"

enum {
#define LLDB_PROPERTIES_dynamicloaderdarwinkernel
#include "DynamicLoaderDarwinKernelPropertiesEnum.inc"
};

class DynamicLoaderDarwinKernelProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    static constexpr llvm::StringLiteral g_setting_name("darwin-kernel");
    return g_setting_name;
  }

  DynamicLoaderDarwinKernelProperties() : Properties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_dynamicloaderdarwinkernel_properties);
  }

  ~DynamicLoaderDarwinKernelProperties() override = default;

  bool GetLoadKexts() const {
    const uint32_t idx = ePropertyLoadKexts;
    return GetPropertyAtIndexAs<bool>(
        idx,
        g_dynamicloaderdarwinkernel_properties[idx].default_uint_value != 0);
  }

  KASLRScanType GetScanType() const {
    const uint32_t idx = ePropertyScanType;
    return GetPropertyAtIndexAs<KASLRScanType>(
        idx,
        static_cast<KASLRScanType>(
            g_dynamicloaderdarwinkernel_properties[idx].default_uint_value));
  }
};

static DynamicLoaderDarwinKernelProperties &GetGlobalProperties() {
  static DynamicLoaderDarwinKernelProperties g_settings;
  return g_settings;
}

static bool is_kernel(Module *module) {
  if (!module)
    return false;
  ObjectFile *objfile = module->GetObjectFile();
  if (!objfile)
    return false;
  if (objfile->GetType() != ObjectFile::eTypeExecutable)
    return false;
  if (objfile->GetStrata() != ObjectFile::eStrataKernel)
    return false;

  return true;
}

// Create an instance of this class. This function is filled into the plugin
// info class that gets handed out by the plugin factory and allows the lldb to
// instantiate an instance of this class.
DynamicLoader *DynamicLoaderDarwinKernel::CreateInstance(Process *process,
                                                         bool force) {
  if (!force) {
    // If the user provided an executable binary and it is not a kernel, this
    // plugin should not create an instance.
    Module *exec = process->GetTarget().GetExecutableModulePointer();
    if (exec && !is_kernel(exec))
      return nullptr;

    // If the target's architecture does not look like an Apple environment,
    // this plugin should not create an instance.
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    switch (triple_ref.getOS()) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
    case llvm::Triple::IOS:
    case llvm::Triple::TvOS:
    case llvm::Triple::WatchOS:
    case llvm::Triple::XROS:
    case llvm::Triple::BridgeOS:
      if (triple_ref.getVendor() != llvm::Triple::Apple) {
        return nullptr;
      }
      break;
    // If we have triple like armv7-unknown-unknown, we should try looking for
    // a Darwin kernel.
    case llvm::Triple::UnknownOS:
      break;
    default:
      return nullptr;
      break;
    }
  }

  // At this point if there is an ExecutableModule, it is a kernel and the
  // Target is some variant of an Apple system. If the Process hasn't provided
  // the kernel load address, we need to look around in memory to find it.
  const addr_t kernel_load_address = SearchForDarwinKernel(process);
  if (CheckForKernelImageAtAddress(kernel_load_address, process).IsValid()) {
    return new DynamicLoaderDarwinKernel(process, kernel_load_address);
  }
  return nullptr;
}

lldb::addr_t
DynamicLoaderDarwinKernel::SearchForDarwinKernel(Process *process) {
  addr_t kernel_load_address = process->GetImageInfoAddress();
  if (kernel_load_address == LLDB_INVALID_ADDRESS)
    kernel_load_address = SearchForKernelAtSameLoadAddr(process);
  if (kernel_load_address == LLDB_INVALID_ADDRESS)
    kernel_load_address = SearchForKernelWithDebugHints(process);
  if (kernel_load_address == LLDB_INVALID_ADDRESS)
    kernel_load_address = SearchForKernelNearPC(process);
  if (kernel_load_address == LLDB_INVALID_ADDRESS)
    kernel_load_address = SearchForKernelViaExhaustiveSearch(process);

  return kernel_load_address;
}

// Check if the kernel binary is loaded in memory without a slide. First verify
// that the ExecutableModule is a kernel before we proceed. Returns the address
// of the kernel if one was found, else LLDB_INVALID_ADDRESS.
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelAtSameLoadAddr(Process *process) {
  Module *exe_module = process->GetTarget().GetExecutableModulePointer();

  if (!is_kernel(process->GetTarget().GetExecutableModulePointer()))
    return LLDB_INVALID_ADDRESS;

  ObjectFile *exe_objfile = exe_module->GetObjectFile();

  if (!exe_objfile->GetBaseAddress().IsValid())
    return LLDB_INVALID_ADDRESS;

  if (CheckForKernelImageAtAddress(
          exe_objfile->GetBaseAddress().GetFileAddress(), process) ==
      exe_module->GetUUID())
    return exe_objfile->GetBaseAddress().GetFileAddress();

  return LLDB_INVALID_ADDRESS;
}

// If the debug flag is included in the boot-args nvram setting, the kernel's
// load address will be noted in the lowglo page at a fixed address Returns the
// address of the kernel if one was found, else LLDB_INVALID_ADDRESS.
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelWithDebugHints(Process *process) {
  if (GetGlobalProperties().GetScanType() == eKASLRScanNone)
    return LLDB_INVALID_ADDRESS;

  Status read_err;
  addr_t kernel_addresses_64[] = {
      0xfffffff000002010ULL,
      0xfffffff000004010ULL, // newest arm64 devices
      0xffffff8000004010ULL, // 2014-2015-ish arm64 devices
      0xffffff8000002010ULL, // oldest arm64 devices
      LLDB_INVALID_ADDRESS};
  addr_t kernel_addresses_32[] = {0xffff0110, // 2016 and earlier armv7 devices
                                  0xffff1010, LLDB_INVALID_ADDRESS};

  uint8_t uval[8];
  if (process->GetAddressByteSize() == 8) {
  for (size_t i = 0; kernel_addresses_64[i] != LLDB_INVALID_ADDRESS; i++) {
      if (process->ReadMemoryFromInferior (kernel_addresses_64[i], uval, 8, read_err) == 8)
      {
          DataExtractor data (&uval, 8, process->GetByteOrder(), process->GetAddressByteSize());
          offset_t offset = 0;
          uint64_t addr = data.GetU64 (&offset);
          if (CheckForKernelImageAtAddress(addr, process).IsValid()) {
              return addr;
          }
      }
  }
  }

  if (process->GetAddressByteSize() == 4) {
  for (size_t i = 0; kernel_addresses_32[i] != LLDB_INVALID_ADDRESS; i++) {
      if (process->ReadMemoryFromInferior (kernel_addresses_32[i], uval, 4, read_err) == 4)
      {
          DataExtractor data (&uval, 4, process->GetByteOrder(), process->GetAddressByteSize());
          offset_t offset = 0;
          uint32_t addr = data.GetU32 (&offset);
          if (CheckForKernelImageAtAddress(addr, process).IsValid()) {
              return addr;
          }
      }
  }
  }

  return LLDB_INVALID_ADDRESS;
}

// If the kernel is currently executing when lldb attaches, and we don't have a
// better way of finding the kernel's load address, try searching backwards
// from the current pc value looking for the kernel's Mach header in memory.
// Returns the address of the kernel if one was found, else
// LLDB_INVALID_ADDRESS.
lldb::addr_t
DynamicLoaderDarwinKernel::SearchForKernelNearPC(Process *process) {
  if (GetGlobalProperties().GetScanType() == eKASLRScanNone ||
      GetGlobalProperties().GetScanType() == eKASLRScanLowgloAddresses) {
    return LLDB_INVALID_ADDRESS;
  }

  ThreadSP thread = process->GetThreadList().GetSelectedThread();
  if (thread.get() == nullptr)
    return LLDB_INVALID_ADDRESS;
  addr_t pc = thread->GetRegisterContext()->GetPC(LLDB_INVALID_ADDRESS);

  int ptrsize = process->GetTarget().GetArchitecture().GetAddressByteSize();

  // The kernel is always loaded in high memory, if the top bit is zero,
  // this isn't a kernel.
  if (ptrsize == 8) {
    if ((pc & (1ULL << 63)) == 0) {
      return LLDB_INVALID_ADDRESS;
    }
  } else {
    if ((pc & (1ULL << 31)) == 0) {
      return LLDB_INVALID_ADDRESS;
    }
  }

  if (pc == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;

  int pagesize = 0x4000;  // 16k pages on 64-bit targets
  if (ptrsize == 4)
    pagesize = 0x1000;    // 4k pages on 32-bit targets

  // The kernel will be loaded on a page boundary.
  // Round the current pc down to the nearest page boundary.
  addr_t addr = pc & ~(pagesize - 1ULL);

  // Search backwards for 128 megabytes, or first memory read error.
  while (pc - addr < 128 * 0x100000) {
    bool read_error;
    if (CheckForKernelImageAtAddress(addr, process, &read_error).IsValid())
      return addr;

    // Stop scanning on the first read error we encounter; we've walked
    // past this executable block of memory.
    if (read_error == true)
      break;

    addr -= pagesize;
  }

  return LLDB_INVALID_ADDRESS;
}

// Scan through the valid address range for a kernel binary. This is uselessly
// slow in 64-bit environments so we don't even try it. This scan is not
// enabled by default even for 32-bit targets. Returns the address of the
// kernel if one was found, else LLDB_INVALID_ADDRESS.
lldb::addr_t DynamicLoaderDarwinKernel::SearchForKernelViaExhaustiveSearch(
    Process *process) {
  if (GetGlobalProperties().GetScanType() != eKASLRScanExhaustiveScan) {
    return LLDB_INVALID_ADDRESS;
  }

  addr_t kernel_range_low, kernel_range_high;
  if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8) {
    kernel_range_low = 1ULL << 63;
    kernel_range_high = UINT64_MAX;
  } else {
    kernel_range_low = 1ULL << 31;
    kernel_range_high = UINT32_MAX;
  }

  // Stepping through memory at one-megabyte resolution looking for a kernel
  // rarely works (fast enough) with a 64-bit address space -- for now, let's
  // not even bother.  We may be attaching to something which *isn't* a kernel
  // and we don't want to spin for minutes on-end looking for a kernel.
  if (process->GetTarget().GetArchitecture().GetAddressByteSize() == 8)
    return LLDB_INVALID_ADDRESS;

  addr_t addr = kernel_range_low;

  while (addr >= kernel_range_low && addr < kernel_range_high) {
    // x86_64 kernels are at offset 0
    if (CheckForKernelImageAtAddress(addr, process).IsValid())
      return addr;
    // 32-bit arm kernels are at offset 0x1000 (one 4k page)
    if (CheckForKernelImageAtAddress(addr + 0x1000, process).IsValid())
      return addr + 0x1000;
    // 64-bit arm kernels are at offset 0x4000 (one 16k page)
    if (CheckForKernelImageAtAddress(addr + 0x4000, process).IsValid())
      return addr + 0x4000;
    addr += 0x100000;
  }
  return LLDB_INVALID_ADDRESS;
}

// Read the mach_header struct out of memory and return it.
// Returns true if the mach_header was successfully read,
// Returns false if there was a problem reading the header, or it was not
// a Mach-O header.

bool
DynamicLoaderDarwinKernel::ReadMachHeader(addr_t addr, Process *process, llvm::MachO::mach_header &header,
                                          bool *read_error) {
  Status error;
  if (read_error)
    *read_error = false;

  // Read the mach header and see whether it looks like a kernel
  if (process->ReadMemory(addr, &header, sizeof(header), error) !=
      sizeof(header)) {
    if (read_error)
      *read_error = true;
    return false;
  }

  const uint32_t magicks[] = { llvm::MachO::MH_MAGIC_64, llvm::MachO::MH_MAGIC, llvm::MachO::MH_CIGAM, llvm::MachO::MH_CIGAM_64};

  bool found_matching_pattern = false;
  for (size_t i = 0; i < std::size(magicks); i++)
    if (::memcmp (&header.magic, &magicks[i], sizeof (uint32_t)) == 0)
        found_matching_pattern = true;

  if (!found_matching_pattern)
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

  return true;
}

// Given an address in memory, look to see if there is a kernel image at that
// address.
// Returns a UUID; if a kernel was not found at that address, UUID.IsValid()
// will be false.
lldb_private::UUID
DynamicLoaderDarwinKernel::CheckForKernelImageAtAddress(lldb::addr_t addr,
                                                        Process *process,
                                                        bool *read_error) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  if (addr == LLDB_INVALID_ADDRESS) {
    if (read_error)
      *read_error = true;
    return UUID();
  }

  LLDB_LOGF(log,
            "DynamicLoaderDarwinKernel::CheckForKernelImageAtAddress: "
            "looking for kernel binary at 0x%" PRIx64,
            addr);

  llvm::MachO::mach_header header;

  if (!ReadMachHeader(addr, process, header, read_error))
    return UUID();

  // First try a quick test -- read the first 4 bytes and see if there is a
  // valid Mach-O magic field there
  // (the first field of the mach_header/mach_header_64 struct).
  // A kernel is an executable which does not have the dynamic link object flag
  // set.
  if (header.filetype == llvm::MachO::MH_EXECUTE &&
      (header.flags & llvm::MachO::MH_DYLDLINK) == 0) {
    // Create a full module to get the UUID
    ModuleSP memory_module_sp =
        process->ReadModuleFromMemory(FileSpec("temp_mach_kernel"), addr);
    if (!memory_module_sp.get())
      return UUID();

    ObjectFile *exe_objfile = memory_module_sp->GetObjectFile();
    if (exe_objfile == nullptr) {
      LLDB_LOGF(log,
                "DynamicLoaderDarwinKernel::CheckForKernelImageAtAddress "
                "found a binary at 0x%" PRIx64
                " but could not create an object file from memory",
                addr);
      return UUID();
    }

    if (is_kernel(memory_module_sp.get())) {
      ArchSpec kernel_arch(eArchTypeMachO, header.cputype, header.cpusubtype);
      if (!process->GetTarget().GetArchitecture().IsCompatibleMatch(
              kernel_arch)) {
        process->GetTarget().SetArchitecture(kernel_arch);
      }
      if (log) {
        std::string uuid_str;
        if (memory_module_sp->GetUUID().IsValid()) {
          uuid_str = "with UUID ";
          uuid_str += memory_module_sp->GetUUID().GetAsString();
        } else {
          uuid_str = "and no LC_UUID found in load commands ";
        }
        LLDB_LOGF(
            log,
            "DynamicLoaderDarwinKernel::CheckForKernelImageAtAddress: "
            "kernel binary image found at 0x%" PRIx64 " with arch '%s' %s",
            addr, kernel_arch.GetTriple().str().c_str(), uuid_str.c_str());
      }
      return memory_module_sp->GetUUID();
    }
  }

  return UUID();
}

// Constructor
DynamicLoaderDarwinKernel::DynamicLoaderDarwinKernel(Process *process,
                                                     lldb::addr_t kernel_addr)
    : DynamicLoader(process), m_kernel_load_address(kernel_addr), m_kernel(),
      m_kext_summary_header_ptr_addr(), m_kext_summary_header_addr(),
      m_kext_summary_header(), m_known_kexts(), m_mutex(),
      m_break_id(LLDB_INVALID_BREAK_ID) {
  Status error;
  process->SetCanRunCode(false);
  PlatformSP platform_sp =
      process->GetTarget().GetDebugger().GetPlatformList().Create(
          PlatformDarwinKernel::GetPluginNameStatic());
  if (platform_sp.get())
    process->GetTarget().SetPlatform(platform_sp);
}

// Destructor
DynamicLoaderDarwinKernel::~DynamicLoaderDarwinKernel() { Clear(true); }

void DynamicLoaderDarwinKernel::UpdateIfNeeded() {
  LoadKernelModuleIfNeeded();
  SetNotificationBreakpointIfNeeded();
}

/// We've attached to a remote connection, or read a corefile.
/// Now load the kernel binary and potentially the kexts, add
/// them to the Target.
void DynamicLoaderDarwinKernel::DidAttach() {
  PrivateInitialize(m_process);
  UpdateIfNeeded();
}

/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
void DynamicLoaderDarwinKernel::DidLaunch() {
  PrivateInitialize(m_process);
  UpdateIfNeeded();
}

// Clear out the state of this class.
void DynamicLoaderDarwinKernel::Clear(bool clear_process) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (m_process->IsAlive() && LLDB_BREAK_ID_IS_VALID(m_break_id))
    m_process->ClearBreakpointSiteByID(m_break_id);

  if (clear_process)
    m_process = nullptr;
  m_kernel.Clear();
  m_known_kexts.clear();
  m_kext_summary_header_ptr_addr.Clear();
  m_kext_summary_header_addr.Clear();
  m_break_id = LLDB_INVALID_BREAK_ID;
}

bool DynamicLoaderDarwinKernel::KextImageInfo::LoadImageAtFileAddress(
    Process *process) {
  if (IsLoaded())
    return true;

  if (m_module_sp) {
    bool changed = false;
    if (m_module_sp->SetLoadAddress(process->GetTarget(), 0, true, changed))
      m_load_process_stop_id = process->GetStopID();
  }
  return false;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetModule(ModuleSP module_sp) {
  m_module_sp = module_sp;
  m_kernel_image = is_kernel(module_sp.get());
}

ModuleSP DynamicLoaderDarwinKernel::KextImageInfo::GetModule() {
  return m_module_sp;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetLoadAddress(
    addr_t load_addr) {
  m_load_address = load_addr;
}

addr_t DynamicLoaderDarwinKernel::KextImageInfo::GetLoadAddress() const {
  return m_load_address;
}

uint64_t DynamicLoaderDarwinKernel::KextImageInfo::GetSize() const {
  return m_size;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetSize(uint64_t size) {
  m_size = size;
}

uint32_t DynamicLoaderDarwinKernel::KextImageInfo::GetProcessStopId() const {
  return m_load_process_stop_id;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetProcessStopId(
    uint32_t stop_id) {
  m_load_process_stop_id = stop_id;
}

bool DynamicLoaderDarwinKernel::KextImageInfo::operator==(
    const KextImageInfo &rhs) const {
  if (m_uuid.IsValid() || rhs.GetUUID().IsValid()) {
    return m_uuid == rhs.GetUUID();
  }

  return m_name == rhs.GetName() && m_load_address == rhs.GetLoadAddress();
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetName(const char *name) {
  m_name = name;
}

std::string DynamicLoaderDarwinKernel::KextImageInfo::GetName() const {
  return m_name;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetUUID(const UUID &uuid) {
  m_uuid = uuid;
}

UUID DynamicLoaderDarwinKernel::KextImageInfo::GetUUID() const {
  return m_uuid;
}

// Given the m_load_address from the kext summaries, and a UUID, try to create
// an in-memory Module at that address.  Require that the MemoryModule have a
// matching UUID and detect if this MemoryModule is a kernel or a kext.
//
// Returns true if m_memory_module_sp is now set to a valid Module.

bool DynamicLoaderDarwinKernel::KextImageInfo::ReadMemoryModule(
    Process *process) {
  Log *log = GetLog(LLDBLog::Host);
  if (m_memory_module_sp.get() != nullptr)
    return true;
  if (m_load_address == LLDB_INVALID_ADDRESS)
    return false;

  FileSpec file_spec(m_name.c_str());

  llvm::MachO::mach_header mh;
  size_t size_to_read = 512;
  if (ReadMachHeader(m_load_address, process, mh)) {
    if (mh.magic == llvm::MachO::MH_CIGAM || mh.magic == llvm::MachO::MH_MAGIC)
      size_to_read = sizeof(llvm::MachO::mach_header) + mh.sizeofcmds;
    if (mh.magic == llvm::MachO::MH_CIGAM_64 ||
        mh.magic == llvm::MachO::MH_MAGIC_64)
      size_to_read = sizeof(llvm::MachO::mach_header_64) + mh.sizeofcmds;
  }

  ModuleSP memory_module_sp =
      process->ReadModuleFromMemory(file_spec, m_load_address, size_to_read);

  if (memory_module_sp.get() == nullptr)
    return false;

  bool this_is_kernel = is_kernel(memory_module_sp.get());

  // If this is a kext, and the kernel specified what UUID we should find at
  // this load address, require that the memory module have a matching UUID or
  // something has gone wrong and we should discard it.
  if (m_uuid.IsValid()) {
    if (m_uuid != memory_module_sp->GetUUID()) {
      if (log) {
        LLDB_LOGF(log,
                  "KextImageInfo::ReadMemoryModule the kernel said to find "
                  "uuid %s at 0x%" PRIx64
                  " but instead we found uuid %s, throwing it away",
                  m_uuid.GetAsString().c_str(), m_load_address,
                  memory_module_sp->GetUUID().GetAsString().c_str());
      }
      return false;
    }
  }

  // If the in-memory Module has a UUID, let's use that.
  if (!m_uuid.IsValid() && memory_module_sp->GetUUID().IsValid()) {
    m_uuid = memory_module_sp->GetUUID();
  }

  m_memory_module_sp = memory_module_sp;
  m_kernel_image = this_is_kernel;
  if (this_is_kernel) {
    if (log) {
      // This is unusual and probably not intended
      LLDB_LOGF(log,
                "KextImageInfo::ReadMemoryModule read the kernel binary out "
                "of memory");
    }
    if (memory_module_sp->GetArchitecture().IsValid()) {
      process->GetTarget().SetArchitecture(memory_module_sp->GetArchitecture());
    }
  }

  return true;
}

bool DynamicLoaderDarwinKernel::KextImageInfo::IsKernel() const {
  return m_kernel_image;
}

void DynamicLoaderDarwinKernel::KextImageInfo::SetIsKernel(bool is_kernel) {
  m_kernel_image = is_kernel;
}

bool DynamicLoaderDarwinKernel::KextImageInfo::LoadImageUsingMemoryModule(
    Process *process, Progress *progress) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  if (IsLoaded())
    return true;

  Target &target = process->GetTarget();

  // kexts will have a uuid from the table.
  // for the kernel, we'll need to read the load commands out of memory to get it.
  if (m_uuid.IsValid() == false) {
    if (ReadMemoryModule(process) == false) {
      Log *log = GetLog(LLDBLog::DynamicLoader);
      LLDB_LOGF(log,
                "Unable to read '%s' from memory at address 0x%" PRIx64
                " to get the segment load addresses.",
                m_name.c_str(), m_load_address);
      return false;
    }
  }

  if (IsKernel() && m_uuid.IsValid()) {
    Stream &s = target.GetDebugger().GetOutputStream();
    s.Printf("Kernel UUID: %s\n", m_uuid.GetAsString().c_str());
    s.Printf("Load Address: 0x%" PRIx64 "\n", m_load_address);

    // Start of a kernel debug session, we have the UUID of the kernel.
    // Go through the target's list of modules and if there are any kernel
    // modules with non-matching UUIDs, remove them.  The user may have added
    // the wrong kernel binary manually and it will only confuse things.
    ModuleList incorrect_kernels;
    for (ModuleSP module_sp : target.GetImages().Modules()) {
      if (is_kernel(module_sp.get()) && module_sp->GetUUID() != m_uuid)
        incorrect_kernels.Append(module_sp);
    }
    target.GetImages().Remove(incorrect_kernels);
  }

  if (!m_module_sp) {
    // See if the kext has already been loaded into the target, probably by the
    // user doing target modules add.
    const ModuleList &target_images = target.GetImages();
    m_module_sp = target_images.FindModule(m_uuid);

    StreamString prog_str;
    // 'mach_kernel' is a fake name we make up to find kernels
    // that were located by the local filesystem scan.
    if (GetName() != "mach_kernel")
      prog_str << GetName() << " ";
    if (GetUUID().IsValid())
      prog_str << GetUUID().GetAsString() << " ";
    if (GetLoadAddress() != LLDB_INVALID_ADDRESS) {
      prog_str << "at 0x";
      prog_str.PutHex64(GetLoadAddress());
    }

    std::unique_ptr<Progress> progress_up;
    if (progress)
      progress->Increment(1, prog_str.GetString().str());
    else {
      if (IsKernel())
        progress_up = std::make_unique<Progress>("Loading kernel",
                                                 prog_str.GetString().str());
      else
        progress_up = std::make_unique<Progress>("Loading kext",
                                                 prog_str.GetString().str());
    }

    // Search for the kext on the local filesystem via the UUID
    if (!m_module_sp && m_uuid.IsValid()) {
      ModuleSpec module_spec;
      module_spec.GetUUID() = m_uuid;
      if (!m_uuid.IsValid())
        module_spec.GetArchitecture() = target.GetArchitecture();
      module_spec.GetFileSpec() = FileSpec(m_name);

      // If the current platform is PlatformDarwinKernel, create a ModuleSpec
      // with the filename set to be the bundle ID for this kext, e.g.
      // "com.apple.filesystems.msdosfs", and ask the platform to find it.
      // PlatformDarwinKernel does a special scan for kexts on the local
      // system.
      PlatformSP platform_sp(target.GetPlatform());
      if (platform_sp) {
        FileSpecList search_paths = target.GetExecutableSearchPaths();
        platform_sp->GetSharedModule(module_spec, process, m_module_sp,
                                     &search_paths, nullptr, nullptr);
      }

      // Ask the Target to find this file on the local system, if possible.
      // This will search in the list of currently-loaded files, look in the
      // standard search paths on the system, and on a Mac it will try calling
      // the DebugSymbols framework with the UUID to find the binary via its
      // search methods.
      if (!m_module_sp) {
        m_module_sp = target.GetOrCreateModule(module_spec, true /* notify */);
      }

      // For the kernel, we really do need an on-disk file copy of the binary
      // to do anything useful. This will force a call to dsymForUUID if it
      // exists, instead of depending on the DebugSymbols preferences being
      // set.
      Status kernel_search_error;
      if (IsKernel() &&
          (!m_module_sp || !m_module_sp->GetSymbolFileFileSpec())) {
        if (PluginManager::DownloadObjectAndSymbolFile(
                module_spec, kernel_search_error, true)) {
          if (FileSystem::Instance().Exists(module_spec.GetFileSpec())) {
            m_module_sp = std::make_shared<Module>(module_spec.GetFileSpec(),
                                                   target.GetArchitecture());
          }
        }
      }

      if (IsKernel() && !m_module_sp) {
        Stream &s = target.GetDebugger().GetErrorStream();
        s.Printf("WARNING: Unable to locate kernel binary on the debugger "
                 "system.\n");
        if (kernel_search_error.Fail() && kernel_search_error.AsCString("") &&
            kernel_search_error.AsCString("")[0] != '\0') {
          s << kernel_search_error.AsCString();
        }
      }
    }

    if (m_module_sp && m_uuid.IsValid() && m_module_sp->GetUUID() == m_uuid &&
        m_module_sp->GetObjectFile()) {
      if (ObjectFileMachO *ondisk_objfile_macho =
              llvm::dyn_cast<ObjectFileMachO>(m_module_sp->GetObjectFile())) {
        if (!IsKernel() && !ondisk_objfile_macho->IsKext()) {
          // We have a non-kext, non-kernel binary.  If we already have this
          // loaded in the Target with load addresses, don't re-load it again.
          ModuleSP existing_module_sp = target.GetImages().FindModule(m_uuid);
          if (existing_module_sp &&
              existing_module_sp->IsLoadedInTarget(&target)) {
            LLDB_LOGF(log,
                      "'%s' with UUID %s is not a kext or kernel, and is "
                      "already registered in target, not loading.",
                      m_name.c_str(), m_uuid.GetAsString().c_str());
            // It's already loaded, return true.
            return true;
          }
        }
      }
    }

    // If we managed to find a module, append it to the target's list of
    // images. If we also have a memory module, require that they have matching
    // UUIDs
    if (m_module_sp) {
      if (m_uuid.IsValid() && m_module_sp->GetUUID() == m_uuid) {
        target.GetImages().AppendIfNeeded(m_module_sp, false);
      }
    }
  }

  // If we've found a binary, read the load commands out of memory so we
  // can set the segment load addresses.
  if (m_module_sp)
    ReadMemoryModule (process);

  static ConstString g_section_name_LINKEDIT("__LINKEDIT");

  if (m_memory_module_sp && m_module_sp) {
    if (m_module_sp->GetUUID() == m_memory_module_sp->GetUUID()) {
      ObjectFile *ondisk_object_file = m_module_sp->GetObjectFile();
      ObjectFile *memory_object_file = m_memory_module_sp->GetObjectFile();

      if (memory_object_file && ondisk_object_file) {
        // The memory_module for kexts may have an invalid __LINKEDIT seg; skip
        // it.
        const bool ignore_linkedit = !IsKernel();

        // Normally a kext will have its segment load commands
        // (LC_SEGMENT vmaddrs) corrected in memory to have their
        // actual segment addresses.
        // Userland proceses have their libraries updated the same way
        // by dyld.  The Mach-O load commands in memory are the canonical
        // addresses.
        //
        // If the kernel gives us a binary where the in-memory segment
        // vmaddr is incorrect, then this binary was put in memory without
        // updating its Mach-O load commands.  We should assume a static
        // slide value will be applied to every segment; we don't have the
        // correct addresses for each individual segment.
        addr_t fixed_slide = LLDB_INVALID_ADDRESS;
        if (ObjectFileMachO *memory_objfile_macho =
                llvm::dyn_cast<ObjectFileMachO>(memory_object_file)) {
          if (Section *header_sect =
                  memory_objfile_macho->GetMachHeaderSection()) {
            if (header_sect->GetFileAddress() != m_load_address) {
              fixed_slide = m_load_address - header_sect->GetFileAddress();
              LLDB_LOGF(
                  log,
                  "kext %s in-memory LC_SEGMENT vmaddr is not correct, using a "
                  "fixed slide of 0x%" PRIx64,
                  m_name.c_str(), fixed_slide);
            }
          }
        }

        SectionList *ondisk_section_list = ondisk_object_file->GetSectionList();
        SectionList *memory_section_list = memory_object_file->GetSectionList();
        if (memory_section_list && ondisk_section_list) {
          const uint32_t num_ondisk_sections = ondisk_section_list->GetSize();
          // There may be CTF sections in the memory image so we can't always
          // just compare the number of sections (which are actually segments
          // in mach-o parlance)
          uint32_t sect_idx = 0;

          // Use the memory_module's addresses for each section to set the file
          // module's load address as appropriate.  We don't want to use a
          // single slide value for the entire kext - different segments may be
          // slid different amounts by the kext loader.

          uint32_t num_sections_loaded = 0;
          for (sect_idx = 0; sect_idx < num_ondisk_sections; ++sect_idx) {
            SectionSP ondisk_section_sp(
                ondisk_section_list->GetSectionAtIndex(sect_idx));
            if (ondisk_section_sp) {
              // Don't ever load __LINKEDIT as it may or may not be actually
              // mapped into memory and there is no current way to tell. Until
              // such an ability exists, do not load the __LINKEDIT.
              if (ignore_linkedit &&
                  ondisk_section_sp->GetName() == g_section_name_LINKEDIT)
                continue;

              if (fixed_slide != LLDB_INVALID_ADDRESS) {
                target.SetSectionLoadAddress(
                    ondisk_section_sp,
                    ondisk_section_sp->GetFileAddress() + fixed_slide);
              } else {
                const Section *memory_section =
                    memory_section_list
                        ->FindSectionByName(ondisk_section_sp->GetName())
                        .get();
                if (memory_section) {
                  target.SetSectionLoadAddress(
                      ondisk_section_sp, memory_section->GetFileAddress());
                  ++num_sections_loaded;
                }
              }
            }
          }
          if (num_sections_loaded > 0)
            m_load_process_stop_id = process->GetStopID();
          else
            m_module_sp.reset(); // No sections were loaded
        } else
          m_module_sp.reset(); // One or both section lists
      } else
        m_module_sp.reset(); // One or both object files missing
    } else
      m_module_sp.reset(); // UUID mismatch
  }

  bool is_loaded = IsLoaded();

  if (is_loaded && m_module_sp && IsKernel()) {
    Stream &s = target.GetDebugger().GetOutputStream();
    ObjectFile *kernel_object_file = m_module_sp->GetObjectFile();
    if (kernel_object_file) {
      addr_t file_address =
          kernel_object_file->GetBaseAddress().GetFileAddress();
      if (m_load_address != LLDB_INVALID_ADDRESS &&
          file_address != LLDB_INVALID_ADDRESS) {
        s.Printf("Kernel slid 0x%" PRIx64 " in memory.\n",
                 m_load_address - file_address);
      }
    }
    {
      s.Printf("Loaded kernel file %s\n",
               m_module_sp->GetFileSpec().GetPath().c_str());
    }
    s.Flush();
  }

  // Notify the target about the module being added;
  // set breakpoints, load dSYM scripts, etc. as needed.
  if (is_loaded && m_module_sp) {
    ModuleList loaded_module_list;
    loaded_module_list.Append(m_module_sp);
    target.ModulesDidLoad(loaded_module_list);
  }

  return is_loaded;
}

uint32_t DynamicLoaderDarwinKernel::KextImageInfo::GetAddressByteSize() {
  if (m_memory_module_sp)
    return m_memory_module_sp->GetArchitecture().GetAddressByteSize();
  if (m_module_sp)
    return m_module_sp->GetArchitecture().GetAddressByteSize();
  return 0;
}

lldb::ByteOrder DynamicLoaderDarwinKernel::KextImageInfo::GetByteOrder() {
  if (m_memory_module_sp)
    return m_memory_module_sp->GetArchitecture().GetByteOrder();
  if (m_module_sp)
    return m_module_sp->GetArchitecture().GetByteOrder();
  return endian::InlHostByteOrder();
}

lldb_private::ArchSpec
DynamicLoaderDarwinKernel::KextImageInfo::GetArchitecture() const {
  if (m_memory_module_sp)
    return m_memory_module_sp->GetArchitecture();
  if (m_module_sp)
    return m_module_sp->GetArchitecture();
  return lldb_private::ArchSpec();
}

// Load the kernel module and initialize the "m_kernel" member. Return true
// _only_ if the kernel is loaded the first time through (subsequent calls to
// this function should return false after the kernel has been already loaded).
void DynamicLoaderDarwinKernel::LoadKernelModuleIfNeeded() {
  if (!m_kext_summary_header_ptr_addr.IsValid()) {
    m_kernel.Clear();
    ModuleSP module_sp = m_process->GetTarget().GetExecutableModule();
    if (is_kernel(module_sp.get())) {
      m_kernel.SetModule(module_sp);
      m_kernel.SetIsKernel(true);
    }

    ConstString kernel_name("mach_kernel");
    if (m_kernel.GetModule().get() && m_kernel.GetModule()->GetObjectFile() &&
        !m_kernel.GetModule()
             ->GetObjectFile()
             ->GetFileSpec()
             .GetFilename()
             .IsEmpty()) {
      kernel_name =
          m_kernel.GetModule()->GetObjectFile()->GetFileSpec().GetFilename();
    }
    m_kernel.SetName(kernel_name.AsCString());

    if (m_kernel.GetLoadAddress() == LLDB_INVALID_ADDRESS) {
      m_kernel.SetLoadAddress(m_kernel_load_address);
      if (m_kernel.GetLoadAddress() == LLDB_INVALID_ADDRESS &&
          m_kernel.GetModule()) {
        // We didn't get a hint from the process, so we will try the kernel at
        // the address that it exists at in the file if we have one
        ObjectFile *kernel_object_file = m_kernel.GetModule()->GetObjectFile();
        if (kernel_object_file) {
          addr_t load_address =
              kernel_object_file->GetBaseAddress().GetLoadAddress(
                  &m_process->GetTarget());
          addr_t file_address =
              kernel_object_file->GetBaseAddress().GetFileAddress();
          if (load_address != LLDB_INVALID_ADDRESS && load_address != 0) {
            m_kernel.SetLoadAddress(load_address);
            if (load_address != file_address) {
              // Don't accidentally relocate the kernel to the File address --
              // the Load address has already been set to its actual in-memory
              // address. Mark it as IsLoaded.
              m_kernel.SetProcessStopId(m_process->GetStopID());
            }
          } else {
            m_kernel.SetLoadAddress(file_address);
          }
        }
      }
    }
    if (m_kernel.GetLoadAddress() != LLDB_INVALID_ADDRESS)
      if (!m_kernel.LoadImageUsingMemoryModule(m_process))
        m_kernel.LoadImageAtFileAddress(m_process);

    // The operating system plugin gets loaded and initialized in
    // LoadImageUsingMemoryModule when we discover the kernel dSYM.  For a core
    // file in particular, that's the wrong place to do this, since  we haven't
    // fixed up the section addresses yet.  So let's redo it here.
    LoadOperatingSystemPlugin(false);

    if (m_kernel.IsLoaded() && m_kernel.GetModule()) {
      static ConstString kext_summary_symbol("gLoadedKextSummaries");
      static ConstString arm64_T1Sz_value("gT1Sz");
      const Symbol *symbol =
          m_kernel.GetModule()->FindFirstSymbolWithNameAndType(
              kext_summary_symbol, eSymbolTypeData);
      if (symbol) {
        m_kext_summary_header_ptr_addr = symbol->GetAddress();
        // Update all image infos
        ReadAllKextSummaries();
      }
      // If the kernel global with the T1Sz setting is available,
      // update the target.process.virtual-addressable-bits to be correct.
      // NB the xnu kernel always has T0Sz and T1Sz the same value.  If
      // it wasn't the same, we would need to set
      // target.process.virtual-addressable-bits = T0Sz
      // target.process.highmem-virtual-addressable-bits = T1Sz
      symbol = m_kernel.GetModule()->FindFirstSymbolWithNameAndType(
          arm64_T1Sz_value, eSymbolTypeData);
      if (symbol) {
        const addr_t orig_code_mask = m_process->GetCodeAddressMask();
        const addr_t orig_data_mask = m_process->GetDataAddressMask();

        m_process->SetCodeAddressMask(0);
        m_process->SetDataAddressMask(0);
        Status error;
        // gT1Sz is 8 bytes.  We may run on a stripped kernel binary
        // where we can't get the size accurately.  Hardcode it.
        const size_t sym_bytesize = 8; // size of gT1Sz value
        uint64_t sym_value =
            m_process->GetTarget().ReadUnsignedIntegerFromMemory(
                symbol->GetAddress(), sym_bytesize, 0, error);
        if (error.Success()) {
          // 64 - T1Sz is the highest bit used for auth.
          // The value we pass in to SetVirtualAddressableBits is
          // the number of bits used for addressing, so if
          // T1Sz is 25, then 64-25 == 39, bits 0..38 are used for
          // addressing, bits 39..63 are used for PAC/TBI or whatever.
          uint32_t virt_addr_bits = 64 - sym_value;
          addr_t mask = AddressableBits::AddressableBitToMask(virt_addr_bits);
          m_process->SetCodeAddressMask(mask);
          m_process->SetDataAddressMask(mask);
        } else {
          m_process->SetCodeAddressMask(orig_code_mask);
          m_process->SetDataAddressMask(orig_data_mask);
        }
      }
    } else {
      m_kernel.Clear();
    }
  }
}

// Static callback function that gets called when our DYLD notification
// breakpoint gets hit. We update all of our image infos and then let our super
// class DynamicLoader class decide if we should stop or not (based on global
// preference).
bool DynamicLoaderDarwinKernel::BreakpointHitCallback(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  return static_cast<DynamicLoaderDarwinKernel *>(baton)->BreakpointHit(
      context, break_id, break_loc_id);
}

bool DynamicLoaderDarwinKernel::BreakpointHit(StoppointCallbackContext *context,
                                              user_id_t break_id,
                                              user_id_t break_loc_id) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderDarwinKernel::BreakpointHit (...)\n");

  ReadAllKextSummaries();

  if (log)
    PutToLog(log);

  return GetStopWhenImagesChange();
}

bool DynamicLoaderDarwinKernel::ReadKextSummaryHeader() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // the all image infos is already valid for this process stop ID

  if (m_kext_summary_header_ptr_addr.IsValid()) {
    const uint32_t addr_size = m_kernel.GetAddressByteSize();
    const ByteOrder byte_order = m_kernel.GetByteOrder();
    Status error;
    // Read enough bytes for a "OSKextLoadedKextSummaryHeader" structure which
    // is currently 4 uint32_t and a pointer.
    uint8_t buf[24];
    DataExtractor data(buf, sizeof(buf), byte_order, addr_size);
    const size_t count = 4 * sizeof(uint32_t) + addr_size;
    const bool force_live_memory = true;
    if (m_process->GetTarget().ReadPointerFromMemory(
            m_kext_summary_header_ptr_addr, error,
            m_kext_summary_header_addr, force_live_memory)) {
      // We got a valid address for our kext summary header and make sure it
      // isn't NULL
      if (m_kext_summary_header_addr.IsValid() &&
          m_kext_summary_header_addr.GetFileAddress() != 0) {
        const size_t bytes_read = m_process->GetTarget().ReadMemory(
            m_kext_summary_header_addr, buf, count, error, force_live_memory);
        if (bytes_read == count) {
          lldb::offset_t offset = 0;
          m_kext_summary_header.version = data.GetU32(&offset);
          if (m_kext_summary_header.version > 128) {
            Stream &s = m_process->GetTarget().GetDebugger().GetOutputStream();
            s.Printf("WARNING: Unable to read kext summary header, got "
                     "improbable version number %u\n",
                     m_kext_summary_header.version);
            // If we get an improbably large version number, we're probably
            // getting bad memory.
            m_kext_summary_header_addr.Clear();
            return false;
          }
          if (m_kext_summary_header.version >= 2) {
            m_kext_summary_header.entry_size = data.GetU32(&offset);
            if (m_kext_summary_header.entry_size > 4096) {
              // If we get an improbably large entry_size, we're probably
              // getting bad memory.
              Stream &s =
                  m_process->GetTarget().GetDebugger().GetOutputStream();
              s.Printf("WARNING: Unable to read kext summary header, got "
                       "improbable entry_size %u\n",
                       m_kext_summary_header.entry_size);
              m_kext_summary_header_addr.Clear();
              return false;
            }
          } else {
            // Versions less than 2 didn't have an entry size, it was hard
            // coded
            m_kext_summary_header.entry_size =
                KERNEL_MODULE_ENTRY_SIZE_VERSION_1;
          }
          m_kext_summary_header.entry_count = data.GetU32(&offset);
          if (m_kext_summary_header.entry_count > 10000) {
            // If we get an improbably large number of kexts, we're probably
            // getting bad memory.
            Stream &s = m_process->GetTarget().GetDebugger().GetOutputStream();
            s.Printf("WARNING: Unable to read kext summary header, got "
                     "improbable number of kexts %u\n",
                     m_kext_summary_header.entry_count);
            m_kext_summary_header_addr.Clear();
            return false;
          }
          return true;
        }
      }
    }
  }
  m_kext_summary_header_addr.Clear();
  return false;
}

// We've either (a) just attached to a new kernel, or (b) the kexts-changed
// breakpoint was hit and we need to figure out what kexts have been added or
// removed. Read the kext summaries from the inferior kernel memory, compare
// them against the m_known_kexts vector and update the m_known_kexts vector as
// needed to keep in sync with the inferior.

bool DynamicLoaderDarwinKernel::ParseKextSummaries(
    const Address &kext_summary_addr, uint32_t count) {
  KextImageInfo::collection kext_summaries;
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log,
            "Kexts-changed breakpoint hit, there are %d kexts currently.\n",
            count);

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (!ReadKextSummaries(kext_summary_addr, count, kext_summaries))
    return false;

  // read the plugin.dynamic-loader.darwin-kernel.load-kexts setting -- if the
  // user requested no kext loading, don't print any messages about kexts &
  // don't try to read them.
  const bool load_kexts = GetGlobalProperties().GetLoadKexts();

  // By default, all kexts we've loaded in the past are marked as "remove" and
  // all of the kexts we just found out about from ReadKextSummaries are marked
  // as "add".
  std::vector<bool> to_be_removed(m_known_kexts.size(), true);
  std::vector<bool> to_be_added(count, true);

  int number_of_new_kexts_being_added = 0;
  int number_of_old_kexts_being_removed = m_known_kexts.size();

  const uint32_t new_kexts_size = kext_summaries.size();
  const uint32_t old_kexts_size = m_known_kexts.size();

  // The m_known_kexts vector may have entries that have been Cleared, or are a
  // kernel.
  for (uint32_t old_kext = 0; old_kext < old_kexts_size; old_kext++) {
    bool ignore = false;
    KextImageInfo &image_info = m_known_kexts[old_kext];
    if (image_info.IsKernel()) {
      ignore = true;
    } else if (image_info.GetLoadAddress() == LLDB_INVALID_ADDRESS &&
               !image_info.GetModule()) {
      ignore = true;
    }

    if (ignore) {
      number_of_old_kexts_being_removed--;
      to_be_removed[old_kext] = false;
    }
  }

  // Scan over the list of kexts we just read from the kernel, note those that
  // need to be added and those already loaded.
  for (uint32_t new_kext = 0; new_kext < new_kexts_size; new_kext++) {
    bool add_this_one = true;
    for (uint32_t old_kext = 0; old_kext < old_kexts_size; old_kext++) {
      if (m_known_kexts[old_kext] == kext_summaries[new_kext]) {
        // We already have this kext, don't re-load it.
        to_be_added[new_kext] = false;
        // This kext is still present, do not remove it.
        to_be_removed[old_kext] = false;

        number_of_old_kexts_being_removed--;
        add_this_one = false;
        break;
      }
    }
    // If this "kext" entry is actually an alias for the kernel -- the kext was
    // compiled into the kernel or something -- then we don't want to load the
    // kernel's text section at a different address.  Ignore this kext entry.
    if (kext_summaries[new_kext].GetUUID().IsValid() &&
        m_kernel.GetUUID().IsValid() &&
        kext_summaries[new_kext].GetUUID() == m_kernel.GetUUID()) {
      to_be_added[new_kext] = false;
      break;
    }
    if (add_this_one) {
      number_of_new_kexts_being_added++;
    }
  }

  if (number_of_new_kexts_being_added == 0 &&
      number_of_old_kexts_being_removed == 0)
    return true;

  Stream &s = m_process->GetTarget().GetDebugger().GetOutputStream();
  if (load_kexts) {
    if (number_of_new_kexts_being_added > 0 &&
        number_of_old_kexts_being_removed > 0) {
      s.Printf("Loading %d kext modules and unloading %d kext modules ",
               number_of_new_kexts_being_added,
               number_of_old_kexts_being_removed);
    } else if (number_of_new_kexts_being_added > 0) {
      s.Printf("Loading %d kext modules ", number_of_new_kexts_being_added);
    } else if (number_of_old_kexts_being_removed > 0) {
      s.Printf("Unloading %d kext modules ", number_of_old_kexts_being_removed);
    }
  }

  if (log) {
    if (load_kexts) {
      LLDB_LOGF(log,
                "DynamicLoaderDarwinKernel::ParseKextSummaries: %d kexts "
                "added, %d kexts removed",
                number_of_new_kexts_being_added,
                number_of_old_kexts_being_removed);
    } else {
      LLDB_LOGF(log,
                "DynamicLoaderDarwinKernel::ParseKextSummaries kext loading is "
                "disabled, else would have %d kexts added, %d kexts removed",
                number_of_new_kexts_being_added,
                number_of_old_kexts_being_removed);
    }
  }

  // Build up a list of <kext-name, uuid> for any kexts that fail to load
  std::vector<std::pair<std::string, UUID>> kexts_failed_to_load;
  if (number_of_new_kexts_being_added > 0) {
    ModuleList loaded_module_list;
    Progress progress("Loading kext", "", number_of_new_kexts_being_added);

    const uint32_t num_of_new_kexts = kext_summaries.size();
    for (uint32_t new_kext = 0; new_kext < num_of_new_kexts; new_kext++) {
      if (to_be_added[new_kext]) {
        KextImageInfo &image_info = kext_summaries[new_kext];
        if (load_kexts) {
          if (!image_info.LoadImageUsingMemoryModule(m_process, &progress)) {
            kexts_failed_to_load.push_back(std::pair<std::string, UUID>(
                kext_summaries[new_kext].GetName(),
                kext_summaries[new_kext].GetUUID()));
            image_info.LoadImageAtFileAddress(m_process);
          }
        }

        m_known_kexts.push_back(image_info);

        if (image_info.GetModule() &&
            m_process->GetStopID() == image_info.GetProcessStopId())
          loaded_module_list.AppendIfNeeded(image_info.GetModule());

        if (log)
          kext_summaries[new_kext].PutToLog(log);
      }
    }
    m_process->GetTarget().ModulesDidLoad(loaded_module_list);
  }

  if (number_of_old_kexts_being_removed > 0) {
    ModuleList loaded_module_list;
    const uint32_t num_of_old_kexts = m_known_kexts.size();
    for (uint32_t old_kext = 0; old_kext < num_of_old_kexts; old_kext++) {
      ModuleList unloaded_module_list;
      if (to_be_removed[old_kext]) {
        KextImageInfo &image_info = m_known_kexts[old_kext];
        // You can't unload the kernel.
        if (!image_info.IsKernel()) {
          if (image_info.GetModule()) {
            unloaded_module_list.AppendIfNeeded(image_info.GetModule());
          }
          s.Printf(".");
          image_info.Clear();
          // should pull it out of the KextImageInfos vector but that would
          // mutate the list and invalidate the to_be_removed bool vector;
          // leaving it in place once Cleared() is relatively harmless.
        }
      }
      m_process->GetTarget().ModulesDidUnload(unloaded_module_list, false);
    }
  }

  if (load_kexts) {
    s.Printf(" done.\n");
    if (kexts_failed_to_load.size() > 0 && number_of_new_kexts_being_added > 0) {
      s.Printf("Failed to load %d of %d kexts:\n",
               (int)kexts_failed_to_load.size(),
               number_of_new_kexts_being_added);
      // print a sorted list of <kext-name, uuid> kexts which failed to load
      unsigned longest_name = 0;
      std::sort(kexts_failed_to_load.begin(), kexts_failed_to_load.end());
      for (const auto &ku : kexts_failed_to_load) {
        if (ku.first.size() > longest_name)
          longest_name = ku.first.size();
      }
      for (const auto &ku : kexts_failed_to_load) {
        std::string uuid;
        if (ku.second.IsValid())
          uuid = ku.second.GetAsString();
        s.Printf(" %-*s %s\n", longest_name, ku.first.c_str(), uuid.c_str());
      }
    }
    s.Flush();
  }

  return true;
}

uint32_t DynamicLoaderDarwinKernel::ReadKextSummaries(
    const Address &kext_summary_addr, uint32_t image_infos_count,
    KextImageInfo::collection &image_infos) {
  const ByteOrder endian = m_kernel.GetByteOrder();
  const uint32_t addr_size = m_kernel.GetAddressByteSize();

  image_infos.resize(image_infos_count);
  const size_t count = image_infos.size() * m_kext_summary_header.entry_size;
  DataBufferHeap data(count, 0);
  Status error;

  const bool force_live_memory = true;
  const size_t bytes_read = m_process->GetTarget().ReadMemory(
      kext_summary_addr, data.GetBytes(), data.GetByteSize(), error, force_live_memory);
  if (bytes_read == count) {

    DataExtractor extractor(data.GetBytes(), data.GetByteSize(), endian,
                            addr_size);
    uint32_t i = 0;
    for (uint32_t kext_summary_offset = 0;
         i < image_infos.size() &&
         extractor.ValidOffsetForDataOfSize(kext_summary_offset,
                                            m_kext_summary_header.entry_size);
         ++i, kext_summary_offset += m_kext_summary_header.entry_size) {
      lldb::offset_t offset = kext_summary_offset;
      const void *name_data =
          extractor.GetData(&offset, KERNEL_MODULE_MAX_NAME);
      if (name_data == nullptr)
        break;
      image_infos[i].SetName((const char *)name_data);
      UUID uuid(extractor.GetData(&offset, 16), 16);
      image_infos[i].SetUUID(uuid);
      image_infos[i].SetLoadAddress(extractor.GetU64(&offset));
      image_infos[i].SetSize(extractor.GetU64(&offset));
    }
    if (i < image_infos.size())
      image_infos.resize(i);
  } else {
    image_infos.clear();
  }
  return image_infos.size();
}

bool DynamicLoaderDarwinKernel::ReadAllKextSummaries() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (ReadKextSummaryHeader()) {
    if (m_kext_summary_header.entry_count > 0 &&
        m_kext_summary_header_addr.IsValid()) {
      Address summary_addr(m_kext_summary_header_addr);
      summary_addr.Slide(m_kext_summary_header.GetSize());
      if (!ParseKextSummaries(summary_addr,
                              m_kext_summary_header.entry_count)) {
        m_known_kexts.clear();
      }
      return true;
    }
  }
  return false;
}

// Dump an image info structure to the file handle provided.
void DynamicLoaderDarwinKernel::KextImageInfo::PutToLog(Log *log) const {
  if (m_load_address == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(log, "uuid={0} name=\"{1}\" (UNLOADED)", m_uuid.GetAsString(),
             m_name);
  } else {
    LLDB_LOG(log, "addr={0:x+16} size={1:x+16} uuid={2} name=\"{3}\"",
        m_load_address, m_size, m_uuid.GetAsString(), m_name);
  }
}

// Dump the _dyld_all_image_infos members and all current image infos that we
// have parsed to the file handle provided.
void DynamicLoaderDarwinKernel::PutToLog(Log *log) const {
  if (log == nullptr)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  LLDB_LOGF(log,
            "gLoadedKextSummaries = 0x%16.16" PRIx64
            " { version=%u, entry_size=%u, entry_count=%u }",
            m_kext_summary_header_addr.GetFileAddress(),
            m_kext_summary_header.version, m_kext_summary_header.entry_size,
            m_kext_summary_header.entry_count);

  size_t i;
  const size_t count = m_known_kexts.size();
  if (count > 0) {
    log->PutCString("Loaded:");
    for (i = 0; i < count; i++)
      m_known_kexts[i].PutToLog(log);
  }
}

void DynamicLoaderDarwinKernel::PrivateInitialize(Process *process) {
  DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s() process state = %s\n",
               __FUNCTION__, StateAsCString(m_process->GetState()));
  Clear(true);
  m_process = process;
}

void DynamicLoaderDarwinKernel::SetNotificationBreakpointIfNeeded() {
  if (m_break_id == LLDB_INVALID_BREAK_ID && m_kernel.GetModule()) {
    DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s() process state = %s\n",
                 __FUNCTION__, StateAsCString(m_process->GetState()));

    const bool internal_bp = true;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolNo;
    FileSpecList module_spec_list;
    module_spec_list.Append(m_kernel.GetModule()->GetFileSpec());
    Breakpoint *bp =
        m_process->GetTarget()
            .CreateBreakpoint(&module_spec_list, nullptr,
                              "OSKextLoadedKextSummariesUpdated",
                              eFunctionNameTypeFull, eLanguageTypeUnknown, 0,
                              skip_prologue, internal_bp, hardware)
            .get();

    bp->SetCallback(DynamicLoaderDarwinKernel::BreakpointHitCallback, this,
                    true);
    m_break_id = bp->GetID();
  }
}

// Member function that gets called when the process state changes.
void DynamicLoaderDarwinKernel::PrivateProcessStateChanged(Process *process,
                                                           StateType state) {
  DEBUG_PRINTF("DynamicLoaderDarwinKernel::%s(%s)\n", __FUNCTION__,
               StateAsCString(state));
  switch (state) {
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateInvalid:
  case eStateUnloaded:
  case eStateExited:
  case eStateDetached:
    Clear(false);
    break;

  case eStateStopped:
    UpdateIfNeeded();
    break;

  case eStateRunning:
  case eStateStepping:
  case eStateCrashed:
  case eStateSuspended:
    break;
  }
}

ThreadPlanSP
DynamicLoaderDarwinKernel::GetStepThroughTrampolinePlan(Thread &thread,
                                                        bool stop_others) {
  ThreadPlanSP thread_plan_sp;
  Log *log = GetLog(LLDBLog::Step);
  LLDB_LOGF(log, "Could not find symbol for step through.");
  return thread_plan_sp;
}

Status DynamicLoaderDarwinKernel::CanLoadImage() {
  Status error;
  error.SetErrorString(
      "always unsafe to load or unload shared libraries in the darwin kernel");
  return error;
}

void DynamicLoaderDarwinKernel::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInitialize);
}

void DynamicLoaderDarwinKernel::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

void DynamicLoaderDarwinKernel::DebuggerInitialize(
    lldb_private::Debugger &debugger) {
  if (!PluginManager::GetSettingForDynamicLoaderPlugin(
          debugger, DynamicLoaderDarwinKernelProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForDynamicLoaderPlugin(
        debugger, GetGlobalProperties().GetValueProperties(),
        "Properties for the DynamicLoaderDarwinKernel plug-in.",
        is_global_setting);
  }
}

llvm::StringRef DynamicLoaderDarwinKernel::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library loads/unloads "
         "in the MacOSX kernel.";
}

lldb::ByteOrder
DynamicLoaderDarwinKernel::GetByteOrderFromMagic(uint32_t magic) {
  switch (magic) {
  case llvm::MachO::MH_MAGIC:
  case llvm::MachO::MH_MAGIC_64:
    return endian::InlHostByteOrder();

  case llvm::MachO::MH_CIGAM:
  case llvm::MachO::MH_CIGAM_64:
    if (endian::InlHostByteOrder() == lldb::eByteOrderBig)
      return lldb::eByteOrderLittle;
    else
      return lldb::eByteOrderBig;

  default:
    break;
  }
  return lldb::eByteOrderInvalid;
}
