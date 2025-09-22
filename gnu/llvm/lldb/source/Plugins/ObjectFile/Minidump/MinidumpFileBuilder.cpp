//===-- MinidumpFileBuilder.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MinidumpFileBuilder.h"

#include "Plugins/Process/minidump/RegisterContextMinidump_ARM64.h"
#include "Plugins/Process/minidump/RegisterContextMinidump_x86_64.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/RegisterValue.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "Plugins/Process/minidump/MinidumpTypes.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <algorithm>
#include <cinttypes>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace llvm::minidump;

Status MinidumpFileBuilder::AddHeaderAndCalculateDirectories() {
  // First set the offset on the file, and on the bytes saved
  m_saved_data_size = HEADER_SIZE;
  // We know we will have at least Misc, SystemInfo, Modules, and ThreadList
  // (corresponding memory list for stacks) And an additional memory list for
  // non-stacks.
  lldb_private::Target &target = m_process_sp->GetTarget();
  m_expected_directories = 6;
  // Check if OS is linux and reserve directory space for all linux specific
  // breakpad extension directories.
  if (target.GetArchitecture().GetTriple().getOS() ==
      llvm::Triple::OSType::Linux)
    m_expected_directories += 9;

  // Go through all of the threads and check for exceptions.
  lldb_private::ThreadList thread_list = m_process_sp->GetThreadList();
  const uint32_t num_threads = thread_list.GetSize();
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    ThreadSP thread_sp(thread_list.GetThreadAtIndex(thread_idx));
    StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
    if (stop_info_sp) {
      const StopReason &stop_reason = stop_info_sp->GetStopReason();
      if (stop_reason == StopReason::eStopReasonException ||
          stop_reason == StopReason::eStopReasonSignal)
        m_expected_directories++;
    }
  }

  m_saved_data_size +=
      m_expected_directories * sizeof(llvm::minidump::Directory);
  Status error;
  offset_t new_offset = m_core_file->SeekFromStart(m_saved_data_size);
  if (new_offset != m_saved_data_size)
    error.SetErrorStringWithFormat("Failed to fill in header and directory "
                                   "sections. Written / Expected (%" PRIx64
                                   " / %" PRIx64 ")",
                                   new_offset, m_saved_data_size);

  return error;
}

Status MinidumpFileBuilder::AddDirectory(StreamType type,
                                         uint64_t stream_size) {
  // We explicitly cast type, an 32b enum, to uint32_t to avoid warnings.
  Status error;
  if (GetCurrentDataEndOffset() > UINT32_MAX) {
    error.SetErrorStringWithFormat("Unable to add directory for stream type "
                                   "%x, offset is greater then 32 bit limit.",
                                   (uint32_t)type);
    return error;
  }

  if (m_directories.size() + 1 > m_expected_directories) {
    error.SetErrorStringWithFormat(
        "Unable to add directory for stream type %x, exceeded expected number "
        "of directories %zu.",
        (uint32_t)type, m_expected_directories);
    return error;
  }

  LocationDescriptor loc;
  loc.DataSize = static_cast<llvm::support::ulittle32_t>(stream_size);
  // Stream will begin at the current end of data section
  loc.RVA = static_cast<llvm::support::ulittle32_t>(GetCurrentDataEndOffset());

  Directory dir;
  dir.Type = static_cast<llvm::support::little_t<StreamType>>(type);
  dir.Location = loc;

  m_directories.push_back(dir);
  return error;
}

Status MinidumpFileBuilder::AddSystemInfo() {
  Status error;
  const llvm::Triple &target_triple =
      m_process_sp->GetTarget().GetArchitecture().GetTriple();
  error =
      AddDirectory(StreamType::SystemInfo, sizeof(llvm::minidump::SystemInfo));
  if (error.Fail())
    return error;

  llvm::minidump::ProcessorArchitecture arch;
  switch (target_triple.getArch()) {
  case llvm::Triple::ArchType::x86_64:
    arch = ProcessorArchitecture::AMD64;
    break;
  case llvm::Triple::ArchType::x86:
    arch = ProcessorArchitecture::X86;
    break;
  case llvm::Triple::ArchType::arm:
    arch = ProcessorArchitecture::ARM;
    break;
  case llvm::Triple::ArchType::aarch64:
    arch = ProcessorArchitecture::ARM64;
    break;
  case llvm::Triple::ArchType::mips64:
  case llvm::Triple::ArchType::mips64el:
  case llvm::Triple::ArchType::mips:
  case llvm::Triple::ArchType::mipsel:
    arch = ProcessorArchitecture::MIPS;
    break;
  case llvm::Triple::ArchType::ppc64:
  case llvm::Triple::ArchType::ppc:
  case llvm::Triple::ArchType::ppc64le:
    arch = ProcessorArchitecture::PPC;
    break;
  default:
    error.SetErrorStringWithFormat("Architecture %s not supported.",
                                   target_triple.getArchName().str().c_str());
    return error;
  };

  llvm::support::little_t<OSPlatform> platform_id;
  switch (target_triple.getOS()) {
  case llvm::Triple::OSType::Linux:
    if (target_triple.getEnvironment() ==
        llvm::Triple::EnvironmentType::Android)
      platform_id = OSPlatform::Android;
    else
      platform_id = OSPlatform::Linux;
    break;
  case llvm::Triple::OSType::Win32:
    platform_id = OSPlatform::Win32NT;
    break;
  case llvm::Triple::OSType::MacOSX:
    platform_id = OSPlatform::MacOSX;
    break;
  case llvm::Triple::OSType::IOS:
    platform_id = OSPlatform::IOS;
    break;
  default:
    error.SetErrorStringWithFormat("OS %s not supported.",
                                   target_triple.getOSName().str().c_str());
    return error;
  };

  llvm::minidump::SystemInfo sys_info;
  sys_info.ProcessorArch =
      static_cast<llvm::support::little_t<ProcessorArchitecture>>(arch);
  // Global offset to beginning of a csd_string in a data section
  sys_info.CSDVersionRVA = static_cast<llvm::support::ulittle32_t>(
      GetCurrentDataEndOffset() + sizeof(llvm::minidump::SystemInfo));
  sys_info.PlatformId = platform_id;
  m_data.AppendData(&sys_info, sizeof(llvm::minidump::SystemInfo));

  std::string csd_string;

  error = WriteString(csd_string, &m_data);
  if (error.Fail()) {
    error.SetErrorString("Unable to convert the csd string to UTF16.");
    return error;
  }

  return error;
}

Status WriteString(const std::string &to_write,
                   lldb_private::DataBufferHeap *buffer) {
  Status error;
  // let the StringRef eat also null termination char
  llvm::StringRef to_write_ref(to_write.c_str(), to_write.size() + 1);
  llvm::SmallVector<llvm::UTF16, 128> to_write_utf16;

  bool converted = convertUTF8ToUTF16String(to_write_ref, to_write_utf16);
  if (!converted) {
    error.SetErrorStringWithFormat(
        "Unable to convert the string to UTF16. Failed to convert %s",
        to_write.c_str());
    return error;
  }

  // size of the UTF16 string should be written without the null termination
  // character that is stored in 2 bytes
  llvm::support::ulittle32_t to_write_size(to_write_utf16.size_in_bytes() - 2);

  buffer->AppendData(&to_write_size, sizeof(llvm::support::ulittle32_t));
  buffer->AppendData(to_write_utf16.data(), to_write_utf16.size_in_bytes());

  return error;
}

llvm::Expected<uint64_t> getModuleFileSize(Target &target,
                                           const ModuleSP &mod) {
  // JIT module has the same vm and file size.
  uint64_t SizeOfImage = 0;
  if (mod->GetObjectFile()->CalculateType() == ObjectFile::Type::eTypeJIT) {
    for (const auto &section : *mod->GetObjectFile()->GetSectionList()) {
      SizeOfImage += section->GetByteSize();
    }
    return SizeOfImage;
  }
  SectionSP sect_sp = mod->GetObjectFile()->GetBaseAddress().GetSection();

  if (!sect_sp) {
    return llvm::createStringError(std::errc::operation_not_supported,
                                   "Couldn't obtain the section information.");
  }
  lldb::addr_t sect_addr = sect_sp->GetLoadBaseAddress(&target);
  // Use memory size since zero fill sections, like ".bss", will be smaller on
  // disk.
  lldb::addr_t sect_size = sect_sp->GetByteSize();
  // This will usually be zero, but make sure to calculate the BaseOfImage
  // offset.
  const lldb::addr_t base_sect_offset =
      mod->GetObjectFile()->GetBaseAddress().GetLoadAddress(&target) -
      sect_addr;
  SizeOfImage = sect_size - base_sect_offset;
  lldb::addr_t next_sect_addr = sect_addr + sect_size;
  Address sect_so_addr;
  target.ResolveLoadAddress(next_sect_addr, sect_so_addr);
  lldb::SectionSP next_sect_sp = sect_so_addr.GetSection();
  while (next_sect_sp &&
         next_sect_sp->GetLoadBaseAddress(&target) == next_sect_addr) {
    sect_size = sect_sp->GetByteSize();
    SizeOfImage += sect_size;
    next_sect_addr += sect_size;
    target.ResolveLoadAddress(next_sect_addr, sect_so_addr);
    next_sect_sp = sect_so_addr.GetSection();
  }

  return SizeOfImage;
}

// ModuleList stream consists of a number of modules, followed by an array
// of llvm::minidump::Module's structures. Every structure informs about a
// single module. Additional data of variable length, such as module's names,
// are stored just after the ModuleList stream. The llvm::minidump::Module
// structures point to this helper data by global offset.
Status MinidumpFileBuilder::AddModuleList() {
  constexpr size_t minidump_module_size = sizeof(llvm::minidump::Module);
  Status error;

  lldb_private::Target &target = m_process_sp->GetTarget();
  const ModuleList &modules = target.GetImages();
  llvm::support::ulittle32_t modules_count =
      static_cast<llvm::support::ulittle32_t>(modules.GetSize());

  // This helps us with getting the correct global offset in minidump
  // file later, when we will be setting up offsets from the
  // the llvm::minidump::Module's structures into helper data
  size_t size_before = GetCurrentDataEndOffset();

  // This is the size of the main part of the ModuleList stream.
  // It consists of a module number and corresponding number of
  // structs describing individual modules
  size_t module_stream_size =
      sizeof(llvm::support::ulittle32_t) + modules_count * minidump_module_size;

  // Adding directory describing this stream.
  error = AddDirectory(StreamType::ModuleList, module_stream_size);
  if (error.Fail())
    return error;

  m_data.AppendData(&modules_count, sizeof(llvm::support::ulittle32_t));

  // Temporary storage for the helper data (of variable length)
  // as these cannot be dumped to m_data before dumping entire
  // array of module structures.
  DataBufferHeap helper_data;

  for (size_t i = 0; i < modules_count; ++i) {
    ModuleSP mod = modules.GetModuleAtIndex(i);
    std::string module_name = mod->GetSpecificationDescription();
    auto maybe_mod_size = getModuleFileSize(target, mod);
    if (!maybe_mod_size) {
      llvm::Error mod_size_err = maybe_mod_size.takeError();
      llvm::handleAllErrors(std::move(mod_size_err),
                            [&](const llvm::ErrorInfoBase &E) {
                              error.SetErrorStringWithFormat(
                                  "Unable to get the size of module %s: %s.",
                                  module_name.c_str(), E.message().c_str());
                            });
      return error;
    }

    uint64_t mod_size = std::move(*maybe_mod_size);

    llvm::support::ulittle32_t signature =
        static_cast<llvm::support::ulittle32_t>(
            static_cast<uint32_t>(minidump::CvSignature::ElfBuildId));
    auto uuid = mod->GetUUID().GetBytes();

    VSFixedFileInfo info;
    info.Signature = static_cast<llvm::support::ulittle32_t>(0u);
    info.StructVersion = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileVersionHigh = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileVersionLow = static_cast<llvm::support::ulittle32_t>(0u);
    info.ProductVersionHigh = static_cast<llvm::support::ulittle32_t>(0u);
    info.ProductVersionLow = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileFlagsMask = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileFlags = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileOS = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileType = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileSubtype = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileDateHigh = static_cast<llvm::support::ulittle32_t>(0u);
    info.FileDateLow = static_cast<llvm::support::ulittle32_t>(0u);

    LocationDescriptor ld;
    ld.DataSize = static_cast<llvm::support::ulittle32_t>(0u);
    ld.RVA = static_cast<llvm::support::ulittle32_t>(0u);

    // Setting up LocationDescriptor for uuid string. The global offset into
    // minidump file is calculated.
    LocationDescriptor ld_cv;
    ld_cv.DataSize = static_cast<llvm::support::ulittle32_t>(
        sizeof(llvm::support::ulittle32_t) + uuid.size());
    ld_cv.RVA = static_cast<llvm::support::ulittle32_t>(
        size_before + module_stream_size + helper_data.GetByteSize());

    helper_data.AppendData(&signature, sizeof(llvm::support::ulittle32_t));
    helper_data.AppendData(uuid.begin(), uuid.size());

    llvm::minidump::Module m;
    m.BaseOfImage = static_cast<llvm::support::ulittle64_t>(
        mod->GetObjectFile()->GetBaseAddress().GetLoadAddress(&target));
    m.SizeOfImage = static_cast<llvm::support::ulittle32_t>(mod_size);
    m.Checksum = static_cast<llvm::support::ulittle32_t>(0);
    m.TimeDateStamp =
        static_cast<llvm::support::ulittle32_t>(std::time(nullptr));
    m.ModuleNameRVA = static_cast<llvm::support::ulittle32_t>(
        size_before + module_stream_size + helper_data.GetByteSize());
    m.VersionInfo = info;
    m.CvRecord = ld_cv;
    m.MiscRecord = ld;

    error = WriteString(module_name, &helper_data);

    if (error.Fail())
      return error;

    m_data.AppendData(&m, sizeof(llvm::minidump::Module));
  }

  m_data.AppendData(helper_data.GetBytes(), helper_data.GetByteSize());
  return error;
}

uint16_t read_register_u16_raw(RegisterContext *reg_ctx,
                               llvm::StringRef reg_name) {
  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
  if (!reg_info)
    return 0;
  lldb_private::RegisterValue reg_value;
  bool success = reg_ctx->ReadRegister(reg_info, reg_value);
  if (!success)
    return 0;
  return reg_value.GetAsUInt16();
}

uint32_t read_register_u32_raw(RegisterContext *reg_ctx,
                               llvm::StringRef reg_name) {
  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
  if (!reg_info)
    return 0;
  lldb_private::RegisterValue reg_value;
  bool success = reg_ctx->ReadRegister(reg_info, reg_value);
  if (!success)
    return 0;
  return reg_value.GetAsUInt32();
}

uint64_t read_register_u64_raw(RegisterContext *reg_ctx,
                               llvm::StringRef reg_name) {
  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
  if (!reg_info)
    return 0;
  lldb_private::RegisterValue reg_value;
  bool success = reg_ctx->ReadRegister(reg_info, reg_value);
  if (!success)
    return 0;
  return reg_value.GetAsUInt64();
}

llvm::support::ulittle16_t read_register_u16(RegisterContext *reg_ctx,
                                             llvm::StringRef reg_name) {
  return static_cast<llvm::support::ulittle16_t>(
      read_register_u16_raw(reg_ctx, reg_name));
}

llvm::support::ulittle32_t read_register_u32(RegisterContext *reg_ctx,
                                             llvm::StringRef reg_name) {
  return static_cast<llvm::support::ulittle32_t>(
      read_register_u32_raw(reg_ctx, reg_name));
}

llvm::support::ulittle64_t read_register_u64(RegisterContext *reg_ctx,
                                             llvm::StringRef reg_name) {
  return static_cast<llvm::support::ulittle64_t>(
      read_register_u64_raw(reg_ctx, reg_name));
}

void read_register_u128(RegisterContext *reg_ctx, llvm::StringRef reg_name,
                        uint8_t *dst) {
  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
  if (reg_info) {
    lldb_private::RegisterValue reg_value;
    if (reg_ctx->ReadRegister(reg_info, reg_value)) {
      Status error;
      uint32_t bytes_copied = reg_value.GetAsMemoryData(
          *reg_info, dst, 16, lldb::ByteOrder::eByteOrderLittle, error);
      if (bytes_copied == 16)
        return;
    }
  }
  // If anything goes wrong, then zero out the register value.
  memset(dst, 0, 16);
}

lldb_private::minidump::MinidumpContext_x86_64
GetThreadContext_x86_64(RegisterContext *reg_ctx) {
  lldb_private::minidump::MinidumpContext_x86_64 thread_context = {};
  thread_context.p1_home = {};
  thread_context.context_flags = static_cast<uint32_t>(
      lldb_private::minidump::MinidumpContext_x86_64_Flags::x86_64_Flag |
      lldb_private::minidump::MinidumpContext_x86_64_Flags::Control |
      lldb_private::minidump::MinidumpContext_x86_64_Flags::Segments |
      lldb_private::minidump::MinidumpContext_x86_64_Flags::Integer);
  thread_context.rax = read_register_u64(reg_ctx, "rax");
  thread_context.rbx = read_register_u64(reg_ctx, "rbx");
  thread_context.rcx = read_register_u64(reg_ctx, "rcx");
  thread_context.rdx = read_register_u64(reg_ctx, "rdx");
  thread_context.rdi = read_register_u64(reg_ctx, "rdi");
  thread_context.rsi = read_register_u64(reg_ctx, "rsi");
  thread_context.rbp = read_register_u64(reg_ctx, "rbp");
  thread_context.rsp = read_register_u64(reg_ctx, "rsp");
  thread_context.r8 = read_register_u64(reg_ctx, "r8");
  thread_context.r9 = read_register_u64(reg_ctx, "r9");
  thread_context.r10 = read_register_u64(reg_ctx, "r10");
  thread_context.r11 = read_register_u64(reg_ctx, "r11");
  thread_context.r12 = read_register_u64(reg_ctx, "r12");
  thread_context.r13 = read_register_u64(reg_ctx, "r13");
  thread_context.r14 = read_register_u64(reg_ctx, "r14");
  thread_context.r15 = read_register_u64(reg_ctx, "r15");
  thread_context.rip = read_register_u64(reg_ctx, "rip");
  thread_context.eflags = read_register_u32(reg_ctx, "rflags");
  thread_context.cs = read_register_u16(reg_ctx, "cs");
  thread_context.fs = read_register_u16(reg_ctx, "fs");
  thread_context.gs = read_register_u16(reg_ctx, "gs");
  thread_context.ss = read_register_u16(reg_ctx, "ss");
  thread_context.ds = read_register_u16(reg_ctx, "ds");
  return thread_context;
}

minidump::RegisterContextMinidump_ARM64::Context
GetThreadContext_ARM64(RegisterContext *reg_ctx) {
  minidump::RegisterContextMinidump_ARM64::Context thread_context = {};
  thread_context.context_flags = static_cast<uint32_t>(
      minidump::RegisterContextMinidump_ARM64::Flags::ARM64_Flag |
      minidump::RegisterContextMinidump_ARM64::Flags::Integer |
      minidump::RegisterContextMinidump_ARM64::Flags::FloatingPoint);
  char reg_name[16];
  for (uint32_t i = 0; i < 31; ++i) {
    snprintf(reg_name, sizeof(reg_name), "x%u", i);
    thread_context.x[i] = read_register_u64(reg_ctx, reg_name);
  }
  // Work around a bug in debugserver where "sp" on arm64 doesn't have the alt
  // name set to "x31"
  thread_context.x[31] = read_register_u64(reg_ctx, "sp");
  thread_context.pc = read_register_u64(reg_ctx, "pc");
  thread_context.cpsr = read_register_u32(reg_ctx, "cpsr");
  thread_context.fpsr = read_register_u32(reg_ctx, "fpsr");
  thread_context.fpcr = read_register_u32(reg_ctx, "fpcr");
  for (uint32_t i = 0; i < 32; ++i) {
    snprintf(reg_name, sizeof(reg_name), "v%u", i);
    read_register_u128(reg_ctx, reg_name, &thread_context.v[i * 16]);
  }
  return thread_context;
}

class ArchThreadContexts {
  llvm::Triple::ArchType m_arch;
  union {
    lldb_private::minidump::MinidumpContext_x86_64 x86_64;
    lldb_private::minidump::RegisterContextMinidump_ARM64::Context arm64;
  };

public:
  ArchThreadContexts(llvm::Triple::ArchType arch) : m_arch(arch) {}

  bool prepareRegisterContext(RegisterContext *reg_ctx) {
    switch (m_arch) {
    case llvm::Triple::ArchType::x86_64:
      x86_64 = GetThreadContext_x86_64(reg_ctx);
      return true;
    case llvm::Triple::ArchType::aarch64:
      arm64 = GetThreadContext_ARM64(reg_ctx);
      return true;
    default:
      break;
    }
    return false;
  }

  const void *data() const { return &x86_64; }

  size_t size() const {
    switch (m_arch) {
    case llvm::Triple::ArchType::x86_64:
      return sizeof(x86_64);
    case llvm::Triple::ArchType::aarch64:
      return sizeof(arm64);
    default:
      break;
    }
    return 0;
  }
};

Status MinidumpFileBuilder::FixThreadStacks() {
  Status error;
  // If we have anything in the heap flush it.
  FlushBufferToDisk();
  m_core_file->SeekFromStart(m_thread_list_start);
  for (auto &pair : m_thread_by_range_end) {
    // The thread objects will get a new memory descriptor added
    // When we are emitting the memory list and then we write it here
    const llvm::minidump::Thread &thread = pair.second;
    size_t bytes_to_write = sizeof(llvm::minidump::Thread);
    size_t bytes_written = bytes_to_write;
    error = m_core_file->Write(&thread, bytes_written);
    if (error.Fail() || bytes_to_write != bytes_written) {
      error.SetErrorStringWithFormat(
          "Wrote incorrect number of bytes to minidump file. (written %zd/%zd)",
          bytes_written, bytes_to_write);
      return error;
    }
  }

  return error;
}

Status MinidumpFileBuilder::AddThreadList() {
  constexpr size_t minidump_thread_size = sizeof(llvm::minidump::Thread);
  lldb_private::ThreadList thread_list = m_process_sp->GetThreadList();

  // size of the entire thread stream consists of:
  // number of threads and threads array
  size_t thread_stream_size = sizeof(llvm::support::ulittle32_t) +
                              thread_list.GetSize() * minidump_thread_size;
  // save for the ability to set up RVA
  size_t size_before = GetCurrentDataEndOffset();
  Status error;
  error = AddDirectory(StreamType::ThreadList, thread_stream_size);
  if (error.Fail())
    return error;

  llvm::support::ulittle32_t thread_count =
      static_cast<llvm::support::ulittle32_t>(thread_list.GetSize());
  m_data.AppendData(&thread_count, sizeof(llvm::support::ulittle32_t));

  // Take the offset after the thread count.
  m_thread_list_start = GetCurrentDataEndOffset();
  DataBufferHeap helper_data;

  const uint32_t num_threads = thread_list.GetSize();
  Log *log = GetLog(LLDBLog::Object);
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    ThreadSP thread_sp(thread_list.GetThreadAtIndex(thread_idx));
    RegisterContextSP reg_ctx_sp(thread_sp->GetRegisterContext());

    if (!reg_ctx_sp) {
      error.SetErrorString("Unable to get the register context.");
      return error;
    }
    RegisterContext *reg_ctx = reg_ctx_sp.get();
    Target &target = m_process_sp->GetTarget();
    const ArchSpec &arch = target.GetArchitecture();
    ArchThreadContexts thread_context(arch.GetMachine());
    if (!thread_context.prepareRegisterContext(reg_ctx)) {
      error.SetErrorStringWithFormat(
          "architecture %s not supported.",
          arch.GetTriple().getArchName().str().c_str());
      return error;
    }

    uint64_t sp = reg_ctx->GetSP();
    MemoryRegionInfo sp_region;
    m_process_sp->GetMemoryRegionInfo(sp, sp_region);

    // Emit a blank descriptor
    MemoryDescriptor stack;
    LocationDescriptor empty_label;
    empty_label.DataSize = 0;
    empty_label.RVA = 0;
    stack.Memory = empty_label;
    stack.StartOfMemoryRange = 0;
    LocationDescriptor thread_context_memory_locator;
    thread_context_memory_locator.DataSize =
        static_cast<llvm::support::ulittle32_t>(thread_context.size());
    thread_context_memory_locator.RVA = static_cast<llvm::support::ulittle32_t>(
        size_before + thread_stream_size + helper_data.GetByteSize());
    // Cache thie thread context memory so we can reuse for exceptions.
    m_tid_to_reg_ctx[thread_sp->GetID()] = thread_context_memory_locator;

    LLDB_LOGF(log, "AddThreadList for thread %d: thread_context %zu bytes",
              thread_idx, thread_context.size());
    helper_data.AppendData(thread_context.data(), thread_context.size());

    llvm::minidump::Thread t;
    t.ThreadId = static_cast<llvm::support::ulittle32_t>(thread_sp->GetID());
    t.SuspendCount = static_cast<llvm::support::ulittle32_t>(
        (thread_sp->GetState() == StateType::eStateSuspended) ? 1 : 0);
    t.PriorityClass = static_cast<llvm::support::ulittle32_t>(0);
    t.Priority = static_cast<llvm::support::ulittle32_t>(0);
    t.EnvironmentBlock = static_cast<llvm::support::ulittle64_t>(0);
    t.Stack = stack, t.Context = thread_context_memory_locator;

    // We save off the stack object so we can circle back and clean it up.
    m_thread_by_range_end[sp_region.GetRange().GetRangeEnd()] = t;
    m_data.AppendData(&t, sizeof(llvm::minidump::Thread));
  }

  LLDB_LOGF(log, "AddThreadList(): total helper_data %" PRIx64 " bytes",
            helper_data.GetByteSize());
  m_data.AppendData(helper_data.GetBytes(), helper_data.GetByteSize());
  return Status();
}

Status MinidumpFileBuilder::AddExceptions() {
  lldb_private::ThreadList thread_list = m_process_sp->GetThreadList();
  Status error;
  const uint32_t num_threads = thread_list.GetSize();
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    ThreadSP thread_sp(thread_list.GetThreadAtIndex(thread_idx));
    StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
    bool add_exception = false;
    if (stop_info_sp) {
      switch (stop_info_sp->GetStopReason()) {
      case eStopReasonSignal:
      case eStopReasonException:
        add_exception = true;
        break;
      default:
        break;
      }
    }
    if (add_exception) {
      constexpr size_t minidump_exception_size =
          sizeof(llvm::minidump::ExceptionStream);
      error = AddDirectory(StreamType::Exception, minidump_exception_size);
      if (error.Fail())
        return error;

      StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
      RegisterContextSP reg_ctx_sp(thread_sp->GetRegisterContext());
      Exception exp_record = {};
      exp_record.ExceptionCode =
          static_cast<llvm::support::ulittle32_t>(stop_info_sp->GetValue());
      exp_record.ExceptionFlags = static_cast<llvm::support::ulittle32_t>(0);
      exp_record.ExceptionRecord = static_cast<llvm::support::ulittle64_t>(0);
      exp_record.ExceptionAddress = reg_ctx_sp->GetPC();
      exp_record.NumberParameters = static_cast<llvm::support::ulittle32_t>(0);
      exp_record.UnusedAlignment = static_cast<llvm::support::ulittle32_t>(0);
      // exp_record.ExceptionInformation;

      ExceptionStream exp_stream;
      exp_stream.ThreadId =
          static_cast<llvm::support::ulittle32_t>(thread_sp->GetID());
      exp_stream.UnusedAlignment = static_cast<llvm::support::ulittle32_t>(0);
      exp_stream.ExceptionRecord = exp_record;
      auto Iter = m_tid_to_reg_ctx.find(thread_sp->GetID());
      if (Iter != m_tid_to_reg_ctx.end()) {
        exp_stream.ThreadContext = Iter->second;
      } else {
        exp_stream.ThreadContext.DataSize = 0;
        exp_stream.ThreadContext.RVA = 0;
      }
      m_data.AppendData(&exp_stream, minidump_exception_size);
    }
  }

  return error;
}

lldb_private::Status MinidumpFileBuilder::AddMiscInfo() {
  Status error;
  error = AddDirectory(StreamType::MiscInfo,
                       sizeof(lldb_private::minidump::MinidumpMiscInfo));
  if (error.Fail())
    return error;

  lldb_private::minidump::MinidumpMiscInfo misc_info;
  misc_info.size = static_cast<llvm::support::ulittle32_t>(
      sizeof(lldb_private::minidump::MinidumpMiscInfo));
  // Default set flags1 to 0, in case that we will not be able to
  // get any information
  misc_info.flags1 = static_cast<llvm::support::ulittle32_t>(0);

  lldb_private::ProcessInstanceInfo process_info;
  m_process_sp->GetProcessInfo(process_info);
  if (process_info.ProcessIDIsValid()) {
    // Set flags1 to reflect that PID is filled in
    misc_info.flags1 =
        static_cast<llvm::support::ulittle32_t>(static_cast<uint32_t>(
            lldb_private::minidump::MinidumpMiscInfoFlags::ProcessID));
    misc_info.process_id =
        static_cast<llvm::support::ulittle32_t>(process_info.GetProcessID());
  }

  m_data.AppendData(&misc_info,
                    sizeof(lldb_private::minidump::MinidumpMiscInfo));
  return error;
}

std::unique_ptr<llvm::MemoryBuffer>
getFileStreamHelper(const std::string &path) {
  auto maybe_stream = llvm::MemoryBuffer::getFileAsStream(path);
  if (!maybe_stream)
    return nullptr;
  return std::move(maybe_stream.get());
}

Status MinidumpFileBuilder::AddLinuxFileStreams() {
  Status error;
  // No-op if we are not on linux.
  if (m_process_sp->GetTarget().GetArchitecture().GetTriple().getOS() !=
      llvm::Triple::Linux)
    return error;

  std::vector<std::pair<StreamType, std::string>> files_with_stream_types = {
      {StreamType::LinuxCPUInfo, "/proc/cpuinfo"},
      {StreamType::LinuxLSBRelease, "/etc/lsb-release"},
  };

  lldb_private::ProcessInstanceInfo process_info;
  m_process_sp->GetProcessInfo(process_info);
  if (process_info.ProcessIDIsValid()) {
    lldb::pid_t pid = process_info.GetProcessID();
    std::string pid_str = std::to_string(pid);
    files_with_stream_types.push_back(
        {StreamType::LinuxProcStatus, "/proc/" + pid_str + "/status"});
    files_with_stream_types.push_back(
        {StreamType::LinuxCMDLine, "/proc/" + pid_str + "/cmdline"});
    files_with_stream_types.push_back(
        {StreamType::LinuxEnviron, "/proc/" + pid_str + "/environ"});
    files_with_stream_types.push_back(
        {StreamType::LinuxAuxv, "/proc/" + pid_str + "/auxv"});
    files_with_stream_types.push_back(
        {StreamType::LinuxMaps, "/proc/" + pid_str + "/maps"});
    files_with_stream_types.push_back(
        {StreamType::LinuxProcStat, "/proc/" + pid_str + "/stat"});
    files_with_stream_types.push_back(
        {StreamType::LinuxProcFD, "/proc/" + pid_str + "/fd"});
  }

  for (const auto &entry : files_with_stream_types) {
    StreamType stream = entry.first;
    std::string path = entry.second;
    auto memory_buffer = getFileStreamHelper(path);

    if (memory_buffer) {
      size_t size = memory_buffer->getBufferSize();
      if (size == 0)
        continue;
      error = AddDirectory(stream, size);
      if (error.Fail())
        return error;
      m_data.AppendData(memory_buffer->getBufferStart(), size);
    }
  }

  return error;
}

Status MinidumpFileBuilder::AddMemoryList(SaveCoreStyle core_style) {
  Status error;

  // We first save the thread stacks to ensure they fit in the first UINT32_MAX
  // bytes of the core file. Thread structures in minidump files can only use
  // 32 bit memory descriptiors, so we emit them first to ensure the memory is
  // in accessible with a 32 bit offset.
  Process::CoreFileMemoryRanges ranges_32;
  Process::CoreFileMemoryRanges ranges_64;
  error = m_process_sp->CalculateCoreFileSaveRanges(
      SaveCoreStyle::eSaveCoreStackOnly, ranges_32);
  if (error.Fail())
    return error;

  // Calculate totalsize including the current offset.
  uint64_t total_size = GetCurrentDataEndOffset();
  total_size += ranges_32.size() * sizeof(llvm::minidump::MemoryDescriptor);
  std::unordered_set<addr_t> stack_start_addresses;
  for (const auto &core_range : ranges_32) {
    stack_start_addresses.insert(core_range.range.start());
    total_size += core_range.range.size();
  }

  if (total_size >= UINT32_MAX) {
    error.SetErrorStringWithFormat("Unable to write minidump. Stack memory "
                                   "exceeds 32b limit. (Num Stacks %zu)",
                                   ranges_32.size());
    return error;
  }

  Process::CoreFileMemoryRanges all_core_memory_ranges;
  if (core_style != SaveCoreStyle::eSaveCoreStackOnly) {
    error = m_process_sp->CalculateCoreFileSaveRanges(core_style,
                                                      all_core_memory_ranges);
    if (error.Fail())
      return error;
  }

  // After saving the stacks, we start packing as much as we can into 32b.
  // We apply a generous padding here so that the Directory, MemoryList and
  // Memory64List sections all begin in 32b addressable space.
  // Then anything overflow extends into 64b addressable space.
  // All core memeroy ranges will either container nothing on stacks only
  // or all the memory ranges including stacks
  if (!all_core_memory_ranges.empty())
    total_size +=
        256 + (all_core_memory_ranges.size() - stack_start_addresses.size()) *
                  sizeof(llvm::minidump::MemoryDescriptor_64);

  for (const auto &core_range : all_core_memory_ranges) {
    const addr_t range_size = core_range.range.size();
    if (stack_start_addresses.count(core_range.range.start()) > 0)
      // Don't double save stacks.
      continue;

    if (total_size + range_size < UINT32_MAX) {
      ranges_32.push_back(core_range);
      total_size += range_size;
    } else {
      ranges_64.push_back(core_range);
    }
  }

  error = AddMemoryList_32(ranges_32);
  if (error.Fail())
    return error;

  // Add the remaining memory as a 64b range.
  if (!ranges_64.empty()) {
    error = AddMemoryList_64(ranges_64);
    if (error.Fail())
      return error;
  }

  return FixThreadStacks();
}

Status MinidumpFileBuilder::DumpHeader() const {
  // write header
  llvm::minidump::Header header;
  header.Signature = static_cast<llvm::support::ulittle32_t>(
      llvm::minidump::Header::MagicSignature);
  header.Version = static_cast<llvm::support::ulittle32_t>(
      llvm::minidump::Header::MagicVersion);
  header.NumberOfStreams =
      static_cast<llvm::support::ulittle32_t>(m_directories.size());
  // We write the directories right after the header.
  header.StreamDirectoryRVA =
      static_cast<llvm::support::ulittle32_t>(HEADER_SIZE);
  header.Checksum = static_cast<llvm::support::ulittle32_t>(
      0u), // not used in most of the writers
      header.TimeDateStamp =
          static_cast<llvm::support::ulittle32_t>(std::time(nullptr));
  header.Flags =
      static_cast<llvm::support::ulittle64_t>(0u); // minidump normal flag

  Status error;
  size_t bytes_written;

  m_core_file->SeekFromStart(0);
  bytes_written = HEADER_SIZE;
  error = m_core_file->Write(&header, bytes_written);
  if (error.Fail() || bytes_written != HEADER_SIZE) {
    if (bytes_written != HEADER_SIZE)
      error.SetErrorStringWithFormat(
          "Unable to write the minidump header (written %zd/%zd)",
          bytes_written, HEADER_SIZE);
    return error;
  }
  return error;
}

offset_t MinidumpFileBuilder::GetCurrentDataEndOffset() const {
  return m_data.GetByteSize() + m_saved_data_size;
}

Status MinidumpFileBuilder::DumpDirectories() const {
  Status error;
  size_t bytes_written;
  m_core_file->SeekFromStart(HEADER_SIZE);
  for (const Directory &dir : m_directories) {
    bytes_written = DIRECTORY_SIZE;
    error = m_core_file->Write(&dir, bytes_written);
    if (error.Fail() || bytes_written != DIRECTORY_SIZE) {
      if (bytes_written != DIRECTORY_SIZE)
        error.SetErrorStringWithFormat(
            "unable to write the directory (written %zd/%zd)", bytes_written,
            DIRECTORY_SIZE);
      return error;
    }
  }

  return error;
}

static uint64_t
GetLargestRangeSize(const Process::CoreFileMemoryRanges &ranges) {
  uint64_t max_size = 0;
  for (const auto &core_range : ranges)
    max_size = std::max(max_size, core_range.range.size());
  return max_size;
}

Status
MinidumpFileBuilder::AddMemoryList_32(Process::CoreFileMemoryRanges &ranges) {
  std::vector<MemoryDescriptor> descriptors;
  Status error;
  if (ranges.size() == 0)
    return error;

  Log *log = GetLog(LLDBLog::Object);
  size_t region_index = 0;
  auto data_up =
      std::make_unique<DataBufferHeap>(GetLargestRangeSize(ranges), 0);
  for (const auto &core_range : ranges) {
    // Take the offset before we write.
    const offset_t offset_for_data = GetCurrentDataEndOffset();
    const addr_t addr = core_range.range.start();
    const addr_t size = core_range.range.size();
    const addr_t end = core_range.range.end();

    LLDB_LOGF(log,
              "AddMemoryList %zu/%zu reading memory for region "
              "(%" PRIx64 " bytes) [%" PRIx64 ", %" PRIx64 ")",
              region_index, ranges.size(), size, addr, addr + size);
    ++region_index;

    const size_t bytes_read =
        m_process_sp->ReadMemory(addr, data_up->GetBytes(), size, error);
    if (error.Fail() || bytes_read == 0) {
      LLDB_LOGF(log, "Failed to read memory region. Bytes read: %zu, error: %s",
                bytes_read, error.AsCString());
      // Just skip sections with errors or zero bytes in 32b mode
      continue;
    } else if (bytes_read != size) {
      LLDB_LOGF(
          log, "Memory region at: %" PRIx64 " failed to read %" PRIx64 " bytes",
          addr, size);
    }

    MemoryDescriptor descriptor;
    descriptor.StartOfMemoryRange =
        static_cast<llvm::support::ulittle64_t>(addr);
    descriptor.Memory.DataSize =
        static_cast<llvm::support::ulittle32_t>(bytes_read);
    descriptor.Memory.RVA =
        static_cast<llvm::support::ulittle32_t>(offset_for_data);
    descriptors.push_back(descriptor);
    if (m_thread_by_range_end.count(end) > 0)
      m_thread_by_range_end[end].Stack = descriptor;

    // Add the data to the buffer, flush as needed.
    error = AddData(data_up->GetBytes(), bytes_read);
    if (error.Fail())
      return error;
  }

  // Add a directory that references this list
  // With a size of the number of ranges as a 32 bit num
  // And then the size of all the ranges
  error = AddDirectory(StreamType::MemoryList,
                       sizeof(llvm::support::ulittle32_t) +
                           descriptors.size() *
                               sizeof(llvm::minidump::MemoryDescriptor));
  if (error.Fail())
    return error;

  llvm::support::ulittle32_t memory_ranges_num =
      static_cast<llvm::support::ulittle32_t>(descriptors.size());
  m_data.AppendData(&memory_ranges_num, sizeof(llvm::support::ulittle32_t));
  // For 32b we can get away with writing off the descriptors after the data.
  // This means no cleanup loop needed.
  m_data.AppendData(descriptors.data(),
                    descriptors.size() * sizeof(MemoryDescriptor));

  return error;
}

Status
MinidumpFileBuilder::AddMemoryList_64(Process::CoreFileMemoryRanges &ranges) {
  Status error;
  if (ranges.empty())
    return error;

  error = AddDirectory(StreamType::Memory64List,
                       (sizeof(llvm::support::ulittle64_t) * 2) +
                           ranges.size() *
                               sizeof(llvm::minidump::MemoryDescriptor_64));
  if (error.Fail())
    return error;

  llvm::support::ulittle64_t memory_ranges_num =
      static_cast<llvm::support::ulittle64_t>(ranges.size());
  m_data.AppendData(&memory_ranges_num, sizeof(llvm::support::ulittle64_t));
  // Capture the starting offset for all the descriptors so we can clean them up
  // if needed.
  offset_t starting_offset =
      GetCurrentDataEndOffset() + sizeof(llvm::support::ulittle64_t);
  // The base_rva needs to start after the directories, which is right after
  // this 8 byte variable.
  offset_t base_rva =
      starting_offset +
      (ranges.size() * sizeof(llvm::minidump::MemoryDescriptor_64));
  llvm::support::ulittle64_t memory_ranges_base_rva =
      static_cast<llvm::support::ulittle64_t>(base_rva);
  m_data.AppendData(&memory_ranges_base_rva,
                    sizeof(llvm::support::ulittle64_t));

  bool cleanup_required = false;
  std::vector<MemoryDescriptor_64> descriptors;
  // Enumerate the ranges and create the memory descriptors so we can append
  // them first
  for (const auto core_range : ranges) {
    // Add the space required to store the memory descriptor
    MemoryDescriptor_64 memory_desc;
    memory_desc.StartOfMemoryRange =
        static_cast<llvm::support::ulittle64_t>(core_range.range.start());
    memory_desc.DataSize =
        static_cast<llvm::support::ulittle64_t>(core_range.range.size());
    descriptors.push_back(memory_desc);
    // Now write this memory descriptor to the buffer.
    m_data.AppendData(&memory_desc, sizeof(MemoryDescriptor_64));
  }

  Log *log = GetLog(LLDBLog::Object);
  size_t region_index = 0;
  auto data_up =
      std::make_unique<DataBufferHeap>(GetLargestRangeSize(ranges), 0);
  for (const auto &core_range : ranges) {
    const addr_t addr = core_range.range.start();
    const addr_t size = core_range.range.size();

    LLDB_LOGF(log,
              "AddMemoryList_64 %zu/%zu reading memory for region "
              "(%" PRIx64 "bytes) "
              "[%" PRIx64 ", %" PRIx64 ")",
              region_index, ranges.size(), size, addr, addr + size);
    ++region_index;

    const size_t bytes_read =
        m_process_sp->ReadMemory(addr, data_up->GetBytes(), size, error);
    if (error.Fail()) {
      LLDB_LOGF(log, "Failed to read memory region. Bytes read: %zu, error: %s",
                bytes_read, error.AsCString());
      error.Clear();
      cleanup_required = true;
      descriptors[region_index].DataSize = 0;
    }
    if (bytes_read != size) {
      LLDB_LOGF(
          log, "Memory region at: %" PRIx64 " failed to read %" PRIx64 " bytes",
          addr, size);
      cleanup_required = true;
      descriptors[region_index].DataSize = bytes_read;
    }

    // Add the data to the buffer, flush as needed.
    error = AddData(data_up->GetBytes(), bytes_read);
    if (error.Fail())
      return error;
  }

  // Early return if there is no cleanup needed.
  if (!cleanup_required) {
    return error;
  } else {
    // Flush to disk we can make the fixes in place.
    FlushBufferToDisk();
    // Fixup the descriptors that were not read correctly.
    m_core_file->SeekFromStart(starting_offset);
    size_t bytes_written = sizeof(MemoryDescriptor_64) * descriptors.size();
    error = m_core_file->Write(descriptors.data(), bytes_written);
    if (error.Fail() ||
        bytes_written != sizeof(MemoryDescriptor_64) * descriptors.size()) {
      error.SetErrorStringWithFormat(
          "unable to write the memory descriptors (written %zd/%zd)",
          bytes_written, sizeof(MemoryDescriptor_64) * descriptors.size());
    }

    return error;
  }
}

Status MinidumpFileBuilder::AddData(const void *data, uint64_t size) {
  // This should also get chunked, because worst case we copy over a big
  // object / memory range, say 5gb. In that case, we'd have to allocate 10gb
  // 5 gb for the buffer we're copying from, and then 5gb for the buffer we're
  // copying to. Which will be short lived and immedaitely go to disk, the goal
  // here is to limit the number of bytes we need to host in memory at any given
  // time.
  m_data.AppendData(data, size);
  if (m_data.GetByteSize() > MAX_WRITE_CHUNK_SIZE)
    return FlushBufferToDisk();

  return Status();
}

Status MinidumpFileBuilder::FlushBufferToDisk() {
  Status error;
  // Set the stream to it's end.
  m_core_file->SeekFromStart(m_saved_data_size);
  addr_t starting_size = m_data.GetByteSize();
  addr_t remaining_bytes = starting_size;
  offset_t offset = 0;

  while (remaining_bytes > 0) {
    size_t bytes_written = remaining_bytes;
    // We don't care how many bytes we wrote unless we got an error
    // so just decrement the remaining bytes.
    error = m_core_file->Write(m_data.GetBytes() + offset, bytes_written);
    if (error.Fail()) {
      error.SetErrorStringWithFormat(
          "Wrote incorrect number of bytes to minidump file. (written %" PRIx64
          "/%" PRIx64 ")",
          starting_size - remaining_bytes, starting_size);
      return error;
    }

    offset += bytes_written;
    remaining_bytes -= bytes_written;
  }

  m_saved_data_size += starting_size;
  m_data.Clear();
  return error;
}

Status MinidumpFileBuilder::DumpFile() {
  Status error;
  // If anything is left unsaved, dump it.
  error = FlushBufferToDisk();
  if (error.Fail())
    return error;

  // Overwrite the header which we filled in earlier.
  error = DumpHeader();
  if (error.Fail())
    return error;

  // Overwrite the space saved for directories
  error = DumpDirectories();
  if (error.Fail())
    return error;

  return error;
}
