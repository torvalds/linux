//===-- NativeProcessOpenBSD.h --------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeProcessOpenBSD_H_
#define liblldb_NativeProcessOpenBSD_H_

#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"

#include "NativeThreadOpenBSD.h"
#include "lldb/Host/common/NativeProcessProtocol.h"

namespace lldb_private {
namespace process_openbsd {
/// @class NativeProcessOpenBSD
/// Manages communication with the inferior (debugee) process.
///
/// Upon construction, this class prepares and launches an inferior process
/// for debugging.
///
/// Changes in the inferior process state are broadcasted.
class NativeProcessOpenBSD : public NativeProcessProtocol {
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

  // ---------------------------------------------------------------------
  // NativeProcessProtocol Interface
  // ---------------------------------------------------------------------
  Status Resume(const ResumeActionList &resume_actions) override;

  Status Halt() override;

  Status Detach() override;

  Status Signal(int signo) override;

  Status Kill() override;

  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info) override;

  Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    size_t &bytes_read) override;

  Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                     size_t &bytes_written) override;

  llvm::Expected<lldb::addr_t> AllocateMemory(size_t size, uint32_t permissions) override;

  llvm::Error DeallocateMemory(lldb::addr_t addr) override;

  lldb::addr_t GetSharedLibraryInfoAddress() override;

  size_t UpdateThreads() override;

  const ArchSpec &GetArchitecture() const override { return m_arch; }

  Status SetBreakpoint(lldb::addr_t addr, uint32_t size,
                       bool hardware) override;

  Status GetLoadedModuleFileSpec(const char *module_path,
                                 FileSpec &file_spec) override;

  Status GetFileLoadAddress(const llvm::StringRef &file_name,
                            lldb::addr_t &load_addr) override;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  GetAuxvData() const override;

  // ---------------------------------------------------------------------
  // Interface used by NativeRegisterContext-derived classes.
  // ---------------------------------------------------------------------
  static Status PtraceWrapper(int req, lldb::pid_t pid, void *addr = nullptr,
                              int data = 0, int *result = nullptr);

private:
  MainLoop::SignalHandleUP m_sigchld_handle;
  ArchSpec m_arch;
  MainLoop& m_main_loop;
  std::vector<std::pair<MemoryRegionInfo, FileSpec>> m_mem_region_cache;

  // ---------------------------------------------------------------------
  // Private Instance Methods
  // ---------------------------------------------------------------------
  NativeProcessOpenBSD(::pid_t pid, int terminal_fd, NativeDelegate &delegate,
                      const ArchSpec &arch, MainLoop &mainloop);

  bool HasThreadNoLock(lldb::tid_t thread_id);

  NativeThreadOpenBSD &AddThread(lldb::tid_t thread_id);

  void MonitorCallback(lldb::pid_t pid, int signal);
  void MonitorExited(lldb::pid_t pid, WaitStatus status);
  void MonitorSIGTRAP(lldb::pid_t pid);
  void MonitorSignal(lldb::pid_t pid, int signal);

  Status PopulateMemoryRegionCache();
  void SigchldHandler();

  Status Attach();
  Status ReinitializeThreads();
};

} // namespace process_openbsd
} // namespace lldb_private

#endif // #ifndef liblldb_NativeProcessOpenBSD_H_
