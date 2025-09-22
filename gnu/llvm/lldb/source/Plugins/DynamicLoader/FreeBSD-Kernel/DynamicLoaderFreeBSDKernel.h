//===-- DynamicLoaderFreeBSDKernel.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_FREEBSD_KERNEL_DYNAMICLOADERFREEBSDKERNEL_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_FREEBSD_KERNEL_DYNAMICLOADERFREEBSDKERNEL_H

#include <mutex>
#include <string>
#include <vector>

#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"
#include "llvm/BinaryFormat/ELF.h"

class DynamicLoaderFreeBSDKernel : public lldb_private::DynamicLoader {
public:
  DynamicLoaderFreeBSDKernel(lldb_private::Process *process,
                             lldb::addr_t kernel_addr);

  ~DynamicLoaderFreeBSDKernel() override;

  // Static Functions

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "freebsd-kernel"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::DynamicLoader *
  CreateInstance(lldb_private::Process *process, bool force);

  static void DebuggerInit(lldb_private::Debugger &debugger);

  static lldb::addr_t FindFreeBSDKernel(lldb_private::Process *process);

  // Hooks for time point that after attach to some proccess
  void DidAttach() override;

  void DidLaunch() override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                                  bool stop_others) override;

  lldb_private::Status CanLoadImage() override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  class KModImageInfo {
  public:
    KModImageInfo()
        : m_module_sp(), m_memory_module_sp(), m_uuid(), m_name(), m_path() {}

    void Clear() {
      m_load_address = LLDB_INVALID_ADDRESS;
      m_name.clear();
      m_uuid.Clear();
      m_module_sp.reset();
      m_memory_module_sp.reset();
      m_stop_id = UINT32_MAX;
      m_path.clear();
    }

    void SetLoadAddress(lldb::addr_t load_address) {
      m_load_address = load_address;
    }

    lldb::addr_t GetLoadAddress() const { return m_load_address; }

    void SetUUID(const lldb_private::UUID uuid) { m_uuid = uuid; }

    lldb_private::UUID GetUUID() const { return m_uuid; }

    void SetName(const char *name) { m_name = name; }

    std::string GetName() const { return m_name; }

    void SetPath(const char *path) { m_path = path; }

    std::string GetPath() const { return m_path; }

    void SetModule(lldb::ModuleSP module) { m_module_sp = module; }

    lldb::ModuleSP GetModule() { return m_module_sp; }

    void SetIsKernel(bool is_kernel) { m_is_kernel = is_kernel; }

    bool IsKernel() const { return m_is_kernel; };

    void SetStopID(uint32_t stop_id) { m_stop_id = stop_id; }

    uint32_t GetStopID() { return m_stop_id; }

    bool IsLoaded() const { return m_stop_id != UINT32_MAX; };

    bool ReadMemoryModule(lldb_private::Process *process);

    bool LoadImageUsingMemoryModule(lldb_private::Process *process);

    bool LoadImageUsingFileAddress(lldb_private::Process *process);

    using collection_type = std::vector<KModImageInfo>;

  private:
    lldb::ModuleSP m_module_sp;
    lldb::ModuleSP m_memory_module_sp;
    lldb::addr_t m_load_address = LLDB_INVALID_ADDRESS;
    lldb_private::UUID m_uuid;
    bool m_is_kernel = false;
    std::string m_name;
    std::string m_path;
    uint32_t m_stop_id = UINT32_MAX;
  };

  void PrivateInitialize(lldb_private::Process *process);

  void Clear(bool clear_process);

  void Update();

  void LoadKernelModules();

  void ReadAllKmods();

  bool ReadAllKmods(lldb_private::Address linker_files_head_address,
                    KModImageInfo::collection_type &kmods_list);

  bool ReadKmodsListHeader();

  bool ParseKmods(lldb_private::Address linker_files_head_address);

  void SetNotificationBreakPoint();

  static lldb_private::UUID
  CheckForKernelImageAtAddress(lldb_private::Process *process,
                               lldb::addr_t address,
                               bool *read_error = nullptr);

  static lldb::addr_t FindKernelAtLoadAddress(lldb_private::Process *process);

  static bool ReadELFHeader(lldb_private::Process *process,
                            lldb::addr_t address, llvm::ELF::Elf32_Ehdr &header,
                            bool *read_error = nullptr);

  lldb_private::Process *m_process;
  lldb_private::Address m_linker_file_list_struct_addr;
  lldb_private::Address m_linker_file_head_addr;
  lldb::addr_t m_kernel_load_address;
  KModImageInfo m_kernel_image_info;
  KModImageInfo::collection_type m_linker_files_list;
  std::recursive_mutex m_mutex;
  std::unordered_map<std::string, lldb_private::UUID> m_kld_name_to_uuid;

private:
  DynamicLoaderFreeBSDKernel(const DynamicLoaderFreeBSDKernel &) = delete;

  const DynamicLoaderFreeBSDKernel &
  operator=(const DynamicLoaderFreeBSDKernel &) = delete;
};

#endif
