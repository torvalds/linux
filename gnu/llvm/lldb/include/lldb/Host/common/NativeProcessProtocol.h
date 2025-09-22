//===-- NativeProcessProtocol.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_COMMON_NATIVEPROCESSPROTOCOL_H
#define LLDB_HOST_COMMON_NATIVEPROCESSPROTOCOL_H

#include "NativeBreakpointList.h"
#include "NativeThreadProtocol.h"
#include "NativeWatchpointList.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/MainLoop.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/TraceGDBRemotePackets.h"
#include "lldb/Utility/UnimplementedError.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lldb_private {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class MemoryRegionInfo;
class ResumeActionList;

struct SVR4LibraryInfo {
  std::string name;
  lldb::addr_t link_map;
  lldb::addr_t base_addr;
  lldb::addr_t ld_addr;
  lldb::addr_t next;
};

// NativeProcessProtocol
class NativeProcessProtocol {
public:
  virtual ~NativeProcessProtocol() = default;

  typedef std::vector<std::unique_ptr<NativeThreadProtocol>> thread_collection;
  template <typename I>
  static NativeThreadProtocol &thread_list_adapter(I &iter) {
    assert(*iter);
    return **iter;
  }
  typedef LockingAdaptedIterable<thread_collection, NativeThreadProtocol &,
                                 thread_list_adapter, std::recursive_mutex>
      ThreadIterable;

  virtual Status Resume(const ResumeActionList &resume_actions) = 0;

  virtual Status Halt() = 0;

  virtual Status Detach() = 0;

  /// Sends a process a UNIX signal \a signal.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status Signal(int signo) = 0;

  /// Tells a process to interrupt all operations as if by a Ctrl-C.
  ///
  /// The default implementation will send a local host's equivalent of
  /// a SIGSTOP to the process via the NativeProcessProtocol::Signal()
  /// operation.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status Interrupt();

  virtual Status Kill() = 0;

  // Tells a process not to stop the inferior on given signals and just
  // reinject them back.
  virtual Status IgnoreSignals(llvm::ArrayRef<int> signals);

  // Memory and memory region functions

  virtual Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                                     MemoryRegionInfo &range_info);

  virtual Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                            size_t &bytes_read) = 0;

  Status ReadMemoryWithoutTrap(lldb::addr_t addr, void *buf, size_t size,
                               size_t &bytes_read);

  virtual Status ReadMemoryTags(int32_t type, lldb::addr_t addr, size_t len,
                                std::vector<uint8_t> &tags);

  virtual Status WriteMemoryTags(int32_t type, lldb::addr_t addr, size_t len,
                                 const std::vector<uint8_t> &tags);

  /// Reads a null terminated string from memory.
  ///
  /// Reads up to \p max_size bytes of memory until it finds a '\0'.
  /// If a '\0' is not found then it reads max_size-1 bytes as a string and a
  /// '\0' is added as the last character of the \p buffer.
  ///
  /// \param[in] addr
  ///     The address in memory to read from.
  ///
  /// \param[in] buffer
  ///     An allocated buffer with at least \p max_size size.
  ///
  /// \param[in] max_size
  ///     The maximum number of bytes to read from memory until it reads the
  ///     string.
  ///
  /// \param[out] total_bytes_read
  ///     The number of bytes read from memory into \p buffer.
  ///
  /// \return
  ///     Returns a StringRef backed up by the \p buffer passed in.
  llvm::Expected<llvm::StringRef>
  ReadCStringFromMemory(lldb::addr_t addr, char *buffer, size_t max_size,
                        size_t &total_bytes_read);

  virtual Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                             size_t &bytes_written) = 0;

  virtual llvm::Expected<lldb::addr_t> AllocateMemory(size_t size,
                                                      uint32_t permissions) {
    return llvm::make_error<UnimplementedError>();
  }

  virtual llvm::Error DeallocateMemory(lldb::addr_t addr) {
    return llvm::make_error<UnimplementedError>();
  }

  virtual lldb::addr_t GetSharedLibraryInfoAddress() = 0;

  virtual llvm::Expected<std::vector<SVR4LibraryInfo>>
  GetLoadedSVR4Libraries() {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Not implemented");
  }

  virtual bool IsAlive() const;

  virtual size_t UpdateThreads() = 0;

  virtual const ArchSpec &GetArchitecture() const = 0;

  // Breakpoint functions
  virtual Status SetBreakpoint(lldb::addr_t addr, uint32_t size,
                               bool hardware) = 0;

  virtual Status RemoveBreakpoint(lldb::addr_t addr, bool hardware = false);

  // Hardware Breakpoint functions
  virtual const HardwareBreakpointMap &GetHardwareBreakpointMap() const;

  virtual Status SetHardwareBreakpoint(lldb::addr_t addr, size_t size);

  virtual Status RemoveHardwareBreakpoint(lldb::addr_t addr);

  // Watchpoint functions
  virtual const NativeWatchpointList::WatchpointMap &GetWatchpointMap() const;

  virtual std::optional<std::pair<uint32_t, uint32_t>>
  GetHardwareDebugSupportInfo() const;

  virtual Status SetWatchpoint(lldb::addr_t addr, size_t size,
                               uint32_t watch_flags, bool hardware);

  virtual Status RemoveWatchpoint(lldb::addr_t addr);

  // Accessors
  lldb::pid_t GetID() const { return m_pid; }

  lldb::StateType GetState() const;

  bool IsRunning() const {
    return m_state == lldb::eStateRunning || IsStepping();
  }

  bool IsStepping() const { return m_state == lldb::eStateStepping; }

  bool CanResume() const { return m_state == lldb::eStateStopped; }

  lldb::ByteOrder GetByteOrder() const {
    return GetArchitecture().GetByteOrder();
  }

  uint32_t GetAddressByteSize() const {
    return GetArchitecture().GetAddressByteSize();
  }

  virtual llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  GetAuxvData() const = 0;

  // Exit Status
  virtual std::optional<WaitStatus> GetExitStatus();

  virtual bool SetExitStatus(WaitStatus status, bool bNotifyStateChange);

  // Access to threads
  NativeThreadProtocol *GetThreadAtIndex(uint32_t idx);

  NativeThreadProtocol *GetThreadByID(lldb::tid_t tid);

  void SetCurrentThreadID(lldb::tid_t tid) { m_current_thread_id = tid; }

  lldb::tid_t GetCurrentThreadID() const { return m_current_thread_id; }

  NativeThreadProtocol *GetCurrentThread() {
    return GetThreadByID(m_current_thread_id);
  }

  ThreadIterable Threads() const {
    return ThreadIterable(m_threads, m_threads_mutex);
  }

  // Access to inferior stdio
  virtual int GetTerminalFileDescriptor() { return m_terminal_fd; }

  // Stop id interface

  uint32_t GetStopID() const;

  // Callbacks for low-level process state changes
  class NativeDelegate {
  public:
    virtual ~NativeDelegate() = default;

    virtual void InitializeDelegate(NativeProcessProtocol *process) = 0;

    virtual void ProcessStateChanged(NativeProcessProtocol *process,
                                     lldb::StateType state) = 0;

    virtual void DidExec(NativeProcessProtocol *process) = 0;

    virtual void
    NewSubprocess(NativeProcessProtocol *parent_process,
                  std::unique_ptr<NativeProcessProtocol> child_process) = 0;
  };

  virtual Status GetLoadedModuleFileSpec(const char *module_path,
                                         FileSpec &file_spec) = 0;

  virtual Status GetFileLoadAddress(const llvm::StringRef &file_name,
                                    lldb::addr_t &load_addr) = 0;

  /// Extension flag constants, returned by Manager::GetSupportedExtensions()
  /// and passed to SetEnabledExtension()
  enum class Extension {
    multiprocess = (1u << 0),
    fork = (1u << 1),
    vfork = (1u << 2),
    pass_signals = (1u << 3),
    auxv = (1u << 4),
    libraries_svr4 = (1u << 5),
    memory_tagging = (1u << 6),
    savecore = (1u << 7),
    siginfo_read = (1u << 8),

    LLVM_MARK_AS_BITMASK_ENUM(siginfo_read)
  };

  class Manager {
  public:
    Manager(MainLoop &mainloop) : m_mainloop(mainloop) {}
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;

    virtual ~Manager();

    /// Launch a process for debugging.
    ///
    /// \param[in] launch_info
    ///     Information required to launch the process.
    ///
    /// \param[in] native_delegate
    ///     The delegate that will receive messages regarding the
    ///     inferior.  Must outlive the NativeProcessProtocol
    ///     instance.
    ///
    /// \param[in] mainloop
    ///     The mainloop instance with which the process can register
    ///     callbacks. Must outlive the NativeProcessProtocol
    ///     instance.
    ///
    /// \return
    ///     A NativeProcessProtocol shared pointer if the operation succeeded or
    ///     an error object if it failed.
    virtual llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Launch(ProcessLaunchInfo &launch_info,
           NativeDelegate &native_delegate) = 0;

    /// Attach to an existing process.
    ///
    /// \param[in] pid
    ///     pid of the process locatable
    ///
    /// \param[in] native_delegate
    ///     The delegate that will receive messages regarding the
    ///     inferior.  Must outlive the NativeProcessProtocol
    ///     instance.
    ///
    /// \param[in] mainloop
    ///     The mainloop instance with which the process can register
    ///     callbacks. Must outlive the NativeProcessProtocol
    ///     instance.
    ///
    /// \return
    ///     A NativeProcessProtocol shared pointer if the operation succeeded or
    ///     an error object if it failed.
    virtual llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Attach(lldb::pid_t pid, NativeDelegate &native_delegate) = 0;

    /// Get the bitmask of extensions supported by this process plugin.
    ///
    /// \return
    ///     A NativeProcessProtocol::Extension bitmask.
    virtual Extension GetSupportedExtensions() const { return {}; }

  protected:
    MainLoop &m_mainloop;
  };

  /// Notify tracers that the target process will resume
  virtual void NotifyTracersProcessWillResume() {}

  /// Notify tracers that the target process just stopped
  virtual void NotifyTracersProcessDidStop() {}

  /// Start tracing a process or its threads.
  ///
  /// \param[in] json_params
  ///     JSON object with the information of what and how to trace.
  ///     In the case of gdb-remote, this object should conform to the
  ///     jLLDBTraceStart packet.
  ///
  ///     This object should have a string entry called "type", which is the
  ///     tracing technology name.
  ///
  /// \param[in] type
  ///     Tracing technology type, as described in the \a json_params.
  ///
  /// \return
  ///     \a llvm::Error::success if the operation was successful, or an
  ///     \a llvm::Error otherwise.
  virtual llvm::Error TraceStart(llvm::StringRef json_params,
                                 llvm::StringRef type) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unsupported tracing type '%s'",
                                   type.data());
  }

  /// \copydoc Process::TraceStop(const TraceStopRequest &)
  virtual llvm::Error TraceStop(const TraceStopRequest &request) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unsupported tracing type '%s'",
                                   request.type.data());
  }

  /// \copydoc Process::TraceGetState(llvm::StringRef type)
  virtual llvm::Expected<llvm::json::Value>
  TraceGetState(llvm::StringRef type) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unsupported tracing type '%s'",
                                   type.data());
  }

  /// \copydoc Process::TraceGetBinaryData(const TraceGetBinaryDataRequest &)
  virtual llvm::Expected<std::vector<uint8_t>>
  TraceGetBinaryData(const TraceGetBinaryDataRequest &request) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Unsupported data kind '%s' for the '%s' tracing technology",
        request.kind.c_str(), request.type.c_str());
  }

  /// \copydoc Process::TraceSupported()
  virtual llvm::Expected<TraceSupportedResponse> TraceSupported() {
    return llvm::make_error<UnimplementedError>();
  }

  /// Method called in order to propagate the bitmap of protocol
  /// extensions supported by the client.
  ///
  /// \param[in] flags
  ///     The bitmap of enabled extensions.
  virtual void SetEnabledExtensions(Extension flags) {
    m_enabled_extensions = flags;
  }

  /// Write a core dump (without crashing the program).
  ///
  /// \param[in] path_hint
  ///     Suggested core dump path (optional, can be empty).
  ///
  /// \return
  ///     Path to the core dump if successfully written, an error
  ///     otherwise.
  virtual llvm::Expected<std::string> SaveCore(llvm::StringRef path_hint) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Not implemented");
  }

protected:
  struct SoftwareBreakpoint {
    uint32_t ref_count;
    llvm::SmallVector<uint8_t, 4> saved_opcodes;
    llvm::ArrayRef<uint8_t> breakpoint_opcodes;
  };

  std::unordered_map<lldb::addr_t, SoftwareBreakpoint> m_software_breakpoints;
  lldb::pid_t m_pid;

  std::vector<std::unique_ptr<NativeThreadProtocol>> m_threads;
  lldb::tid_t m_current_thread_id = LLDB_INVALID_THREAD_ID;
  mutable std::recursive_mutex m_threads_mutex;

  lldb::StateType m_state = lldb::eStateInvalid;
  mutable std::recursive_mutex m_state_mutex;

  std::optional<WaitStatus> m_exit_status;

  NativeDelegate &m_delegate;
  NativeWatchpointList m_watchpoint_list;
  HardwareBreakpointMap m_hw_breakpoints_map;
  int m_terminal_fd;
  uint32_t m_stop_id = 0;

  // Set of signal numbers that LLDB directly injects back to inferior without
  // stopping it.
  llvm::DenseSet<int> m_signals_to_ignore;

  // Extensions enabled per the last SetEnabledExtensions() call.
  Extension m_enabled_extensions;

  // lldb_private::Host calls should be used to launch a process for debugging,
  // and then the process should be attached to. When attaching to a process
  // lldb_private::Host calls should be used to locate the process to attach
  // to, and then this function should be called.
  NativeProcessProtocol(lldb::pid_t pid, int terminal_fd,
                        NativeDelegate &delegate);

  void SetID(lldb::pid_t pid) { m_pid = pid; }

  // interface for state handling
  void SetState(lldb::StateType state, bool notify_delegates = true);

  // Derived classes need not implement this.  It can be used as a hook to
  // clear internal caches that should be invalidated when stop ids change.
  //
  // Note this function is called with the state mutex obtained by the caller.
  virtual void DoStopIDBumped(uint32_t newBumpId);

  // interface for software breakpoints

  Status SetSoftwareBreakpoint(lldb::addr_t addr, uint32_t size_hint);
  Status RemoveSoftwareBreakpoint(lldb::addr_t addr);

  virtual llvm::Expected<llvm::ArrayRef<uint8_t>>
  GetSoftwareBreakpointTrapOpcode(size_t size_hint);

  /// Return the offset of the PC relative to the software breakpoint that was hit. If an
  /// architecture (e.g. arm) reports breakpoint hits before incrementing the PC, this offset
  /// will be 0. If an architecture (e.g. intel) reports breakpoints hits after incrementing the
  /// PC, this offset will be the size of the breakpoint opcode.
  virtual size_t GetSoftwareBreakpointPCOffset();

  // Adjust the thread's PC after hitting a software breakpoint. On
  // architectures where the PC points after the breakpoint instruction, this
  // resets it to point to the breakpoint itself.
  void FixupBreakpointPCAsNeeded(NativeThreadProtocol &thread);

  /// Notify the delegate that an exec occurred.
  ///
  /// Provide a mechanism for a delegate to clear out any exec-
  /// sensitive data.
  virtual void NotifyDidExec();

  NativeThreadProtocol *GetThreadByIDUnlocked(lldb::tid_t tid);

private:
  void SynchronouslyNotifyProcessStateChanged(lldb::StateType state);
  llvm::Expected<SoftwareBreakpoint>
  EnableSoftwareBreakpoint(lldb::addr_t addr, uint32_t size_hint);
};
} // namespace lldb_private

#endif // LLDB_HOST_COMMON_NATIVEPROCESSPROTOCOL_H
