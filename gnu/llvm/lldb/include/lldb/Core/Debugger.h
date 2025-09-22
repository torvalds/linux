//===-- Debugger.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DEBUGGER_H
#define LLDB_CORE_DEBUGGER_H

#include <cstdint>

#include <memory>
#include <optional>
#include <vector>

#include "lldb/Core/DebuggerEvents.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Diagnostics.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Threading.h"

#include <cassert>
#include <cstddef>
#include <cstdio>

namespace llvm {
class raw_ostream;
class ThreadPoolInterface;
} // namespace llvm

namespace lldb_private {
class Address;
class CallbackLogHandler;
class CommandInterpreter;
class LogHandler;
class Process;
class Stream;
class SymbolContext;
class Target;

namespace repro {
class DataRecorder;
}

/// \class Debugger Debugger.h "lldb/Core/Debugger.h"
/// A class to manage flag bits.
///
/// Provides a global root objects for the debugger core.

class Debugger : public std::enable_shared_from_this<Debugger>,
                 public UserID,
                 public Properties {
public:
  using DebuggerList = std::vector<lldb::DebuggerSP>;

  static llvm::StringRef GetStaticBroadcasterClass();

  /// Get the public broadcaster for this debugger.
  Broadcaster &GetBroadcaster() { return m_broadcaster; }
  const Broadcaster &GetBroadcaster() const { return m_broadcaster; }

  ~Debugger() override;

  static lldb::DebuggerSP
  CreateInstance(lldb::LogOutputCallback log_callback = nullptr,
                 void *baton = nullptr);

  static lldb::TargetSP FindTargetWithProcessID(lldb::pid_t pid);

  static lldb::TargetSP FindTargetWithProcess(Process *process);

  static void Initialize(LoadPluginCallbackType load_plugin_callback);

  static void Terminate();

  static void SettingsInitialize();

  static void SettingsTerminate();

  static void Destroy(lldb::DebuggerSP &debugger_sp);

  static lldb::DebuggerSP FindDebuggerWithID(lldb::user_id_t id);

  static lldb::DebuggerSP
  FindDebuggerWithInstanceName(llvm::StringRef instance_name);

  static size_t GetNumDebuggers();

  static lldb::DebuggerSP GetDebuggerAtIndex(size_t index);

  static bool FormatDisassemblerAddress(const FormatEntity::Entry *format,
                                        const SymbolContext *sc,
                                        const SymbolContext *prev_sc,
                                        const ExecutionContext *exe_ctx,
                                        const Address *addr, Stream &s);

  static void AssertCallback(llvm::StringRef message, llvm::StringRef backtrace,
                             llvm::StringRef prompt);

  void Clear();

  bool GetAsyncExecution();

  void SetAsyncExecution(bool async);

  lldb::FileSP GetInputFileSP() { return m_input_file_sp; }

  lldb::StreamFileSP GetOutputStreamSP() { return m_output_stream_sp; }

  lldb::StreamFileSP GetErrorStreamSP() { return m_error_stream_sp; }

  File &GetInputFile() { return *m_input_file_sp; }

  File &GetOutputFile() { return m_output_stream_sp->GetFile(); }

  File &GetErrorFile() { return m_error_stream_sp->GetFile(); }

  StreamFile &GetOutputStream() { return *m_output_stream_sp; }

  StreamFile &GetErrorStream() { return *m_error_stream_sp; }

  repro::DataRecorder *GetInputRecorder();

  Status SetInputString(const char *data);

  void SetInputFile(lldb::FileSP file);

  void SetOutputFile(lldb::FileSP file);

  void SetErrorFile(lldb::FileSP file);

  void SaveInputTerminalState();

  void RestoreInputTerminalState();

  lldb::StreamSP GetAsyncOutputStream();

  lldb::StreamSP GetAsyncErrorStream();

  CommandInterpreter &GetCommandInterpreter() {
    assert(m_command_interpreter_up.get());
    return *m_command_interpreter_up;
  }

  ScriptInterpreter *
  GetScriptInterpreter(bool can_create = true,
                       std::optional<lldb::ScriptLanguage> language = {});

  lldb::ListenerSP GetListener() { return m_listener_sp; }

  // This returns the Debugger's scratch source manager.  It won't be able to
  // look up files in debug information, but it can look up files by absolute
  // path and display them to you. To get the target's source manager, call
  // GetSourceManager on the target instead.
  SourceManager &GetSourceManager();

  lldb::TargetSP GetSelectedTarget() {
    return m_target_list.GetSelectedTarget();
  }

  ExecutionContext GetSelectedExecutionContext();
  /// Get accessor for the target list.
  ///
  /// The target list is part of the global debugger object. This the single
  /// debugger shared instance to control where targets get created and to
  /// allow for tracking and searching for targets based on certain criteria.
  ///
  /// \return
  ///     A global shared target list.
  TargetList &GetTargetList() { return m_target_list; }

  PlatformList &GetPlatformList() { return m_platform_list; }

  void DispatchInputInterrupt();

  void DispatchInputEndOfFile();

  // If any of the streams are not set, set them to the in/out/err stream of
  // the top most input reader to ensure they at least have something
  void AdoptTopIOHandlerFilesIfInvalid(lldb::FileSP &in,
                                       lldb::StreamFileSP &out,
                                       lldb::StreamFileSP &err);

  /// Run the given IO handler and return immediately.
  void RunIOHandlerAsync(const lldb::IOHandlerSP &reader_sp,
                         bool cancel_top_handler = true);

  /// Run the given IO handler and block until it's complete.
  void RunIOHandlerSync(const lldb::IOHandlerSP &reader_sp);

  ///  Remove the given IO handler if it's currently active.
  bool RemoveIOHandler(const lldb::IOHandlerSP &reader_sp);

  bool IsTopIOHandler(const lldb::IOHandlerSP &reader_sp);

  bool CheckTopIOHandlerTypes(IOHandler::Type top_type,
                              IOHandler::Type second_top_type);

  void PrintAsync(const char *s, size_t len, bool is_stdout);

  llvm::StringRef GetTopIOHandlerControlSequence(char ch);

  const char *GetIOHandlerCommandPrefix();

  const char *GetIOHandlerHelpPrologue();

  void ClearIOHandlers();

  bool EnableLog(llvm::StringRef channel,
                 llvm::ArrayRef<const char *> categories,
                 llvm::StringRef log_file, uint32_t log_options,
                 size_t buffer_size, LogHandlerKind log_handler_kind,
                 llvm::raw_ostream &error_stream);

  void SetLoggingCallback(lldb::LogOutputCallback log_callback, void *baton);

  // Properties Functions
  enum StopDisassemblyType {
    eStopDisassemblyTypeNever = 0,
    eStopDisassemblyTypeNoDebugInfo,
    eStopDisassemblyTypeNoSource,
    eStopDisassemblyTypeAlways
  };

  Status SetPropertyValue(const ExecutionContext *exe_ctx,
                          VarSetOperationType op, llvm::StringRef property_path,
                          llvm::StringRef value) override;

  bool GetAutoConfirm() const;

  const FormatEntity::Entry *GetDisassemblyFormat() const;

  const FormatEntity::Entry *GetFrameFormat() const;

  const FormatEntity::Entry *GetFrameFormatUnique() const;

  uint64_t GetStopDisassemblyMaxSize() const;

  const FormatEntity::Entry *GetThreadFormat() const;

  const FormatEntity::Entry *GetThreadStopFormat() const;

  lldb::ScriptLanguage GetScriptLanguage() const;

  bool SetScriptLanguage(lldb::ScriptLanguage script_lang);

  lldb::LanguageType GetREPLLanguage() const;

  bool SetREPLLanguage(lldb::LanguageType repl_lang);

  uint64_t GetTerminalWidth() const;

  bool SetTerminalWidth(uint64_t term_width);

  llvm::StringRef GetPrompt() const;

  llvm::StringRef GetPromptAnsiPrefix() const;

  llvm::StringRef GetPromptAnsiSuffix() const;

  void SetPrompt(llvm::StringRef p);
  void SetPrompt(const char *) = delete;

  bool GetUseExternalEditor() const;
  bool SetUseExternalEditor(bool use_external_editor_p);

  llvm::StringRef GetExternalEditor() const;

  bool SetExternalEditor(llvm::StringRef editor);

  bool GetUseColor() const;

  bool SetUseColor(bool use_color);

  bool GetShowProgress() const;

  bool SetShowProgress(bool show_progress);

  llvm::StringRef GetShowProgressAnsiPrefix() const;

  llvm::StringRef GetShowProgressAnsiSuffix() const;

  bool GetUseAutosuggestion() const;

  llvm::StringRef GetAutosuggestionAnsiPrefix() const;

  llvm::StringRef GetAutosuggestionAnsiSuffix() const;

  llvm::StringRef GetRegexMatchAnsiPrefix() const;

  llvm::StringRef GetRegexMatchAnsiSuffix() const;

  bool GetShowDontUsePoHint() const;

  bool GetUseSourceCache() const;

  bool SetUseSourceCache(bool use_source_cache);

  bool GetHighlightSource() const;

  lldb::StopShowColumn GetStopShowColumn() const;

  llvm::StringRef GetStopShowColumnAnsiPrefix() const;

  llvm::StringRef GetStopShowColumnAnsiSuffix() const;

  uint64_t GetStopSourceLineCount(bool before) const;

  StopDisassemblyType GetStopDisassemblyDisplay() const;

  uint64_t GetDisassemblyLineCount() const;

  llvm::StringRef GetStopShowLineMarkerAnsiPrefix() const;

  llvm::StringRef GetStopShowLineMarkerAnsiSuffix() const;

  bool GetAutoOneLineSummaries() const;

  bool GetAutoIndent() const;

  bool SetAutoIndent(bool b);

  bool GetPrintDecls() const;

  bool SetPrintDecls(bool b);

  uint64_t GetTabSize() const;

  bool SetTabSize(uint64_t tab_size);

  lldb::DWIMPrintVerbosity GetDWIMPrintVerbosity() const;

  bool GetEscapeNonPrintables() const;

  bool GetNotifyVoid() const;

  const std::string &GetInstanceName() { return m_instance_name; }

  bool LoadPlugin(const FileSpec &spec, Status &error);

  void RunIOHandlers();

  bool IsForwardingEvents();

  void EnableForwardEvents(const lldb::ListenerSP &listener_sp);

  void CancelForwardEvents(const lldb::ListenerSP &listener_sp);

  bool IsHandlingEvents() const { return m_event_handler_thread.IsJoinable(); }

  Status RunREPL(lldb::LanguageType language, const char *repl_options);

  /// Interruption in LLDB:
  ///
  /// This is a voluntary interruption mechanism, not preemptive.  Parts of lldb
  /// that do work that can be safely interrupted call
  /// Debugger::InterruptRequested and if that returns true, they should return
  /// at a safe point, shortcutting the rest of the work they were to do.
  ///
  /// lldb clients can both offer a CommandInterpreter (through
  /// RunCommandInterpreter) and use the SB API's for their own purposes, so it
  /// is convenient to separate "interrupting the CommandInterpreter execution"
  /// and interrupting the work it is doing with the SB API's.  So there are two
  /// ways to cause an interrupt:
  ///   * CommandInterpreter::InterruptCommand: Interrupts the command currently
  ///     running in the command interpreter IOHandler thread
  ///   * Debugger::RequestInterrupt: Interrupts are active on anything but the
  ///     CommandInterpreter thread till CancelInterruptRequest is called.
  ///
  /// Since the two checks are mutually exclusive, however, it's also convenient
  /// to have just one function to check the interrupt state.

  /// Bump the "interrupt requested" count on the debugger to support
  /// cooperative interruption.  If this is non-zero, InterruptRequested will
  /// return true.  Interruptible operations are expected to query the
  /// InterruptRequested API periodically, and interrupt what they were doing
  /// if it returns \b true.
  ///
  void RequestInterrupt();

  /// Decrement the "interrupt requested" counter.
  void CancelInterruptRequest();

  /// This is the correct way to query the state of Interruption.
  /// If you are on the RunCommandInterpreter thread, it will check the
  /// command interpreter state, and if it is on another thread it will
  /// check the debugger Interrupt Request state.
  /// \param[in] cur_func
  /// For reporting if the interruption was requested.  Don't provide this by
  /// hand, use INTERRUPT_REQUESTED so this gets done consistently.
  ///
  /// \param[in] formatv
  /// A formatv string for the interrupt message.  If the elements of the
  /// message are expensive to compute, you can use the no-argument form of
  /// InterruptRequested, then make up the report using REPORT_INTERRUPTION.
  ///
  /// \return
  ///  A boolean value, if \b true an interruptible operation should interrupt
  ///  itself.
  template <typename... Args>
  bool InterruptRequested(const char *cur_func, const char *formatv,
                          Args &&...args) {
    bool ret_val = InterruptRequested();
    if (ret_val) {
      if (!formatv)
        formatv = "Unknown message";
      if (!cur_func)
        cur_func = "<UNKNOWN>";
      ReportInterruption(InterruptionReport(
          cur_func, llvm::formatv(formatv, std::forward<Args>(args)...)));
    }
    return ret_val;
  }

  /// This handy define will keep you from having to generate a report for the
  /// interruption by hand.  Use this except in the case where the arguments to
  /// the message description are expensive to compute.
#define INTERRUPT_REQUESTED(debugger, ...)                                     \
  (debugger).InterruptRequested(__func__, __VA_ARGS__)

  // This form just queries for whether to interrupt, and does no reporting:
  bool InterruptRequested();

  // FIXME: Do we want to capture a backtrace at the interruption point?
  class InterruptionReport {
  public:
    InterruptionReport(std::string function_name, std::string description)
        : m_function_name(std::move(function_name)),
          m_description(std::move(description)),
          m_interrupt_time(std::chrono::system_clock::now()),
          m_thread_id(llvm::get_threadid()) {}

    InterruptionReport(std::string function_name,
                       const llvm::formatv_object_base &payload);

    template <typename... Args>
    InterruptionReport(std::string function_name, const char *format,
                       Args &&...args)
        : InterruptionReport(
              function_name,
              llvm::formatv(format, std::forward<Args>(args)...)) {}

    std::string m_function_name;
    std::string m_description;
    const std::chrono::time_point<std::chrono::system_clock> m_interrupt_time;
    const uint64_t m_thread_id;
  };
  void ReportInterruption(const InterruptionReport &report);
#define REPORT_INTERRUPTION(debugger, ...)                                     \
  (debugger).ReportInterruption(                                               \
      Debugger::InterruptionReport(__func__, __VA_ARGS__))

  static DebuggerList DebuggersRequestingInterruption();

public:
  // This is for use in the command interpreter, when you either want the
  // selected target, or if no target is present you want to prime the dummy
  // target with entities that will be copied over to new targets.
  Target &GetSelectedOrDummyTarget(bool prefer_dummy = false);
  Target &GetDummyTarget() { return *m_dummy_target_sp; }

  lldb::BroadcasterManagerSP GetBroadcasterManager() {
    return m_broadcaster_manager_sp;
  }

  /// Shared thread pool. Use only with ThreadPoolTaskGroup.
  static llvm::ThreadPoolInterface &GetThreadPool();

  /// Report warning events.
  ///
  /// Warning events will be delivered to any debuggers that have listeners
  /// for the eBroadcastBitWarning.
  ///
  /// \param[in] message
  ///   The warning message to be reported.
  ///
  /// \param [in] debugger_id
  ///   If this optional parameter has a value, it indicates the unique
  ///   debugger identifier that this diagnostic should be delivered to. If
  ///   this optional parameter does not have a value, the diagnostic event
  ///   will be delivered to all debuggers.
  ///
  /// \param [in] once
  ///   If a pointer is passed to a std::once_flag, then it will be used to
  ///   ensure the given warning is only broadcast once.
  static void
  ReportWarning(std::string message,
                std::optional<lldb::user_id_t> debugger_id = std::nullopt,
                std::once_flag *once = nullptr);

  /// Report error events.
  ///
  /// Error events will be delivered to any debuggers that have listeners
  /// for the eBroadcastBitError.
  ///
  /// \param[in] message
  ///   The error message to be reported.
  ///
  /// \param [in] debugger_id
  ///   If this optional parameter has a value, it indicates the unique
  ///   debugger identifier that this diagnostic should be delivered to. If
  ///   this optional parameter does not have a value, the diagnostic event
  ///   will be delivered to all debuggers.
  ///
  /// \param [in] once
  ///   If a pointer is passed to a std::once_flag, then it will be used to
  ///   ensure the given error is only broadcast once.
  static void
  ReportError(std::string message,
              std::optional<lldb::user_id_t> debugger_id = std::nullopt,
              std::once_flag *once = nullptr);

  /// Report info events.
  ///
  /// Unlike warning and error events, info events are not broadcast but are
  /// logged for diagnostic purposes.
  ///
  /// \param[in] message
  ///   The info message to be reported.
  ///
  /// \param [in] debugger_id
  ///   If this optional parameter has a value, it indicates this diagnostic is
  ///   associated with a unique debugger instance.
  ///
  /// \param [in] once
  ///   If a pointer is passed to a std::once_flag, then it will be used to
  ///   ensure the given info is only logged once.
  static void
  ReportInfo(std::string message,
             std::optional<lldb::user_id_t> debugger_id = std::nullopt,
             std::once_flag *once = nullptr);

  static void ReportSymbolChange(const ModuleSpec &module_spec);

  /// DEPRECATED: We used to only support one Destroy callback. Now that we
  /// support Add and Remove, you should only remove callbacks that you added.
  /// Use Add and Remove instead.
  ///
  /// Clear all previously added callbacks and only add the given one.
  void
  SetDestroyCallback(lldb_private::DebuggerDestroyCallback destroy_callback,
                     void *baton);

  /// Add a callback for when the debugger is destroyed. Return a token, which
  /// can be used to remove said callback. Multiple callbacks can be added by
  /// calling this function multiple times, and will be invoked in FIFO order.
  lldb::callback_token_t
  AddDestroyCallback(lldb_private::DebuggerDestroyCallback destroy_callback,
                     void *baton);

  /// Remove the specified callback. Return true if successful.
  bool RemoveDestroyCallback(lldb::callback_token_t token);

  /// Manually start the global event handler thread. It is useful to plugins
  /// that directly use the \a lldb_private namespace and want to use the
  /// debugger's default event handler thread instead of defining their own.
  bool StartEventHandlerThread();

  /// Manually stop the debugger's default event handler.
  void StopEventHandlerThread();

  /// Force flushing the process's pending stdout and stderr to the debugger's
  /// asynchronous stdout and stderr streams.
  void FlushProcessOutput(Process &process, bool flush_stdout,
                          bool flush_stderr);

  SourceManager::SourceFileCache &GetSourceFileCache() {
    return m_source_file_cache;
  }

protected:
  friend class CommandInterpreter;
  friend class REPL;
  friend class Progress;
  friend class ProgressManager;

  /// Report progress events.
  ///
  /// Progress events will be delivered to any debuggers that have listeners
  /// for the eBroadcastBitProgress. This function is called by the
  /// lldb_private::Progress class to deliver the events to any debuggers that
  /// qualify.
  ///
  /// \param [in] progress_id
  ///   The unique integer identifier for the progress to report.
  ///
  /// \param[in] message
  ///   The title of the progress dialog to display in the UI.
  ///
  /// \param [in] completed
  ///   The amount of work completed. If \a completed is zero, then this event
  ///   is a progress started event. If \a completed is equal to \a total, then
  ///   this event is a progress end event. Otherwise completed indicates the
  ///   current progress compare to the total value.
  ///
  /// \param [in] total
  ///   The total amount of work units that need to be completed. If this value
  ///   is UINT64_MAX, then an indeterminate progress indicator should be
  ///   displayed.
  ///
  /// \param [in] debugger_id
  ///   If this optional parameter has a value, it indicates the unique
  ///   debugger identifier that this progress should be delivered to. If this
  ///   optional parameter does not have a value, the progress will be
  ///   delivered to all debuggers.
  static void
  ReportProgress(uint64_t progress_id, std::string title, std::string details,
                 uint64_t completed, uint64_t total,
                 std::optional<lldb::user_id_t> debugger_id,
                 uint32_t progress_category_bit = lldb::eBroadcastBitProgress);

  static void ReportDiagnosticImpl(lldb::Severity severity, std::string message,
                                   std::optional<lldb::user_id_t> debugger_id,
                                   std::once_flag *once);

  void HandleDestroyCallback();

  void PrintProgress(const ProgressEventData &data);

  void PushIOHandler(const lldb::IOHandlerSP &reader_sp,
                     bool cancel_top_handler = true);

  bool PopIOHandler(const lldb::IOHandlerSP &reader_sp);

  bool HasIOHandlerThread() const;

  bool StartIOHandlerThread();

  void StopIOHandlerThread();

  // Sets the IOHandler thread to the new_thread, and returns
  // the previous IOHandler thread.
  HostThread SetIOHandlerThread(HostThread &new_thread);

  void JoinIOHandlerThread();

  bool IsIOHandlerThreadCurrentThread() const;

  lldb::thread_result_t IOHandlerThread();

  lldb::thread_result_t DefaultEventHandler();

  void HandleBreakpointEvent(const lldb::EventSP &event_sp);

  void HandleProcessEvent(const lldb::EventSP &event_sp);

  void HandleThreadEvent(const lldb::EventSP &event_sp);

  void HandleProgressEvent(const lldb::EventSP &event_sp);

  void HandleDiagnosticEvent(const lldb::EventSP &event_sp);

  // Ensures two threads don't attempt to flush process output in parallel.
  std::mutex m_output_flush_mutex;

  void InstanceInitialize();

  // these should never be NULL
  lldb::FileSP m_input_file_sp;
  lldb::StreamFileSP m_output_stream_sp;
  lldb::StreamFileSP m_error_stream_sp;

  /// Used for shadowing the input file when capturing a reproducer.
  repro::DataRecorder *m_input_recorder;

  lldb::BroadcasterManagerSP m_broadcaster_manager_sp; // The debugger acts as a
                                                       // broadcaster manager of
                                                       // last resort.
  // It needs to get constructed before the target_list or any other member
  // that might want to broadcast through the debugger.

  TerminalState m_terminal_state;
  TargetList m_target_list;

  PlatformList m_platform_list;
  lldb::ListenerSP m_listener_sp;
  std::unique_ptr<SourceManager> m_source_manager_up; // This is a scratch
                                                      // source manager that we
                                                      // return if we have no
                                                      // targets.
  SourceManager::SourceFileCache m_source_file_cache; // All the source managers
                                                      // for targets created in
                                                      // this debugger used this
                                                      // shared
                                                      // source file cache.
  std::unique_ptr<CommandInterpreter> m_command_interpreter_up;

  std::recursive_mutex m_script_interpreter_mutex;
  std::array<lldb::ScriptInterpreterSP, lldb::eScriptLanguageUnknown>
      m_script_interpreters;

  IOHandlerStack m_io_handler_stack;
  std::recursive_mutex m_io_handler_synchronous_mutex;

  std::optional<uint64_t> m_current_event_id;

  llvm::StringMap<std::weak_ptr<LogHandler>> m_stream_handlers;
  std::shared_ptr<CallbackLogHandler> m_callback_handler_sp;
  const std::string m_instance_name;
  static LoadPluginCallbackType g_load_plugin_callback;
  typedef std::vector<llvm::sys::DynamicLibrary> LoadedPluginsList;
  LoadedPluginsList m_loaded_plugins;
  HostThread m_event_handler_thread;
  HostThread m_io_handler_thread;
  Broadcaster m_sync_broadcaster; ///< Private debugger synchronization.
  Broadcaster m_broadcaster;      ///< Public Debugger event broadcaster.
  lldb::ListenerSP m_forward_listener_sp;
  llvm::once_flag m_clear_once;
  lldb::TargetSP m_dummy_target_sp;
  Diagnostics::CallbackID m_diagnostics_callback_id;

  std::mutex m_destroy_callback_mutex;
  lldb::callback_token_t m_destroy_callback_next_token = 0;
  struct DestroyCallbackInfo {
    DestroyCallbackInfo() {}
    DestroyCallbackInfo(lldb::callback_token_t token,
                        lldb_private::DebuggerDestroyCallback callback,
                        void *baton)
        : token(token), callback(callback), baton(baton) {}
    lldb::callback_token_t token;
    lldb_private::DebuggerDestroyCallback callback;
    void *baton;
  };
  llvm::SmallVector<DestroyCallbackInfo, 2> m_destroy_callbacks;

  uint32_t m_interrupt_requested = 0; ///< Tracks interrupt requests
  std::mutex m_interrupt_mutex;

  // Events for m_sync_broadcaster
  enum {
    eBroadcastBitEventThreadIsListening = (1 << 0),
  };

private:
  // Use Debugger::CreateInstance() to get a shared pointer to a new debugger
  // object
  Debugger(lldb::LogOutputCallback m_log_callback, void *baton);

  Debugger(const Debugger &) = delete;
  const Debugger &operator=(const Debugger &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_DEBUGGER_H
