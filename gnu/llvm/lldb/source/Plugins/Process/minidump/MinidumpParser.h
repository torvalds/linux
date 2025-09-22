//===-- MinidumpParser.h -----------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPPARSER_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPPARSER_H

#include "MinidumpTypes.h"

#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Minidump.h"

// C includes

// C++ includes
#include <cstring>
#include <optional>
#include <unordered_map>

namespace lldb_private {

namespace minidump {

// Describes a range of memory captured in the Minidump
struct Range {
  lldb::addr_t start; // virtual address of the beginning of the range
  // range_ref - absolute pointer to the first byte of the range and size
  llvm::ArrayRef<uint8_t> range_ref;

  Range(lldb::addr_t start, llvm::ArrayRef<uint8_t> range_ref)
      : start(start), range_ref(range_ref) {}

  friend bool operator==(const Range &lhs, const Range &rhs) {
    return lhs.start == rhs.start && lhs.range_ref == rhs.range_ref;
  }
};

class MinidumpParser {
public:
  static llvm::Expected<MinidumpParser>
  Create(const lldb::DataBufferSP &data_buf_sp);

  llvm::ArrayRef<uint8_t> GetData();

  llvm::ArrayRef<uint8_t> GetStream(StreamType stream_type);

  UUID GetModuleUUID(const minidump::Module *module);

  llvm::ArrayRef<minidump::Thread> GetThreads();

  llvm::ArrayRef<uint8_t> GetThreadContext(const LocationDescriptor &location);

  llvm::ArrayRef<uint8_t> GetThreadContext(const minidump::Thread &td);

  llvm::ArrayRef<uint8_t> GetThreadContextWow64(const minidump::Thread &td);

  ArchSpec GetArchitecture();

  const MinidumpMiscInfo *GetMiscInfo();

  std::optional<LinuxProcStatus> GetLinuxProcStatus();

  std::optional<lldb::pid_t> GetPid();

  llvm::ArrayRef<minidump::Module> GetModuleList();

  // There are cases in which there is more than one record in the ModuleList
  // for the same module name.(e.g. when the binary has non contiguous segments)
  // So this function returns a filtered module list - if it finds records that
  // have the same name, it keeps the copy with the lowest load address.
  std::vector<const minidump::Module *> GetFilteredModuleList();

  const llvm::minidump::ExceptionStream *GetExceptionStream();

  std::optional<Range> FindMemoryRange(lldb::addr_t addr);

  llvm::ArrayRef<uint8_t> GetMemory(lldb::addr_t addr, size_t size);

  /// Returns a list of memory regions and a flag indicating whether the list is
  /// complete (includes all regions mapped into the process memory).
  std::pair<MemoryRegionInfos, bool> BuildMemoryRegions();

  static llvm::StringRef GetStreamTypeAsString(StreamType stream_type);

  llvm::object::MinidumpFile &GetMinidumpFile() { return *m_file; }

  static MemoryRegionInfo GetMemoryRegionInfo(const MemoryRegionInfos &regions,
                                              lldb::addr_t load_addr);

private:
  MinidumpParser(lldb::DataBufferSP data_sp,
                 std::unique_ptr<llvm::object::MinidumpFile> file);

  lldb::DataBufferSP m_data_sp;
  std::unique_ptr<llvm::object::MinidumpFile> m_file;
  ArchSpec m_arch;
};

} // end namespace minidump
} // end namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_MINIDUMPPARSER_H
