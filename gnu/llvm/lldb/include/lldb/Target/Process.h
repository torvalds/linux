//===-- Process.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_PROCESS_H
#define LLDB_TARGET_PROCESS_H

#include "lldb/Host/Config.h"

#include <climits>

#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Breakpoint/StopPointSiteList.h"
#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Core/LoadedModuleInfoList.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/ThreadSafeValue.h"
#include "lldb/Core/ThreadedCommunication.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Host/ProcessRunLock.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Target/Memory.h"
#include "lldb/Target/MemoryTagManager.h"
#include "lldb/Target/QueueList.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Target/ThreadPlanStack.h"
#include "lldb/Target/Trace.h"
#include "lldb/Utility/AddressableBits.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/TraceGDBRemotePackets.h"
#include "lldb/Utility/UnimplementedError.h"
#include "lldb/Utility/UserIDResolver.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/AddressRanges.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VersionTuple.h"

namespace lldb_private {

template <typename B, typename S> struct Range;

class ProcessExperimentalProperties : public Properties {
public:
  ProcessExperimentalProperties();
};

class ProcessProperties : public Properties {
public:
  // Pass nullptr for "process" if the ProcessProperties are to be the global
  // copy
  ProcessProperties(lldb_private::Process *process);

  ~ProcessProperties() override;

  bool GetDisableMemoryCache() const;
  uint64_t GetMemoryCacheLineSize() const;
  Args GetExtraStartupCommands() const;
  void SetExtraStartupCommands(const Args &args);
  FileSpec GetPythonOSPluginPath() const;
  uint32_t GetVirtualAddressableBits() const;
  void SetVirtualAddressableBits(uint32_t bits);
  uint32_t GetHighmemVirtualAddressableBits() const;
  void SetHighmemVirtualAddressableBits(uint32_t bits);
  void SetPythonOSPluginPath(const FileSpec &file);
  bool GetIgnoreBreakpointsInExpressions() const;
  void SetIgnoreBreakpointsInExpressions(bool ignore);
  bool GetUnwindOnErrorInExpressions() const;
  void SetUnwindOnErrorInExpressions(bool ignore);
  bool GetStopOnSharedLibraryEvents() const;
  void SetStopOnSharedLibraryEvents(bool stop);
  bool GetDisableLangRuntimeUnwindPlans() const;
  void SetDisableLangRuntimeUnwindPlans(bool disable);
  bool GetDetachKeepsStopped() const;
  void SetDetachKeepsStopped(bool keep_stopped);
  bool GetWarningsOptimization() const;
  bool GetWarningsUnsupportedLanguage() const;
  bool GetStopOnExec() const;
  std::chrono::seconds GetUtilityExpressionTimeout() const;
  std::chrono::seconds GetInterruptTimeout() const;
  bool GetOSPluginReportsAllThreads() const;
  void SetOSPluginReportsAllThreads(bool does_report);
  bool GetSteppingRunsAllThreads() const;
  FollowForkMode GetFollowForkMode() const;

protected:
  Process *m_process; // Can be nullptr for global ProcessProperties
  std::unique_ptr<ProcessExperimentalProperties> m_experimental_properties_up;
};

// ProcessAttachInfo
//
// Describes any information that is required to attach to a process.

class ProcessAttachInfo : public ProcessInstanceInfo {
public:
  ProcessAttachInfo() = default;

  ProcessAttachInfo(const ProcessLaunchInfo &launch_info)
      : m_resume_count(0), m_wait_for_launch(false), m_ignore_existing(true),
        m_continue_once_attached(false), m_detach_on_error(true),
        m_async(false) {
    ProcessInfo::operator=(launch_info);
    SetProcessPluginName(launch_info.GetProcessPluginName());
    SetResumeCount(launch_info.GetResumeCount());
    m_detach_on_error = launch_info.GetDetachOnError();
  }

  bool GetWaitForLaunch() const { return m_wait_for_launch; }

  void SetWaitForLaunch(bool b) { m_wait_for_launch = b; }

  bool GetAsync() const { return m_async; }

  void SetAsync(bool b) { m_async = b; }

  bool GetIgnoreExisting() const { return m_ignore_existing; }

  void SetIgnoreExisting(bool b) { m_ignore_existing = b; }

  bool GetContinueOnceAttached() const { return m_continue_once_attached; }

  void SetContinueOnceAttached(bool b) { m_continue_once_attached = b; }

  uint32_t GetResumeCount() const { return m_resume_count; }

  void SetResumeCount(uint32_t c) { m_resume_count = c; }

  llvm::StringRef GetProcessPluginName() const {
    return llvm::StringRef(m_plugin_name);
  }

  void SetProcessPluginName(llvm::StringRef plugin) {
    m_plugin_name = std::string(plugin);
  }

  void Clear() {
    ProcessInstanceInfo::Clear();
    m_plugin_name.clear();
    m_resume_count = 0;
    m_wait_for_launch = false;
    m_ignore_existing = true;
    m_continue_once_attached = false;
  }

  bool ProcessInfoSpecified() const {
    if (GetExecutableFile())
      return true;
    if (GetProcessID() != LLDB_INVALID_PROCESS_ID)
      return true;
    if (GetParentProcessID() != LLDB_INVALID_PROCESS_ID)
      return true;
    return false;
  }

  bool GetDetachOnError() const { return m_detach_on_error; }

  void SetDetachOnError(bool enable) { m_detach_on_error = enable; }

  lldb::ListenerSP GetListenerForProcess(Debugger &debugger);

protected:
  std::string m_plugin_name;
  uint32_t m_resume_count = 0; // How many times do we resume after launching
  bool m_wait_for_launch = false;
  bool m_ignore_existing = true;
  bool m_continue_once_attached = false; // Supports the use-case scenario of
                                         // immediately continuing the process
                                         // once attached.
  bool m_detach_on_error =
      true; // If we are debugging remotely, instruct the stub to
            // detach rather than killing the target on error.
  bool m_async =
      false; // Use an async attach where we start the attach and return
             // immediately (used by GUI programs with --waitfor so they can
             // call SBProcess::Stop() to cancel attach)
};

// This class tracks the Modification state of the process.  Things that can
// currently modify the program are running the program (which will up the
// StopID) and writing memory (which will up the MemoryID.)
// FIXME: Should we also include modification of register states?

class ProcessModID {
  friend bool operator==(const ProcessModID &lhs, const ProcessModID &rhs);

public:
  ProcessModID() = default;

  ProcessModID(const ProcessModID &rhs)
      : m_stop_id(rhs.m_stop_id), m_memory_id(rhs.m_memory_id) {}

  const ProcessModID &operator=(const ProcessModID &rhs) {
    if (this != &rhs) {
      m_stop_id = rhs.m_stop_id;
      m_memory_id = rhs.m_memory_id;
    }
    return *this;
  }

  ~ProcessModID() = default;

  uint32_t BumpStopID() {
    const uint32_t prev_stop_id = m_stop_id++;
    if (!IsLastResumeForUserExpression())
      m_last_natural_stop_id++;
    return prev_stop_id;
  }

  void BumpMemoryID() { m_memory_id++; }

  void BumpResumeID() {
    m_resume_id++;
    if (m_running_user_expression > 0)
      m_last_user_expression_resume = m_resume_id;
  }

  bool IsRunningUtilityFunction() const {
    return m_running_utility_function > 0;
  }

  uint32_t GetStopID() const { return m_stop_id; }
  uint32_t GetLastNaturalStopID() const { return m_last_natural_stop_id; }
  uint32_t GetMemoryID() const { return m_memory_id; }
  uint32_t GetResumeID() const { return m_resume_id; }
  uint32_t GetLastUserExpressionResumeID() const {
    return m_last_user_expression_resume;
  }

  bool MemoryIDEqual(const ProcessModID &compare) const {
    return m_memory_id == compare.m_memory_id;
  }

  bool StopIDEqual(const ProcessModID &compare) const {
    return m_stop_id == compare.m_stop_id;
  }

  void SetInvalid() { m_stop_id = UINT32_MAX; }

  bool IsValid() const { return m_stop_id != UINT32_MAX; }

  bool IsLastResumeForUserExpression() const {
    // If we haven't yet resumed the target, then it can't be for a user
    // expression...
    if (m_resume_id == 0)
      return false;

    return m_resume_id == m_last_user_expression_resume;
  }

  bool IsRunningExpression() const {
    // Don't return true if we are no longer running an expression:
    if (m_running_user_expression || m_running_utility_function)
      return true;
    return false;
  }

  void SetRunningUserExpression(bool on) {
    if (on)
      m_running_user_expression++;
    else
      m_running_user_expression--;
  }

  void SetRunningUtilityFunction(bool on) {
    if (on)
      m_running_utility_function++;
    else {
      assert(m_running_utility_function > 0 &&
             "Called SetRunningUtilityFunction(false) without calling "
             "SetRunningUtilityFunction(true) before?");
      m_running_utility_function--;
    }
  }

  void SetStopEventForLastNaturalStopID(lldb::EventSP event_sp) {
    m_last_natural_stop_event = std::move(event_sp);
  }

  lldb::EventSP GetStopEventForStopID(uint32_t stop_id) const {
    if (stop_id == m_last_natural_stop_id)
      return m_last_natural_stop_event;
    return lldb::EventSP();
  }

private:
  uint32_t m_stop_id = 0;
  uint32_t m_last_natural_stop_id = 0;
  uint32_t m_resume_id = 0;
  uint32_t m_memory_id = 0;
  uint32_t m_last_user_expression_resume = 0;
  uint32_t m_running_user_expression = false;
  uint32_t m_running_utility_function = 0;
  lldb::EventSP m_last_natural_stop_event;
};

inline bool operator==(const ProcessModID &lhs, const ProcessModID &rhs) {
  if (lhs.StopIDEqual(rhs) && lhs.MemoryIDEqual(rhs))
    return true;
  else
    return false;
}

inline bool operator!=(const ProcessModID &lhs, const ProcessModID &rhs) {
  return (!lhs.StopIDEqual(rhs) || !lhs.MemoryIDEqual(rhs));
}

/// \class Process Process.h "lldb/Target/Process.h"
/// A plug-in interface definition class for debugging a process.
class Process : public std::enable_shared_from_this<Process>,
                public ProcessProperties,
                public Broadcaster,
                public ExecutionContextScope,
                public PluginInterface {
  friend class FunctionCaller; // For WaitForStateChangeEventsPrivate
  friend class Debugger; // For PopProcessIOHandler and ProcessIOHandlerIsActive
  friend class DynamicLoader; // For LoadOperatingSystemPlugin
  friend class ProcessEventData;
  friend class StopInfo;
  friend class Target;
  friend class ThreadList;

public:
  /// Broadcaster event bits definitions.
  enum {
    eBroadcastBitStateChanged = (1 << 0),
    eBroadcastBitInterrupt = (1 << 1),
    eBroadcastBitSTDOUT = (1 << 2),
    eBroadcastBitSTDERR = (1 << 3),
    eBroadcastBitProfileData = (1 << 4),
    eBroadcastBitStructuredData = (1 << 5),
  };
  // This is all the event bits the public process broadcaster broadcasts.
  // The process shadow listener signs up for all these bits...
  static constexpr int g_all_event_bits =
      eBroadcastBitStateChanged | eBroadcastBitInterrupt | eBroadcastBitSTDOUT |
      eBroadcastBitSTDERR | eBroadcastBitProfileData |
      eBroadcastBitStructuredData;

  enum {
    eBroadcastInternalStateControlStop = (1 << 0),
    eBroadcastInternalStateControlPause = (1 << 1),
    eBroadcastInternalStateControlResume = (1 << 2)
  };

  typedef Range<lldb::addr_t, lldb::addr_t> LoadRange;
  // We use a read/write lock to allow on or more clients to access the process
  // state while the process is stopped (reader). We lock the write lock to
  // control access to the process while it is running (readers, or clients
  // that want the process stopped can block waiting for the process to stop,
  // or just try to lock it to see if they can immediately access the stopped
  // process. If the try read lock fails, then the process is running.
  typedef ProcessRunLock::ProcessRunLocker StopLocker;

  // These two functions fill out the Broadcaster interface:

  static llvm::StringRef GetStaticBroadcasterClass();

  static constexpr llvm::StringRef AttachSynchronousHijackListenerName =
      "lldb.internal.Process.AttachSynchronous.hijack";
  static constexpr llvm::StringRef LaunchSynchronousHijackListenerName =
      "lldb.internal.Process.LaunchSynchronous.hijack";
  static constexpr llvm::StringRef ResumeSynchronousHijackListenerName =
      "lldb.internal.Process.ResumeSynchronous.hijack";

  llvm::StringRef GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

/// A notification structure that can be used by clients to listen
/// for changes in a process's lifetime.
///
/// \see RegisterNotificationCallbacks (const Notifications&) @see
/// UnregisterNotificationCallbacks (const Notifications&)
  typedef struct {
    void *baton;
    void (*initialize)(void *baton, Process *process);
    void (*process_state_changed)(void *baton, Process *process,
                                  lldb::StateType state);
  } Notifications;

  class ProcessEventData : public EventData {
    friend class Process;

  public:
    ProcessEventData();
    ProcessEventData(const lldb::ProcessSP &process, lldb::StateType state);

    ~ProcessEventData() override;

    static llvm::StringRef GetFlavorString();

    llvm::StringRef GetFlavor() const override;

    lldb::ProcessSP GetProcessSP() const { return m_process_wp.lock(); }

    lldb::StateType GetState() const { return m_state; }
    bool GetRestarted() const { return m_restarted; }

    size_t GetNumRestartedReasons() { return m_restarted_reasons.size(); }

    const char *GetRestartedReasonAtIndex(size_t idx) {
      return ((idx < m_restarted_reasons.size())
                  ? m_restarted_reasons[idx].c_str()
                  : nullptr);
    }

    bool GetInterrupted() const { return m_interrupted; }

    void Dump(Stream *s) const override;

    virtual bool ShouldStop(Event *event_ptr, bool &found_valid_stopinfo);

    void DoOnRemoval(Event *event_ptr) override;

    static const Process::ProcessEventData *
    GetEventDataFromEvent(const Event *event_ptr);

    static lldb::ProcessSP GetProcessFromEvent(const Event *event_ptr);

    static lldb::StateType GetStateFromEvent(const Event *event_ptr);

    static bool GetRestartedFromEvent(const Event *event_ptr);

    static size_t GetNumRestartedReasons(const Event *event_ptr);

    static const char *GetRestartedReasonAtIndex(const Event *event_ptr,
                                                 size_t idx);

    static void AddRestartedReason(Event *event_ptr, const char *reason);

    static void SetRestartedInEvent(Event *event_ptr, bool new_value);

    static bool GetInterruptedFromEvent(const Event *event_ptr);

    static void SetInterruptedInEvent(Event *event_ptr, bool new_value);

    static bool SetUpdateStateOnRemoval(Event *event_ptr);

  private:
    bool ForwardEventToPendingListeners(Event *event_ptr) override;

    void SetUpdateStateOnRemoval() { m_update_state++; }

    void SetRestarted(bool new_value) { m_restarted = new_value; }

    void SetInterrupted(bool new_value) { m_interrupted = new_value; }

    void AddRestartedReason(const char *reason) {
      m_restarted_reasons.push_back(reason);
    }

    lldb::ProcessWP m_process_wp;
    lldb::StateType m_state = lldb::eStateInvalid;
    std::vector<std::string> m_restarted_reasons;
    bool m_restarted = false; // For "eStateStopped" events, this is true if the
                              // target was automatically restarted.
    int m_update_state = 0;
    bool m_interrupted = false;

    ProcessEventData(const ProcessEventData &) = delete;
    const ProcessEventData &operator=(const ProcessEventData &) = delete;
  };

  /// Destructor.
  ///
  /// The destructor is virtual since this class is designed to be inherited
  /// from by the plug-in instance.
  ~Process() override;

  static void SettingsInitialize();

  static void SettingsTerminate();

  static ProcessProperties &GetGlobalProperties();

  /// Find a Process plug-in that can debug \a module using the currently
  /// selected architecture.
  ///
  /// Scans all loaded plug-in interfaces that implement versions of the
  /// Process plug-in interface and returns the first instance that can debug
  /// the file.
  ///
  /// \see Process::CanDebug ()
  static lldb::ProcessSP FindPlugin(lldb::TargetSP target_sp,
                                    llvm::StringRef plugin_name,
                                    lldb::ListenerSP listener_sp,
                                    const FileSpec *crash_file_path,
                                    bool can_connect);

  /// Static function that can be used with the \b host function
  /// Host::StartMonitoringChildProcess ().
  ///
  /// This function can be used by lldb_private::Process subclasses when they
  /// want to watch for a local process and have its exit status automatically
  /// set when the host child process exits. Subclasses should call
  /// Host::StartMonitoringChildProcess () with:
  ///     callback = Process::SetHostProcessExitStatus
  ///     pid = Process::GetID()
  ///     monitor_signals = false
  static bool
  SetProcessExitStatus(lldb::pid_t pid, // The process ID we want to monitor
                       bool exited,
                       int signo,   // Zero for no signal
                       int status); // Exit value of process if signal is zero

  lldb::ByteOrder GetByteOrder() const;

  uint32_t GetAddressByteSize() const;

  /// Returns the pid of the process or LLDB_INVALID_PROCESS_ID if there is
  /// no known pid.
  lldb::pid_t GetID() const { return m_pid; }

  /// Sets the stored pid.
  ///
  /// This does not change the pid of underlying process.
  void SetID(lldb::pid_t new_pid) { m_pid = new_pid; }

  uint32_t GetUniqueID() const { return m_process_unique_id; }

  /// Check if a plug-in instance can debug the file in \a module.
  ///
  /// Each plug-in is given a chance to say whether it can debug the file in
  /// \a module. If the Process plug-in instance can debug a file on the
  /// current system, it should return \b true.
  ///
  /// \return
  ///     Returns \b true if this Process plug-in instance can
  ///     debug the executable, \b false otherwise.
  virtual bool CanDebug(lldb::TargetSP target,
                        bool plugin_specified_by_name) = 0;

  /// This object is about to be destroyed, do any necessary cleanup.
  ///
  /// Subclasses that override this method should always call this superclass
  /// method.
  /// If you are running Finalize in your Process subclass Destructor, pass
  /// \b true.  If we are in the destructor, shared_from_this will no longer
  /// work, so we have to avoid doing anything that might trigger that.
  virtual void Finalize(bool destructing);

  /// Return whether this object is valid (i.e. has not been finalized.)
  ///
  /// \return
  ///     Returns \b true if this Process has not been finalized
  ///     and \b false otherwise.
  bool IsValid() const { return !m_finalizing; }

  /// Return a multi-word command object that can be used to expose plug-in
  /// specific commands.
  ///
  /// This object will be used to resolve plug-in commands and can be
  /// triggered by a call to:
  ///
  ///     (lldb) process command <args>
  ///
  /// \return
  ///     A CommandObject which can be one of the concrete subclasses
  ///     of CommandObject like CommandObjectRaw, CommandObjectParsed,
  ///     or CommandObjectMultiword.
  virtual CommandObject *GetPluginCommandObject() { return nullptr; }

  /// The underlying plugin might store the low-level communication history for
  /// this session.  Dump it into the provided stream.
  virtual void DumpPluginHistory(Stream &s) { return; }

  /// Launch a new process.
  ///
  /// Launch a new process by spawning a new process using the target object's
  /// executable module's file as the file to launch.
  ///
  /// This function is not meant to be overridden by Process subclasses. It
  /// will first call Process::WillLaunch (Module *) and if that returns \b
  /// true, Process::DoLaunch (Module*, char const *[],char const *[],const
  /// char *,const char *, const char *) will be called to actually do the
  /// launching. If DoLaunch returns \b true, then Process::DidLaunch() will
  /// be called.
  ///
  /// \param[in] launch_info
  ///     Details regarding the environment, STDIN/STDOUT/STDERR
  ///     redirection, working path, etc. related to the requested launch.
  ///
  /// \return
  ///     An error object. Call GetID() to get the process ID if
  ///     the error object is success.
  virtual Status Launch(ProcessLaunchInfo &launch_info);

  virtual Status LoadCore();

  virtual Status DoLoadCore() {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support loading core files.", GetPluginName());
    return error;
  }

  /// The "ShadowListener" for a process is just an ordinary Listener that
  /// listens for all the Process event bits.  It's convenient because you can
  /// specify it in the LaunchInfo or AttachInfo, so it will get events from
  /// the very start of the process.
  void SetShadowListener(lldb::ListenerSP shadow_listener_sp) {
    if (shadow_listener_sp)
      AddListener(shadow_listener_sp, g_all_event_bits);
  }

  // FUTURE WORK: GetLoadImageUtilityFunction are the first use we've
  // had of having other plugins cache data in the Process.  This is handy for
  // long-living plugins - like the Platform - which manage interactions whose
  // lifetime is governed by the Process lifetime.  If we find we need to do
  // this more often, we should construct a general solution to the problem.
  // The consensus suggestion was that we have a token based registry in the
  // Process. Some undecided questions are  (1) who manages the tokens.  It's
  // probably best that you add the element  and get back a token that
  // represents it.  That will avoid collisions.  But there may be some utility
  // in the registerer controlling the token? (2) whether the thing added
  // should be simply owned by Process, and just go away when it does (3)
  // whether the registree should be notified of the Process' demise.
  //
  // We are postponing designing this till we have at least a second use case.
  /// Get the cached UtilityFunction that assists in loading binary images
  /// into the process.
  ///
  /// \param[in] platform
  ///     The platform fetching the UtilityFunction.
  /// \param[in] factory
  ///     A function that will be called only once per-process in a
  ///     thread-safe way to create the UtilityFunction if it has not
  ///     been initialized yet.
  ///
  /// \return
  ///     The cached utility function or null if the platform is not the
  ///     same as the target's platform.
  UtilityFunction *GetLoadImageUtilityFunction(
      Platform *platform,
      llvm::function_ref<std::unique_ptr<UtilityFunction>()> factory);

  /// Get the dynamic loader plug-in for this process.
  ///
  /// The default action is to let the DynamicLoader plug-ins check the main
  /// executable and the DynamicLoader will select itself automatically.
  /// Subclasses can override this if inspecting the executable is not
  /// desired, or if Process subclasses can only use a specific DynamicLoader
  /// plug-in.
  virtual DynamicLoader *GetDynamicLoader();

  void SetDynamicLoader(lldb::DynamicLoaderUP dyld);

  // Returns AUXV structure found in many ELF-based environments.
  //
  // The default action is to return an empty data buffer.
  //
  // \return
  //    A data extractor containing the contents of the AUXV data.
  virtual DataExtractor GetAuxvData();

  /// Sometimes processes know how to retrieve and load shared libraries. This
  /// is normally done by DynamicLoader plug-ins, but sometimes the connection
  /// to the process allows retrieving this information. The dynamic loader
  /// plug-ins can use this function if they can't determine the current
  /// shared library load state.
  ///
  /// \return
  ///    A status object indicating if the operation was sucessful or not.
  virtual llvm::Error LoadModules() {
    return llvm::make_error<llvm::StringError>("Not implemented.",
                                               llvm::inconvertibleErrorCode());
  }

  /// Query remote GDBServer for a detailed loaded library list
  /// \return
  ///    The list of modules currently loaded by the process, or an error.
  virtual llvm::Expected<LoadedModuleInfoList> GetLoadedModuleList() {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Not implemented");
  }

  /// Save core dump into the specified file.
  ///
  /// \param[in] outfile
  ///     Path to store core dump in.
  ///
  /// \return
  ///     true if saved successfully, false if saving the core dump
  ///     is not supported by the plugin, error otherwise.
  virtual llvm::Expected<bool> SaveCore(llvm::StringRef outfile);

  struct CoreFileMemoryRange {
    llvm::AddressRange range;  /// The address range to save into the core file.
    uint32_t lldb_permissions; /// A bit set of lldb::Permissions bits.

    bool operator==(const CoreFileMemoryRange &rhs) const {
      return range == rhs.range && lldb_permissions == rhs.lldb_permissions;
    }

    bool operator!=(const CoreFileMemoryRange &rhs) const {
      return !(*this == rhs);
    }

    bool operator<(const CoreFileMemoryRange &rhs) const {
      if (range < rhs.range)
        return true;
      if (range == rhs.range)
        return lldb_permissions < rhs.lldb_permissions;
      return false;
    }
  };

  using CoreFileMemoryRanges = std::vector<CoreFileMemoryRange>;

  /// Helper function for Process::SaveCore(...) that calculates the address
  /// ranges that should be saved. This allows all core file plug-ins to save
  /// consistent memory ranges given a \a core_style.
  Status CalculateCoreFileSaveRanges(lldb::SaveCoreStyle core_style,
                                     CoreFileMemoryRanges &ranges);

protected:
  virtual JITLoaderList &GetJITLoaders();

public:
  /// Get the system architecture for this process.
  virtual ArchSpec GetSystemArchitecture() { return {}; }

  /// Get the system runtime plug-in for this process.
  ///
  /// \return
  ///   Returns a pointer to the SystemRuntime plugin for this Process
  ///   if one is available.  Else returns nullptr.
  virtual SystemRuntime *GetSystemRuntime();

  /// Attach to an existing process using the process attach info.
  ///
  /// This function is not meant to be overridden by Process subclasses. It
  /// will first call WillAttach (lldb::pid_t) or WillAttach (const char *),
  /// and if that returns \b true, DoAttach (lldb::pid_t) or DoAttach (const
  /// char *) will be called to actually do the attach. If DoAttach returns \b
  /// true, then Process::DidAttach() will be called.
  ///
  /// \param[in] attach_info
  ///     The process attach info.
  ///
  /// \return
  ///     Returns \a pid if attaching was successful, or
  ///     LLDB_INVALID_PROCESS_ID if attaching fails.
  virtual Status Attach(ProcessAttachInfo &attach_info);

  /// Attach to a remote system via a URL
  ///
  /// \param[in] remote_url
  ///     The URL format that we are connecting to.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status ConnectRemote(llvm::StringRef remote_url);

  bool GetShouldDetach() const { return m_should_detach; }

  void SetShouldDetach(bool b) { m_should_detach = b; }

  /// Get the image vector for the current process.
  ///
  /// \return
  ///     The constant reference to the member m_image_tokens.
  const std::vector<lldb::addr_t>& GetImageTokens() { return m_image_tokens; }

  /// Get the image information address for the current process.
  ///
  /// Some runtimes have system functions that can help dynamic loaders locate
  /// the dynamic loader information needed to observe shared libraries being
  /// loaded or unloaded. This function is in the Process interface (as
  /// opposed to the DynamicLoader interface) to ensure that remote debugging
  /// can take advantage of this functionality.
  ///
  /// \return
  ///     The address of the dynamic loader information, or
  ///     LLDB_INVALID_ADDRESS if this is not supported by this
  ///     interface.
  virtual lldb::addr_t GetImageInfoAddress();

  /// Called when the process is about to broadcast a public stop.
  ///
  /// There are public and private stops. Private stops are when the process
  /// is doing things like stepping and the client doesn't need to know about
  /// starts and stop that implement a thread plan. Single stepping over a
  /// source line in code might end up being implemented by one or more
  /// process starts and stops. Public stops are when clients will be notified
  /// that the process is stopped. These events typically trigger UI updates
  /// (thread stack frames to be displayed, variables to be displayed, and
  /// more). This function can be overriden and allows process subclasses to
  /// do something before the eBroadcastBitStateChanged event is sent to
  /// public clients.
  virtual void WillPublicStop() {}

/// Register for process and thread notifications.
///
/// Clients can register notification callbacks by filling out a
/// Process::Notifications structure and calling this function.
///
/// \param[in] callbacks
///     A structure that contains the notification baton and
///     callback functions.
///
/// \see Process::Notifications
  void RegisterNotificationCallbacks(const Process::Notifications &callbacks);

/// Unregister for process and thread notifications.
///
/// Clients can unregister notification callbacks by passing a copy of the
/// original baton and callbacks in \a callbacks.
///
/// \param[in] callbacks
///     A structure that contains the notification baton and
///     callback functions.
///
/// \return
///     Returns \b true if the notification callbacks were
///     successfully removed from the process, \b false otherwise.
///
/// \see Process::Notifications
  bool UnregisterNotificationCallbacks(const Process::Notifications &callbacks);

  //==================================================================
  // Built in Process Control functions
  //==================================================================
  /// Resumes all of a process's threads as configured using the Thread run
  /// control functions.
  ///
  /// Threads for a process should be updated with one of the run control
  /// actions (resume, step, or suspend) that they should take when the
  /// process is resumed. If no run control action is given to a thread it
  /// will be resumed by default.
  ///
  /// This function is not meant to be overridden by Process subclasses. This
  /// function will take care of disabling any breakpoints that threads may be
  /// stopped at, single stepping, and re-enabling breakpoints, and enabling
  /// the basic flow control that the plug-in instances need not worry about.
  ///
  /// N.B. This function also sets the Write side of the Run Lock, which is
  /// unset when the corresponding stop event is pulled off the Public Event
  /// Queue.  If you need to resume the process without setting the Run Lock,
  /// use PrivateResume (though you should only do that from inside the
  /// Process class.
  ///
  /// \return
  ///     Returns an error object.
  ///
  /// \see Thread:Resume()
  /// \see Thread:Step()
  /// \see Thread:Suspend()
  Status Resume();

  /// Resume a process, and wait for it to stop.
  Status ResumeSynchronous(Stream *stream);

  /// Halts a running process.
  ///
  /// This function is not meant to be overridden by Process subclasses. If
  /// the process is successfully halted, a eStateStopped process event with
  /// GetInterrupted will be broadcast.  If false, we will halt the process
  /// with no events generated by the halt.
  ///
  /// \param[in] clear_thread_plans
  ///     If true, when the process stops, clear all thread plans.
  ///
  /// \param[in] use_run_lock
  ///     Whether to release the run lock after the stop.
  ///
  /// \return
  ///     Returns an error object.  If the error is empty, the process is
  ///     halted.
  ///     otherwise the halt has failed.
  Status Halt(bool clear_thread_plans = false, bool use_run_lock = true);

  /// Detaches from a running or stopped process.
  ///
  /// This function is not meant to be overridden by Process subclasses.
  ///
  /// \param[in] keep_stopped
  ///     If true, don't resume the process on detach.
  ///
  /// \return
  ///     Returns an error object.
  Status Detach(bool keep_stopped);

  /// Kills the process and shuts down all threads that were spawned to track
  /// and monitor the process.
  ///
  /// This function is not meant to be overridden by Process subclasses.
  ///
  /// \param[in] force_kill
  ///     Whether lldb should force a kill (instead of a detach) from
  ///     the inferior process.  Normally if lldb launched a binary and
  ///     Destroy is called, lldb kills it.  If lldb attached to a
  ///     running process and Destroy is called, lldb detaches.  If
  ///     this behavior needs to be over-ridden, this is the bool that
  ///     can be used.
  ///
  /// \return
  ///     Returns an error object.
  Status Destroy(bool force_kill);

  /// Sends a process a UNIX signal \a signal.
  ///
  /// This function is not meant to be overridden by Process subclasses.
  ///
  /// \return
  ///     Returns an error object.
  Status Signal(int signal);

  void SetUnixSignals(lldb::UnixSignalsSP &&signals_sp);

  const lldb::UnixSignalsSP &GetUnixSignals();

  //==================================================================
  // Plug-in Process Control Overrides
  //==================================================================

  /// Called before attaching to a process.
  ///
  /// \return
  ///     Returns an error object.
  Status WillAttachToProcessWithID(lldb::pid_t pid);

  /// Called before attaching to a process.
  ///
  /// Allow Process plug-ins to execute some code before attaching a process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status DoWillAttachToProcessWithID(lldb::pid_t pid) {
    return Status();
  }

  /// Called before attaching to a process.
  ///
  /// \return
  ///     Returns an error object.
  Status WillAttachToProcessWithName(const char *process_name,
                                     bool wait_for_launch);

  /// Called before attaching to a process.
  ///
  /// Allow Process plug-ins to execute some code before attaching a process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status DoWillAttachToProcessWithName(const char *process_name,
                                               bool wait_for_launch) {
    return Status();
  }

  /// Attach to a remote system via a URL
  ///
  /// \param[in] remote_url
  ///     The URL format that we are connecting to.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status DoConnectRemote(llvm::StringRef remote_url) {
    Status error;
    error.SetErrorString("remote connections are not supported");
    return error;
  }

  /// Attach to an existing process using a process ID.
  ///
  /// \param[in] pid
  ///     The process ID that we should attempt to attach to.
  ///
  /// \param[in] attach_info
  ///     Information on how to do the attach. For example, GetUserID()
  ///     will return the uid to attach as.
  ///
  /// \return
  ///     Returns a successful Status attaching was successful, or
  ///     an appropriate (possibly platform-specific) error code if
  ///     attaching fails.
  /// hanming : need flag
  virtual Status DoAttachToProcessWithID(lldb::pid_t pid,
                                         const ProcessAttachInfo &attach_info) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support attaching to a process by pid",
        GetPluginName());
    return error;
  }

  /// Attach to an existing process using a partial process name.
  ///
  /// \param[in] process_name
  ///     The name of the process to attach to.
  ///
  /// \param[in] attach_info
  ///     Information on how to do the attach. For example, GetUserID()
  ///     will return the uid to attach as.
  ///
  /// \return
  ///     Returns a successful Status attaching was successful, or
  ///     an appropriate (possibly platform-specific) error code if
  ///     attaching fails.
  virtual Status
  DoAttachToProcessWithName(const char *process_name,
                            const ProcessAttachInfo &attach_info) {
    Status error;
    error.SetErrorString("attach by name is not supported");
    return error;
  }

  /// Called after attaching a process.
  ///
  /// \param[in] process_arch
  ///     If you can figure out the process architecture after attach, fill it
  ///     in here.
  ///
  /// Allow Process plug-ins to execute some code after attaching to a
  /// process.
  virtual void DidAttach(ArchSpec &process_arch) { process_arch.Clear(); }

  /// Called after a process re-execs itself.
  ///
  /// Allow Process plug-ins to execute some code after a process has exec'ed
  /// itself. Subclasses typically should override DoDidExec() as the
  /// lldb_private::Process class needs to remove its dynamic loader, runtime,
  /// ABI and other plug-ins, as well as unload all shared libraries.
  virtual void DidExec();

  /// Subclasses of Process should implement this function if they need to do
  /// anything after a process exec's itself.
  virtual void DoDidExec() {}

  /// Called after a reported fork.
  virtual void DidFork(lldb::pid_t child_pid, lldb::tid_t child_tid) {}

  /// Called after a reported vfork.
  virtual void DidVFork(lldb::pid_t child_pid, lldb::tid_t child_tid) {}

  /// Called after reported vfork completion.
  virtual void DidVForkDone() {}

  /// Called before launching to a process.
  /// \return
  ///     Returns an error object.
  Status WillLaunch(Module *module);

  /// Called before launching to a process.
  ///
  /// Allow Process plug-ins to execute some code before launching a process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status DoWillLaunch(Module *module) { return Status(); }

  /// Launch a new process.
  ///
  /// Launch a new process by spawning a new process using \a exe_module's
  /// file as the file to launch. Launch details are provided in \a
  /// launch_info.
  ///
  /// \param[in] exe_module
  ///     The module from which to extract the file specification and
  ///     launch.
  ///
  /// \param[in] launch_info
  ///     Details (e.g. arguments, stdio redirection, etc.) for the
  ///     requested launch.
  ///
  /// \return
  ///     An Status instance indicating success or failure of the
  ///     operation.
  virtual Status DoLaunch(Module *exe_module, ProcessLaunchInfo &launch_info) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support launching processes", GetPluginName());
    return error;
  }

  /// Called after launching a process.
  ///
  /// Allow Process plug-ins to execute some code after launching a process.
  virtual void DidLaunch() {}

  /// Called before resuming to a process.
  ///
  /// Allow Process plug-ins to execute some code before resuming a process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status WillResume() { return Status(); }

  /// Resumes all of a process's threads as configured using the Thread run
  /// control functions.
  ///
  /// Threads for a process should be updated with one of the run control
  /// actions (resume, step, or suspend) that they should take when the
  /// process is resumed. If no run control action is given to a thread it
  /// will be resumed by default.
  ///
  /// \return
  ///     Returns \b true if the process successfully resumes using
  ///     the thread run control actions, \b false otherwise.
  ///
  /// \see Thread:Resume()
  /// \see Thread:Step()
  /// \see Thread:Suspend()
  virtual Status DoResume() {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support resuming processes", GetPluginName());
    return error;
  }

  /// Called after resuming a process.
  ///
  /// Allow Process plug-ins to execute some code after resuming a process.
  virtual void DidResume() {}

  /// Called before halting to a process.
  ///
  /// Allow Process plug-ins to execute some code before halting a process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status WillHalt() { return Status(); }

  /// Halts a running process.
  ///
  /// DoHalt must produce one and only one stop StateChanged event if it
  /// actually stops the process.  If the stop happens through some natural
  /// event (for instance a SIGSTOP), then forwarding that event will do.
  /// Otherwise, you must generate the event manually. This function is called
  /// from the context of the private state thread.
  ///
  /// \param[out] caused_stop
  ///     If true, then this Halt caused the stop, otherwise, the
  ///     process was already stopped.
  ///
  /// \return
  ///     Returns \b true if the process successfully halts, \b false
  ///     otherwise.
  virtual Status DoHalt(bool &caused_stop) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support halting processes", GetPluginName());
    return error;
  }

  /// Called after halting a process.
  ///
  /// Allow Process plug-ins to execute some code after halting a process.
  virtual void DidHalt() {}

  /// Called before detaching from a process.
  ///
  /// Allow Process plug-ins to execute some code before detaching from a
  /// process.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status WillDetach() { return Status(); }

  /// Detaches from a running or stopped process.
  ///
  /// \return
  ///     Returns \b true if the process successfully detaches, \b
  ///     false otherwise.
  virtual Status DoDetach(bool keep_stopped) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support detaching from processes",
        GetPluginName());
    return error;
  }

  /// Called after detaching from a process.
  ///
  /// Allow Process plug-ins to execute some code after detaching from a
  /// process.
  virtual void DidDetach() {}

  virtual bool DetachRequiresHalt() { return false; }

  /// Called before sending a signal to a process.
  ///
  /// Allow Process plug-ins to execute some code before sending a signal to a
  /// process.
  ///
  /// \return
  ///     Returns no error if it is safe to proceed with a call to
  ///     Process::DoSignal(int), otherwise an error describing what
  ///     prevents the signal from being sent.
  virtual Status WillSignal() { return Status(); }

  /// Sends a process a UNIX signal \a signal.
  ///
  /// \return
  ///     Returns an error object.
  virtual Status DoSignal(int signal) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support sending signals to processes",
        GetPluginName());
    return error;
  }

  virtual Status WillDestroy() { return Status(); }

  virtual Status DoDestroy() = 0;

  virtual void DidDestroy() {}

  virtual bool DestroyRequiresHalt() { return true; }

  /// Called after sending a signal to a process.
  ///
  /// Allow Process plug-ins to execute some code after sending a signal to a
  /// process.
  virtual void DidSignal() {}

  /// Currently called as part of ShouldStop.
  /// FIXME: Should really happen when the target stops before the
  /// event is taken from the queue...
  ///
  /// This callback is called as the event
  /// is about to be queued up to allow Process plug-ins to execute some code
  /// prior to clients being notified that a process was stopped. Common
  /// operations include updating the thread list, invalidating any thread
  /// state (registers, stack, etc) prior to letting the notification go out.
  ///
  virtual void RefreshStateAfterStop() = 0;

  /// Sometimes the connection to a process can detect the host OS version
  /// that the process is running on. The current platform should be checked
  /// first in case the platform is connected, but clients can fall back onto
  /// this function if the platform fails to identify the host OS version. The
  /// platform should be checked first in case you are running a simulator
  /// platform that might itself be running natively, but have different
  /// heuristics for figuring out which OS is emulating.
  ///
  /// \return
  ///     Returns the version tuple of the host OS. In case of failure an empty
  ///     VersionTuple is returner.
  virtual llvm::VersionTuple GetHostOSVersion() { return llvm::VersionTuple(); }

  /// \return the macCatalyst version of the host OS.
  virtual llvm::VersionTuple GetHostMacCatalystVersion() { return {}; }

  /// Get the target object pointer for this module.
  ///
  /// \return
  ///     A Target object pointer to the target that owns this
  ///     module.
  Target &GetTarget() { return *m_target_wp.lock(); }

  /// Get the const target object pointer for this module.
  ///
  /// \return
  ///     A const Target object pointer to the target that owns this
  ///     module.
  const Target &GetTarget() const { return *m_target_wp.lock(); }

  /// Flush all data in the process.
  ///
  /// Flush the memory caches, all threads, and any other cached data in the
  /// process.
  ///
  /// This function can be called after a world changing event like adding a
  /// new symbol file, or after the process makes a large context switch (from
  /// boot ROM to booted into an OS).
  void Flush();

  /// Get accessor for the current process state.
  ///
  /// \return
  ///     The current state of the process.
  ///
  /// \see lldb::StateType
  lldb::StateType GetState();

  lldb::ExpressionResults
  RunThreadPlan(ExecutionContext &exe_ctx, lldb::ThreadPlanSP &thread_plan_sp,
                const EvaluateExpressionOptions &options,
                DiagnosticManager &diagnostic_manager);

  static const char *ExecutionResultAsCString(lldb::ExpressionResults result);

  void GetStatus(Stream &ostrm);

  size_t GetThreadStatus(Stream &ostrm, bool only_threads_with_stop_reason,
                         uint32_t start_frame, uint32_t num_frames,
                         uint32_t num_frames_with_source,
                         bool stop_format);

  void SendAsyncInterrupt();

  // Notify this process class that modules got loaded.
  //
  // If subclasses override this method, they must call this version before
  // doing anything in the subclass version of the function.
  virtual void ModulesDidLoad(ModuleList &module_list);

  /// Retrieve the list of shared libraries that are loaded for this process
  /// This method is used on pre-macOS 10.12, pre-iOS 10, pre-tvOS 10, pre-
  /// watchOS 3 systems.  The following two methods are for newer versions of
  /// those OSes.
  ///
  /// For certain platforms, the time it takes for the DynamicLoader plugin to
  /// read all of the shared libraries out of memory over a slow communication
  /// channel may be too long.  In that instance, the gdb-remote stub may be
  /// able to retrieve the necessary information about the solibs out of
  /// memory and return a concise summary sufficient for the DynamicLoader
  /// plugin.
  ///
  /// \param [in] image_list_address
  ///     The address where the table of shared libraries is stored in memory,
  ///     if that is appropriate for this platform.  Else this may be
  ///     passed as LLDB_INVALID_ADDRESS.
  ///
  /// \param [in] image_count
  ///     The number of shared libraries that are present in this process, if
  ///     that is appropriate for this platofrm  Else this may be passed as
  ///     LLDB_INVALID_ADDRESS.
  ///
  /// \return
  ///     A StructuredDataSP object which, if non-empty, will contain the
  ///     information the DynamicLoader needs to get the initial scan of
  ///     solibs resolved.
  virtual lldb_private::StructuredData::ObjectSP
  GetLoadedDynamicLibrariesInfos(lldb::addr_t image_list_address,
                                 lldb::addr_t image_count) {
    return StructuredData::ObjectSP();
  }

  // On macOS 10.12, tvOS 10, iOS 10, watchOS 3 and newer, debugserver can
  // return the full list of loaded shared libraries without needing any input.
  virtual lldb_private::StructuredData::ObjectSP
  GetLoadedDynamicLibrariesInfos() {
    return StructuredData::ObjectSP();
  }

  // On macOS 10.12, tvOS 10, iOS 10, watchOS 3 and newer, debugserver can
  // return information about binaries given their load addresses.
  virtual lldb_private::StructuredData::ObjectSP GetLoadedDynamicLibrariesInfos(
      const std::vector<lldb::addr_t> &load_addresses) {
    return StructuredData::ObjectSP();
  }

  // Get information about the library shared cache, if that exists
  //
  // On macOS 10.12, tvOS 10, iOS 10, watchOS 3 and newer, debugserver can
  // return information about the library shared cache (a set of standard
  // libraries that are loaded at the same location for all processes on a
  // system) in use.
  virtual lldb_private::StructuredData::ObjectSP GetSharedCacheInfo() {
    return StructuredData::ObjectSP();
  }

  // Get information about the launch state of the process, if possible.
  //
  // On Darwin systems, libdyld can report on process state, most importantly
  // the startup stages where the system library is not yet initialized.
  virtual lldb_private::StructuredData::ObjectSP
  GetDynamicLoaderProcessState() {
    return {};
  }

  /// Print a user-visible warning about a module being built with
  /// optimization
  ///
  /// Prints a async warning message to the user one time per Module where a
  /// function is found that was compiled with optimization, per Process.
  ///
  /// \param [in] sc
  ///     A SymbolContext with eSymbolContextFunction and eSymbolContextModule
  ///     pre-computed.
  void PrintWarningOptimization(const SymbolContext &sc);

  /// Print a user-visible warning about a function written in a
  /// language that this version of LLDB doesn't support.
  ///
  /// \see PrintWarningOptimization
  void PrintWarningUnsupportedLanguage(const SymbolContext &sc);

  virtual bool GetProcessInfo(ProcessInstanceInfo &info);

  /// Get the exit status for a process.
  ///
  /// \return
  ///     The process's return code, or -1 if the current process
  ///     state is not eStateExited.
  int GetExitStatus();

  /// Get a textual description of what the process exited.
  ///
  /// \return
  ///     The textual description of why the process exited, or nullptr
  ///     if there is no description available.
  const char *GetExitDescription();

  virtual void DidExit() {}

  /// Get the current address mask in the Process
  ///
  /// This mask can used to set/clear non-address bits in an addr_t.
  ///
  /// \return
  ///   The current address mask.
  ///   Bits which are set to 1 are not used for addressing.
  ///   An address mask of 0 means all bits are used for addressing.
  ///   An address mask of LLDB_INVALID_ADDRESS_MASK (all 1's) means
  ///   that no mask has been set.
  lldb::addr_t GetCodeAddressMask();
  lldb::addr_t GetDataAddressMask();

  /// The highmem masks are for targets where we may have different masks
  /// for low memory versus high memory addresses, and they will be left
  /// as LLDB_INVALID_ADDRESS_MASK normally, meaning the base masks
  /// should be applied to all addresses.
  lldb::addr_t GetHighmemCodeAddressMask();
  lldb::addr_t GetHighmemDataAddressMask();

  void SetCodeAddressMask(lldb::addr_t code_address_mask);
  void SetDataAddressMask(lldb::addr_t data_address_mask);

  void SetHighmemCodeAddressMask(lldb::addr_t code_address_mask);
  void SetHighmemDataAddressMask(lldb::addr_t data_address_mask);

  /// Some targets might use bits in a code address to indicate a mode switch,
  /// ARM uses bit zero to signify a code address is thumb, so any ARM ABI
  /// plug-ins would strip those bits.
  /// Or use the high bits to authenticate a pointer value.
  lldb::addr_t FixCodeAddress(lldb::addr_t pc);
  lldb::addr_t FixDataAddress(lldb::addr_t pc);

  /// Use this method when you do not know, or do not care what kind of address
  /// you are fixing. On platforms where there would be a difference between the
  /// two types, it will pick the safest option.
  ///
  /// Its purpose is to signal that no specific choice was made and provide an
  /// alternative to randomly picking FixCode/FixData address. Which could break
  /// platforms where there is a difference (only Arm Thumb at this time).
  lldb::addr_t FixAnyAddress(lldb::addr_t pc);

  /// Get the Modification ID of the process.
  ///
  /// \return
  ///     The modification ID of the process.
  ProcessModID GetModID() const { return m_mod_id; }

  const ProcessModID &GetModIDRef() const { return m_mod_id; }

  uint32_t GetStopID() const { return m_mod_id.GetStopID(); }

  uint32_t GetResumeID() const { return m_mod_id.GetResumeID(); }

  uint32_t GetLastUserExpressionResumeID() const {
    return m_mod_id.GetLastUserExpressionResumeID();
  }

  uint32_t GetLastNaturalStopID() const {
    return m_mod_id.GetLastNaturalStopID();
  }

  lldb::EventSP GetStopEventForStopID(uint32_t stop_id) const {
    return m_mod_id.GetStopEventForStopID(stop_id);
  }

  /// Set accessor for the process exit status (return code).
  ///
  /// Sometimes a child exits and the exit can be detected by global functions
  /// (signal handler for SIGCHLD for example). This accessor allows the exit
  /// status to be set from an external source.
  ///
  /// Setting this will cause a eStateExited event to be posted to the process
  /// event queue.
  ///
  /// \param[in] exit_status
  ///     The value for the process's return code.
  ///
  /// \param[in] exit_string
  ///     A StringRef containing the reason for exiting. May be empty.
  ///
  /// \return
  ///     Returns \b false if the process was already in an exited state, \b
  ///     true otherwise.
  virtual bool SetExitStatus(int exit_status, llvm::StringRef exit_string);

  /// Check if a process is still alive.
  ///
  /// \return
  ///     Returns \b true if the process is still valid, \b false
  ///     otherwise.
  virtual bool IsAlive();

  virtual bool IsLiveDebugSession() const { return true; };

  /// Provide a way to retrieve the core dump file that is loaded for debugging.
  /// Only available if IsLiveDebugSession() returns true.
  ///
  /// \return
  ///     File path to the core file.
  virtual FileSpec GetCoreFile() const { return {}; }

  /// Before lldb detaches from a process, it warns the user that they are
  /// about to lose their debug session. In some cases, this warning doesn't
  /// need to be emitted -- for instance, with core file debugging where the
  /// user can reconstruct the "state" by simply re-running the debugger on
  /// the core file.
  ///
  /// \return
  ///     Returns \b true if the user should be warned about detaching from
  ///     this process.
  virtual bool WarnBeforeDetach() const { return true; }

  /// Read of memory from a process.
  ///
  /// This function will read memory from the current process's address space
  /// and remove any traps that may have been inserted into the memory.
  ///
  /// This function is not meant to be overridden by Process subclasses, the
  /// subclasses should implement Process::DoReadMemory (lldb::addr_t, size_t,
  /// void *).
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start reading
  ///     memory from.
  ///
  /// \param[out] buf
  ///     A byte buffer that is at least \a size bytes long that
  ///     will receive the memory bytes.
  ///
  /// \param[in] size
  ///     The number of bytes to read.
  ///
  /// \param[out] error
  ///     An error that indicates the success or failure of this
  ///     operation. If error indicates success (error.Success()),
  ///     then the value returned can be trusted, otherwise zero
  ///     will be returned.
  ///
  /// \return
  ///     The number of bytes that were actually read into \a buf. If
  ///     the returned number is greater than zero, yet less than \a
  ///     size, then this function will get called again with \a
  ///     vm_addr, \a buf, and \a size updated appropriately. Zero is
  ///     returned in the case of an error.
  virtual size_t ReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                            Status &error);

  /// Read of memory from a process.
  ///
  /// This function has the same semantics of ReadMemory except that it
  /// bypasses caching.
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start reading
  ///     memory from.
  ///
  /// \param[out] buf
  ///     A byte buffer that is at least \a size bytes long that
  ///     will receive the memory bytes.
  ///
  /// \param[in] size
  ///     The number of bytes to read.
  ///
  /// \param[out] error
  ///     An error that indicates the success or failure of this
  ///     operation. If error indicates success (error.Success()),
  ///     then the value returned can be trusted, otherwise zero
  ///     will be returned.
  ///
  /// \return
  ///     The number of bytes that were actually read into \a buf. If
  ///     the returned number is greater than zero, yet less than \a
  ///     size, then this function will get called again with \a
  ///     vm_addr, \a buf, and \a size updated appropriately. Zero is
  ///     returned in the case of an error.
  size_t ReadMemoryFromInferior(lldb::addr_t vm_addr, void *buf, size_t size,
                                Status &error);

  /// Read a NULL terminated C string from memory
  ///
  /// This function will read a cache page at a time until the NULL
  /// C string terminator is found. It will stop reading if the NULL
  /// termination byte isn't found before reading \a cstr_max_len bytes, and
  /// the results are always guaranteed to be NULL terminated (at most
  /// cstr_max_len - 1 bytes will be read).
  size_t ReadCStringFromMemory(lldb::addr_t vm_addr, char *cstr,
                               size_t cstr_max_len, Status &error);

  size_t ReadCStringFromMemory(lldb::addr_t vm_addr, std::string &out_str,
                               Status &error);

  /// Reads an unsigned integer of the specified byte size from process
  /// memory.
  ///
  /// \param[in] load_addr
  ///     A load address of the integer to read.
  ///
  /// \param[in] byte_size
  ///     The size in byte of the integer to read.
  ///
  /// \param[in] fail_value
  ///     The value to return if we fail to read an integer.
  ///
  /// \param[out] error
  ///     An error that indicates the success or failure of this
  ///     operation. If error indicates success (error.Success()),
  ///     then the value returned can be trusted, otherwise zero
  ///     will be returned.
  ///
  /// \return
  ///     The unsigned integer that was read from the process memory
  ///     space. If the integer was smaller than a uint64_t, any
  ///     unused upper bytes will be zero filled. If the process
  ///     byte order differs from the host byte order, the integer
  ///     value will be appropriately byte swapped into host byte
  ///     order.
  uint64_t ReadUnsignedIntegerFromMemory(lldb::addr_t load_addr,
                                         size_t byte_size, uint64_t fail_value,
                                         Status &error);

  int64_t ReadSignedIntegerFromMemory(lldb::addr_t load_addr, size_t byte_size,
                                      int64_t fail_value, Status &error);

  lldb::addr_t ReadPointerFromMemory(lldb::addr_t vm_addr, Status &error);

  bool WritePointerToMemory(lldb::addr_t vm_addr, lldb::addr_t ptr_value,
                            Status &error);

  /// Actually do the writing of memory to a process.
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start writing
  ///     memory to.
  ///
  /// \param[in] buf
  ///     A byte buffer that is at least \a size bytes long that
  ///     contains the data to write.
  ///
  /// \param[in] size
  ///     The number of bytes to write.
  ///
  /// \param[out] error
  ///     An error value in case the memory write fails.
  ///
  /// \return
  ///     The number of bytes that were actually written.
  virtual size_t DoWriteMemory(lldb::addr_t vm_addr, const void *buf,
                               size_t size, Status &error) {
    error.SetErrorStringWithFormatv(
        "error: {0} does not support writing to processes", GetPluginName());
    return 0;
  }

  /// Write all or part of a scalar value to memory.
  ///
  /// The value contained in \a scalar will be swapped to match the byte order
  /// of the process that is being debugged. If \a size is less than the size
  /// of scalar, the least significant \a size bytes from scalar will be
  /// written. If \a size is larger than the byte size of scalar, then the
  /// extra space will be padded with zeros and the scalar value will be
  /// placed in the least significant bytes in memory.
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start writing
  ///     memory to.
  ///
  /// \param[in] scalar
  ///     The scalar to write to the debugged process.
  ///
  /// \param[in] size
  ///     This value can be smaller or larger than the scalar value
  ///     itself. If \a size is smaller than the size of \a scalar,
  ///     the least significant bytes in \a scalar will be used. If
  ///     \a size is larger than the byte size of \a scalar, then
  ///     the extra space will be padded with zeros. If \a size is
  ///     set to UINT32_MAX, then the size of \a scalar will be used.
  ///
  /// \param[out] error
  ///     An error value in case the memory write fails.
  ///
  /// \return
  ///     The number of bytes that were actually written.
  size_t WriteScalarToMemory(lldb::addr_t vm_addr, const Scalar &scalar,
                             size_t size, Status &error);

  size_t ReadScalarIntegerFromMemory(lldb::addr_t addr, uint32_t byte_size,
                                     bool is_signed, Scalar &scalar,
                                     Status &error);

  /// Write memory to a process.
  ///
  /// This function will write memory to the current process's address space
  /// and maintain any traps that might be present due to software
  /// breakpoints.
  ///
  /// This function is not meant to be overridden by Process subclasses, the
  /// subclasses should implement Process::DoWriteMemory (lldb::addr_t,
  /// size_t, void *).
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start writing
  ///     memory to.
  ///
  /// \param[in] buf
  ///     A byte buffer that is at least \a size bytes long that
  ///     contains the data to write.
  ///
  /// \param[in] size
  ///     The number of bytes to write.
  ///
  /// \return
  ///     The number of bytes that were actually written.
  // TODO: change this to take an ArrayRef<uint8_t>
  size_t WriteMemory(lldb::addr_t vm_addr, const void *buf, size_t size,
                     Status &error);

  /// Actually allocate memory in the process.
  ///
  /// This function will allocate memory in the process's address space.  This
  /// can't rely on the generic function calling mechanism, since that
  /// requires this function.
  ///
  /// \param[in] size
  ///     The size of the allocation requested.
  ///
  /// \return
  ///     The address of the allocated buffer in the process, or
  ///     LLDB_INVALID_ADDRESS if the allocation failed.

  virtual lldb::addr_t DoAllocateMemory(size_t size, uint32_t permissions,
                                        Status &error) {
    error.SetErrorStringWithFormatv(
        "error: {0} does not support allocating in the debug process",
        GetPluginName());
    return LLDB_INVALID_ADDRESS;
  }

  virtual Status WriteObjectFile(std::vector<ObjectFile::LoadableData> entries);

  /// The public interface to allocating memory in the process.
  ///
  /// This function will allocate memory in the process's address space.  This
  /// can't rely on the generic function calling mechanism, since that
  /// requires this function.
  ///
  /// \param[in] size
  ///     The size of the allocation requested.
  ///
  /// \param[in] permissions
  ///     Or together any of the lldb::Permissions bits.  The permissions on
  ///     a given memory allocation can't be changed after allocation.  Note
  ///     that a block that isn't set writable can still be written on from
  ///     lldb,
  ///     just not by the process itself.
  ///
  /// \param[in,out] error
  ///     An error object to fill in if things go wrong.
  /// \return
  ///     The address of the allocated buffer in the process, or
  ///     LLDB_INVALID_ADDRESS if the allocation failed.
  lldb::addr_t AllocateMemory(size_t size, uint32_t permissions, Status &error);

  /// The public interface to allocating memory in the process, this also
  /// clears the allocated memory.
  ///
  /// This function will allocate memory in the process's address space.  This
  /// can't rely on the generic function calling mechanism, since that
  /// requires this function.
  ///
  /// \param[in] size
  ///     The size of the allocation requested.
  ///
  /// \param[in] permissions
  ///     Or together any of the lldb::Permissions bits.  The permissions on
  ///     a given memory allocation can't be changed after allocation.  Note
  ///     that a block that isn't set writable can still be written on from
  ///     lldb,
  ///     just not by the process itself.
  ///
  /// \param[in,out] error
  ///     An error object to fill in if things go wrong.
  ///
  /// \return
  ///     The address of the allocated buffer in the process, or
  ///     LLDB_INVALID_ADDRESS if the allocation failed.

  lldb::addr_t CallocateMemory(size_t size, uint32_t permissions,
                               Status &error);

  /// If this architecture and process supports memory tagging, return a tag
  /// manager that can be used to maniupulate those memory tags.
  ///
  /// \return
  ///     Either a valid pointer to a tag manager or an error describing why one
  ///     could not be provided.
  llvm::Expected<const MemoryTagManager *> GetMemoryTagManager();

  /// Read memory tags for the range addr to addr+len. It is assumed
  /// that this range has already been granule aligned.
  /// (see MemoryTagManager::MakeTaggedRange)
  ///
  /// This calls DoReadMemoryTags to do the target specific operations.
  ///
  /// \param[in] addr
  ///     Start of memory range to read tags for.
  ///
  /// \param[in] len
  ///     Length of memory range to read tags for (in bytes).
  ///
  /// \return
  ///     If this architecture or process does not support memory tagging,
  ///     an error saying so.
  ///     If it does, either the memory tags or an error describing a
  ///     failure to read or unpack them.
  virtual llvm::Expected<std::vector<lldb::addr_t>>
  ReadMemoryTags(lldb::addr_t addr, size_t len);

  /// Write memory tags for a range of memory.
  /// (calls DoWriteMemoryTags to do the target specific work)
  ///
  /// \param[in] addr
  ///     The address to start writing tags from. It is assumed that this
  ///     address is granule aligned.
  ///
  /// \param[in] len
  ///     The size of the range to write tags for. It is assumed that this
  ///     is some multiple of the granule size. This len can be different
  ///     from (number of tags * granule size) in the case where you want
  ///     lldb-server to repeat tags across the range.
  ///
  /// \param[in] tags
  ///     Allocation tags to be written. Since lldb-server can repeat tags for a
  ///     range, the number of tags doesn't have to match the number of granules
  ///     in the range. (though most of the time it will)
  ///
  /// \return
  ///     A Status telling you if the write succeeded or not.
  Status WriteMemoryTags(lldb::addr_t addr, size_t len,
                         const std::vector<lldb::addr_t> &tags);

  /// Resolve dynamically loaded indirect functions.
  ///
  /// \param[in] address
  ///     The load address of the indirect function to resolve.
  ///
  /// \param[out] error
  ///     An error value in case the resolve fails.
  ///
  /// \return
  ///     The address of the resolved function.
  ///     LLDB_INVALID_ADDRESS if the resolution failed.
  virtual lldb::addr_t ResolveIndirectFunction(const Address *address,
                                               Status &error);

  /// Locate the memory region that contains load_addr.
  ///
  /// If load_addr is within the address space the process has mapped
  /// range_info will be filled in with the start and end of that range as
  /// well as the permissions for that range and range_info. GetMapped will
  /// return true.
  ///
  /// If load_addr is outside any mapped region then range_info will have its
  /// start address set to load_addr and the end of the range will indicate
  /// the start of the next mapped range or be set to LLDB_INVALID_ADDRESS if
  /// there are no valid mapped ranges between load_addr and the end of the
  /// process address space.
  ///
  /// GetMemoryRegionInfo calls DoGetMemoryRegionInfo. Override that function in
  /// process subclasses.
  ///
  /// \param[in] load_addr
  ///     The load address to query the range_info for. May include non
  ///     address bits, these will be removed by the ABI plugin if there is
  ///     one.
  ///
  /// \param[out] range_info
  ///     An range_info value containing the details of the range.
  ///
  /// \return
  ///     An error value.
  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info);

  /// Obtain all the mapped memory regions within this process.
  ///
  /// \param[out] region_list
  ///     A vector to contain MemoryRegionInfo objects for all mapped
  ///     ranges.
  ///
  /// \return
  ///     An error value.
  virtual Status
  GetMemoryRegions(lldb_private::MemoryRegionInfos &region_list);

  /// Get the number of watchpoints supported by this target.
  ///
  /// We may be able to determine the number of watchpoints available
  /// on this target; retrieve this value if possible.
  ///
  /// This number may be less than the number of watchpoints a user
  /// can specify. This is because a single user watchpoint may require
  /// multiple watchpoint slots to implement. Due to the size
  /// and/or alignment of objects.
  ///
  /// \return
  ///     Returns the number of watchpoints, if available.
  virtual std::optional<uint32_t> GetWatchpointSlotCount() {
    return std::nullopt;
  }

  /// Whether lldb will be notified about watchpoints after
  /// the instruction has completed executing, or if the
  /// instruction is rolled back and it is notified before it
  /// executes.
  /// The default behavior is "exceptions received after instruction
  /// has executed", except for certain CPU architectures.
  /// Process subclasses may override this if they have additional
  /// information.
  ///
  /// \return
  ///     Returns true for targets where lldb is notified after
  ///     the instruction has completed executing.
  bool GetWatchpointReportedAfter();

  lldb::ModuleSP ReadModuleFromMemory(const FileSpec &file_spec,
                                      lldb::addr_t header_addr,
                                      size_t size_to_read = 512);

  /// Attempt to get the attributes for a region of memory in the process.
  ///
  /// It may be possible for the remote debug server to inspect attributes for
  /// a region of memory in the process, such as whether there is a valid page
  /// of memory at a given address or whether that page is
  /// readable/writable/executable by the process.
  ///
  /// \param[in] load_addr
  ///     The address of interest in the process.
  ///
  /// \param[out] permissions
  ///     If this call returns successfully, this bitmask will have
  ///     its Permissions bits set to indicate whether the region is
  ///     readable/writable/executable.  If this call fails, the
  ///     bitmask values are undefined.
  ///
  /// \return
  ///     Returns true if it was able to determine the attributes of the
  ///     memory region.  False if not.
  virtual bool GetLoadAddressPermissions(lldb::addr_t load_addr,
                                         uint32_t &permissions);

  /// Determines whether executing JIT-compiled code in this process is
  /// possible.
  ///
  /// \return
  ///     True if execution of JIT code is possible; false otherwise.
  bool CanJIT();

  /// Sets whether executing JIT-compiled code in this process is possible.
  ///
  /// \param[in] can_jit
  ///     True if execution of JIT code is possible; false otherwise.
  void SetCanJIT(bool can_jit);

  /// Determines whether executing function calls using the interpreter is
  /// possible for this process.
  ///
  /// \return
  ///     True if possible; false otherwise.
  bool CanInterpretFunctionCalls() { return m_can_interpret_function_calls; }

  /// Sets whether executing function calls using the interpreter is possible
  /// for this process.
  ///
  /// \param[in] can_interpret_function_calls
  ///     True if possible; false otherwise.
  void SetCanInterpretFunctionCalls(bool can_interpret_function_calls) {
    m_can_interpret_function_calls = can_interpret_function_calls;
  }

  /// Sets whether executing code in this process is possible. This could be
  /// either through JIT or interpreting.
  ///
  /// \param[in] can_run_code
  ///     True if execution of code is possible; false otherwise.
  void SetCanRunCode(bool can_run_code);

  /// Actually deallocate memory in the process.
  ///
  /// This function will deallocate memory in the process's address space that
  /// was allocated with AllocateMemory.
  ///
  /// \param[in] ptr
  ///     A return value from AllocateMemory, pointing to the memory you
  ///     want to deallocate.
  ///
  /// \return
  ///     \b true if the memory was deallocated, \b false otherwise.
  virtual Status DoDeallocateMemory(lldb::addr_t ptr) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support deallocating in the debug process",
        GetPluginName());
    return error;
  }

  /// The public interface to deallocating memory in the process.
  ///
  /// This function will deallocate memory in the process's address space that
  /// was allocated with AllocateMemory.
  ///
  /// \param[in] ptr
  ///     A return value from AllocateMemory, pointing to the memory you
  ///     want to deallocate.
  ///
  /// \return
  ///     \b true if the memory was deallocated, \b false otherwise.
  Status DeallocateMemory(lldb::addr_t ptr);

  /// Get any available STDOUT.
  ///
  /// Calling this method is a valid operation only if all of the following
  /// conditions are true: 1) The process was launched, and not attached to.
  /// 2) The process was not launched with eLaunchFlagDisableSTDIO. 3) The
  /// process was launched without supplying a valid file path
  ///    for STDOUT.
  ///
  /// Note that the implementation will probably need to start a read thread
  /// in the background to make sure that the pipe is drained and the STDOUT
  /// buffered appropriately, to prevent the process from deadlocking trying
  /// to write to a full buffer.
  ///
  /// Events will be queued indicating that there is STDOUT available that can
  /// be retrieved using this function.
  ///
  /// \param[out] buf
  ///     A buffer that will receive any STDOUT bytes that are
  ///     currently available.
  ///
  /// \param[in] buf_size
  ///     The size in bytes for the buffer \a buf.
  ///
  /// \return
  ///     The number of bytes written into \a buf. If this value is
  ///     equal to \a buf_size, another call to this function should
  ///     be made to retrieve more STDOUT data.
  virtual size_t GetSTDOUT(char *buf, size_t buf_size, Status &error);

  /// Get any available STDERR.
  ///
  /// Calling this method is a valid operation only if all of the following
  /// conditions are true: 1) The process was launched, and not attached to.
  /// 2) The process was not launched with eLaunchFlagDisableSTDIO. 3) The
  /// process was launched without supplying a valid file path
  ///    for STDERR.
  ///
  /// Note that the implementation will probably need to start a read thread
  /// in the background to make sure that the pipe is drained and the STDERR
  /// buffered appropriately, to prevent the process from deadlocking trying
  /// to write to a full buffer.
  ///
  /// Events will be queued indicating that there is STDERR available that can
  /// be retrieved using this function.
  ///
  /// \param[in] buf
  ///     A buffer that will receive any STDERR bytes that are
  ///     currently available.
  ///
  /// \param[out] buf_size
  ///     The size in bytes for the buffer \a buf.
  ///
  /// \return
  ///     The number of bytes written into \a buf. If this value is
  ///     equal to \a buf_size, another call to this function should
  ///     be made to retrieve more STDERR data.
  virtual size_t GetSTDERR(char *buf, size_t buf_size, Status &error);

  /// Puts data into this process's STDIN.
  ///
  /// Calling this method is a valid operation only if all of the following
  /// conditions are true: 1) The process was launched, and not attached to.
  /// 2) The process was not launched with eLaunchFlagDisableSTDIO. 3) The
  /// process was launched without supplying a valid file path
  ///    for STDIN.
  ///
  /// \param[in] buf
  ///     A buffer that contains the data to write to the process's STDIN.
  ///
  /// \param[in] buf_size
  ///     The size in bytes for the buffer \a buf.
  ///
  /// \return
  ///     The number of bytes written into \a buf. If this value is
  ///     less than \a buf_size, another call to this function should
  ///     be made to write the rest of the data.
  virtual size_t PutSTDIN(const char *buf, size_t buf_size, Status &error) {
    error.SetErrorString("stdin unsupported");
    return 0;
  }

  /// Get any available profile data.
  ///
  /// \param[out] buf
  ///     A buffer that will receive any profile data bytes that are
  ///     currently available.
  ///
  /// \param[out] buf_size
  ///     The size in bytes for the buffer \a buf.
  ///
  /// \return
  ///     The number of bytes written into \a buf. If this value is
  ///     equal to \a buf_size, another call to this function should
  ///     be made to retrieve more profile data.
  virtual size_t GetAsyncProfileData(char *buf, size_t buf_size, Status &error);

  // Process Breakpoints
  size_t GetSoftwareBreakpointTrapOpcode(BreakpointSite *bp_site);

  virtual Status EnableBreakpointSite(BreakpointSite *bp_site) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support enabling breakpoints", GetPluginName());
    return error;
  }

  virtual Status DisableBreakpointSite(BreakpointSite *bp_site) {
    Status error;
    error.SetErrorStringWithFormatv(
        "error: {0} does not support disabling breakpoints", GetPluginName());
    return error;
  }

  // This is implemented completely using the lldb::Process API. Subclasses
  // don't need to implement this function unless the standard flow of read
  // existing opcode, write breakpoint opcode, verify breakpoint opcode doesn't
  // work for a specific process plug-in.
  virtual Status EnableSoftwareBreakpoint(BreakpointSite *bp_site);

  // This is implemented completely using the lldb::Process API. Subclasses
  // don't need to implement this function unless the standard flow of
  // restoring original opcode in memory and verifying the restored opcode
  // doesn't work for a specific process plug-in.
  virtual Status DisableSoftwareBreakpoint(BreakpointSite *bp_site);

  StopPointSiteList<lldb_private::BreakpointSite> &GetBreakpointSiteList();

  const StopPointSiteList<lldb_private::BreakpointSite> &
  GetBreakpointSiteList() const;

  void DisableAllBreakpointSites();

  Status ClearBreakpointSiteByID(lldb::user_id_t break_id);

  lldb::break_id_t CreateBreakpointSite(const lldb::BreakpointLocationSP &owner,
                                        bool use_hardware);

  Status DisableBreakpointSiteByID(lldb::user_id_t break_id);

  Status EnableBreakpointSiteByID(lldb::user_id_t break_id);

  // BreakpointLocations use RemoveConstituentFromBreakpointSite to remove
  // themselves from the constituent's list of this breakpoint sites.
  void RemoveConstituentFromBreakpointSite(lldb::user_id_t site_id,
                                           lldb::user_id_t constituent_id,
                                           lldb::BreakpointSiteSP &bp_site_sp);

  // Process Watchpoints (optional)
  virtual Status EnableWatchpoint(lldb::WatchpointSP wp_sp, bool notify = true);

  virtual Status DisableWatchpoint(lldb::WatchpointSP wp_sp,
                                   bool notify = true);

  // Thread Queries

  /// Update the thread list.
  ///
  /// This method performs some general clean up before invoking
  /// \a DoUpdateThreadList, which should be implemented by each
  /// process plugin.
  ///
  /// \return
  ///     \b true if the new thread list could be generated, \b false otherwise.
  bool UpdateThreadList(ThreadList &old_thread_list,
                        ThreadList &new_thread_list);

  void UpdateThreadListIfNeeded();

  ThreadList &GetThreadList() { return m_thread_list; }

  StopPointSiteList<lldb_private::WatchpointResource> &
  GetWatchpointResourceList() {
    return m_watchpoint_resource_list;
  }

  // When ExtendedBacktraces are requested, the HistoryThreads that are created
  // need an owner -- they're saved here in the Process.  The threads in this
  // list are not iterated over - driver programs need to request the extended
  // backtrace calls starting from a root concrete thread one by one.
  ThreadList &GetExtendedThreadList() { return m_extended_thread_list; }

  ThreadList::ThreadIterable Threads() { return m_thread_list.Threads(); }

  uint32_t GetNextThreadIndexID(uint64_t thread_id);

  lldb::ThreadSP CreateOSPluginThread(lldb::tid_t tid, lldb::addr_t context);

  // Returns true if an index id has been assigned to a thread.
  bool HasAssignedIndexIDToThread(uint64_t sb_thread_id);

  // Given a thread_id, it will assign a more reasonable index id for display
  // to the user. If the thread_id has previously been assigned, the same index
  // id will be used.
  uint32_t AssignIndexIDToThread(uint64_t thread_id);

  // Queue Queries

  virtual void UpdateQueueListIfNeeded();

  QueueList &GetQueueList() {
    UpdateQueueListIfNeeded();
    return m_queue_list;
  }

  QueueList::QueueIterable Queues() {
    UpdateQueueListIfNeeded();
    return m_queue_list.Queues();
  }

  // Event Handling
  lldb::StateType GetNextEvent(lldb::EventSP &event_sp);

  // Returns the process state when it is stopped. If specified, event_sp_ptr
  // is set to the event which triggered the stop. If wait_always = false, and
  // the process is already stopped, this function returns immediately. If the
  // process is hijacked and use_run_lock is true (the default), then this
  // function releases the run lock after the stop. Setting use_run_lock to
  // false will avoid this behavior.
  // If we are waiting to stop that will return control to the user,
  // then we also want to run SelectMostRelevantFrame, which is controlled
  // by "select_most_relevant".
  lldb::StateType
  WaitForProcessToStop(const Timeout<std::micro> &timeout,
                       lldb::EventSP *event_sp_ptr = nullptr,
                       bool wait_always = true,
                       lldb::ListenerSP hijack_listener = lldb::ListenerSP(),
                       Stream *stream = nullptr, bool use_run_lock = true,
                       SelectMostRelevant select_most_relevant =
                           DoNoSelectMostRelevantFrame);

  uint32_t GetIOHandlerID() const { return m_iohandler_sync.GetValue(); }

  /// Waits for the process state to be running within a given msec timeout.
  ///
  /// The main purpose of this is to implement an interlock waiting for
  /// HandlePrivateEvent to push an IOHandler.
  ///
  /// \param[in] timeout
  ///     The maximum time length to wait for the process to transition to the
  ///     eStateRunning state.
  void SyncIOHandler(uint32_t iohandler_id, const Timeout<std::micro> &timeout);

  lldb::StateType GetStateChangedEvents(
      lldb::EventSP &event_sp, const Timeout<std::micro> &timeout,
      lldb::ListenerSP
          hijack_listener); // Pass an empty ListenerSP to use builtin listener

  /// Centralize the code that handles and prints descriptions for process
  /// state changes.
  ///
  /// \param[in] event_sp
  ///     The process state changed event
  ///
  /// \param[in] stream
  ///     The output stream to get the state change description
  ///
  /// \param[in,out] pop_process_io_handler
  ///     If this value comes in set to \b true, then pop the Process IOHandler
  ///     if needed.
  ///     Else this variable will be set to \b true or \b false to indicate if
  ///     the process
  ///     needs to have its process IOHandler popped.
  ///
  /// \return
  ///     \b true if the event describes a process state changed event, \b false
  ///     otherwise.
  static bool
  HandleProcessStateChangedEvent(const lldb::EventSP &event_sp, Stream *stream,
                                 SelectMostRelevant select_most_relevant,
                                 bool &pop_process_io_handler);

  Event *PeekAtStateChangedEvents();

  class ProcessEventHijacker {
  public:
    ProcessEventHijacker(Process &process, lldb::ListenerSP listener_sp)
        : m_process(process) {
      m_process.HijackProcessEvents(std::move(listener_sp));
    }

    ~ProcessEventHijacker() { m_process.RestoreProcessEvents(); }

  private:
    Process &m_process;
  };

  friend class ProcessEventHijacker;
  friend class ProcessProperties;
  /// If you need to ensure that you and only you will hear about some public
  /// event, then make a new listener, set to listen to process events, and
  /// then call this with that listener.  Then you will have to wait on that
  /// listener explicitly for events (rather than using the GetNextEvent &
  /// WaitFor* calls above.  Be sure to call RestoreProcessEvents when you are
  /// done.
  ///
  /// \param[in] listener_sp
  ///     This is the new listener to whom all process events will be delivered.
  ///
  /// \return
  ///     Returns \b true if the new listener could be installed,
  ///     \b false otherwise.
  bool HijackProcessEvents(lldb::ListenerSP listener_sp);

  /// Restores the process event broadcasting to its normal state.
  ///
  void RestoreProcessEvents();

  bool StateChangedIsHijackedForSynchronousResume();

  bool StateChangedIsExternallyHijacked();

  const lldb::ABISP &GetABI();

  OperatingSystem *GetOperatingSystem() { return m_os_up.get(); }

  std::vector<LanguageRuntime *> GetLanguageRuntimes();

  LanguageRuntime *GetLanguageRuntime(lldb::LanguageType language);

  bool IsPossibleDynamicValue(ValueObject &in_value);

  bool IsRunning() const;

  DynamicCheckerFunctions *GetDynamicCheckers() {
    return m_dynamic_checkers_up.get();
  }

  void SetDynamicCheckers(DynamicCheckerFunctions *dynamic_checkers);

/// Prune ThreadPlanStacks for unreported threads.
///
/// \param[in] tid
///     The tid whose Plan Stack we are seeking to prune.
///
/// \return
///     \b true if the TID is found or \b false if not.
bool PruneThreadPlansForTID(lldb::tid_t tid);

/// Prune ThreadPlanStacks for all unreported threads.
void PruneThreadPlans();

  /// Find the thread plan stack associated with thread with \a tid.
  ///
  /// \param[in] tid
  ///     The tid whose Plan Stack we are seeking.
  ///
  /// \return
  ///     Returns a ThreadPlan if the TID is found or nullptr if not.
  ThreadPlanStack *FindThreadPlans(lldb::tid_t tid);

  /// Dump the thread plans associated with thread with \a tid.
  ///
  /// \param[in,out] strm
  ///     The stream to which to dump the output
  ///
  /// \param[in] tid
  ///     The tid whose Plan Stack we are dumping
  ///
  /// \param[in] desc_level
  ///     How much detail to dump
  ///
  /// \param[in] internal
  ///     If \b true dump all plans, if false only user initiated plans
  ///
  /// \param[in] condense_trivial
  ///     If true, only dump a header if the plan stack is just the base plan.
  ///
  /// \param[in] skip_unreported_plans
  ///     If true, only dump a plan if it is currently backed by an
  ///     lldb_private::Thread *.
  ///
  /// \return
  ///     Returns \b true if TID was found, \b false otherwise
  bool DumpThreadPlansForTID(Stream &strm, lldb::tid_t tid,
                             lldb::DescriptionLevel desc_level, bool internal,
                             bool condense_trivial, bool skip_unreported_plans);

  /// Dump all the thread plans for this process.
  ///
  /// \param[in,out] strm
  ///     The stream to which to dump the output
  ///
  /// \param[in] desc_level
  ///     How much detail to dump
  ///
  /// \param[in] internal
  ///     If \b true dump all plans, if false only user initiated plans
  ///
  /// \param[in] condense_trivial
  ///     If true, only dump a header if the plan stack is just the base plan.
  ///
  /// \param[in] skip_unreported_plans
  ///     If true, skip printing all thread plan stacks that don't currently
  ///     have a backing lldb_private::Thread *.
  void DumpThreadPlans(Stream &strm, lldb::DescriptionLevel desc_level,
                       bool internal, bool condense_trivial,
                       bool skip_unreported_plans);

  /// Call this to set the lldb in the mode where it breaks on new thread
  /// creations, and then auto-restarts.  This is useful when you are trying
  /// to run only one thread, but either that thread or the kernel is creating
  /// new threads in the process.  If you stop when the thread is created, you
  /// can immediately suspend it, and keep executing only the one thread you
  /// intend.
  ///
  /// \return
  ///     Returns \b true if we were able to start up the notification
  ///     \b false otherwise.
  virtual bool StartNoticingNewThreads() { return true; }

  /// Call this to turn off the stop & notice new threads mode.
  ///
  /// \return
  ///     Returns \b true if we were able to start up the notification
  ///     \b false otherwise.
  virtual bool StopNoticingNewThreads() { return true; }

  void SetRunningUserExpression(bool on);
  void SetRunningUtilityFunction(bool on);

  // lldb::ExecutionContextScope pure virtual functions
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override { return shared_from_this(); }

  lldb::ThreadSP CalculateThread() override { return lldb::ThreadSP(); }

  lldb::StackFrameSP CalculateStackFrame() override {
    return lldb::StackFrameSP();
  }

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  void SetSTDIOFileDescriptor(int file_descriptor);

  // Add a permanent region of memory that should never be read or written to.
  // This can be used to ensure that memory reads or writes to certain areas of
  // memory never end up being sent to the DoReadMemory or DoWriteMemory
  // functions which can improve performance.
  void AddInvalidMemoryRegion(const LoadRange &region);

  // Remove a permanent region of memory that should never be read or written
  // to that was previously added with AddInvalidMemoryRegion.
  bool RemoveInvalidMemoryRange(const LoadRange &region);

  // If the setup code of a thread plan needs to do work that might involve
  // calling a function in the target, it should not do that work directly in
  // one of the thread plan functions (DidPush/WillResume) because such work
  // needs to be handled carefully.  Instead, put that work in a
  // PreResumeAction callback, and register it with the process.  It will get
  // done before the actual "DoResume" gets called.

  typedef bool(PreResumeActionCallback)(void *);

  void AddPreResumeAction(PreResumeActionCallback callback, void *baton);

  bool RunPreResumeActions();

  void ClearPreResumeActions();

  void ClearPreResumeAction(PreResumeActionCallback callback, void *baton);

  ProcessRunLock &GetRunLock();

  bool CurrentThreadIsPrivateStateThread();

  virtual Status SendEventData(const char *data) {
    Status return_error("Sending an event is not supported for this process.");
    return return_error;
  }

  lldb::ThreadCollectionSP GetHistoryThreads(lldb::addr_t addr);

  lldb::InstrumentationRuntimeSP
  GetInstrumentationRuntime(lldb::InstrumentationRuntimeType type);

  /// Try to fetch the module specification for a module with the given file
  /// name and architecture. Process sub-classes have to override this method
  /// if they support platforms where the Platform object can't get the module
  /// spec for all module.
  ///
  /// \param[in] module_file_spec
  ///     The file name of the module to get specification for.
  ///
  /// \param[in] arch
  ///     The architecture of the module to get specification for.
  ///
  /// \param[out] module_spec
  ///     The fetched module specification if the return value is
  ///     \b true, unchanged otherwise.
  ///
  /// \return
  ///     Returns \b true if the module spec fetched successfully,
  ///     \b false otherwise.
  virtual bool GetModuleSpec(const FileSpec &module_file_spec,
                             const ArchSpec &arch, ModuleSpec &module_spec);

  virtual void PrefetchModuleSpecs(llvm::ArrayRef<FileSpec> module_file_specs,
                                   const llvm::Triple &triple) {}

  /// Try to find the load address of a file.
  /// The load address is defined as the address of the first memory region
  /// what contains data mapped from the specified file.
  ///
  /// \param[in] file
  ///     The name of the file whose load address we are looking for
  ///
  /// \param[out] is_loaded
  ///     \b True if the file is loaded into the memory and false
  ///     otherwise.
  ///
  /// \param[out] load_addr
  ///     The load address of the file if it is loaded into the
  ///     processes address space, LLDB_INVALID_ADDRESS otherwise.
  virtual Status GetFileLoadAddress(const FileSpec &file, bool &is_loaded,
                                    lldb::addr_t &load_addr) {
    return Status("Not supported");
  }

  /// Fetch process defined metadata.
  ///
  /// \return
  ///     A StructuredDataSP object which, if non-empty, will contain the
  ///     information related to the process.
  virtual StructuredData::DictionarySP GetMetadata() { return nullptr; }

  size_t AddImageToken(lldb::addr_t image_ptr);

  lldb::addr_t GetImagePtrFromToken(size_t token) const;

  void ResetImageToken(size_t token);

  /// Find the next branch instruction to set a breakpoint on
  ///
  /// When instruction stepping through a source line, instead of stepping
  /// through each instruction, we can put a breakpoint on the next branch
  /// instruction (within the range of instructions we are stepping through)
  /// and continue the process to there, yielding significant performance
  /// benefits over instruction stepping.
  ///
  /// \param[in] default_stop_addr
  ///     The address of the instruction where lldb would put a
  ///     breakpoint normally.
  ///
  /// \param[in] range_bounds
  ///     The range which the breakpoint must be contained within.
  ///     Typically a source line.
  ///
  /// \return
  ///     The address of the next branch instruction, or the end of
  ///     the range provided in range_bounds.  If there are any
  ///     problems with the disassembly or getting the instructions,
  ///     the original default_stop_addr will be returned.
  Address AdvanceAddressToNextBranchInstruction(Address default_stop_addr,
                                                AddressRange range_bounds);

  /// Configure asynchronous structured data feature.
  ///
  /// Each Process type that supports using an asynchronous StructuredData
  /// feature should implement this to enable/disable/configure the feature.
  /// The default implementation here will always return an error indiciating
  /// the feature is unsupported.
  ///
  /// StructuredDataPlugin implementations will call this to configure a
  /// feature that has been reported as being supported.
  ///
  /// \param[in] type_name
  ///     The StructuredData type name as previously discovered by
  ///     the Process-derived instance.
  ///
  /// \param[in] config_sp
  ///     Configuration data for the feature being enabled.  This config
  ///     data, which may be null, will be passed along to the feature
  ///     to process.  The feature will dictate whether this is a dictionary,
  ///     an array or some other object.  If the feature needs to be
  ///     set up properly before it can be enabled, then the config should
  ///     also take an enable/disable flag.
  ///
  /// \return
  ///     Returns the result of attempting to configure the feature.
  virtual Status
  ConfigureStructuredData(llvm::StringRef type_name,
                          const StructuredData::ObjectSP &config_sp);

  /// Broadcasts the given structured data object from the given plugin.
  ///
  /// StructuredDataPlugin instances can use this to optionally broadcast any
  /// of their data if they want to make it available for clients.  The data
  /// will come in on the structured data event bit
  /// (eBroadcastBitStructuredData).
  ///
  /// \param[in] object_sp
  ///     The structured data object to broadcast.
  ///
  /// \param[in] plugin_sp
  ///     The plugin that will be reported in the event's plugin
  ///     parameter.
  void BroadcastStructuredData(const StructuredData::ObjectSP &object_sp,
                               const lldb::StructuredDataPluginSP &plugin_sp);

  /// Returns the StructuredDataPlugin associated with a given type name, if
  /// there is one.
  ///
  /// There will only be a plugin for a given StructuredDataType if the
  /// debugged process monitor claims that the feature is supported. This is
  /// one way to tell whether a feature is available.
  ///
  /// \return
  ///     The plugin if one is available for the specified feature;
  ///     otherwise, returns an empty shared pointer.
  lldb::StructuredDataPluginSP
  GetStructuredDataPlugin(llvm::StringRef type_name) const;

  virtual void *GetImplementation() { return nullptr; }

  virtual void ForceScriptedState(lldb::StateType state) {}

  SourceManager::SourceFileCache &GetSourceFileCache() {
    return m_source_file_cache;
  }

  /// Find a pattern within a memory region.
  ///
  /// This function searches for a pattern represented by the provided buffer
  /// within the memory range specified by the low and high addresses. It uses
  /// a bad character heuristic to optimize the search process.
  ///
  /// \param[in] low The starting address of the memory region to be searched.
  /// (inclusive)
  ///
  /// \param[in] high The ending address of the memory region to be searched.
  /// (exclusive)
  ///
  /// \param[in] buf A pointer to the buffer containing the pattern to be
  /// searched.
  ///
  /// \param[in] buffer_size The size of the buffer in bytes.
  ///
  /// \return The address where the pattern was found or LLDB_INVALID_ADDRESS if
  /// not found.
  lldb::addr_t FindInMemory(lldb::addr_t low, lldb::addr_t high,
                            const uint8_t *buf, size_t size);

  AddressRanges FindRangesInMemory(const uint8_t *buf, uint64_t size,
                                   const AddressRanges &ranges,
                                   size_t alignment, size_t max_matches,
                                   Status &error);

  lldb::addr_t FindInMemory(const uint8_t *buf, uint64_t size,
                            const AddressRange &range, size_t alignment,
                            Status &error);

protected:
  friend class Trace;

  /// Construct with a shared pointer to a target, and the Process listener.
  /// Uses the Host UnixSignalsSP by default.
  Process(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp);

  /// Construct with a shared pointer to a target, the Process listener, and
  /// the appropriate UnixSignalsSP for the process.
  Process(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
          const lldb::UnixSignalsSP &unix_signals_sp);

  ///  Get the processor tracing type supported for this process.
  ///  Responses might be different depending on the architecture and
  ///  capabilities of the underlying OS.
  ///
  ///  \return
  ///     The supported trace type or an \a llvm::Error if tracing is
  ///     not supported for the inferior.
  virtual llvm::Expected<TraceSupportedResponse> TraceSupported();

  /// Start tracing a process or its threads.
  ///
  /// \param[in] request
  ///     JSON object with the information necessary to start tracing. In the
  ///     case of gdb-remote processes, this JSON object should conform to the
  ///     jLLDBTraceStart packet.
  ///
  /// \return
  ///     \a llvm::Error::success if the operation was successful, or
  ///     \a llvm::Error otherwise.
  virtual llvm::Error TraceStart(const llvm::json::Value &request) {
    return llvm::make_error<UnimplementedError>();
  }

  /// Stop tracing a live process or its threads.
  ///
  /// \param[in] request
  ///     The information determining which threads or process to stop tracing.
  ///
  /// \return
  ///     \a llvm::Error::success if the operation was successful, or
  ///     \a llvm::Error otherwise.
  virtual llvm::Error TraceStop(const TraceStopRequest &request) {
    return llvm::make_error<UnimplementedError>();
  }

  /// Get the current tracing state of the process and its threads.
  ///
  /// \param[in] type
  ///     Tracing technology type to consider.
  ///
  /// \return
  ///     A JSON object string with custom data depending on the trace
  ///     technology, or an \a llvm::Error in case of errors.
  virtual llvm::Expected<std::string> TraceGetState(llvm::StringRef type) {
    return llvm::make_error<UnimplementedError>();
  }

  /// Get binary data given a trace technology and a data identifier.
  ///
  /// \param[in] request
  ///     Object with the params of the requested data.
  ///
  /// \return
  ///     A vector of bytes with the requested data, or an \a llvm::Error in
  ///     case of failures.
  virtual llvm::Expected<std::vector<uint8_t>>
  TraceGetBinaryData(const TraceGetBinaryDataRequest &request) {
    return llvm::make_error<UnimplementedError>();
  }

  // This calls a function of the form "void * (*)(void)".
  bool CallVoidArgVoidPtrReturn(const Address *address,
                                lldb::addr_t &returned_func,
                                bool trap_exceptions = false);

  /// Update the thread list following process plug-in's specific logic.
  ///
  /// This method should only be invoked by \a UpdateThreadList.
  ///
  /// \return
  ///     \b true if the new thread list could be generated, \b false otherwise.
  virtual bool DoUpdateThreadList(ThreadList &old_thread_list,
                                  ThreadList &new_thread_list) = 0;

  /// Actually do the reading of memory from a process.
  ///
  /// Subclasses must override this function and can return fewer bytes than
  /// requested when memory requests are too large. This class will break up
  /// the memory requests and keep advancing the arguments along as needed.
  ///
  /// \param[in] vm_addr
  ///     A virtual load address that indicates where to start reading
  ///     memory from.
  ///
  /// \param[in] size
  ///     The number of bytes to read.
  ///
  /// \param[out] buf
  ///     A byte buffer that is at least \a size bytes long that
  ///     will receive the memory bytes.
  ///
  /// \param[out] error
  ///     An error that indicates the success or failure of this
  ///     operation. If error indicates success (error.Success()),
  ///     then the value returned can be trusted, otherwise zero
  ///     will be returned.
  ///
  /// \return
  ///     The number of bytes that were actually read into \a buf.
  ///     Zero is returned in the case of an error.
  virtual size_t DoReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                              Status &error) = 0;

  virtual void DoFindInMemory(lldb::addr_t start_addr, lldb::addr_t end_addr,
                              const uint8_t *buf, size_t size,
                              AddressRanges &matches, size_t alignment,
                              size_t max_matches);

  /// DoGetMemoryRegionInfo is called by GetMemoryRegionInfo after it has
  /// removed non address bits from load_addr. Override this method in
  /// subclasses of Process.
  ///
  /// See GetMemoryRegionInfo for details of the logic.
  ///
  /// \param[in] load_addr
  ///     The load address to query the range_info for. (non address bits
  ///     removed)
  ///
  /// \param[out] range_info
  ///     An range_info value containing the details of the range.
  ///
  /// \return
  ///     An error value.
  virtual Status DoGetMemoryRegionInfo(lldb::addr_t load_addr,
                                       MemoryRegionInfo &range_info) {
    return Status("Process::DoGetMemoryRegionInfo() not supported");
  }

  /// Provide an override value in the subclass for lldb's
  /// CPU-based logic for whether watchpoint exceptions are
  /// received before or after an instruction executes.
  ///
  /// If a Process subclass needs to override this architecture-based
  /// result, it may do so by overriding this method.
  ///
  /// \return
  ///     No boolean returned means there is no override of the
  ///     default architecture-based behavior.
  ///     true is returned for targets where watchpoints are reported
  ///     after the instruction has completed.
  ///     false is returned for targets where watchpoints are reported
  ///     before the instruction executes.
  virtual std::optional<bool> DoGetWatchpointReportedAfter() {
    return std::nullopt;
  }

  lldb::StateType GetPrivateState();

  /// The "private" side of resuming a process.  This doesn't alter the state
  /// of m_run_lock, but just causes the process to resume.
  ///
  /// \return
  ///     An Status object describing the success or failure of the resume.
  Status PrivateResume();

  // Called internally
  void CompleteAttach();

  // NextEventAction provides a way to register an action on the next event
  // that is delivered to this process.  There is currently only one next event
  // action allowed in the process at one time.  If a new "NextEventAction" is
  // added while one is already present, the old action will be discarded (with
  // HandleBeingUnshipped called after it is discarded.)
  //
  // If you want to resume the process as a result of a resume action, call
  // RequestResume, don't call Resume directly.
  class NextEventAction {
  public:
    enum EventActionResult {
      eEventActionSuccess,
      eEventActionRetry,
      eEventActionExit
    };

    NextEventAction(Process *process) : m_process(process) {}

    virtual ~NextEventAction() = default;

    virtual EventActionResult PerformAction(lldb::EventSP &event_sp) = 0;
    virtual void HandleBeingUnshipped() {}
    virtual EventActionResult HandleBeingInterrupted() = 0;
    virtual const char *GetExitString() = 0;
    void RequestResume() { m_process->m_resume_requested = true; }

  protected:
    Process *m_process;
  };

  void SetNextEventAction(Process::NextEventAction *next_event_action) {
    if (m_next_event_action_up)
      m_next_event_action_up->HandleBeingUnshipped();

    m_next_event_action_up.reset(next_event_action);
  }

  // This is the completer for Attaching:
  class AttachCompletionHandler : public NextEventAction {
  public:
    AttachCompletionHandler(Process *process, uint32_t exec_count);

    ~AttachCompletionHandler() override = default;

    EventActionResult PerformAction(lldb::EventSP &event_sp) override;
    EventActionResult HandleBeingInterrupted() override;
    const char *GetExitString() override;

  private:
    uint32_t m_exec_count;
    std::string m_exit_string;
  };

  bool PrivateStateThreadIsValid() const {
    lldb::StateType state = m_private_state.GetValue();
    return state != lldb::eStateInvalid && state != lldb::eStateDetached &&
           state != lldb::eStateExited && m_private_state_thread.IsJoinable();
  }

  void ForceNextEventDelivery() { m_force_next_event_delivery = true; }

  /// Loads any plugins associated with asynchronous structured data and maps
  /// the relevant supported type name to the plugin.
  ///
  /// Processes can receive asynchronous structured data from the process
  /// monitor.  This method will load and map any structured data plugins that
  /// support the given set of supported type names. Later, if any of these
  /// features are enabled, the process monitor is free to generate
  /// asynchronous structured data.  The data must come in as a single \b
  /// StructuredData::Dictionary.  That dictionary must have a string field
  /// named 'type', with a value that equals the relevant type name string
  /// (one of the values in \b supported_type_names).
  ///
  /// \param[in] supported_type_names
  ///     An array of zero or more type names.  Each must be unique.
  ///     For each entry in the list, a StructuredDataPlugin will be
  ///     searched for that supports the structured data type name.
  void MapSupportedStructuredDataPlugins(
      const StructuredData::Array &supported_type_names);

  /// Route the incoming structured data dictionary to the right plugin.
  ///
  /// The incoming structured data must be a dictionary, and it must have a
  /// key named 'type' that stores a string value.  The string value must be
  /// the name of the structured data feature that knows how to handle it.
  ///
  /// \param[in] object_sp
  ///     When non-null and pointing to a dictionary, the 'type'
  ///     key's string value is used to look up the plugin that
  ///     was registered for that structured data type.  It then
  ///     calls the following method on the StructuredDataPlugin
  ///     instance:
  ///
  ///     virtual void
  ///     HandleArrivalOfStructuredData(Process &process,
  ///                                   llvm::StringRef type_name,
  ///                                   const StructuredData::ObjectSP
  ///                                   &object_sp)
  ///
  /// \return
  ///     True if the structured data was routed to a plugin; otherwise,
  ///     false.
  bool RouteAsyncStructuredData(const StructuredData::ObjectSP object_sp);

  /// Check whether the process supports memory tagging.
  ///
  /// \return
  ///     true if the process supports memory tagging,
  ///     false otherwise.
  virtual bool SupportsMemoryTagging() { return false; }

  /// Does the final operation to read memory tags. E.g. sending a GDB packet.
  /// It assumes that ReadMemoryTags has checked that memory tagging is enabled
  /// and has expanded the memory range as needed.
  ///
  /// \param[in] addr
  ///    Start of address range to read memory tags for.
  ///
  /// \param[in] len
  ///    Length of the memory range to read tags for (in bytes).
  ///
  /// \param[in] type
  ///    Type of tags to read (get this from a MemoryTagManager)
  ///
  /// \return
  ///     The packed tag data received from the remote or an error
  ///     if the read failed.
  virtual llvm::Expected<std::vector<uint8_t>>
  DoReadMemoryTags(lldb::addr_t addr, size_t len, int32_t type) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        llvm::formatv("{0} does not support reading memory tags",
                      GetPluginName()));
  }

  /// Does the final operation to write memory tags. E.g. sending a GDB packet.
  /// It assumes that WriteMemoryTags has checked that memory tagging is enabled
  /// and has packed the tag data.
  ///
  /// \param[in] addr
  ///    Start of address range to write memory tags for.
  ///
  /// \param[in] len
  ///    Length of the memory range to write tags for (in bytes).
  ///
  /// \param[in] type
  ///    Type of tags to read (get this from a MemoryTagManager)
  ///
  /// \param[in] tags
  ///    Packed tags to be written.
  ///
  /// \return
  ///     Status telling you whether the write succeeded.
  virtual Status DoWriteMemoryTags(lldb::addr_t addr, size_t len, int32_t type,
                                   const std::vector<uint8_t> &tags) {
    Status status;
    status.SetErrorStringWithFormatv("{0} does not support writing memory tags",
                                     GetPluginName());
    return status;
  }

  // Type definitions
  typedef std::map<lldb::LanguageType, lldb::LanguageRuntimeSP>
      LanguageRuntimeCollection;

  struct PreResumeCallbackAndBaton {
    bool (*callback)(void *);
    void *baton;
    PreResumeCallbackAndBaton(PreResumeActionCallback in_callback,
                              void *in_baton)
        : callback(in_callback), baton(in_baton) {}
    bool operator== (const PreResumeCallbackAndBaton &rhs) {
      return callback == rhs.callback && baton == rhs.baton;
    }
  };

  // Member variables
  std::weak_ptr<Target> m_target_wp; ///< The target that owns this process.
  lldb::pid_t m_pid = LLDB_INVALID_PROCESS_ID;
  ThreadSafeValue<lldb::StateType> m_public_state;
  ThreadSafeValue<lldb::StateType>
      m_private_state;                     // The actual state of our process
  Broadcaster m_private_state_broadcaster; // This broadcaster feeds state
                                           // changed events into the private
                                           // state thread's listener.
  Broadcaster m_private_state_control_broadcaster; // This is the control
                                                   // broadcaster, used to
                                                   // pause, resume & stop the
                                                   // private state thread.
  lldb::ListenerSP m_private_state_listener_sp; // This is the listener for the
                                                // private state thread.
  HostThread m_private_state_thread; ///< Thread ID for the thread that watches
                                     ///internal state events
  ProcessModID m_mod_id; ///< Tracks the state of the process over stops and
                         ///other alterations.
  uint32_t m_process_unique_id; ///< Each lldb_private::Process class that is
                                ///created gets a unique integer ID that
                                ///increments with each new instance
  uint32_t m_thread_index_id;   ///< Each thread is created with a 1 based index
                                ///that won't get re-used.
  std::map<uint64_t, uint32_t> m_thread_id_to_index_id_map;
  int m_exit_status; ///< The exit status of the process, or -1 if not set.
  std::string m_exit_string; ///< A textual description of why a process exited.
  std::mutex m_exit_status_mutex; ///< Mutex so m_exit_status m_exit_string can
                                  ///be safely accessed from multiple threads
  std::recursive_mutex m_thread_mutex;
  ThreadList m_thread_list_real; ///< The threads for this process as are known
                                 ///to the protocol we are debugging with
  ThreadList m_thread_list; ///< The threads for this process as the user will
                            ///see them. This is usually the same as
  ///< m_thread_list_real, but might be different if there is an OS plug-in
  ///creating memory threads
  ThreadPlanStackMap m_thread_plans; ///< This is the list of thread plans for
                                     /// threads in m_thread_list, as well as
                                     /// threads we knew existed, but haven't
                                     /// determined that they have died yet.
  ThreadList
      m_extended_thread_list; ///< Constituent for extended threads that may be
                              /// generated, cleared on natural stops
  uint32_t m_extended_thread_stop_id; ///< The natural stop id when
                                      ///extended_thread_list was last updated
  QueueList
      m_queue_list; ///< The list of libdispatch queues at a given stop point
  uint32_t m_queue_list_stop_id; ///< The natural stop id when queue list was
                                 ///last fetched
  StopPointSiteList<lldb_private::WatchpointResource>
      m_watchpoint_resource_list; ///< Watchpoint resources currently in use.
  std::vector<Notifications> m_notifications; ///< The list of notifications
                                              ///that this process can deliver.
  std::vector<lldb::addr_t> m_image_tokens;
  StopPointSiteList<lldb_private::BreakpointSite>
      m_breakpoint_site_list; ///< This is the list of breakpoint
                              /// locations we intend to insert in
                              /// the target.
  lldb::DynamicLoaderUP m_dyld_up;
  lldb::JITLoaderListUP m_jit_loaders_up;
  lldb::DynamicCheckerFunctionsUP m_dynamic_checkers_up; ///< The functions used
                                                         /// by the expression
                                                         /// parser to validate
                                                         /// data that
                                                         /// expressions use.
  lldb::OperatingSystemUP m_os_up;
  lldb::SystemRuntimeUP m_system_runtime_up;
  lldb::UnixSignalsSP
      m_unix_signals_sp; /// This is the current signal set for this process.
  lldb::ABISP m_abi_sp;
  lldb::IOHandlerSP m_process_input_reader;
  mutable std::mutex m_process_input_reader_mutex;
  ThreadedCommunication m_stdio_communication;
  std::recursive_mutex m_stdio_communication_mutex;
  bool m_stdin_forward; /// Remember if stdin must be forwarded to remote debug
                        /// server
  std::string m_stdout_data;
  std::string m_stderr_data;
  std::recursive_mutex m_profile_data_comm_mutex;
  std::vector<std::string> m_profile_data;
  Predicate<uint32_t> m_iohandler_sync;
  MemoryCache m_memory_cache;
  AllocatedMemoryCache m_allocated_memory_cache;
  bool m_should_detach; /// Should we detach if the process object goes away
                        /// with an explicit call to Kill or Detach?
  LanguageRuntimeCollection m_language_runtimes;
  std::recursive_mutex m_language_runtimes_mutex;
  InstrumentationRuntimeCollection m_instrumentation_runtimes;
  std::unique_ptr<NextEventAction> m_next_event_action_up;
  std::vector<PreResumeCallbackAndBaton> m_pre_resume_actions;
  ProcessRunLock m_public_run_lock;
  ProcessRunLock m_private_run_lock;
  bool m_currently_handling_do_on_removals;
  bool m_resume_requested; // If m_currently_handling_event or
                           // m_currently_handling_do_on_removals are true,
                           // Resume will only request a resume, using this
                           // flag to check.

  /// This is set at the beginning of Process::Finalize() to stop functions
  /// from looking up or creating things during or after a finalize call.
  std::atomic<bool> m_finalizing;
  // When we are "Finalizing" we need to do some cleanup.  But if the Finalize
  // call is coming in the Destructor, we can't do any actual work in the
  // process because that is likely to call "shared_from_this" which crashes
  // if run while destructing.  We use this flag to determine that.
  std::atomic<bool> m_destructing;

  /// Mask for code an data addresses.
  /// The default value LLDB_INVALID_ADDRESS_MASK means no mask has been set,
  /// and addresses values should not be modified.
  /// In these masks, the bits are set to 1 indicate bits that are not
  /// significant for addressing.
  /// The highmem masks are for targets where we may have different masks
  /// for low memory versus high memory addresses, and they will be left
  /// as LLDB_INVALID_ADDRESS_MASK normally, meaning the base masks
  /// should be applied to all addresses.
  /// @{
  lldb::addr_t m_code_address_mask = LLDB_INVALID_ADDRESS_MASK;
  lldb::addr_t m_data_address_mask = LLDB_INVALID_ADDRESS_MASK;
  lldb::addr_t m_highmem_code_address_mask = LLDB_INVALID_ADDRESS_MASK;
  lldb::addr_t m_highmem_data_address_mask = LLDB_INVALID_ADDRESS_MASK;
  /// @}

  bool m_clear_thread_plans_on_stop;
  bool m_force_next_event_delivery;
  lldb::StateType m_last_broadcast_state; /// This helps with the Public event
                                          /// coalescing in
                                          /// ShouldBroadcastEvent.
  std::map<lldb::addr_t, lldb::addr_t> m_resolved_indirect_addresses;
  bool m_destroy_in_process;
  bool m_can_interpret_function_calls; // Some targets, e.g the OSX kernel,
                                       // don't support the ability to modify
                                       // the stack.
  std::mutex m_run_thread_plan_lock;
  llvm::StringMap<lldb::StructuredDataPluginSP> m_structured_data_plugin_map;

  enum { eCanJITDontKnow = 0, eCanJITYes, eCanJITNo } m_can_jit;

  std::unique_ptr<UtilityFunction> m_dlopen_utility_func_up;
  llvm::once_flag m_dlopen_utility_func_flag_once;

  /// Per process source file cache.
  SourceManager::SourceFileCache m_source_file_cache;

  size_t RemoveBreakpointOpcodesFromBuffer(lldb::addr_t addr, size_t size,
                                           uint8_t *buf) const;

  void SynchronouslyNotifyStateChanged(lldb::StateType state);

  void SetPublicState(lldb::StateType new_state, bool restarted);

  void SetPrivateState(lldb::StateType state);

  bool StartPrivateStateThread(bool is_secondary_thread = false);

  void StopPrivateStateThread();

  void PausePrivateStateThread();

  void ResumePrivateStateThread();

private:
  // The starts up the private state thread that will watch for events from the
  // debugee. Pass true for is_secondary_thread in the case where you have to
  // temporarily spin up a secondary state thread to handle events from a hand-
  // called function on the primary private state thread.

  lldb::thread_result_t RunPrivateStateThread(bool is_secondary_thread);

protected:
  void HandlePrivateEvent(lldb::EventSP &event_sp);

  Status HaltPrivate();

  lldb::StateType WaitForProcessStopPrivate(lldb::EventSP &event_sp,
                                            const Timeout<std::micro> &timeout);

  // This waits for both the state change broadcaster, and the control
  // broadcaster. If control_only, it only waits for the control broadcaster.

  bool GetEventsPrivate(lldb::EventSP &event_sp,
                        const Timeout<std::micro> &timeout, bool control_only);

  lldb::StateType
  GetStateChangedEventsPrivate(lldb::EventSP &event_sp,
                               const Timeout<std::micro> &timeout);

  size_t WriteMemoryPrivate(lldb::addr_t addr, const void *buf, size_t size,
                            Status &error);

  void AppendSTDOUT(const char *s, size_t len);

  void AppendSTDERR(const char *s, size_t len);

  void BroadcastAsyncProfileData(const std::string &one_profile_data);

  static void STDIOReadThreadBytesReceived(void *baton, const void *src,
                                           size_t src_len);

  bool PushProcessIOHandler();

  bool PopProcessIOHandler();

  bool ProcessIOHandlerIsActive();

  bool ProcessIOHandlerExists() const {
    std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
    return static_cast<bool>(m_process_input_reader);
  }

  Status StopForDestroyOrDetach(lldb::EventSP &exit_event_sp);

  virtual Status UpdateAutomaticSignalFiltering();

  void LoadOperatingSystemPlugin(bool flush);

  void SetAddressableBitMasks(AddressableBits bit_masks);

private:
  Status DestroyImpl(bool force_kill);

  /// This is the part of the event handling that for a process event. It
  /// decides what to do with the event and returns true if the event needs to
  /// be propagated to the user, and false otherwise. If the event is not
  /// propagated, this call will most likely set the target to executing
  /// again. There is only one place where this call should be called,
  /// HandlePrivateEvent. Don't call it from anywhere else...
  ///
  /// \param[in] event_ptr
  ///     This is the event we are handling.
  ///
  /// \return
  ///     Returns \b true if the event should be reported to the
  ///     user, \b false otherwise.
  bool ShouldBroadcastEvent(Event *event_ptr);

  void ControlPrivateStateThread(uint32_t signal);

  Status LaunchPrivate(ProcessLaunchInfo &launch_info, lldb::StateType &state,
                       lldb::EventSP &event_sp);

  lldb::EventSP CreateEventFromProcessState(uint32_t event_type);

  Process(const Process &) = delete;
  const Process &operator=(const Process &) = delete;
};

/// RAII guard that should be acquired when an utility function is called within
/// a given process.
class UtilityFunctionScope {
  Process *m_process;

public:
  UtilityFunctionScope(Process *p) : m_process(p) {
    if (m_process)
      m_process->SetRunningUtilityFunction(true);
  }
  ~UtilityFunctionScope() {
    if (m_process)
      m_process->SetRunningUtilityFunction(false);
  }
};

} // namespace lldb_private

#endif // LLDB_TARGET_PROCESS_H
