//===-- ScriptInterpreter.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_SCRIPTINTERPRETER_H
#define LLDB_INTERPRETER_SCRIPTINTERPRETER_H

#include "lldb/API/SBAttachInfo.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBData.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBStream.h"
#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/ThreadedCommunication.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Interpreter/Interfaces/OperatingSystemInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedPlatformInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedProcessInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedThreadInterface.h"
#include "lldb/Interpreter/ScriptObject.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"
#include <optional>

namespace lldb_private {

class ScriptInterpreterLocker {
public:
  ScriptInterpreterLocker() = default;

  virtual ~ScriptInterpreterLocker() = default;

private:
  ScriptInterpreterLocker(const ScriptInterpreterLocker &) = delete;
  const ScriptInterpreterLocker &
  operator=(const ScriptInterpreterLocker &) = delete;
};

class ExecuteScriptOptions {
public:
  ExecuteScriptOptions() = default;

  bool GetEnableIO() const { return m_enable_io; }

  bool GetSetLLDBGlobals() const { return m_set_lldb_globals; }

  // If this is true then any exceptions raised by the script will be
  // cleared with PyErr_Clear().   If false then they will be left for
  // the caller to clean up
  bool GetMaskoutErrors() const { return m_maskout_errors; }

  ExecuteScriptOptions &SetEnableIO(bool enable) {
    m_enable_io = enable;
    return *this;
  }

  ExecuteScriptOptions &SetSetLLDBGlobals(bool set) {
    m_set_lldb_globals = set;
    return *this;
  }

  ExecuteScriptOptions &SetMaskoutErrors(bool maskout) {
    m_maskout_errors = maskout;
    return *this;
  }

private:
  bool m_enable_io = true;
  bool m_set_lldb_globals = true;
  bool m_maskout_errors = true;
};

class LoadScriptOptions {
public:
  LoadScriptOptions() = default;

  bool GetInitSession() const { return m_init_session; }
  bool GetSilent() const { return m_silent; }

  LoadScriptOptions &SetInitSession(bool b) {
    m_init_session = b;
    return *this;
  }

  LoadScriptOptions &SetSilent(bool b) {
    m_silent = b;
    return *this;
  }

private:
  bool m_init_session = false;
  bool m_silent = false;
};

class ScriptInterpreterIORedirect {
public:
  /// Create an IO redirect. If IO is enabled, this will redirects the output
  /// to the command return object if set or to the debugger otherwise. If IO
  /// is disabled, it will redirect all IO to /dev/null.
  static llvm::Expected<std::unique_ptr<ScriptInterpreterIORedirect>>
  Create(bool enable_io, Debugger &debugger, CommandReturnObject *result);

  ~ScriptInterpreterIORedirect();

  lldb::FileSP GetInputFile() const { return m_input_file_sp; }
  lldb::FileSP GetOutputFile() const { return m_output_file_sp->GetFileSP(); }
  lldb::FileSP GetErrorFile() const { return m_error_file_sp->GetFileSP(); }

  /// Flush our output and error file handles.
  void Flush();

private:
  ScriptInterpreterIORedirect(std::unique_ptr<File> input,
                              std::unique_ptr<File> output);
  ScriptInterpreterIORedirect(Debugger &debugger, CommandReturnObject *result);

  lldb::FileSP m_input_file_sp;
  lldb::StreamFileSP m_output_file_sp;
  lldb::StreamFileSP m_error_file_sp;
  ThreadedCommunication m_communication;
  bool m_disconnect;
};

class ScriptInterpreter : public PluginInterface {
public:
  enum ScriptReturnType {
    eScriptReturnTypeCharPtr,
    eScriptReturnTypeBool,
    eScriptReturnTypeShortInt,
    eScriptReturnTypeShortIntUnsigned,
    eScriptReturnTypeInt,
    eScriptReturnTypeIntUnsigned,
    eScriptReturnTypeLongInt,
    eScriptReturnTypeLongIntUnsigned,
    eScriptReturnTypeLongLong,
    eScriptReturnTypeLongLongUnsigned,
    eScriptReturnTypeFloat,
    eScriptReturnTypeDouble,
    eScriptReturnTypeChar,
    eScriptReturnTypeCharStrOrNone,
    eScriptReturnTypeOpaqueObject
  };

  ScriptInterpreter(Debugger &debugger, lldb::ScriptLanguage script_lang);

  virtual StructuredData::DictionarySP GetInterpreterInfo();

  ~ScriptInterpreter() override = default;

  virtual bool Interrupt() { return false; }

  virtual bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) = 0;

  virtual void ExecuteInterpreterLoop() = 0;

  virtual bool ExecuteOneLineWithReturn(
      llvm::StringRef in_string, ScriptReturnType return_type, void *ret_value,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) {
    return true;
  }

  virtual Status ExecuteMultipleLines(
      const char *in_string,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual Status
  ExportFunctionDefinitionToInterpreter(StringList &function_def) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual Status GenerateBreakpointCommandCallbackData(StringList &input,
                                                       std::string &output,
                                                       bool has_extra_args,
                                                       bool is_callback) {
    Status error;
    error.SetErrorString("not implemented");
    return error;
  }

  virtual bool GenerateWatchpointCommandCallbackData(StringList &input,
                                                     std::string &output,
                                                     bool is_callback) {
    return false;
  }

  virtual bool GenerateTypeScriptFunction(const char *oneliner,
                                          std::string &output,
                                          const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateTypeScriptFunction(StringList &input,
                                          std::string &output,
                                          const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateScriptAliasFunction(StringList &input,
                                           std::string &output) {
    return false;
  }

  virtual bool GenerateTypeSynthClass(StringList &input, std::string &output,
                                      const void *name_token = nullptr) {
    return false;
  }

  virtual bool GenerateTypeSynthClass(const char *oneliner, std::string &output,
                                      const void *name_token = nullptr) {
    return false;
  }

  virtual StructuredData::ObjectSP
  CreateSyntheticScriptedProvider(const char *class_name,
                                  lldb::ValueObjectSP valobj) {
    return StructuredData::ObjectSP();
  }

  virtual StructuredData::GenericSP
  CreateScriptCommandObject(const char *class_name) {
    return StructuredData::GenericSP();
  }

  virtual StructuredData::GenericSP
  CreateFrameRecognizer(const char *class_name) {
    return StructuredData::GenericSP();
  }

  virtual lldb::ValueObjectListSP GetRecognizedArguments(
      const StructuredData::ObjectSP &implementor,
      lldb::StackFrameSP frame_sp) {
    return lldb::ValueObjectListSP();
  }

  virtual StructuredData::GenericSP
  CreateScriptedBreakpointResolver(const char *class_name,
                                   const StructuredDataImpl &args_data,
                                   lldb::BreakpointSP &bkpt_sp) {
    return StructuredData::GenericSP();
  }

  virtual bool
  ScriptedBreakpointResolverSearchCallback(StructuredData::GenericSP implementor_sp,
                                           SymbolContext *sym_ctx)
  {
    return false;
  }

  virtual lldb::SearchDepth
  ScriptedBreakpointResolverSearchDepth(StructuredData::GenericSP implementor_sp)
  {
    return lldb::eSearchDepthModule;
  }

  virtual StructuredData::GenericSP
  CreateScriptedStopHook(lldb::TargetSP target_sp, const char *class_name,
                         const StructuredDataImpl &args_data, Status &error) {
    error.SetErrorString("Creating scripted stop-hooks with the current "
                         "script interpreter is not supported.");
    return StructuredData::GenericSP();
  }

  // This dispatches to the handle_stop method of the stop-hook class.  It
  // returns a "should_stop" bool.
  virtual bool
  ScriptedStopHookHandleStop(StructuredData::GenericSP implementor_sp,
                             ExecutionContext &exc_ctx,
                             lldb::StreamSP stream_sp) {
    return true;
  }

  virtual StructuredData::ObjectSP
  LoadPluginModule(const FileSpec &file_spec, lldb_private::Status &error) {
    return StructuredData::ObjectSP();
  }

  virtual StructuredData::DictionarySP
  GetDynamicSettings(StructuredData::ObjectSP plugin_module_sp, Target *target,
                     const char *setting_name, lldb_private::Status &error) {
    return StructuredData::DictionarySP();
  }

  virtual Status GenerateFunction(const char *signature,
                                  const StringList &input,
                                  bool is_callback) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  virtual void CollectDataForBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &options,
      CommandReturnObject &result);

  virtual void
  CollectDataForWatchpointCommandCallback(WatchpointOptions *wp_options,
                                          CommandReturnObject &result);

  /// Set the specified text as the callback for the breakpoint.
  Status SetBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      const char *callback_text);

  virtual Status SetBreakpointCommandCallback(BreakpointOptions &bp_options,
                                              const char *callback_text,
                                              bool is_callback) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  /// This one is for deserialization:
  virtual Status SetBreakpointCommandCallback(
      BreakpointOptions &bp_options,
      std::unique_ptr<BreakpointOptions::CommandData> &data_up) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  Status SetBreakpointCommandCallbackFunction(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      const char *function_name, StructuredData::ObjectSP extra_args_sp);

  /// Set a script function as the callback for the breakpoint.
  virtual Status
  SetBreakpointCommandCallbackFunction(BreakpointOptions &bp_options,
                                       const char *function_name,
                                       StructuredData::ObjectSP extra_args_sp) {
    Status error;
    error.SetErrorString("unimplemented");
    return error;
  }

  /// Set a one-liner as the callback for the watchpoint.
  virtual void SetWatchpointCommandCallback(WatchpointOptions *wp_options,
                                            const char *user_input,
                                            bool is_callback) {}

  virtual bool GetScriptedSummary(const char *function_name,
                                  lldb::ValueObjectSP valobj,
                                  StructuredData::ObjectSP &callee_wrapper_sp,
                                  const TypeSummaryOptions &options,
                                  std::string &retval) {
    return false;
  }

  // Calls the specified formatter matching Python function and returns its
  // result (true if it's a match, false if we should keep looking for a
  // matching formatter).
  virtual bool FormatterCallbackFunction(const char *function_name,
                                         lldb::TypeImplSP type_impl_sp) {
    return true;
  }

  virtual void Clear() {
    // Clean up any ref counts to SBObjects that might be in global variables
  }

  virtual size_t
  CalculateNumChildren(const StructuredData::ObjectSP &implementor,
                       uint32_t max) {
    return 0;
  }

  virtual lldb::ValueObjectSP
  GetChildAtIndex(const StructuredData::ObjectSP &implementor, uint32_t idx) {
    return lldb::ValueObjectSP();
  }

  virtual int
  GetIndexOfChildWithName(const StructuredData::ObjectSP &implementor,
                          const char *child_name) {
    return UINT32_MAX;
  }

  virtual bool
  UpdateSynthProviderInstance(const StructuredData::ObjectSP &implementor) {
    return false;
  }

  virtual bool MightHaveChildrenSynthProviderInstance(
      const StructuredData::ObjectSP &implementor) {
    return true;
  }

  virtual lldb::ValueObjectSP
  GetSyntheticValue(const StructuredData::ObjectSP &implementor) {
    return nullptr;
  }

  virtual ConstString
  GetSyntheticTypeName(const StructuredData::ObjectSP &implementor) {
    return ConstString();
  }

  virtual bool
  RunScriptBasedCommand(const char *impl_function, llvm::StringRef args,
                        ScriptedCommandSynchronicity synchronicity,
                        lldb_private::CommandReturnObject &cmd_retobj,
                        Status &error,
                        const lldb_private::ExecutionContext &exe_ctx) {
    return false;
  }

  virtual bool RunScriptBasedCommand(
      StructuredData::GenericSP impl_obj_sp, llvm::StringRef args,
      ScriptedCommandSynchronicity synchronicity,
      lldb_private::CommandReturnObject &cmd_retobj, Status &error,
      const lldb_private::ExecutionContext &exe_ctx) {
    return false;
  }

  virtual bool RunScriptBasedParsedCommand(
      StructuredData::GenericSP impl_obj_sp, Args& args,
      ScriptedCommandSynchronicity synchronicity,
      lldb_private::CommandReturnObject &cmd_retobj, Status &error,
      const lldb_private::ExecutionContext &exe_ctx) {
    return false;
  }

  virtual std::optional<std::string>
  GetRepeatCommandForScriptedCommand(StructuredData::GenericSP impl_obj_sp,
                                     Args &args) {
    return std::nullopt;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      Process *process, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function, Thread *thread,
                                      std::string &output, Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function, Target *target,
                                      std::string &output, Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      StackFrame *frame, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool RunScriptFormatKeyword(const char *impl_function,
                                      ValueObject *value, std::string &output,
                                      Status &error) {
    error.SetErrorString("unimplemented");
    return false;
  }

  virtual bool GetDocumentationForItem(const char *item, std::string &dest) {
    dest.clear();
    return false;
  }

  virtual bool
  GetShortHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                               std::string &dest) {
    dest.clear();
    return false;
  }
  
  virtual StructuredData::ObjectSP
  GetOptionsForCommandObject(StructuredData::GenericSP cmd_obj_sp) {
    return {};
  }

  virtual StructuredData::ObjectSP
  GetArgumentsForCommandObject(StructuredData::GenericSP cmd_obj_sp) {
    return {};
  }
  
  virtual bool SetOptionValueForCommandObject(
      StructuredData::GenericSP cmd_obj_sp, ExecutionContext *exe_ctx, 
      llvm::StringRef long_option, llvm::StringRef value) {
    return false;
  }

  virtual void OptionParsingStartedForCommandObject(
      StructuredData::GenericSP cmd_obj_sp) {
    return;
  }

  virtual uint32_t
  GetFlagsForCommandObject(StructuredData::GenericSP cmd_obj_sp) {
    return 0;
  }

  virtual bool GetLongHelpForCommandObject(StructuredData::GenericSP cmd_obj_sp,
                                           std::string &dest) {
    dest.clear();
    return false;
  }

  virtual bool CheckObjectExists(const char *name) { return false; }

  virtual bool
  LoadScriptingModule(const char *filename, const LoadScriptOptions &options,
                      lldb_private::Status &error,
                      StructuredData::ObjectSP *module_sp = nullptr,
                      FileSpec extra_search_dir = {});

  virtual bool IsReservedWord(const char *word) { return false; }

  virtual std::unique_ptr<ScriptInterpreterLocker> AcquireInterpreterLock();

  const char *GetScriptInterpreterPtyName();

  virtual llvm::Expected<unsigned>
  GetMaxPositionalArgumentsForCallable(const llvm::StringRef &callable_name) {
    return llvm::createStringError(
    llvm::inconvertibleErrorCode(), "Unimplemented function");
  }

  static std::string LanguageToString(lldb::ScriptLanguage language);

  static lldb::ScriptLanguage StringToLanguage(const llvm::StringRef &string);

  lldb::ScriptLanguage GetLanguage() { return m_script_lang; }

  virtual lldb::ScriptedProcessInterfaceUP CreateScriptedProcessInterface() {
    return {};
  }

  virtual lldb::ScriptedThreadInterfaceSP CreateScriptedThreadInterface() {
    return {};
  }

  virtual lldb::ScriptedThreadPlanInterfaceSP
  CreateScriptedThreadPlanInterface() {
    return {};
  }

  virtual lldb::OperatingSystemInterfaceSP CreateOperatingSystemInterface() {
    return {};
  }

  virtual lldb::ScriptedPlatformInterfaceUP GetScriptedPlatformInterface() {
    return {};
  }

  virtual StructuredData::ObjectSP
  CreateStructuredDataFromScriptObject(ScriptObject obj) {
    return {};
  }

  lldb::DataExtractorSP
  GetDataExtractorFromSBData(const lldb::SBData &data) const;

  Status GetStatusFromSBError(const lldb::SBError &error) const;

  Event *GetOpaqueTypeFromSBEvent(const lldb::SBEvent &event) const;

  lldb::StreamSP GetOpaqueTypeFromSBStream(const lldb::SBStream &stream) const;

  lldb::BreakpointSP
  GetOpaqueTypeFromSBBreakpoint(const lldb::SBBreakpoint &breakpoint) const;

  lldb::ProcessAttachInfoSP
  GetOpaqueTypeFromSBAttachInfo(const lldb::SBAttachInfo &attach_info) const;

  lldb::ProcessLaunchInfoSP
  GetOpaqueTypeFromSBLaunchInfo(const lldb::SBLaunchInfo &launch_info) const;

  std::optional<MemoryRegionInfo> GetOpaqueTypeFromSBMemoryRegionInfo(
      const lldb::SBMemoryRegionInfo &mem_region) const;

protected:
  Debugger &m_debugger;
  lldb::ScriptLanguage m_script_lang;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_SCRIPTINTERPRETER_H
