//===-- Target.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_TARGET_H
#define LLDB_TARGET_TARGET_H

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lldb/Breakpoint/BreakpointList.h"
#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/WatchpointList.h"
#include "lldb/Core/Architecture.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/PathMappingList.h"
#include "lldb/Target/SectionLoadHistory.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

OptionEnumValues GetDynamicValueTypes();

enum InlineStrategy {
  eInlineBreakpointsNever = 0,
  eInlineBreakpointsHeaders,
  eInlineBreakpointsAlways
};

enum LoadScriptFromSymFile {
  eLoadScriptFromSymFileTrue,
  eLoadScriptFromSymFileFalse,
  eLoadScriptFromSymFileWarn
};

enum LoadCWDlldbinitFile {
  eLoadCWDlldbinitTrue,
  eLoadCWDlldbinitFalse,
  eLoadCWDlldbinitWarn
};

enum ImportStdModule {
  eImportStdModuleFalse,
  eImportStdModuleFallback,
  eImportStdModuleTrue,
};

enum DynamicClassInfoHelper {
  eDynamicClassInfoHelperAuto,
  eDynamicClassInfoHelperRealizedClassesStruct,
  eDynamicClassInfoHelperCopyRealizedClassList,
  eDynamicClassInfoHelperGetRealizedClassList,
};

class TargetExperimentalProperties : public Properties {
public:
  TargetExperimentalProperties();
};

class TargetProperties : public Properties {
public:
  TargetProperties(Target *target);

  ~TargetProperties() override;

  ArchSpec GetDefaultArchitecture() const;

  void SetDefaultArchitecture(const ArchSpec &arch);

  bool GetMoveToNearestCode() const;

  lldb::DynamicValueType GetPreferDynamicValue() const;

  bool SetPreferDynamicValue(lldb::DynamicValueType d);

  bool GetPreloadSymbols() const;

  void SetPreloadSymbols(bool b);

  bool GetDisableASLR() const;

  void SetDisableASLR(bool b);

  bool GetInheritTCC() const;

  void SetInheritTCC(bool b);

  bool GetDetachOnError() const;

  void SetDetachOnError(bool b);

  bool GetDisableSTDIO() const;

  void SetDisableSTDIO(bool b);

  const char *GetDisassemblyFlavor() const;

  InlineStrategy GetInlineStrategy() const;

  llvm::StringRef GetArg0() const;

  void SetArg0(llvm::StringRef arg);

  bool GetRunArguments(Args &args) const;

  void SetRunArguments(const Args &args);

  // Get the whole environment including the platform inherited environment and
  // the target specific environment, excluding the unset environment variables.
  Environment GetEnvironment() const;
  // Get the platform inherited environment, excluding the unset environment
  // variables.
  Environment GetInheritedEnvironment() const;
  // Get the target specific environment only, without the platform inherited
  // environment.
  Environment GetTargetEnvironment() const;
  // Set the target specific environment.
  void SetEnvironment(Environment env);

  bool GetSkipPrologue() const;

  PathMappingList &GetSourcePathMap() const;

  bool GetAutoSourceMapRelative() const;

  FileSpecList GetExecutableSearchPaths();

  void AppendExecutableSearchPaths(const FileSpec &);

  FileSpecList GetDebugFileSearchPaths();

  FileSpecList GetClangModuleSearchPaths();

  bool GetEnableAutoImportClangModules() const;

  ImportStdModule GetImportStdModule() const;

  DynamicClassInfoHelper GetDynamicClassInfoHelper() const;

  bool GetEnableAutoApplyFixIts() const;

  uint64_t GetNumberOfRetriesWithFixits() const;

  bool GetEnableNotifyAboutFixIts() const;

  FileSpec GetSaveJITObjectsDir() const;

  bool GetEnableSyntheticValue() const;

  bool ShowHexVariableValuesWithLeadingZeroes() const;

  uint32_t GetMaxZeroPaddingInFloatFormat() const;

  uint32_t GetMaximumNumberOfChildrenToDisplay() const;

  /// Get the max depth value, augmented with a bool to indicate whether the
  /// depth is the default.
  ///
  /// When the user has customized the max depth, the bool will be false.
  ///
  /// \returns the max depth, and true if the max depth is the system default,
  /// otherwise false.
  std::pair<uint32_t, bool> GetMaximumDepthOfChildrenToDisplay() const;

  uint32_t GetMaximumSizeOfStringSummary() const;

  uint32_t GetMaximumMemReadSize() const;

  FileSpec GetStandardInputPath() const;
  FileSpec GetStandardErrorPath() const;
  FileSpec GetStandardOutputPath() const;

  void SetStandardInputPath(llvm::StringRef path);
  void SetStandardOutputPath(llvm::StringRef path);
  void SetStandardErrorPath(llvm::StringRef path);

  void SetStandardInputPath(const char *path) = delete;
  void SetStandardOutputPath(const char *path) = delete;
  void SetStandardErrorPath(const char *path) = delete;

  bool GetBreakpointsConsultPlatformAvoidList();

  SourceLanguage GetLanguage() const;

  llvm::StringRef GetExpressionPrefixContents();

  uint64_t GetExprErrorLimit() const;

  uint64_t GetExprAllocAddress() const;

  uint64_t GetExprAllocSize() const;

  uint64_t GetExprAllocAlign() const;

  bool GetUseHexImmediates() const;

  bool GetUseFastStepping() const;

  bool GetDisplayExpressionsInCrashlogs() const;

  LoadScriptFromSymFile GetLoadScriptFromSymbolFile() const;

  LoadCWDlldbinitFile GetLoadCWDlldbinitFile() const;

  Disassembler::HexImmediateStyle GetHexImmediateStyle() const;

  MemoryModuleLoadLevel GetMemoryModuleLoadLevel() const;

  bool GetUserSpecifiedTrapHandlerNames(Args &args) const;

  void SetUserSpecifiedTrapHandlerNames(const Args &args);

  bool GetDisplayRuntimeSupportValues() const;

  void SetDisplayRuntimeSupportValues(bool b);

  bool GetDisplayRecognizedArguments() const;

  void SetDisplayRecognizedArguments(bool b);

  const ProcessLaunchInfo &GetProcessLaunchInfo() const;

  void SetProcessLaunchInfo(const ProcessLaunchInfo &launch_info);

  bool GetInjectLocalVariables(ExecutionContext *exe_ctx) const;

  void SetRequireHardwareBreakpoints(bool b);

  bool GetRequireHardwareBreakpoints() const;

  bool GetAutoInstallMainExecutable() const;

  void UpdateLaunchInfoFromProperties();

  void SetDebugUtilityExpression(bool debug);

  bool GetDebugUtilityExpression() const;

private:
  std::optional<bool>
  GetExperimentalPropertyValue(size_t prop_idx,
                               ExecutionContext *exe_ctx = nullptr) const;

  // Callbacks for m_launch_info.
  void Arg0ValueChangedCallback();
  void RunArgsValueChangedCallback();
  void EnvVarsValueChangedCallback();
  void InputPathValueChangedCallback();
  void OutputPathValueChangedCallback();
  void ErrorPathValueChangedCallback();
  void DetachOnErrorValueChangedCallback();
  void DisableASLRValueChangedCallback();
  void InheritTCCValueChangedCallback();
  void DisableSTDIOValueChangedCallback();

  // Settings checker for target.jit-save-objects-dir:
  void CheckJITObjectsDir();

  Environment ComputeEnvironment() const;

  // Member variables.
  ProcessLaunchInfo m_launch_info;
  std::unique_ptr<TargetExperimentalProperties> m_experimental_properties_up;
  Target *m_target;
};

class EvaluateExpressionOptions {
public:
// MSVC has a bug here that reports C4268: 'const' static/global data
// initialized with compiler generated default constructor fills the object
// with zeros. Confirmed that MSVC is *not* zero-initializing, it's just a
// bogus warning.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4268)
#endif
  static constexpr std::chrono::milliseconds default_timeout{500};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

  static constexpr ExecutionPolicy default_execution_policy =
      eExecutionPolicyOnlyWhenNeeded;

  EvaluateExpressionOptions() = default;

  ExecutionPolicy GetExecutionPolicy() const { return m_execution_policy; }

  void SetExecutionPolicy(ExecutionPolicy policy = eExecutionPolicyAlways) {
    m_execution_policy = policy;
  }

  SourceLanguage GetLanguage() const { return m_language; }

  void SetLanguage(lldb::LanguageType language_type) {
    m_language = SourceLanguage(language_type);
  }

  /// Set the language using a pair of language code and version as
  /// defined by the DWARF 6 specification.
  /// WARNING: These codes may change until DWARF 6 is finalized.
  void SetLanguage(uint16_t name, uint32_t version) {
    m_language = SourceLanguage(name, version);
  }

  bool DoesCoerceToId() const { return m_coerce_to_id; }

  const char *GetPrefix() const {
    return (m_prefix.empty() ? nullptr : m_prefix.c_str());
  }

  void SetPrefix(const char *prefix) {
    if (prefix && prefix[0])
      m_prefix = prefix;
    else
      m_prefix.clear();
  }

  void SetCoerceToId(bool coerce = true) { m_coerce_to_id = coerce; }

  bool DoesUnwindOnError() const { return m_unwind_on_error; }

  void SetUnwindOnError(bool unwind = false) { m_unwind_on_error = unwind; }

  bool DoesIgnoreBreakpoints() const { return m_ignore_breakpoints; }

  void SetIgnoreBreakpoints(bool ignore = false) {
    m_ignore_breakpoints = ignore;
  }

  bool DoesKeepInMemory() const { return m_keep_in_memory; }

  void SetKeepInMemory(bool keep = true) { m_keep_in_memory = keep; }

  lldb::DynamicValueType GetUseDynamic() const { return m_use_dynamic; }

  void
  SetUseDynamic(lldb::DynamicValueType dynamic = lldb::eDynamicCanRunTarget) {
    m_use_dynamic = dynamic;
  }

  const Timeout<std::micro> &GetTimeout() const { return m_timeout; }

  void SetTimeout(const Timeout<std::micro> &timeout) { m_timeout = timeout; }

  const Timeout<std::micro> &GetOneThreadTimeout() const {
    return m_one_thread_timeout;
  }

  void SetOneThreadTimeout(const Timeout<std::micro> &timeout) {
    m_one_thread_timeout = timeout;
  }

  bool GetTryAllThreads() const { return m_try_others; }

  void SetTryAllThreads(bool try_others = true) { m_try_others = try_others; }

  bool GetStopOthers() const { return m_stop_others; }

  void SetStopOthers(bool stop_others = true) { m_stop_others = stop_others; }

  bool GetDebug() const { return m_debug; }

  void SetDebug(bool b) {
    m_debug = b;
    if (m_debug)
      m_generate_debug_info = true;
  }

  bool GetGenerateDebugInfo() const { return m_generate_debug_info; }

  void SetGenerateDebugInfo(bool b) { m_generate_debug_info = b; }

  bool GetColorizeErrors() const { return m_ansi_color_errors; }

  void SetColorizeErrors(bool b) { m_ansi_color_errors = b; }

  bool GetTrapExceptions() const { return m_trap_exceptions; }

  void SetTrapExceptions(bool b) { m_trap_exceptions = b; }

  bool GetREPLEnabled() const { return m_repl; }

  void SetREPLEnabled(bool b) { m_repl = b; }

  void SetCancelCallback(lldb::ExpressionCancelCallback callback, void *baton) {
    m_cancel_callback_baton = baton;
    m_cancel_callback = callback;
  }

  bool InvokeCancelCallback(lldb::ExpressionEvaluationPhase phase) const {
    return ((m_cancel_callback != nullptr)
                ? m_cancel_callback(phase, m_cancel_callback_baton)
                : false);
  }

  // Allows the expression contents to be remapped to point to the specified
  // file and line using #line directives.
  void SetPoundLine(const char *path, uint32_t line) const {
    if (path && path[0]) {
      m_pound_line_file = path;
      m_pound_line_line = line;
    } else {
      m_pound_line_file.clear();
      m_pound_line_line = 0;
    }
  }

  const char *GetPoundLineFilePath() const {
    return (m_pound_line_file.empty() ? nullptr : m_pound_line_file.c_str());
  }

  uint32_t GetPoundLineLine() const { return m_pound_line_line; }

  void SetSuppressPersistentResult(bool b) { m_suppress_persistent_result = b; }

  bool GetSuppressPersistentResult() const {
    return m_suppress_persistent_result;
  }

  void SetAutoApplyFixIts(bool b) { m_auto_apply_fixits = b; }

  bool GetAutoApplyFixIts() const { return m_auto_apply_fixits; }

  void SetRetriesWithFixIts(uint64_t number_of_retries) {
    m_retries_with_fixits = number_of_retries;
  }

  uint64_t GetRetriesWithFixIts() const { return m_retries_with_fixits; }

  bool IsForUtilityExpr() const { return m_running_utility_expression; }

  void SetIsForUtilityExpr(bool b) { m_running_utility_expression = b; }

private:
  ExecutionPolicy m_execution_policy = default_execution_policy;
  SourceLanguage m_language;
  std::string m_prefix;
  bool m_coerce_to_id = false;
  bool m_unwind_on_error = true;
  bool m_ignore_breakpoints = false;
  bool m_keep_in_memory = false;
  bool m_try_others = true;
  bool m_stop_others = true;
  bool m_debug = false;
  bool m_trap_exceptions = true;
  bool m_repl = false;
  bool m_generate_debug_info = false;
  bool m_ansi_color_errors = false;
  bool m_suppress_persistent_result = false;
  bool m_auto_apply_fixits = true;
  uint64_t m_retries_with_fixits = 1;
  /// True if the executed code should be treated as utility code that is only
  /// used by LLDB internally.
  bool m_running_utility_expression = false;

  lldb::DynamicValueType m_use_dynamic = lldb::eNoDynamicValues;
  Timeout<std::micro> m_timeout = default_timeout;
  Timeout<std::micro> m_one_thread_timeout = std::nullopt;
  lldb::ExpressionCancelCallback m_cancel_callback = nullptr;
  void *m_cancel_callback_baton = nullptr;
  // If m_pound_line_file is not empty and m_pound_line_line is non-zero, use
  // #line %u "%s" before the expression content to remap where the source
  // originates
  mutable std::string m_pound_line_file;
  mutable uint32_t m_pound_line_line = 0;
};

// Target
class Target : public std::enable_shared_from_this<Target>,
               public TargetProperties,
               public Broadcaster,
               public ExecutionContextScope,
               public ModuleList::Notifier {
public:
  friend class TargetList;
  friend class Debugger;

  /// Broadcaster event bits definitions.
  enum {
    eBroadcastBitBreakpointChanged = (1 << 0),
    eBroadcastBitModulesLoaded = (1 << 1),
    eBroadcastBitModulesUnloaded = (1 << 2),
    eBroadcastBitWatchpointChanged = (1 << 3),
    eBroadcastBitSymbolsLoaded = (1 << 4),
    eBroadcastBitSymbolsChanged = (1 << 5),
  };

  // These two functions fill out the Broadcaster interface:

  static llvm::StringRef GetStaticBroadcasterClass();

  llvm::StringRef GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

  // This event data class is for use by the TargetList to broadcast new target
  // notifications.
  class TargetEventData : public EventData {
  public:
    TargetEventData(const lldb::TargetSP &target_sp);

    TargetEventData(const lldb::TargetSP &target_sp,
                    const ModuleList &module_list);

    ~TargetEventData() override;

    static llvm::StringRef GetFlavorString();

    llvm::StringRef GetFlavor() const override {
      return TargetEventData::GetFlavorString();
    }

    void Dump(Stream *s) const override;

    static const TargetEventData *GetEventDataFromEvent(const Event *event_ptr);

    static lldb::TargetSP GetTargetFromEvent(const Event *event_ptr);

    static ModuleList GetModuleListFromEvent(const Event *event_ptr);

    const lldb::TargetSP &GetTarget() const { return m_target_sp; }

    const ModuleList &GetModuleList() const { return m_module_list; }

  private:
    lldb::TargetSP m_target_sp;
    ModuleList m_module_list;

    TargetEventData(const TargetEventData &) = delete;
    const TargetEventData &operator=(const TargetEventData &) = delete;
  };

  ~Target() override;

  static void SettingsInitialize();

  static void SettingsTerminate();

  static FileSpecList GetDefaultExecutableSearchPaths();

  static FileSpecList GetDefaultDebugFileSearchPaths();

  static ArchSpec GetDefaultArchitecture();

  static void SetDefaultArchitecture(const ArchSpec &arch);

  bool IsDummyTarget() const { return m_is_dummy_target; }

  const std::string &GetLabel() const { return m_label; }

  /// Set a label for a target.
  ///
  /// The label cannot be used by another target or be only integral.
  ///
  /// \return
  ///     The label for this target or an error if the label didn't match the
  ///     requirements.
  llvm::Error SetLabel(llvm::StringRef label);

  /// Find a binary on the system and return its Module,
  /// or return an existing Module that is already in the Target.
  ///
  /// Given a ModuleSpec, find a binary satisifying that specification,
  /// or identify a matching Module already present in the Target,
  /// and return a shared pointer to it.
  ///
  /// \param[in] module_spec
  ///     The criteria that must be matched for the binary being loaded.
  ///     e.g. UUID, architecture, file path.
  ///
  /// \param[in] notify
  ///     If notify is true, and the Module is new to this Target,
  ///     Target::ModulesDidLoad will be called.
  ///     If notify is false, it is assumed that the caller is adding
  ///     multiple Modules and will call ModulesDidLoad with the
  ///     full list at the end.
  ///     ModulesDidLoad must be called when a Module/Modules have
  ///     been added to the target, one way or the other.
  ///
  /// \param[out] error_ptr
  ///     Optional argument, pointing to a Status object to fill in
  ///     with any results / messages while attempting to find/load
  ///     this binary.  Many callers will be internal functions that
  ///     will handle / summarize the failures in a custom way and
  ///     don't use these messages.
  ///
  /// \return
  ///     An empty ModuleSP will be returned if no matching file
  ///     was found.  If error_ptr was non-nullptr, an error message
  ///     will likely be provided.
  lldb::ModuleSP GetOrCreateModule(const ModuleSpec &module_spec, bool notify,
                                   Status *error_ptr = nullptr);

  // Settings accessors

  static TargetProperties &GetGlobalProperties();

  std::recursive_mutex &GetAPIMutex();

  void DeleteCurrentProcess();

  void CleanupProcess();

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the
  /// supplied stream \a s. The dumped content will be only what has
  /// been loaded or parsed up to this point at which this function
  /// is called, so this is a good way to see what has been parsed
  /// in a target.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s, lldb::DescriptionLevel description_level);

  // If listener_sp is null, the listener of the owning Debugger object will be
  // used.
  const lldb::ProcessSP &CreateProcess(lldb::ListenerSP listener_sp,
                                       llvm::StringRef plugin_name,
                                       const FileSpec *crash_file,
                                       bool can_connect);

  const lldb::ProcessSP &GetProcessSP() const;

  bool IsValid() { return m_valid; }

  void Destroy();

  Status Launch(ProcessLaunchInfo &launch_info,
                Stream *stream); // Optional stream to receive first stop info

  Status Attach(ProcessAttachInfo &attach_info,
                Stream *stream); // Optional stream to receive first stop info

  // This part handles the breakpoints.

  BreakpointList &GetBreakpointList(bool internal = false);

  const BreakpointList &GetBreakpointList(bool internal = false) const;

  lldb::BreakpointSP GetLastCreatedBreakpoint() {
    return m_last_created_breakpoint;
  }

  lldb::BreakpointSP GetBreakpointByID(lldb::break_id_t break_id);

  lldb::BreakpointSP CreateBreakpointAtUserEntry(Status &error);

  // Use this to create a file and line breakpoint to a given module or all
  // module it is nullptr
  lldb::BreakpointSP CreateBreakpoint(const FileSpecList *containingModules,
                                      const FileSpec &file, uint32_t line_no,
                                      uint32_t column, lldb::addr_t offset,
                                      LazyBool check_inlines,
                                      LazyBool skip_prologue, bool internal,
                                      bool request_hardware,
                                      LazyBool move_to_nearest_code);

  // Use this to create breakpoint that matches regex against the source lines
  // in files given in source_file_list: If function_names is non-empty, also
  // filter by function after the matches are made.
  lldb::BreakpointSP CreateSourceRegexBreakpoint(
      const FileSpecList *containingModules,
      const FileSpecList *source_file_list,
      const std::unordered_set<std::string> &function_names,
      RegularExpression source_regex, bool internal, bool request_hardware,
      LazyBool move_to_nearest_code);

  // Use this to create a breakpoint from a load address
  lldb::BreakpointSP CreateBreakpoint(lldb::addr_t load_addr, bool internal,
                                      bool request_hardware);

  // Use this to create a breakpoint from a load address and a module file spec
  lldb::BreakpointSP CreateAddressInModuleBreakpoint(lldb::addr_t file_addr,
                                                     bool internal,
                                                     const FileSpec &file_spec,
                                                     bool request_hardware);

  // Use this to create Address breakpoints:
  lldb::BreakpointSP CreateBreakpoint(const Address &addr, bool internal,
                                      bool request_hardware);

  // Use this to create a function breakpoint by regexp in
  // containingModule/containingSourceFiles, or all modules if it is nullptr
  // When "skip_prologue is set to eLazyBoolCalculate, we use the current
  // target setting, else we use the values passed in
  lldb::BreakpointSP CreateFuncRegexBreakpoint(
      const FileSpecList *containingModules,
      const FileSpecList *containingSourceFiles, RegularExpression func_regexp,
      lldb::LanguageType requested_language, LazyBool skip_prologue,
      bool internal, bool request_hardware);

  // Use this to create a function breakpoint by name in containingModule, or
  // all modules if it is nullptr When "skip_prologue is set to
  // eLazyBoolCalculate, we use the current target setting, else we use the
  // values passed in. func_name_type_mask is or'ed values from the
  // FunctionNameType enum.
  lldb::BreakpointSP CreateBreakpoint(
      const FileSpecList *containingModules,
      const FileSpecList *containingSourceFiles, const char *func_name,
      lldb::FunctionNameType func_name_type_mask, lldb::LanguageType language,
      lldb::addr_t offset, LazyBool skip_prologue, bool internal,
      bool request_hardware);

  lldb::BreakpointSP
  CreateExceptionBreakpoint(enum lldb::LanguageType language, bool catch_bp,
                            bool throw_bp, bool internal,
                            Args *additional_args = nullptr,
                            Status *additional_args_error = nullptr);

  lldb::BreakpointSP CreateScriptedBreakpoint(
      const llvm::StringRef class_name, const FileSpecList *containingModules,
      const FileSpecList *containingSourceFiles, bool internal,
      bool request_hardware, StructuredData::ObjectSP extra_args_sp,
      Status *creation_error = nullptr);

  // This is the same as the func_name breakpoint except that you can specify a
  // vector of names.  This is cheaper than a regular expression breakpoint in
  // the case where you just want to set a breakpoint on a set of names you
  // already know. func_name_type_mask is or'ed values from the
  // FunctionNameType enum.
  lldb::BreakpointSP CreateBreakpoint(
      const FileSpecList *containingModules,
      const FileSpecList *containingSourceFiles, const char *func_names[],
      size_t num_names, lldb::FunctionNameType func_name_type_mask,
      lldb::LanguageType language, lldb::addr_t offset, LazyBool skip_prologue,
      bool internal, bool request_hardware);

  lldb::BreakpointSP
  CreateBreakpoint(const FileSpecList *containingModules,
                   const FileSpecList *containingSourceFiles,
                   const std::vector<std::string> &func_names,
                   lldb::FunctionNameType func_name_type_mask,
                   lldb::LanguageType language, lldb::addr_t m_offset,
                   LazyBool skip_prologue, bool internal,
                   bool request_hardware);

  // Use this to create a general breakpoint:
  lldb::BreakpointSP CreateBreakpoint(lldb::SearchFilterSP &filter_sp,
                                      lldb::BreakpointResolverSP &resolver_sp,
                                      bool internal, bool request_hardware,
                                      bool resolve_indirect_symbols);

  // Use this to create a watchpoint:
  lldb::WatchpointSP CreateWatchpoint(lldb::addr_t addr, size_t size,
                                      const CompilerType *type, uint32_t kind,
                                      Status &error);

  lldb::WatchpointSP GetLastCreatedWatchpoint() {
    return m_last_created_watchpoint;
  }

  WatchpointList &GetWatchpointList() { return m_watchpoint_list; }

  // Manages breakpoint names:
  void AddNameToBreakpoint(BreakpointID &id, llvm::StringRef name,
                           Status &error);

  void AddNameToBreakpoint(lldb::BreakpointSP &bp_sp, llvm::StringRef name,
                           Status &error);

  void RemoveNameFromBreakpoint(lldb::BreakpointSP &bp_sp, ConstString name);

  BreakpointName *FindBreakpointName(ConstString name, bool can_create,
                                     Status &error);

  void DeleteBreakpointName(ConstString name);

  void ConfigureBreakpointName(BreakpointName &bp_name,
                               const BreakpointOptions &options,
                               const BreakpointName::Permissions &permissions);
  void ApplyNameToBreakpoints(BreakpointName &bp_name);

  void AddBreakpointName(std::unique_ptr<BreakpointName> bp_name);

  void GetBreakpointNames(std::vector<std::string> &names);

  // This call removes ALL breakpoints regardless of permission.
  void RemoveAllBreakpoints(bool internal_also = false);

  // This removes all the breakpoints, but obeys the ePermDelete on them.
  void RemoveAllowedBreakpoints();

  void DisableAllBreakpoints(bool internal_also = false);

  void DisableAllowedBreakpoints();

  void EnableAllBreakpoints(bool internal_also = false);

  void EnableAllowedBreakpoints();

  bool DisableBreakpointByID(lldb::break_id_t break_id);

  bool EnableBreakpointByID(lldb::break_id_t break_id);

  bool RemoveBreakpointByID(lldb::break_id_t break_id);

  /// Resets the hit count of all breakpoints.
  void ResetBreakpointHitCounts();

  // The flag 'end_to_end', default to true, signifies that the operation is
  // performed end to end, for both the debugger and the debuggee.

  bool RemoveAllWatchpoints(bool end_to_end = true);

  bool DisableAllWatchpoints(bool end_to_end = true);

  bool EnableAllWatchpoints(bool end_to_end = true);

  bool ClearAllWatchpointHitCounts();

  bool ClearAllWatchpointHistoricValues();

  bool IgnoreAllWatchpoints(uint32_t ignore_count);

  bool DisableWatchpointByID(lldb::watch_id_t watch_id);

  bool EnableWatchpointByID(lldb::watch_id_t watch_id);

  bool RemoveWatchpointByID(lldb::watch_id_t watch_id);

  bool IgnoreWatchpointByID(lldb::watch_id_t watch_id, uint32_t ignore_count);

  Status SerializeBreakpointsToFile(const FileSpec &file,
                                    const BreakpointIDList &bp_ids,
                                    bool append);

  Status CreateBreakpointsFromFile(const FileSpec &file,
                                   BreakpointIDList &new_bps);

  Status CreateBreakpointsFromFile(const FileSpec &file,
                                   std::vector<std::string> &names,
                                   BreakpointIDList &new_bps);

  /// Get \a load_addr as a callable code load address for this target
  ///
  /// Take \a load_addr and potentially add any address bits that are
  /// needed to make the address callable. For ARM this can set bit
  /// zero (if it already isn't) if \a load_addr is a thumb function.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.
  lldb::addr_t GetCallableLoadAddress(
      lldb::addr_t load_addr,
      AddressClass addr_class = AddressClass::eInvalid) const;

  /// Get \a load_addr as an opcode for this target.
  ///
  /// Take \a load_addr and potentially strip any address bits that are
  /// needed to make the address point to an opcode. For ARM this can
  /// clear bit zero (if it already isn't) if \a load_addr is a
  /// thumb function and load_addr is in code.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.
  lldb::addr_t
  GetOpcodeLoadAddress(lldb::addr_t load_addr,
                       AddressClass addr_class = AddressClass::eInvalid) const;

  // Get load_addr as breakable load address for this target. Take a addr and
  // check if for any reason there is a better address than this to put a
  // breakpoint on. If there is then return that address. For MIPS, if
  // instruction at addr is a delay slot instruction then this method will find
  // the address of its previous instruction and return that address.
  lldb::addr_t GetBreakableLoadAddress(lldb::addr_t addr);

  void ModulesDidLoad(ModuleList &module_list);

  void ModulesDidUnload(ModuleList &module_list, bool delete_locations);

  void SymbolsDidLoad(ModuleList &module_list);

  void ClearModules(bool delete_locations);

  /// Called as the last function in Process::DidExec().
  ///
  /// Process::DidExec() will clear a lot of state in the process,
  /// then try to reload a dynamic loader plugin to discover what
  /// binaries are currently available and then this function should
  /// be called to allow the target to do any cleanup after everything
  /// has been figured out. It can remove breakpoints that no longer
  /// make sense as the exec might have changed the target
  /// architecture, and unloaded some modules that might get deleted.
  void DidExec();

  /// Gets the module for the main executable.
  ///
  /// Each process has a notion of a main executable that is the file
  /// that will be executed or attached to. Executable files can have
  /// dependent modules that are discovered from the object files, or
  /// discovered at runtime as things are dynamically loaded.
  ///
  /// \return
  ///     The shared pointer to the executable module which can
  ///     contains a nullptr Module object if no executable has been
  ///     set.
  ///
  /// \see DynamicLoader
  /// \see ObjectFile::GetDependentModules (FileSpecList&)
  /// \see Process::SetExecutableModule(lldb::ModuleSP&)
  lldb::ModuleSP GetExecutableModule();

  Module *GetExecutableModulePointer();

  /// Set the main executable module.
  ///
  /// Each process has a notion of a main executable that is the file
  /// that will be executed or attached to. Executable files can have
  /// dependent modules that are discovered from the object files, or
  /// discovered at runtime as things are dynamically loaded.
  ///
  /// Setting the executable causes any of the current dependent
  /// image information to be cleared and replaced with the static
  /// dependent image information found by calling
  /// ObjectFile::GetDependentModules (FileSpecList&) on the main
  /// executable and any modules on which it depends. Calling
  /// Process::GetImages() will return the newly found images that
  /// were obtained from all of the object files.
  ///
  /// \param[in] module_sp
  ///     A shared pointer reference to the module that will become
  ///     the main executable for this process.
  ///
  /// \param[in] load_dependent_files
  ///     If \b true then ask the object files to track down any
  ///     known dependent files.
  ///
  /// \see ObjectFile::GetDependentModules (FileSpecList&)
  /// \see Process::GetImages()
  void SetExecutableModule(
      lldb::ModuleSP &module_sp,
      LoadDependentFiles load_dependent_files = eLoadDependentsDefault);

  bool LoadScriptingResources(std::list<Status> &errors,
                              Stream &feedback_stream,
                              bool continue_on_error = true) {
    return m_images.LoadScriptingResourcesInTarget(
        this, errors, feedback_stream, continue_on_error);
  }

  /// Get accessor for the images for this process.
  ///
  /// Each process has a notion of a main executable that is the file
  /// that will be executed or attached to. Executable files can have
  /// dependent modules that are discovered from the object files, or
  /// discovered at runtime as things are dynamically loaded. After
  /// a main executable has been set, the images will contain a list
  /// of all the files that the executable depends upon as far as the
  /// object files know. These images will usually contain valid file
  /// virtual addresses only. When the process is launched or attached
  /// to, the DynamicLoader plug-in will discover where these images
  /// were loaded in memory and will resolve the load virtual
  /// addresses is each image, and also in images that are loaded by
  /// code.
  ///
  /// \return
  ///     A list of Module objects in a module list.
  const ModuleList &GetImages() const { return m_images; }

  ModuleList &GetImages() { return m_images; }

  /// Return whether this FileSpec corresponds to a module that should be
  /// considered for general searches.
  ///
  /// This API will be consulted by the SearchFilterForUnconstrainedSearches
  /// and any module that returns \b true will not be searched.  Note the
  /// SearchFilterForUnconstrainedSearches is the search filter that
  /// gets used in the CreateBreakpoint calls when no modules is provided.
  ///
  /// The target call at present just consults the Platform's call of the
  /// same name.
  ///
  /// \param[in] module_spec
  ///     Path to the module.
  ///
  /// \return \b true if the module should be excluded, \b false otherwise.
  bool ModuleIsExcludedForUnconstrainedSearches(const FileSpec &module_spec);

  /// Return whether this module should be considered for general searches.
  ///
  /// This API will be consulted by the SearchFilterForUnconstrainedSearches
  /// and any module that returns \b true will not be searched.  Note the
  /// SearchFilterForUnconstrainedSearches is the search filter that
  /// gets used in the CreateBreakpoint calls when no modules is provided.
  ///
  /// The target call at present just consults the Platform's call of the
  /// same name.
  ///
  /// FIXME: When we get time we should add a way for the user to set modules
  /// that they
  /// don't want searched, in addition to or instead of the platform ones.
  ///
  /// \param[in] module_sp
  ///     A shared pointer reference to the module that checked.
  ///
  /// \return \b true if the module should be excluded, \b false otherwise.
  bool
  ModuleIsExcludedForUnconstrainedSearches(const lldb::ModuleSP &module_sp);

  const ArchSpec &GetArchitecture() const { return m_arch.GetSpec(); }

  /// Returns the name of the target's ABI plugin.
  llvm::StringRef GetABIName() const;

  /// Set the architecture for this target.
  ///
  /// If the current target has no Images read in, then this just sets the
  /// architecture, which will be used to select the architecture of the
  /// ExecutableModule when that is set. If the current target has an
  /// ExecutableModule, then calling SetArchitecture with a different
  /// architecture from the currently selected one will reset the
  /// ExecutableModule to that slice of the file backing the ExecutableModule.
  /// If the file backing the ExecutableModule does not contain a fork of this
  /// architecture, then this code will return false, and the architecture
  /// won't be changed. If the input arch_spec is the same as the already set
  /// architecture, this is a no-op.
  ///
  /// \param[in] arch_spec
  ///     The new architecture.
  ///
  /// \param[in] set_platform
  ///     If \b true, then the platform will be adjusted if the currently
  ///     selected platform is not compatible with the architecture being set.
  ///     If \b false, then just the architecture will be set even if the
  ///     currently selected platform isn't compatible (in case it might be
  ///     manually set following this function call).
  ///
  /// \param[in] merged
  ///     If true, arch_spec is merged with the current
  ///     architecture. Otherwise it's replaced.
  ///
  /// \return
  ///     \b true if the architecture was successfully set, \b false otherwise.
  bool SetArchitecture(const ArchSpec &arch_spec, bool set_platform = false,
                       bool merge = true);

  bool MergeArchitecture(const ArchSpec &arch_spec);

  Architecture *GetArchitecturePlugin() const { return m_arch.GetPlugin(); }

  Debugger &GetDebugger() { return m_debugger; }

  size_t ReadMemoryFromFileCache(const Address &addr, void *dst, size_t dst_len,
                                 Status &error);

  // Reading memory through the target allows us to skip going to the process
  // for reading memory if possible and it allows us to try and read from any
  // constant sections in our object files on disk. If you always want live
  // program memory, read straight from the process. If you possibly want to
  // read from const sections in object files, read from the target. This
  // version of ReadMemory will try and read memory from the process if the
  // process is alive. The order is:
  // 1 - if (force_live_memory == false) and the address falls in a read-only
  // section, then read from the file cache
  // 2 - if there is a process, then read from memory
  // 3 - if there is no process, then read from the file cache
  //
  // The method is virtual for mocking in the unit tests.
  virtual size_t ReadMemory(const Address &addr, void *dst, size_t dst_len,
                            Status &error, bool force_live_memory = false,
                            lldb::addr_t *load_addr_ptr = nullptr);

  size_t ReadCStringFromMemory(const Address &addr, std::string &out_str,
                               Status &error, bool force_live_memory = false);

  size_t ReadCStringFromMemory(const Address &addr, char *dst,
                               size_t dst_max_len, Status &result_error,
                               bool force_live_memory = false);

  /// Read a NULL terminated string from memory
  ///
  /// This function will read a cache page at a time until a NULL string
  /// terminator is found. It will stop reading if an aligned sequence of NULL
  /// termination \a type_width bytes is not found before reading \a
  /// cstr_max_len bytes.  The results are always guaranteed to be NULL
  /// terminated, and that no more than (max_bytes - type_width) bytes will be
  /// read.
  ///
  /// \param[in] addr
  ///     The address to start the memory read.
  ///
  /// \param[in] dst
  ///     A character buffer containing at least max_bytes.
  ///
  /// \param[in] max_bytes
  ///     The maximum number of bytes to read.
  ///
  /// \param[in] error
  ///     The error status of the read operation.
  ///
  /// \param[in] type_width
  ///     The size of the null terminator (1 to 4 bytes per
  ///     character).  Defaults to 1.
  ///
  /// \return
  ///     The error status or the number of bytes prior to the null terminator.
  size_t ReadStringFromMemory(const Address &addr, char *dst, size_t max_bytes,
                              Status &error, size_t type_width,
                              bool force_live_memory = true);

  size_t ReadScalarIntegerFromMemory(const Address &addr, uint32_t byte_size,
                                     bool is_signed, Scalar &scalar,
                                     Status &error,
                                     bool force_live_memory = false);

  uint64_t ReadUnsignedIntegerFromMemory(const Address &addr,
                                         size_t integer_byte_size,
                                         uint64_t fail_value, Status &error,
                                         bool force_live_memory = false);

  bool ReadPointerFromMemory(const Address &addr, Status &error,
                             Address &pointer_addr,
                             bool force_live_memory = false);

  SectionLoadList &GetSectionLoadList() {
    return m_section_load_history.GetCurrentSectionLoadList();
  }

  static Target *GetTargetFromContexts(const ExecutionContext *exe_ctx_ptr,
                                       const SymbolContext *sc_ptr);

  // lldb::ExecutionContextScope pure virtual functions
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override;

  lldb::ThreadSP CalculateThread() override;

  lldb::StackFrameSP CalculateStackFrame() override;

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  PathMappingList &GetImageSearchPathList();

  llvm::Expected<lldb::TypeSystemSP>
  GetScratchTypeSystemForLanguage(lldb::LanguageType language,
                                  bool create_on_demand = true);

  std::vector<lldb::TypeSystemSP>
  GetScratchTypeSystems(bool create_on_demand = true);

  PersistentExpressionState *
  GetPersistentExpressionStateForLanguage(lldb::LanguageType language);

  // Creates a UserExpression for the given language, the rest of the
  // parameters have the same meaning as for the UserExpression constructor.
  // Returns a new-ed object which the caller owns.

  UserExpression *
  GetUserExpressionForLanguage(llvm::StringRef expr, llvm::StringRef prefix,
                               SourceLanguage language,
                               Expression::ResultType desired_type,
                               const EvaluateExpressionOptions &options,
                               ValueObject *ctx_obj, Status &error);

  // Creates a FunctionCaller for the given language, the rest of the
  // parameters have the same meaning as for the FunctionCaller constructor.
  // Since a FunctionCaller can't be
  // IR Interpreted, it makes no sense to call this with an
  // ExecutionContextScope that lacks
  // a Process.
  // Returns a new-ed object which the caller owns.

  FunctionCaller *GetFunctionCallerForLanguage(lldb::LanguageType language,
                                               const CompilerType &return_type,
                                               const Address &function_address,
                                               const ValueList &arg_value_list,
                                               const char *name, Status &error);

  /// Creates and installs a UtilityFunction for the given language.
  llvm::Expected<std::unique_ptr<UtilityFunction>>
  CreateUtilityFunction(std::string expression, std::string name,
                        lldb::LanguageType language, ExecutionContext &exe_ctx);

  // Install any files through the platform that need be to installed prior to
  // launching or attaching.
  Status Install(ProcessLaunchInfo *launch_info);

  bool ResolveFileAddress(lldb::addr_t load_addr, Address &so_addr);

  bool ResolveLoadAddress(lldb::addr_t load_addr, Address &so_addr,
                          uint32_t stop_id = SectionLoadHistory::eStopIDNow);

  bool SetSectionLoadAddress(const lldb::SectionSP &section,
                             lldb::addr_t load_addr,
                             bool warn_multiple = false);

  size_t UnloadModuleSections(const lldb::ModuleSP &module_sp);

  size_t UnloadModuleSections(const ModuleList &module_list);

  bool SetSectionUnloaded(const lldb::SectionSP &section_sp);

  bool SetSectionUnloaded(const lldb::SectionSP &section_sp,
                          lldb::addr_t load_addr);

  void ClearAllLoadedSections();

  /// Set the \a Trace object containing processor trace information of this
  /// target.
  ///
  /// \param[in] trace_sp
  ///   The trace object.
  void SetTrace(const lldb::TraceSP &trace_sp);

  /// Get the \a Trace object containing processor trace information of this
  /// target.
  ///
  /// \return
  ///   The trace object. It might be undefined.
  lldb::TraceSP GetTrace();

  /// Create a \a Trace object for the current target using the using the
  /// default supported tracing technology for this process.
  ///
  /// \return
  ///     The new \a Trace or an \a llvm::Error if a \a Trace already exists or
  ///     the trace couldn't be created.
  llvm::Expected<lldb::TraceSP> CreateTrace();

  /// If a \a Trace object is present, this returns it, otherwise a new Trace is
  /// created with \a Trace::CreateTrace.
  llvm::Expected<lldb::TraceSP> GetTraceOrCreate();

  // Since expressions results can persist beyond the lifetime of a process,
  // and the const expression results are available after a process is gone, we
  // provide a way for expressions to be evaluated from the Target itself. If
  // an expression is going to be run, then it should have a frame filled in in
  // the execution context.
  lldb::ExpressionResults EvaluateExpression(
      llvm::StringRef expression, ExecutionContextScope *exe_scope,
      lldb::ValueObjectSP &result_valobj_sp,
      const EvaluateExpressionOptions &options = EvaluateExpressionOptions(),
      std::string *fixed_expression = nullptr, ValueObject *ctx_obj = nullptr);

  lldb::ExpressionVariableSP GetPersistentVariable(ConstString name);

  lldb::addr_t GetPersistentSymbol(ConstString name);

  /// This method will return the address of the starting function for
  /// this binary, e.g. main() or its equivalent.  This can be used as
  /// an address of a function that is not called once a binary has
  /// started running - e.g. as a return address for inferior function
  /// calls that are unambiguous completion of the function call, not
  /// called during the course of the inferior function code running.
  ///
  /// If no entry point can be found, an invalid address is returned.
  ///
  /// \param [out] err
  ///     This object will be set to failure if no entry address could
  ///     be found, and may contain a helpful error message.
  //
  /// \return
  ///     Returns the entry address for this program, or an error
  ///     if none can be found.
  llvm::Expected<lldb_private::Address> GetEntryPointAddress();

  CompilerType GetRegisterType(const std::string &name,
                               const lldb_private::RegisterFlags &flags,
                               uint32_t byte_size);

  // Target Stop Hooks
  class StopHook : public UserID {
  public:
    StopHook(const StopHook &rhs);
    virtual ~StopHook() = default;

    enum class StopHookKind  : uint32_t { CommandBased = 0, ScriptBased };
    enum class StopHookResult : uint32_t {
      KeepStopped = 0,
      RequestContinue,
      AlreadyContinued
    };

    lldb::TargetSP &GetTarget() { return m_target_sp; }

    // Set the specifier.  The stop hook will own the specifier, and is
    // responsible for deleting it when we're done.
    void SetSpecifier(SymbolContextSpecifier *specifier);

    SymbolContextSpecifier *GetSpecifier() { return m_specifier_sp.get(); }

    bool ExecutionContextPasses(const ExecutionContext &exe_ctx);

    // Called on stop, this gets passed the ExecutionContext for each "stop
    // with a reason" thread.  It should add to the stream whatever text it
    // wants to show the user, and return False to indicate it wants the target
    // not to stop.
    virtual StopHookResult HandleStop(ExecutionContext &exe_ctx,
                                      lldb::StreamSP output) = 0;

    // Set the Thread Specifier.  The stop hook will own the thread specifier,
    // and is responsible for deleting it when we're done.
    void SetThreadSpecifier(ThreadSpec *specifier);

    ThreadSpec *GetThreadSpecifier() { return m_thread_spec_up.get(); }

    bool IsActive() { return m_active; }

    void SetIsActive(bool is_active) { m_active = is_active; }

    void SetAutoContinue(bool auto_continue) {
      m_auto_continue = auto_continue;
    }

    bool GetAutoContinue() const { return m_auto_continue; }

    void GetDescription(Stream &s, lldb::DescriptionLevel level) const;
    virtual void GetSubclassDescription(Stream &s,
                                        lldb::DescriptionLevel level) const = 0;

  protected:
    lldb::TargetSP m_target_sp;
    lldb::SymbolContextSpecifierSP m_specifier_sp;
    std::unique_ptr<ThreadSpec> m_thread_spec_up;
    bool m_active = true;
    bool m_auto_continue = false;

    StopHook(lldb::TargetSP target_sp, lldb::user_id_t uid);
  };

  class StopHookCommandLine : public StopHook {
  public:
    ~StopHookCommandLine() override = default;

    StringList &GetCommands() { return m_commands; }
    void SetActionFromString(const std::string &strings);
    void SetActionFromStrings(const std::vector<std::string> &strings);

    StopHookResult HandleStop(ExecutionContext &exc_ctx,
                              lldb::StreamSP output_sp) override;
    void GetSubclassDescription(Stream &s,
                                lldb::DescriptionLevel level) const override;

  private:
    StringList m_commands;
    // Use CreateStopHook to make a new empty stop hook. The GetCommandPointer
    // and fill it with commands, and SetSpecifier to set the specifier shared
    // pointer (can be null, that will match anything.)
    StopHookCommandLine(lldb::TargetSP target_sp, lldb::user_id_t uid)
        : StopHook(target_sp, uid) {}
    friend class Target;
  };

  class StopHookScripted : public StopHook {
  public:
    ~StopHookScripted() override = default;
    StopHookResult HandleStop(ExecutionContext &exc_ctx,
                              lldb::StreamSP output) override;

    Status SetScriptCallback(std::string class_name,
                             StructuredData::ObjectSP extra_args_sp);

    void GetSubclassDescription(Stream &s,
                                lldb::DescriptionLevel level) const override;

  private:
    std::string m_class_name;
    /// This holds the dictionary of keys & values that can be used to
    /// parametrize any given callback's behavior.
    StructuredDataImpl m_extra_args;
    /// This holds the python callback object.
    StructuredData::GenericSP m_implementation_sp;

    /// Use CreateStopHook to make a new empty stop hook. The GetCommandPointer
    /// and fill it with commands, and SetSpecifier to set the specifier shared
    /// pointer (can be null, that will match anything.)
    StopHookScripted(lldb::TargetSP target_sp, lldb::user_id_t uid)
        : StopHook(target_sp, uid) {}
    friend class Target;
  };

  typedef std::shared_ptr<StopHook> StopHookSP;

  /// Add an empty stop hook to the Target's stop hook list, and returns a
  /// shared pointer to it in new_hook. Returns the id of the new hook.
  StopHookSP CreateStopHook(StopHook::StopHookKind kind);

  /// If you tried to create a stop hook, and that failed, call this to
  /// remove the stop hook, as it will also reset the stop hook counter.
  void UndoCreateStopHook(lldb::user_id_t uid);

  // Runs the stop hooks that have been registered for this target.
  // Returns true if the stop hooks cause the target to resume.
  bool RunStopHooks();

  size_t GetStopHookSize();

  bool SetSuppresStopHooks(bool suppress) {
    bool old_value = m_suppress_stop_hooks;
    m_suppress_stop_hooks = suppress;
    return old_value;
  }

  bool GetSuppressStopHooks() { return m_suppress_stop_hooks; }

  bool RemoveStopHookByID(lldb::user_id_t uid);

  void RemoveAllStopHooks();

  StopHookSP GetStopHookByID(lldb::user_id_t uid);

  bool SetStopHookActiveStateByID(lldb::user_id_t uid, bool active_state);

  void SetAllStopHooksActiveState(bool active_state);

  size_t GetNumStopHooks() const { return m_stop_hooks.size(); }

  StopHookSP GetStopHookAtIndex(size_t index) {
    if (index >= GetNumStopHooks())
      return StopHookSP();
    StopHookCollection::iterator pos = m_stop_hooks.begin();

    while (index > 0) {
      pos++;
      index--;
    }
    return (*pos).second;
  }

  lldb::PlatformSP GetPlatform() { return m_platform_sp; }

  void SetPlatform(const lldb::PlatformSP &platform_sp) {
    m_platform_sp = platform_sp;
  }

  SourceManager &GetSourceManager();

  // Methods.
  lldb::SearchFilterSP
  GetSearchFilterForModule(const FileSpec *containingModule);

  lldb::SearchFilterSP
  GetSearchFilterForModuleList(const FileSpecList *containingModuleList);

  lldb::SearchFilterSP
  GetSearchFilterForModuleAndCUList(const FileSpecList *containingModules,
                                    const FileSpecList *containingSourceFiles);

  lldb::REPLSP GetREPL(Status &err, lldb::LanguageType language,
                       const char *repl_options, bool can_create);

  void SetREPL(lldb::LanguageType language, lldb::REPLSP repl_sp);

  StackFrameRecognizerManager &GetFrameRecognizerManager() {
    return *m_frame_recognizer_manager_up;
  }

  void SaveScriptedLaunchInfo(lldb_private::ProcessInfo &process_info);

  /// Add a signal for the target.  This will get copied over to the process
  /// if the signal exists on that target.  Only the values with Yes and No are
  /// set, Calculate values will be ignored.
protected:
  struct DummySignalValues {
    LazyBool pass = eLazyBoolCalculate;
    LazyBool notify = eLazyBoolCalculate;
    LazyBool stop = eLazyBoolCalculate;
    DummySignalValues(LazyBool pass, LazyBool notify, LazyBool stop)
        : pass(pass), notify(notify), stop(stop) {}
    DummySignalValues() = default;
  };
  using DummySignalElement = llvm::StringMapEntry<DummySignalValues>;
  static bool UpdateSignalFromDummy(lldb::UnixSignalsSP signals_sp,
                                    const DummySignalElement &element);
  static bool ResetSignalFromDummy(lldb::UnixSignalsSP signals_sp,
                                   const DummySignalElement &element);

public:
  /// Add a signal to the Target's list of stored signals/actions.  These
  /// values will get copied into any processes launched from
  /// this target.
  void AddDummySignal(llvm::StringRef name, LazyBool pass, LazyBool print,
                      LazyBool stop);
  /// Updates the signals in signals_sp using the stored dummy signals.
  /// If warning_stream_sp is not null, if any stored signals are not found in
  /// the current process, a warning will be emitted here.
  void UpdateSignalsFromDummy(lldb::UnixSignalsSP signals_sp,
                              lldb::StreamSP warning_stream_sp);
  /// Clear the dummy signals in signal_names from the target, or all signals
  /// if signal_names is empty.  Also remove the behaviors they set from the
  /// process's signals if it exists.
  void ClearDummySignals(Args &signal_names);
  /// Print all the signals set in this target.
  void PrintDummySignals(Stream &strm, Args &signals);

protected:
  /// Implementing of ModuleList::Notifier.

  void NotifyModuleAdded(const ModuleList &module_list,
                         const lldb::ModuleSP &module_sp) override;

  void NotifyModuleRemoved(const ModuleList &module_list,
                           const lldb::ModuleSP &module_sp) override;

  void NotifyModuleUpdated(const ModuleList &module_list,
                           const lldb::ModuleSP &old_module_sp,
                           const lldb::ModuleSP &new_module_sp) override;

  void NotifyWillClearList(const ModuleList &module_list) override;

  void NotifyModulesRemoved(lldb_private::ModuleList &module_list) override;

  class Arch {
  public:
    explicit Arch(const ArchSpec &spec);
    const Arch &operator=(const ArchSpec &spec);

    const ArchSpec &GetSpec() const { return m_spec; }
    Architecture *GetPlugin() const { return m_plugin_up.get(); }

  private:
    ArchSpec m_spec;
    std::unique_ptr<Architecture> m_plugin_up;
  };

  // Member variables.
  Debugger &m_debugger;
  lldb::PlatformSP m_platform_sp; ///< The platform for this target.
  std::recursive_mutex m_mutex; ///< An API mutex that is used by the lldb::SB*
                                /// classes make the SB interface thread safe
  /// When the private state thread calls SB API's - usually because it is
  /// running OS plugin or Python ThreadPlan code - it should not block on the
  /// API mutex that is held by the code that kicked off the sequence of events
  /// that led us to run the code.  We hand out this mutex instead when we
  /// detect that code is running on the private state thread.
  std::recursive_mutex m_private_mutex;
  Arch m_arch;
  std::string m_label;
  ModuleList m_images; ///< The list of images for this process (shared
                       /// libraries and anything dynamically loaded).
  SectionLoadHistory m_section_load_history;
  BreakpointList m_breakpoint_list;
  BreakpointList m_internal_breakpoint_list;
  using BreakpointNameList =
      std::map<ConstString, std::unique_ptr<BreakpointName>>;
  BreakpointNameList m_breakpoint_names;

  lldb::BreakpointSP m_last_created_breakpoint;
  WatchpointList m_watchpoint_list;
  lldb::WatchpointSP m_last_created_watchpoint;
  // We want to tightly control the process destruction process so we can
  // correctly tear down everything that we need to, so the only class that
  // knows about the process lifespan is this target class.
  lldb::ProcessSP m_process_sp;
  lldb::SearchFilterSP m_search_filter_sp;
  PathMappingList m_image_search_paths;
  TypeSystemMap m_scratch_type_system_map;

  typedef std::map<lldb::LanguageType, lldb::REPLSP> REPLMap;
  REPLMap m_repl_map;

  lldb::SourceManagerUP m_source_manager_up;

  typedef std::map<lldb::user_id_t, StopHookSP> StopHookCollection;
  StopHookCollection m_stop_hooks;
  lldb::user_id_t m_stop_hook_next_id;
  uint32_t m_latest_stop_hook_id; /// This records the last natural stop at
                                  /// which we ran a stop-hook.
  bool m_valid;
  bool m_suppress_stop_hooks; /// Used to not run stop hooks for expressions
  bool m_is_dummy_target;
  unsigned m_next_persistent_variable_index = 0;
  /// An optional \a lldb_private::Trace object containing processor trace
  /// information of this target.
  lldb::TraceSP m_trace_sp;
  /// Stores the frame recognizers of this target.
  lldb::StackFrameRecognizerManagerUP m_frame_recognizer_manager_up;
  /// These are used to set the signal state when you don't have a process and
  /// more usefully in the Dummy target where you can't know exactly what
  /// signals you will have.
  llvm::StringMap<DummySignalValues> m_dummy_signals;

  static void ImageSearchPathsChanged(const PathMappingList &path_list,
                                      void *baton);

  // Utilities for `statistics` command.
private:
  // Target metrics storage.
  TargetStats m_stats;

public:
  /// Get metrics associated with this target in JSON format.
  ///
  /// Target metrics help measure timings and information that is contained in
  /// a target. These are designed to help measure performance of a debug
  /// session as well as represent the current state of the target, like
  /// information on the currently modules, currently set breakpoints and more.
  ///
  /// \return
  ///     Returns a JSON value that contains all target metrics.
  llvm::json::Value
  ReportStatistics(const lldb_private::StatisticsOptions &options);

  TargetStats &GetStatistics() { return m_stats; }

protected:
  /// Construct with optional file and arch.
  ///
  /// This member is private. Clients must use
  /// TargetList::CreateTarget(const FileSpec*, const ArchSpec*)
  /// so all targets can be tracked from the central target list.
  ///
  /// \see TargetList::CreateTarget(const FileSpec*, const ArchSpec*)
  Target(Debugger &debugger, const ArchSpec &target_arch,
         const lldb::PlatformSP &platform_sp, bool is_dummy_target);

  // Helper function.
  bool ProcessIsValid();

  // Copy breakpoints, stop hooks and so forth from the dummy target:
  void PrimeFromDummyTarget(Target &target);

  void AddBreakpoint(lldb::BreakpointSP breakpoint_sp, bool internal);

  void FinalizeFileActions(ProcessLaunchInfo &info);

  /// Return a recommended size for memory reads at \a addr, optimizing for
  /// cache usage.
  lldb::addr_t GetReasonableReadSize(const Address &addr);

  Target(const Target &) = delete;
  const Target &operator=(const Target &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_TARGET_H
