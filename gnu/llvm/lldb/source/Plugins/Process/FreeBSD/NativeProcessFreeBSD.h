//===-- NativeProcessFreeBSD.h -------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeProcessFreeBSD_H_
#define liblldb_NativeProcessFreeBSD_H_

#include "Plugins/Process/POSIX/NativeProcessELF.h"
#include "Plugins/Process/Utility/NativeProcessSoftwareSingleStep.h"

#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"

#include "NativeThreadFreeBSD.h"

namespace lldb_private {
namespace process_freebsd {
/// \class NativeProcessFreeBSD
/// Manages communication with the inferior (debugee) process.
///
/// Upon construction, this class prepares and launches an inferior process
/// for debugging.
///
/// Changes in the inferior process state are broadcasted.
class NativeProcessFreeBSD : public NativeProcessELF,
                             private NativeProcessSoftwareSingleStep {
public:
  class Manager : public NativeProcessProtocol::Manager {
  public:
    using NativeProcessProtocol::Manager::Manager;

    llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Launch(ProcessLaunchInfo &launch_info,
           NativeDelegate &native_delegate) override;

    llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Attach(lldb::pid_t pid, NativeDelegate &native_delegate) override;

    Extension GetSupportedExtensions() const override;
  };

  // NativeProcessProtocol Interface
  Status Resume(const ResumeActionList &resume_actions) override;

  Status Halt() override;

  Status Detach() override;

  Status Signal(int signo) override;

  Status Interrupt() override;

  Status Kill() override;

  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info) override;

  Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    size_t &bytes_read) override;

  Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                     size_t &bytes_written) override;

  size_t UpdateThreads() override;

  const ArchSpec &GetArchitecture() const override { return m_arch; }

  Status SetBreakpoint(lldb::addr_t addr, uint32_t size,
                       bool hardware) override;

  // The two following methods are probably not necessary and probably
  // will never be called.  Nevertheless, we implement them right now
  // to reduce the differences between different platforms and reduce
  // the risk of the lack of implementation actually breaking something,
  // at least for the time being.
  Status GetLoadedModuleFileSpec(const char *module_path,
                                 FileSpec &file_spec) override;
  Status GetFileLoadAddress(const llvm::StringRef &file_name,
                            lldb::addr_t &load_addr) override;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  GetAuxvData() const override;

  // Interface used by NativeRegisterContext-derived classes.
  static Status PtraceWrapper(int req, lldb::pid_t pid, void *addr = nullptr,
                              int data = 0, int *result = nullptr);

  bool SupportHardwareSingleStepping() const;

  llvm::Expected<std::string> SaveCore(llvm::StringRef path_hint) override;

protected:
  llvm::Expected<llvm::ArrayRef<uint8_t>>
  GetSoftwareBreakpointTrapOpcode(size_t size_hint) override;

private:
  MainLoop::SignalHandleUP m_sigchld_handle;
  ArchSpec m_arch;
  MainLoop& m_main_loop;
  LazyBool m_supports_mem_region = eLazyBoolCalculate;
  std::vector<std::pair<MemoryRegionInfo, FileSpec>> m_mem_region_cache;

  // Private Instance Methods
  NativeProcessFreeBSD(::pid_t pid, int terminal_fd, NativeDelegate &delegate,
                       const ArchSpec &arch, MainLoop &mainloop);

  bool HasThreadNoLock(lldb::tid_t thread_id);

  NativeThreadFreeBSD &AddThread(lldb::tid_t thread_id);
  void RemoveThread(lldb::tid_t thread_id);

  void MonitorCallback(lldb::pid_t pid, int signal);
  void MonitorExited(lldb::pid_t pid, WaitStatus status);
  void MonitorSIGSTOP(lldb::pid_t pid);
  void MonitorSIGTRAP(lldb::pid_t pid);
  void MonitorSignal(lldb::pid_t pid, int signal);
  void MonitorClone(::pid_t child_pid, bool is_vfork,
                    NativeThreadFreeBSD &parent_thread);

  Status PopulateMemoryRegionCache();
  void SigchldHandler();

  Status Attach();
  Status SetupTrace();
  Status ReinitializeThreads();
};

} // namespace process_freebsd
} // namespace lldb_private

#endif // #ifndef liblldb_NativeProcessFreeBSD_H_
