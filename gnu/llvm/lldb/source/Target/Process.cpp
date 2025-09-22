//===-- Process.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/DynamicCheckerFunctions.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/AssertFrameRecognizer.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Target/JITLoader.h"
#include "lldb/Target/JITLoaderList.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/MemoryHistory.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/StructuredDataPlugin.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanBase.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Target/ThreadPlanStack.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Target/VerboseTrapFrameRecognizer.h"
#include "lldb/Utility/AddressableBits.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/SelectHelper.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;
using namespace std::chrono;

// Comment out line below to disable memory caching, overriding the process
// setting target.process.disable-memory-cache
#define ENABLE_MEMORY_CACHING

#ifdef ENABLE_MEMORY_CACHING
#define DISABLE_MEM_CACHE_DEFAULT false
#else
#define DISABLE_MEM_CACHE_DEFAULT true
#endif

class ProcessOptionValueProperties
    : public Cloneable<ProcessOptionValueProperties, OptionValueProperties> {
public:
  ProcessOptionValueProperties(llvm::StringRef name) : Cloneable(name) {}

  const Property *
  GetPropertyAtIndex(size_t idx,
                     const ExecutionContext *exe_ctx) const override {
    // When getting the value for a key from the process options, we will
    // always try and grab the setting from the current process if there is
    // one. Else we just use the one from this instance.
    if (exe_ctx) {
      Process *process = exe_ctx->GetProcessPtr();
      if (process) {
        ProcessOptionValueProperties *instance_properties =
            static_cast<ProcessOptionValueProperties *>(
                process->GetValueProperties().get());
        if (this != instance_properties)
          return instance_properties->ProtectedGetPropertyAtIndex(idx);
      }
    }
    return ProtectedGetPropertyAtIndex(idx);
  }
};

class ProcessMemoryIterator {
public:
  ProcessMemoryIterator(Process &process, lldb::addr_t base)
      : m_process(process), m_base_addr(base) {}

  bool IsValid() { return m_is_valid; }

  uint8_t operator[](lldb::addr_t offset) {
    if (!IsValid())
      return 0;

    uint8_t retval = 0;
    Status error;
    if (0 == m_process.ReadMemory(m_base_addr + offset, &retval, 1, error)) {
      m_is_valid = false;
      return 0;
    }

    return retval;
  }

private:
  Process &m_process;
  const lldb::addr_t m_base_addr;
  bool m_is_valid = true;
};

static constexpr OptionEnumValueElement g_follow_fork_mode_values[] = {
    {
        eFollowParent,
        "parent",
        "Continue tracing the parent process and detach the child.",
    },
    {
        eFollowChild,
        "child",
        "Trace the child process and detach the parent.",
    },
};

#define LLDB_PROPERTIES_process
#include "TargetProperties.inc"

enum {
#define LLDB_PROPERTIES_process
#include "TargetPropertiesEnum.inc"
  ePropertyExperimental,
};

#define LLDB_PROPERTIES_process_experimental
#include "TargetProperties.inc"

enum {
#define LLDB_PROPERTIES_process_experimental
#include "TargetPropertiesEnum.inc"
};

class ProcessExperimentalOptionValueProperties
    : public Cloneable<ProcessExperimentalOptionValueProperties,
                       OptionValueProperties> {
public:
  ProcessExperimentalOptionValueProperties()
      : Cloneable(Properties::GetExperimentalSettingsName()) {}
};

ProcessExperimentalProperties::ProcessExperimentalProperties()
    : Properties(OptionValuePropertiesSP(
          new ProcessExperimentalOptionValueProperties())) {
  m_collection_sp->Initialize(g_process_experimental_properties);
}

ProcessProperties::ProcessProperties(lldb_private::Process *process)
    : Properties(),
      m_process(process) // Can be nullptr for global ProcessProperties
{
  if (process == nullptr) {
    // Global process properties, set them up one time
    m_collection_sp = std::make_shared<ProcessOptionValueProperties>("process");
    m_collection_sp->Initialize(g_process_properties);
    m_collection_sp->AppendProperty(
        "thread", "Settings specific to threads.", true,
        Thread::GetGlobalProperties().GetValueProperties());
  } else {
    m_collection_sp =
        OptionValueProperties::CreateLocalCopy(Process::GetGlobalProperties());
    m_collection_sp->SetValueChangedCallback(
        ePropertyPythonOSPluginPath,
        [this] { m_process->LoadOperatingSystemPlugin(true); });
  }

  m_experimental_properties_up =
      std::make_unique<ProcessExperimentalProperties>();
  m_collection_sp->AppendProperty(
      Properties::GetExperimentalSettingsName(),
      "Experimental settings - setting these won't produce "
      "errors if the setting is not present.",
      true, m_experimental_properties_up->GetValueProperties());
}

ProcessProperties::~ProcessProperties() = default;

bool ProcessProperties::GetDisableMemoryCache() const {
  const uint32_t idx = ePropertyDisableMemCache;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

uint64_t ProcessProperties::GetMemoryCacheLineSize() const {
  const uint32_t idx = ePropertyMemCacheLineSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_process_properties[idx].default_uint_value);
}

Args ProcessProperties::GetExtraStartupCommands() const {
  Args args;
  const uint32_t idx = ePropertyExtraStartCommand;
  m_collection_sp->GetPropertyAtIndexAsArgs(idx, args);
  return args;
}

void ProcessProperties::SetExtraStartupCommands(const Args &args) {
  const uint32_t idx = ePropertyExtraStartCommand;
  m_collection_sp->SetPropertyAtIndexFromArgs(idx, args);
}

FileSpec ProcessProperties::GetPythonOSPluginPath() const {
  const uint32_t idx = ePropertyPythonOSPluginPath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

uint32_t ProcessProperties::GetVirtualAddressableBits() const {
  const uint32_t idx = ePropertyVirtualAddressableBits;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_process_properties[idx].default_uint_value);
}

void ProcessProperties::SetVirtualAddressableBits(uint32_t bits) {
  const uint32_t idx = ePropertyVirtualAddressableBits;
  SetPropertyAtIndex(idx, static_cast<uint64_t>(bits));
}

uint32_t ProcessProperties::GetHighmemVirtualAddressableBits() const {
  const uint32_t idx = ePropertyHighmemVirtualAddressableBits;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_process_properties[idx].default_uint_value);
}

void ProcessProperties::SetHighmemVirtualAddressableBits(uint32_t bits) {
  const uint32_t idx = ePropertyHighmemVirtualAddressableBits;
  SetPropertyAtIndex(idx, static_cast<uint64_t>(bits));
}

void ProcessProperties::SetPythonOSPluginPath(const FileSpec &file) {
  const uint32_t idx = ePropertyPythonOSPluginPath;
  SetPropertyAtIndex(idx, file);
}

bool ProcessProperties::GetIgnoreBreakpointsInExpressions() const {
  const uint32_t idx = ePropertyIgnoreBreakpointsInExpressions;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

void ProcessProperties::SetIgnoreBreakpointsInExpressions(bool ignore) {
  const uint32_t idx = ePropertyIgnoreBreakpointsInExpressions;
  SetPropertyAtIndex(idx, ignore);
}

bool ProcessProperties::GetUnwindOnErrorInExpressions() const {
  const uint32_t idx = ePropertyUnwindOnErrorInExpressions;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

void ProcessProperties::SetUnwindOnErrorInExpressions(bool ignore) {
  const uint32_t idx = ePropertyUnwindOnErrorInExpressions;
  SetPropertyAtIndex(idx, ignore);
}

bool ProcessProperties::GetStopOnSharedLibraryEvents() const {
  const uint32_t idx = ePropertyStopOnSharedLibraryEvents;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

void ProcessProperties::SetStopOnSharedLibraryEvents(bool stop) {
  const uint32_t idx = ePropertyStopOnSharedLibraryEvents;
  SetPropertyAtIndex(idx, stop);
}

bool ProcessProperties::GetDisableLangRuntimeUnwindPlans() const {
  const uint32_t idx = ePropertyDisableLangRuntimeUnwindPlans;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

void ProcessProperties::SetDisableLangRuntimeUnwindPlans(bool disable) {
  const uint32_t idx = ePropertyDisableLangRuntimeUnwindPlans;
  SetPropertyAtIndex(idx, disable);
  m_process->Flush();
}

bool ProcessProperties::GetDetachKeepsStopped() const {
  const uint32_t idx = ePropertyDetachKeepsStopped;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

void ProcessProperties::SetDetachKeepsStopped(bool stop) {
  const uint32_t idx = ePropertyDetachKeepsStopped;
  SetPropertyAtIndex(idx, stop);
}

bool ProcessProperties::GetWarningsOptimization() const {
  const uint32_t idx = ePropertyWarningOptimization;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

bool ProcessProperties::GetWarningsUnsupportedLanguage() const {
  const uint32_t idx = ePropertyWarningUnsupportedLanguage;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

bool ProcessProperties::GetStopOnExec() const {
  const uint32_t idx = ePropertyStopOnExec;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

std::chrono::seconds ProcessProperties::GetUtilityExpressionTimeout() const {
  const uint32_t idx = ePropertyUtilityExpressionTimeout;
  uint64_t value = GetPropertyAtIndexAs<uint64_t>(
      idx, g_process_properties[idx].default_uint_value);
  return std::chrono::seconds(value);
}

std::chrono::seconds ProcessProperties::GetInterruptTimeout() const {
  const uint32_t idx = ePropertyInterruptTimeout;
  uint64_t value = GetPropertyAtIndexAs<uint64_t>(
      idx, g_process_properties[idx].default_uint_value);
  return std::chrono::seconds(value);
}

bool ProcessProperties::GetSteppingRunsAllThreads() const {
  const uint32_t idx = ePropertySteppingRunsAllThreads;
  return GetPropertyAtIndexAs<bool>(
      idx, g_process_properties[idx].default_uint_value != 0);
}

bool ProcessProperties::GetOSPluginReportsAllThreads() const {
  const bool fail_value = true;
  const Property *exp_property =
      m_collection_sp->GetPropertyAtIndex(ePropertyExperimental);
  OptionValueProperties *exp_values =
      exp_property->GetValue()->GetAsProperties();
  if (!exp_values)
    return fail_value;

  return exp_values
      ->GetPropertyAtIndexAs<bool>(ePropertyOSPluginReportsAllThreads)
      .value_or(fail_value);
}

void ProcessProperties::SetOSPluginReportsAllThreads(bool does_report) {
  const Property *exp_property =
      m_collection_sp->GetPropertyAtIndex(ePropertyExperimental);
  OptionValueProperties *exp_values =
      exp_property->GetValue()->GetAsProperties();
  if (exp_values)
    exp_values->SetPropertyAtIndex(ePropertyOSPluginReportsAllThreads,
                                   does_report);
}

FollowForkMode ProcessProperties::GetFollowForkMode() const {
  const uint32_t idx = ePropertyFollowForkMode;
  return GetPropertyAtIndexAs<FollowForkMode>(
      idx, static_cast<FollowForkMode>(
               g_process_properties[idx].default_uint_value));
}

ProcessSP Process::FindPlugin(lldb::TargetSP target_sp,
                              llvm::StringRef plugin_name,
                              ListenerSP listener_sp,
                              const FileSpec *crash_file_path,
                              bool can_connect) {
  static uint32_t g_process_unique_id = 0;

  ProcessSP process_sp;
  ProcessCreateInstance create_callback = nullptr;
  if (!plugin_name.empty()) {
    create_callback =
        PluginManager::GetProcessCreateCallbackForPluginName(plugin_name);
    if (create_callback) {
      process_sp = create_callback(target_sp, listener_sp, crash_file_path,
                                   can_connect);
      if (process_sp) {
        if (process_sp->CanDebug(target_sp, true)) {
          process_sp->m_process_unique_id = ++g_process_unique_id;
        } else
          process_sp.reset();
      }
    }
  } else {
    for (uint32_t idx = 0;
         (create_callback =
              PluginManager::GetProcessCreateCallbackAtIndex(idx)) != nullptr;
         ++idx) {
      process_sp = create_callback(target_sp, listener_sp, crash_file_path,
                                   can_connect);
      if (process_sp) {
        if (process_sp->CanDebug(target_sp, false)) {
          process_sp->m_process_unique_id = ++g_process_unique_id;
          break;
        } else
          process_sp.reset();
      }
    }
  }
  return process_sp;
}

llvm::StringRef Process::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.process");
  return class_name;
}

Process::Process(lldb::TargetSP target_sp, ListenerSP listener_sp)
    : Process(target_sp, listener_sp, UnixSignals::CreateForHost()) {
  // This constructor just delegates to the full Process constructor,
  // defaulting to using the Host's UnixSignals.
}

Process::Process(lldb::TargetSP target_sp, ListenerSP listener_sp,
                 const UnixSignalsSP &unix_signals_sp)
    : ProcessProperties(this),
      Broadcaster((target_sp->GetDebugger().GetBroadcasterManager()),
                  Process::GetStaticBroadcasterClass().str()),
      m_target_wp(target_sp), m_public_state(eStateUnloaded),
      m_private_state(eStateUnloaded),
      m_private_state_broadcaster(nullptr,
                                  "lldb.process.internal_state_broadcaster"),
      m_private_state_control_broadcaster(
          nullptr, "lldb.process.internal_state_control_broadcaster"),
      m_private_state_listener_sp(
          Listener::MakeListener("lldb.process.internal_state_listener")),
      m_mod_id(), m_process_unique_id(0), m_thread_index_id(0),
      m_thread_id_to_index_id_map(), m_exit_status(-1),
      m_thread_list_real(*this), m_thread_list(*this), m_thread_plans(*this),
      m_extended_thread_list(*this), m_extended_thread_stop_id(0),
      m_queue_list(this), m_queue_list_stop_id(0),
      m_unix_signals_sp(unix_signals_sp), m_abi_sp(), m_process_input_reader(),
      m_stdio_communication("process.stdio"), m_stdio_communication_mutex(),
      m_stdin_forward(false), m_stdout_data(), m_stderr_data(),
      m_profile_data_comm_mutex(), m_profile_data(), m_iohandler_sync(0),
      m_memory_cache(*this), m_allocated_memory_cache(*this),
      m_should_detach(false), m_next_event_action_up(), m_public_run_lock(),
      m_private_run_lock(), m_currently_handling_do_on_removals(false),
      m_resume_requested(false), m_finalizing(false), m_destructing(false),
      m_clear_thread_plans_on_stop(false), m_force_next_event_delivery(false),
      m_last_broadcast_state(eStateInvalid), m_destroy_in_process(false),
      m_can_interpret_function_calls(false), m_run_thread_plan_lock(),
      m_can_jit(eCanJITDontKnow) {
  CheckInWithManager();

  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p Process::Process()", static_cast<void *>(this));

  if (!m_unix_signals_sp)
    m_unix_signals_sp = std::make_shared<UnixSignals>();

  SetEventName(eBroadcastBitStateChanged, "state-changed");
  SetEventName(eBroadcastBitInterrupt, "interrupt");
  SetEventName(eBroadcastBitSTDOUT, "stdout-available");
  SetEventName(eBroadcastBitSTDERR, "stderr-available");
  SetEventName(eBroadcastBitProfileData, "profile-data-available");
  SetEventName(eBroadcastBitStructuredData, "structured-data-available");

  m_private_state_control_broadcaster.SetEventName(
      eBroadcastInternalStateControlStop, "control-stop");
  m_private_state_control_broadcaster.SetEventName(
      eBroadcastInternalStateControlPause, "control-pause");
  m_private_state_control_broadcaster.SetEventName(
      eBroadcastInternalStateControlResume, "control-resume");

  // The listener passed into process creation is the primary listener:
  // It always listens for all the event bits for Process:
  SetPrimaryListener(listener_sp);

  m_private_state_listener_sp->StartListeningForEvents(
      &m_private_state_broadcaster,
      eBroadcastBitStateChanged | eBroadcastBitInterrupt);

  m_private_state_listener_sp->StartListeningForEvents(
      &m_private_state_control_broadcaster,
      eBroadcastInternalStateControlStop | eBroadcastInternalStateControlPause |
          eBroadcastInternalStateControlResume);
  // We need something valid here, even if just the default UnixSignalsSP.
  assert(m_unix_signals_sp && "null m_unix_signals_sp after initialization");

  // Allow the platform to override the default cache line size
  OptionValueSP value_sp =
      m_collection_sp->GetPropertyAtIndex(ePropertyMemCacheLineSize)
          ->GetValue();
  uint64_t platform_cache_line_size =
      target_sp->GetPlatform()->GetDefaultMemoryCacheLineSize();
  if (!value_sp->OptionWasSet() && platform_cache_line_size != 0)
    value_sp->SetValueAs(platform_cache_line_size);

  // FIXME: Frame recognizer registration should not be done in Target.
  // We should have a plugin do the registration instead, for example, a
  // common C LanguageRuntime plugin.
  RegisterAssertFrameRecognizer(this);
  RegisterVerboseTrapFrameRecognizer(*this);
}

Process::~Process() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p Process::~Process()", static_cast<void *>(this));
  StopPrivateStateThread();

  // ThreadList::Clear() will try to acquire this process's mutex, so
  // explicitly clear the thread list here to ensure that the mutex is not
  // destroyed before the thread list.
  m_thread_list.Clear();
}

ProcessProperties &Process::GetGlobalProperties() {
  // NOTE: intentional leak so we don't crash if global destructor chain gets
  // called as other threads still use the result of this function
  static ProcessProperties *g_settings_ptr =
      new ProcessProperties(nullptr);
  return *g_settings_ptr;
}

void Process::Finalize(bool destructing) {
  if (m_finalizing.exchange(true))
    return;
  if (destructing)
    m_destructing.exchange(true);

  // Destroy the process. This will call the virtual function DoDestroy under
  // the hood, giving our derived class a chance to do the ncessary tear down.
  DestroyImpl(false);

  // Clear our broadcaster before we proceed with destroying
  Broadcaster::Clear();

  // Do any cleanup needed prior to being destructed... Subclasses that
  // override this method should call this superclass method as well.

  // We need to destroy the loader before the derived Process class gets
  // destroyed since it is very likely that undoing the loader will require
  // access to the real process.
  m_dynamic_checkers_up.reset();
  m_abi_sp.reset();
  m_os_up.reset();
  m_system_runtime_up.reset();
  m_dyld_up.reset();
  m_jit_loaders_up.reset();
  m_thread_plans.Clear();
  m_thread_list_real.Destroy();
  m_thread_list.Destroy();
  m_extended_thread_list.Destroy();
  m_queue_list.Clear();
  m_queue_list_stop_id = 0;
  m_watchpoint_resource_list.Clear();
  std::vector<Notifications> empty_notifications;
  m_notifications.swap(empty_notifications);
  m_image_tokens.clear();
  m_memory_cache.Clear();
  m_allocated_memory_cache.Clear(/*deallocate_memory=*/true);
  {
    std::lock_guard<std::recursive_mutex> guard(m_language_runtimes_mutex);
    m_language_runtimes.clear();
  }
  m_instrumentation_runtimes.clear();
  m_next_event_action_up.reset();
  // Clear the last natural stop ID since it has a strong reference to this
  // process
  m_mod_id.SetStopEventForLastNaturalStopID(EventSP());
  // We have to be very careful here as the m_private_state_listener might
  // contain events that have ProcessSP values in them which can keep this
  // process around forever. These events need to be cleared out.
  m_private_state_listener_sp->Clear();
  m_public_run_lock.TrySetRunning(); // This will do nothing if already locked
  m_public_run_lock.SetStopped();
  m_private_run_lock.TrySetRunning(); // This will do nothing if already locked
  m_private_run_lock.SetStopped();
  m_structured_data_plugin_map.clear();
}

void Process::RegisterNotificationCallbacks(const Notifications &callbacks) {
  m_notifications.push_back(callbacks);
  if (callbacks.initialize != nullptr)
    callbacks.initialize(callbacks.baton, this);
}

bool Process::UnregisterNotificationCallbacks(const Notifications &callbacks) {
  std::vector<Notifications>::iterator pos, end = m_notifications.end();
  for (pos = m_notifications.begin(); pos != end; ++pos) {
    if (pos->baton == callbacks.baton &&
        pos->initialize == callbacks.initialize &&
        pos->process_state_changed == callbacks.process_state_changed) {
      m_notifications.erase(pos);
      return true;
    }
  }
  return false;
}

void Process::SynchronouslyNotifyStateChanged(StateType state) {
  std::vector<Notifications>::iterator notification_pos,
      notification_end = m_notifications.end();
  for (notification_pos = m_notifications.begin();
       notification_pos != notification_end; ++notification_pos) {
    if (notification_pos->process_state_changed)
      notification_pos->process_state_changed(notification_pos->baton, this,
                                              state);
  }
}

// FIXME: We need to do some work on events before the general Listener sees
// them.
// For instance if we are continuing from a breakpoint, we need to ensure that
// we do the little "insert real insn, step & stop" trick.  But we can't do
// that when the event is delivered by the broadcaster - since that is done on
// the thread that is waiting for new events, so if we needed more than one
// event for our handling, we would stall.  So instead we do it when we fetch
// the event off of the queue.
//

StateType Process::GetNextEvent(EventSP &event_sp) {
  StateType state = eStateInvalid;

  if (GetPrimaryListener()->GetEventForBroadcaster(this, event_sp,
                                            std::chrono::seconds(0)) &&
      event_sp)
    state = Process::ProcessEventData::GetStateFromEvent(event_sp.get());

  return state;
}

void Process::SyncIOHandler(uint32_t iohandler_id,
                            const Timeout<std::micro> &timeout) {
  // don't sync (potentially context switch) in case where there is no process
  // IO
  if (!ProcessIOHandlerExists())
    return;

  auto Result = m_iohandler_sync.WaitForValueNotEqualTo(iohandler_id, timeout);

  Log *log = GetLog(LLDBLog::Process);
  if (Result) {
    LLDB_LOG(
        log,
        "waited from m_iohandler_sync to change from {0}. New value is {1}.",
        iohandler_id, *Result);
  } else {
    LLDB_LOG(log, "timed out waiting for m_iohandler_sync to change from {0}.",
             iohandler_id);
  }
}

StateType Process::WaitForProcessToStop(
    const Timeout<std::micro> &timeout, EventSP *event_sp_ptr, bool wait_always,
    ListenerSP hijack_listener_sp, Stream *stream, bool use_run_lock,
    SelectMostRelevant select_most_relevant) {
  // We can't just wait for a "stopped" event, because the stopped event may
  // have restarted the target. We have to actually check each event, and in
  // the case of a stopped event check the restarted flag on the event.
  if (event_sp_ptr)
    event_sp_ptr->reset();
  StateType state = GetState();
  // If we are exited or detached, we won't ever get back to any other valid
  // state...
  if (state == eStateDetached || state == eStateExited)
    return state;

  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "timeout = {0}", timeout);

  if (!wait_always && StateIsStoppedState(state, true) &&
      StateIsStoppedState(GetPrivateState(), true)) {
    LLDB_LOGF(log,
              "Process::%s returning without waiting for events; process "
              "private and public states are already 'stopped'.",
              __FUNCTION__);
    // We need to toggle the run lock as this won't get done in
    // SetPublicState() if the process is hijacked.
    if (hijack_listener_sp && use_run_lock)
      m_public_run_lock.SetStopped();
    return state;
  }

  while (state != eStateInvalid) {
    EventSP event_sp;
    state = GetStateChangedEvents(event_sp, timeout, hijack_listener_sp);
    if (event_sp_ptr && event_sp)
      *event_sp_ptr = event_sp;

    bool pop_process_io_handler = (hijack_listener_sp.get() != nullptr);
    Process::HandleProcessStateChangedEvent(
        event_sp, stream, select_most_relevant, pop_process_io_handler);

    switch (state) {
    case eStateCrashed:
    case eStateDetached:
    case eStateExited:
    case eStateUnloaded:
      // We need to toggle the run lock as this won't get done in
      // SetPublicState() if the process is hijacked.
      if (hijack_listener_sp && use_run_lock)
        m_public_run_lock.SetStopped();
      return state;
    case eStateStopped:
      if (Process::ProcessEventData::GetRestartedFromEvent(event_sp.get()))
        continue;
      else {
        // We need to toggle the run lock as this won't get done in
        // SetPublicState() if the process is hijacked.
        if (hijack_listener_sp && use_run_lock)
          m_public_run_lock.SetStopped();
        return state;
      }
    default:
      continue;
    }
  }
  return state;
}

bool Process::HandleProcessStateChangedEvent(
    const EventSP &event_sp, Stream *stream,
    SelectMostRelevant select_most_relevant,
    bool &pop_process_io_handler) {
  const bool handle_pop = pop_process_io_handler;

  pop_process_io_handler = false;
  ProcessSP process_sp =
      Process::ProcessEventData::GetProcessFromEvent(event_sp.get());

  if (!process_sp)
    return false;

  StateType event_state =
      Process::ProcessEventData::GetStateFromEvent(event_sp.get());
  if (event_state == eStateInvalid)
    return false;

  switch (event_state) {
  case eStateInvalid:
  case eStateUnloaded:
  case eStateAttaching:
  case eStateLaunching:
  case eStateStepping:
  case eStateDetached:
    if (stream)
      stream->Printf("Process %" PRIu64 " %s\n", process_sp->GetID(),
                     StateAsCString(event_state));
    if (event_state == eStateDetached)
      pop_process_io_handler = true;
    break;

  case eStateConnected:
  case eStateRunning:
    // Don't be chatty when we run...
    break;

  case eStateExited:
    if (stream)
      process_sp->GetStatus(*stream);
    pop_process_io_handler = true;
    break;

  case eStateStopped:
  case eStateCrashed:
  case eStateSuspended:
    // Make sure the program hasn't been auto-restarted:
    if (Process::ProcessEventData::GetRestartedFromEvent(event_sp.get())) {
      if (stream) {
        size_t num_reasons =
            Process::ProcessEventData::GetNumRestartedReasons(event_sp.get());
        if (num_reasons > 0) {
          // FIXME: Do we want to report this, or would that just be annoyingly
          // chatty?
          if (num_reasons == 1) {
            const char *reason =
                Process::ProcessEventData::GetRestartedReasonAtIndex(
                    event_sp.get(), 0);
            stream->Printf("Process %" PRIu64 " stopped and restarted: %s\n",
                           process_sp->GetID(),
                           reason ? reason : "<UNKNOWN REASON>");
          } else {
            stream->Printf("Process %" PRIu64
                           " stopped and restarted, reasons:\n",
                           process_sp->GetID());

            for (size_t i = 0; i < num_reasons; i++) {
              const char *reason =
                  Process::ProcessEventData::GetRestartedReasonAtIndex(
                      event_sp.get(), i);
              stream->Printf("\t%s\n", reason ? reason : "<UNKNOWN REASON>");
            }
          }
        }
      }
    } else {
      StopInfoSP curr_thread_stop_info_sp;
      // Lock the thread list so it doesn't change on us, this is the scope for
      // the locker:
      {
        ThreadList &thread_list = process_sp->GetThreadList();
        std::lock_guard<std::recursive_mutex> guard(thread_list.GetMutex());

        ThreadSP curr_thread(thread_list.GetSelectedThread());
        ThreadSP thread;
        StopReason curr_thread_stop_reason = eStopReasonInvalid;
        bool prefer_curr_thread = false;
        if (curr_thread && curr_thread->IsValid()) {
          curr_thread_stop_reason = curr_thread->GetStopReason();
          switch (curr_thread_stop_reason) {
          case eStopReasonNone:
          case eStopReasonInvalid:
            // Don't prefer the current thread if it didn't stop for a reason.
            break;
          case eStopReasonSignal: {
            // We need to do the same computation we do for other threads
            // below in case the current thread happens to be the one that
            // stopped for the no-stop signal.
            uint64_t signo = curr_thread->GetStopInfo()->GetValue();
            if (process_sp->GetUnixSignals()->GetShouldStop(signo))
              prefer_curr_thread = true;
          } break;
          default:
            prefer_curr_thread = true;
            break;
          }
          curr_thread_stop_info_sp = curr_thread->GetStopInfo();
        }

        if (!prefer_curr_thread) {
          // Prefer a thread that has just completed its plan over another
          // thread as current thread.
          ThreadSP plan_thread;
          ThreadSP other_thread;

          const size_t num_threads = thread_list.GetSize();
          size_t i;
          for (i = 0; i < num_threads; ++i) {
            thread = thread_list.GetThreadAtIndex(i);
            StopReason thread_stop_reason = thread->GetStopReason();
            switch (thread_stop_reason) {
            case eStopReasonInvalid:
            case eStopReasonNone:
              break;

            case eStopReasonSignal: {
              // Don't select a signal thread if we weren't going to stop at
              // that signal.  We have to have had another reason for stopping
              // here, and the user doesn't want to see this thread.
              uint64_t signo = thread->GetStopInfo()->GetValue();
              if (process_sp->GetUnixSignals()->GetShouldStop(signo)) {
                if (!other_thread)
                  other_thread = thread;
              }
              break;
            }
            case eStopReasonTrace:
            case eStopReasonBreakpoint:
            case eStopReasonWatchpoint:
            case eStopReasonException:
            case eStopReasonExec:
            case eStopReasonFork:
            case eStopReasonVFork:
            case eStopReasonVForkDone:
            case eStopReasonThreadExiting:
            case eStopReasonInstrumentation:
            case eStopReasonProcessorTrace:
              if (!other_thread)
                other_thread = thread;
              break;
            case eStopReasonPlanComplete:
              if (!plan_thread)
                plan_thread = thread;
              break;
            }
          }
          if (plan_thread)
            thread_list.SetSelectedThreadByID(plan_thread->GetID());
          else if (other_thread)
            thread_list.SetSelectedThreadByID(other_thread->GetID());
          else {
            if (curr_thread && curr_thread->IsValid())
              thread = curr_thread;
            else
              thread = thread_list.GetThreadAtIndex(0);

            if (thread)
              thread_list.SetSelectedThreadByID(thread->GetID());
          }
        }
      }
      // Drop the ThreadList mutex by here, since GetThreadStatus below might
      // have to run code, e.g. for Data formatters, and if we hold the
      // ThreadList mutex, then the process is going to have a hard time
      // restarting the process.
      if (stream) {
        Debugger &debugger = process_sp->GetTarget().GetDebugger();
        if (debugger.GetTargetList().GetSelectedTarget().get() ==
            &process_sp->GetTarget()) {
          ThreadSP thread_sp = process_sp->GetThreadList().GetSelectedThread();

          if (!thread_sp || !thread_sp->IsValid())
            return false;

          const bool only_threads_with_stop_reason = true;
          const uint32_t start_frame =
              thread_sp->GetSelectedFrameIndex(select_most_relevant);
          const uint32_t num_frames = 1;
          const uint32_t num_frames_with_source = 1;
          const bool stop_format = true;

          process_sp->GetStatus(*stream);
          process_sp->GetThreadStatus(*stream, only_threads_with_stop_reason,
                                      start_frame, num_frames,
                                      num_frames_with_source,
                                      stop_format);
          if (curr_thread_stop_info_sp) {
            lldb::addr_t crashing_address;
            ValueObjectSP valobj_sp = StopInfo::GetCrashingDereference(
                curr_thread_stop_info_sp, &crashing_address);
            if (valobj_sp) {
              const ValueObject::GetExpressionPathFormat format =
                  ValueObject::GetExpressionPathFormat::
                      eGetExpressionPathFormatHonorPointers;
              stream->PutCString("Likely cause: ");
              valobj_sp->GetExpressionPath(*stream, format);
              stream->Printf(" accessed 0x%" PRIx64 "\n", crashing_address);
            }
          }
        } else {
          uint32_t target_idx = debugger.GetTargetList().GetIndexOfTarget(
              process_sp->GetTarget().shared_from_this());
          if (target_idx != UINT32_MAX)
            stream->Printf("Target %d: (", target_idx);
          else
            stream->Printf("Target <unknown index>: (");
          process_sp->GetTarget().Dump(stream, eDescriptionLevelBrief);
          stream->Printf(") stopped.\n");
        }
      }

      // Pop the process IO handler
      pop_process_io_handler = true;
    }
    break;
  }

  if (handle_pop && pop_process_io_handler)
    process_sp->PopProcessIOHandler();

  return true;
}

bool Process::HijackProcessEvents(ListenerSP listener_sp) {
  if (listener_sp) {
    return HijackBroadcaster(listener_sp, eBroadcastBitStateChanged |
                                              eBroadcastBitInterrupt);
  } else
    return false;
}

void Process::RestoreProcessEvents() { RestoreBroadcaster(); }

StateType Process::GetStateChangedEvents(EventSP &event_sp,
                                         const Timeout<std::micro> &timeout,
                                         ListenerSP hijack_listener_sp) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "timeout = {0}, event_sp)...", timeout);

  ListenerSP listener_sp = hijack_listener_sp;
  if (!listener_sp)
    listener_sp = GetPrimaryListener();

  StateType state = eStateInvalid;
  if (listener_sp->GetEventForBroadcasterWithType(
          this, eBroadcastBitStateChanged | eBroadcastBitInterrupt, event_sp,
          timeout)) {
    if (event_sp && event_sp->GetType() == eBroadcastBitStateChanged)
      state = Process::ProcessEventData::GetStateFromEvent(event_sp.get());
    else
      LLDB_LOG(log, "got no event or was interrupted.");
  }

  LLDB_LOG(log, "timeout = {0}, event_sp) => {1}", timeout, state);
  return state;
}

Event *Process::PeekAtStateChangedEvents() {
  Log *log = GetLog(LLDBLog::Process);

  LLDB_LOGF(log, "Process::%s...", __FUNCTION__);

  Event *event_ptr;
  event_ptr = GetPrimaryListener()->PeekAtNextEventForBroadcasterWithType(
      this, eBroadcastBitStateChanged);
  if (log) {
    if (event_ptr) {
      LLDB_LOGF(log, "Process::%s (event_ptr) => %s", __FUNCTION__,
                StateAsCString(ProcessEventData::GetStateFromEvent(event_ptr)));
    } else {
      LLDB_LOGF(log, "Process::%s no events found", __FUNCTION__);
    }
  }
  return event_ptr;
}

StateType
Process::GetStateChangedEventsPrivate(EventSP &event_sp,
                                      const Timeout<std::micro> &timeout) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "timeout = {0}, event_sp)...", timeout);

  StateType state = eStateInvalid;
  if (m_private_state_listener_sp->GetEventForBroadcasterWithType(
          &m_private_state_broadcaster,
          eBroadcastBitStateChanged | eBroadcastBitInterrupt, event_sp,
          timeout))
    if (event_sp && event_sp->GetType() == eBroadcastBitStateChanged)
      state = Process::ProcessEventData::GetStateFromEvent(event_sp.get());

  LLDB_LOG(log, "timeout = {0}, event_sp) => {1}", timeout,
           state == eStateInvalid ? "TIMEOUT" : StateAsCString(state));
  return state;
}

bool Process::GetEventsPrivate(EventSP &event_sp,
                               const Timeout<std::micro> &timeout,
                               bool control_only) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOG(log, "timeout = {0}, event_sp)...", timeout);

  if (control_only)
    return m_private_state_listener_sp->GetEventForBroadcaster(
        &m_private_state_control_broadcaster, event_sp, timeout);
  else
    return m_private_state_listener_sp->GetEvent(event_sp, timeout);
}

bool Process::IsRunning() const {
  return StateIsRunningState(m_public_state.GetValue());
}

int Process::GetExitStatus() {
  std::lock_guard<std::mutex> guard(m_exit_status_mutex);

  if (m_public_state.GetValue() == eStateExited)
    return m_exit_status;
  return -1;
}

const char *Process::GetExitDescription() {
  std::lock_guard<std::mutex> guard(m_exit_status_mutex);

  if (m_public_state.GetValue() == eStateExited && !m_exit_string.empty())
    return m_exit_string.c_str();
  return nullptr;
}

bool Process::SetExitStatus(int status, llvm::StringRef exit_string) {
  // Use a mutex to protect setting the exit status.
  std::lock_guard<std::mutex> guard(m_exit_status_mutex);

  Log *log(GetLog(LLDBLog::State | LLDBLog::Process));
  LLDB_LOG(log, "(plugin = {0} status = {1} ({1:x8}), description=\"{2}\")",
           GetPluginName(), status, exit_string);

  // We were already in the exited state
  if (m_private_state.GetValue() == eStateExited) {
    LLDB_LOG(
        log,
        "(plugin = {0}) ignoring exit status because state was already set "
        "to eStateExited",
        GetPluginName());
    return false;
  }

  m_exit_status = status;
  if (!exit_string.empty())
    m_exit_string = exit_string.str();
  else
    m_exit_string.clear();

  // Clear the last natural stop ID since it has a strong reference to this
  // process
  m_mod_id.SetStopEventForLastNaturalStopID(EventSP());

  SetPrivateState(eStateExited);

  // Allow subclasses to do some cleanup
  DidExit();

  return true;
}

bool Process::IsAlive() {
  switch (m_private_state.GetValue()) {
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateStopped:
  case eStateRunning:
  case eStateStepping:
  case eStateCrashed:
  case eStateSuspended:
    return true;
  default:
    return false;
  }
}

// This static callback can be used to watch for local child processes on the
// current host. The child process exits, the process will be found in the
// global target list (we want to be completely sure that the
// lldb_private::Process doesn't go away before we can deliver the signal.
bool Process::SetProcessExitStatus(
    lldb::pid_t pid, bool exited,
    int signo,      // Zero for no signal
    int exit_status // Exit value of process if signal is zero
    ) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log,
            "Process::SetProcessExitStatus (pid=%" PRIu64
            ", exited=%i, signal=%i, exit_status=%i)\n",
            pid, exited, signo, exit_status);

  if (exited) {
    TargetSP target_sp(Debugger::FindTargetWithProcessID(pid));
    if (target_sp) {
      ProcessSP process_sp(target_sp->GetProcessSP());
      if (process_sp) {
        llvm::StringRef signal_str =
            process_sp->GetUnixSignals()->GetSignalAsStringRef(signo);
        process_sp->SetExitStatus(exit_status, signal_str);
      }
    }
    return true;
  }
  return false;
}

bool Process::UpdateThreadList(ThreadList &old_thread_list,
                               ThreadList &new_thread_list) {
  m_thread_plans.ClearThreadCache();
  return DoUpdateThreadList(old_thread_list, new_thread_list);
}

void Process::UpdateThreadListIfNeeded() {
  const uint32_t stop_id = GetStopID();
  if (m_thread_list.GetSize(false) == 0 ||
      stop_id != m_thread_list.GetStopID()) {
    bool clear_unused_threads = true;
    const StateType state = GetPrivateState();
    if (StateIsStoppedState(state, true)) {
      std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
      m_thread_list.SetStopID(stop_id);

      // m_thread_list does have its own mutex, but we need to hold onto the
      // mutex between the call to UpdateThreadList(...) and the
      // os->UpdateThreadList(...) so it doesn't change on us
      ThreadList &old_thread_list = m_thread_list;
      ThreadList real_thread_list(*this);
      ThreadList new_thread_list(*this);
      // Always update the thread list with the protocol specific thread list,
      // but only update if "true" is returned
      if (UpdateThreadList(m_thread_list_real, real_thread_list)) {
        // Don't call into the OperatingSystem to update the thread list if we
        // are shutting down, since that may call back into the SBAPI's,
        // requiring the API lock which is already held by whoever is shutting
        // us down, causing a deadlock.
        OperatingSystem *os = GetOperatingSystem();
        if (os && !m_destroy_in_process) {
          // Clear any old backing threads where memory threads might have been
          // backed by actual threads from the lldb_private::Process subclass
          size_t num_old_threads = old_thread_list.GetSize(false);
          for (size_t i = 0; i < num_old_threads; ++i)
            old_thread_list.GetThreadAtIndex(i, false)->ClearBackingThread();
          // See if the OS plugin reports all threads.  If it does, then
          // it is safe to clear unseen thread's plans here.  Otherwise we
          // should preserve them in case they show up again:
          clear_unused_threads = GetOSPluginReportsAllThreads();

          // Turn off dynamic types to ensure we don't run any expressions.
          // Objective-C can run an expression to determine if a SBValue is a
          // dynamic type or not and we need to avoid this. OperatingSystem
          // plug-ins can't run expressions that require running code...

          Target &target = GetTarget();
          const lldb::DynamicValueType saved_prefer_dynamic =
              target.GetPreferDynamicValue();
          if (saved_prefer_dynamic != lldb::eNoDynamicValues)
            target.SetPreferDynamicValue(lldb::eNoDynamicValues);

          // Now let the OperatingSystem plug-in update the thread list

          os->UpdateThreadList(
              old_thread_list, // Old list full of threads created by OS plug-in
              real_thread_list, // The actual thread list full of threads
                                // created by each lldb_private::Process
                                // subclass
              new_thread_list); // The new thread list that we will show to the
                                // user that gets filled in

          if (saved_prefer_dynamic != lldb::eNoDynamicValues)
            target.SetPreferDynamicValue(saved_prefer_dynamic);
        } else {
          // No OS plug-in, the new thread list is the same as the real thread
          // list.
          new_thread_list = real_thread_list;
        }

        m_thread_list_real.Update(real_thread_list);
        m_thread_list.Update(new_thread_list);
        m_thread_list.SetStopID(stop_id);

        if (GetLastNaturalStopID() != m_extended_thread_stop_id) {
          // Clear any extended threads that we may have accumulated previously
          m_extended_thread_list.Clear();
          m_extended_thread_stop_id = GetLastNaturalStopID();

          m_queue_list.Clear();
          m_queue_list_stop_id = GetLastNaturalStopID();
        }
      }
      // Now update the plan stack map.
      // If we do have an OS plugin, any absent real threads in the
      // m_thread_list have already been removed from the ThreadPlanStackMap.
      // So any remaining threads are OS Plugin threads, and those we want to
      // preserve in case they show up again.
      m_thread_plans.Update(m_thread_list, clear_unused_threads);
    }
  }
}

ThreadPlanStack *Process::FindThreadPlans(lldb::tid_t tid) {
  return m_thread_plans.Find(tid);
}

bool Process::PruneThreadPlansForTID(lldb::tid_t tid) {
  return m_thread_plans.PrunePlansForTID(tid);
}

void Process::PruneThreadPlans() {
  m_thread_plans.Update(GetThreadList(), true, false);
}

bool Process::DumpThreadPlansForTID(Stream &strm, lldb::tid_t tid,
                                    lldb::DescriptionLevel desc_level,
                                    bool internal, bool condense_trivial,
                                    bool skip_unreported_plans) {
  return m_thread_plans.DumpPlansForTID(
      strm, tid, desc_level, internal, condense_trivial, skip_unreported_plans);
}
void Process::DumpThreadPlans(Stream &strm, lldb::DescriptionLevel desc_level,
                              bool internal, bool condense_trivial,
                              bool skip_unreported_plans) {
  m_thread_plans.DumpPlans(strm, desc_level, internal, condense_trivial,
                           skip_unreported_plans);
}

void Process::UpdateQueueListIfNeeded() {
  if (m_system_runtime_up) {
    if (m_queue_list.GetSize() == 0 ||
        m_queue_list_stop_id != GetLastNaturalStopID()) {
      const StateType state = GetPrivateState();
      if (StateIsStoppedState(state, true)) {
        m_system_runtime_up->PopulateQueueList(m_queue_list);
        m_queue_list_stop_id = GetLastNaturalStopID();
      }
    }
  }
}

ThreadSP Process::CreateOSPluginThread(lldb::tid_t tid, lldb::addr_t context) {
  OperatingSystem *os = GetOperatingSystem();
  if (os)
    return os->CreateThread(tid, context);
  return ThreadSP();
}

uint32_t Process::GetNextThreadIndexID(uint64_t thread_id) {
  return AssignIndexIDToThread(thread_id);
}

bool Process::HasAssignedIndexIDToThread(uint64_t thread_id) {
  return (m_thread_id_to_index_id_map.find(thread_id) !=
          m_thread_id_to_index_id_map.end());
}

uint32_t Process::AssignIndexIDToThread(uint64_t thread_id) {
  uint32_t result = 0;
  std::map<uint64_t, uint32_t>::iterator iterator =
      m_thread_id_to_index_id_map.find(thread_id);
  if (iterator == m_thread_id_to_index_id_map.end()) {
    result = ++m_thread_index_id;
    m_thread_id_to_index_id_map[thread_id] = result;
  } else {
    result = iterator->second;
  }

  return result;
}

StateType Process::GetState() {
  if (CurrentThreadIsPrivateStateThread())
    return m_private_state.GetValue();
  else
    return m_public_state.GetValue();
}

void Process::SetPublicState(StateType new_state, bool restarted) {
  const bool new_state_is_stopped = StateIsStoppedState(new_state, false);
  if (new_state_is_stopped) {
    // This will only set the time if the public stop time has no value, so
    // it is ok to call this multiple times. With a public stop we can't look
    // at the stop ID because many private stops might have happened, so we
    // can't check for a stop ID of zero. This allows the "statistics" command
    // to dump the time it takes to reach somewhere in your code, like a
    // breakpoint you set.
    GetTarget().GetStatistics().SetFirstPublicStopTime();
  }

  Log *log(GetLog(LLDBLog::State | LLDBLog::Process));
  LLDB_LOGF(log, "(plugin = %s, state = %s, restarted = %i)",
           GetPluginName().data(), StateAsCString(new_state), restarted);
  const StateType old_state = m_public_state.GetValue();
  m_public_state.SetValue(new_state);

  // On the transition from Run to Stopped, we unlock the writer end of the run
  // lock.  The lock gets locked in Resume, which is the public API to tell the
  // program to run.
  if (!StateChangedIsExternallyHijacked()) {
    if (new_state == eStateDetached) {
      LLDB_LOGF(log,
               "(plugin = %s, state = %s) -- unlocking run lock for detach",
               GetPluginName().data(), StateAsCString(new_state));
      m_public_run_lock.SetStopped();
    } else {
      const bool old_state_is_stopped = StateIsStoppedState(old_state, false);
      if ((old_state_is_stopped != new_state_is_stopped)) {
        if (new_state_is_stopped && !restarted) {
          LLDB_LOGF(log, "(plugin = %s, state = %s) -- unlocking run lock",
                   GetPluginName().data(), StateAsCString(new_state));
          m_public_run_lock.SetStopped();
        }
      }
    }
  }
}

Status Process::Resume() {
  Log *log(GetLog(LLDBLog::State | LLDBLog::Process));
  LLDB_LOGF(log, "(plugin = %s) -- locking run lock", GetPluginName().data());
  if (!m_public_run_lock.TrySetRunning()) {
    Status error("Resume request failed - process still running.");
    LLDB_LOGF(log, "(plugin = %s) -- TrySetRunning failed, not resuming.",
             GetPluginName().data());
    return error;
  }
  Status error = PrivateResume();
  if (!error.Success()) {
    // Undo running state change
    m_public_run_lock.SetStopped();
  }
  return error;
}

Status Process::ResumeSynchronous(Stream *stream) {
  Log *log(GetLog(LLDBLog::State | LLDBLog::Process));
  LLDB_LOGF(log, "Process::ResumeSynchronous -- locking run lock");
  if (!m_public_run_lock.TrySetRunning()) {
    Status error("Resume request failed - process still running.");
    LLDB_LOGF(log, "Process::Resume: -- TrySetRunning failed, not resuming.");
    return error;
  }

  ListenerSP listener_sp(
      Listener::MakeListener(ResumeSynchronousHijackListenerName.data()));
  HijackProcessEvents(listener_sp);

  Status error = PrivateResume();
  if (error.Success()) {
    StateType state =
        WaitForProcessToStop(std::nullopt, nullptr, true, listener_sp, stream,
                             true /* use_run_lock */, SelectMostRelevantFrame);
    const bool must_be_alive =
        false; // eStateExited is ok, so this must be false
    if (!StateIsStoppedState(state, must_be_alive))
      error.SetErrorStringWithFormat(
          "process not in stopped state after synchronous resume: %s",
          StateAsCString(state));
  } else {
    // Undo running state change
    m_public_run_lock.SetStopped();
  }

  // Undo the hijacking of process events...
  RestoreProcessEvents();

  return error;
}

bool Process::StateChangedIsExternallyHijacked() {
  if (IsHijackedForEvent(eBroadcastBitStateChanged)) {
    llvm::StringRef hijacking_name = GetHijackingListenerName();
    if (!hijacking_name.starts_with("lldb.internal"))
      return true;
  }
  return false;
}

bool Process::StateChangedIsHijackedForSynchronousResume() {
  if (IsHijackedForEvent(eBroadcastBitStateChanged)) {
    llvm::StringRef hijacking_name = GetHijackingListenerName();
    if (hijacking_name == ResumeSynchronousHijackListenerName)
      return true;
  }
  return false;
}

StateType Process::GetPrivateState() { return m_private_state.GetValue(); }

void Process::SetPrivateState(StateType new_state) {
  // Use m_destructing not m_finalizing here.  If we are finalizing a process
  // that we haven't started tearing down, we'd like to be able to nicely
  // detach if asked, but that requires the event system be live.  That will
  // not be true for an in-the-middle-of-being-destructed Process, since the
  // event system relies on Process::shared_from_this, which may have already
  // been destroyed.
  if (m_destructing)
    return;

  Log *log(GetLog(LLDBLog::State | LLDBLog::Process | LLDBLog::Unwind));
  bool state_changed = false;

  LLDB_LOGF(log, "(plugin = %s, state = %s)", GetPluginName().data(),
           StateAsCString(new_state));

  std::lock_guard<std::recursive_mutex> thread_guard(m_thread_list.GetMutex());
  std::lock_guard<std::recursive_mutex> guard(m_private_state.GetMutex());

  const StateType old_state = m_private_state.GetValueNoLock();
  state_changed = old_state != new_state;

  const bool old_state_is_stopped = StateIsStoppedState(old_state, false);
  const bool new_state_is_stopped = StateIsStoppedState(new_state, false);
  if (old_state_is_stopped != new_state_is_stopped) {
    if (new_state_is_stopped)
      m_private_run_lock.SetStopped();
    else
      m_private_run_lock.SetRunning();
  }

  if (state_changed) {
    m_private_state.SetValueNoLock(new_state);
    EventSP event_sp(
        new Event(eBroadcastBitStateChanged,
                  new ProcessEventData(shared_from_this(), new_state)));
    if (StateIsStoppedState(new_state, false)) {
      // Note, this currently assumes that all threads in the list stop when
      // the process stops.  In the future we will want to support a debugging
      // model where some threads continue to run while others are stopped.
      // When that happens we will either need a way for the thread list to
      // identify which threads are stopping or create a special thread list
      // containing only threads which actually stopped.
      //
      // The process plugin is responsible for managing the actual behavior of
      // the threads and should have stopped any threads that are going to stop
      // before we get here.
      m_thread_list.DidStop();

      if (m_mod_id.BumpStopID() == 0)
        GetTarget().GetStatistics().SetFirstPrivateStopTime();

      if (!m_mod_id.IsLastResumeForUserExpression())
        m_mod_id.SetStopEventForLastNaturalStopID(event_sp);
      m_memory_cache.Clear();
      LLDB_LOGF(log, "(plugin = %s, state = %s, stop_id = %u",
               GetPluginName().data(), StateAsCString(new_state),
               m_mod_id.GetStopID());
    }

    m_private_state_broadcaster.BroadcastEvent(event_sp);
  } else {
    LLDB_LOGF(log, "(plugin = %s, state = %s) state didn't change. Ignoring...",
             GetPluginName().data(), StateAsCString(new_state));
  }
}

void Process::SetRunningUserExpression(bool on) {
  m_mod_id.SetRunningUserExpression(on);
}

void Process::SetRunningUtilityFunction(bool on) {
  m_mod_id.SetRunningUtilityFunction(on);
}

addr_t Process::GetImageInfoAddress() { return LLDB_INVALID_ADDRESS; }

const lldb::ABISP &Process::GetABI() {
  if (!m_abi_sp)
    m_abi_sp = ABI::FindPlugin(shared_from_this(), GetTarget().GetArchitecture());
  return m_abi_sp;
}

std::vector<LanguageRuntime *> Process::GetLanguageRuntimes() {
  std::vector<LanguageRuntime *> language_runtimes;

  if (m_finalizing)
    return language_runtimes;

  std::lock_guard<std::recursive_mutex> guard(m_language_runtimes_mutex);
  // Before we pass off a copy of the language runtimes, we must make sure that
  // our collection is properly populated. It's possible that some of the
  // language runtimes were not loaded yet, either because nobody requested it
  // yet or the proper condition for loading wasn't yet met (e.g. libc++.so
  // hadn't been loaded).
  for (const lldb::LanguageType lang_type : Language::GetSupportedLanguages()) {
    if (LanguageRuntime *runtime = GetLanguageRuntime(lang_type))
      language_runtimes.emplace_back(runtime);
  }

  return language_runtimes;
}

LanguageRuntime *Process::GetLanguageRuntime(lldb::LanguageType language) {
  if (m_finalizing)
    return nullptr;

  LanguageRuntime *runtime = nullptr;

  std::lock_guard<std::recursive_mutex> guard(m_language_runtimes_mutex);
  LanguageRuntimeCollection::iterator pos;
  pos = m_language_runtimes.find(language);
  if (pos == m_language_runtimes.end() || !pos->second) {
    lldb::LanguageRuntimeSP runtime_sp(
        LanguageRuntime::FindPlugin(this, language));

    m_language_runtimes[language] = runtime_sp;
    runtime = runtime_sp.get();
  } else
    runtime = pos->second.get();

  if (runtime)
    // It's possible that a language runtime can support multiple LanguageTypes,
    // for example, CPPLanguageRuntime will support eLanguageTypeC_plus_plus,
    // eLanguageTypeC_plus_plus_03, etc. Because of this, we should get the
    // primary language type and make sure that our runtime supports it.
    assert(runtime->GetLanguageType() == Language::GetPrimaryLanguage(language));

  return runtime;
}

bool Process::IsPossibleDynamicValue(ValueObject &in_value) {
  if (m_finalizing)
    return false;

  if (in_value.IsDynamic())
    return false;
  LanguageType known_type = in_value.GetObjectRuntimeLanguage();

  if (known_type != eLanguageTypeUnknown && known_type != eLanguageTypeC) {
    LanguageRuntime *runtime = GetLanguageRuntime(known_type);
    return runtime ? runtime->CouldHaveDynamicValue(in_value) : false;
  }

  for (LanguageRuntime *runtime : GetLanguageRuntimes()) {
    if (runtime->CouldHaveDynamicValue(in_value))
      return true;
  }

  return false;
}

void Process::SetDynamicCheckers(DynamicCheckerFunctions *dynamic_checkers) {
  m_dynamic_checkers_up.reset(dynamic_checkers);
}

StopPointSiteList<BreakpointSite> &Process::GetBreakpointSiteList() {
  return m_breakpoint_site_list;
}

const StopPointSiteList<BreakpointSite> &
Process::GetBreakpointSiteList() const {
  return m_breakpoint_site_list;
}

void Process::DisableAllBreakpointSites() {
  m_breakpoint_site_list.ForEach([this](BreakpointSite *bp_site) -> void {
    //        bp_site->SetEnabled(true);
    DisableBreakpointSite(bp_site);
  });
}

Status Process::ClearBreakpointSiteByID(lldb::user_id_t break_id) {
  Status error(DisableBreakpointSiteByID(break_id));

  if (error.Success())
    m_breakpoint_site_list.Remove(break_id);

  return error;
}

Status Process::DisableBreakpointSiteByID(lldb::user_id_t break_id) {
  Status error;
  BreakpointSiteSP bp_site_sp = m_breakpoint_site_list.FindByID(break_id);
  if (bp_site_sp) {
    if (bp_site_sp->IsEnabled())
      error = DisableBreakpointSite(bp_site_sp.get());
  } else {
    error.SetErrorStringWithFormat("invalid breakpoint site ID: %" PRIu64,
                                   break_id);
  }

  return error;
}

Status Process::EnableBreakpointSiteByID(lldb::user_id_t break_id) {
  Status error;
  BreakpointSiteSP bp_site_sp = m_breakpoint_site_list.FindByID(break_id);
  if (bp_site_sp) {
    if (!bp_site_sp->IsEnabled())
      error = EnableBreakpointSite(bp_site_sp.get());
  } else {
    error.SetErrorStringWithFormat("invalid breakpoint site ID: %" PRIu64,
                                   break_id);
  }
  return error;
}

lldb::break_id_t
Process::CreateBreakpointSite(const BreakpointLocationSP &constituent,
                              bool use_hardware) {
  addr_t load_addr = LLDB_INVALID_ADDRESS;

  bool show_error = true;
  switch (GetState()) {
  case eStateInvalid:
  case eStateUnloaded:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateDetached:
  case eStateExited:
    show_error = false;
    break;

  case eStateStopped:
  case eStateRunning:
  case eStateStepping:
  case eStateCrashed:
  case eStateSuspended:
    show_error = IsAlive();
    break;
  }

  // Reset the IsIndirect flag here, in case the location changes from pointing
  // to a indirect symbol to a regular symbol.
  constituent->SetIsIndirect(false);

  if (constituent->ShouldResolveIndirectFunctions()) {
    Symbol *symbol = constituent->GetAddress().CalculateSymbolContextSymbol();
    if (symbol && symbol->IsIndirect()) {
      Status error;
      Address symbol_address = symbol->GetAddress();
      load_addr = ResolveIndirectFunction(&symbol_address, error);
      if (!error.Success() && show_error) {
        GetTarget().GetDebugger().GetErrorStream().Printf(
            "warning: failed to resolve indirect function at 0x%" PRIx64
            " for breakpoint %i.%i: %s\n",
            symbol->GetLoadAddress(&GetTarget()),
            constituent->GetBreakpoint().GetID(), constituent->GetID(),
            error.AsCString() ? error.AsCString() : "unknown error");
        return LLDB_INVALID_BREAK_ID;
      }
      Address resolved_address(load_addr);
      load_addr = resolved_address.GetOpcodeLoadAddress(&GetTarget());
      constituent->SetIsIndirect(true);
    } else
      load_addr = constituent->GetAddress().GetOpcodeLoadAddress(&GetTarget());
  } else
    load_addr = constituent->GetAddress().GetOpcodeLoadAddress(&GetTarget());

  if (load_addr != LLDB_INVALID_ADDRESS) {
    BreakpointSiteSP bp_site_sp;

    // Look up this breakpoint site.  If it exists, then add this new
    // constituent, otherwise create a new breakpoint site and add it.

    bp_site_sp = m_breakpoint_site_list.FindByAddress(load_addr);

    if (bp_site_sp) {
      bp_site_sp->AddConstituent(constituent);
      constituent->SetBreakpointSite(bp_site_sp);
      return bp_site_sp->GetID();
    } else {
      bp_site_sp.reset(
          new BreakpointSite(constituent, load_addr, use_hardware));
      if (bp_site_sp) {
        Status error = EnableBreakpointSite(bp_site_sp.get());
        if (error.Success()) {
          constituent->SetBreakpointSite(bp_site_sp);
          return m_breakpoint_site_list.Add(bp_site_sp);
        } else {
          if (show_error || use_hardware) {
            // Report error for setting breakpoint...
            GetTarget().GetDebugger().GetErrorStream().Printf(
                "warning: failed to set breakpoint site at 0x%" PRIx64
                " for breakpoint %i.%i: %s\n",
                load_addr, constituent->GetBreakpoint().GetID(),
                constituent->GetID(),
                error.AsCString() ? error.AsCString() : "unknown error");
          }
        }
      }
    }
  }
  // We failed to enable the breakpoint
  return LLDB_INVALID_BREAK_ID;
}

void Process::RemoveConstituentFromBreakpointSite(
    lldb::user_id_t constituent_id, lldb::user_id_t constituent_loc_id,
    BreakpointSiteSP &bp_site_sp) {
  uint32_t num_constituents =
      bp_site_sp->RemoveConstituent(constituent_id, constituent_loc_id);
  if (num_constituents == 0) {
    // Don't try to disable the site if we don't have a live process anymore.
    if (IsAlive())
      DisableBreakpointSite(bp_site_sp.get());
    m_breakpoint_site_list.RemoveByAddress(bp_site_sp->GetLoadAddress());
  }
}

size_t Process::RemoveBreakpointOpcodesFromBuffer(addr_t bp_addr, size_t size,
                                                  uint8_t *buf) const {
  size_t bytes_removed = 0;
  StopPointSiteList<BreakpointSite> bp_sites_in_range;

  if (m_breakpoint_site_list.FindInRange(bp_addr, bp_addr + size,
                                         bp_sites_in_range)) {
    bp_sites_in_range.ForEach([bp_addr, size,
                               buf](BreakpointSite *bp_site) -> void {
      if (bp_site->GetType() == BreakpointSite::eSoftware) {
        addr_t intersect_addr;
        size_t intersect_size;
        size_t opcode_offset;
        if (bp_site->IntersectsRange(bp_addr, size, &intersect_addr,
                                     &intersect_size, &opcode_offset)) {
          assert(bp_addr <= intersect_addr && intersect_addr < bp_addr + size);
          assert(bp_addr < intersect_addr + intersect_size &&
                 intersect_addr + intersect_size <= bp_addr + size);
          assert(opcode_offset + intersect_size <= bp_site->GetByteSize());
          size_t buf_offset = intersect_addr - bp_addr;
          ::memcpy(buf + buf_offset,
                   bp_site->GetSavedOpcodeBytes() + opcode_offset,
                   intersect_size);
        }
      }
    });
  }
  return bytes_removed;
}

size_t Process::GetSoftwareBreakpointTrapOpcode(BreakpointSite *bp_site) {
  PlatformSP platform_sp(GetTarget().GetPlatform());
  if (platform_sp)
    return platform_sp->GetSoftwareBreakpointTrapOpcode(GetTarget(), bp_site);
  return 0;
}

Status Process::EnableSoftwareBreakpoint(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != nullptr);
  Log *log = GetLog(LLDBLog::Breakpoints);
  const addr_t bp_addr = bp_site->GetLoadAddress();
  LLDB_LOGF(
      log, "Process::EnableSoftwareBreakpoint (site_id = %d) addr = 0x%" PRIx64,
      bp_site->GetID(), (uint64_t)bp_addr);
  if (bp_site->IsEnabled()) {
    LLDB_LOGF(
        log,
        "Process::EnableSoftwareBreakpoint (site_id = %d) addr = 0x%" PRIx64
        " -- already enabled",
        bp_site->GetID(), (uint64_t)bp_addr);
    return error;
  }

  if (bp_addr == LLDB_INVALID_ADDRESS) {
    error.SetErrorString("BreakpointSite contains an invalid load address.");
    return error;
  }
  // Ask the lldb::Process subclass to fill in the correct software breakpoint
  // trap for the breakpoint site
  const size_t bp_opcode_size = GetSoftwareBreakpointTrapOpcode(bp_site);

  if (bp_opcode_size == 0) {
    error.SetErrorStringWithFormat("Process::GetSoftwareBreakpointTrapOpcode() "
                                   "returned zero, unable to get breakpoint "
                                   "trap for address 0x%" PRIx64,
                                   bp_addr);
  } else {
    const uint8_t *const bp_opcode_bytes = bp_site->GetTrapOpcodeBytes();

    if (bp_opcode_bytes == nullptr) {
      error.SetErrorString(
          "BreakpointSite doesn't contain a valid breakpoint trap opcode.");
      return error;
    }

    // Save the original opcode by reading it
    if (DoReadMemory(bp_addr, bp_site->GetSavedOpcodeBytes(), bp_opcode_size,
                     error) == bp_opcode_size) {
      // Write a software breakpoint in place of the original opcode
      if (DoWriteMemory(bp_addr, bp_opcode_bytes, bp_opcode_size, error) ==
          bp_opcode_size) {
        uint8_t verify_bp_opcode_bytes[64];
        if (DoReadMemory(bp_addr, verify_bp_opcode_bytes, bp_opcode_size,
                         error) == bp_opcode_size) {
          if (::memcmp(bp_opcode_bytes, verify_bp_opcode_bytes,
                       bp_opcode_size) == 0) {
            bp_site->SetEnabled(true);
            bp_site->SetType(BreakpointSite::eSoftware);
            LLDB_LOGF(log,
                      "Process::EnableSoftwareBreakpoint (site_id = %d) "
                      "addr = 0x%" PRIx64 " -- SUCCESS",
                      bp_site->GetID(), (uint64_t)bp_addr);
          } else
            error.SetErrorString(
                "failed to verify the breakpoint trap in memory.");
        } else
          error.SetErrorString(
              "Unable to read memory to verify breakpoint trap.");
      } else
        error.SetErrorString("Unable to write breakpoint trap to memory.");
    } else
      error.SetErrorString("Unable to read memory at breakpoint address.");
  }
  if (log && error.Fail())
    LLDB_LOGF(
        log,
        "Process::EnableSoftwareBreakpoint (site_id = %d) addr = 0x%" PRIx64
        " -- FAILED: %s",
        bp_site->GetID(), (uint64_t)bp_addr, error.AsCString());
  return error;
}

Status Process::DisableSoftwareBreakpoint(BreakpointSite *bp_site) {
  Status error;
  assert(bp_site != nullptr);
  Log *log = GetLog(LLDBLog::Breakpoints);
  addr_t bp_addr = bp_site->GetLoadAddress();
  lldb::user_id_t breakID = bp_site->GetID();
  LLDB_LOGF(log,
            "Process::DisableSoftwareBreakpoint (breakID = %" PRIu64
            ") addr = 0x%" PRIx64,
            breakID, (uint64_t)bp_addr);

  if (bp_site->IsHardware()) {
    error.SetErrorString("Breakpoint site is a hardware breakpoint.");
  } else if (bp_site->IsEnabled()) {
    const size_t break_op_size = bp_site->GetByteSize();
    const uint8_t *const break_op = bp_site->GetTrapOpcodeBytes();
    if (break_op_size > 0) {
      // Clear a software breakpoint instruction
      uint8_t curr_break_op[8];
      assert(break_op_size <= sizeof(curr_break_op));
      bool break_op_found = false;

      // Read the breakpoint opcode
      if (DoReadMemory(bp_addr, curr_break_op, break_op_size, error) ==
          break_op_size) {
        bool verify = false;
        // Make sure the breakpoint opcode exists at this address
        if (::memcmp(curr_break_op, break_op, break_op_size) == 0) {
          break_op_found = true;
          // We found a valid breakpoint opcode at this address, now restore
          // the saved opcode.
          if (DoWriteMemory(bp_addr, bp_site->GetSavedOpcodeBytes(),
                            break_op_size, error) == break_op_size) {
            verify = true;
          } else
            error.SetErrorString(
                "Memory write failed when restoring original opcode.");
        } else {
          error.SetErrorString(
              "Original breakpoint trap is no longer in memory.");
          // Set verify to true and so we can check if the original opcode has
          // already been restored
          verify = true;
        }

        if (verify) {
          uint8_t verify_opcode[8];
          assert(break_op_size < sizeof(verify_opcode));
          // Verify that our original opcode made it back to the inferior
          if (DoReadMemory(bp_addr, verify_opcode, break_op_size, error) ==
              break_op_size) {
            // compare the memory we just read with the original opcode
            if (::memcmp(bp_site->GetSavedOpcodeBytes(), verify_opcode,
                         break_op_size) == 0) {
              // SUCCESS
              bp_site->SetEnabled(false);
              LLDB_LOGF(log,
                        "Process::DisableSoftwareBreakpoint (site_id = %d) "
                        "addr = 0x%" PRIx64 " -- SUCCESS",
                        bp_site->GetID(), (uint64_t)bp_addr);
              return error;
            } else {
              if (break_op_found)
                error.SetErrorString("Failed to restore original opcode.");
            }
          } else
            error.SetErrorString("Failed to read memory to verify that "
                                 "breakpoint trap was restored.");
        }
      } else
        error.SetErrorString(
            "Unable to read memory that should contain the breakpoint trap.");
    }
  } else {
    LLDB_LOGF(
        log,
        "Process::DisableSoftwareBreakpoint (site_id = %d) addr = 0x%" PRIx64
        " -- already disabled",
        bp_site->GetID(), (uint64_t)bp_addr);
    return error;
  }

  LLDB_LOGF(
      log,
      "Process::DisableSoftwareBreakpoint (site_id = %d) addr = 0x%" PRIx64
      " -- FAILED: %s",
      bp_site->GetID(), (uint64_t)bp_addr, error.AsCString());
  return error;
}

// Uncomment to verify memory caching works after making changes to caching
// code
//#define VERIFY_MEMORY_READS

size_t Process::ReadMemory(addr_t addr, void *buf, size_t size, Status &error) {
  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixAnyAddress(addr);

  error.Clear();
  if (!GetDisableMemoryCache()) {
#if defined(VERIFY_MEMORY_READS)
    // Memory caching is enabled, with debug verification

    if (buf && size) {
      // Uncomment the line below to make sure memory caching is working.
      // I ran this through the test suite and got no assertions, so I am
      // pretty confident this is working well. If any changes are made to
      // memory caching, uncomment the line below and test your changes!

      // Verify all memory reads by using the cache first, then redundantly
      // reading the same memory from the inferior and comparing to make sure
      // everything is exactly the same.
      std::string verify_buf(size, '\0');
      assert(verify_buf.size() == size);
      const size_t cache_bytes_read =
          m_memory_cache.Read(this, addr, buf, size, error);
      Status verify_error;
      const size_t verify_bytes_read =
          ReadMemoryFromInferior(addr, const_cast<char *>(verify_buf.data()),
                                 verify_buf.size(), verify_error);
      assert(cache_bytes_read == verify_bytes_read);
      assert(memcmp(buf, verify_buf.data(), verify_buf.size()) == 0);
      assert(verify_error.Success() == error.Success());
      return cache_bytes_read;
    }
    return 0;
#else  // !defined(VERIFY_MEMORY_READS)
    // Memory caching is enabled, without debug verification

    return m_memory_cache.Read(addr, buf, size, error);
#endif // defined (VERIFY_MEMORY_READS)
  } else {
    // Memory caching is disabled

    return ReadMemoryFromInferior(addr, buf, size, error);
  }
}

void Process::DoFindInMemory(lldb::addr_t start_addr, lldb::addr_t end_addr,
                             const uint8_t *buf, size_t size,
                             AddressRanges &matches, size_t alignment,
                             size_t max_matches) {
  // Inputs are already validated in FindInMemory() functions.
  assert(buf != nullptr);
  assert(size > 0);
  assert(alignment > 0);
  assert(max_matches > 0);
  assert(start_addr != LLDB_INVALID_ADDRESS);
  assert(end_addr != LLDB_INVALID_ADDRESS);
  assert(start_addr < end_addr);

  lldb::addr_t start = llvm::alignTo(start_addr, alignment);
  while (matches.size() < max_matches && (start + size) < end_addr) {
    const lldb::addr_t found_addr = FindInMemory(start, end_addr, buf, size);
    if (found_addr == LLDB_INVALID_ADDRESS)
      break;

    if (found_addr % alignment) {
      // We need to check the alignment because the FindInMemory uses a special
      // algorithm to efficiently search mememory but doesn't support alignment.
      start = llvm::alignTo(start + 1, alignment);
      continue;
    }

    matches.emplace_back(found_addr, size);
    start = found_addr + alignment;
  }
}

AddressRanges Process::FindRangesInMemory(const uint8_t *buf, uint64_t size,
                                          const AddressRanges &ranges,
                                          size_t alignment, size_t max_matches,
                                          Status &error) {
  AddressRanges matches;
  if (buf == nullptr) {
    error.SetErrorString("buffer is null");
    return matches;
  }
  if (size == 0) {
    error.SetErrorString("buffer size is zero");
    return matches;
  }
  if (ranges.empty()) {
    error.SetErrorString("empty ranges");
    return matches;
  }
  if (alignment == 0) {
    error.SetErrorString("alignment must be greater than zero");
    return matches;
  }
  if (max_matches == 0) {
    error.SetErrorString("max_matches must be greater than zero");
    return matches;
  }

  int resolved_ranges = 0;
  Target &target = GetTarget();
  for (size_t i = 0; i < ranges.size(); ++i) {
    if (matches.size() >= max_matches)
      break;
    const AddressRange &range = ranges[i];
    if (range.IsValid() == false)
      continue;

    const lldb::addr_t start_addr =
        range.GetBaseAddress().GetLoadAddress(&target);
    if (start_addr == LLDB_INVALID_ADDRESS)
      continue;

    ++resolved_ranges;
    const lldb::addr_t end_addr = start_addr + range.GetByteSize();
    DoFindInMemory(start_addr, end_addr, buf, size, matches, alignment,
                   max_matches);
  }

  if (resolved_ranges > 0)
    error.Clear();
  else
    error.SetErrorString("unable to resolve any ranges");

  return matches;
}

lldb::addr_t Process::FindInMemory(const uint8_t *buf, uint64_t size,
                                   const AddressRange &range, size_t alignment,
                                   Status &error) {
  if (buf == nullptr) {
    error.SetErrorString("buffer is null");
    return LLDB_INVALID_ADDRESS;
  }
  if (size == 0) {
    error.SetErrorString("buffer size is zero");
    return LLDB_INVALID_ADDRESS;
  }
  if (!range.IsValid()) {
    error.SetErrorString("range is invalid");
    return LLDB_INVALID_ADDRESS;
  }
  if (alignment == 0) {
    error.SetErrorString("alignment must be greater than zero");
    return LLDB_INVALID_ADDRESS;
  }

  Target &target = GetTarget();
  const lldb::addr_t start_addr =
      range.GetBaseAddress().GetLoadAddress(&target);
  if (start_addr == LLDB_INVALID_ADDRESS) {
    error.SetErrorString("range load address is invalid");
    return LLDB_INVALID_ADDRESS;
  }
  const lldb::addr_t end_addr = start_addr + range.GetByteSize();

  AddressRanges matches;
  DoFindInMemory(start_addr, end_addr, buf, size, matches, alignment, 1);
  if (matches.empty())
    return LLDB_INVALID_ADDRESS;

  error.Clear();
  return matches[0].GetBaseAddress().GetLoadAddress(&target);
}

size_t Process::ReadCStringFromMemory(addr_t addr, std::string &out_str,
                                      Status &error) {
  char buf[256];
  out_str.clear();
  addr_t curr_addr = addr;
  while (true) {
    size_t length = ReadCStringFromMemory(curr_addr, buf, sizeof(buf), error);
    if (length == 0)
      break;
    out_str.append(buf, length);
    // If we got "length - 1" bytes, we didn't get the whole C string, we need
    // to read some more characters
    if (length == sizeof(buf) - 1)
      curr_addr += length;
    else
      break;
  }
  return out_str.size();
}

// Deprecated in favor of ReadStringFromMemory which has wchar support and
// correct code to find null terminators.
size_t Process::ReadCStringFromMemory(addr_t addr, char *dst,
                                      size_t dst_max_len,
                                      Status &result_error) {
  size_t total_cstr_len = 0;
  if (dst && dst_max_len) {
    result_error.Clear();
    // NULL out everything just to be safe
    memset(dst, 0, dst_max_len);
    Status error;
    addr_t curr_addr = addr;
    const size_t cache_line_size = m_memory_cache.GetMemoryCacheLineSize();
    size_t bytes_left = dst_max_len - 1;
    char *curr_dst = dst;

    while (bytes_left > 0) {
      addr_t cache_line_bytes_left =
          cache_line_size - (curr_addr % cache_line_size);
      addr_t bytes_to_read =
          std::min<addr_t>(bytes_left, cache_line_bytes_left);
      size_t bytes_read = ReadMemory(curr_addr, curr_dst, bytes_to_read, error);

      if (bytes_read == 0) {
        result_error = error;
        dst[total_cstr_len] = '\0';
        break;
      }
      const size_t len = strlen(curr_dst);

      total_cstr_len += len;

      if (len < bytes_to_read)
        break;

      curr_dst += bytes_read;
      curr_addr += bytes_read;
      bytes_left -= bytes_read;
    }
  } else {
    if (dst == nullptr)
      result_error.SetErrorString("invalid arguments");
    else
      result_error.Clear();
  }
  return total_cstr_len;
}

size_t Process::ReadMemoryFromInferior(addr_t addr, void *buf, size_t size,
                                       Status &error) {
  LLDB_SCOPED_TIMER();

  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixAnyAddress(addr);

  if (buf == nullptr || size == 0)
    return 0;

  size_t bytes_read = 0;
  uint8_t *bytes = (uint8_t *)buf;

  while (bytes_read < size) {
    const size_t curr_size = size - bytes_read;
    const size_t curr_bytes_read =
        DoReadMemory(addr + bytes_read, bytes + bytes_read, curr_size, error);
    bytes_read += curr_bytes_read;
    if (curr_bytes_read == curr_size || curr_bytes_read == 0)
      break;
  }

  // Replace any software breakpoint opcodes that fall into this range back
  // into "buf" before we return
  if (bytes_read > 0)
    RemoveBreakpointOpcodesFromBuffer(addr, bytes_read, (uint8_t *)buf);
  return bytes_read;
}

uint64_t Process::ReadUnsignedIntegerFromMemory(lldb::addr_t vm_addr,
                                                size_t integer_byte_size,
                                                uint64_t fail_value,
                                                Status &error) {
  Scalar scalar;
  if (ReadScalarIntegerFromMemory(vm_addr, integer_byte_size, false, scalar,
                                  error))
    return scalar.ULongLong(fail_value);
  return fail_value;
}

int64_t Process::ReadSignedIntegerFromMemory(lldb::addr_t vm_addr,
                                             size_t integer_byte_size,
                                             int64_t fail_value,
                                             Status &error) {
  Scalar scalar;
  if (ReadScalarIntegerFromMemory(vm_addr, integer_byte_size, true, scalar,
                                  error))
    return scalar.SLongLong(fail_value);
  return fail_value;
}

addr_t Process::ReadPointerFromMemory(lldb::addr_t vm_addr, Status &error) {
  Scalar scalar;
  if (ReadScalarIntegerFromMemory(vm_addr, GetAddressByteSize(), false, scalar,
                                  error))
    return scalar.ULongLong(LLDB_INVALID_ADDRESS);
  return LLDB_INVALID_ADDRESS;
}

bool Process::WritePointerToMemory(lldb::addr_t vm_addr, lldb::addr_t ptr_value,
                                   Status &error) {
  Scalar scalar;
  const uint32_t addr_byte_size = GetAddressByteSize();
  if (addr_byte_size <= 4)
    scalar = (uint32_t)ptr_value;
  else
    scalar = ptr_value;
  return WriteScalarToMemory(vm_addr, scalar, addr_byte_size, error) ==
         addr_byte_size;
}

size_t Process::WriteMemoryPrivate(addr_t addr, const void *buf, size_t size,
                                   Status &error) {
  size_t bytes_written = 0;
  const uint8_t *bytes = (const uint8_t *)buf;

  while (bytes_written < size) {
    const size_t curr_size = size - bytes_written;
    const size_t curr_bytes_written = DoWriteMemory(
        addr + bytes_written, bytes + bytes_written, curr_size, error);
    bytes_written += curr_bytes_written;
    if (curr_bytes_written == curr_size || curr_bytes_written == 0)
      break;
  }
  return bytes_written;
}

size_t Process::WriteMemory(addr_t addr, const void *buf, size_t size,
                            Status &error) {
  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixAnyAddress(addr);

#if defined(ENABLE_MEMORY_CACHING)
  m_memory_cache.Flush(addr, size);
#endif

  if (buf == nullptr || size == 0)
    return 0;

  m_mod_id.BumpMemoryID();

  // We need to write any data that would go where any current software traps
  // (enabled software breakpoints) any software traps (breakpoints) that we
  // may have placed in our tasks memory.

  StopPointSiteList<BreakpointSite> bp_sites_in_range;
  if (!m_breakpoint_site_list.FindInRange(addr, addr + size, bp_sites_in_range))
    return WriteMemoryPrivate(addr, buf, size, error);

  // No breakpoint sites overlap
  if (bp_sites_in_range.IsEmpty())
    return WriteMemoryPrivate(addr, buf, size, error);

  const uint8_t *ubuf = (const uint8_t *)buf;
  uint64_t bytes_written = 0;

  bp_sites_in_range.ForEach([this, addr, size, &bytes_written, &ubuf,
                             &error](BreakpointSite *bp) -> void {
    if (error.Fail())
      return;

    if (bp->GetType() != BreakpointSite::eSoftware)
      return;

    addr_t intersect_addr;
    size_t intersect_size;
    size_t opcode_offset;
    const bool intersects = bp->IntersectsRange(
        addr, size, &intersect_addr, &intersect_size, &opcode_offset);
    UNUSED_IF_ASSERT_DISABLED(intersects);
    assert(intersects);
    assert(addr <= intersect_addr && intersect_addr < addr + size);
    assert(addr < intersect_addr + intersect_size &&
           intersect_addr + intersect_size <= addr + size);
    assert(opcode_offset + intersect_size <= bp->GetByteSize());

    // Check for bytes before this breakpoint
    const addr_t curr_addr = addr + bytes_written;
    if (intersect_addr > curr_addr) {
      // There are some bytes before this breakpoint that we need to just
      // write to memory
      size_t curr_size = intersect_addr - curr_addr;
      size_t curr_bytes_written =
          WriteMemoryPrivate(curr_addr, ubuf + bytes_written, curr_size, error);
      bytes_written += curr_bytes_written;
      if (curr_bytes_written != curr_size) {
        // We weren't able to write all of the requested bytes, we are
        // done looping and will return the number of bytes that we have
        // written so far.
        if (error.Success())
          error.SetErrorToGenericError();
      }
    }
    // Now write any bytes that would cover up any software breakpoints
    // directly into the breakpoint opcode buffer
    ::memcpy(bp->GetSavedOpcodeBytes() + opcode_offset, ubuf + bytes_written,
             intersect_size);
    bytes_written += intersect_size;
  });

  // Write any remaining bytes after the last breakpoint if we have any left
  if (bytes_written < size)
    bytes_written +=
        WriteMemoryPrivate(addr + bytes_written, ubuf + bytes_written,
                           size - bytes_written, error);

  return bytes_written;
}

size_t Process::WriteScalarToMemory(addr_t addr, const Scalar &scalar,
                                    size_t byte_size, Status &error) {
  if (byte_size == UINT32_MAX)
    byte_size = scalar.GetByteSize();
  if (byte_size > 0) {
    uint8_t buf[32];
    const size_t mem_size =
        scalar.GetAsMemoryData(buf, byte_size, GetByteOrder(), error);
    if (mem_size > 0)
      return WriteMemory(addr, buf, mem_size, error);
    else
      error.SetErrorString("failed to get scalar as memory data");
  } else {
    error.SetErrorString("invalid scalar value");
  }
  return 0;
}

size_t Process::ReadScalarIntegerFromMemory(addr_t addr, uint32_t byte_size,
                                            bool is_signed, Scalar &scalar,
                                            Status &error) {
  uint64_t uval = 0;
  if (byte_size == 0) {
    error.SetErrorString("byte size is zero");
  } else if (byte_size & (byte_size - 1)) {
    error.SetErrorStringWithFormat("byte size %u is not a power of 2",
                                   byte_size);
  } else if (byte_size <= sizeof(uval)) {
    const size_t bytes_read = ReadMemory(addr, &uval, byte_size, error);
    if (bytes_read == byte_size) {
      DataExtractor data(&uval, sizeof(uval), GetByteOrder(),
                         GetAddressByteSize());
      lldb::offset_t offset = 0;
      if (byte_size <= 4)
        scalar = data.GetMaxU32(&offset, byte_size);
      else
        scalar = data.GetMaxU64(&offset, byte_size);
      if (is_signed)
        scalar.SignExtend(byte_size * 8);
      return bytes_read;
    }
  } else {
    error.SetErrorStringWithFormat(
        "byte size of %u is too large for integer scalar type", byte_size);
  }
  return 0;
}

Status Process::WriteObjectFile(std::vector<ObjectFile::LoadableData> entries) {
  Status error;
  for (const auto &Entry : entries) {
    WriteMemory(Entry.Dest, Entry.Contents.data(), Entry.Contents.size(),
                error);
    if (!error.Success())
      break;
  }
  return error;
}

#define USE_ALLOCATE_MEMORY_CACHE 1
addr_t Process::AllocateMemory(size_t size, uint32_t permissions,
                               Status &error) {
  if (GetPrivateState() != eStateStopped) {
    error.SetErrorToGenericError();
    return LLDB_INVALID_ADDRESS;
  }

#if defined(USE_ALLOCATE_MEMORY_CACHE)
  return m_allocated_memory_cache.AllocateMemory(size, permissions, error);
#else
  addr_t allocated_addr = DoAllocateMemory(size, permissions, error);
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log,
            "Process::AllocateMemory(size=%" PRIu64
            ", permissions=%s) => 0x%16.16" PRIx64
            " (m_stop_id = %u m_memory_id = %u)",
            (uint64_t)size, GetPermissionsAsCString(permissions),
            (uint64_t)allocated_addr, m_mod_id.GetStopID(),
            m_mod_id.GetMemoryID());
  return allocated_addr;
#endif
}

addr_t Process::CallocateMemory(size_t size, uint32_t permissions,
                                Status &error) {
  addr_t return_addr = AllocateMemory(size, permissions, error);
  if (error.Success()) {
    std::string buffer(size, 0);
    WriteMemory(return_addr, buffer.c_str(), size, error);
  }
  return return_addr;
}

bool Process::CanJIT() {
  if (m_can_jit == eCanJITDontKnow) {
    Log *log = GetLog(LLDBLog::Process);
    Status err;

    uint64_t allocated_memory = AllocateMemory(
        8, ePermissionsReadable | ePermissionsWritable | ePermissionsExecutable,
        err);

    if (err.Success()) {
      m_can_jit = eCanJITYes;
      LLDB_LOGF(log,
                "Process::%s pid %" PRIu64
                " allocation test passed, CanJIT () is true",
                __FUNCTION__, GetID());
    } else {
      m_can_jit = eCanJITNo;
      LLDB_LOGF(log,
                "Process::%s pid %" PRIu64
                " allocation test failed, CanJIT () is false: %s",
                __FUNCTION__, GetID(), err.AsCString());
    }

    DeallocateMemory(allocated_memory);
  }

  return m_can_jit == eCanJITYes;
}

void Process::SetCanJIT(bool can_jit) {
  m_can_jit = (can_jit ? eCanJITYes : eCanJITNo);
}

void Process::SetCanRunCode(bool can_run_code) {
  SetCanJIT(can_run_code);
  m_can_interpret_function_calls = can_run_code;
}

Status Process::DeallocateMemory(addr_t ptr) {
  Status error;
#if defined(USE_ALLOCATE_MEMORY_CACHE)
  if (!m_allocated_memory_cache.DeallocateMemory(ptr)) {
    error.SetErrorStringWithFormat(
        "deallocation of memory at 0x%" PRIx64 " failed.", (uint64_t)ptr);
  }
#else
  error = DoDeallocateMemory(ptr);

  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log,
            "Process::DeallocateMemory(addr=0x%16.16" PRIx64
            ") => err = %s (m_stop_id = %u, m_memory_id = %u)",
            ptr, error.AsCString("SUCCESS"), m_mod_id.GetStopID(),
            m_mod_id.GetMemoryID());
#endif
  return error;
}

bool Process::GetWatchpointReportedAfter() {
  if (std::optional<bool> subclass_override = DoGetWatchpointReportedAfter())
    return *subclass_override;

  bool reported_after = true;
  const ArchSpec &arch = GetTarget().GetArchitecture();
  if (!arch.IsValid())
    return reported_after;
  llvm::Triple triple = arch.GetTriple();

  if (triple.isMIPS() || triple.isPPC64() || triple.isRISCV() ||
      triple.isAArch64() || triple.isArmMClass() || triple.isARM())
    reported_after = false;

  return reported_after;
}

ModuleSP Process::ReadModuleFromMemory(const FileSpec &file_spec,
                                       lldb::addr_t header_addr,
                                       size_t size_to_read) {
  Log *log = GetLog(LLDBLog::Host);
  if (log) {
    LLDB_LOGF(log,
              "Process::ReadModuleFromMemory reading %s binary from memory",
              file_spec.GetPath().c_str());
  }
  ModuleSP module_sp(new Module(file_spec, ArchSpec()));
  if (module_sp) {
    Status error;
    std::unique_ptr<Progress> progress_up;
    // Reading an ObjectFile from a local corefile is very fast,
    // only print a progress update if we're reading from a
    // live session which might go over gdb remote serial protocol.
    if (IsLiveDebugSession())
      progress_up = std::make_unique<Progress>(
          "Reading binary from memory", file_spec.GetFilename().GetString());

    ObjectFile *objfile = module_sp->GetMemoryObjectFile(
        shared_from_this(), header_addr, error, size_to_read);
    if (objfile)
      return module_sp;
  }
  return ModuleSP();
}

bool Process::GetLoadAddressPermissions(lldb::addr_t load_addr,
                                        uint32_t &permissions) {
  MemoryRegionInfo range_info;
  permissions = 0;
  Status error(GetMemoryRegionInfo(load_addr, range_info));
  if (!error.Success())
    return false;
  if (range_info.GetReadable() == MemoryRegionInfo::eDontKnow ||
      range_info.GetWritable() == MemoryRegionInfo::eDontKnow ||
      range_info.GetExecutable() == MemoryRegionInfo::eDontKnow) {
    return false;
  }
  permissions = range_info.GetLLDBPermissions();
  return true;
}

Status Process::EnableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;
  error.SetErrorString("watchpoints are not supported");
  return error;
}

Status Process::DisableWatchpoint(WatchpointSP wp_sp, bool notify) {
  Status error;
  error.SetErrorString("watchpoints are not supported");
  return error;
}

StateType
Process::WaitForProcessStopPrivate(EventSP &event_sp,
                                   const Timeout<std::micro> &timeout) {
  StateType state;

  while (true) {
    event_sp.reset();
    state = GetStateChangedEventsPrivate(event_sp, timeout);

    if (StateIsStoppedState(state, false))
      break;

    // If state is invalid, then we timed out
    if (state == eStateInvalid)
      break;

    if (event_sp)
      HandlePrivateEvent(event_sp);
  }
  return state;
}

void Process::LoadOperatingSystemPlugin(bool flush) {
  std::lock_guard<std::recursive_mutex> guard(m_thread_mutex);
  if (flush)
    m_thread_list.Clear();
  m_os_up.reset(OperatingSystem::FindPlugin(this, nullptr));
  if (flush)
    Flush();
}

Status Process::Launch(ProcessLaunchInfo &launch_info) {
  StateType state_after_launch = eStateInvalid;
  EventSP first_stop_event_sp;
  Status status =
      LaunchPrivate(launch_info, state_after_launch, first_stop_event_sp);
  if (status.Fail())
    return status;

  if (state_after_launch != eStateStopped &&
      state_after_launch != eStateCrashed)
    return Status();

  // Note, the stop event was consumed above, but not handled. This
  // was done to give DidLaunch a chance to run. The target is either
  // stopped or crashed. Directly set the state.  This is done to
  // prevent a stop message with a bunch of spurious output on thread
  // status, as well as not pop a ProcessIOHandler.
  SetPublicState(state_after_launch, false);

  if (PrivateStateThreadIsValid())
    ResumePrivateStateThread();
  else
    StartPrivateStateThread();

  // Target was stopped at entry as was intended. Need to notify the
  // listeners about it.
  if (launch_info.GetFlags().Test(eLaunchFlagStopAtEntry))
    HandlePrivateEvent(first_stop_event_sp);

  return Status();
}

Status Process::LaunchPrivate(ProcessLaunchInfo &launch_info, StateType &state,
                              EventSP &event_sp) {
  Status error;
  m_abi_sp.reset();
  m_dyld_up.reset();
  m_jit_loaders_up.reset();
  m_system_runtime_up.reset();
  m_os_up.reset();

  {
    std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
    m_process_input_reader.reset();
  }

  Module *exe_module = GetTarget().GetExecutableModulePointer();

  // The "remote executable path" is hooked up to the local Executable
  // module.  But we should be able to debug a remote process even if the
  // executable module only exists on the remote.  However, there needs to
  // be a way to express this path, without actually having a module.
  // The way to do that is to set the ExecutableFile in the LaunchInfo.
  // Figure that out here:

  FileSpec exe_spec_to_use;
  if (!exe_module) {
    if (!launch_info.GetExecutableFile() && !launch_info.IsScriptedProcess()) {
      error.SetErrorString("executable module does not exist");
      return error;
    }
    exe_spec_to_use = launch_info.GetExecutableFile();
  } else
    exe_spec_to_use = exe_module->GetFileSpec();

  if (exe_module && FileSystem::Instance().Exists(exe_module->GetFileSpec())) {
    // Install anything that might need to be installed prior to launching.
    // For host systems, this will do nothing, but if we are connected to a
    // remote platform it will install any needed binaries
    error = GetTarget().Install(&launch_info);
    if (error.Fail())
      return error;
  }

  // Listen and queue events that are broadcasted during the process launch.
  ListenerSP listener_sp(Listener::MakeListener("LaunchEventHijack"));
  HijackProcessEvents(listener_sp);
  auto on_exit = llvm::make_scope_exit([this]() { RestoreProcessEvents(); });

  if (PrivateStateThreadIsValid())
    PausePrivateStateThread();

  error = WillLaunch(exe_module);
  if (error.Fail()) {
    std::string local_exec_file_path = exe_spec_to_use.GetPath();
    return Status("file doesn't exist: '%s'", local_exec_file_path.c_str());
  }

  const bool restarted = false;
  SetPublicState(eStateLaunching, restarted);
  m_should_detach = false;

  if (m_public_run_lock.TrySetRunning()) {
    // Now launch using these arguments.
    error = DoLaunch(exe_module, launch_info);
  } else {
    // This shouldn't happen
    error.SetErrorString("failed to acquire process run lock");
  }

  if (error.Fail()) {
    if (GetID() != LLDB_INVALID_PROCESS_ID) {
      SetID(LLDB_INVALID_PROCESS_ID);
      const char *error_string = error.AsCString();
      if (error_string == nullptr)
        error_string = "launch failed";
      SetExitStatus(-1, error_string);
    }
    return error;
  }

  // Now wait for the process to launch and return control to us, and then
  // call DidLaunch:
  state = WaitForProcessStopPrivate(event_sp, seconds(10));

  if (state == eStateInvalid || !event_sp) {
    // We were able to launch the process, but we failed to catch the
    // initial stop.
    error.SetErrorString("failed to catch stop after launch");
    SetExitStatus(0, error.AsCString());
    Destroy(false);
    return error;
  }

  if (state == eStateExited) {
    // We exited while trying to launch somehow.  Don't call DidLaunch
    // as that's not likely to work, and return an invalid pid.
    HandlePrivateEvent(event_sp);
    return Status();
  }

  if (state == eStateStopped || state == eStateCrashed) {
    DidLaunch();

    // Now that we know the process type, update its signal responses from the
    // ones stored in the Target:
    if (m_unix_signals_sp) {
      StreamSP warning_strm = GetTarget().GetDebugger().GetAsyncErrorStream();
      GetTarget().UpdateSignalsFromDummy(m_unix_signals_sp, warning_strm);
    }

    DynamicLoader *dyld = GetDynamicLoader();
    if (dyld)
      dyld->DidLaunch();

    GetJITLoaders().DidLaunch();

    SystemRuntime *system_runtime = GetSystemRuntime();
    if (system_runtime)
      system_runtime->DidLaunch();

    if (!m_os_up)
      LoadOperatingSystemPlugin(false);

    // We successfully launched the process and stopped, now it the
    // right time to set up signal filters before resuming.
    UpdateAutomaticSignalFiltering();
    return Status();
  }

  return Status("Unexpected process state after the launch: %s, expected %s, "
                "%s, %s or %s",
                StateAsCString(state), StateAsCString(eStateInvalid),
                StateAsCString(eStateExited), StateAsCString(eStateStopped),
                StateAsCString(eStateCrashed));
}

Status Process::LoadCore() {
  Status error = DoLoadCore();
  if (error.Success()) {
    ListenerSP listener_sp(
        Listener::MakeListener("lldb.process.load_core_listener"));
    HijackProcessEvents(listener_sp);

    if (PrivateStateThreadIsValid())
      ResumePrivateStateThread();
    else
      StartPrivateStateThread();

    DynamicLoader *dyld = GetDynamicLoader();
    if (dyld)
      dyld->DidAttach();

    GetJITLoaders().DidAttach();

    SystemRuntime *system_runtime = GetSystemRuntime();
    if (system_runtime)
      system_runtime->DidAttach();

    if (!m_os_up)
      LoadOperatingSystemPlugin(false);

    // We successfully loaded a core file, now pretend we stopped so we can
    // show all of the threads in the core file and explore the crashed state.
    SetPrivateState(eStateStopped);

    // Wait for a stopped event since we just posted one above...
    lldb::EventSP event_sp;
    StateType state =
        WaitForProcessToStop(std::nullopt, &event_sp, true, listener_sp,
                             nullptr, true, SelectMostRelevantFrame);

    if (!StateIsStoppedState(state, false)) {
      Log *log = GetLog(LLDBLog::Process);
      LLDB_LOGF(log, "Process::Halt() failed to stop, state is: %s",
                StateAsCString(state));
      error.SetErrorString(
          "Did not get stopped event after loading the core file.");
    }
    RestoreProcessEvents();
  }
  return error;
}

DynamicLoader *Process::GetDynamicLoader() {
  if (!m_dyld_up)
    m_dyld_up.reset(DynamicLoader::FindPlugin(this, ""));
  return m_dyld_up.get();
}

void Process::SetDynamicLoader(DynamicLoaderUP dyld_up) {
  m_dyld_up = std::move(dyld_up);
}

DataExtractor Process::GetAuxvData() { return DataExtractor(); }

llvm::Expected<bool> Process::SaveCore(llvm::StringRef outfile) {
  return false;
}

JITLoaderList &Process::GetJITLoaders() {
  if (!m_jit_loaders_up) {
    m_jit_loaders_up = std::make_unique<JITLoaderList>();
    JITLoader::LoadPlugins(this, *m_jit_loaders_up);
  }
  return *m_jit_loaders_up;
}

SystemRuntime *Process::GetSystemRuntime() {
  if (!m_system_runtime_up)
    m_system_runtime_up.reset(SystemRuntime::FindPlugin(this));
  return m_system_runtime_up.get();
}

Process::AttachCompletionHandler::AttachCompletionHandler(Process *process,
                                                          uint32_t exec_count)
    : NextEventAction(process), m_exec_count(exec_count) {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(
      log,
      "Process::AttachCompletionHandler::%s process=%p, exec_count=%" PRIu32,
      __FUNCTION__, static_cast<void *>(process), exec_count);
}

Process::NextEventAction::EventActionResult
Process::AttachCompletionHandler::PerformAction(lldb::EventSP &event_sp) {
  Log *log = GetLog(LLDBLog::Process);

  StateType state = ProcessEventData::GetStateFromEvent(event_sp.get());
  LLDB_LOGF(log,
            "Process::AttachCompletionHandler::%s called with state %s (%d)",
            __FUNCTION__, StateAsCString(state), static_cast<int>(state));

  switch (state) {
  case eStateAttaching:
    return eEventActionSuccess;

  case eStateRunning:
  case eStateConnected:
    return eEventActionRetry;

  case eStateStopped:
  case eStateCrashed:
    // During attach, prior to sending the eStateStopped event,
    // lldb_private::Process subclasses must set the new process ID.
    assert(m_process->GetID() != LLDB_INVALID_PROCESS_ID);
    // We don't want these events to be reported, so go set the
    // ShouldReportStop here:
    m_process->GetThreadList().SetShouldReportStop(eVoteNo);

    if (m_exec_count > 0) {
      --m_exec_count;

      LLDB_LOGF(log,
                "Process::AttachCompletionHandler::%s state %s: reduced "
                "remaining exec count to %" PRIu32 ", requesting resume",
                __FUNCTION__, StateAsCString(state), m_exec_count);

      RequestResume();
      return eEventActionRetry;
    } else {
      LLDB_LOGF(log,
                "Process::AttachCompletionHandler::%s state %s: no more "
                "execs expected to start, continuing with attach",
                __FUNCTION__, StateAsCString(state));

      m_process->CompleteAttach();
      return eEventActionSuccess;
    }
    break;

  default:
  case eStateExited:
  case eStateInvalid:
    break;
  }

  m_exit_string.assign("No valid Process");
  return eEventActionExit;
}

Process::NextEventAction::EventActionResult
Process::AttachCompletionHandler::HandleBeingInterrupted() {
  return eEventActionSuccess;
}

const char *Process::AttachCompletionHandler::GetExitString() {
  return m_exit_string.c_str();
}

ListenerSP ProcessAttachInfo::GetListenerForProcess(Debugger &debugger) {
  if (m_listener_sp)
    return m_listener_sp;
  else
    return debugger.GetListener();
}

Status Process::WillLaunch(Module *module) {
  return DoWillLaunch(module);
}

Status Process::WillAttachToProcessWithID(lldb::pid_t pid) {
  return DoWillAttachToProcessWithID(pid);
}

Status Process::WillAttachToProcessWithName(const char *process_name,
                                            bool wait_for_launch) {
  return DoWillAttachToProcessWithName(process_name, wait_for_launch);
}

Status Process::Attach(ProcessAttachInfo &attach_info) {
  m_abi_sp.reset();
  {
    std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
    m_process_input_reader.reset();
  }
  m_dyld_up.reset();
  m_jit_loaders_up.reset();
  m_system_runtime_up.reset();
  m_os_up.reset();

  lldb::pid_t attach_pid = attach_info.GetProcessID();
  Status error;
  if (attach_pid == LLDB_INVALID_PROCESS_ID) {
    char process_name[PATH_MAX];

    if (attach_info.GetExecutableFile().GetPath(process_name,
                                                sizeof(process_name))) {
      const bool wait_for_launch = attach_info.GetWaitForLaunch();

      if (wait_for_launch) {
        error = WillAttachToProcessWithName(process_name, wait_for_launch);
        if (error.Success()) {
          if (m_public_run_lock.TrySetRunning()) {
            m_should_detach = true;
            const bool restarted = false;
            SetPublicState(eStateAttaching, restarted);
            // Now attach using these arguments.
            error = DoAttachToProcessWithName(process_name, attach_info);
          } else {
            // This shouldn't happen
            error.SetErrorString("failed to acquire process run lock");
          }

          if (error.Fail()) {
            if (GetID() != LLDB_INVALID_PROCESS_ID) {
              SetID(LLDB_INVALID_PROCESS_ID);
              if (error.AsCString() == nullptr)
                error.SetErrorString("attach failed");

              SetExitStatus(-1, error.AsCString());
            }
          } else {
            SetNextEventAction(new Process::AttachCompletionHandler(
                this, attach_info.GetResumeCount()));
            StartPrivateStateThread();
          }
          return error;
        }
      } else {
        ProcessInstanceInfoList process_infos;
        PlatformSP platform_sp(GetTarget().GetPlatform());

        if (platform_sp) {
          ProcessInstanceInfoMatch match_info;
          match_info.GetProcessInfo() = attach_info;
          match_info.SetNameMatchType(NameMatch::Equals);
          platform_sp->FindProcesses(match_info, process_infos);
          const uint32_t num_matches = process_infos.size();
          if (num_matches == 1) {
            attach_pid = process_infos[0].GetProcessID();
            // Fall through and attach using the above process ID
          } else {
            match_info.GetProcessInfo().GetExecutableFile().GetPath(
                process_name, sizeof(process_name));
            if (num_matches > 1) {
              StreamString s;
              ProcessInstanceInfo::DumpTableHeader(s, true, false);
              for (size_t i = 0; i < num_matches; i++) {
                process_infos[i].DumpAsTableRow(
                    s, platform_sp->GetUserIDResolver(), true, false);
              }
              error.SetErrorStringWithFormat(
                  "more than one process named %s:\n%s", process_name,
                  s.GetData());
            } else
              error.SetErrorStringWithFormat(
                  "could not find a process named %s", process_name);
          }
        } else {
          error.SetErrorString(
              "invalid platform, can't find processes by name");
          return error;
        }
      }
    } else {
      error.SetErrorString("invalid process name");
    }
  }

  if (attach_pid != LLDB_INVALID_PROCESS_ID) {
    error = WillAttachToProcessWithID(attach_pid);
    if (error.Success()) {

      if (m_public_run_lock.TrySetRunning()) {
        // Now attach using these arguments.
        m_should_detach = true;
        const bool restarted = false;
        SetPublicState(eStateAttaching, restarted);
        error = DoAttachToProcessWithID(attach_pid, attach_info);
      } else {
        // This shouldn't happen
        error.SetErrorString("failed to acquire process run lock");
      }

      if (error.Success()) {
        SetNextEventAction(new Process::AttachCompletionHandler(
            this, attach_info.GetResumeCount()));
        StartPrivateStateThread();
      } else {
        if (GetID() != LLDB_INVALID_PROCESS_ID)
          SetID(LLDB_INVALID_PROCESS_ID);

        const char *error_string = error.AsCString();
        if (error_string == nullptr)
          error_string = "attach failed";

        SetExitStatus(-1, error_string);
      }
    }
  }
  return error;
}

void Process::CompleteAttach() {
  Log *log(GetLog(LLDBLog::Process | LLDBLog::Target));
  LLDB_LOGF(log, "Process::%s()", __FUNCTION__);

  // Let the process subclass figure out at much as it can about the process
  // before we go looking for a dynamic loader plug-in.
  ArchSpec process_arch;
  DidAttach(process_arch);

  if (process_arch.IsValid()) {
    LLDB_LOG(log,
             "Process::{0} replacing process architecture with DidAttach() "
             "architecture: \"{1}\"",
             __FUNCTION__, process_arch.GetTriple().getTriple());
    GetTarget().SetArchitecture(process_arch);
  }

  // We just attached.  If we have a platform, ask it for the process
  // architecture, and if it isn't the same as the one we've already set,
  // switch architectures.
  PlatformSP platform_sp(GetTarget().GetPlatform());
  assert(platform_sp);
  ArchSpec process_host_arch = GetSystemArchitecture();
  if (platform_sp) {
    const ArchSpec &target_arch = GetTarget().GetArchitecture();
    if (target_arch.IsValid() && !platform_sp->IsCompatibleArchitecture(
                                     target_arch, process_host_arch,
                                     ArchSpec::CompatibleMatch, nullptr)) {
      ArchSpec platform_arch;
      platform_sp = GetTarget().GetDebugger().GetPlatformList().GetOrCreate(
          target_arch, process_host_arch, &platform_arch);
      if (platform_sp) {
        GetTarget().SetPlatform(platform_sp);
        GetTarget().SetArchitecture(platform_arch);
        LLDB_LOG(log,
                 "switching platform to {0} and architecture to {1} based on "
                 "info from attach",
                 platform_sp->GetName(), platform_arch.GetTriple().getTriple());
      }
    } else if (!process_arch.IsValid()) {
      ProcessInstanceInfo process_info;
      GetProcessInfo(process_info);
      const ArchSpec &process_arch = process_info.GetArchitecture();
      const ArchSpec &target_arch = GetTarget().GetArchitecture();
      if (process_arch.IsValid() &&
          target_arch.IsCompatibleMatch(process_arch) &&
          !target_arch.IsExactMatch(process_arch)) {
        GetTarget().SetArchitecture(process_arch);
        LLDB_LOGF(log,
                  "Process::%s switching architecture to %s based on info "
                  "the platform retrieved for pid %" PRIu64,
                  __FUNCTION__, process_arch.GetTriple().getTriple().c_str(),
                  GetID());
      }
    }
  }
  // Now that we know the process type, update its signal responses from the
  // ones stored in the Target:
  if (m_unix_signals_sp) {
    StreamSP warning_strm = GetTarget().GetDebugger().GetAsyncErrorStream();
    GetTarget().UpdateSignalsFromDummy(m_unix_signals_sp, warning_strm);
  }

  // We have completed the attach, now it is time to find the dynamic loader
  // plug-in
  DynamicLoader *dyld = GetDynamicLoader();
  if (dyld) {
    dyld->DidAttach();
    if (log) {
      ModuleSP exe_module_sp = GetTarget().GetExecutableModule();
      LLDB_LOG(log,
               "after DynamicLoader::DidAttach(), target "
               "executable is {0} (using {1} plugin)",
               exe_module_sp ? exe_module_sp->GetFileSpec() : FileSpec(),
               dyld->GetPluginName());
    }
  }

  GetJITLoaders().DidAttach();

  SystemRuntime *system_runtime = GetSystemRuntime();
  if (system_runtime) {
    system_runtime->DidAttach();
    if (log) {
      ModuleSP exe_module_sp = GetTarget().GetExecutableModule();
      LLDB_LOG(log,
               "after SystemRuntime::DidAttach(), target "
               "executable is {0} (using {1} plugin)",
               exe_module_sp ? exe_module_sp->GetFileSpec() : FileSpec(),
               system_runtime->GetPluginName());
    }
  }

  if (!m_os_up) {
    LoadOperatingSystemPlugin(false);
    if (m_os_up) {
      // Somebody might have gotten threads before now, but we need to force the
      // update after we've loaded the OperatingSystem plugin or it won't get a
      // chance to process the threads.
      m_thread_list.Clear();
      UpdateThreadListIfNeeded();
    }
  }
  // Figure out which one is the executable, and set that in our target:
  ModuleSP new_executable_module_sp;
  for (ModuleSP module_sp : GetTarget().GetImages().Modules()) {
    if (module_sp && module_sp->IsExecutable()) {
      if (GetTarget().GetExecutableModulePointer() != module_sp.get())
        new_executable_module_sp = module_sp;
      break;
    }
  }
  if (new_executable_module_sp) {
    GetTarget().SetExecutableModule(new_executable_module_sp,
                                    eLoadDependentsNo);
    if (log) {
      ModuleSP exe_module_sp = GetTarget().GetExecutableModule();
      LLDB_LOGF(
          log,
          "Process::%s after looping through modules, target executable is %s",
          __FUNCTION__,
          exe_module_sp ? exe_module_sp->GetFileSpec().GetPath().c_str()
                        : "<none>");
    }
  }
}

Status Process::ConnectRemote(llvm::StringRef remote_url) {
  m_abi_sp.reset();
  {
    std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
    m_process_input_reader.reset();
  }

  // Find the process and its architecture.  Make sure it matches the
  // architecture of the current Target, and if not adjust it.

  Status error(DoConnectRemote(remote_url));
  if (error.Success()) {
    if (GetID() != LLDB_INVALID_PROCESS_ID) {
      EventSP event_sp;
      StateType state = WaitForProcessStopPrivate(event_sp, std::nullopt);

      if (state == eStateStopped || state == eStateCrashed) {
        // If we attached and actually have a process on the other end, then
        // this ended up being the equivalent of an attach.
        CompleteAttach();

        // This delays passing the stopped event to listeners till
        // CompleteAttach gets a chance to complete...
        HandlePrivateEvent(event_sp);
      }
    }

    if (PrivateStateThreadIsValid())
      ResumePrivateStateThread();
    else
      StartPrivateStateThread();
  }
  return error;
}

Status Process::PrivateResume() {
  Log *log(GetLog(LLDBLog::Process | LLDBLog::Step));
  LLDB_LOGF(log,
            "Process::PrivateResume() m_stop_id = %u, public state: %s "
            "private state: %s",
            m_mod_id.GetStopID(), StateAsCString(m_public_state.GetValue()),
            StateAsCString(m_private_state.GetValue()));

  // If signals handing status changed we might want to update our signal
  // filters before resuming.
  UpdateAutomaticSignalFiltering();

  Status error(WillResume());
  // Tell the process it is about to resume before the thread list
  if (error.Success()) {
    // Now let the thread list know we are about to resume so it can let all of
    // our threads know that they are about to be resumed. Threads will each be
    // called with Thread::WillResume(StateType) where StateType contains the
    // state that they are supposed to have when the process is resumed
    // (suspended/running/stepping). Threads should also check their resume
    // signal in lldb::Thread::GetResumeSignal() to see if they are supposed to
    // start back up with a signal.
    if (m_thread_list.WillResume()) {
      // Last thing, do the PreResumeActions.
      if (!RunPreResumeActions()) {
        error.SetErrorString(
            "Process::PrivateResume PreResumeActions failed, not resuming.");
      } else {
        m_mod_id.BumpResumeID();
        error = DoResume();
        if (error.Success()) {
          DidResume();
          m_thread_list.DidResume();
          LLDB_LOGF(log, "Process thinks the process has resumed.");
        } else {
          LLDB_LOGF(log, "Process::PrivateResume() DoResume failed.");
          return error;
        }
      }
    } else {
      // Somebody wanted to run without running (e.g. we were faking a step
      // from one frame of a set of inlined frames that share the same PC to
      // another.)  So generate a continue & a stopped event, and let the world
      // handle them.
      LLDB_LOGF(log,
                "Process::PrivateResume() asked to simulate a start & stop.");

      SetPrivateState(eStateRunning);
      SetPrivateState(eStateStopped);
    }
  } else
    LLDB_LOGF(log, "Process::PrivateResume() got an error \"%s\".",
              error.AsCString("<unknown error>"));
  return error;
}

Status Process::Halt(bool clear_thread_plans, bool use_run_lock) {
  if (!StateIsRunningState(m_public_state.GetValue()))
    return Status("Process is not running.");

  // Don't clear the m_clear_thread_plans_on_stop, only set it to true if in
  // case it was already set and some thread plan logic calls halt on its own.
  m_clear_thread_plans_on_stop |= clear_thread_plans;

  ListenerSP halt_listener_sp(
      Listener::MakeListener("lldb.process.halt_listener"));
  HijackProcessEvents(halt_listener_sp);

  EventSP event_sp;

  SendAsyncInterrupt();

  if (m_public_state.GetValue() == eStateAttaching) {
    // Don't hijack and eat the eStateExited as the code that was doing the
    // attach will be waiting for this event...
    RestoreProcessEvents();
    Destroy(false);
    SetExitStatus(SIGKILL, "Cancelled async attach.");
    return Status();
  }

  // Wait for the process halt timeout seconds for the process to stop.
  // If we are going to use the run lock, that means we're stopping out to the
  // user, so we should also select the most relevant frame.
  SelectMostRelevant select_most_relevant =
      use_run_lock ? SelectMostRelevantFrame : DoNoSelectMostRelevantFrame;
  StateType state = WaitForProcessToStop(GetInterruptTimeout(), &event_sp, true,
                                         halt_listener_sp, nullptr,
                                         use_run_lock, select_most_relevant);
  RestoreProcessEvents();

  if (state == eStateInvalid || !event_sp) {
    // We timed out and didn't get a stop event...
    return Status("Halt timed out. State = %s", StateAsCString(GetState()));
  }

  BroadcastEvent(event_sp);

  return Status();
}

lldb::addr_t Process::FindInMemory(lldb::addr_t low, lldb::addr_t high,
                                   const uint8_t *buf, size_t size) {
  const size_t region_size = high - low;

  if (region_size < size)
    return LLDB_INVALID_ADDRESS;

  std::vector<size_t> bad_char_heuristic(256, size);
  ProcessMemoryIterator iterator(*this, low);

  for (size_t idx = 0; idx < size - 1; idx++) {
    decltype(bad_char_heuristic)::size_type bcu_idx = buf[idx];
    bad_char_heuristic[bcu_idx] = size - idx - 1;
  }
  for (size_t s = 0; s <= (region_size - size);) {
    int64_t j = size - 1;
    while (j >= 0 && buf[j] == iterator[s + j])
      j--;
    if (j < 0)
      return low + s;
    else
      s += bad_char_heuristic[iterator[s + size - 1]];
  }

  return LLDB_INVALID_ADDRESS;
}

Status Process::StopForDestroyOrDetach(lldb::EventSP &exit_event_sp) {
  Status error;

  // Check both the public & private states here.  If we're hung evaluating an
  // expression, for instance, then the public state will be stopped, but we
  // still need to interrupt.
  if (m_public_state.GetValue() == eStateRunning ||
      m_private_state.GetValue() == eStateRunning) {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(log, "Process::%s() About to stop.", __FUNCTION__);

    ListenerSP listener_sp(
        Listener::MakeListener("lldb.Process.StopForDestroyOrDetach.hijack"));
    HijackProcessEvents(listener_sp);

    SendAsyncInterrupt();

    // Consume the interrupt event.
    StateType state = WaitForProcessToStop(GetInterruptTimeout(),
                                           &exit_event_sp, true, listener_sp);

    RestoreProcessEvents();

    // If the process exited while we were waiting for it to stop, put the
    // exited event into the shared pointer passed in and return.  Our caller
    // doesn't need to do anything else, since they don't have a process
    // anymore...

    if (state == eStateExited || m_private_state.GetValue() == eStateExited) {
      LLDB_LOGF(log, "Process::%s() Process exited while waiting to stop.",
                __FUNCTION__);
      return error;
    } else
      exit_event_sp.reset(); // It is ok to consume any non-exit stop events

    if (state != eStateStopped) {
      LLDB_LOGF(log, "Process::%s() failed to stop, state is: %s", __FUNCTION__,
                StateAsCString(state));
      // If we really couldn't stop the process then we should just error out
      // here, but if the lower levels just bobbled sending the event and we
      // really are stopped, then continue on.
      StateType private_state = m_private_state.GetValue();
      if (private_state != eStateStopped) {
        return Status(
            "Attempt to stop the target in order to detach timed out. "
            "State = %s",
            StateAsCString(GetState()));
      }
    }
  }
  return error;
}

Status Process::Detach(bool keep_stopped) {
  EventSP exit_event_sp;
  Status error;
  m_destroy_in_process = true;

  error = WillDetach();

  if (error.Success()) {
    if (DetachRequiresHalt()) {
      error = StopForDestroyOrDetach(exit_event_sp);
      if (!error.Success()) {
        m_destroy_in_process = false;
        return error;
      } else if (exit_event_sp) {
        // We shouldn't need to do anything else here.  There's no process left
        // to detach from...
        StopPrivateStateThread();
        m_destroy_in_process = false;
        return error;
      }
    }

    m_thread_list.DiscardThreadPlans();
    DisableAllBreakpointSites();

    error = DoDetach(keep_stopped);
    if (error.Success()) {
      DidDetach();
      StopPrivateStateThread();
    } else {
      return error;
    }
  }
  m_destroy_in_process = false;

  // If we exited when we were waiting for a process to stop, then forward the
  // event here so we don't lose the event
  if (exit_event_sp) {
    // Directly broadcast our exited event because we shut down our private
    // state thread above
    BroadcastEvent(exit_event_sp);
  }

  // If we have been interrupted (to kill us) in the middle of running, we may
  // not end up propagating the last events through the event system, in which
  // case we might strand the write lock.  Unlock it here so when we do to tear
  // down the process we don't get an error destroying the lock.

  m_public_run_lock.SetStopped();
  return error;
}

Status Process::Destroy(bool force_kill) {
  // If we've already called Process::Finalize then there's nothing useful to
  // be done here.  Finalize has actually called Destroy already.
  if (m_finalizing)
    return {};
  return DestroyImpl(force_kill);
}

Status Process::DestroyImpl(bool force_kill) {
  // Tell ourselves we are in the process of destroying the process, so that we
  // don't do any unnecessary work that might hinder the destruction.  Remember
  // to set this back to false when we are done.  That way if the attempt
  // failed and the process stays around for some reason it won't be in a
  // confused state.

  if (force_kill)
    m_should_detach = false;

  if (GetShouldDetach()) {
    // FIXME: This will have to be a process setting:
    bool keep_stopped = false;
    Detach(keep_stopped);
  }

  m_destroy_in_process = true;

  Status error(WillDestroy());
  if (error.Success()) {
    EventSP exit_event_sp;
    if (DestroyRequiresHalt()) {
      error = StopForDestroyOrDetach(exit_event_sp);
    }

    if (m_public_state.GetValue() == eStateStopped) {
      // Ditch all thread plans, and remove all our breakpoints: in case we
      // have to restart the target to kill it, we don't want it hitting a
      // breakpoint... Only do this if we've stopped, however, since if we
      // didn't manage to halt it above, then we're not going to have much luck
      // doing this now.
      m_thread_list.DiscardThreadPlans();
      DisableAllBreakpointSites();
    }

    error = DoDestroy();
    if (error.Success()) {
      DidDestroy();
      StopPrivateStateThread();
    }
    m_stdio_communication.StopReadThread();
    m_stdio_communication.Disconnect();
    m_stdin_forward = false;

    {
      std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
      if (m_process_input_reader) {
        m_process_input_reader->SetIsDone(true);
        m_process_input_reader->Cancel();
        m_process_input_reader.reset();
      }
    }

    // If we exited when we were waiting for a process to stop, then forward
    // the event here so we don't lose the event
    if (exit_event_sp) {
      // Directly broadcast our exited event because we shut down our private
      // state thread above
      BroadcastEvent(exit_event_sp);
    }

    // If we have been interrupted (to kill us) in the middle of running, we
    // may not end up propagating the last events through the event system, in
    // which case we might strand the write lock.  Unlock it here so when we do
    // to tear down the process we don't get an error destroying the lock.
    m_public_run_lock.SetStopped();
  }

  m_destroy_in_process = false;

  return error;
}

Status Process::Signal(int signal) {
  Status error(WillSignal());
  if (error.Success()) {
    error = DoSignal(signal);
    if (error.Success())
      DidSignal();
  }
  return error;
}

void Process::SetUnixSignals(UnixSignalsSP &&signals_sp) {
  assert(signals_sp && "null signals_sp");
  m_unix_signals_sp = std::move(signals_sp);
}

const lldb::UnixSignalsSP &Process::GetUnixSignals() {
  assert(m_unix_signals_sp && "null m_unix_signals_sp");
  return m_unix_signals_sp;
}

lldb::ByteOrder Process::GetByteOrder() const {
  return GetTarget().GetArchitecture().GetByteOrder();
}

uint32_t Process::GetAddressByteSize() const {
  return GetTarget().GetArchitecture().GetAddressByteSize();
}

bool Process::ShouldBroadcastEvent(Event *event_ptr) {
  const StateType state =
      Process::ProcessEventData::GetStateFromEvent(event_ptr);
  bool return_value = true;
  Log *log(GetLog(LLDBLog::Events | LLDBLog::Process));

  switch (state) {
  case eStateDetached:
  case eStateExited:
  case eStateUnloaded:
    m_stdio_communication.SynchronizeWithReadThread();
    m_stdio_communication.StopReadThread();
    m_stdio_communication.Disconnect();
    m_stdin_forward = false;

    [[fallthrough]];
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
    // These events indicate changes in the state of the debugging session,
    // always report them.
    return_value = true;
    break;
  case eStateInvalid:
    // We stopped for no apparent reason, don't report it.
    return_value = false;
    break;
  case eStateRunning:
  case eStateStepping:
    // If we've started the target running, we handle the cases where we are
    // already running and where there is a transition from stopped to running
    // differently. running -> running: Automatically suppress extra running
    // events stopped -> running: Report except when there is one or more no
    // votes
    //     and no yes votes.
    SynchronouslyNotifyStateChanged(state);
    if (m_force_next_event_delivery)
      return_value = true;
    else {
      switch (m_last_broadcast_state) {
      case eStateRunning:
      case eStateStepping:
        // We always suppress multiple runnings with no PUBLIC stop in between.
        return_value = false;
        break;
      default:
        // TODO: make this work correctly. For now always report
        // run if we aren't running so we don't miss any running events. If I
        // run the lldb/test/thread/a.out file and break at main.cpp:58, run
        // and hit the breakpoints on multiple threads, then somehow during the
        // stepping over of all breakpoints no run gets reported.

        // This is a transition from stop to run.
        switch (m_thread_list.ShouldReportRun(event_ptr)) {
        case eVoteYes:
        case eVoteNoOpinion:
          return_value = true;
          break;
        case eVoteNo:
          return_value = false;
          break;
        }
        break;
      }
    }
    break;
  case eStateStopped:
  case eStateCrashed:
  case eStateSuspended:
    // We've stopped.  First see if we're going to restart the target. If we
    // are going to stop, then we always broadcast the event. If we aren't
    // going to stop, let the thread plans decide if we're going to report this
    // event. If no thread has an opinion, we don't report it.

    m_stdio_communication.SynchronizeWithReadThread();
    RefreshStateAfterStop();
    if (ProcessEventData::GetInterruptedFromEvent(event_ptr)) {
      LLDB_LOGF(log,
                "Process::ShouldBroadcastEvent (%p) stopped due to an "
                "interrupt, state: %s",
                static_cast<void *>(event_ptr), StateAsCString(state));
      // Even though we know we are going to stop, we should let the threads
      // have a look at the stop, so they can properly set their state.
      m_thread_list.ShouldStop(event_ptr);
      return_value = true;
    } else {
      bool was_restarted = ProcessEventData::GetRestartedFromEvent(event_ptr);
      bool should_resume = false;

      // It makes no sense to ask "ShouldStop" if we've already been
      // restarted... Asking the thread list is also not likely to go well,
      // since we are running again. So in that case just report the event.

      if (!was_restarted)
        should_resume = !m_thread_list.ShouldStop(event_ptr);

      if (was_restarted || should_resume || m_resume_requested) {
        Vote report_stop_vote = m_thread_list.ShouldReportStop(event_ptr);
        LLDB_LOGF(log,
                  "Process::ShouldBroadcastEvent: should_resume: %i state: "
                  "%s was_restarted: %i report_stop_vote: %d.",
                  should_resume, StateAsCString(state), was_restarted,
                  report_stop_vote);

        switch (report_stop_vote) {
        case eVoteYes:
          return_value = true;
          break;
        case eVoteNoOpinion:
        case eVoteNo:
          return_value = false;
          break;
        }

        if (!was_restarted) {
          LLDB_LOGF(log,
                    "Process::ShouldBroadcastEvent (%p) Restarting process "
                    "from state: %s",
                    static_cast<void *>(event_ptr), StateAsCString(state));
          ProcessEventData::SetRestartedInEvent(event_ptr, true);
          PrivateResume();
        }
      } else {
        return_value = true;
        SynchronouslyNotifyStateChanged(state);
      }
    }
    break;
  }

  // Forcing the next event delivery is a one shot deal.  So reset it here.
  m_force_next_event_delivery = false;

  // We do some coalescing of events (for instance two consecutive running
  // events get coalesced.) But we only coalesce against events we actually
  // broadcast.  So we use m_last_broadcast_state to track that.  NB - you
  // can't use "m_public_state.GetValue()" for that purpose, as was originally
  // done, because the PublicState reflects the last event pulled off the
  // queue, and there may be several events stacked up on the queue unserviced.
  // So the PublicState may not reflect the last broadcasted event yet.
  // m_last_broadcast_state gets updated here.

  if (return_value)
    m_last_broadcast_state = state;

  LLDB_LOGF(log,
            "Process::ShouldBroadcastEvent (%p) => new state: %s, last "
            "broadcast state: %s - %s",
            static_cast<void *>(event_ptr), StateAsCString(state),
            StateAsCString(m_last_broadcast_state),
            return_value ? "YES" : "NO");
  return return_value;
}

bool Process::StartPrivateStateThread(bool is_secondary_thread) {
  Log *log = GetLog(LLDBLog::Events);

  bool already_running = PrivateStateThreadIsValid();
  LLDB_LOGF(log, "Process::%s()%s ", __FUNCTION__,
            already_running ? " already running"
                            : " starting private state thread");

  if (!is_secondary_thread && already_running)
    return true;

  // Create a thread that watches our internal state and controls which events
  // make it to clients (into the DCProcess event queue).
  char thread_name[1024];
  uint32_t max_len = llvm::get_max_thread_name_length();
  if (max_len > 0 && max_len <= 30) {
    // On platforms with abbreviated thread name lengths, choose thread names
    // that fit within the limit.
    if (already_running)
      snprintf(thread_name, sizeof(thread_name), "intern-state-OV");
    else
      snprintf(thread_name, sizeof(thread_name), "intern-state");
  } else {
    if (already_running)
      snprintf(thread_name, sizeof(thread_name),
               "<lldb.process.internal-state-override(pid=%" PRIu64 ")>",
               GetID());
    else
      snprintf(thread_name, sizeof(thread_name),
               "<lldb.process.internal-state(pid=%" PRIu64 ")>", GetID());
  }

  llvm::Expected<HostThread> private_state_thread =
      ThreadLauncher::LaunchThread(
          thread_name,
          [this, is_secondary_thread] {
            return RunPrivateStateThread(is_secondary_thread);
          },
          8 * 1024 * 1024);
  if (!private_state_thread) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::Host), private_state_thread.takeError(),
                   "failed to launch host thread: {0}");
    return false;
  }

  assert(private_state_thread->IsJoinable());
  m_private_state_thread = *private_state_thread;
  ResumePrivateStateThread();
  return true;
}

void Process::PausePrivateStateThread() {
  ControlPrivateStateThread(eBroadcastInternalStateControlPause);
}

void Process::ResumePrivateStateThread() {
  ControlPrivateStateThread(eBroadcastInternalStateControlResume);
}

void Process::StopPrivateStateThread() {
  if (m_private_state_thread.IsJoinable())
    ControlPrivateStateThread(eBroadcastInternalStateControlStop);
  else {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(
        log,
        "Went to stop the private state thread, but it was already invalid.");
  }
}

void Process::ControlPrivateStateThread(uint32_t signal) {
  Log *log = GetLog(LLDBLog::Process);

  assert(signal == eBroadcastInternalStateControlStop ||
         signal == eBroadcastInternalStateControlPause ||
         signal == eBroadcastInternalStateControlResume);

  LLDB_LOGF(log, "Process::%s (signal = %d)", __FUNCTION__, signal);

  // Signal the private state thread
  if (m_private_state_thread.IsJoinable()) {
    // Broadcast the event.
    // It is important to do this outside of the if below, because it's
    // possible that the thread state is invalid but that the thread is waiting
    // on a control event instead of simply being on its way out (this should
    // not happen, but it apparently can).
    LLDB_LOGF(log, "Sending control event of type: %d.", signal);
    std::shared_ptr<EventDataReceipt> event_receipt_sp(new EventDataReceipt());
    m_private_state_control_broadcaster.BroadcastEvent(signal,
                                                       event_receipt_sp);

    // Wait for the event receipt or for the private state thread to exit
    bool receipt_received = false;
    if (PrivateStateThreadIsValid()) {
      while (!receipt_received) {
        // Check for a receipt for n seconds and then check if the private
        // state thread is still around.
        receipt_received =
          event_receipt_sp->WaitForEventReceived(GetUtilityExpressionTimeout());
        if (!receipt_received) {
          // Check if the private state thread is still around. If it isn't
          // then we are done waiting
          if (!PrivateStateThreadIsValid())
            break; // Private state thread exited or is exiting, we are done
        }
      }
    }

    if (signal == eBroadcastInternalStateControlStop) {
      thread_result_t result = {};
      m_private_state_thread.Join(&result);
      m_private_state_thread.Reset();
    }
  } else {
    LLDB_LOGF(
        log,
        "Private state thread already dead, no need to signal it to stop.");
  }
}

void Process::SendAsyncInterrupt() {
  if (PrivateStateThreadIsValid())
    m_private_state_broadcaster.BroadcastEvent(Process::eBroadcastBitInterrupt,
                                               nullptr);
  else
    BroadcastEvent(Process::eBroadcastBitInterrupt, nullptr);
}

void Process::HandlePrivateEvent(EventSP &event_sp) {
  Log *log = GetLog(LLDBLog::Process);
  m_resume_requested = false;

  const StateType new_state =
      Process::ProcessEventData::GetStateFromEvent(event_sp.get());

  // First check to see if anybody wants a shot at this event:
  if (m_next_event_action_up) {
    NextEventAction::EventActionResult action_result =
        m_next_event_action_up->PerformAction(event_sp);
    LLDB_LOGF(log, "Ran next event action, result was %d.", action_result);

    switch (action_result) {
    case NextEventAction::eEventActionSuccess:
      SetNextEventAction(nullptr);
      break;

    case NextEventAction::eEventActionRetry:
      break;

    case NextEventAction::eEventActionExit:
      // Handle Exiting Here.  If we already got an exited event, we should
      // just propagate it.  Otherwise, swallow this event, and set our state
      // to exit so the next event will kill us.
      if (new_state != eStateExited) {
        // FIXME: should cons up an exited event, and discard this one.
        SetExitStatus(0, m_next_event_action_up->GetExitString());
        SetNextEventAction(nullptr);
        return;
      }
      SetNextEventAction(nullptr);
      break;
    }
  }

  // See if we should broadcast this state to external clients?
  const bool should_broadcast = ShouldBroadcastEvent(event_sp.get());

  if (should_broadcast) {
    const bool is_hijacked = IsHijackedForEvent(eBroadcastBitStateChanged);
    if (log) {
      LLDB_LOGF(log,
                "Process::%s (pid = %" PRIu64
                ") broadcasting new state %s (old state %s) to %s",
                __FUNCTION__, GetID(), StateAsCString(new_state),
                StateAsCString(GetState()),
                is_hijacked ? "hijacked" : "public");
    }
    Process::ProcessEventData::SetUpdateStateOnRemoval(event_sp.get());
    if (StateIsRunningState(new_state)) {
      // Only push the input handler if we aren't fowarding events, as this
      // means the curses GUI is in use... Or don't push it if we are launching
      // since it will come up stopped.
      if (!GetTarget().GetDebugger().IsForwardingEvents() &&
          new_state != eStateLaunching && new_state != eStateAttaching) {
        PushProcessIOHandler();
        m_iohandler_sync.SetValue(m_iohandler_sync.GetValue() + 1,
                                  eBroadcastAlways);
        LLDB_LOGF(log, "Process::%s updated m_iohandler_sync to %d",
                  __FUNCTION__, m_iohandler_sync.GetValue());
      }
    } else if (StateIsStoppedState(new_state, false)) {
      if (!Process::ProcessEventData::GetRestartedFromEvent(event_sp.get())) {
        // If the lldb_private::Debugger is handling the events, we don't want
        // to pop the process IOHandler here, we want to do it when we receive
        // the stopped event so we can carefully control when the process
        // IOHandler is popped because when we stop we want to display some
        // text stating how and why we stopped, then maybe some
        // process/thread/frame info, and then we want the "(lldb) " prompt to
        // show up. If we pop the process IOHandler here, then we will cause
        // the command interpreter to become the top IOHandler after the
        // process pops off and it will update its prompt right away... See the
        // Debugger.cpp file where it calls the function as
        // "process_sp->PopProcessIOHandler()" to see where I am talking about.
        // Otherwise we end up getting overlapping "(lldb) " prompts and
        // garbled output.
        //
        // If we aren't handling the events in the debugger (which is indicated
        // by "m_target.GetDebugger().IsHandlingEvents()" returning false) or
        // we are hijacked, then we always pop the process IO handler manually.
        // Hijacking happens when the internal process state thread is running
        // thread plans, or when commands want to run in synchronous mode and
        // they call "process->WaitForProcessToStop()". An example of something
        // that will hijack the events is a simple expression:
        //
        //  (lldb) expr (int)puts("hello")
        //
        // This will cause the internal process state thread to resume and halt
        // the process (and _it_ will hijack the eBroadcastBitStateChanged
        // events) and we do need the IO handler to be pushed and popped
        // correctly.

        if (is_hijacked || !GetTarget().GetDebugger().IsHandlingEvents())
          PopProcessIOHandler();
      }
    }

    BroadcastEvent(event_sp);
  } else {
    if (log) {
      LLDB_LOGF(
          log,
          "Process::%s (pid = %" PRIu64
          ") suppressing state %s (old state %s): should_broadcast == false",
          __FUNCTION__, GetID(), StateAsCString(new_state),
          StateAsCString(GetState()));
    }
  }
}

Status Process::HaltPrivate() {
  EventSP event_sp;
  Status error(WillHalt());
  if (error.Fail())
    return error;

  // Ask the process subclass to actually halt our process
  bool caused_stop;
  error = DoHalt(caused_stop);

  DidHalt();
  return error;
}

thread_result_t Process::RunPrivateStateThread(bool is_secondary_thread) {
  bool control_only = true;

  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log, "Process::%s (arg = %p, pid = %" PRIu64 ") thread starting...",
            __FUNCTION__, static_cast<void *>(this), GetID());

  bool exit_now = false;
  bool interrupt_requested = false;
  while (!exit_now) {
    EventSP event_sp;
    GetEventsPrivate(event_sp, std::nullopt, control_only);
    if (event_sp->BroadcasterIs(&m_private_state_control_broadcaster)) {
      LLDB_LOGF(log,
                "Process::%s (arg = %p, pid = %" PRIu64
                ") got a control event: %d",
                __FUNCTION__, static_cast<void *>(this), GetID(),
                event_sp->GetType());

      switch (event_sp->GetType()) {
      case eBroadcastInternalStateControlStop:
        exit_now = true;
        break; // doing any internal state management below

      case eBroadcastInternalStateControlPause:
        control_only = true;
        break;

      case eBroadcastInternalStateControlResume:
        control_only = false;
        break;
      }

      continue;
    } else if (event_sp->GetType() == eBroadcastBitInterrupt) {
      if (m_public_state.GetValue() == eStateAttaching) {
        LLDB_LOGF(log,
                  "Process::%s (arg = %p, pid = %" PRIu64
                  ") woke up with an interrupt while attaching - "
                  "forwarding interrupt.",
                  __FUNCTION__, static_cast<void *>(this), GetID());
        // The server may be spinning waiting for a process to appear, in which
        // case we should tell it to stop doing that.  Normally, we don't NEED
        // to do that because we will next close the communication to the stub
        // and that will get it to shut down.  But there are remote debugging
        // cases where relying on that side-effect causes the shutdown to be
        // flakey, so we should send a positive signal to interrupt the wait.
        Status error = HaltPrivate();
        BroadcastEvent(eBroadcastBitInterrupt, nullptr);
      } else if (StateIsRunningState(m_last_broadcast_state)) {
        LLDB_LOGF(log,
                  "Process::%s (arg = %p, pid = %" PRIu64
                  ") woke up with an interrupt - Halting.",
                  __FUNCTION__, static_cast<void *>(this), GetID());
        Status error = HaltPrivate();
        if (error.Fail() && log)
          LLDB_LOGF(log,
                    "Process::%s (arg = %p, pid = %" PRIu64
                    ") failed to halt the process: %s",
                    __FUNCTION__, static_cast<void *>(this), GetID(),
                    error.AsCString());
        // Halt should generate a stopped event. Make a note of the fact that
        // we were doing the interrupt, so we can set the interrupted flag
        // after we receive the event. We deliberately set this to true even if
        // HaltPrivate failed, so that we can interrupt on the next natural
        // stop.
        interrupt_requested = true;
      } else {
        // This can happen when someone (e.g. Process::Halt) sees that we are
        // running and sends an interrupt request, but the process actually
        // stops before we receive it. In that case, we can just ignore the
        // request. We use m_last_broadcast_state, because the Stopped event
        // may not have been popped of the event queue yet, which is when the
        // public state gets updated.
        LLDB_LOGF(log,
                  "Process::%s ignoring interrupt as we have already stopped.",
                  __FUNCTION__);
      }
      continue;
    }

    const StateType internal_state =
        Process::ProcessEventData::GetStateFromEvent(event_sp.get());

    if (internal_state != eStateInvalid) {
      if (m_clear_thread_plans_on_stop &&
          StateIsStoppedState(internal_state, true)) {
        m_clear_thread_plans_on_stop = false;
        m_thread_list.DiscardThreadPlans();
      }

      if (interrupt_requested) {
        if (StateIsStoppedState(internal_state, true)) {
          // We requested the interrupt, so mark this as such in the stop event
          // so clients can tell an interrupted process from a natural stop
          ProcessEventData::SetInterruptedInEvent(event_sp.get(), true);
          interrupt_requested = false;
        } else if (log) {
          LLDB_LOGF(log,
                    "Process::%s interrupt_requested, but a non-stopped "
                    "state '%s' received.",
                    __FUNCTION__, StateAsCString(internal_state));
        }
      }

      HandlePrivateEvent(event_sp);
    }

    if (internal_state == eStateInvalid || internal_state == eStateExited ||
        internal_state == eStateDetached) {
      LLDB_LOGF(log,
                "Process::%s (arg = %p, pid = %" PRIu64
                ") about to exit with internal state %s...",
                __FUNCTION__, static_cast<void *>(this), GetID(),
                StateAsCString(internal_state));

      break;
    }
  }

  // Verify log is still enabled before attempting to write to it...
  LLDB_LOGF(log, "Process::%s (arg = %p, pid = %" PRIu64 ") thread exiting...",
            __FUNCTION__, static_cast<void *>(this), GetID());

  // If we are a secondary thread, then the primary thread we are working for
  // will have already acquired the public_run_lock, and isn't done with what
  // it was doing yet, so don't try to change it on the way out.
  if (!is_secondary_thread)
    m_public_run_lock.SetStopped();
  return {};
}

// Process Event Data

Process::ProcessEventData::ProcessEventData() : EventData(), m_process_wp() {}

Process::ProcessEventData::ProcessEventData(const ProcessSP &process_sp,
                                            StateType state)
    : EventData(), m_process_wp(), m_state(state) {
  if (process_sp)
    m_process_wp = process_sp;
}

Process::ProcessEventData::~ProcessEventData() = default;

llvm::StringRef Process::ProcessEventData::GetFlavorString() {
  return "Process::ProcessEventData";
}

llvm::StringRef Process::ProcessEventData::GetFlavor() const {
  return ProcessEventData::GetFlavorString();
}

bool Process::ProcessEventData::ShouldStop(Event *event_ptr,
                                           bool &found_valid_stopinfo) {
  found_valid_stopinfo = false;

  ProcessSP process_sp(m_process_wp.lock());
  if (!process_sp)
    return false;

  ThreadList &curr_thread_list = process_sp->GetThreadList();
  uint32_t num_threads = curr_thread_list.GetSize();

  // The actions might change one of the thread's stop_info's opinions about
  // whether we should stop the process, so we need to query that as we go.

  // One other complication here, is that we try to catch any case where the
  // target has run (except for expressions) and immediately exit, but if we
  // get that wrong (which is possible) then the thread list might have
  // changed, and that would cause our iteration here to crash.  We could
  // make a copy of the thread list, but we'd really like to also know if it
  // has changed at all, so we store the original thread ID's of all threads and
  // check what we get back against this list & bag out if anything differs.
  std::vector<std::pair<ThreadSP, size_t>> not_suspended_threads;
  for (uint32_t idx = 0; idx < num_threads; ++idx) {
    lldb::ThreadSP thread_sp = curr_thread_list.GetThreadAtIndex(idx);

    /*
     Filter out all suspended threads, they could not be the reason
     of stop and no need to perform any actions on them.
     */
    if (thread_sp->GetResumeState() != eStateSuspended)
      not_suspended_threads.emplace_back(thread_sp, thread_sp->GetIndexID());
  }

  // Use this to track whether we should continue from here.  We will only
  // continue the target running if no thread says we should stop.  Of course
  // if some thread's PerformAction actually sets the target running, then it
  // doesn't matter what the other threads say...

  bool still_should_stop = false;

  // Sometimes - for instance if we have a bug in the stub we are talking to,
  // we stop but no thread has a valid stop reason.  In that case we should
  // just stop, because we have no way of telling what the right thing to do
  // is, and it's better to let the user decide than continue behind their
  // backs.

  for (auto [thread_sp, thread_index] : not_suspended_threads) {
    if (curr_thread_list.GetSize() != num_threads) {
      Log *log(GetLog(LLDBLog::Step | LLDBLog::Process));
      LLDB_LOGF(
          log,
          "Number of threads changed from %u to %u while processing event.",
          num_threads, curr_thread_list.GetSize());
      break;
    }

    if (thread_sp->GetIndexID() != thread_index) {
      Log *log(GetLog(LLDBLog::Step | LLDBLog::Process));
      LLDB_LOG(log,
               "The thread {0} changed from {1} to {2} while processing event.",
               thread_sp.get(), thread_index, thread_sp->GetIndexID());
      break;
    }

    StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
    if (stop_info_sp && stop_info_sp->IsValid()) {
      found_valid_stopinfo = true;
      bool this_thread_wants_to_stop;
      if (stop_info_sp->GetOverrideShouldStop()) {
        this_thread_wants_to_stop =
            stop_info_sp->GetOverriddenShouldStopValue();
      } else {
        stop_info_sp->PerformAction(event_ptr);
        // The stop action might restart the target.  If it does, then we
        // want to mark that in the event so that whoever is receiving it
        // will know to wait for the running event and reflect that state
        // appropriately. We also need to stop processing actions, since they
        // aren't expecting the target to be running.

        // FIXME: we might have run.
        if (stop_info_sp->HasTargetRunSinceMe()) {
          SetRestarted(true);
          break;
        }

        this_thread_wants_to_stop = stop_info_sp->ShouldStop(event_ptr);
      }

      if (!still_should_stop)
        still_should_stop = this_thread_wants_to_stop;
    }
  }

  return still_should_stop;
}

bool Process::ProcessEventData::ForwardEventToPendingListeners(
    Event *event_ptr) {
  // STDIO and the other async event notifications should always be forwarded.
  if (event_ptr->GetType() != Process::eBroadcastBitStateChanged)
    return true;

  // For state changed events, if the update state is zero, we are handling
  // this on the private state thread.  We should wait for the public event.
  return m_update_state == 1;
}

void Process::ProcessEventData::DoOnRemoval(Event *event_ptr) {
  // We only have work to do for state changed events:
  if (event_ptr->GetType() != Process::eBroadcastBitStateChanged)
    return;

  ProcessSP process_sp(m_process_wp.lock());

  if (!process_sp)
    return;

  // This function gets called twice for each event, once when the event gets
  // pulled off of the private process event queue, and then any number of
  // times, first when it gets pulled off of the public event queue, then other
  // times when we're pretending that this is where we stopped at the end of
  // expression evaluation.  m_update_state is used to distinguish these three
  // cases; it is 0 when we're just pulling it off for private handling, and >
  // 1 for expression evaluation, and we don't want to do the breakpoint
  // command handling then.
  if (m_update_state != 1)
    return;

  process_sp->SetPublicState(
      m_state, Process::ProcessEventData::GetRestartedFromEvent(event_ptr));

  if (m_state == eStateStopped && !m_restarted) {
    // Let process subclasses know we are about to do a public stop and do
    // anything they might need to in order to speed up register and memory
    // accesses.
    process_sp->WillPublicStop();
  }

  // If this is a halt event, even if the halt stopped with some reason other
  // than a plain interrupt (e.g. we had already stopped for a breakpoint when
  // the halt request came through) don't do the StopInfo actions, as they may
  // end up restarting the process.
  if (m_interrupted)
    return;

  // If we're not stopped or have restarted, then skip the StopInfo actions:
  if (m_state != eStateStopped || m_restarted) {
    return;
  }

  bool does_anybody_have_an_opinion = false;
  bool still_should_stop = ShouldStop(event_ptr, does_anybody_have_an_opinion);

  if (GetRestarted()) {
    return;
  }

  if (!still_should_stop && does_anybody_have_an_opinion) {
    // We've been asked to continue, so do that here.
    SetRestarted(true);
    // Use the private resume method here, since we aren't changing the run
    // lock state.
    process_sp->PrivateResume();
  } else {
    bool hijacked = process_sp->IsHijackedForEvent(eBroadcastBitStateChanged) &&
                    !process_sp->StateChangedIsHijackedForSynchronousResume();

    if (!hijacked) {
      // If we didn't restart, run the Stop Hooks here.
      // Don't do that if state changed events aren't hooked up to the
      // public (or SyncResume) broadcasters.  StopHooks are just for
      // real public stops.  They might also restart the target,
      // so watch for that.
      if (process_sp->GetTarget().RunStopHooks())
        SetRestarted(true);
    }
  }
}

void Process::ProcessEventData::Dump(Stream *s) const {
  ProcessSP process_sp(m_process_wp.lock());

  if (process_sp)
    s->Printf(" process = %p (pid = %" PRIu64 "), ",
              static_cast<void *>(process_sp.get()), process_sp->GetID());
  else
    s->PutCString(" process = NULL, ");

  s->Printf("state = %s", StateAsCString(GetState()));
}

const Process::ProcessEventData *
Process::ProcessEventData::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr) {
    const EventData *event_data = event_ptr->GetData();
    if (event_data &&
        event_data->GetFlavor() == ProcessEventData::GetFlavorString())
      return static_cast<const ProcessEventData *>(event_ptr->GetData());
  }
  return nullptr;
}

ProcessSP
Process::ProcessEventData::GetProcessFromEvent(const Event *event_ptr) {
  ProcessSP process_sp;
  const ProcessEventData *data = GetEventDataFromEvent(event_ptr);
  if (data)
    process_sp = data->GetProcessSP();
  return process_sp;
}

StateType Process::ProcessEventData::GetStateFromEvent(const Event *event_ptr) {
  const ProcessEventData *data = GetEventDataFromEvent(event_ptr);
  if (data == nullptr)
    return eStateInvalid;
  else
    return data->GetState();
}

bool Process::ProcessEventData::GetRestartedFromEvent(const Event *event_ptr) {
  const ProcessEventData *data = GetEventDataFromEvent(event_ptr);
  if (data == nullptr)
    return false;
  else
    return data->GetRestarted();
}

void Process::ProcessEventData::SetRestartedInEvent(Event *event_ptr,
                                                    bool new_value) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data != nullptr)
    data->SetRestarted(new_value);
}

size_t
Process::ProcessEventData::GetNumRestartedReasons(const Event *event_ptr) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data != nullptr)
    return data->GetNumRestartedReasons();
  else
    return 0;
}

const char *
Process::ProcessEventData::GetRestartedReasonAtIndex(const Event *event_ptr,
                                                     size_t idx) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data != nullptr)
    return data->GetRestartedReasonAtIndex(idx);
  else
    return nullptr;
}

void Process::ProcessEventData::AddRestartedReason(Event *event_ptr,
                                                   const char *reason) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data != nullptr)
    data->AddRestartedReason(reason);
}

bool Process::ProcessEventData::GetInterruptedFromEvent(
    const Event *event_ptr) {
  const ProcessEventData *data = GetEventDataFromEvent(event_ptr);
  if (data == nullptr)
    return false;
  else
    return data->GetInterrupted();
}

void Process::ProcessEventData::SetInterruptedInEvent(Event *event_ptr,
                                                      bool new_value) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data != nullptr)
    data->SetInterrupted(new_value);
}

bool Process::ProcessEventData::SetUpdateStateOnRemoval(Event *event_ptr) {
  ProcessEventData *data =
      const_cast<ProcessEventData *>(GetEventDataFromEvent(event_ptr));
  if (data) {
    data->SetUpdateStateOnRemoval();
    return true;
  }
  return false;
}

lldb::TargetSP Process::CalculateTarget() { return m_target_wp.lock(); }

void Process::CalculateExecutionContext(ExecutionContext &exe_ctx) {
  exe_ctx.SetTargetPtr(&GetTarget());
  exe_ctx.SetProcessPtr(this);
  exe_ctx.SetThreadPtr(nullptr);
  exe_ctx.SetFramePtr(nullptr);
}

// uint32_t
// Process::ListProcessesMatchingName (const char *name, StringList &matches,
// std::vector<lldb::pid_t> &pids)
//{
//    return 0;
//}
//
// ArchSpec
// Process::GetArchSpecForExistingProcess (lldb::pid_t pid)
//{
//    return Host::GetArchSpecForExistingProcess (pid);
//}
//
// ArchSpec
// Process::GetArchSpecForExistingProcess (const char *process_name)
//{
//    return Host::GetArchSpecForExistingProcess (process_name);
//}

EventSP Process::CreateEventFromProcessState(uint32_t event_type) {
  auto event_data_sp =
      std::make_shared<ProcessEventData>(shared_from_this(), GetState());
  return std::make_shared<Event>(event_type, event_data_sp);
}

void Process::AppendSTDOUT(const char *s, size_t len) {
  std::lock_guard<std::recursive_mutex> guard(m_stdio_communication_mutex);
  m_stdout_data.append(s, len);
  auto event_sp = CreateEventFromProcessState(eBroadcastBitSTDOUT);
  BroadcastEventIfUnique(event_sp);
}

void Process::AppendSTDERR(const char *s, size_t len) {
  std::lock_guard<std::recursive_mutex> guard(m_stdio_communication_mutex);
  m_stderr_data.append(s, len);
  auto event_sp = CreateEventFromProcessState(eBroadcastBitSTDERR);
  BroadcastEventIfUnique(event_sp);
}

void Process::BroadcastAsyncProfileData(const std::string &one_profile_data) {
  std::lock_guard<std::recursive_mutex> guard(m_profile_data_comm_mutex);
  m_profile_data.push_back(one_profile_data);
  auto event_sp = CreateEventFromProcessState(eBroadcastBitProfileData);
  BroadcastEventIfUnique(event_sp);
}

void Process::BroadcastStructuredData(const StructuredData::ObjectSP &object_sp,
                                      const StructuredDataPluginSP &plugin_sp) {
  auto data_sp = std::make_shared<EventDataStructuredData>(
      shared_from_this(), object_sp, plugin_sp);
  BroadcastEvent(eBroadcastBitStructuredData, data_sp);
}

StructuredDataPluginSP
Process::GetStructuredDataPlugin(llvm::StringRef type_name) const {
  auto find_it = m_structured_data_plugin_map.find(type_name);
  if (find_it != m_structured_data_plugin_map.end())
    return find_it->second;
  else
    return StructuredDataPluginSP();
}

size_t Process::GetAsyncProfileData(char *buf, size_t buf_size, Status &error) {
  std::lock_guard<std::recursive_mutex> guard(m_profile_data_comm_mutex);
  if (m_profile_data.empty())
    return 0;

  std::string &one_profile_data = m_profile_data.front();
  size_t bytes_available = one_profile_data.size();
  if (bytes_available > 0) {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(log, "Process::GetProfileData (buf = %p, size = %" PRIu64 ")",
              static_cast<void *>(buf), static_cast<uint64_t>(buf_size));
    if (bytes_available > buf_size) {
      memcpy(buf, one_profile_data.c_str(), buf_size);
      one_profile_data.erase(0, buf_size);
      bytes_available = buf_size;
    } else {
      memcpy(buf, one_profile_data.c_str(), bytes_available);
      m_profile_data.erase(m_profile_data.begin());
    }
  }
  return bytes_available;
}

// Process STDIO

size_t Process::GetSTDOUT(char *buf, size_t buf_size, Status &error) {
  std::lock_guard<std::recursive_mutex> guard(m_stdio_communication_mutex);
  size_t bytes_available = m_stdout_data.size();
  if (bytes_available > 0) {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(log, "Process::GetSTDOUT (buf = %p, size = %" PRIu64 ")",
              static_cast<void *>(buf), static_cast<uint64_t>(buf_size));
    if (bytes_available > buf_size) {
      memcpy(buf, m_stdout_data.c_str(), buf_size);
      m_stdout_data.erase(0, buf_size);
      bytes_available = buf_size;
    } else {
      memcpy(buf, m_stdout_data.c_str(), bytes_available);
      m_stdout_data.clear();
    }
  }
  return bytes_available;
}

size_t Process::GetSTDERR(char *buf, size_t buf_size, Status &error) {
  std::lock_guard<std::recursive_mutex> gaurd(m_stdio_communication_mutex);
  size_t bytes_available = m_stderr_data.size();
  if (bytes_available > 0) {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(log, "Process::GetSTDERR (buf = %p, size = %" PRIu64 ")",
              static_cast<void *>(buf), static_cast<uint64_t>(buf_size));
    if (bytes_available > buf_size) {
      memcpy(buf, m_stderr_data.c_str(), buf_size);
      m_stderr_data.erase(0, buf_size);
      bytes_available = buf_size;
    } else {
      memcpy(buf, m_stderr_data.c_str(), bytes_available);
      m_stderr_data.clear();
    }
  }
  return bytes_available;
}

void Process::STDIOReadThreadBytesReceived(void *baton, const void *src,
                                           size_t src_len) {
  Process *process = (Process *)baton;
  process->AppendSTDOUT(static_cast<const char *>(src), src_len);
}

class IOHandlerProcessSTDIO : public IOHandler {
public:
  IOHandlerProcessSTDIO(Process *process, int write_fd)
      : IOHandler(process->GetTarget().GetDebugger(),
                  IOHandler::Type::ProcessIO),
        m_process(process),
        m_read_file(GetInputFD(), File::eOpenOptionReadOnly, false),
        m_write_file(write_fd, File::eOpenOptionWriteOnly, false) {
    m_pipe.CreateNew(false);
  }

  ~IOHandlerProcessSTDIO() override = default;

  void SetIsRunning(bool running) {
    std::lock_guard<std::mutex> guard(m_mutex);
    SetIsDone(!running);
    m_is_running = running;
  }

  // Each IOHandler gets to run until it is done. It should read data from the
  // "in" and place output into "out" and "err and return when done.
  void Run() override {
    if (!m_read_file.IsValid() || !m_write_file.IsValid() ||
        !m_pipe.CanRead() || !m_pipe.CanWrite()) {
      SetIsDone(true);
      return;
    }

    SetIsDone(false);
    const int read_fd = m_read_file.GetDescriptor();
    Terminal terminal(read_fd);
    TerminalState terminal_state(terminal, false);
    // FIXME: error handling?
    llvm::consumeError(terminal.SetCanonical(false));
    llvm::consumeError(terminal.SetEcho(false));
// FD_ZERO, FD_SET are not supported on windows
#ifndef _WIN32
    const int pipe_read_fd = m_pipe.GetReadFileDescriptor();
    SetIsRunning(true);
    while (true) {
      {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (GetIsDone())
          break;
      }

      SelectHelper select_helper;
      select_helper.FDSetRead(read_fd);
      select_helper.FDSetRead(pipe_read_fd);
      Status error = select_helper.Select();

      if (error.Fail())
        break;

      char ch = 0;
      size_t n;
      if (select_helper.FDIsSetRead(read_fd)) {
        n = 1;
        if (m_read_file.Read(&ch, n).Success() && n == 1) {
          if (m_write_file.Write(&ch, n).Fail() || n != 1)
            break;
        } else
          break;
      }

      if (select_helper.FDIsSetRead(pipe_read_fd)) {
        size_t bytes_read;
        // Consume the interrupt byte
        Status error = m_pipe.Read(&ch, 1, bytes_read);
        if (error.Success()) {
          if (ch == 'q')
            break;
          if (ch == 'i')
            if (StateIsRunningState(m_process->GetState()))
              m_process->SendAsyncInterrupt();
        }
      }
    }
    SetIsRunning(false);
#endif
  }

  void Cancel() override {
    std::lock_guard<std::mutex> guard(m_mutex);
    SetIsDone(true);
    // Only write to our pipe to cancel if we are in
    // IOHandlerProcessSTDIO::Run(). We can end up with a python command that
    // is being run from the command interpreter:
    //
    // (lldb) step_process_thousands_of_times
    //
    // In this case the command interpreter will be in the middle of handling
    // the command and if the process pushes and pops the IOHandler thousands
    // of times, we can end up writing to m_pipe without ever consuming the
    // bytes from the pipe in IOHandlerProcessSTDIO::Run() and end up
    // deadlocking when the pipe gets fed up and blocks until data is consumed.
    if (m_is_running) {
      char ch = 'q'; // Send 'q' for quit
      size_t bytes_written = 0;
      m_pipe.Write(&ch, 1, bytes_written);
    }
  }

  bool Interrupt() override {
    // Do only things that are safe to do in an interrupt context (like in a
    // SIGINT handler), like write 1 byte to a file descriptor. This will
    // interrupt the IOHandlerProcessSTDIO::Run() and we can look at the byte
    // that was written to the pipe and then call
    // m_process->SendAsyncInterrupt() from a much safer location in code.
    if (m_active) {
      char ch = 'i'; // Send 'i' for interrupt
      size_t bytes_written = 0;
      Status result = m_pipe.Write(&ch, 1, bytes_written);
      return result.Success();
    } else {
      // This IOHandler might be pushed on the stack, but not being run
      // currently so do the right thing if we aren't actively watching for
      // STDIN by sending the interrupt to the process. Otherwise the write to
      // the pipe above would do nothing. This can happen when the command
      // interpreter is running and gets a "expression ...". It will be on the
      // IOHandler thread and sending the input is complete to the delegate
      // which will cause the expression to run, which will push the process IO
      // handler, but not run it.

      if (StateIsRunningState(m_process->GetState())) {
        m_process->SendAsyncInterrupt();
        return true;
      }
    }
    return false;
  }

  void GotEOF() override {}

protected:
  Process *m_process;
  NativeFile m_read_file;  // Read from this file (usually actual STDIN for LLDB
  NativeFile m_write_file; // Write to this file (usually the primary pty for
                           // getting io to debuggee)
  Pipe m_pipe;
  std::mutex m_mutex;
  bool m_is_running = false;
};

void Process::SetSTDIOFileDescriptor(int fd) {
  // First set up the Read Thread for reading/handling process I/O
  m_stdio_communication.SetConnection(
      std::make_unique<ConnectionFileDescriptor>(fd, true));
  if (m_stdio_communication.IsConnected()) {
    m_stdio_communication.SetReadThreadBytesReceivedCallback(
        STDIOReadThreadBytesReceived, this);
    m_stdio_communication.StartReadThread();

    // Now read thread is set up, set up input reader.
    {
      std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
      if (!m_process_input_reader)
        m_process_input_reader =
            std::make_shared<IOHandlerProcessSTDIO>(this, fd);
    }
  }
}

bool Process::ProcessIOHandlerIsActive() {
  std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
  IOHandlerSP io_handler_sp(m_process_input_reader);
  if (io_handler_sp)
    return GetTarget().GetDebugger().IsTopIOHandler(io_handler_sp);
  return false;
}

bool Process::PushProcessIOHandler() {
  std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
  IOHandlerSP io_handler_sp(m_process_input_reader);
  if (io_handler_sp) {
    Log *log = GetLog(LLDBLog::Process);
    LLDB_LOGF(log, "Process::%s pushing IO handler", __FUNCTION__);

    io_handler_sp->SetIsDone(false);
    // If we evaluate an utility function, then we don't cancel the current
    // IOHandler. Our IOHandler is non-interactive and shouldn't disturb the
    // existing IOHandler that potentially provides the user interface (e.g.
    // the IOHandler for Editline).
    bool cancel_top_handler = !m_mod_id.IsRunningUtilityFunction();
    GetTarget().GetDebugger().RunIOHandlerAsync(io_handler_sp,
                                                cancel_top_handler);
    return true;
  }
  return false;
}

bool Process::PopProcessIOHandler() {
  std::lock_guard<std::mutex> guard(m_process_input_reader_mutex);
  IOHandlerSP io_handler_sp(m_process_input_reader);
  if (io_handler_sp)
    return GetTarget().GetDebugger().RemoveIOHandler(io_handler_sp);
  return false;
}

// The process needs to know about installed plug-ins
void Process::SettingsInitialize() { Thread::SettingsInitialize(); }

void Process::SettingsTerminate() { Thread::SettingsTerminate(); }

namespace {
// RestorePlanState is used to record the "is private", "is controlling" and
// "okay
// to discard" fields of the plan we are running, and reset it on Clean or on
// destruction. It will only reset the state once, so you can call Clean and
// then monkey with the state and it won't get reset on you again.

class RestorePlanState {
public:
  RestorePlanState(lldb::ThreadPlanSP thread_plan_sp)
      : m_thread_plan_sp(thread_plan_sp) {
    if (m_thread_plan_sp) {
      m_private = m_thread_plan_sp->GetPrivate();
      m_is_controlling = m_thread_plan_sp->IsControllingPlan();
      m_okay_to_discard = m_thread_plan_sp->OkayToDiscard();
    }
  }

  ~RestorePlanState() { Clean(); }

  void Clean() {
    if (!m_already_reset && m_thread_plan_sp) {
      m_already_reset = true;
      m_thread_plan_sp->SetPrivate(m_private);
      m_thread_plan_sp->SetIsControllingPlan(m_is_controlling);
      m_thread_plan_sp->SetOkayToDiscard(m_okay_to_discard);
    }
  }

private:
  lldb::ThreadPlanSP m_thread_plan_sp;
  bool m_already_reset = false;
  bool m_private = false;
  bool m_is_controlling = false;
  bool m_okay_to_discard = false;
};
} // anonymous namespace

static microseconds
GetOneThreadExpressionTimeout(const EvaluateExpressionOptions &options) {
  const milliseconds default_one_thread_timeout(250);

  // If the overall wait is forever, then we don't need to worry about it.
  if (!options.GetTimeout()) {
    return options.GetOneThreadTimeout() ? *options.GetOneThreadTimeout()
                                         : default_one_thread_timeout;
  }

  // If the one thread timeout is set, use it.
  if (options.GetOneThreadTimeout())
    return *options.GetOneThreadTimeout();

  // Otherwise use half the total timeout, bounded by the
  // default_one_thread_timeout.
  return std::min<microseconds>(default_one_thread_timeout,
                                *options.GetTimeout() / 2);
}

static Timeout<std::micro>
GetExpressionTimeout(const EvaluateExpressionOptions &options,
                     bool before_first_timeout) {
  // If we are going to run all threads the whole time, or if we are only going
  // to run one thread, we can just return the overall timeout.
  if (!options.GetStopOthers() || !options.GetTryAllThreads())
    return options.GetTimeout();

  if (before_first_timeout)
    return GetOneThreadExpressionTimeout(options);

  if (!options.GetTimeout())
    return std::nullopt;
  else
    return *options.GetTimeout() - GetOneThreadExpressionTimeout(options);
}

static std::optional<ExpressionResults>
HandleStoppedEvent(lldb::tid_t thread_id, const ThreadPlanSP &thread_plan_sp,
                   RestorePlanState &restorer, const EventSP &event_sp,
                   EventSP &event_to_broadcast_sp,
                   const EvaluateExpressionOptions &options,
                   bool handle_interrupts) {
  Log *log = GetLog(LLDBLog::Step | LLDBLog::Process);

  ThreadSP thread_sp = thread_plan_sp->GetTarget()
                           .GetProcessSP()
                           ->GetThreadList()
                           .FindThreadByID(thread_id);
  if (!thread_sp) {
    LLDB_LOG(log,
             "The thread on which we were running the "
             "expression: tid = {0}, exited while "
             "the expression was running.",
             thread_id);
    return eExpressionThreadVanished;
  }

  ThreadPlanSP plan = thread_sp->GetCompletedPlan();
  if (plan == thread_plan_sp && plan->PlanSucceeded()) {
    LLDB_LOG(log, "execution completed successfully");

    // Restore the plan state so it will get reported as intended when we are
    // done.
    restorer.Clean();
    return eExpressionCompleted;
  }

  StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
  if (stop_info_sp && stop_info_sp->GetStopReason() == eStopReasonBreakpoint &&
      stop_info_sp->ShouldNotify(event_sp.get())) {
    LLDB_LOG(log, "stopped for breakpoint: {0}.", stop_info_sp->GetDescription());
    if (!options.DoesIgnoreBreakpoints()) {
      // Restore the plan state and then force Private to false.  We are going
      // to stop because of this plan so we need it to become a public plan or
      // it won't report correctly when we continue to its termination later
      // on.
      restorer.Clean();
      thread_plan_sp->SetPrivate(false);
      event_to_broadcast_sp = event_sp;
    }
    return eExpressionHitBreakpoint;
  }

  if (!handle_interrupts &&
      Process::ProcessEventData::GetInterruptedFromEvent(event_sp.get()))
    return std::nullopt;

  LLDB_LOG(log, "thread plan did not successfully complete");
  if (!options.DoesUnwindOnError())
    event_to_broadcast_sp = event_sp;
  return eExpressionInterrupted;
}

ExpressionResults
Process::RunThreadPlan(ExecutionContext &exe_ctx,
                       lldb::ThreadPlanSP &thread_plan_sp,
                       const EvaluateExpressionOptions &options,
                       DiagnosticManager &diagnostic_manager) {
  ExpressionResults return_value = eExpressionSetupError;

  std::lock_guard<std::mutex> run_thread_plan_locker(m_run_thread_plan_lock);

  if (!thread_plan_sp) {
    diagnostic_manager.PutString(
        lldb::eSeverityError, "RunThreadPlan called with empty thread plan.");
    return eExpressionSetupError;
  }

  if (!thread_plan_sp->ValidatePlan(nullptr)) {
    diagnostic_manager.PutString(
        lldb::eSeverityError,
        "RunThreadPlan called with an invalid thread plan.");
    return eExpressionSetupError;
  }

  if (exe_ctx.GetProcessPtr() != this) {
    diagnostic_manager.PutString(lldb::eSeverityError,
                                 "RunThreadPlan called on wrong process.");
    return eExpressionSetupError;
  }

  Thread *thread = exe_ctx.GetThreadPtr();
  if (thread == nullptr) {
    diagnostic_manager.PutString(lldb::eSeverityError,
                                 "RunThreadPlan called with invalid thread.");
    return eExpressionSetupError;
  }

  // Record the thread's id so we can tell when a thread we were using
  // to run the expression exits during the expression evaluation.
  lldb::tid_t expr_thread_id = thread->GetID();

  // We need to change some of the thread plan attributes for the thread plan
  // runner.  This will restore them when we are done:

  RestorePlanState thread_plan_restorer(thread_plan_sp);

  // We rely on the thread plan we are running returning "PlanCompleted" if
  // when it successfully completes. For that to be true the plan can't be
  // private - since private plans suppress themselves in the GetCompletedPlan
  // call.

  thread_plan_sp->SetPrivate(false);

  // The plans run with RunThreadPlan also need to be terminal controlling plans
  // or when they are done we will end up asking the plan above us whether we
  // should stop, which may give the wrong answer.

  thread_plan_sp->SetIsControllingPlan(true);
  thread_plan_sp->SetOkayToDiscard(false);

  // If we are running some utility expression for LLDB, we now have to mark
  // this in the ProcesModID of this process. This RAII takes care of marking
  // and reverting the mark it once we are done running the expression.
  UtilityFunctionScope util_scope(options.IsForUtilityExpr() ? this : nullptr);

  if (m_private_state.GetValue() != eStateStopped) {
    diagnostic_manager.PutString(
        lldb::eSeverityError,
        "RunThreadPlan called while the private state was not stopped.");
    return eExpressionSetupError;
  }

  // Save the thread & frame from the exe_ctx for restoration after we run
  const uint32_t thread_idx_id = thread->GetIndexID();
  StackFrameSP selected_frame_sp =
      thread->GetSelectedFrame(DoNoSelectMostRelevantFrame);
  if (!selected_frame_sp) {
    thread->SetSelectedFrame(nullptr);
    selected_frame_sp = thread->GetSelectedFrame(DoNoSelectMostRelevantFrame);
    if (!selected_frame_sp) {
      diagnostic_manager.Printf(
          lldb::eSeverityError,
          "RunThreadPlan called without a selected frame on thread %d",
          thread_idx_id);
      return eExpressionSetupError;
    }
  }

  // Make sure the timeout values make sense. The one thread timeout needs to
  // be smaller than the overall timeout.
  if (options.GetOneThreadTimeout() && options.GetTimeout() &&
      *options.GetTimeout() < *options.GetOneThreadTimeout()) {
    diagnostic_manager.PutString(lldb::eSeverityError,
                                 "RunThreadPlan called with one thread "
                                 "timeout greater than total timeout");
    return eExpressionSetupError;
  }

  StackID ctx_frame_id = selected_frame_sp->GetStackID();

  // N.B. Running the target may unset the currently selected thread and frame.
  // We don't want to do that either, so we should arrange to reset them as
  // well.

  lldb::ThreadSP selected_thread_sp = GetThreadList().GetSelectedThread();

  uint32_t selected_tid;
  StackID selected_stack_id;
  if (selected_thread_sp) {
    selected_tid = selected_thread_sp->GetIndexID();
    selected_stack_id =
        selected_thread_sp->GetSelectedFrame(DoNoSelectMostRelevantFrame)
            ->GetStackID();
  } else {
    selected_tid = LLDB_INVALID_THREAD_ID;
  }

  HostThread backup_private_state_thread;
  lldb::StateType old_state = eStateInvalid;
  lldb::ThreadPlanSP stopper_base_plan_sp;

  Log *log(GetLog(LLDBLog::Step | LLDBLog::Process));
  if (m_private_state_thread.EqualsThread(Host::GetCurrentThread())) {
    // Yikes, we are running on the private state thread!  So we can't wait for
    // public events on this thread, since we are the thread that is generating
    // public events. The simplest thing to do is to spin up a temporary thread
    // to handle private state thread events while we are fielding public
    // events here.
    LLDB_LOGF(log, "Running thread plan on private state thread, spinning up "
                   "another state thread to handle the events.");

    backup_private_state_thread = m_private_state_thread;

    // One other bit of business: we want to run just this thread plan and
    // anything it pushes, and then stop, returning control here. But in the
    // normal course of things, the plan above us on the stack would be given a
    // shot at the stop event before deciding to stop, and we don't want that.
    // So we insert a "stopper" base plan on the stack before the plan we want
    // to run.  Since base plans always stop and return control to the user,
    // that will do just what we want.
    stopper_base_plan_sp.reset(new ThreadPlanBase(*thread));
    thread->QueueThreadPlan(stopper_base_plan_sp, false);
    // Have to make sure our public state is stopped, since otherwise the
    // reporting logic below doesn't work correctly.
    old_state = m_public_state.GetValue();
    m_public_state.SetValueNoLock(eStateStopped);

    // Now spin up the private state thread:
    StartPrivateStateThread(true);
  }

  thread->QueueThreadPlan(
      thread_plan_sp, false); // This used to pass "true" does that make sense?

  if (options.GetDebug()) {
    // In this case, we aren't actually going to run, we just want to stop
    // right away. Flush this thread so we will refetch the stacks and show the
    // correct backtrace.
    // FIXME: To make this prettier we should invent some stop reason for this,
    // but that
    // is only cosmetic, and this functionality is only of use to lldb
    // developers who can live with not pretty...
    thread->Flush();
    return eExpressionStoppedForDebug;
  }

  ListenerSP listener_sp(
      Listener::MakeListener("lldb.process.listener.run-thread-plan"));

  lldb::EventSP event_to_broadcast_sp;

  {
    // This process event hijacker Hijacks the Public events and its destructor
    // makes sure that the process events get restored on exit to the function.
    //
    // If the event needs to propagate beyond the hijacker (e.g., the process
    // exits during execution), then the event is put into
    // event_to_broadcast_sp for rebroadcasting.

    ProcessEventHijacker run_thread_plan_hijacker(*this, listener_sp);

    if (log) {
      StreamString s;
      thread_plan_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
      LLDB_LOGF(log,
                "Process::RunThreadPlan(): Resuming thread %u - 0x%4.4" PRIx64
                " to run thread plan \"%s\".",
                thread_idx_id, expr_thread_id, s.GetData());
    }

    bool got_event;
    lldb::EventSP event_sp;
    lldb::StateType stop_state = lldb::eStateInvalid;

    bool before_first_timeout = true; // This is set to false the first time
                                      // that we have to halt the target.
    bool do_resume = true;
    bool handle_running_event = true;

    // This is just for accounting:
    uint32_t num_resumes = 0;

    // If we are going to run all threads the whole time, or if we are only
    // going to run one thread, then we don't need the first timeout.  So we
    // pretend we are after the first timeout already.
    if (!options.GetStopOthers() || !options.GetTryAllThreads())
      before_first_timeout = false;

    LLDB_LOGF(log, "Stop others: %u, try all: %u, before_first: %u.\n",
              options.GetStopOthers(), options.GetTryAllThreads(),
              before_first_timeout);

    // This isn't going to work if there are unfetched events on the queue. Are
    // there cases where we might want to run the remaining events here, and
    // then try to call the function?  That's probably being too tricky for our
    // own good.

    Event *other_events = listener_sp->PeekAtNextEvent();
    if (other_events != nullptr) {
      diagnostic_manager.PutString(
          lldb::eSeverityError,
          "RunThreadPlan called with pending events on the queue.");
      return eExpressionSetupError;
    }

    // We also need to make sure that the next event is delivered.  We might be
    // calling a function as part of a thread plan, in which case the last
    // delivered event could be the running event, and we don't want event
    // coalescing to cause us to lose OUR running event...
    ForceNextEventDelivery();

// This while loop must exit out the bottom, there's cleanup that we need to do
// when we are done. So don't call return anywhere within it.

#ifdef LLDB_RUN_THREAD_HALT_WITH_EVENT
    // It's pretty much impossible to write test cases for things like: One
    // thread timeout expires, I go to halt, but the process already stopped on
    // the function call stop breakpoint.  Turning on this define will make us
    // not fetch the first event till after the halt.  So if you run a quick
    // function, it will have completed, and the completion event will be
    // waiting, when you interrupt for halt. The expression evaluation should
    // still succeed.
    bool miss_first_event = true;
#endif
    while (true) {
      // We usually want to resume the process if we get to the top of the
      // loop. The only exception is if we get two running events with no
      // intervening stop, which can happen, we will just wait for then next
      // stop event.
      LLDB_LOGF(log,
                "Top of while loop: do_resume: %i handle_running_event: %i "
                "before_first_timeout: %i.",
                do_resume, handle_running_event, before_first_timeout);

      if (do_resume || handle_running_event) {
        // Do the initial resume and wait for the running event before going
        // further.

        if (do_resume) {
          num_resumes++;
          Status resume_error = PrivateResume();
          if (!resume_error.Success()) {
            diagnostic_manager.Printf(
                lldb::eSeverityError,
                "couldn't resume inferior the %d time: \"%s\".", num_resumes,
                resume_error.AsCString());
            return_value = eExpressionSetupError;
            break;
          }
        }

        got_event =
            listener_sp->GetEvent(event_sp, GetUtilityExpressionTimeout());
        if (!got_event) {
          LLDB_LOGF(log,
                    "Process::RunThreadPlan(): didn't get any event after "
                    "resume %" PRIu32 ", exiting.",
                    num_resumes);

          diagnostic_manager.Printf(lldb::eSeverityError,
                                    "didn't get any event after resume %" PRIu32
                                    ", exiting.",
                                    num_resumes);
          return_value = eExpressionSetupError;
          break;
        }

        stop_state =
            Process::ProcessEventData::GetStateFromEvent(event_sp.get());

        if (stop_state != eStateRunning) {
          bool restarted = false;

          if (stop_state == eStateStopped) {
            restarted = Process::ProcessEventData::GetRestartedFromEvent(
                event_sp.get());
            LLDB_LOGF(
                log,
                "Process::RunThreadPlan(): didn't get running event after "
                "resume %d, got %s instead (restarted: %i, do_resume: %i, "
                "handle_running_event: %i).",
                num_resumes, StateAsCString(stop_state), restarted, do_resume,
                handle_running_event);
          }

          if (restarted) {
            // This is probably an overabundance of caution, I don't think I
            // should ever get a stopped & restarted event here.  But if I do,
            // the best thing is to Halt and then get out of here.
            const bool clear_thread_plans = false;
            const bool use_run_lock = false;
            Halt(clear_thread_plans, use_run_lock);
          }

          diagnostic_manager.Printf(
              lldb::eSeverityError,
              "didn't get running event after initial resume, got %s instead.",
              StateAsCString(stop_state));
          return_value = eExpressionSetupError;
          break;
        }

        if (log)
          log->PutCString("Process::RunThreadPlan(): resuming succeeded.");
        // We need to call the function synchronously, so spin waiting for it
        // to return. If we get interrupted while executing, we're going to
        // lose our context, and won't be able to gather the result at this
        // point. We set the timeout AFTER the resume, since the resume takes
        // some time and we don't want to charge that to the timeout.
      } else {
        if (log)
          log->PutCString("Process::RunThreadPlan(): waiting for next event.");
      }

      do_resume = true;
      handle_running_event = true;

      // Now wait for the process to stop again:
      event_sp.reset();

      Timeout<std::micro> timeout =
          GetExpressionTimeout(options, before_first_timeout);
      if (log) {
        if (timeout) {
          auto now = system_clock::now();
          LLDB_LOGF(log,
                    "Process::RunThreadPlan(): about to wait - now is %s - "
                    "endpoint is %s",
                    llvm::to_string(now).c_str(),
                    llvm::to_string(now + *timeout).c_str());
        } else {
          LLDB_LOGF(log, "Process::RunThreadPlan(): about to wait forever.");
        }
      }

#ifdef LLDB_RUN_THREAD_HALT_WITH_EVENT
      // See comment above...
      if (miss_first_event) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        miss_first_event = false;
        got_event = false;
      } else
#endif
        got_event = listener_sp->GetEvent(event_sp, timeout);

      if (got_event) {
        if (event_sp) {
          bool keep_going = false;
          if (event_sp->GetType() == eBroadcastBitInterrupt) {
            const bool clear_thread_plans = false;
            const bool use_run_lock = false;
            Halt(clear_thread_plans, use_run_lock);
            return_value = eExpressionInterrupted;
            diagnostic_manager.PutString(lldb::eSeverityInfo,
                                         "execution halted by user interrupt.");
            LLDB_LOGF(log, "Process::RunThreadPlan(): Got  interrupted by "
                           "eBroadcastBitInterrupted, exiting.");
            break;
          } else {
            stop_state =
                Process::ProcessEventData::GetStateFromEvent(event_sp.get());
            LLDB_LOGF(log,
                      "Process::RunThreadPlan(): in while loop, got event: %s.",
                      StateAsCString(stop_state));

            switch (stop_state) {
            case lldb::eStateStopped: {
              if (Process::ProcessEventData::GetRestartedFromEvent(
                      event_sp.get())) {
                // If we were restarted, we just need to go back up to fetch
                // another event.
                LLDB_LOGF(log, "Process::RunThreadPlan(): Got a stop and "
                               "restart, so we'll continue waiting.");
                keep_going = true;
                do_resume = false;
                handle_running_event = true;
              } else {
                const bool handle_interrupts = true;
                return_value = *HandleStoppedEvent(
                    expr_thread_id, thread_plan_sp, thread_plan_restorer,
                    event_sp, event_to_broadcast_sp, options,
                    handle_interrupts);
                if (return_value == eExpressionThreadVanished)
                  keep_going = false;
              }
            } break;

            case lldb::eStateRunning:
              // This shouldn't really happen, but sometimes we do get two
              // running events without an intervening stop, and in that case
              // we should just go back to waiting for the stop.
              do_resume = false;
              keep_going = true;
              handle_running_event = false;
              break;

            default:
              LLDB_LOGF(log,
                        "Process::RunThreadPlan(): execution stopped with "
                        "unexpected state: %s.",
                        StateAsCString(stop_state));

              if (stop_state == eStateExited)
                event_to_broadcast_sp = event_sp;

              diagnostic_manager.PutString(
                  lldb::eSeverityError,
                  "execution stopped with unexpected state.");
              return_value = eExpressionInterrupted;
              break;
            }
          }

          if (keep_going)
            continue;
          else
            break;
        } else {
          if (log)
            log->PutCString("Process::RunThreadPlan(): got_event was true, but "
                            "the event pointer was null.  How odd...");
          return_value = eExpressionInterrupted;
          break;
        }
      } else {
        // If we didn't get an event that means we've timed out... We will
        // interrupt the process here.  Depending on what we were asked to do
        // we will either exit, or try with all threads running for the same
        // timeout.

        if (log) {
          if (options.GetTryAllThreads()) {
            if (before_first_timeout) {
              LLDB_LOG(log,
                       "Running function with one thread timeout timed out.");
            } else
              LLDB_LOG(log, "Restarting function with all threads enabled and "
                            "timeout: {0} timed out, abandoning execution.",
                       timeout);
          } else
            LLDB_LOG(log, "Running function with timeout: {0} timed out, "
                          "abandoning execution.",
                     timeout);
        }

        // It is possible that between the time we issued the Halt, and we get
        // around to calling Halt the target could have stopped.  That's fine,
        // Halt will figure that out and send the appropriate Stopped event.
        // BUT it is also possible that we stopped & restarted (e.g. hit a
        // signal with "stop" set to false.)  In
        // that case, we'll get the stopped & restarted event, and we should go
        // back to waiting for the Halt's stopped event.  That's what this
        // while loop does.

        bool back_to_top = true;
        uint32_t try_halt_again = 0;
        bool do_halt = true;
        const uint32_t num_retries = 5;
        while (try_halt_again < num_retries) {
          Status halt_error;
          if (do_halt) {
            LLDB_LOGF(log, "Process::RunThreadPlan(): Running Halt.");
            const bool clear_thread_plans = false;
            const bool use_run_lock = false;
            Halt(clear_thread_plans, use_run_lock);
          }
          if (halt_error.Success()) {
            if (log)
              log->PutCString("Process::RunThreadPlan(): Halt succeeded.");

            got_event =
                listener_sp->GetEvent(event_sp, GetUtilityExpressionTimeout());

            if (got_event) {
              stop_state =
                  Process::ProcessEventData::GetStateFromEvent(event_sp.get());
              if (log) {
                LLDB_LOGF(log,
                          "Process::RunThreadPlan(): Stopped with event: %s",
                          StateAsCString(stop_state));
                if (stop_state == lldb::eStateStopped &&
                    Process::ProcessEventData::GetInterruptedFromEvent(
                        event_sp.get()))
                  log->PutCString("    Event was the Halt interruption event.");
              }

              if (stop_state == lldb::eStateStopped) {
                if (Process::ProcessEventData::GetRestartedFromEvent(
                        event_sp.get())) {
                  if (log)
                    log->PutCString("Process::RunThreadPlan(): Went to halt "
                                    "but got a restarted event, there must be "
                                    "an un-restarted stopped event so try "
                                    "again...  "
                                    "Exiting wait loop.");
                  try_halt_again++;
                  do_halt = false;
                  continue;
                }

                // Between the time we initiated the Halt and the time we
                // delivered it, the process could have already finished its
                // job.  Check that here:
                const bool handle_interrupts = false;
                if (auto result = HandleStoppedEvent(
                        expr_thread_id, thread_plan_sp, thread_plan_restorer,
                        event_sp, event_to_broadcast_sp, options,
                        handle_interrupts)) {
                  return_value = *result;
                  back_to_top = false;
                  break;
                }

                if (!options.GetTryAllThreads()) {
                  if (log)
                    log->PutCString("Process::RunThreadPlan(): try_all_threads "
                                    "was false, we stopped so now we're "
                                    "quitting.");
                  return_value = eExpressionInterrupted;
                  back_to_top = false;
                  break;
                }

                if (before_first_timeout) {
                  // Set all the other threads to run, and return to the top of
                  // the loop, which will continue;
                  before_first_timeout = false;
                  thread_plan_sp->SetStopOthers(false);
                  if (log)
                    log->PutCString(
                        "Process::RunThreadPlan(): about to resume.");

                  back_to_top = true;
                  break;
                } else {
                  // Running all threads failed, so return Interrupted.
                  if (log)
                    log->PutCString("Process::RunThreadPlan(): running all "
                                    "threads timed out.");
                  return_value = eExpressionInterrupted;
                  back_to_top = false;
                  break;
                }
              }
            } else {
              if (log)
                log->PutCString("Process::RunThreadPlan(): halt said it "
                                "succeeded, but I got no event.  "
                                "I'm getting out of here passing Interrupted.");
              return_value = eExpressionInterrupted;
              back_to_top = false;
              break;
            }
          } else {
            try_halt_again++;
            continue;
          }
        }

        if (!back_to_top || try_halt_again > num_retries)
          break;
        else
          continue;
      }
    } // END WAIT LOOP

    // If we had to start up a temporary private state thread to run this
    // thread plan, shut it down now.
    if (backup_private_state_thread.IsJoinable()) {
      StopPrivateStateThread();
      Status error;
      m_private_state_thread = backup_private_state_thread;
      if (stopper_base_plan_sp) {
        thread->DiscardThreadPlansUpToPlan(stopper_base_plan_sp);
      }
      if (old_state != eStateInvalid)
        m_public_state.SetValueNoLock(old_state);
    }

    // If our thread went away on us, we need to get out of here without
    // doing any more work.  We don't have to clean up the thread plan, that
    // will have happened when the Thread was destroyed.
    if (return_value == eExpressionThreadVanished) {
      return return_value;
    }

    if (return_value != eExpressionCompleted && log) {
      // Print a backtrace into the log so we can figure out where we are:
      StreamString s;
      s.PutCString("Thread state after unsuccessful completion: \n");
      thread->GetStackFrameStatus(s, 0, UINT32_MAX, true, UINT32_MAX);
      log->PutString(s.GetString());
    }
    // Restore the thread state if we are going to discard the plan execution.
    // There are three cases where this could happen: 1) The execution
    // successfully completed 2) We hit a breakpoint, and ignore_breakpoints
    // was true 3) We got some other error, and discard_on_error was true
    bool should_unwind = (return_value == eExpressionInterrupted &&
                          options.DoesUnwindOnError()) ||
                         (return_value == eExpressionHitBreakpoint &&
                          options.DoesIgnoreBreakpoints());

    if (return_value == eExpressionCompleted || should_unwind) {
      thread_plan_sp->RestoreThreadState();
    }

    // Now do some processing on the results of the run:
    if (return_value == eExpressionInterrupted ||
        return_value == eExpressionHitBreakpoint) {
      if (log) {
        StreamString s;
        if (event_sp)
          event_sp->Dump(&s);
        else {
          log->PutCString("Process::RunThreadPlan(): Stop event that "
                          "interrupted us is NULL.");
        }

        StreamString ts;

        const char *event_explanation = nullptr;

        do {
          if (!event_sp) {
            event_explanation = "<no event>";
            break;
          } else if (event_sp->GetType() == eBroadcastBitInterrupt) {
            event_explanation = "<user interrupt>";
            break;
          } else {
            const Process::ProcessEventData *event_data =
                Process::ProcessEventData::GetEventDataFromEvent(
                    event_sp.get());

            if (!event_data) {
              event_explanation = "<no event data>";
              break;
            }

            Process *process = event_data->GetProcessSP().get();

            if (!process) {
              event_explanation = "<no process>";
              break;
            }

            ThreadList &thread_list = process->GetThreadList();

            uint32_t num_threads = thread_list.GetSize();
            uint32_t thread_index;

            ts.Printf("<%u threads> ", num_threads);

            for (thread_index = 0; thread_index < num_threads; ++thread_index) {
              Thread *thread = thread_list.GetThreadAtIndex(thread_index).get();

              if (!thread) {
                ts.Printf("<?> ");
                continue;
              }

              ts.Printf("<0x%4.4" PRIx64 " ", thread->GetID());
              RegisterContext *register_context =
                  thread->GetRegisterContext().get();

              if (register_context)
                ts.Printf("[ip 0x%" PRIx64 "] ", register_context->GetPC());
              else
                ts.Printf("[ip unknown] ");

              // Show the private stop info here, the public stop info will be
              // from the last natural stop.
              lldb::StopInfoSP stop_info_sp = thread->GetPrivateStopInfo();
              if (stop_info_sp) {
                const char *stop_desc = stop_info_sp->GetDescription();
                if (stop_desc)
                  ts.PutCString(stop_desc);
              }
              ts.Printf(">");
            }

            event_explanation = ts.GetData();
          }
        } while (false);

        if (event_explanation)
          LLDB_LOGF(log,
                    "Process::RunThreadPlan(): execution interrupted: %s %s",
                    s.GetData(), event_explanation);
        else
          LLDB_LOGF(log, "Process::RunThreadPlan(): execution interrupted: %s",
                    s.GetData());
      }

      if (should_unwind) {
        LLDB_LOGF(log,
                  "Process::RunThreadPlan: ExecutionInterrupted - "
                  "discarding thread plans up to %p.",
                  static_cast<void *>(thread_plan_sp.get()));
        thread->DiscardThreadPlansUpToPlan(thread_plan_sp);
      } else {
        LLDB_LOGF(log,
                  "Process::RunThreadPlan: ExecutionInterrupted - for "
                  "plan: %p not discarding.",
                  static_cast<void *>(thread_plan_sp.get()));
      }
    } else if (return_value == eExpressionSetupError) {
      if (log)
        log->PutCString("Process::RunThreadPlan(): execution set up error.");

      if (options.DoesUnwindOnError()) {
        thread->DiscardThreadPlansUpToPlan(thread_plan_sp);
      }
    } else {
      if (thread->IsThreadPlanDone(thread_plan_sp.get())) {
        if (log)
          log->PutCString("Process::RunThreadPlan(): thread plan is done");
        return_value = eExpressionCompleted;
      } else if (thread->WasThreadPlanDiscarded(thread_plan_sp.get())) {
        if (log)
          log->PutCString(
              "Process::RunThreadPlan(): thread plan was discarded");
        return_value = eExpressionDiscarded;
      } else {
        if (log)
          log->PutCString(
              "Process::RunThreadPlan(): thread plan stopped in mid course");
        if (options.DoesUnwindOnError() && thread_plan_sp) {
          if (log)
            log->PutCString("Process::RunThreadPlan(): discarding thread plan "
                            "'cause unwind_on_error is set.");
          thread->DiscardThreadPlansUpToPlan(thread_plan_sp);
        }
      }
    }

    // Thread we ran the function in may have gone away because we ran the
    // target Check that it's still there, and if it is put it back in the
    // context. Also restore the frame in the context if it is still present.
    thread = GetThreadList().FindThreadByIndexID(thread_idx_id, true).get();
    if (thread) {
      exe_ctx.SetFrameSP(thread->GetFrameWithStackID(ctx_frame_id));
    }

    // Also restore the current process'es selected frame & thread, since this
    // function calling may be done behind the user's back.

    if (selected_tid != LLDB_INVALID_THREAD_ID) {
      if (GetThreadList().SetSelectedThreadByIndexID(selected_tid) &&
          selected_stack_id.IsValid()) {
        // We were able to restore the selected thread, now restore the frame:
        std::lock_guard<std::recursive_mutex> guard(GetThreadList().GetMutex());
        StackFrameSP old_frame_sp =
            GetThreadList().GetSelectedThread()->GetFrameWithStackID(
                selected_stack_id);
        if (old_frame_sp)
          GetThreadList().GetSelectedThread()->SetSelectedFrame(
              old_frame_sp.get());
      }
    }
  }

  // If the process exited during the run of the thread plan, notify everyone.

  if (event_to_broadcast_sp) {
    if (log)
      log->PutCString("Process::RunThreadPlan(): rebroadcasting event.");
    BroadcastEvent(event_to_broadcast_sp);
  }

  return return_value;
}

const char *Process::ExecutionResultAsCString(ExpressionResults result) {
  const char *result_name = "<unknown>";

  switch (result) {
  case eExpressionCompleted:
    result_name = "eExpressionCompleted";
    break;
  case eExpressionDiscarded:
    result_name = "eExpressionDiscarded";
    break;
  case eExpressionInterrupted:
    result_name = "eExpressionInterrupted";
    break;
  case eExpressionHitBreakpoint:
    result_name = "eExpressionHitBreakpoint";
    break;
  case eExpressionSetupError:
    result_name = "eExpressionSetupError";
    break;
  case eExpressionParseError:
    result_name = "eExpressionParseError";
    break;
  case eExpressionResultUnavailable:
    result_name = "eExpressionResultUnavailable";
    break;
  case eExpressionTimedOut:
    result_name = "eExpressionTimedOut";
    break;
  case eExpressionStoppedForDebug:
    result_name = "eExpressionStoppedForDebug";
    break;
  case eExpressionThreadVanished:
    result_name = "eExpressionThreadVanished";
  }
  return result_name;
}

void Process::GetStatus(Stream &strm) {
  const StateType state = GetState();
  if (StateIsStoppedState(state, false)) {
    if (state == eStateExited) {
      int exit_status = GetExitStatus();
      const char *exit_description = GetExitDescription();
      strm.Printf("Process %" PRIu64 " exited with status = %i (0x%8.8x) %s\n",
                  GetID(), exit_status, exit_status,
                  exit_description ? exit_description : "");
    } else {
      if (state == eStateConnected)
        strm.Printf("Connected to remote target.\n");
      else
        strm.Printf("Process %" PRIu64 " %s\n", GetID(), StateAsCString(state));
    }
  } else {
    strm.Printf("Process %" PRIu64 " is running.\n", GetID());
  }
}

size_t Process::GetThreadStatus(Stream &strm,
                                bool only_threads_with_stop_reason,
                                uint32_t start_frame, uint32_t num_frames,
                                uint32_t num_frames_with_source,
                                bool stop_format) {
  size_t num_thread_infos_dumped = 0;

  // You can't hold the thread list lock while calling Thread::GetStatus.  That
  // very well might run code (e.g. if we need it to get return values or
  // arguments.)  For that to work the process has to be able to acquire it.
  // So instead copy the thread ID's, and look them up one by one:

  uint32_t num_threads;
  std::vector<lldb::tid_t> thread_id_array;
  // Scope for thread list locker;
  {
    std::lock_guard<std::recursive_mutex> guard(GetThreadList().GetMutex());
    ThreadList &curr_thread_list = GetThreadList();
    num_threads = curr_thread_list.GetSize();
    uint32_t idx;
    thread_id_array.resize(num_threads);
    for (idx = 0; idx < num_threads; ++idx)
      thread_id_array[idx] = curr_thread_list.GetThreadAtIndex(idx)->GetID();
  }

  for (uint32_t i = 0; i < num_threads; i++) {
    ThreadSP thread_sp(GetThreadList().FindThreadByID(thread_id_array[i]));
    if (thread_sp) {
      if (only_threads_with_stop_reason) {
        StopInfoSP stop_info_sp = thread_sp->GetStopInfo();
        if (!stop_info_sp || !stop_info_sp->IsValid())
          continue;
      }
      thread_sp->GetStatus(strm, start_frame, num_frames,
                           num_frames_with_source,
                           stop_format);
      ++num_thread_infos_dumped;
    } else {
      Log *log = GetLog(LLDBLog::Process);
      LLDB_LOGF(log, "Process::GetThreadStatus - thread 0x" PRIu64
                     " vanished while running Thread::GetStatus.");
    }
  }
  return num_thread_infos_dumped;
}

void Process::AddInvalidMemoryRegion(const LoadRange &region) {
  m_memory_cache.AddInvalidRange(region.GetRangeBase(), region.GetByteSize());
}

bool Process::RemoveInvalidMemoryRange(const LoadRange &region) {
  return m_memory_cache.RemoveInvalidRange(region.GetRangeBase(),
                                           region.GetByteSize());
}

void Process::AddPreResumeAction(PreResumeActionCallback callback,
                                 void *baton) {
  m_pre_resume_actions.push_back(PreResumeCallbackAndBaton(callback, baton));
}

bool Process::RunPreResumeActions() {
  bool result = true;
  while (!m_pre_resume_actions.empty()) {
    struct PreResumeCallbackAndBaton action = m_pre_resume_actions.back();
    m_pre_resume_actions.pop_back();
    bool this_result = action.callback(action.baton);
    if (result)
      result = this_result;
  }
  return result;
}

void Process::ClearPreResumeActions() { m_pre_resume_actions.clear(); }

void Process::ClearPreResumeAction(PreResumeActionCallback callback, void *baton)
{
    PreResumeCallbackAndBaton element(callback, baton);
    auto found_iter = std::find(m_pre_resume_actions.begin(), m_pre_resume_actions.end(), element);
    if (found_iter != m_pre_resume_actions.end())
    {
        m_pre_resume_actions.erase(found_iter);
    }
}

ProcessRunLock &Process::GetRunLock() {
  if (m_private_state_thread.EqualsThread(Host::GetCurrentThread()))
    return m_private_run_lock;
  else
    return m_public_run_lock;
}

bool Process::CurrentThreadIsPrivateStateThread()
{
  return m_private_state_thread.EqualsThread(Host::GetCurrentThread());
}


void Process::Flush() {
  m_thread_list.Flush();
  m_extended_thread_list.Flush();
  m_extended_thread_stop_id = 0;
  m_queue_list.Clear();
  m_queue_list_stop_id = 0;
}

lldb::addr_t Process::GetCodeAddressMask() {
  if (uint32_t num_bits_setting = GetVirtualAddressableBits())
    return AddressableBits::AddressableBitToMask(num_bits_setting);

  return m_code_address_mask;
}

lldb::addr_t Process::GetDataAddressMask() {
  if (uint32_t num_bits_setting = GetVirtualAddressableBits())
    return AddressableBits::AddressableBitToMask(num_bits_setting);

  return m_data_address_mask;
}

lldb::addr_t Process::GetHighmemCodeAddressMask() {
  if (uint32_t num_bits_setting = GetHighmemVirtualAddressableBits())
    return AddressableBits::AddressableBitToMask(num_bits_setting);

  if (m_highmem_code_address_mask != LLDB_INVALID_ADDRESS_MASK)
    return m_highmem_code_address_mask;
  return GetCodeAddressMask();
}

lldb::addr_t Process::GetHighmemDataAddressMask() {
  if (uint32_t num_bits_setting = GetHighmemVirtualAddressableBits())
    return AddressableBits::AddressableBitToMask(num_bits_setting);

  if (m_highmem_data_address_mask != LLDB_INVALID_ADDRESS_MASK)
    return m_highmem_data_address_mask;
  return GetDataAddressMask();
}

void Process::SetCodeAddressMask(lldb::addr_t code_address_mask) {
  LLDB_LOG(GetLog(LLDBLog::Process),
           "Setting Process code address mask to {0:x}", code_address_mask);
  m_code_address_mask = code_address_mask;
}

void Process::SetDataAddressMask(lldb::addr_t data_address_mask) {
  LLDB_LOG(GetLog(LLDBLog::Process),
           "Setting Process data address mask to {0:x}", data_address_mask);
  m_data_address_mask = data_address_mask;
}

void Process::SetHighmemCodeAddressMask(lldb::addr_t code_address_mask) {
  LLDB_LOG(GetLog(LLDBLog::Process),
           "Setting Process highmem code address mask to {0:x}",
           code_address_mask);
  m_highmem_code_address_mask = code_address_mask;
}

void Process::SetHighmemDataAddressMask(lldb::addr_t data_address_mask) {
  LLDB_LOG(GetLog(LLDBLog::Process),
           "Setting Process highmem data address mask to {0:x}",
           data_address_mask);
  m_highmem_data_address_mask = data_address_mask;
}

addr_t Process::FixCodeAddress(addr_t addr) {
  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixCodeAddress(addr);
  return addr;
}

addr_t Process::FixDataAddress(addr_t addr) {
  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixDataAddress(addr);
  return addr;
}

addr_t Process::FixAnyAddress(addr_t addr) {
  if (ABISP abi_sp = GetABI())
    addr = abi_sp->FixAnyAddress(addr);
  return addr;
}

void Process::DidExec() {
  Log *log = GetLog(LLDBLog::Process);
  LLDB_LOGF(log, "Process::%s()", __FUNCTION__);

  Target &target = GetTarget();
  target.CleanupProcess();
  target.ClearModules(false);
  m_dynamic_checkers_up.reset();
  m_abi_sp.reset();
  m_system_runtime_up.reset();
  m_os_up.reset();
  m_dyld_up.reset();
  m_jit_loaders_up.reset();
  m_image_tokens.clear();
  // After an exec, the inferior is a new process and these memory regions are
  // no longer allocated.
  m_allocated_memory_cache.Clear(/*deallocte_memory=*/false);
  {
    std::lock_guard<std::recursive_mutex> guard(m_language_runtimes_mutex);
    m_language_runtimes.clear();
  }
  m_instrumentation_runtimes.clear();
  m_thread_list.DiscardThreadPlans();
  m_memory_cache.Clear(true);
  DoDidExec();
  CompleteAttach();
  // Flush the process (threads and all stack frames) after running
  // CompleteAttach() in case the dynamic loader loaded things in new
  // locations.
  Flush();

  // After we figure out what was loaded/unloaded in CompleteAttach, we need to
  // let the target know so it can do any cleanup it needs to.
  target.DidExec();
}

addr_t Process::ResolveIndirectFunction(const Address *address, Status &error) {
  if (address == nullptr) {
    error.SetErrorString("Invalid address argument");
    return LLDB_INVALID_ADDRESS;
  }

  addr_t function_addr = LLDB_INVALID_ADDRESS;

  addr_t addr = address->GetLoadAddress(&GetTarget());
  std::map<addr_t, addr_t>::const_iterator iter =
      m_resolved_indirect_addresses.find(addr);
  if (iter != m_resolved_indirect_addresses.end()) {
    function_addr = (*iter).second;
  } else {
    if (!CallVoidArgVoidPtrReturn(address, function_addr)) {
      Symbol *symbol = address->CalculateSymbolContextSymbol();
      error.SetErrorStringWithFormat(
          "Unable to call resolver for indirect function %s",
          symbol ? symbol->GetName().AsCString() : "<UNKNOWN>");
      function_addr = LLDB_INVALID_ADDRESS;
    } else {
      if (ABISP abi_sp = GetABI())
        function_addr = abi_sp->FixCodeAddress(function_addr);
      m_resolved_indirect_addresses.insert(
          std::pair<addr_t, addr_t>(addr, function_addr));
    }
  }
  return function_addr;
}

void Process::ModulesDidLoad(ModuleList &module_list) {
  // Inform the system runtime of the modified modules.
  SystemRuntime *sys_runtime = GetSystemRuntime();
  if (sys_runtime)
    sys_runtime->ModulesDidLoad(module_list);

  GetJITLoaders().ModulesDidLoad(module_list);

  // Give the instrumentation runtimes a chance to be created before informing
  // them of the modified modules.
  InstrumentationRuntime::ModulesDidLoad(module_list, this,
                                         m_instrumentation_runtimes);
  for (auto &runtime : m_instrumentation_runtimes)
    runtime.second->ModulesDidLoad(module_list);

  // Give the language runtimes a chance to be created before informing them of
  // the modified modules.
  for (const lldb::LanguageType lang_type : Language::GetSupportedLanguages()) {
    if (LanguageRuntime *runtime = GetLanguageRuntime(lang_type))
      runtime->ModulesDidLoad(module_list);
  }

  // If we don't have an operating system plug-in, try to load one since
  // loading shared libraries might cause a new one to try and load
  if (!m_os_up)
    LoadOperatingSystemPlugin(false);

  // Inform the structured-data plugins of the modified modules.
  for (auto &pair : m_structured_data_plugin_map) {
    if (pair.second)
      pair.second->ModulesDidLoad(*this, module_list);
  }
}

void Process::PrintWarningOptimization(const SymbolContext &sc) {
  if (!GetWarningsOptimization())
    return;
  if (!sc.module_sp || !sc.function || !sc.function->GetIsOptimized())
    return;
  sc.module_sp->ReportWarningOptimization(GetTarget().GetDebugger().GetID());
}

void Process::PrintWarningUnsupportedLanguage(const SymbolContext &sc) {
  if (!GetWarningsUnsupportedLanguage())
    return;
  if (!sc.module_sp)
    return;
  LanguageType language = sc.GetLanguage();
  if (language == eLanguageTypeUnknown ||
      language == lldb::eLanguageTypeAssembly ||
      language == lldb::eLanguageTypeMipsAssembler)
    return;
  LanguageSet plugins =
      PluginManager::GetAllTypeSystemSupportedLanguagesForTypes();
  if (plugins[language])
    return;
  sc.module_sp->ReportWarningUnsupportedLanguage(
      language, GetTarget().GetDebugger().GetID());
}

bool Process::GetProcessInfo(ProcessInstanceInfo &info) {
  info.Clear();

  PlatformSP platform_sp = GetTarget().GetPlatform();
  if (!platform_sp)
    return false;

  return platform_sp->GetProcessInfo(GetID(), info);
}

ThreadCollectionSP Process::GetHistoryThreads(lldb::addr_t addr) {
  ThreadCollectionSP threads;

  const MemoryHistorySP &memory_history =
      MemoryHistory::FindPlugin(shared_from_this());

  if (!memory_history) {
    return threads;
  }

  threads = std::make_shared<ThreadCollection>(
      memory_history->GetHistoryThreads(addr));

  return threads;
}

InstrumentationRuntimeSP
Process::GetInstrumentationRuntime(lldb::InstrumentationRuntimeType type) {
  InstrumentationRuntimeCollection::iterator pos;
  pos = m_instrumentation_runtimes.find(type);
  if (pos == m_instrumentation_runtimes.end()) {
    return InstrumentationRuntimeSP();
  } else
    return (*pos).second;
}

bool Process::GetModuleSpec(const FileSpec &module_file_spec,
                            const ArchSpec &arch, ModuleSpec &module_spec) {
  module_spec.Clear();
  return false;
}

size_t Process::AddImageToken(lldb::addr_t image_ptr) {
  m_image_tokens.push_back(image_ptr);
  return m_image_tokens.size() - 1;
}

lldb::addr_t Process::GetImagePtrFromToken(size_t token) const {
  if (token < m_image_tokens.size())
    return m_image_tokens[token];
  return LLDB_INVALID_IMAGE_TOKEN;
}

void Process::ResetImageToken(size_t token) {
  if (token < m_image_tokens.size())
    m_image_tokens[token] = LLDB_INVALID_IMAGE_TOKEN;
}

Address
Process::AdvanceAddressToNextBranchInstruction(Address default_stop_addr,
                                               AddressRange range_bounds) {
  Target &target = GetTarget();
  DisassemblerSP disassembler_sp;
  InstructionList *insn_list = nullptr;

  Address retval = default_stop_addr;

  if (!target.GetUseFastStepping())
    return retval;
  if (!default_stop_addr.IsValid())
    return retval;

  const char *plugin_name = nullptr;
  const char *flavor = nullptr;
  disassembler_sp = Disassembler::DisassembleRange(
      target.GetArchitecture(), plugin_name, flavor, GetTarget(), range_bounds);
  if (disassembler_sp)
    insn_list = &disassembler_sp->GetInstructionList();

  if (insn_list == nullptr) {
    return retval;
  }

  size_t insn_offset =
      insn_list->GetIndexOfInstructionAtAddress(default_stop_addr);
  if (insn_offset == UINT32_MAX) {
    return retval;
  }

  uint32_t branch_index = insn_list->GetIndexOfNextBranchInstruction(
      insn_offset, false /* ignore_calls*/, nullptr);
  if (branch_index == UINT32_MAX) {
    return retval;
  }

  if (branch_index > insn_offset) {
    Address next_branch_insn_address =
        insn_list->GetInstructionAtIndex(branch_index)->GetAddress();
    if (next_branch_insn_address.IsValid() &&
        range_bounds.ContainsFileAddress(next_branch_insn_address)) {
      retval = next_branch_insn_address;
    }
  }

  return retval;
}

Status Process::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                    MemoryRegionInfo &range_info) {
  if (const lldb::ABISP &abi = GetABI())
    load_addr = abi->FixAnyAddress(load_addr);
  return DoGetMemoryRegionInfo(load_addr, range_info);
}

Status Process::GetMemoryRegions(lldb_private::MemoryRegionInfos &region_list) {
  Status error;

  lldb::addr_t range_end = 0;
  const lldb::ABISP &abi = GetABI();

  region_list.clear();
  do {
    lldb_private::MemoryRegionInfo region_info;
    error = GetMemoryRegionInfo(range_end, region_info);
    // GetMemoryRegionInfo should only return an error if it is unimplemented.
    if (error.Fail()) {
      region_list.clear();
      break;
    }

    // We only check the end address, not start and end, because we assume that
    // the start will not have non-address bits until the first unmappable
    // region. We will have exited the loop by that point because the previous
    // region, the last mappable region, will have non-address bits in its end
    // address.
    range_end = region_info.GetRange().GetRangeEnd();
    if (region_info.GetMapped() == MemoryRegionInfo::eYes) {
      region_list.push_back(std::move(region_info));
    }
  } while (
      // For a process with no non-address bits, all address bits
      // set means the end of memory.
      range_end != LLDB_INVALID_ADDRESS &&
      // If we have non-address bits and some are set then the end
      // is at or beyond the end of mappable memory.
      !(abi && (abi->FixAnyAddress(range_end) != range_end)));

  return error;
}

Status
Process::ConfigureStructuredData(llvm::StringRef type_name,
                                 const StructuredData::ObjectSP &config_sp) {
  // If you get this, the Process-derived class needs to implement a method to
  // enable an already-reported asynchronous structured data feature. See
  // ProcessGDBRemote for an example implementation over gdb-remote.
  return Status("unimplemented");
}

void Process::MapSupportedStructuredDataPlugins(
    const StructuredData::Array &supported_type_names) {
  Log *log = GetLog(LLDBLog::Process);

  // Bail out early if there are no type names to map.
  if (supported_type_names.GetSize() == 0) {
    LLDB_LOG(log, "no structured data types supported");
    return;
  }

  // These StringRefs are backed by the input parameter.
  std::set<llvm::StringRef> type_names;

  LLDB_LOG(log,
           "the process supports the following async structured data types:");

  supported_type_names.ForEach(
      [&type_names, &log](StructuredData::Object *object) {
        // There shouldn't be null objects in the array.
        if (!object)
          return false;

        // All type names should be strings.
        const llvm::StringRef type_name = object->GetStringValue();
        if (type_name.empty())
          return false;

        type_names.insert(type_name);
        LLDB_LOG(log, "- {0}", type_name);
        return true;
      });

  // For each StructuredDataPlugin, if the plugin handles any of the types in
  // the supported_type_names, map that type name to that plugin. Stop when
  // we've consumed all the type names.
  // FIXME: should we return an error if there are type names nobody
  // supports?
  for (uint32_t plugin_index = 0; !type_names.empty(); plugin_index++) {
    auto create_instance =
        PluginManager::GetStructuredDataPluginCreateCallbackAtIndex(
            plugin_index);
    if (!create_instance)
      break;

    // Create the plugin.
    StructuredDataPluginSP plugin_sp = (*create_instance)(*this);
    if (!plugin_sp) {
      // This plugin doesn't think it can work with the process. Move on to the
      // next.
      continue;
    }

    // For any of the remaining type names, map any that this plugin supports.
    std::vector<llvm::StringRef> names_to_remove;
    for (llvm::StringRef type_name : type_names) {
      if (plugin_sp->SupportsStructuredDataType(type_name)) {
        m_structured_data_plugin_map.insert(
            std::make_pair(type_name, plugin_sp));
        names_to_remove.push_back(type_name);
        LLDB_LOG(log, "using plugin {0} for type name {1}",
                 plugin_sp->GetPluginName(), type_name);
      }
    }

    // Remove the type names that were consumed by this plugin.
    for (llvm::StringRef type_name : names_to_remove)
      type_names.erase(type_name);
  }
}

bool Process::RouteAsyncStructuredData(
    const StructuredData::ObjectSP object_sp) {
  // Nothing to do if there's no data.
  if (!object_sp)
    return false;

  // The contract is this must be a dictionary, so we can look up the routing
  // key via the top-level 'type' string value within the dictionary.
  StructuredData::Dictionary *dictionary = object_sp->GetAsDictionary();
  if (!dictionary)
    return false;

  // Grab the async structured type name (i.e. the feature/plugin name).
  llvm::StringRef type_name;
  if (!dictionary->GetValueForKeyAsString("type", type_name))
    return false;

  // Check if there's a plugin registered for this type name.
  auto find_it = m_structured_data_plugin_map.find(type_name);
  if (find_it == m_structured_data_plugin_map.end()) {
    // We don't have a mapping for this structured data type.
    return false;
  }

  // Route the structured data to the plugin.
  find_it->second->HandleArrivalOfStructuredData(*this, type_name, object_sp);
  return true;
}

Status Process::UpdateAutomaticSignalFiltering() {
  // Default implementation does nothign.
  // No automatic signal filtering to speak of.
  return Status();
}

UtilityFunction *Process::GetLoadImageUtilityFunction(
    Platform *platform,
    llvm::function_ref<std::unique_ptr<UtilityFunction>()> factory) {
  if (platform != GetTarget().GetPlatform().get())
    return nullptr;
  llvm::call_once(m_dlopen_utility_func_flag_once,
                  [&] { m_dlopen_utility_func_up = factory(); });
  return m_dlopen_utility_func_up.get();
}

llvm::Expected<TraceSupportedResponse> Process::TraceSupported() {
  if (!IsLiveDebugSession())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Can't trace a non-live process.");
  return llvm::make_error<UnimplementedError>();
}

bool Process::CallVoidArgVoidPtrReturn(const Address *address,
                                       addr_t &returned_func,
                                       bool trap_exceptions) {
  Thread *thread = GetThreadList().GetExpressionExecutionThread().get();
  if (thread == nullptr || address == nullptr)
    return false;

  EvaluateExpressionOptions options;
  options.SetStopOthers(true);
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTryAllThreads(true);
  options.SetDebug(false);
  options.SetTimeout(GetUtilityExpressionTimeout());
  options.SetTrapExceptions(trap_exceptions);

  auto type_system_or_err =
      GetTarget().GetScratchTypeSystemForLanguage(eLanguageTypeC);
  if (!type_system_or_err) {
    llvm::consumeError(type_system_or_err.takeError());
    return false;
  }
  auto ts = *type_system_or_err;
  if (!ts)
    return false;
  CompilerType void_ptr_type =
      ts->GetBasicTypeFromAST(eBasicTypeVoid).GetPointerType();
  lldb::ThreadPlanSP call_plan_sp(new ThreadPlanCallFunction(
      *thread, *address, void_ptr_type, llvm::ArrayRef<addr_t>(), options));
  if (call_plan_sp) {
    DiagnosticManager diagnostics;

    StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
    if (frame) {
      ExecutionContext exe_ctx;
      frame->CalculateExecutionContext(exe_ctx);
      ExpressionResults result =
          RunThreadPlan(exe_ctx, call_plan_sp, options, diagnostics);
      if (result == eExpressionCompleted) {
        returned_func =
            call_plan_sp->GetReturnValueObject()->GetValueAsUnsigned(
                LLDB_INVALID_ADDRESS);

        if (GetAddressByteSize() == 4) {
          if (returned_func == UINT32_MAX)
            return false;
        } else if (GetAddressByteSize() == 8) {
          if (returned_func == UINT64_MAX)
            return false;
        }
        return true;
      }
    }
  }

  return false;
}

llvm::Expected<const MemoryTagManager *> Process::GetMemoryTagManager() {
  Architecture *arch = GetTarget().GetArchitecturePlugin();
  const MemoryTagManager *tag_manager =
      arch ? arch->GetMemoryTagManager() : nullptr;
  if (!arch || !tag_manager) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "This architecture does not support memory tagging");
  }

  if (!SupportsMemoryTagging()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Process does not support memory tagging");
  }

  return tag_manager;
}

llvm::Expected<std::vector<lldb::addr_t>>
Process::ReadMemoryTags(lldb::addr_t addr, size_t len) {
  llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
      GetMemoryTagManager();
  if (!tag_manager_or_err)
    return tag_manager_or_err.takeError();

  const MemoryTagManager *tag_manager = *tag_manager_or_err;
  llvm::Expected<std::vector<uint8_t>> tag_data =
      DoReadMemoryTags(addr, len, tag_manager->GetAllocationTagType());
  if (!tag_data)
    return tag_data.takeError();

  return tag_manager->UnpackTagsData(*tag_data,
                                     len / tag_manager->GetGranuleSize());
}

Status Process::WriteMemoryTags(lldb::addr_t addr, size_t len,
                                const std::vector<lldb::addr_t> &tags) {
  llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
      GetMemoryTagManager();
  if (!tag_manager_or_err)
    return Status(tag_manager_or_err.takeError());

  const MemoryTagManager *tag_manager = *tag_manager_or_err;
  llvm::Expected<std::vector<uint8_t>> packed_tags =
      tag_manager->PackTags(tags);
  if (!packed_tags) {
    return Status(packed_tags.takeError());
  }

  return DoWriteMemoryTags(addr, len, tag_manager->GetAllocationTagType(),
                           *packed_tags);
}

// Create a CoreFileMemoryRange from a MemoryRegionInfo
static Process::CoreFileMemoryRange
CreateCoreFileMemoryRange(const MemoryRegionInfo &region) {
  const addr_t addr = region.GetRange().GetRangeBase();
  llvm::AddressRange range(addr, addr + region.GetRange().GetByteSize());
  return {range, region.GetLLDBPermissions()};
}

// Add dirty pages to the core file ranges and return true if dirty pages
// were added. Return false if the dirty page information is not valid or in
// the region.
static bool AddDirtyPages(const MemoryRegionInfo &region,
                          Process::CoreFileMemoryRanges &ranges) {
  const auto &dirty_page_list = region.GetDirtyPageList();
  if (!dirty_page_list)
    return false;
  const uint32_t lldb_permissions = region.GetLLDBPermissions();
  const addr_t page_size = region.GetPageSize();
  if (page_size == 0)
    return false;
  llvm::AddressRange range(0, 0);
  for (addr_t page_addr : *dirty_page_list) {
    if (range.empty()) {
      // No range yet, initialize the range with the current dirty page.
      range = llvm::AddressRange(page_addr, page_addr + page_size);
    } else {
      if (range.end() == page_addr) {
        // Combine consective ranges.
        range = llvm::AddressRange(range.start(), page_addr + page_size);
      } else {
        // Add previous contiguous range and init the new range with the
        // current dirty page.
        ranges.push_back({range, lldb_permissions});
        range = llvm::AddressRange(page_addr, page_addr + page_size);
      }
    }
  }
  // The last range
  if (!range.empty())
    ranges.push_back({range, lldb_permissions});
  return true;
}

// Given a region, add the region to \a ranges.
//
// Only add the region if it isn't empty and if it has some permissions.
// If \a try_dirty_pages is true, then try to add only the dirty pages for a
// given region. If the region has dirty page information, only dirty pages
// will be added to \a ranges, else the entire range will be added to \a
// ranges.
static void AddRegion(const MemoryRegionInfo &region, bool try_dirty_pages,
                      Process::CoreFileMemoryRanges &ranges) {
  // Don't add empty ranges.
  if (region.GetRange().GetByteSize() == 0)
    return;
  // Don't add ranges with no read permissions.
  if ((region.GetLLDBPermissions() & lldb::ePermissionsReadable) == 0)
    return;
  if (try_dirty_pages && AddDirtyPages(region, ranges))
    return;
  ranges.push_back(CreateCoreFileMemoryRange(region));
}

static void SaveOffRegionsWithStackPointers(
    Process &process, const MemoryRegionInfos &regions,
    Process::CoreFileMemoryRanges &ranges, std::set<addr_t> &stack_ends) {
  const bool try_dirty_pages = true;

  // Before we take any dump, we want to save off the used portions of the
  // stacks and mark those memory regions as saved. This prevents us from saving
  // the unused portion of the stack below the stack pointer. Saving space on
  // the dump.
  for (lldb::ThreadSP thread_sp : process.GetThreadList().Threads()) {
    if (!thread_sp)
      continue;
    StackFrameSP frame_sp = thread_sp->GetStackFrameAtIndex(0);
    if (!frame_sp)
      continue;
    RegisterContextSP reg_ctx_sp = frame_sp->GetRegisterContext();
    if (!reg_ctx_sp)
      continue;
    const addr_t sp = reg_ctx_sp->GetSP();
    const size_t red_zone = process.GetABI()->GetRedZoneSize();
    lldb_private::MemoryRegionInfo sp_region;
    if (process.GetMemoryRegionInfo(sp, sp_region).Success()) {
      const size_t stack_head = (sp - red_zone);
      const size_t stack_size = sp_region.GetRange().GetRangeEnd() - stack_head;
      sp_region.GetRange().SetRangeBase(stack_head);
      sp_region.GetRange().SetByteSize(stack_size);
      stack_ends.insert(sp_region.GetRange().GetRangeEnd());
      AddRegion(sp_region, try_dirty_pages, ranges);
    }
  }
}

// Save all memory regions that are not empty or have at least some permissions
// for a full core file style.
static void GetCoreFileSaveRangesFull(Process &process,
                                      const MemoryRegionInfos &regions,
                                      Process::CoreFileMemoryRanges &ranges,
                                      std::set<addr_t> &stack_ends) {

  // Don't add only dirty pages, add full regions.
const bool try_dirty_pages = false;
  for (const auto &region : regions)
    if (stack_ends.count(region.GetRange().GetRangeEnd()) == 0)
      AddRegion(region, try_dirty_pages, ranges);
}

// Save only the dirty pages to the core file. Make sure the process has at
// least some dirty pages, as some OS versions don't support reporting what
// pages are dirty within an memory region. If no memory regions have dirty
// page information fall back to saving out all ranges with write permissions.
static void GetCoreFileSaveRangesDirtyOnly(
    Process &process, const MemoryRegionInfos &regions,
    Process::CoreFileMemoryRanges &ranges, std::set<addr_t> &stack_ends) {

  // Iterate over the regions and find all dirty pages.
  bool have_dirty_page_info = false;
  for (const auto &region : regions) {
    if (stack_ends.count(region.GetRange().GetRangeEnd()) == 0 &&
        AddDirtyPages(region, ranges))
      have_dirty_page_info = true;
  }

  if (!have_dirty_page_info) {
    // We didn't find support for reporting dirty pages from the process
    // plug-in so fall back to any region with write access permissions.
    const bool try_dirty_pages = false;
    for (const auto &region : regions)
      if (stack_ends.count(region.GetRange().GetRangeEnd()) == 0 &&
          region.GetWritable() == MemoryRegionInfo::eYes)
        AddRegion(region, try_dirty_pages, ranges);
  }
}

// Save all thread stacks to the core file. Some OS versions support reporting
// when a memory region is stack related. We check on this information, but we
// also use the stack pointers of each thread and add those in case the OS
// doesn't support reporting stack memory. This function also attempts to only
// emit dirty pages from the stack if the memory regions support reporting
// dirty regions as this will make the core file smaller. If the process
// doesn't support dirty regions, then it will fall back to adding the full
// stack region.
static void GetCoreFileSaveRangesStackOnly(
    Process &process, const MemoryRegionInfos &regions,
    Process::CoreFileMemoryRanges &ranges, std::set<addr_t> &stack_ends) {
  const bool try_dirty_pages = true;
  // Some platforms support annotating the region information that tell us that
  // it comes from a thread stack. So look for those regions first.

  for (const auto &region : regions) {
    // Save all the stack memory ranges not associated with a stack pointer.
    if (stack_ends.count(region.GetRange().GetRangeEnd()) == 0 &&
        region.IsStackMemory() == MemoryRegionInfo::eYes)
      AddRegion(region, try_dirty_pages, ranges);
  }
}

Status Process::CalculateCoreFileSaveRanges(lldb::SaveCoreStyle core_style,
                                            CoreFileMemoryRanges &ranges) {
  lldb_private::MemoryRegionInfos regions;
  Status err = GetMemoryRegions(regions);
  if (err.Fail())
    return err;
  if (regions.empty())
    return Status("failed to get any valid memory regions from the process");
  if (core_style == eSaveCoreUnspecified)
    return Status("callers must set the core_style to something other than "
                  "eSaveCoreUnspecified");

  std::set<addr_t> stack_ends;
  SaveOffRegionsWithStackPointers(*this, regions, ranges, stack_ends);

  switch (core_style) {
  case eSaveCoreUnspecified:
    break;

  case eSaveCoreFull:
    GetCoreFileSaveRangesFull(*this, regions, ranges, stack_ends);
    break;

  case eSaveCoreDirtyOnly:
    GetCoreFileSaveRangesDirtyOnly(*this, regions, ranges, stack_ends);
    break;

  case eSaveCoreStackOnly:
    GetCoreFileSaveRangesStackOnly(*this, regions, ranges, stack_ends);
    break;
  }

  if (err.Fail())
    return err;

  if (ranges.empty())
    return Status("no valid address ranges found for core style");

  return Status(); // Success!
}

void Process::SetAddressableBitMasks(AddressableBits bit_masks) {
  uint32_t low_memory_addr_bits = bit_masks.GetLowmemAddressableBits();
  uint32_t high_memory_addr_bits = bit_masks.GetHighmemAddressableBits();

  if (low_memory_addr_bits == 0 && high_memory_addr_bits == 0)
    return;

  if (low_memory_addr_bits != 0) {
    addr_t low_addr_mask =
        AddressableBits::AddressableBitToMask(low_memory_addr_bits);
    SetCodeAddressMask(low_addr_mask);
    SetDataAddressMask(low_addr_mask);
  }

  if (high_memory_addr_bits != 0) {
    addr_t high_addr_mask =
        AddressableBits::AddressableBitToMask(high_memory_addr_bits);
    SetHighmemCodeAddressMask(high_addr_mask);
    SetHighmemDataAddressMask(high_addr_mask);
  }
}
