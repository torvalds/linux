//===-- ProcessMachCore.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_PROCESSMACHCORE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_PROCESSMACHCORE_H

#include <list>
#include <vector>

#include "lldb/Target/PostMortemProcess.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"

class ThreadKDP;

class ProcessMachCore : public lldb_private::PostMortemProcess {
public:
  // Constructors and Destructors
  ProcessMachCore(lldb::TargetSP target_sp, lldb::ListenerSP listener,
                  const lldb_private::FileSpec &core_file);

  ~ProcessMachCore() override;

  static lldb::ProcessSP
  CreateInstance(lldb::TargetSP target_sp, lldb::ListenerSP listener,
                 const lldb_private::FileSpec *crash_file_path,
                 bool can_connect);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "mach-o-core"; }

  static llvm::StringRef GetPluginDescriptionStatic();

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

  // Process Queries
  bool IsAlive() override;

  bool WarnBeforeDetach() const override;

  // Process Memory
  size_t ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    lldb_private::Status &error) override;

  size_t DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                      lldb_private::Status &error) override;

  lldb::addr_t GetImageInfoAddress() override;

protected:
  friend class ThreadMachCore;

  void Clear();

  bool DoUpdateThreadList(lldb_private::ThreadList &old_thread_list,
                          lldb_private::ThreadList &new_thread_list) override;

  lldb_private::ObjectFile *GetCoreObjectFile();

  lldb_private::Status
  DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                        lldb_private::MemoryRegionInfo &region_info) override;

private:
  void CreateMemoryRegions();

  bool LoadBinaryViaLowmemUUID();

  /// \return
  ///   True if any metadata were found indicating the binary that should
  ///   be loaded, regardless of whether the specified binary could be found.
  ///   False if no metadata were present.
  bool LoadBinariesViaMetadata();

  void LoadBinariesViaExhaustiveSearch();
  void LoadBinariesAndSetDYLD();
  void CleanupMemoryRegionPermissions();

  bool CheckAddressForDyldOrKernel(lldb::addr_t addr, lldb::addr_t &dyld,
                                   lldb::addr_t &kernel);

  enum CorefilePreference { eUserProcessCorefile, eKernelCorefile };

  /// If a core file can be interpreted multiple ways, this establishes
  /// which style wins.
  ///
  /// If a core file contains both a kernel binary and a user-process
  /// dynamic loader, lldb needs to pick one over the other.  This could
  /// be a kernel corefile that happens to have a copy of dyld in its
  /// memory.  Or it could be a user process coredump of lldb while doing
  /// kernel debugging - so a copy of the kernel is in its heap.  This
  /// should become a setting so it can be over-ridden when necessary.
  CorefilePreference GetCorefilePreference() {
    // For now, if both user process and kernel binaries a present,
    // assume this is a kernel coredump which has a copy of a user
    // process dyld in one of its pages.
    return eKernelCorefile;
  }

  // For ProcessMachCore only
  typedef lldb_private::Range<lldb::addr_t, lldb::addr_t> FileRange;
  typedef lldb_private::RangeDataVector<lldb::addr_t, lldb::addr_t, FileRange>
      VMRangeToFileOffset;
  typedef lldb_private::RangeDataVector<lldb::addr_t, lldb::addr_t, uint32_t>
      VMRangeToPermissions;

  VMRangeToFileOffset m_core_aranges;
  VMRangeToPermissions m_core_range_infos;
  lldb::ModuleSP m_core_module_sp;
  lldb::addr_t m_dyld_addr;
  lldb::addr_t m_mach_kernel_addr;
  llvm::StringRef m_dyld_plugin_name;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACH_CORE_PROCESSMACHCORE_H
