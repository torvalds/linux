//===-- MachProcess.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/15/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHPROCESS_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHPROCESS_H

#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <optional>
#include <pthread.h>
#include <sys/signal.h>
#include <uuid/uuid.h>
#include <vector>

#include "DNBBreakpoint.h"
#include "DNBDefs.h"
#include "DNBError.h"
#include "DNBThreadResumeActions.h"
#include "Genealogy.h"
#include "JSONGenerator.h"
#include "MachException.h"
#include "MachTask.h"
#include "MachThreadList.h"
#include "MachVMMemory.h"
#include "PThreadCondition.h"
#include "PThreadEvent.h"
#include "PThreadMutex.h"
#include "RNBContext.h"
#include "ThreadInfo.h"

class DNBThreadResumeActions;

class MachProcess {
public:
  // Constructors and Destructors
  MachProcess();
  ~MachProcess();

  // A structure that can hold everything debugserver needs to know from
  // a binary's Mach-O header / load commands.

  struct mach_o_segment {
    std::string name;
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint64_t maxprot;
    uint64_t initprot;
    uint64_t nsects;
    uint64_t flags;
  };

  struct mach_o_information {
    struct mach_header_64 mach_header;
    std::vector<struct mach_o_segment> segments;
    uuid_t uuid;
    std::string min_version_os_name;
    std::string min_version_os_version;
  };

  struct binary_image_information {
    std::string filename;
    uint64_t load_address;
    struct mach_o_information macho_info;
    bool is_valid_mach_header;

    binary_image_information()
        : filename(), load_address(INVALID_NUB_ADDRESS),
          is_valid_mach_header(false) {}
  };

  // Child process control
  pid_t AttachForDebug(pid_t pid,
                       const RNBContext::IgnoredExceptions &ignored_exceptions,
                       char *err_str,
                       size_t err_len);
  pid_t LaunchForDebug(const char *path, char const *argv[], char const *envp[],
                       const char *working_directory, const char *stdin_path,
                       const char *stdout_path, const char *stderr_path,
                       bool no_stdio, nub_launch_flavor_t launch_flavor,
                       int disable_aslr, const char *event_data,
                       const RNBContext::IgnoredExceptions &ignored_exceptions,
                       DNBError &err);

  static uint32_t GetCPUTypeForLocalProcess(pid_t pid);
  static pid_t ForkChildForPTraceDebugging(const char *path, char const *argv[],
                                           char const *envp[],
                                           MachProcess *process, DNBError &err);
  static pid_t PosixSpawnChildForPTraceDebugging(
      const char *path, cpu_type_t cpu_type, cpu_subtype_t cpu_subtype,
      char const *argv[], char const *envp[], const char *working_directory,
      const char *stdin_path, const char *stdout_path, const char *stderr_path,
      bool no_stdio, MachProcess *process, int disable_aslr, DNBError &err);
  nub_addr_t GetDYLDAllImageInfosAddress();
  std::optional<std::pair<cpu_type_t, cpu_subtype_t>>
  GetMainBinaryCPUTypes(nub_process_t pid);
  static const void *PrepareForAttach(const char *path,
                                      nub_launch_flavor_t launch_flavor,
                                      bool waitfor, DNBError &err_str);
  static void CleanupAfterAttach(const void *attach_token,
                                 nub_launch_flavor_t launch_flavor,
                                 bool success, DNBError &err_str);
  static nub_process_t CheckForProcess(const void *attach_token,
                                       nub_launch_flavor_t launch_flavor);
#if defined(WITH_BKS) || defined(WITH_FBS)
  pid_t BoardServiceLaunchForDebug(const char *app_bundle_path,
                                   char const *argv[], char const *envp[],
                                   bool no_stdio, bool disable_aslr,
                                   const char *event_data,
                                   const RNBContext::IgnoredExceptions &ignored_exceptions,
                                   DNBError &launch_err);
  pid_t BoardServiceForkChildForPTraceDebugging(
      const char *path, char const *argv[], char const *envp[], bool no_stdio,
      bool disable_aslr, const char *event_data, DNBError &launch_err);
  bool BoardServiceSendEvent(const char *event, DNBError &error);
#endif
  static bool GetOSVersionNumbers(uint64_t *major, uint64_t *minor,
                                  uint64_t *patch);
  static std::string GetMacCatalystVersionString();

  static nub_process_t GetParentProcessID(nub_process_t child_pid);

  static bool ProcessIsBeingDebugged(nub_process_t pid);

#ifdef WITH_BKS
  static void BKSCleanupAfterAttach(const void *attach_token,
                                    DNBError &err_str);
#endif // WITH_BKS
#ifdef WITH_FBS
  static void FBSCleanupAfterAttach(const void *attach_token,
                                    DNBError &err_str);
#endif // WITH_FBS
#ifdef WITH_SPRINGBOARD
  pid_t SBLaunchForDebug(const char *app_bundle_path, char const *argv[],
                         char const *envp[], bool no_stdio, bool disable_aslr,
                         bool unmask_signals, DNBError &launch_err);
  static pid_t SBForkChildForPTraceDebugging(const char *path,
                                             char const *argv[],
                                             char const *envp[], bool no_stdio,
                                             MachProcess *process,
                                             DNBError &launch_err);
#endif // WITH_SPRINGBOARD
  nub_addr_t LookupSymbol(const char *name, const char *shlib);
  void SetNameToAddressCallback(DNBCallbackNameToAddress callback,
                                void *baton) {
    m_name_to_addr_callback = callback;
    m_name_to_addr_baton = baton;
  }
  void
  SetSharedLibraryInfoCallback(DNBCallbackCopyExecutableImageInfos callback,
                               void *baton) {
    m_image_infos_callback = callback;
    m_image_infos_baton = baton;
  }

  bool Resume(const DNBThreadResumeActions &thread_actions);
  bool Signal(int signal, const struct timespec *timeout_abstime = NULL);
  bool Interrupt();
  bool SendEvent(const char *event, DNBError &send_err);
  bool Kill(const struct timespec *timeout_abstime = NULL);
  bool Detach();
  nub_size_t ReadMemory(nub_addr_t addr, nub_size_t size, void *buf);
  nub_size_t WriteMemory(nub_addr_t addr, nub_size_t size, const void *buf);

  // Path and arg accessors
  const char *Path() const { return m_path.c_str(); }
  size_t ArgumentCount() const { return m_args.size(); }
  const char *ArgumentAtIndex(size_t arg_idx) const {
    if (arg_idx < m_args.size())
      return m_args[arg_idx].c_str();
    return NULL;
  }

  // Breakpoint functions
  DNBBreakpoint *CreateBreakpoint(nub_addr_t addr, nub_size_t length,
                                  bool hardware);
  bool DisableBreakpoint(nub_addr_t addr, bool remove);
  void DisableAllBreakpoints(bool remove);
  bool EnableBreakpoint(nub_addr_t addr);
  DNBBreakpointList &Breakpoints() { return m_breakpoints; }
  const DNBBreakpointList &Breakpoints() const { return m_breakpoints; }

  // Watchpoint functions
  DNBBreakpoint *CreateWatchpoint(nub_addr_t addr, nub_size_t length,
                                  uint32_t watch_type, bool hardware);
  bool DisableWatchpoint(nub_addr_t addr, bool remove);
  void DisableAllWatchpoints(bool remove);
  bool EnableWatchpoint(nub_addr_t addr);
  uint32_t GetNumSupportedHardwareWatchpoints() const;
  DNBBreakpointList &Watchpoints() { return m_watchpoints; }
  const DNBBreakpointList &Watchpoints() const { return m_watchpoints; }

  // Exception thread functions
  bool StartSTDIOThread();
  static void *STDIOThread(void *arg);
  void ExceptionMessageReceived(const MachException::Message &exceptionMessage);
  task_t ExceptionMessageBundleComplete();
  void SharedLibrariesUpdated();
  nub_size_t CopyImageInfos(struct DNBExecutableImageInfo **image_infos,
                            bool only_changed);

  // Profile functions
  void SetEnableAsyncProfiling(bool enable, uint64_t internal_usec,
                               DNBProfileDataScanType scan_type);
  bool IsProfilingEnabled() { return m_profile_enabled; }
  useconds_t ProfileInterval() { return m_profile_interval_usec; }
  bool StartProfileThread();
  static void *ProfileThread(void *arg);
  void SignalAsyncProfileData(const char *info);
  size_t GetAsyncProfileData(char *buf, size_t buf_size);

  // Accessors
  pid_t ProcessID() const { return m_pid; }
  bool ProcessIDIsValid() const { return m_pid > 0; }
  pid_t SetProcessID(pid_t pid);
  MachTask &Task() { return m_task; }
  const MachTask &Task() const { return m_task; }

  PThreadEvent &Events() { return m_events; }
  const DNBRegisterSetInfo *GetRegisterSetInfo(nub_thread_t tid,
                                               nub_size_t *num_reg_sets) const;
  bool GetRegisterValue(nub_thread_t tid, uint32_t set, uint32_t reg,
                        DNBRegisterValue *reg_value) const;
  bool SetRegisterValue(nub_thread_t tid, uint32_t set, uint32_t reg,
                        const DNBRegisterValue *value) const;
  nub_bool_t SyncThreadState(nub_thread_t tid);
  const char *ThreadGetName(nub_thread_t tid);
  nub_state_t ThreadGetState(nub_thread_t tid);
  ThreadInfo::QoS GetRequestedQoS(nub_thread_t tid, nub_addr_t tsd,
                                  uint64_t dti_qos_class_index);
  nub_addr_t GetPThreadT(nub_thread_t tid);
  nub_addr_t GetDispatchQueueT(nub_thread_t tid);
  nub_addr_t
  GetTSDAddressForThread(nub_thread_t tid,
                         uint64_t plo_pthread_tsd_base_address_offset,
                         uint64_t plo_pthread_tsd_base_offset,
                         uint64_t plo_pthread_tsd_entry_size);

  struct DeploymentInfo {
    DeploymentInfo() = default;
    operator bool() { return platform > 0; }
    /// The Mach-O platform type;
    unsigned char platform = 0;
    uint32_t major_version = 0;
    uint32_t minor_version = 0;
    uint32_t patch_version = 0;
  };
  DeploymentInfo GetDeploymentInfo(const struct load_command &,
                                   uint64_t load_command_address,
                                   bool is_executable);
  static std::optional<std::string> GetPlatformString(unsigned char platform);
  bool GetMachOInformationFromMemory(uint32_t platform,
                                     nub_addr_t mach_o_header_addr,
                                     int wordsize,
                                     struct mach_o_information &inf);
  JSONGenerator::ObjectSP FormatDynamicLibrariesIntoJSON(
      const std::vector<struct binary_image_information> &image_infos,
      bool report_load_commands);
  uint32_t GetPlatform();
  /// Get the runtime platform from DYLD via SPI.
  uint32_t GetProcessPlatformViaDYLDSPI();
  /// Use the dyld SPI present in macOS 10.12, iOS 10, tvOS 10,
  /// watchOS 3 and newer to get the load address, uuid, and filenames
  /// of all the libraries.  This only fills in those three fields in
  /// the 'struct binary_image_information' - call
  /// GetMachOInformationFromMemory to fill in the mach-o header/load
  /// command details.
  void GetAllLoadedBinariesViaDYLDSPI(
      std::vector<struct binary_image_information> &image_infos);
  JSONGenerator::ObjectSP
  GetLibrariesInfoForAddresses(nub_process_t pid,
                               std::vector<uint64_t> &macho_addresses);
  JSONGenerator::ObjectSP
  GetAllLoadedLibrariesInfos(nub_process_t pid,
                             bool fetch_report_load_commands);
  JSONGenerator::ObjectSP GetSharedCacheInfo(nub_process_t pid);

  nub_size_t GetNumThreads() const;
  nub_thread_t GetThreadAtIndex(nub_size_t thread_idx) const;
  nub_thread_t GetCurrentThread();
  nub_thread_t GetCurrentThreadMachPort();
  nub_thread_t SetCurrentThread(nub_thread_t tid);
  MachThreadList &GetThreadList() { return m_thread_list; }
  bool GetThreadStoppedReason(nub_thread_t tid,
                              struct DNBThreadStopInfo *stop_info);
  void DumpThreadStoppedReason(nub_thread_t tid) const;
  const char *GetThreadInfo(nub_thread_t tid) const;

  nub_thread_t GetThreadIDForMachPortNumber(thread_t mach_port_number) const;

  uint32_t GetCPUType();
  nub_state_t GetState();
  void SetState(nub_state_t state);
  bool IsRunning(nub_state_t state) {
    return state == eStateRunning || IsStepping(state);
  }
  bool IsStepping(nub_state_t state) { return state == eStateStepping; }
  bool CanResume(nub_state_t state) { return state == eStateStopped; }

  bool GetExitStatus(int *status) {
    if (GetState() == eStateExited) {
      if (status)
        *status = m_exit_status;
      return true;
    }
    return false;
  }
  void SetExitStatus(int status) {
    m_exit_status = status;
    SetState(eStateExited);
  }
  const char *GetExitInfo() { return m_exit_info.c_str(); }

  void SetExitInfo(const char *info);

  uint32_t StopCount() const { return m_stop_count; }
  void SetChildFileDescriptors(int stdin_fileno, int stdout_fileno,
                               int stderr_fileno) {
    m_child_stdin = stdin_fileno;
    m_child_stdout = stdout_fileno;
    m_child_stderr = stderr_fileno;
  }

  int GetStdinFileDescriptor() const { return m_child_stdin; }
  int GetStdoutFileDescriptor() const { return m_child_stdout; }
  int GetStderrFileDescriptor() const { return m_child_stderr; }
  void AppendSTDOUT(char *s, size_t len);
  size_t GetAvailableSTDOUT(char *buf, size_t buf_size);
  size_t GetAvailableSTDERR(char *buf, size_t buf_size);
  void CloseChildFileDescriptors() {
    if (m_child_stdin >= 0) {
      ::close(m_child_stdin);
      m_child_stdin = -1;
    }
    if (m_child_stdout >= 0) {
      ::close(m_child_stdout);
      m_child_stdout = -1;
    }
    if (m_child_stderr >= 0) {
      ::close(m_child_stderr);
      m_child_stderr = -1;
    }
  }

  void CalculateBoardStatus();

  bool ProcessUsingBackBoard();

  bool ProcessUsingFrontBoard();

  // Size of addresses in the inferior process (4 or 8).
  int GetInferiorAddrSize(pid_t pid);

  Genealogy::ThreadActivitySP GetGenealogyInfoForThread(nub_thread_t tid,
                                                        bool &timed_out);

  Genealogy::ProcessExecutableInfoSP GetGenealogyImageInfo(size_t idx);

  DNBProfileDataScanType GetProfileScanType() { return m_profile_scan_type; }

  JSONGenerator::ObjectSP GetDyldProcessState();

private:
  enum {
    eMachProcessFlagsNone = 0,
    eMachProcessFlagsAttached = (1 << 0),
    eMachProcessFlagsUsingBKS = (1 << 2), // only read via ProcessUsingBackBoard()
    eMachProcessFlagsUsingFBS = (1 << 3), // only read via ProcessUsingFrontBoard()
    eMachProcessFlagsBoardCalculated = (1 << 4)
  };

  enum {
    eMachProcessProfileNone = 0,
    eMachProcessProfileCancel = (1 << 0)
  };

  void Clear(bool detaching = false);
  void ReplyToAllExceptions();
  void PrivateResume();
  void StopProfileThread();

  void RefineWatchpointStopInfo(nub_thread_t tid,
                                struct DNBThreadStopInfo *stop_info);

  uint32_t Flags() const { return m_flags; }
  nub_state_t DoSIGSTOP(bool clear_bps_and_wps, bool allow_running,
                        uint32_t *thread_idx_ptr);

  pid_t m_pid;           // Process ID of child process
  cpu_type_t m_cpu_type; // The CPU type of this process
  uint32_t m_platform;   // The platform of this process
  int m_child_stdin;
  int m_child_stdout;
  int m_child_stderr;
  std::string m_path; // A path to the executable if we have one
  std::vector<std::string>
      m_args;              // The arguments with which the process was lauched
  int m_exit_status;       // The exit status for the process
  std::string m_exit_info; // Any extra info that we may have about the exit
  MachTask m_task;         // The mach task for this process
  uint32_t m_flags;      // Process specific flags (see eMachProcessFlags enums)
  uint32_t m_stop_count; // A count of many times have we stopped
  pthread_t m_stdio_thread;   // Thread ID for the thread that watches for child
                              // process stdio
  PThreadMutex m_stdio_mutex; // Multithreaded protection for stdio
  std::string m_stdout_data;

  bool m_profile_enabled; // A flag to indicate if profiling is enabled
  useconds_t m_profile_interval_usec; // If enable, the profiling interval in
                                      // microseconds
  DNBProfileDataScanType
      m_profile_scan_type; // Indicates what needs to be profiled
  pthread_t
      m_profile_thread; // Thread ID for the thread that profiles the inferior
  PThreadMutex
      m_profile_data_mutex; // Multithreaded protection for profile info data
  std::vector<std::string>
      m_profile_data; // Profile data, must be protected by m_profile_data_mutex
  PThreadEvent m_profile_events; // Used for the profile thread cancellable wait  
  DNBThreadResumeActions m_thread_actions; // The thread actions for the current
                                           // MachProcess::Resume() call
  MachException::Message::collection m_exception_messages; // A collection of
                                                           // exception messages
                                                           // caught when
                                                           // listening to the
                                                           // exception port
  PThreadMutex m_exception_messages_mutex; // Multithreaded protection for
                                           // m_exception_messages

  MachThreadList m_thread_list; // A list of threads that is maintained/updated
                                // after each stop
  Genealogy m_activities; // A list of activities that is updated after every
                          // stop lazily
  nub_state_t m_state;    // The state of our process
  PThreadMutex m_state_mutex; // Multithreaded protection for m_state
  PThreadEvent m_events;      // Process related events in the child processes
                              // lifetime can be waited upon
  PThreadEvent m_private_events; // Used to coordinate running and stopping the
                                 // process without affecting m_events
  DNBBreakpointList m_breakpoints; // Breakpoint list for this process
  DNBBreakpointList m_watchpoints; // Watchpoint list for this process
  DNBCallbackNameToAddress m_name_to_addr_callback;
  void *m_name_to_addr_baton;
  DNBCallbackCopyExecutableImageInfos m_image_infos_callback;
  void *m_image_infos_baton;
  std::string
      m_bundle_id; // If we are a SB or BKS process, this will be our bundle ID.
  int m_sent_interrupt_signo; // When we call MachProcess::Interrupt(), we want
                              // to send a single signal
  // to the inferior and only send the signal if we aren't already stopped.
  // If we end up sending a signal to stop the process we store it until we
  // receive an exception with this signal. This helps us to verify we got
  // the signal that interrupted the process. We might stop due to another
  // reason after an interrupt signal is sent, so this helps us ensure that
  // we don't report a spurious stop on the next resume.
  int m_auto_resume_signo; // If we resume the process and still haven't
                           // received our interrupt signal
  // acknowledgement, we will shortly after the next resume. We store the
  // interrupt signal in this variable so when we get the interrupt signal
  // as the sole reason for the process being stopped, we can auto resume
  // the process.
  bool m_did_exec;

  void *(*m_dyld_process_info_create)(task_t task, uint64_t timestamp,
                                      kern_return_t *kernelError);
  void (*m_dyld_process_info_for_each_image)(
      void *info, void (^callback)(uint64_t machHeaderAddress,
                                   const uuid_t uuid, const char *path));
  void (*m_dyld_process_info_release)(void *info);
  void (*m_dyld_process_info_get_cache)(void *info, void *cacheInfo);
  uint32_t (*m_dyld_process_info_get_platform)(void *info);
  void (*m_dyld_process_info_get_state)(void *info, void *stateInfo);
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHPROCESS_H
