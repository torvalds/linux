//===-- ProcessKDP.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_PROCESSKDP_H
#define LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_PROCESSKDP_H

#include <list>
#include <vector>

#include "lldb/Core/ThreadSafeValue.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"

#include "CommunicationKDP.h"

class ThreadKDP;

class ProcessKDP : public lldb_private::Process {
public:
  // Constructors and Destructors
  static lldb::ProcessSP
  CreateInstance(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                 const lldb_private::FileSpec *crash_file_path,
                 bool can_connect);

  static void Initialize();

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "kdp-remote"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  // Constructors and Destructors
  ProcessKDP(lldb::TargetSP target_sp, lldb::ListenerSP listener);

  ~ProcessKDP() override;

  // Check if a given Process
  bool CanDebug(lldb::TargetSP target_sp,
                bool plugin_specified_by_name) override;
  lldb_private::CommandObject *GetPluginCommandObject() override;

  // Creating a new process, or attaching to an existing one
  lldb_private::Status DoWillLaunch(lldb_private::Module *module) override;

  lldb_private::Status
  DoLaunch(lldb_private::Module *exe_module,
           lldb_private::ProcessLaunchInfo &launch_info) override;

  lldb_private::Status DoWillAttachToProcessWithID(lldb::pid_t pid) override;

  lldb_private::Status
  DoWillAttachToProcessWithName(const char *process_name,
                                bool wait_for_launch) override;

  lldb_private::Status DoConnectRemote(llvm::StringRef remote_url) override;

  lldb_private::Status DoAttachToProcessWithID(
      lldb::pid_t pid,
      const lldb_private::ProcessAttachInfo &attach_info) override;

  lldb_private::Status DoAttachToProcessWithName(
      const char *process_name,
      const lldb_private::ProcessAttachInfo &attach_info) override;

  void DidAttach(lldb_private::ArchSpec &process_arch) override;

  lldb::addr_t GetImageInfoAddress() override;

  lldb_private::DynamicLoader *GetDynamicLoader() override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // Process Control
  lldb_private::Status WillResume() override;

  lldb_private::Status DoResume() override;

  lldb_private::Status DoHalt(bool &caused_stop) override;

  lldb_private::Status DoDetach(bool keep_stopped) override;

  lldb_private::Status DoSignal(int signal) override;

  lldb_private::Status DoDestroy() override;

  void RefreshStateAfterStop() override;

  // Process Queries
  bool IsAlive() override;

  // Process Memory
  size_t DoReadMemory(lldb::addr_t addr, void *buf, size_t size,
                      lldb_private::Status &error) override;

  size_t DoWriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                       lldb_private::Status &error) override;

  lldb::addr_t DoAllocateMemory(size_t size, uint32_t permissions,
                                lldb_private::Status &error) override;

  lldb_private::Status DoDeallocateMemory(lldb::addr_t ptr) override;

  // Process Breakpoints
  lldb_private::Status
  EnableBreakpointSite(lldb_private::BreakpointSite *bp_site) override;

  lldb_private::Status
  DisableBreakpointSite(lldb_private::BreakpointSite *bp_site) override;

  CommunicationKDP &GetCommunication() { return m_comm; }

protected:
  friend class ThreadKDP;
  friend class CommunicationKDP;

  // Accessors
  bool IsRunning(lldb::StateType state) {
    return state == lldb::eStateRunning || IsStepping(state);
  }

  bool IsStepping(lldb::StateType state) {
    return state == lldb::eStateStepping;
  }

  bool CanResume(lldb::StateType state) { return state == lldb::eStateStopped; }

  bool HasExited(lldb::StateType state) { return state == lldb::eStateExited; }

  bool GetHostArchitecture(lldb_private::ArchSpec &arch);

  bool ProcessIDIsValid() const;

  void Clear();

  bool DoUpdateThreadList(lldb_private::ThreadList &old_thread_list,
                          lldb_private::ThreadList &new_thread_list) override;

  enum {
    eBroadcastBitAsyncContinue = (1 << 0),
    eBroadcastBitAsyncThreadShouldExit = (1 << 1)
  };

  lldb::ThreadSP GetKernelThread();

  /// Broadcaster event bits definitions.
  CommunicationKDP m_comm;
  lldb_private::Broadcaster m_async_broadcaster;
  lldb_private::HostThread m_async_thread;
  llvm::StringRef m_dyld_plugin_name;
  lldb::addr_t m_kernel_load_addr;
  lldb::CommandObjectSP m_command_sp;
  lldb::ThreadWP m_kernel_thread_wp;

  bool StartAsyncThread();

  void StopAsyncThread();

  void *AsyncThread();

private:
  // For ProcessKDP only

  ProcessKDP(const ProcessKDP &) = delete;
  const ProcessKDP &operator=(const ProcessKDP &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_PROCESS_MACOSX_KERNEL_PROCESSKDP_H
