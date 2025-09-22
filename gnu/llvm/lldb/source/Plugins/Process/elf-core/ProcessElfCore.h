//===-- ProcessElfCore.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Notes about Linux Process core dumps:
//  1) Linux core dump is stored as ELF file.
//  2) The ELF file's PT_NOTE and PT_LOAD segments describes the program's
//     address space and thread contexts.
//  3) PT_NOTE segment contains note entries which describes a thread context.
//  4) PT_LOAD segment describes a valid contiguous range of process address
//     space.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_PROCESSELFCORE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_PROCESSELFCORE_H

#include <list>
#include <vector>

#include "lldb/Target/PostMortemProcess.h"
#include "lldb/Utility/Status.h"

#include "Plugins/ObjectFile/ELF/ELFHeader.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"

struct ThreadData;

class ProcessElfCore : public lldb_private::PostMortemProcess {
public:
  // Constructors and Destructors
  static lldb::ProcessSP
  CreateInstance(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                 const lldb_private::FileSpec *crash_file_path,
                 bool can_connect);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "elf-core"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  // Constructors and Destructors
  ProcessElfCore(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                 const lldb_private::FileSpec &core_file);

  ~ProcessElfCore() override;

  // Check if a given Process
  bool CanDebug(lldb::TargetSP target_sp,
                bool plugin_specified_by_name) override;

  // Creating a new process, or attaching to an existing one
  lldb_private::Status DoLoadCore() override;

  lldb_private::DynamicLoader *GetDynamicLoader() override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // Process Control
  lldb_private::Status DoDestroy() override;

  void RefreshStateAfterStop() override;

  lldb_private::Status WillResume() override {
    lldb_private::Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support resuming processes", GetPluginName());
    return error;
  }

  // Process Queries
  bool IsAlive() override;

  bool WarnBeforeDetach() const override { return false; }

  // Process Memory
  size_t ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    lldb_private::Status &error) override;

  size_t DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                      lldb_private::Status &error) override;

  // We do not implement DoReadMemoryTags. Instead all the work is done in
  // ReadMemoryTags which avoids having to unpack and repack tags.
  llvm::Expected<std::vector<lldb::addr_t>> ReadMemoryTags(lldb::addr_t addr,
                                                           size_t len) override;

  lldb::addr_t GetImageInfoAddress() override;

  lldb_private::ArchSpec GetArchitecture();

  // Returns AUXV structure found in the core file
  lldb_private::DataExtractor GetAuxvData() override;

  bool GetProcessInfo(lldb_private::ProcessInstanceInfo &info) override;

protected:
  void Clear();

  bool DoUpdateThreadList(lldb_private::ThreadList &old_thread_list,
                          lldb_private::ThreadList &new_thread_list) override;

  lldb_private::Status
  DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                        lldb_private::MemoryRegionInfo &region_info) override;

  bool SupportsMemoryTagging() override { return !m_core_tag_ranges.IsEmpty(); }

private:
  struct NT_FILE_Entry {
    lldb::addr_t start;
    lldb::addr_t end;
    lldb::addr_t file_ofs;
    std::string path;
    // Add a UUID member for convenient access. The UUID value is not in the
    // NT_FILE entries, we will find it in core memory and store it here for
    // easy access.
    lldb_private::UUID uuid;
  };

  // For ProcessElfCore only
  typedef lldb_private::Range<lldb::addr_t, lldb::addr_t> FileRange;
  typedef lldb_private::RangeDataVector<lldb::addr_t, lldb::addr_t, FileRange>
      VMRangeToFileOffset;
  typedef lldb_private::RangeDataVector<lldb::addr_t, lldb::addr_t, uint32_t>
      VMRangeToPermissions;

  lldb::ModuleSP m_core_module_sp;
  std::string m_dyld_plugin_name;

  // True if m_thread_contexts contains valid entries
  bool m_thread_data_valid = false;

  // Contain thread data read from NOTE segments
  std::vector<ThreadData> m_thread_data;

  // AUXV structure found from the NOTE segment
  lldb_private::DataExtractor m_auxv;

  // Address ranges found in the core
  VMRangeToFileOffset m_core_aranges;

  // Permissions for all ranges
  VMRangeToPermissions m_core_range_infos;

  // Memory tag ranges found in the core
  VMRangeToFileOffset m_core_tag_ranges;

  // NT_FILE entries found from the NOTE segment
  std::vector<NT_FILE_Entry> m_nt_file_entries;

  // Parse thread(s) data structures(prstatus, prpsinfo) from given NOTE segment
  llvm::Error ParseThreadContextsFromNoteSegment(
      const elf::ELFProgramHeader &segment_header,
      const lldb_private::DataExtractor &segment_data);

  // Returns number of thread contexts stored in the core file
  uint32_t GetNumThreadContexts();

  // Populate gnu uuid for each NT_FILE entry
  void UpdateBuildIdForNTFileEntries();

  // Returns the value of certain type of note of a given start address
  lldb_private::UUID FindBuidIdInCoreMemory(lldb::addr_t address);

  // Parse a contiguous address range of the process from LOAD segment
  lldb::addr_t
  AddAddressRangeFromLoadSegment(const elf::ELFProgramHeader &header);

  // Parse a contiguous address range from a memory tag segment
  lldb::addr_t
  AddAddressRangeFromMemoryTagSegment(const elf::ELFProgramHeader &header);

  llvm::Expected<std::vector<lldb_private::CoreNote>>
  parseSegment(const lldb_private::DataExtractor &segment);
  llvm::Error parseFreeBSDNotes(llvm::ArrayRef<lldb_private::CoreNote> notes);
  llvm::Error parseNetBSDNotes(llvm::ArrayRef<lldb_private::CoreNote> notes);
  llvm::Error parseOpenBSDNotes(llvm::ArrayRef<lldb_private::CoreNote> notes);
  llvm::Error parseLinuxNotes(llvm::ArrayRef<lldb_private::CoreNote> notes);
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_ELF_CORE_PROCESSELFCORE_H
