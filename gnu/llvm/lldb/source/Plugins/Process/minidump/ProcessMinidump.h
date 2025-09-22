//===-- ProcessMinidump.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_PROCESSMINIDUMP_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_PROCESSMINIDUMP_H

#include "MinidumpParser.h"
#include "MinidumpTypes.h"

#include "lldb/Target/PostMortemProcess.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace lldb_private {

namespace minidump {

class ProcessMinidump : public PostMortemProcess {
public:
  static lldb::ProcessSP CreateInstance(lldb::TargetSP target_sp,
                                        lldb::ListenerSP listener_sp,
                                        const FileSpec *crash_file_path,
                                        bool can_connect);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "minidump"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  ProcessMinidump(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                  const FileSpec &core_file, lldb::DataBufferSP code_data);

  ~ProcessMinidump() override;

  bool CanDebug(lldb::TargetSP target_sp,
                bool plugin_specified_by_name) override;

  CommandObject *GetPluginCommandObject() override;

  Status DoLoadCore() override;

  DynamicLoader *GetDynamicLoader() override { return nullptr; }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  SystemRuntime *GetSystemRuntime() override { return nullptr; }

  Status DoDestroy() override;

  void RefreshStateAfterStop() override;

  bool IsAlive() override;

  bool WarnBeforeDetach() const override;

  size_t ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    Status &error) override;

  size_t DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                      Status &error) override;

  ArchSpec GetArchitecture();

  Status GetMemoryRegions(
      lldb_private::MemoryRegionInfos &region_list) override;

  bool GetProcessInfo(ProcessInstanceInfo &info) override;

  Status WillResume() override {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support resuming processes", GetPluginName());
    return error;
  }

  std::optional<MinidumpParser> m_minidump_parser;

protected:
  void Clear();

  bool DoUpdateThreadList(ThreadList &old_thread_list,
                          ThreadList &new_thread_list) override;

  Status DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                               MemoryRegionInfo &range_info) override;

  void ReadModuleList();

  lldb::ModuleSP GetOrCreateModule(lldb_private::UUID minidump_uuid,
                                   llvm::StringRef name,
                                   lldb_private::ModuleSpec module_spec);

  JITLoaderList &GetJITLoaders() override;

private:
  lldb::DataBufferSP m_core_data;
  llvm::ArrayRef<minidump::Thread> m_thread_list;
  const minidump::ExceptionStream *m_active_exception;
  lldb::CommandObjectSP m_command_sp;
  bool m_is_wow64;
  std::optional<MemoryRegionInfos> m_memory_regions;

  void BuildMemoryRegions();
};

} // namespace minidump
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_PROCESSMINIDUMP_H
