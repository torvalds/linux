//===-- ProcessWindows.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Windows_Common_ProcessWindows_H_
#define liblldb_Plugins_Process_Windows_Common_ProcessWindows_H_

#include "lldb/Target/Process.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"

#include "Plugins/DynamicLoader/Windows-DYLD/DynamicLoaderWindowsDYLD.h"
#include "ProcessDebugger.h"

namespace lldb_private {

class HostProcess;

class ProcessWindows : public Process, public ProcessDebugger {
public:
  // Static functions.
  static lldb::ProcessSP CreateInstance(lldb::TargetSP target_sp,
                                        lldb::ListenerSP listener_sp,
                                        const FileSpec *,
                                        bool can_connect);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "windows"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  ~ProcessWindows();

  size_t GetSTDOUT(char *buf, size_t buf_size, Status &error) override;
  size_t GetSTDERR(char *buf, size_t buf_size, Status &error) override;
  size_t PutSTDIN(const char *buf, size_t buf_size, Status &error) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  Status EnableBreakpointSite(BreakpointSite *bp_site) override;
  Status DisableBreakpointSite(BreakpointSite *bp_site) override;

  Status DoDetach(bool keep_stopped) override;
  Status DoLaunch(Module *exe_module, ProcessLaunchInfo &launch_info) override;
  Status DoAttachToProcessWithID(
      lldb::pid_t pid,
      const lldb_private::ProcessAttachInfo &attach_info) override;
  Status DoResume() override;
  Status DoDestroy() override;
  Status DoHalt(bool &caused_stop) override;

  void DidLaunch() override;
  void DidAttach(lldb_private::ArchSpec &arch_spec) override;

  void RefreshStateAfterStop() override;

  bool CanDebug(lldb::TargetSP target_sp,
                bool plugin_specified_by_name) override;
  bool DestroyRequiresHalt() override { return false; }
  bool DoUpdateThreadList(ThreadList &old_thread_list,
                          ThreadList &new_thread_list) override;
  bool IsAlive() override;

  ArchSpec GetSystemArchitecture() override;

  size_t DoReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                      Status &error) override;
  size_t DoWriteMemory(lldb::addr_t vm_addr, const void *buf, size_t size,
                       Status &error) override;
  lldb::addr_t DoAllocateMemory(size_t size, uint32_t permissions,
                                Status &error) override;
  Status DoDeallocateMemory(lldb::addr_t ptr) override;

  lldb::addr_t GetImageInfoAddress() override;

  DynamicLoaderWindowsDYLD *GetDynamicLoader() override;

  // IDebugDelegate overrides.
  void OnExitProcess(uint32_t exit_code) override;
  void OnDebuggerConnected(lldb::addr_t image_base) override;
  ExceptionResult OnDebugException(bool first_chance,
                                   const ExceptionRecord &record) override;
  void OnCreateThread(const HostThread &thread) override;
  void OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) override;
  void OnLoadDll(const ModuleSpec &module_spec,
                 lldb::addr_t module_addr) override;
  void OnUnloadDll(lldb::addr_t module_addr) override;
  void OnDebugString(const std::string &string) override;
  void OnDebuggerError(const Status &error, uint32_t type) override;

  std::optional<uint32_t> GetWatchpointSlotCount() override;
  Status EnableWatchpoint(lldb::WatchpointSP wp_sp,
                          bool notify = true) override;
  Status DisableWatchpoint(lldb::WatchpointSP wp_sp,
                           bool notify = true) override;

protected:
  ProcessWindows(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp);

  Status DoGetMemoryRegionInfo(lldb::addr_t vm_addr,
                               MemoryRegionInfo &info) override;

private:
  struct WatchpointInfo {
    uint32_t slot_id;
    lldb::addr_t address;
    uint32_t size;
    bool read;
    bool write;
  };
  std::map<lldb::break_id_t, WatchpointInfo> m_watchpoints;
  std::vector<lldb::break_id_t> m_watchpoint_ids;
};
} // namespace lldb_private

#endif // liblldb_Plugins_Process_Windows_Common_ProcessWindows_H_
