//===-- ScriptInterpreterPythonImpl.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SCRIPTINTERPRETERPYTHONIMPL_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SCRIPTINTERPRETERPYTHONIMPL_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "lldb-python.h"

#include "PythonDataObjects.h"
#include "ScriptInterpreterPython.h"

#include "lldb/Host/Terminal.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_private {
class IOHandlerPythonInterpreter;
class ScriptInterpreterPythonImpl : public ScriptInterpreterPython {
public:
  friend class IOHandlerPythonInterpreter;

  ScriptInterpreterPythonImpl(Debugger &debugger);

  ~ScriptInterpreterPythonImpl() override;

  bool Interrupt() override;

  bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  void ExecuteInterpreterLoop() override;

  bool ExecuteOneLineWithReturn(
      llvm::StringRef in_string,
      ScriptInterpreter::ScriptReturnType return_type, void *ret_value,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  lldb_private::Status ExecuteMultipleLines(
      const char *in_string,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  Status
  ExportFunctionDefinitionToInterpreter(StringList &function_def) override;

  bool GenerateTypeScriptFunction(StringList &input, std::string &output,
                                  const void *name_token = nullptr) override;

  bool GenerateTypeSynthClass(StringList &input, std::string &output,
                              const void *name_token = nullptr) override;

  bool GenerateTypeSynthClass(const char *oneliner, std::string &output,
                              const void *name_token = nullptr) override;

  // use this if the function code is just a one-liner script
  bool GenerateTypeScriptFunction(const char *oneliner, std::string &output,
                                  const void *name_token = nullptr) override;

  bool GenerateScriptAliasFunction(StringList &input,
                                   std::string &output) override;

  StructuredData::ObjectSP
  CreateSyntheticScriptedProvider(const char *class_name,
                                  lldb::ValueObjectSP valobj) override;

  StructuredData::GenericSP
  CreateScriptCommandObject(const char *class_name) override;

  StructuredData::ObjectSP
  CreateStructuredDataFromScriptObject(ScriptObject obj) override;

  StructuredData::GenericSP
  CreateScriptedBreakpointResolver(const char *class_name,
                                   const StructuredDataImpl &args_data,
                                   lldb::BreakpointSP &bkpt_sp) override;
  bool ScriptedBreakpointResolverSearchCallback(
      StructuredData::GenericSP implementor_sp,
      SymbolContext *sym_ctx) override;

  lldb::SearchDepth ScriptedBreakpointResolverSearchDepth(
      StructuredData::GenericSP implementor_sp) override;

  StructuredData::GenericSP
  CreateScriptedStopHook(lldb::TargetSP target_sp, const char *class_name,
                         const StructuredDataImpl &args_data,
                         Status &error) override;

  bool ScriptedStopHookHandleStop(StructuredData::GenericSP implementor_sp,
                                  ExecutionContext &exc_ctx,
                                  lldb::StreamSP stream_sp) override;

  StructuredData::GenericSP
  CreateFrameRecognizer(const char *class_name) override;

  lldb::ValueObjectListSP
  GetRecognizedArguments(const StructuredData::ObjectSP &implementor,
                         lldb::StackFrameSP frame_sp) override;

  lldb::ScriptedProcessInterfaceUP CreateScriptedProcessInterface() override;

  lldb::ScriptedThreadInterfaceSP CreateScriptedThreadInterface() override;

  lldb::ScriptedThreadPlanInterfaceSP
  CreateScriptedThreadPlanInterface() override;

  lldb::OperatingSystemInterfaceSP CreateOperatingSystemInterface() override;

  StructuredData::ObjectSP
  LoadPluginModule(const FileSpec &file_spec,
                   lldb_private::Status &error) override;

  StructuredData::DictionarySP
  GetDynamicSettings(StructuredData::ObjectSP plugin_module_sp, Target *target,
                     const char *setting_name,
                     lldb_private::Status &error) override;

  size_t CalculateNumChildren(const StructuredData::ObjectSP &implementor,
                              uint32_t max) override;

  lldb::ValueObjectSP
  GetChildAtIndex(const StructuredData::ObjectSP &implementor,
                  uint32_t idx) override;

  int GetIndexOfChildWithName(const StructuredData::ObjectSP &implementor,
                              const char *child_name) override;

  bool UpdateSynthProviderInstance(
      const StructuredData::ObjectSP &implementor) override;

  bool MightHaveChildrenSynthProviderInstance(
      const StructuredData::ObjectSP &implementor) override;

  lldb::ValueObjectSP
  GetSyntheticValue(const StructuredData::ObjectSP &implementor) override;

  ConstString
  GetSyntheticTypeName(const StructuredData::ObjectSP &implementor) override;

  bool
  RunScriptBasedCommand(const char *impl_function, llvm::StringRef args,
                        ScriptedCommandSynchronicity synchronicity,
                        lldb_private::CommandReturnObject &cmd_retobj,
                        Status &error,
                        const lldb_private::ExecutionContext &exe_ctx) override;

  bool RunScriptBasedCommand(
      StructuredData::GenericSP impl_obj_sp, llvm::StringRef args,
      ScriptedCommandSynchronicity synchronicity,
      lldb_private::CommandReturnObject &cmd_retobj, Status &error,
      const lldb_private::ExecutionContext &exe_ctx) override;

  bool RunScriptBasedParsedCommand(
      StructuredData::GenericSP impl_obj_sp, Args &args,
      ScriptedCommandSynchronicity synchronicity,
      lldb_private::CommandReturnObject &cmd_retobj, Status &error,
      const lldb_private::ExecutionContext &exe_ctx) override;

  std::optional<std::string>
  GetRepeatCommandForScriptedCommand(StructuredData::GenericSP impl_obj_sp,
                                     Args &args) override;

  Status GenerateFunction(const char *signature, const StringList &input,
                          bool is_callback) override;

  Status GenerateBreakpointCommandCallbackData(StringList &input,
                                               std::string &output,
                                               bool has_extra_args,
                                               bool is_callback) override;

  bool GenerateWatchpointCommandCallbackData(StringList &input,
                                             std::string &output,
                                             bool is_callback) override;

  bool GetScriptedSummary(const char *function_name, lldb::ValueObjectSP valobj,
                          StructuredData::ObjectSP &callee_wrapper_sp,
                          const TypeSummaryOptions &options,
                          std::string &retval) override;

  bool FormatterCallbackFunction(const char *function_name,
                                 lldb::TypeImplSP type_impl_sp) override;

  bool GetDocumentationForItem(const char *item, std::string &dest) override;

  bool GetShortHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                                    std::string &dest) override;

  uint32_t
  GetFlagsForCommandObject(StructuredData::GenericSP cmd_obj_sp) override;

  bool GetLongHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                                   std::string &dest) override;
                                   
  StructuredData::ObjectSP
  GetOptionsForCommandObject(StructuredData::GenericSP cmd_obj_sp) override;

  StructuredData::ObjectSP
  GetArgumentsForCommandObject(StructuredData::GenericSP cmd_obj_sp) override;

  bool SetOptionValueForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                                      ExecutionContext *exe_ctx,
                                      llvm::StringRef long_option, 
                                      llvm::StringRef value) override;

  void OptionParsingStartedForCommandObject(
      StructuredData::GenericSP cmd_obj_sp) override;

  bool CheckObjectExists(const char *name) override {
    if (!name || !name[0])
      return false;
    std::string temp;
    return GetDocumentationForItem(name, temp);
  }

  bool RunScriptFormatKeyword(const char *impl_function, Process *process,
                              std::string &output, Status &error) override;

  bool RunScriptFormatKeyword(const char *impl_function, Thread *thread,
                              std::string &output, Status &error) override;

  bool RunScriptFormatKeyword(const char *impl_function, Target *target,
                              std::string &output, Status &error) override;

  bool RunScriptFormatKeyword(const char *impl_function, StackFrame *frame,
                              std::string &output, Status &error) override;

  bool RunScriptFormatKeyword(const char *impl_function, ValueObject *value,
                              std::string &output, Status &error) override;

  bool LoadScriptingModule(const char *filename,
                           const LoadScriptOptions &options,
                           lldb_private::Status &error,
                           StructuredData::ObjectSP *module_sp = nullptr,
                           FileSpec extra_search_dir = {}) override;

  bool IsReservedWord(const char *word) override;

  std::unique_ptr<ScriptInterpreterLocker> AcquireInterpreterLock() override;

  void CollectDataForBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      CommandReturnObject &result) override;

  void
  CollectDataForWatchpointCommandCallback(WatchpointOptions *wp_options,
                                          CommandReturnObject &result) override;

  /// Set the callback body text into the callback for the breakpoint.
  Status SetBreakpointCommandCallback(BreakpointOptions &bp_options,
                                      const char *callback_body,
                                      bool is_callback) override;

  Status SetBreakpointCommandCallbackFunction(
      BreakpointOptions &bp_options, const char *function_name,
      StructuredData::ObjectSP extra_args_sp) override;

  /// This one is for deserialization:
  Status SetBreakpointCommandCallback(
      BreakpointOptions &bp_options,
      std::unique_ptr<BreakpointOptions::CommandData> &data_up) override;

  Status SetBreakpointCommandCallback(BreakpointOptions &bp_options,
                                      const char *command_body_text,
                                      StructuredData::ObjectSP extra_args_sp,
                                      bool uses_extra_args,
                                      bool is_callback);

  /// Set a one-liner as the callback for the watchpoint.
  void SetWatchpointCommandCallback(WatchpointOptions *wp_options,
                                    const char *user_input,
                                    bool is_callback) override;

  const char *GetDictionaryName() { return m_dictionary_name.c_str(); }

  PyThreadState *GetThreadState() { return m_command_thread_state; }

  void SetThreadState(PyThreadState *s) {
    if (s)
      m_command_thread_state = s;
  }

  // IOHandlerDelegate
  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override;

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override;

  static lldb::ScriptInterpreterSP CreateInstance(Debugger &debugger);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  class Locker : public ScriptInterpreterLocker {
  public:
    enum OnEntry {
      AcquireLock = 0x0001,
      InitSession = 0x0002,
      InitGlobals = 0x0004,
      NoSTDIN = 0x0008
    };

    enum OnLeave {
      FreeLock = 0x0001,
      FreeAcquiredLock = 0x0002, // do not free the lock if we already held it
                                 // when calling constructor
      TearDownSession = 0x0004
    };

    Locker(ScriptInterpreterPythonImpl *py_interpreter,
           uint16_t on_entry = AcquireLock | InitSession,
           uint16_t on_leave = FreeLock | TearDownSession,
           lldb::FileSP in = nullptr, lldb::FileSP out = nullptr,
           lldb::FileSP err = nullptr);

    ~Locker() override;

  private:
    bool DoAcquireLock();

    bool DoInitSession(uint16_t on_entry_flags, lldb::FileSP in,
                       lldb::FileSP out, lldb::FileSP err);

    bool DoFreeLock();

    bool DoTearDownSession();

    bool m_teardown_session;
    ScriptInterpreterPythonImpl *m_python_interpreter;
    PyGILState_STATE m_GILState;
  };

  static bool BreakpointCallbackFunction(void *baton,
                                         StoppointCallbackContext *context,
                                         lldb::user_id_t break_id,
                                         lldb::user_id_t break_loc_id);
  static bool WatchpointCallbackFunction(void *baton,
                                         StoppointCallbackContext *context,
                                         lldb::user_id_t watch_id);
  static void Initialize();

  class SynchronicityHandler {
  private:
    lldb::DebuggerSP m_debugger_sp;
    ScriptedCommandSynchronicity m_synch_wanted;
    bool m_old_asynch;

  public:
    SynchronicityHandler(lldb::DebuggerSP, ScriptedCommandSynchronicity);

    ~SynchronicityHandler();
  };

  enum class AddLocation { Beginning, End };

  static void AddToSysPath(AddLocation location, std::string path);

  bool EnterSession(uint16_t on_entry_flags, lldb::FileSP in, lldb::FileSP out,
                    lldb::FileSP err);

  void LeaveSession();

  uint32_t IsExecutingPython() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_lock_count > 0;
  }

  uint32_t IncrementLockCount() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return ++m_lock_count;
  }

  uint32_t DecrementLockCount() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_lock_count > 0)
      --m_lock_count;
    return m_lock_count;
  }

  enum ActiveIOHandler {
    eIOHandlerNone,
    eIOHandlerBreakpoint,
    eIOHandlerWatchpoint
  };

  python::PythonModule &GetMainModule();

  python::PythonDictionary &GetSessionDictionary();

  python::PythonDictionary &GetSysModuleDictionary();

  llvm::Expected<unsigned> GetMaxPositionalArgumentsForCallable(
      const llvm::StringRef &callable_name) override;

  bool GetEmbeddedInterpreterModuleObjects();

  bool SetStdHandle(lldb::FileSP file, const char *py_name,
                    python::PythonObject &save_file, const char *mode);

  python::PythonObject m_saved_stdin;
  python::PythonObject m_saved_stdout;
  python::PythonObject m_saved_stderr;
  python::PythonModule m_main_module;
  python::PythonDictionary m_session_dict;
  python::PythonDictionary m_sys_module_dict;
  python::PythonObject m_run_one_line_function;
  python::PythonObject m_run_one_line_str_global;
  std::string m_dictionary_name;
  ActiveIOHandler m_active_io_handler;
  bool m_session_is_active;
  bool m_pty_secondary_is_open;
  bool m_valid_session;
  uint32_t m_lock_count;
  std::mutex m_mutex;
  PyThreadState *m_command_thread_state;
};

class IOHandlerPythonInterpreter : public IOHandler {
public:
  IOHandlerPythonInterpreter(Debugger &debugger,
                             ScriptInterpreterPythonImpl *python)
      : IOHandler(debugger, IOHandler::Type::PythonInterpreter),
        m_python(python) {}

  ~IOHandlerPythonInterpreter() override = default;

  llvm::StringRef GetControlSequence(char ch) override {
    static constexpr llvm::StringLiteral control_sequence("quit()\n");
    if (ch == 'd')
      return control_sequence;
    return {};
  }

  void Run() override {
    if (m_python) {
      int stdin_fd = GetInputFD();
      if (stdin_fd >= 0) {
        Terminal terminal(stdin_fd);
        TerminalState terminal_state(terminal);

        if (terminal.IsATerminal()) {
          // FIXME: error handling?
          llvm::consumeError(terminal.SetCanonical(false));
          llvm::consumeError(terminal.SetEcho(true));
        }

        ScriptInterpreterPythonImpl::Locker locker(
            m_python,
            ScriptInterpreterPythonImpl::Locker::AcquireLock |
                ScriptInterpreterPythonImpl::Locker::InitSession |
                ScriptInterpreterPythonImpl::Locker::InitGlobals,
            ScriptInterpreterPythonImpl::Locker::FreeAcquiredLock |
                ScriptInterpreterPythonImpl::Locker::TearDownSession);

        // The following call drops into the embedded interpreter loop and
        // stays there until the user chooses to exit from the Python
        // interpreter. This embedded interpreter will, as any Python code that
        // performs I/O, unlock the GIL before a system call that can hang, and
        // lock it when the syscall has returned.

        // We need to surround the call to the embedded interpreter with calls
        // to PyGILState_Ensure and PyGILState_Release (using the Locker
        // above). This is because Python has a global lock which must be held
        // whenever we want to touch any Python objects. Otherwise, if the user
        // calls Python code, the interpreter state will be off, and things
        // could hang (it's happened before).

        StreamString run_string;
        run_string.Printf("run_python_interpreter (%s)",
                          m_python->GetDictionaryName());
        PyRun_SimpleString(run_string.GetData());
      }
    }
    SetIsDone(true);
  }

  void Cancel() override {}

  bool Interrupt() override { return m_python->Interrupt(); }

  void GotEOF() override {}

protected:
  ScriptInterpreterPythonImpl *m_python;
};

} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_SCRIPTINTERPRETERPYTHONIMPL_H
