//===-- Target.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Target.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointPrecondition.h"
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Breakpoint/BreakpointResolverAddress.h"
#include "lldb/Breakpoint/BreakpointResolverFileLine.h"
#include "lldb/Breakpoint/BreakpointResolverFileRegex.h"
#include "lldb/Breakpoint/BreakpointResolverName.h"
#include "lldb/Breakpoint/BreakpointResolverScripted.h"
#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/REPL.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupWatchpoint.h"
#include "lldb/Interpreter/OptionValues.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterTypeBuilder.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"

#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

using namespace lldb;
using namespace lldb_private;

constexpr std::chrono::milliseconds EvaluateExpressionOptions::default_timeout;

Target::Arch::Arch(const ArchSpec &spec)
    : m_spec(spec),
      m_plugin_up(PluginManager::CreateArchitectureInstance(spec)) {}

const Target::Arch &Target::Arch::operator=(const ArchSpec &spec) {
  m_spec = spec;
  m_plugin_up = PluginManager::CreateArchitectureInstance(spec);
  return *this;
}

llvm::StringRef Target::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.target");
  return class_name;
}

Target::Target(Debugger &debugger, const ArchSpec &target_arch,
               const lldb::PlatformSP &platform_sp, bool is_dummy_target)
    : TargetProperties(this),
      Broadcaster(debugger.GetBroadcasterManager(),
                  Target::GetStaticBroadcasterClass().str()),
      ExecutionContextScope(), m_debugger(debugger), m_platform_sp(platform_sp),
      m_mutex(), m_arch(target_arch), m_images(this), m_section_load_history(),
      m_breakpoint_list(false), m_internal_breakpoint_list(true),
      m_watchpoint_list(), m_process_sp(), m_search_filter_sp(),
      m_image_search_paths(ImageSearchPathsChanged, this),
      m_source_manager_up(), m_stop_hooks(), m_stop_hook_next_id(0),
      m_latest_stop_hook_id(0), m_valid(true), m_suppress_stop_hooks(false),
      m_is_dummy_target(is_dummy_target),
      m_frame_recognizer_manager_up(
          std::make_unique<StackFrameRecognizerManager>()) {
  SetEventName(eBroadcastBitBreakpointChanged, "breakpoint-changed");
  SetEventName(eBroadcastBitModulesLoaded, "modules-loaded");
  SetEventName(eBroadcastBitModulesUnloaded, "modules-unloaded");
  SetEventName(eBroadcastBitWatchpointChanged, "watchpoint-changed");
  SetEventName(eBroadcastBitSymbolsLoaded, "symbols-loaded");

  CheckInWithManager();

  LLDB_LOG(GetLog(LLDBLog::Object), "{0} Target::Target()",
           static_cast<void *>(this));
  if (target_arch.IsValid()) {
    LLDB_LOG(GetLog(LLDBLog::Target),
             "Target::Target created with architecture {0} ({1})",
             target_arch.GetArchitectureName(),
             target_arch.GetTriple().getTriple().c_str());
  }

  UpdateLaunchInfoFromProperties();
}

Target::~Target() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOG(log, "{0} Target::~Target()", static_cast<void *>(this));
  DeleteCurrentProcess();
}

void Target::PrimeFromDummyTarget(Target &target) {
  m_stop_hooks = target.m_stop_hooks;

  for (const auto &breakpoint_sp : target.m_breakpoint_list.Breakpoints()) {
    if (breakpoint_sp->IsInternal())
      continue;

    BreakpointSP new_bp(
        Breakpoint::CopyFromBreakpoint(shared_from_this(), *breakpoint_sp));
    AddBreakpoint(std::move(new_bp), false);
  }

  for (const auto &bp_name_entry : target.m_breakpoint_names) {
    AddBreakpointName(std::make_unique<BreakpointName>(*bp_name_entry.second));
  }

  m_frame_recognizer_manager_up = std::make_unique<StackFrameRecognizerManager>(
      *target.m_frame_recognizer_manager_up);

  m_dummy_signals = target.m_dummy_signals;
}

void Target::Dump(Stream *s, lldb::DescriptionLevel description_level) {
  //    s->Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  if (description_level != lldb::eDescriptionLevelBrief) {
    s->Indent();
    s->PutCString("Target\n");
    s->IndentMore();
    m_images.Dump(s);
    m_breakpoint_list.Dump(s);
    m_internal_breakpoint_list.Dump(s);
    s->IndentLess();
  } else {
    Module *exe_module = GetExecutableModulePointer();
    if (exe_module)
      s->PutCString(exe_module->GetFileSpec().GetFilename().GetCString());
    else
      s->PutCString("No executable module.");
  }
}

void Target::CleanupProcess() {
  // Do any cleanup of the target we need to do between process instances.
  // NB It is better to do this before destroying the process in case the
  // clean up needs some help from the process.
  m_breakpoint_list.ClearAllBreakpointSites();
  m_internal_breakpoint_list.ClearAllBreakpointSites();
  ResetBreakpointHitCounts();
  // Disable watchpoints just on the debugger side.
  std::unique_lock<std::recursive_mutex> lock;
  this->GetWatchpointList().GetListMutex(lock);
  DisableAllWatchpoints(false);
  ClearAllWatchpointHitCounts();
  ClearAllWatchpointHistoricValues();
  m_latest_stop_hook_id = 0;
}

void Target::DeleteCurrentProcess() {
  if (m_process_sp) {
    // We dispose any active tracing sessions on the current process
    m_trace_sp.reset();
    m_section_load_history.Clear();
    if (m_process_sp->IsAlive())
      m_process_sp->Destroy(false);

    m_process_sp->Finalize(false /* not destructing */);

    CleanupProcess();

    m_process_sp.reset();
  }
}

const lldb::ProcessSP &Target::CreateProcess(ListenerSP listener_sp,
                                             llvm::StringRef plugin_name,
                                             const FileSpec *crash_file,
                                             bool can_connect) {
  if (!listener_sp)
    listener_sp = GetDebugger().GetListener();
  DeleteCurrentProcess();
  m_process_sp = Process::FindPlugin(shared_from_this(), plugin_name,
                                     listener_sp, crash_file, can_connect);
  return m_process_sp;
}

const lldb::ProcessSP &Target::GetProcessSP() const { return m_process_sp; }

lldb::REPLSP Target::GetREPL(Status &err, lldb::LanguageType language,
                             const char *repl_options, bool can_create) {
  if (language == eLanguageTypeUnknown)
    language = m_debugger.GetREPLLanguage();

  if (language == eLanguageTypeUnknown) {
    LanguageSet repl_languages = Language::GetLanguagesSupportingREPLs();

    if (auto single_lang = repl_languages.GetSingularLanguage()) {
      language = *single_lang;
    } else if (repl_languages.Empty()) {
      err.SetErrorString(
          "LLDB isn't configured with REPL support for any languages.");
      return REPLSP();
    } else {
      err.SetErrorString(
          "Multiple possible REPL languages.  Please specify a language.");
      return REPLSP();
    }
  }

  REPLMap::iterator pos = m_repl_map.find(language);

  if (pos != m_repl_map.end()) {
    return pos->second;
  }

  if (!can_create) {
    err.SetErrorStringWithFormat(
        "Couldn't find an existing REPL for %s, and can't create a new one",
        Language::GetNameForLanguageType(language));
    return lldb::REPLSP();
  }

  Debugger *const debugger = nullptr;
  lldb::REPLSP ret = REPL::Create(err, language, debugger, this, repl_options);

  if (ret) {
    m_repl_map[language] = ret;
    return m_repl_map[language];
  }

  if (err.Success()) {
    err.SetErrorStringWithFormat("Couldn't create a REPL for %s",
                                 Language::GetNameForLanguageType(language));
  }

  return lldb::REPLSP();
}

void Target::SetREPL(lldb::LanguageType language, lldb::REPLSP repl_sp) {
  lldbassert(!m_repl_map.count(language));

  m_repl_map[language] = repl_sp;
}

void Target::Destroy() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_valid = false;
  DeleteCurrentProcess();
  m_platform_sp.reset();
  m_arch = ArchSpec();
  ClearModules(true);
  m_section_load_history.Clear();
  const bool notify = false;
  m_breakpoint_list.RemoveAll(notify);
  m_internal_breakpoint_list.RemoveAll(notify);
  m_last_created_breakpoint.reset();
  m_watchpoint_list.RemoveAll(notify);
  m_last_created_watchpoint.reset();
  m_search_filter_sp.reset();
  m_image_search_paths.Clear(notify);
  m_stop_hooks.clear();
  m_stop_hook_next_id = 0;
  m_suppress_stop_hooks = false;
  m_repl_map.clear();
  Args signal_args;
  ClearDummySignals(signal_args);
}

llvm::StringRef Target::GetABIName() const {
  lldb::ABISP abi_sp;
  if (m_process_sp)
    abi_sp = m_process_sp->GetABI();
  if (!abi_sp)
    abi_sp = ABI::FindPlugin(ProcessSP(), GetArchitecture());
  if (abi_sp)
      return abi_sp->GetPluginName();
  return {};
}

BreakpointList &Target::GetBreakpointList(bool internal) {
  if (internal)
    return m_internal_breakpoint_list;
  else
    return m_breakpoint_list;
}

const BreakpointList &Target::GetBreakpointList(bool internal) const {
  if (internal)
    return m_internal_breakpoint_list;
  else
    return m_breakpoint_list;
}

BreakpointSP Target::GetBreakpointByID(break_id_t break_id) {
  BreakpointSP bp_sp;

  if (LLDB_BREAK_ID_IS_INTERNAL(break_id))
    bp_sp = m_internal_breakpoint_list.FindBreakpointByID(break_id);
  else
    bp_sp = m_breakpoint_list.FindBreakpointByID(break_id);

  return bp_sp;
}

lldb::BreakpointSP
lldb_private::Target::CreateBreakpointAtUserEntry(Status &error) {
  ModuleSP main_module_sp = GetExecutableModule();
  FileSpecList shared_lib_filter;
  shared_lib_filter.Append(main_module_sp->GetFileSpec());
  llvm::SetVector<std::string, std::vector<std::string>,
                  std::unordered_set<std::string>>
      entryPointNamesSet;
  for (LanguageType lang_type : Language::GetSupportedLanguages()) {
    Language *lang = Language::FindPlugin(lang_type);
    if (!lang) {
      error.SetErrorString("Language not found\n");
      return lldb::BreakpointSP();
    }
    std::string entryPointName = lang->GetUserEntryPointName().str();
    if (!entryPointName.empty())
      entryPointNamesSet.insert(entryPointName);
  }
  if (entryPointNamesSet.empty()) {
    error.SetErrorString("No entry point name found\n");
    return lldb::BreakpointSP();
  }
  BreakpointSP bp_sp = CreateBreakpoint(
      &shared_lib_filter,
      /*containingSourceFiles=*/nullptr, entryPointNamesSet.takeVector(),
      /*func_name_type_mask=*/eFunctionNameTypeFull,
      /*language=*/eLanguageTypeUnknown,
      /*offset=*/0,
      /*skip_prologue=*/eLazyBoolNo,
      /*internal=*/false,
      /*hardware=*/false);
  if (!bp_sp) {
    error.SetErrorString("Breakpoint creation failed.\n");
    return lldb::BreakpointSP();
  }
  bp_sp->SetOneShot(true);
  return bp_sp;
}

BreakpointSP Target::CreateSourceRegexBreakpoint(
    const FileSpecList *containingModules,
    const FileSpecList *source_file_spec_list,
    const std::unordered_set<std::string> &function_names,
    RegularExpression source_regex, bool internal, bool hardware,
    LazyBool move_to_nearest_code) {
  SearchFilterSP filter_sp(GetSearchFilterForModuleAndCUList(
      containingModules, source_file_spec_list));
  if (move_to_nearest_code == eLazyBoolCalculate)
    move_to_nearest_code = GetMoveToNearestCode() ? eLazyBoolYes : eLazyBoolNo;
  BreakpointResolverSP resolver_sp(new BreakpointResolverFileRegex(
      nullptr, std::move(source_regex), function_names,
      !static_cast<bool>(move_to_nearest_code)));

  return CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
}

BreakpointSP Target::CreateBreakpoint(const FileSpecList *containingModules,
                                      const FileSpec &file, uint32_t line_no,
                                      uint32_t column, lldb::addr_t offset,
                                      LazyBool check_inlines,
                                      LazyBool skip_prologue, bool internal,
                                      bool hardware,
                                      LazyBool move_to_nearest_code) {
  FileSpec remapped_file;
  std::optional<llvm::StringRef> removed_prefix_opt =
      GetSourcePathMap().ReverseRemapPath(file, remapped_file);
  if (!removed_prefix_opt)
    remapped_file = file;

  if (check_inlines == eLazyBoolCalculate) {
    const InlineStrategy inline_strategy = GetInlineStrategy();
    switch (inline_strategy) {
    case eInlineBreakpointsNever:
      check_inlines = eLazyBoolNo;
      break;

    case eInlineBreakpointsHeaders:
      if (remapped_file.IsSourceImplementationFile())
        check_inlines = eLazyBoolNo;
      else
        check_inlines = eLazyBoolYes;
      break;

    case eInlineBreakpointsAlways:
      check_inlines = eLazyBoolYes;
      break;
    }
  }
  SearchFilterSP filter_sp;
  if (check_inlines == eLazyBoolNo) {
    // Not checking for inlines, we are looking only for matching compile units
    FileSpecList compile_unit_list;
    compile_unit_list.Append(remapped_file);
    filter_sp = GetSearchFilterForModuleAndCUList(containingModules,
                                                  &compile_unit_list);
  } else {
    filter_sp = GetSearchFilterForModuleList(containingModules);
  }
  if (skip_prologue == eLazyBoolCalculate)
    skip_prologue = GetSkipPrologue() ? eLazyBoolYes : eLazyBoolNo;
  if (move_to_nearest_code == eLazyBoolCalculate)
    move_to_nearest_code = GetMoveToNearestCode() ? eLazyBoolYes : eLazyBoolNo;

  SourceLocationSpec location_spec(remapped_file, line_no, column,
                                   check_inlines,
                                   !static_cast<bool>(move_to_nearest_code));
  if (!location_spec)
    return nullptr;

  BreakpointResolverSP resolver_sp(new BreakpointResolverFileLine(
      nullptr, offset, skip_prologue, location_spec, removed_prefix_opt));
  return CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
}

BreakpointSP Target::CreateBreakpoint(lldb::addr_t addr, bool internal,
                                      bool hardware) {
  Address so_addr;

  // Check for any reason we want to move this breakpoint to other address.
  addr = GetBreakableLoadAddress(addr);

  // Attempt to resolve our load address if possible, though it is ok if it
  // doesn't resolve to section/offset.

  // Try and resolve as a load address if possible
  GetSectionLoadList().ResolveLoadAddress(addr, so_addr);
  if (!so_addr.IsValid()) {
    // The address didn't resolve, so just set this as an absolute address
    so_addr.SetOffset(addr);
  }
  BreakpointSP bp_sp(CreateBreakpoint(so_addr, internal, hardware));
  return bp_sp;
}

BreakpointSP Target::CreateBreakpoint(const Address &addr, bool internal,
                                      bool hardware) {
  SearchFilterSP filter_sp(
      new SearchFilterForUnconstrainedSearches(shared_from_this()));
  BreakpointResolverSP resolver_sp(
      new BreakpointResolverAddress(nullptr, addr));
  return CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, false);
}

lldb::BreakpointSP
Target::CreateAddressInModuleBreakpoint(lldb::addr_t file_addr, bool internal,
                                        const FileSpec &file_spec,
                                        bool request_hardware) {
  SearchFilterSP filter_sp(
      new SearchFilterForUnconstrainedSearches(shared_from_this()));
  BreakpointResolverSP resolver_sp(new BreakpointResolverAddress(
      nullptr, file_addr, file_spec));
  return CreateBreakpoint(filter_sp, resolver_sp, internal, request_hardware,
                          false);
}

BreakpointSP Target::CreateBreakpoint(
    const FileSpecList *containingModules,
    const FileSpecList *containingSourceFiles, const char *func_name,
    FunctionNameType func_name_type_mask, LanguageType language,
    lldb::addr_t offset, LazyBool skip_prologue, bool internal, bool hardware) {
  BreakpointSP bp_sp;
  if (func_name) {
    SearchFilterSP filter_sp(GetSearchFilterForModuleAndCUList(
        containingModules, containingSourceFiles));

    if (skip_prologue == eLazyBoolCalculate)
      skip_prologue = GetSkipPrologue() ? eLazyBoolYes : eLazyBoolNo;
    if (language == lldb::eLanguageTypeUnknown)
      language = GetLanguage().AsLanguageType();

    BreakpointResolverSP resolver_sp(new BreakpointResolverName(
        nullptr, func_name, func_name_type_mask, language, Breakpoint::Exact,
        offset, skip_prologue));
    bp_sp = CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
  }
  return bp_sp;
}

lldb::BreakpointSP
Target::CreateBreakpoint(const FileSpecList *containingModules,
                         const FileSpecList *containingSourceFiles,
                         const std::vector<std::string> &func_names,
                         FunctionNameType func_name_type_mask,
                         LanguageType language, lldb::addr_t offset,
                         LazyBool skip_prologue, bool internal, bool hardware) {
  BreakpointSP bp_sp;
  size_t num_names = func_names.size();
  if (num_names > 0) {
    SearchFilterSP filter_sp(GetSearchFilterForModuleAndCUList(
        containingModules, containingSourceFiles));

    if (skip_prologue == eLazyBoolCalculate)
      skip_prologue = GetSkipPrologue() ? eLazyBoolYes : eLazyBoolNo;
    if (language == lldb::eLanguageTypeUnknown)
      language = GetLanguage().AsLanguageType();

    BreakpointResolverSP resolver_sp(
        new BreakpointResolverName(nullptr, func_names, func_name_type_mask,
                                   language, offset, skip_prologue));
    bp_sp = CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
  }
  return bp_sp;
}

BreakpointSP
Target::CreateBreakpoint(const FileSpecList *containingModules,
                         const FileSpecList *containingSourceFiles,
                         const char *func_names[], size_t num_names,
                         FunctionNameType func_name_type_mask,
                         LanguageType language, lldb::addr_t offset,
                         LazyBool skip_prologue, bool internal, bool hardware) {
  BreakpointSP bp_sp;
  if (num_names > 0) {
    SearchFilterSP filter_sp(GetSearchFilterForModuleAndCUList(
        containingModules, containingSourceFiles));

    if (skip_prologue == eLazyBoolCalculate) {
      if (offset == 0)
        skip_prologue = GetSkipPrologue() ? eLazyBoolYes : eLazyBoolNo;
      else
        skip_prologue = eLazyBoolNo;
    }
    if (language == lldb::eLanguageTypeUnknown)
      language = GetLanguage().AsLanguageType();

    BreakpointResolverSP resolver_sp(new BreakpointResolverName(
        nullptr, func_names, num_names, func_name_type_mask, language, offset,
        skip_prologue));
    resolver_sp->SetOffset(offset);
    bp_sp = CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
  }
  return bp_sp;
}

SearchFilterSP
Target::GetSearchFilterForModule(const FileSpec *containingModule) {
  SearchFilterSP filter_sp;
  if (containingModule != nullptr) {
    // TODO: We should look into sharing module based search filters
    // across many breakpoints like we do for the simple target based one
    filter_sp = std::make_shared<SearchFilterByModule>(shared_from_this(),
                                                       *containingModule);
  } else {
    if (!m_search_filter_sp)
      m_search_filter_sp =
          std::make_shared<SearchFilterForUnconstrainedSearches>(
              shared_from_this());
    filter_sp = m_search_filter_sp;
  }
  return filter_sp;
}

SearchFilterSP
Target::GetSearchFilterForModuleList(const FileSpecList *containingModules) {
  SearchFilterSP filter_sp;
  if (containingModules && containingModules->GetSize() != 0) {
    // TODO: We should look into sharing module based search filters
    // across many breakpoints like we do for the simple target based one
    filter_sp = std::make_shared<SearchFilterByModuleList>(shared_from_this(),
                                                           *containingModules);
  } else {
    if (!m_search_filter_sp)
      m_search_filter_sp =
          std::make_shared<SearchFilterForUnconstrainedSearches>(
              shared_from_this());
    filter_sp = m_search_filter_sp;
  }
  return filter_sp;
}

SearchFilterSP Target::GetSearchFilterForModuleAndCUList(
    const FileSpecList *containingModules,
    const FileSpecList *containingSourceFiles) {
  if (containingSourceFiles == nullptr || containingSourceFiles->GetSize() == 0)
    return GetSearchFilterForModuleList(containingModules);

  SearchFilterSP filter_sp;
  if (containingModules == nullptr) {
    // We could make a special "CU List only SearchFilter".  Better yet was if
    // these could be composable, but that will take a little reworking.

    filter_sp = std::make_shared<SearchFilterByModuleListAndCU>(
        shared_from_this(), FileSpecList(), *containingSourceFiles);
  } else {
    filter_sp = std::make_shared<SearchFilterByModuleListAndCU>(
        shared_from_this(), *containingModules, *containingSourceFiles);
  }
  return filter_sp;
}

BreakpointSP Target::CreateFuncRegexBreakpoint(
    const FileSpecList *containingModules,
    const FileSpecList *containingSourceFiles, RegularExpression func_regex,
    lldb::LanguageType requested_language, LazyBool skip_prologue,
    bool internal, bool hardware) {
  SearchFilterSP filter_sp(GetSearchFilterForModuleAndCUList(
      containingModules, containingSourceFiles));
  bool skip = (skip_prologue == eLazyBoolCalculate)
                  ? GetSkipPrologue()
                  : static_cast<bool>(skip_prologue);
  BreakpointResolverSP resolver_sp(new BreakpointResolverName(
      nullptr, std::move(func_regex), requested_language, 0, skip));

  return CreateBreakpoint(filter_sp, resolver_sp, internal, hardware, true);
}

lldb::BreakpointSP
Target::CreateExceptionBreakpoint(enum lldb::LanguageType language,
                                  bool catch_bp, bool throw_bp, bool internal,
                                  Args *additional_args, Status *error) {
  BreakpointSP exc_bkpt_sp = LanguageRuntime::CreateExceptionBreakpoint(
      *this, language, catch_bp, throw_bp, internal);
  if (exc_bkpt_sp && additional_args) {
    BreakpointPreconditionSP precondition_sp = exc_bkpt_sp->GetPrecondition();
    if (precondition_sp && additional_args) {
      if (error)
        *error = precondition_sp->ConfigurePrecondition(*additional_args);
      else
        precondition_sp->ConfigurePrecondition(*additional_args);
    }
  }
  return exc_bkpt_sp;
}

lldb::BreakpointSP Target::CreateScriptedBreakpoint(
    const llvm::StringRef class_name, const FileSpecList *containingModules,
    const FileSpecList *containingSourceFiles, bool internal,
    bool request_hardware, StructuredData::ObjectSP extra_args_sp,
    Status *creation_error) {
  SearchFilterSP filter_sp;

  lldb::SearchDepth depth = lldb::eSearchDepthTarget;
  bool has_files =
      containingSourceFiles && containingSourceFiles->GetSize() > 0;
  bool has_modules = containingModules && containingModules->GetSize() > 0;

  if (has_files && has_modules) {
    filter_sp = GetSearchFilterForModuleAndCUList(containingModules,
                                                  containingSourceFiles);
  } else if (has_files) {
    filter_sp =
        GetSearchFilterForModuleAndCUList(nullptr, containingSourceFiles);
  } else if (has_modules) {
    filter_sp = GetSearchFilterForModuleList(containingModules);
  } else {
    filter_sp = std::make_shared<SearchFilterForUnconstrainedSearches>(
        shared_from_this());
  }

  BreakpointResolverSP resolver_sp(new BreakpointResolverScripted(
      nullptr, class_name, depth, StructuredDataImpl(extra_args_sp)));
  return CreateBreakpoint(filter_sp, resolver_sp, internal, false, true);
}

BreakpointSP Target::CreateBreakpoint(SearchFilterSP &filter_sp,
                                      BreakpointResolverSP &resolver_sp,
                                      bool internal, bool request_hardware,
                                      bool resolve_indirect_symbols) {
  BreakpointSP bp_sp;
  if (filter_sp && resolver_sp) {
    const bool hardware = request_hardware || GetRequireHardwareBreakpoints();
    bp_sp.reset(new Breakpoint(*this, filter_sp, resolver_sp, hardware,
                               resolve_indirect_symbols));
    resolver_sp->SetBreakpoint(bp_sp);
    AddBreakpoint(bp_sp, internal);
  }
  return bp_sp;
}

void Target::AddBreakpoint(lldb::BreakpointSP bp_sp, bool internal) {
  if (!bp_sp)
    return;
  if (internal)
    m_internal_breakpoint_list.Add(bp_sp, false);
  else
    m_breakpoint_list.Add(bp_sp, true);

  Log *log = GetLog(LLDBLog::Breakpoints);
  if (log) {
    StreamString s;
    bp_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
    LLDB_LOGF(log, "Target::%s (internal = %s) => break_id = %s\n",
              __FUNCTION__, bp_sp->IsInternal() ? "yes" : "no", s.GetData());
  }

  bp_sp->ResolveBreakpoint();

  if (!internal) {
    m_last_created_breakpoint = bp_sp;
  }
}

void Target::AddNameToBreakpoint(BreakpointID &id, llvm::StringRef name,
                                 Status &error) {
  BreakpointSP bp_sp =
      m_breakpoint_list.FindBreakpointByID(id.GetBreakpointID());
  if (!bp_sp) {
    StreamString s;
    id.GetDescription(&s, eDescriptionLevelBrief);
    error.SetErrorStringWithFormat("Could not find breakpoint %s", s.GetData());
    return;
  }
  AddNameToBreakpoint(bp_sp, name, error);
}

void Target::AddNameToBreakpoint(BreakpointSP &bp_sp, llvm::StringRef name,
                                 Status &error) {
  if (!bp_sp)
    return;

  BreakpointName *bp_name = FindBreakpointName(ConstString(name), true, error);
  if (!bp_name)
    return;

  bp_name->ConfigureBreakpoint(bp_sp);
  bp_sp->AddName(name);
}

void Target::AddBreakpointName(std::unique_ptr<BreakpointName> bp_name) {
  m_breakpoint_names.insert(
      std::make_pair(bp_name->GetName(), std::move(bp_name)));
}

BreakpointName *Target::FindBreakpointName(ConstString name, bool can_create,
                                           Status &error) {
  BreakpointID::StringIsBreakpointName(name.GetStringRef(), error);
  if (!error.Success())
    return nullptr;

  BreakpointNameList::iterator iter = m_breakpoint_names.find(name);
  if (iter != m_breakpoint_names.end()) {
    return iter->second.get();
  }

  if (!can_create) {
    error.SetErrorStringWithFormat("Breakpoint name \"%s\" doesn't exist and "
                                   "can_create is false.",
                                   name.AsCString());
    return nullptr;
  }

  return m_breakpoint_names
      .insert(std::make_pair(name, std::make_unique<BreakpointName>(name)))
      .first->second.get();
}

void Target::DeleteBreakpointName(ConstString name) {
  BreakpointNameList::iterator iter = m_breakpoint_names.find(name);

  if (iter != m_breakpoint_names.end()) {
    const char *name_cstr = name.AsCString();
    m_breakpoint_names.erase(iter);
    for (auto bp_sp : m_breakpoint_list.Breakpoints())
      bp_sp->RemoveName(name_cstr);
  }
}

void Target::RemoveNameFromBreakpoint(lldb::BreakpointSP &bp_sp,
                                      ConstString name) {
  bp_sp->RemoveName(name.AsCString());
}

void Target::ConfigureBreakpointName(
    BreakpointName &bp_name, const BreakpointOptions &new_options,
    const BreakpointName::Permissions &new_permissions) {
  bp_name.GetOptions().CopyOverSetOptions(new_options);
  bp_name.GetPermissions().MergeInto(new_permissions);
  ApplyNameToBreakpoints(bp_name);
}

void Target::ApplyNameToBreakpoints(BreakpointName &bp_name) {
  llvm::Expected<std::vector<BreakpointSP>> expected_vector =
      m_breakpoint_list.FindBreakpointsByName(bp_name.GetName().AsCString());

  if (!expected_vector) {
    LLDB_LOG(GetLog(LLDBLog::Breakpoints), "invalid breakpoint name: {}",
             llvm::toString(expected_vector.takeError()));
    return;
  }

  for (auto bp_sp : *expected_vector)
    bp_name.ConfigureBreakpoint(bp_sp);
}

void Target::GetBreakpointNames(std::vector<std::string> &names) {
  names.clear();
  for (const auto& bp_name_entry : m_breakpoint_names) {
    names.push_back(bp_name_entry.first.AsCString());
  }
  llvm::sort(names);
}

bool Target::ProcessIsValid() {
  return (m_process_sp && m_process_sp->IsAlive());
}

static bool CheckIfWatchpointsSupported(Target *target, Status &error) {
  std::optional<uint32_t> num_supported_hardware_watchpoints =
      target->GetProcessSP()->GetWatchpointSlotCount();

  // If unable to determine the # of watchpoints available,
  // assume they are supported.
  if (!num_supported_hardware_watchpoints)
    return true;

  if (*num_supported_hardware_watchpoints == 0) {
    error.SetErrorStringWithFormat(
        "Target supports (%u) hardware watchpoint slots.\n",
        *num_supported_hardware_watchpoints);
    return false;
  }
  return true;
}

// See also Watchpoint::SetWatchpointType(uint32_t type) and the
// OptionGroupWatchpoint::WatchType enum type.
WatchpointSP Target::CreateWatchpoint(lldb::addr_t addr, size_t size,
                                      const CompilerType *type, uint32_t kind,
                                      Status &error) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log,
            "Target::%s (addr = 0x%8.8" PRIx64 " size = %" PRIu64
            " type = %u)\n",
            __FUNCTION__, addr, (uint64_t)size, kind);

  WatchpointSP wp_sp;
  if (!ProcessIsValid()) {
    error.SetErrorString("process is not alive");
    return wp_sp;
  }

  if (addr == LLDB_INVALID_ADDRESS || size == 0) {
    if (size == 0)
      error.SetErrorString("cannot set a watchpoint with watch_size of 0");
    else
      error.SetErrorStringWithFormat("invalid watch address: %" PRIu64, addr);
    return wp_sp;
  }

  if (!LLDB_WATCH_TYPE_IS_VALID(kind)) {
    error.SetErrorStringWithFormat("invalid watchpoint type: %d", kind);
  }

  if (!CheckIfWatchpointsSupported(this, error))
    return wp_sp;

  // Currently we only support one watchpoint per address, with total number of
  // watchpoints limited by the hardware which the inferior is running on.

  // Grab the list mutex while doing operations.
  const bool notify = false; // Don't notify about all the state changes we do
                             // on creating the watchpoint.

  // Mask off ignored bits from watchpoint address.
  if (ABISP abi = m_process_sp->GetABI())
    addr = abi->FixDataAddress(addr);

  // LWP_TODO this sequence is looking for an existing watchpoint
  // at the exact same user-specified address, disables the new one
  // if addr/size/type match.  If type/size differ, disable old one.
  // This isn't correct, we need both watchpoints to use a shared
  // WatchpointResource in the target, and expand the WatchpointResource
  // to handle the needs of both Watchpoints.
  // Also, even if the addresses don't match, they may need to be
  // supported by the same WatchpointResource, e.g. a watchpoint
  // watching 1 byte at 0x102 and a watchpoint watching 1 byte at 0x103.
  // They're in the same word and must be watched by a single hardware
  // watchpoint register.

  std::unique_lock<std::recursive_mutex> lock;
  this->GetWatchpointList().GetListMutex(lock);
  WatchpointSP matched_sp = m_watchpoint_list.FindByAddress(addr);
  if (matched_sp) {
    size_t old_size = matched_sp->GetByteSize();
    uint32_t old_type =
        (matched_sp->WatchpointRead() ? LLDB_WATCH_TYPE_READ : 0) |
        (matched_sp->WatchpointWrite() ? LLDB_WATCH_TYPE_WRITE : 0) |
        (matched_sp->WatchpointModify() ? LLDB_WATCH_TYPE_MODIFY : 0);
    // Return the existing watchpoint if both size and type match.
    if (size == old_size && kind == old_type) {
      wp_sp = matched_sp;
      wp_sp->SetEnabled(false, notify);
    } else {
      // Nil the matched watchpoint; we will be creating a new one.
      m_process_sp->DisableWatchpoint(matched_sp, notify);
      m_watchpoint_list.Remove(matched_sp->GetID(), true);
    }
  }

  if (!wp_sp) {
    wp_sp = std::make_shared<Watchpoint>(*this, addr, size, type);
    wp_sp->SetWatchpointType(kind, notify);
    m_watchpoint_list.Add(wp_sp, true);
  }

  error = m_process_sp->EnableWatchpoint(wp_sp, notify);
  LLDB_LOGF(log, "Target::%s (creation of watchpoint %s with id = %u)\n",
            __FUNCTION__, error.Success() ? "succeeded" : "failed",
            wp_sp->GetID());

  if (error.Fail()) {
    // Enabling the watchpoint on the device side failed. Remove the said
    // watchpoint from the list maintained by the target instance.
    m_watchpoint_list.Remove(wp_sp->GetID(), true);
    wp_sp.reset();
  } else
    m_last_created_watchpoint = wp_sp;
  return wp_sp;
}

void Target::RemoveAllowedBreakpoints() {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s \n", __FUNCTION__);

  m_breakpoint_list.RemoveAllowed(true);

  m_last_created_breakpoint.reset();
}

void Target::RemoveAllBreakpoints(bool internal_also) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (internal_also = %s)\n", __FUNCTION__,
            internal_also ? "yes" : "no");

  m_breakpoint_list.RemoveAll(true);
  if (internal_also)
    m_internal_breakpoint_list.RemoveAll(false);

  m_last_created_breakpoint.reset();
}

void Target::DisableAllBreakpoints(bool internal_also) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (internal_also = %s)\n", __FUNCTION__,
            internal_also ? "yes" : "no");

  m_breakpoint_list.SetEnabledAll(false);
  if (internal_also)
    m_internal_breakpoint_list.SetEnabledAll(false);
}

void Target::DisableAllowedBreakpoints() {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s", __FUNCTION__);

  m_breakpoint_list.SetEnabledAllowed(false);
}

void Target::EnableAllBreakpoints(bool internal_also) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (internal_also = %s)\n", __FUNCTION__,
            internal_also ? "yes" : "no");

  m_breakpoint_list.SetEnabledAll(true);
  if (internal_also)
    m_internal_breakpoint_list.SetEnabledAll(true);
}

void Target::EnableAllowedBreakpoints() {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s", __FUNCTION__);

  m_breakpoint_list.SetEnabledAllowed(true);
}

bool Target::RemoveBreakpointByID(break_id_t break_id) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (break_id = %i, internal = %s)\n", __FUNCTION__,
            break_id, LLDB_BREAK_ID_IS_INTERNAL(break_id) ? "yes" : "no");

  if (DisableBreakpointByID(break_id)) {
    if (LLDB_BREAK_ID_IS_INTERNAL(break_id))
      m_internal_breakpoint_list.Remove(break_id, false);
    else {
      if (m_last_created_breakpoint) {
        if (m_last_created_breakpoint->GetID() == break_id)
          m_last_created_breakpoint.reset();
      }
      m_breakpoint_list.Remove(break_id, true);
    }
    return true;
  }
  return false;
}

bool Target::DisableBreakpointByID(break_id_t break_id) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (break_id = %i, internal = %s)\n", __FUNCTION__,
            break_id, LLDB_BREAK_ID_IS_INTERNAL(break_id) ? "yes" : "no");

  BreakpointSP bp_sp;

  if (LLDB_BREAK_ID_IS_INTERNAL(break_id))
    bp_sp = m_internal_breakpoint_list.FindBreakpointByID(break_id);
  else
    bp_sp = m_breakpoint_list.FindBreakpointByID(break_id);
  if (bp_sp) {
    bp_sp->SetEnabled(false);
    return true;
  }
  return false;
}

bool Target::EnableBreakpointByID(break_id_t break_id) {
  Log *log = GetLog(LLDBLog::Breakpoints);
  LLDB_LOGF(log, "Target::%s (break_id = %i, internal = %s)\n", __FUNCTION__,
            break_id, LLDB_BREAK_ID_IS_INTERNAL(break_id) ? "yes" : "no");

  BreakpointSP bp_sp;

  if (LLDB_BREAK_ID_IS_INTERNAL(break_id))
    bp_sp = m_internal_breakpoint_list.FindBreakpointByID(break_id);
  else
    bp_sp = m_breakpoint_list.FindBreakpointByID(break_id);

  if (bp_sp) {
    bp_sp->SetEnabled(true);
    return true;
  }
  return false;
}

void Target::ResetBreakpointHitCounts() {
  GetBreakpointList().ResetHitCounts();
}

Status Target::SerializeBreakpointsToFile(const FileSpec &file,
                                          const BreakpointIDList &bp_ids,
                                          bool append) {
  Status error;

  if (!file) {
    error.SetErrorString("Invalid FileSpec.");
    return error;
  }

  std::string path(file.GetPath());
  StructuredData::ObjectSP input_data_sp;

  StructuredData::ArraySP break_store_sp;
  StructuredData::Array *break_store_ptr = nullptr;

  if (append) {
    input_data_sp = StructuredData::ParseJSONFromFile(file, error);
    if (error.Success()) {
      break_store_ptr = input_data_sp->GetAsArray();
      if (!break_store_ptr) {
        error.SetErrorStringWithFormat(
            "Tried to append to invalid input file %s", path.c_str());
        return error;
      }
    }
  }

  if (!break_store_ptr) {
    break_store_sp = std::make_shared<StructuredData::Array>();
    break_store_ptr = break_store_sp.get();
  }

  StreamFile out_file(path.c_str(),
                      File::eOpenOptionTruncate | File::eOpenOptionWriteOnly |
                          File::eOpenOptionCanCreate |
                          File::eOpenOptionCloseOnExec,
                      lldb::eFilePermissionsFileDefault);
  if (!out_file.GetFile().IsValid()) {
    error.SetErrorStringWithFormat("Unable to open output file: %s.",
                                   path.c_str());
    return error;
  }

  std::unique_lock<std::recursive_mutex> lock;
  GetBreakpointList().GetListMutex(lock);

  if (bp_ids.GetSize() == 0) {
    const BreakpointList &breakpoints = GetBreakpointList();

    size_t num_breakpoints = breakpoints.GetSize();
    for (size_t i = 0; i < num_breakpoints; i++) {
      Breakpoint *bp = breakpoints.GetBreakpointAtIndex(i).get();
      StructuredData::ObjectSP bkpt_save_sp = bp->SerializeToStructuredData();
      // If a breakpoint can't serialize it, just ignore it for now:
      if (bkpt_save_sp)
        break_store_ptr->AddItem(bkpt_save_sp);
    }
  } else {

    std::unordered_set<lldb::break_id_t> processed_bkpts;
    const size_t count = bp_ids.GetSize();
    for (size_t i = 0; i < count; ++i) {
      BreakpointID cur_bp_id = bp_ids.GetBreakpointIDAtIndex(i);
      lldb::break_id_t bp_id = cur_bp_id.GetBreakpointID();

      if (bp_id != LLDB_INVALID_BREAK_ID) {
        // Only do each breakpoint once:
        std::pair<std::unordered_set<lldb::break_id_t>::iterator, bool>
            insert_result = processed_bkpts.insert(bp_id);
        if (!insert_result.second)
          continue;

        Breakpoint *bp = GetBreakpointByID(bp_id).get();
        StructuredData::ObjectSP bkpt_save_sp = bp->SerializeToStructuredData();
        // If the user explicitly asked to serialize a breakpoint, and we
        // can't, then raise an error:
        if (!bkpt_save_sp) {
          error.SetErrorStringWithFormat("Unable to serialize breakpoint %d",
                                         bp_id);
          return error;
        }
        break_store_ptr->AddItem(bkpt_save_sp);
      }
    }
  }

  break_store_ptr->Dump(out_file, false);
  out_file.PutChar('\n');
  return error;
}

Status Target::CreateBreakpointsFromFile(const FileSpec &file,
                                         BreakpointIDList &new_bps) {
  std::vector<std::string> no_names;
  return CreateBreakpointsFromFile(file, no_names, new_bps);
}

Status Target::CreateBreakpointsFromFile(const FileSpec &file,
                                         std::vector<std::string> &names,
                                         BreakpointIDList &new_bps) {
  std::unique_lock<std::recursive_mutex> lock;
  GetBreakpointList().GetListMutex(lock);

  Status error;
  StructuredData::ObjectSP input_data_sp =
      StructuredData::ParseJSONFromFile(file, error);
  if (!error.Success()) {
    return error;
  } else if (!input_data_sp || !input_data_sp->IsValid()) {
    error.SetErrorStringWithFormat("Invalid JSON from input file: %s.",
                                   file.GetPath().c_str());
    return error;
  }

  StructuredData::Array *bkpt_array = input_data_sp->GetAsArray();
  if (!bkpt_array) {
    error.SetErrorStringWithFormat(
        "Invalid breakpoint data from input file: %s.", file.GetPath().c_str());
    return error;
  }

  size_t num_bkpts = bkpt_array->GetSize();
  size_t num_names = names.size();

  for (size_t i = 0; i < num_bkpts; i++) {
    StructuredData::ObjectSP bkpt_object_sp = bkpt_array->GetItemAtIndex(i);
    // Peel off the breakpoint key, and feed the rest to the Breakpoint:
    StructuredData::Dictionary *bkpt_dict = bkpt_object_sp->GetAsDictionary();
    if (!bkpt_dict) {
      error.SetErrorStringWithFormat(
          "Invalid breakpoint data for element %zu from input file: %s.", i,
          file.GetPath().c_str());
      return error;
    }
    StructuredData::ObjectSP bkpt_data_sp =
        bkpt_dict->GetValueForKey(Breakpoint::GetSerializationKey());
    if (num_names &&
        !Breakpoint::SerializedBreakpointMatchesNames(bkpt_data_sp, names))
      continue;

    BreakpointSP bkpt_sp = Breakpoint::CreateFromStructuredData(
        shared_from_this(), bkpt_data_sp, error);
    if (!error.Success()) {
      error.SetErrorStringWithFormat(
          "Error restoring breakpoint %zu from %s: %s.", i,
          file.GetPath().c_str(), error.AsCString());
      return error;
    }
    new_bps.AddBreakpointID(BreakpointID(bkpt_sp->GetID()));
  }
  return error;
}

// The flag 'end_to_end', default to true, signifies that the operation is
// performed end to end, for both the debugger and the debuggee.

// Assumption: Caller holds the list mutex lock for m_watchpoint_list for end
// to end operations.
bool Target::RemoveAllWatchpoints(bool end_to_end) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  if (!end_to_end) {
    m_watchpoint_list.RemoveAll(true);
    return true;
  }

  // Otherwise, it's an end to end operation.

  if (!ProcessIsValid())
    return false;

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    Status rc = m_process_sp->DisableWatchpoint(wp_sp);
    if (rc.Fail())
      return false;
  }
  m_watchpoint_list.RemoveAll(true);
  m_last_created_watchpoint.reset();
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list for end
// to end operations.
bool Target::DisableAllWatchpoints(bool end_to_end) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  if (!end_to_end) {
    m_watchpoint_list.SetEnabledAll(false);
    return true;
  }

  // Otherwise, it's an end to end operation.

  if (!ProcessIsValid())
    return false;

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    Status rc = m_process_sp->DisableWatchpoint(wp_sp);
    if (rc.Fail())
      return false;
  }
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list for end
// to end operations.
bool Target::EnableAllWatchpoints(bool end_to_end) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  if (!end_to_end) {
    m_watchpoint_list.SetEnabledAll(true);
    return true;
  }

  // Otherwise, it's an end to end operation.

  if (!ProcessIsValid())
    return false;

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    Status rc = m_process_sp->EnableWatchpoint(wp_sp);
    if (rc.Fail())
      return false;
  }
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::ClearAllWatchpointHitCounts() {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    wp_sp->ResetHitCount();
  }
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::ClearAllWatchpointHistoricValues() {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    wp_sp->ResetHistoricValues();
  }
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list during
// these operations.
bool Target::IgnoreAllWatchpoints(uint32_t ignore_count) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s\n", __FUNCTION__);

  if (!ProcessIsValid())
    return false;

  for (WatchpointSP wp_sp : m_watchpoint_list.Watchpoints()) {
    if (!wp_sp)
      return false;

    wp_sp->SetIgnoreCount(ignore_count);
  }
  return true; // Success!
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::DisableWatchpointByID(lldb::watch_id_t watch_id) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s (watch_id = %i)\n", __FUNCTION__, watch_id);

  if (!ProcessIsValid())
    return false;

  WatchpointSP wp_sp = m_watchpoint_list.FindByID(watch_id);
  if (wp_sp) {
    Status rc = m_process_sp->DisableWatchpoint(wp_sp);
    if (rc.Success())
      return true;

    // Else, fallthrough.
  }
  return false;
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::EnableWatchpointByID(lldb::watch_id_t watch_id) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s (watch_id = %i)\n", __FUNCTION__, watch_id);

  if (!ProcessIsValid())
    return false;

  WatchpointSP wp_sp = m_watchpoint_list.FindByID(watch_id);
  if (wp_sp) {
    Status rc = m_process_sp->EnableWatchpoint(wp_sp);
    if (rc.Success())
      return true;

    // Else, fallthrough.
  }
  return false;
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::RemoveWatchpointByID(lldb::watch_id_t watch_id) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s (watch_id = %i)\n", __FUNCTION__, watch_id);

  WatchpointSP watch_to_remove_sp = m_watchpoint_list.FindByID(watch_id);
  if (watch_to_remove_sp == m_last_created_watchpoint)
    m_last_created_watchpoint.reset();

  if (DisableWatchpointByID(watch_id)) {
    m_watchpoint_list.Remove(watch_id, true);
    return true;
  }
  return false;
}

// Assumption: Caller holds the list mutex lock for m_watchpoint_list.
bool Target::IgnoreWatchpointByID(lldb::watch_id_t watch_id,
                                  uint32_t ignore_count) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  LLDB_LOGF(log, "Target::%s (watch_id = %i)\n", __FUNCTION__, watch_id);

  if (!ProcessIsValid())
    return false;

  WatchpointSP wp_sp = m_watchpoint_list.FindByID(watch_id);
  if (wp_sp) {
    wp_sp->SetIgnoreCount(ignore_count);
    return true;
  }
  return false;
}

ModuleSP Target::GetExecutableModule() {
  // search for the first executable in the module list
  for (size_t i = 0; i < m_images.GetSize(); ++i) {
    ModuleSP module_sp = m_images.GetModuleAtIndex(i);
    lldb_private::ObjectFile *obj = module_sp->GetObjectFile();
    if (obj == nullptr)
      continue;
    if (obj->GetType() == ObjectFile::Type::eTypeExecutable)
      return module_sp;
  }
  // as fall back return the first module loaded
  return m_images.GetModuleAtIndex(0);
}

Module *Target::GetExecutableModulePointer() {
  return GetExecutableModule().get();
}

static void LoadScriptingResourceForModule(const ModuleSP &module_sp,
                                           Target *target) {
  Status error;
  StreamString feedback_stream;
  if (module_sp && !module_sp->LoadScriptingResourceInTarget(target, error,
                                                             feedback_stream)) {
    if (error.AsCString())
      target->GetDebugger().GetErrorStream().Printf(
          "unable to load scripting data for module %s - error reported was "
          "%s\n",
          module_sp->GetFileSpec().GetFileNameStrippingExtension().GetCString(),
          error.AsCString());
  }
  if (feedback_stream.GetSize())
    target->GetDebugger().GetErrorStream().Printf("%s\n",
                                                  feedback_stream.GetData());
}

void Target::ClearModules(bool delete_locations) {
  ModulesDidUnload(m_images, delete_locations);
  m_section_load_history.Clear();
  m_images.Clear();
  m_scratch_type_system_map.Clear();
}

void Target::DidExec() {
  // When a process exec's we need to know about it so we can do some cleanup.
  m_breakpoint_list.RemoveInvalidLocations(m_arch.GetSpec());
  m_internal_breakpoint_list.RemoveInvalidLocations(m_arch.GetSpec());
}

void Target::SetExecutableModule(ModuleSP &executable_sp,
                                 LoadDependentFiles load_dependent_files) {
  Log *log = GetLog(LLDBLog::Target);
  ClearModules(false);

  if (executable_sp) {
    ElapsedTime elapsed(m_stats.GetCreateTime());
    LLDB_SCOPED_TIMERF("Target::SetExecutableModule (executable = '%s')",
                       executable_sp->GetFileSpec().GetPath().c_str());

    const bool notify = true;
    m_images.Append(executable_sp,
                    notify); // The first image is our executable file

    // If we haven't set an architecture yet, reset our architecture based on
    // what we found in the executable module.
    if (!m_arch.GetSpec().IsValid()) {
      m_arch = executable_sp->GetArchitecture();
      LLDB_LOG(log,
               "Target::SetExecutableModule setting architecture to {0} ({1}) "
               "based on executable file",
               m_arch.GetSpec().GetArchitectureName(),
               m_arch.GetSpec().GetTriple().getTriple());
    }

    FileSpecList dependent_files;
    ObjectFile *executable_objfile = executable_sp->GetObjectFile();
    bool load_dependents = true;
    switch (load_dependent_files) {
    case eLoadDependentsDefault:
      load_dependents = executable_sp->IsExecutable();
      break;
    case eLoadDependentsYes:
      load_dependents = true;
      break;
    case eLoadDependentsNo:
      load_dependents = false;
      break;
    }

    if (executable_objfile && load_dependents) {
      ModuleList added_modules;
      executable_objfile->GetDependentModules(dependent_files);
      for (uint32_t i = 0; i < dependent_files.GetSize(); i++) {
        FileSpec dependent_file_spec(dependent_files.GetFileSpecAtIndex(i));
        FileSpec platform_dependent_file_spec;
        if (m_platform_sp)
          m_platform_sp->GetFileWithUUID(dependent_file_spec, nullptr,
                                         platform_dependent_file_spec);
        else
          platform_dependent_file_spec = dependent_file_spec;

        ModuleSpec module_spec(platform_dependent_file_spec, m_arch.GetSpec());
        ModuleSP image_module_sp(
            GetOrCreateModule(module_spec, false /* notify */));
        if (image_module_sp) {
          added_modules.AppendIfNeeded(image_module_sp, false);
          ObjectFile *objfile = image_module_sp->GetObjectFile();
          if (objfile)
            objfile->GetDependentModules(dependent_files);
        }
      }
      ModulesDidLoad(added_modules);
    }
  }
}

bool Target::SetArchitecture(const ArchSpec &arch_spec, bool set_platform,
                             bool merge) {
  Log *log = GetLog(LLDBLog::Target);
  bool missing_local_arch = !m_arch.GetSpec().IsValid();
  bool replace_local_arch = true;
  bool compatible_local_arch = false;
  ArchSpec other(arch_spec);

  // Changing the architecture might mean that the currently selected platform
  // isn't compatible. Set the platform correctly if we are asked to do so,
  // otherwise assume the user will set the platform manually.
  if (set_platform) {
    if (other.IsValid()) {
      auto platform_sp = GetPlatform();
      if (!platform_sp || !platform_sp->IsCompatibleArchitecture(
                              other, {}, ArchSpec::CompatibleMatch, nullptr)) {
        ArchSpec platform_arch;
        if (PlatformSP arch_platform_sp =
                GetDebugger().GetPlatformList().GetOrCreate(other, {},
                                                            &platform_arch)) {
          SetPlatform(arch_platform_sp);
          if (platform_arch.IsValid())
            other = platform_arch;
        }
      }
    }
  }

  if (!missing_local_arch) {
    if (merge && m_arch.GetSpec().IsCompatibleMatch(arch_spec)) {
      other.MergeFrom(m_arch.GetSpec());

      if (m_arch.GetSpec().IsCompatibleMatch(other)) {
        compatible_local_arch = true;

        if (m_arch.GetSpec().GetTriple() == other.GetTriple())
          replace_local_arch = false;
      }
    }
  }

  if (compatible_local_arch || missing_local_arch) {
    // If we haven't got a valid arch spec, or the architectures are compatible
    // update the architecture, unless the one we already have is more
    // specified
    if (replace_local_arch)
      m_arch = other;
    LLDB_LOG(log,
             "Target::SetArchitecture merging compatible arch; arch "
             "is now {0} ({1})",
             m_arch.GetSpec().GetArchitectureName(),
             m_arch.GetSpec().GetTriple().getTriple());
    return true;
  }

  // If we have an executable file, try to reset the executable to the desired
  // architecture
  LLDB_LOGF(
      log,
      "Target::SetArchitecture changing architecture to %s (%s) from %s (%s)",
      arch_spec.GetArchitectureName(),
      arch_spec.GetTriple().getTriple().c_str(),
      m_arch.GetSpec().GetArchitectureName(),
      m_arch.GetSpec().GetTriple().getTriple().c_str());
  m_arch = other;
  ModuleSP executable_sp = GetExecutableModule();

  ClearModules(true);
  // Need to do something about unsetting breakpoints.

  if (executable_sp) {
    LLDB_LOGF(log,
              "Target::SetArchitecture Trying to select executable file "
              "architecture %s (%s)",
              arch_spec.GetArchitectureName(),
              arch_spec.GetTriple().getTriple().c_str());
    ModuleSpec module_spec(executable_sp->GetFileSpec(), other);
    FileSpecList search_paths = GetExecutableSearchPaths();
    Status error = ModuleList::GetSharedModule(module_spec, executable_sp,
                                               &search_paths, nullptr, nullptr);

    if (!error.Fail() && executable_sp) {
      SetExecutableModule(executable_sp, eLoadDependentsYes);
      return true;
    }
  }
  return false;
}

bool Target::MergeArchitecture(const ArchSpec &arch_spec) {
  Log *log = GetLog(LLDBLog::Target);
  if (arch_spec.IsValid()) {
    if (m_arch.GetSpec().IsCompatibleMatch(arch_spec)) {
      // The current target arch is compatible with "arch_spec", see if we can
      // improve our current architecture using bits from "arch_spec"

      LLDB_LOGF(log,
                "Target::MergeArchitecture target has arch %s, merging with "
                "arch %s",
                m_arch.GetSpec().GetTriple().getTriple().c_str(),
                arch_spec.GetTriple().getTriple().c_str());

      // Merge bits from arch_spec into "merged_arch" and set our architecture
      ArchSpec merged_arch(m_arch.GetSpec());
      merged_arch.MergeFrom(arch_spec);
      return SetArchitecture(merged_arch);
    } else {
      // The new architecture is different, we just need to replace it
      return SetArchitecture(arch_spec);
    }
  }
  return false;
}

void Target::NotifyWillClearList(const ModuleList &module_list) {}

void Target::NotifyModuleAdded(const ModuleList &module_list,
                               const ModuleSP &module_sp) {
  // A module is being added to this target for the first time
  if (m_valid) {
    ModuleList my_module_list;
    my_module_list.Append(module_sp);
    ModulesDidLoad(my_module_list);
  }
}

void Target::NotifyModuleRemoved(const ModuleList &module_list,
                                 const ModuleSP &module_sp) {
  // A module is being removed from this target.
  if (m_valid) {
    ModuleList my_module_list;
    my_module_list.Append(module_sp);
    ModulesDidUnload(my_module_list, false);
  }
}

void Target::NotifyModuleUpdated(const ModuleList &module_list,
                                 const ModuleSP &old_module_sp,
                                 const ModuleSP &new_module_sp) {
  // A module is replacing an already added module
  if (m_valid) {
    m_breakpoint_list.UpdateBreakpointsWhenModuleIsReplaced(old_module_sp,
                                                            new_module_sp);
    m_internal_breakpoint_list.UpdateBreakpointsWhenModuleIsReplaced(
        old_module_sp, new_module_sp);
  }
}

void Target::NotifyModulesRemoved(lldb_private::ModuleList &module_list) {
  ModulesDidUnload(module_list, false);
}

void Target::ModulesDidLoad(ModuleList &module_list) {
  const size_t num_images = module_list.GetSize();
  if (m_valid && num_images) {
    for (size_t idx = 0; idx < num_images; ++idx) {
      ModuleSP module_sp(module_list.GetModuleAtIndex(idx));
      LoadScriptingResourceForModule(module_sp, this);
    }
    m_breakpoint_list.UpdateBreakpoints(module_list, true, false);
    m_internal_breakpoint_list.UpdateBreakpoints(module_list, true, false);
    if (m_process_sp) {
      m_process_sp->ModulesDidLoad(module_list);
    }
    auto data_sp =
        std::make_shared<TargetEventData>(shared_from_this(), module_list);
    BroadcastEvent(eBroadcastBitModulesLoaded, data_sp);
  }
}

void Target::SymbolsDidLoad(ModuleList &module_list) {
  if (m_valid && module_list.GetSize()) {
    if (m_process_sp) {
      for (LanguageRuntime *runtime : m_process_sp->GetLanguageRuntimes()) {
        runtime->SymbolsDidLoad(module_list);
      }
    }

    m_breakpoint_list.UpdateBreakpoints(module_list, true, false);
    m_internal_breakpoint_list.UpdateBreakpoints(module_list, true, false);
    auto data_sp =
        std::make_shared<TargetEventData>(shared_from_this(), module_list);
    BroadcastEvent(eBroadcastBitSymbolsLoaded, data_sp);
  }
}

void Target::ModulesDidUnload(ModuleList &module_list, bool delete_locations) {
  if (m_valid && module_list.GetSize()) {
    UnloadModuleSections(module_list);
    auto data_sp =
        std::make_shared<TargetEventData>(shared_from_this(), module_list);
    BroadcastEvent(eBroadcastBitModulesUnloaded, data_sp);
    m_breakpoint_list.UpdateBreakpoints(module_list, false, delete_locations);
    m_internal_breakpoint_list.UpdateBreakpoints(module_list, false,
                                                 delete_locations);

    // If a module was torn down it will have torn down the 'TypeSystemClang's
    // that we used as source 'ASTContext's for the persistent variables in
    // the current target. Those would now be unsafe to access because the
    // 'DeclOrigin' are now possibly stale. Thus clear all persistent
    // variables. We only want to flush 'TypeSystem's if the module being
    // unloaded was capable of describing a source type. JITted module unloads
    // happen frequently for Objective-C utility functions or the REPL and rely
    // on the persistent variables to stick around.
    const bool should_flush_type_systems =
        module_list.AnyOf([](lldb_private::Module &module) {
          auto *object_file = module.GetObjectFile();

          if (!object_file)
            return false;

          auto type = object_file->GetType();

          // eTypeExecutable: when debugged binary was rebuilt
          // eTypeSharedLibrary: if dylib was re-loaded
          return module.FileHasChanged() &&
                 (type == ObjectFile::eTypeObjectFile ||
                  type == ObjectFile::eTypeExecutable ||
                  type == ObjectFile::eTypeSharedLibrary);
        });

    if (should_flush_type_systems)
      m_scratch_type_system_map.Clear();
  }
}

bool Target::ModuleIsExcludedForUnconstrainedSearches(
    const FileSpec &module_file_spec) {
  if (GetBreakpointsConsultPlatformAvoidList()) {
    ModuleList matchingModules;
    ModuleSpec module_spec(module_file_spec);
    GetImages().FindModules(module_spec, matchingModules);
    size_t num_modules = matchingModules.GetSize();

    // If there is more than one module for this file spec, only
    // return true if ALL the modules are on the black list.
    if (num_modules > 0) {
      for (size_t i = 0; i < num_modules; i++) {
        if (!ModuleIsExcludedForUnconstrainedSearches(
                matchingModules.GetModuleAtIndex(i)))
          return false;
      }
      return true;
    }
  }
  return false;
}

bool Target::ModuleIsExcludedForUnconstrainedSearches(
    const lldb::ModuleSP &module_sp) {
  if (GetBreakpointsConsultPlatformAvoidList()) {
    if (m_platform_sp)
      return m_platform_sp->ModuleIsExcludedForUnconstrainedSearches(*this,
                                                                     module_sp);
  }
  return false;
}

size_t Target::ReadMemoryFromFileCache(const Address &addr, void *dst,
                                       size_t dst_len, Status &error) {
  SectionSP section_sp(addr.GetSection());
  if (section_sp) {
    // If the contents of this section are encrypted, the on-disk file is
    // unusable.  Read only from live memory.
    if (section_sp->IsEncrypted()) {
      error.SetErrorString("section is encrypted");
      return 0;
    }
    ModuleSP module_sp(section_sp->GetModule());
    if (module_sp) {
      ObjectFile *objfile = section_sp->GetModule()->GetObjectFile();
      if (objfile) {
        size_t bytes_read = objfile->ReadSectionData(
            section_sp.get(), addr.GetOffset(), dst, dst_len);
        if (bytes_read > 0)
          return bytes_read;
        else
          error.SetErrorStringWithFormat("error reading data from section %s",
                                         section_sp->GetName().GetCString());
      } else
        error.SetErrorString("address isn't from a object file");
    } else
      error.SetErrorString("address isn't in a module");
  } else
    error.SetErrorString("address doesn't contain a section that points to a "
                         "section in a object file");

  return 0;
}

size_t Target::ReadMemory(const Address &addr, void *dst, size_t dst_len,
                          Status &error, bool force_live_memory,
                          lldb::addr_t *load_addr_ptr) {
  error.Clear();

  Address fixed_addr = addr;
  if (ProcessIsValid())
    if (const ABISP &abi = m_process_sp->GetABI())
      fixed_addr.SetLoadAddress(abi->FixAnyAddress(addr.GetLoadAddress(this)),
                                this);

  // if we end up reading this from process memory, we will fill this with the
  // actual load address
  if (load_addr_ptr)
    *load_addr_ptr = LLDB_INVALID_ADDRESS;

  size_t bytes_read = 0;

  addr_t load_addr = LLDB_INVALID_ADDRESS;
  addr_t file_addr = LLDB_INVALID_ADDRESS;
  Address resolved_addr;
  if (!fixed_addr.IsSectionOffset()) {
    SectionLoadList &section_load_list = GetSectionLoadList();
    if (section_load_list.IsEmpty()) {
      // No sections are loaded, so we must assume we are not running yet and
      // anything we are given is a file address.
      file_addr =
          fixed_addr.GetOffset(); // "fixed_addr" doesn't have a section, so
                                  // its offset is the file address
      m_images.ResolveFileAddress(file_addr, resolved_addr);
    } else {
      // We have at least one section loaded. This can be because we have
      // manually loaded some sections with "target modules load ..." or
      // because we have a live process that has sections loaded through
      // the dynamic loader
      load_addr =
          fixed_addr.GetOffset(); // "fixed_addr" doesn't have a section, so
                                  // its offset is the load address
      section_load_list.ResolveLoadAddress(load_addr, resolved_addr);
    }
  }
  if (!resolved_addr.IsValid())
    resolved_addr = fixed_addr;

  // If we read from the file cache but can't get as many bytes as requested,
  // we keep the result around in this buffer, in case this result is the
  // best we can do.
  std::unique_ptr<uint8_t[]> file_cache_read_buffer;
  size_t file_cache_bytes_read = 0;

  // Read from file cache if read-only section.
  if (!force_live_memory && resolved_addr.IsSectionOffset()) {
    SectionSP section_sp(resolved_addr.GetSection());
    if (section_sp) {
      auto permissions = Flags(section_sp->GetPermissions());
      bool is_readonly = !permissions.Test(ePermissionsWritable) &&
                         permissions.Test(ePermissionsReadable);
      if (is_readonly) {
        file_cache_bytes_read =
            ReadMemoryFromFileCache(resolved_addr, dst, dst_len, error);
        if (file_cache_bytes_read == dst_len)
          return file_cache_bytes_read;
        else if (file_cache_bytes_read > 0) {
          file_cache_read_buffer =
              std::make_unique<uint8_t[]>(file_cache_bytes_read);
          std::memcpy(file_cache_read_buffer.get(), dst, file_cache_bytes_read);
        }
      }
    }
  }

  if (ProcessIsValid()) {
    if (load_addr == LLDB_INVALID_ADDRESS)
      load_addr = resolved_addr.GetLoadAddress(this);

    if (load_addr == LLDB_INVALID_ADDRESS) {
      ModuleSP addr_module_sp(resolved_addr.GetModule());
      if (addr_module_sp && addr_module_sp->GetFileSpec())
        error.SetErrorStringWithFormatv(
            "{0:F}[{1:x+}] can't be resolved, {0:F} is not currently loaded",
            addr_module_sp->GetFileSpec(), resolved_addr.GetFileAddress());
      else
        error.SetErrorStringWithFormat("0x%" PRIx64 " can't be resolved",
                                       resolved_addr.GetFileAddress());
    } else {
      bytes_read = m_process_sp->ReadMemory(load_addr, dst, dst_len, error);
      if (bytes_read != dst_len) {
        if (error.Success()) {
          if (bytes_read == 0)
            error.SetErrorStringWithFormat(
                "read memory from 0x%" PRIx64 " failed", load_addr);
          else
            error.SetErrorStringWithFormat(
                "only %" PRIu64 " of %" PRIu64
                " bytes were read from memory at 0x%" PRIx64,
                (uint64_t)bytes_read, (uint64_t)dst_len, load_addr);
        }
      }
      if (bytes_read) {
        if (load_addr_ptr)
          *load_addr_ptr = load_addr;
        return bytes_read;
      }
    }
  }

  if (file_cache_read_buffer && file_cache_bytes_read > 0) {
    // Reading from the process failed. If we've previously succeeded in reading
    // something from the file cache, then copy that over and return that.
    std::memcpy(dst, file_cache_read_buffer.get(), file_cache_bytes_read);
    return file_cache_bytes_read;
  }

  if (!file_cache_read_buffer && resolved_addr.IsSectionOffset()) {
    // If we didn't already try and read from the object file cache, then try
    // it after failing to read from the process.
    return ReadMemoryFromFileCache(resolved_addr, dst, dst_len, error);
  }
  return 0;
}

size_t Target::ReadCStringFromMemory(const Address &addr, std::string &out_str,
                                     Status &error, bool force_live_memory) {
  char buf[256];
  out_str.clear();
  addr_t curr_addr = addr.GetLoadAddress(this);
  Address address(addr);
  while (true) {
    size_t length = ReadCStringFromMemory(address, buf, sizeof(buf), error,
                                          force_live_memory);
    if (length == 0)
      break;
    out_str.append(buf, length);
    // If we got "length - 1" bytes, we didn't get the whole C string, we need
    // to read some more characters
    if (length == sizeof(buf) - 1)
      curr_addr += length;
    else
      break;
    address = Address(curr_addr);
  }
  return out_str.size();
}

size_t Target::ReadCStringFromMemory(const Address &addr, char *dst,
                                     size_t dst_max_len, Status &result_error,
                                     bool force_live_memory) {
  size_t total_cstr_len = 0;
  if (dst && dst_max_len) {
    result_error.Clear();
    // NULL out everything just to be safe
    memset(dst, 0, dst_max_len);
    Status error;
    addr_t curr_addr = addr.GetLoadAddress(this);
    Address address(addr);

    // We could call m_process_sp->GetMemoryCacheLineSize() but I don't think
    // this really needs to be tied to the memory cache subsystem's cache line
    // size, so leave this as a fixed constant.
    const size_t cache_line_size = 512;

    size_t bytes_left = dst_max_len - 1;
    char *curr_dst = dst;

    while (bytes_left > 0) {
      addr_t cache_line_bytes_left =
          cache_line_size - (curr_addr % cache_line_size);
      addr_t bytes_to_read =
          std::min<addr_t>(bytes_left, cache_line_bytes_left);
      size_t bytes_read = ReadMemory(address, curr_dst, bytes_to_read, error,
                                     force_live_memory);

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
      address = Address(curr_addr);
    }
  } else {
    if (dst == nullptr)
      result_error.SetErrorString("invalid arguments");
    else
      result_error.Clear();
  }
  return total_cstr_len;
}

addr_t Target::GetReasonableReadSize(const Address &addr) {
  addr_t load_addr = addr.GetLoadAddress(this);
  if (load_addr != LLDB_INVALID_ADDRESS && m_process_sp) {
    // Avoid crossing cache line boundaries.
    addr_t cache_line_size = m_process_sp->GetMemoryCacheLineSize();
    return cache_line_size - (load_addr % cache_line_size);
  }

  // The read is going to go to the file cache, so we can just pick a largish
  // value.
  return 0x1000;
}

size_t Target::ReadStringFromMemory(const Address &addr, char *dst,
                                    size_t max_bytes, Status &error,
                                    size_t type_width, bool force_live_memory) {
  if (!dst || !max_bytes || !type_width || max_bytes < type_width)
    return 0;

  size_t total_bytes_read = 0;

  // Ensure a null terminator independent of the number of bytes that is
  // read.
  memset(dst, 0, max_bytes);
  size_t bytes_left = max_bytes - type_width;

  const char terminator[4] = {'\0', '\0', '\0', '\0'};
  assert(sizeof(terminator) >= type_width && "Attempting to validate a "
                                             "string with more than 4 bytes "
                                             "per character!");

  Address address = addr;
  char *curr_dst = dst;

  error.Clear();
  while (bytes_left > 0 && error.Success()) {
    addr_t bytes_to_read =
        std::min<addr_t>(bytes_left, GetReasonableReadSize(address));
    size_t bytes_read =
        ReadMemory(address, curr_dst, bytes_to_read, error, force_live_memory);

    if (bytes_read == 0)
      break;

    // Search for a null terminator of correct size and alignment in
    // bytes_read
    size_t aligned_start = total_bytes_read - total_bytes_read % type_width;
    for (size_t i = aligned_start;
         i + type_width <= total_bytes_read + bytes_read; i += type_width)
      if (::memcmp(&dst[i], terminator, type_width) == 0) {
        error.Clear();
        return i;
      }

    total_bytes_read += bytes_read;
    curr_dst += bytes_read;
    address.Slide(bytes_read);
    bytes_left -= bytes_read;
  }
  return total_bytes_read;
}

size_t Target::ReadScalarIntegerFromMemory(const Address &addr, uint32_t byte_size,
                                           bool is_signed, Scalar &scalar,
                                           Status &error,
                                           bool force_live_memory) {
  uint64_t uval;

  if (byte_size <= sizeof(uval)) {
    size_t bytes_read =
        ReadMemory(addr, &uval, byte_size, error, force_live_memory);
    if (bytes_read == byte_size) {
      DataExtractor data(&uval, sizeof(uval), m_arch.GetSpec().GetByteOrder(),
                         m_arch.GetSpec().GetAddressByteSize());
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

uint64_t Target::ReadUnsignedIntegerFromMemory(const Address &addr,
                                               size_t integer_byte_size,
                                               uint64_t fail_value, Status &error,
                                               bool force_live_memory) {
  Scalar scalar;
  if (ReadScalarIntegerFromMemory(addr, integer_byte_size, false, scalar, error,
                                  force_live_memory))
    return scalar.ULongLong(fail_value);
  return fail_value;
}

bool Target::ReadPointerFromMemory(const Address &addr, Status &error,
                                   Address &pointer_addr,
                                   bool force_live_memory) {
  Scalar scalar;
  if (ReadScalarIntegerFromMemory(addr, m_arch.GetSpec().GetAddressByteSize(),
                                  false, scalar, error, force_live_memory)) {
    addr_t pointer_vm_addr = scalar.ULongLong(LLDB_INVALID_ADDRESS);
    if (pointer_vm_addr != LLDB_INVALID_ADDRESS) {
      SectionLoadList &section_load_list = GetSectionLoadList();
      if (section_load_list.IsEmpty()) {
        // No sections are loaded, so we must assume we are not running yet and
        // anything we are given is a file address.
        m_images.ResolveFileAddress(pointer_vm_addr, pointer_addr);
      } else {
        // We have at least one section loaded. This can be because we have
        // manually loaded some sections with "target modules load ..." or
        // because we have a live process that has sections loaded through
        // the dynamic loader
        section_load_list.ResolveLoadAddress(pointer_vm_addr, pointer_addr);
      }
      // We weren't able to resolve the pointer value, so just return an
      // address with no section
      if (!pointer_addr.IsValid())
        pointer_addr.SetOffset(pointer_vm_addr);
      return true;
    }
  }
  return false;
}

ModuleSP Target::GetOrCreateModule(const ModuleSpec &module_spec, bool notify,
                                   Status *error_ptr) {
  ModuleSP module_sp;

  Status error;

  // First see if we already have this module in our module list.  If we do,
  // then we're done, we don't need to consult the shared modules list.  But
  // only do this if we are passed a UUID.

  if (module_spec.GetUUID().IsValid())
    module_sp = m_images.FindFirstModule(module_spec);

  if (!module_sp) {
    llvm::SmallVector<ModuleSP, 1>
        old_modules; // This will get filled in if we have a new version
                     // of the library
    bool did_create_module = false;
    FileSpecList search_paths = GetExecutableSearchPaths();
    FileSpec symbol_file_spec;

    // Call locate module callback if set. This allows users to implement their
    // own module cache system. For example, to leverage build system artifacts,
    // to bypass pulling files from remote platform, or to search symbol files
    // from symbol servers.
    if (m_platform_sp)
      m_platform_sp->CallLocateModuleCallbackIfSet(
          module_spec, module_sp, symbol_file_spec, &did_create_module);

    // The result of this CallLocateModuleCallbackIfSet is one of the following.
    // 1. module_sp:loaded, symbol_file_spec:set
    //      The callback found a module file and a symbol file for the
    //      module_spec. We will call module_sp->SetSymbolFileFileSpec with
    //      the symbol_file_spec later.
    // 2. module_sp:loaded, symbol_file_spec:empty
    //      The callback only found a module file for the module_spec.
    // 3. module_sp:empty, symbol_file_spec:set
    //      The callback only found a symbol file for the module. We continue
    //      to find a module file for this module_spec and we will call
    //      module_sp->SetSymbolFileFileSpec with the symbol_file_spec later.
    // 4. module_sp:empty, symbol_file_spec:empty
    //      Platform does not exist, the callback is not set, the callback did
    //      not find any module files nor any symbol files, the callback failed,
    //      or something went wrong. We continue to find a module file for this
    //      module_spec.

    if (!module_sp) {
      // If there are image search path entries, try to use them to acquire a
      // suitable image.
      if (m_image_search_paths.GetSize()) {
        ModuleSpec transformed_spec(module_spec);
        ConstString transformed_dir;
        if (m_image_search_paths.RemapPath(
                module_spec.GetFileSpec().GetDirectory(), transformed_dir)) {
          transformed_spec.GetFileSpec().SetDirectory(transformed_dir);
          transformed_spec.GetFileSpec().SetFilename(
                module_spec.GetFileSpec().GetFilename());
          error = ModuleList::GetSharedModule(transformed_spec, module_sp,
                                              &search_paths, &old_modules,
                                              &did_create_module);
        }
      }
    }

    if (!module_sp) {
      // If we have a UUID, we can check our global shared module list in case
      // we already have it. If we don't have a valid UUID, then we can't since
      // the path in "module_spec" will be a platform path, and we will need to
      // let the platform find that file. For example, we could be asking for
      // "/usr/lib/dyld" and if we do not have a UUID, we don't want to pick
      // the local copy of "/usr/lib/dyld" since our platform could be a remote
      // platform that has its own "/usr/lib/dyld" in an SDK or in a local file
      // cache.
      if (module_spec.GetUUID().IsValid()) {
        // We have a UUID, it is OK to check the global module list...
        error =
            ModuleList::GetSharedModule(module_spec, module_sp, &search_paths,
                                        &old_modules, &did_create_module);
      }

      if (!module_sp) {
        // The platform is responsible for finding and caching an appropriate
        // module in the shared module cache.
        if (m_platform_sp) {
          error = m_platform_sp->GetSharedModule(
              module_spec, m_process_sp.get(), module_sp, &search_paths,
              &old_modules, &did_create_module);
        } else {
          error.SetErrorString("no platform is currently set");
        }
      }
    }

    // We found a module that wasn't in our target list.  Let's make sure that
    // there wasn't an equivalent module in the list already, and if there was,
    // let's remove it.
    if (module_sp) {
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile) {
        switch (objfile->GetType()) {
        case ObjectFile::eTypeCoreFile: /// A core file that has a checkpoint of
                                        /// a program's execution state
        case ObjectFile::eTypeExecutable:    /// A normal executable
        case ObjectFile::eTypeDynamicLinker: /// The platform's dynamic linker
                                             /// executable
        case ObjectFile::eTypeObjectFile:    /// An intermediate object file
        case ObjectFile::eTypeSharedLibrary: /// A shared library that can be
                                             /// used during execution
          break;
        case ObjectFile::eTypeDebugInfo: /// An object file that contains only
                                         /// debug information
          if (error_ptr)
            error_ptr->SetErrorString("debug info files aren't valid target "
                                      "modules, please specify an executable");
          return ModuleSP();
        case ObjectFile::eTypeStubLibrary: /// A library that can be linked
                                           /// against but not used for
                                           /// execution
          if (error_ptr)
            error_ptr->SetErrorString("stub libraries aren't valid target "
                                      "modules, please specify an executable");
          return ModuleSP();
        default:
          if (error_ptr)
            error_ptr->SetErrorString(
                "unsupported file type, please specify an executable");
          return ModuleSP();
        }
        // GetSharedModule is not guaranteed to find the old shared module, for
        // instance in the common case where you pass in the UUID, it is only
        // going to find the one module matching the UUID.  In fact, it has no
        // good way to know what the "old module" relevant to this target is,
        // since there might be many copies of a module with this file spec in
        // various running debug sessions, but only one of them will belong to
        // this target. So let's remove the UUID from the module list, and look
        // in the target's module list. Only do this if there is SOMETHING else
        // in the module spec...
        if (module_spec.GetUUID().IsValid() &&
            !module_spec.GetFileSpec().GetFilename().IsEmpty() &&
            !module_spec.GetFileSpec().GetDirectory().IsEmpty()) {
          ModuleSpec module_spec_copy(module_spec.GetFileSpec());
          module_spec_copy.GetUUID().Clear();

          ModuleList found_modules;
          m_images.FindModules(module_spec_copy, found_modules);
          found_modules.ForEach([&](const ModuleSP &found_module) -> bool {
            old_modules.push_back(found_module);
            return true;
          });
        }

        // If the locate module callback had found a symbol file, set it to the
        // module_sp before preloading symbols.
        if (symbol_file_spec)
          module_sp->SetSymbolFileFileSpec(symbol_file_spec);

        // Preload symbols outside of any lock, so hopefully we can do this for
        // each library in parallel.
        if (GetPreloadSymbols())
          module_sp->PreloadSymbols();
        llvm::SmallVector<ModuleSP, 1> replaced_modules;
        for (ModuleSP &old_module_sp : old_modules) {
          if (m_images.GetIndexForModule(old_module_sp.get()) !=
              LLDB_INVALID_INDEX32) {
            if (replaced_modules.empty())
              m_images.ReplaceModule(old_module_sp, module_sp);
            else
              m_images.Remove(old_module_sp);

            replaced_modules.push_back(std::move(old_module_sp));
          }
        }

        if (replaced_modules.size() > 1) {
          // The same new module replaced multiple old modules
          // simultaneously.  It's not clear this should ever
          // happen (if we always replace old modules as we add
          // new ones, presumably we should never have more than
          // one old one).  If there are legitimate cases where
          // this happens, then the ModuleList::Notifier interface
          // may need to be adjusted to allow reporting this.
          // In the meantime, just log that this has happened; just
          // above we called ReplaceModule on the first one, and Remove
          // on the rest.
          if (Log *log = GetLog(LLDBLog::Target | LLDBLog::Modules)) {
            StreamString message;
            auto dump = [&message](Module &dump_module) -> void {
              UUID dump_uuid = dump_module.GetUUID();

              message << '[';
              dump_module.GetDescription(message.AsRawOstream());
              message << " (uuid ";

              if (dump_uuid.IsValid())
                dump_uuid.Dump(message);
              else
                message << "not specified";

              message << ")]";
            };

            message << "New module ";
            dump(*module_sp);
            message.AsRawOstream()
                << llvm::formatv(" simultaneously replaced {0} old modules: ",
                                 replaced_modules.size());
            for (ModuleSP &replaced_module_sp : replaced_modules)
              dump(*replaced_module_sp);

            log->PutString(message.GetString());
          }
        }

        if (replaced_modules.empty())
          m_images.Append(module_sp, notify);

        for (ModuleSP &old_module_sp : replaced_modules) {
          Module *old_module_ptr = old_module_sp.get();
          old_module_sp.reset();
          ModuleList::RemoveSharedModuleIfOrphaned(old_module_ptr);
        }
      } else
        module_sp.reset();
    }
  }
  if (error_ptr)
    *error_ptr = error;
  return module_sp;
}

TargetSP Target::CalculateTarget() { return shared_from_this(); }

ProcessSP Target::CalculateProcess() { return m_process_sp; }

ThreadSP Target::CalculateThread() { return ThreadSP(); }

StackFrameSP Target::CalculateStackFrame() { return StackFrameSP(); }

void Target::CalculateExecutionContext(ExecutionContext &exe_ctx) {
  exe_ctx.Clear();
  exe_ctx.SetTargetPtr(this);
}

PathMappingList &Target::GetImageSearchPathList() {
  return m_image_search_paths;
}

void Target::ImageSearchPathsChanged(const PathMappingList &path_list,
                                     void *baton) {
  Target *target = (Target *)baton;
  ModuleSP exe_module_sp(target->GetExecutableModule());
  if (exe_module_sp)
    target->SetExecutableModule(exe_module_sp, eLoadDependentsYes);
}

llvm::Expected<lldb::TypeSystemSP>
Target::GetScratchTypeSystemForLanguage(lldb::LanguageType language,
                                        bool create_on_demand) {
  if (!m_valid)
    return llvm::createStringError("Invalid Target");

  if (language == eLanguageTypeMipsAssembler // GNU AS and LLVM use it for all
                                             // assembly code
      || language == eLanguageTypeUnknown) {
    LanguageSet languages_for_expressions =
        Language::GetLanguagesSupportingTypeSystemsForExpressions();

    if (languages_for_expressions[eLanguageTypeC]) {
      language = eLanguageTypeC; // LLDB's default.  Override by setting the
                                 // target language.
    } else {
      if (languages_for_expressions.Empty())
        return llvm::createStringError(
            "No expression support for any languages");
      language = (LanguageType)languages_for_expressions.bitvector.find_first();
    }
  }

  return m_scratch_type_system_map.GetTypeSystemForLanguage(language, this,
                                                            create_on_demand);
}

CompilerType Target::GetRegisterType(const std::string &name,
                                     const lldb_private::RegisterFlags &flags,
                                     uint32_t byte_size) {
  RegisterTypeBuilderSP provider = PluginManager::GetRegisterTypeBuilder(*this);
  assert(provider);
  return provider->GetRegisterType(name, flags, byte_size);
}

std::vector<lldb::TypeSystemSP>
Target::GetScratchTypeSystems(bool create_on_demand) {
  if (!m_valid)
    return {};

  // Some TypeSystem instances are associated with several LanguageTypes so
  // they will show up several times in the loop below. The SetVector filters
  // out all duplicates as they serve no use for the caller.
  std::vector<lldb::TypeSystemSP> scratch_type_systems;

  LanguageSet languages_for_expressions =
      Language::GetLanguagesSupportingTypeSystemsForExpressions();

  for (auto bit : languages_for_expressions.bitvector.set_bits()) {
    auto language = (LanguageType)bit;
    auto type_system_or_err =
        GetScratchTypeSystemForLanguage(language, create_on_demand);
    if (!type_system_or_err)
      LLDB_LOG_ERROR(
          GetLog(LLDBLog::Target), type_system_or_err.takeError(),
          "Language '{1}' has expression support but no scratch type "
          "system available: {0}",
          Language::GetNameForLanguageType(language));
    else
      if (auto ts = *type_system_or_err)
        scratch_type_systems.push_back(ts);
  }

  std::sort(scratch_type_systems.begin(), scratch_type_systems.end());
  scratch_type_systems.erase(
      std::unique(scratch_type_systems.begin(), scratch_type_systems.end()),
      scratch_type_systems.end());
  return scratch_type_systems;
}

PersistentExpressionState *
Target::GetPersistentExpressionStateForLanguage(lldb::LanguageType language) {
  auto type_system_or_err = GetScratchTypeSystemForLanguage(language, true);

  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(
        GetLog(LLDBLog::Target), std::move(err),
        "Unable to get persistent expression state for language {1}: {0}",
        Language::GetNameForLanguageType(language));
    return nullptr;
  }

  if (auto ts = *type_system_or_err)
    return ts->GetPersistentExpressionState();

  LLDB_LOG(GetLog(LLDBLog::Target),
           "Unable to get persistent expression state for language {1}: {0}",
           Language::GetNameForLanguageType(language));
  return nullptr;
}

UserExpression *Target::GetUserExpressionForLanguage(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj,
    Status &error) {
  auto type_system_or_err =
      GetScratchTypeSystemForLanguage(language.AsLanguageType());
  if (auto err = type_system_or_err.takeError()) {
    error.SetErrorStringWithFormat(
        "Could not find type system for language %s: %s",
        Language::GetNameForLanguageType(language.AsLanguageType()),
        llvm::toString(std::move(err)).c_str());
    return nullptr;
  }

  auto ts = *type_system_or_err;
  if (!ts) {
    error.SetErrorStringWithFormat(
        "Type system for language %s is no longer live",
        language.GetDescription().data());
    return nullptr;
  }

  auto *user_expr = ts->GetUserExpression(expr, prefix, language, desired_type,
                                          options, ctx_obj);
  if (!user_expr)
    error.SetErrorStringWithFormat(
        "Could not create an expression for language %s",
        language.GetDescription().data());

  return user_expr;
}

FunctionCaller *Target::GetFunctionCallerForLanguage(
    lldb::LanguageType language, const CompilerType &return_type,
    const Address &function_address, const ValueList &arg_value_list,
    const char *name, Status &error) {
  auto type_system_or_err = GetScratchTypeSystemForLanguage(language);
  if (auto err = type_system_or_err.takeError()) {
    error.SetErrorStringWithFormat(
        "Could not find type system for language %s: %s",
        Language::GetNameForLanguageType(language),
        llvm::toString(std::move(err)).c_str());
    return nullptr;
  }
  auto ts = *type_system_or_err;
  if (!ts) {
    error.SetErrorStringWithFormat(
        "Type system for language %s is no longer live",
        Language::GetNameForLanguageType(language));
    return nullptr;
  }
  auto *persistent_fn = ts->GetFunctionCaller(return_type, function_address,
                                              arg_value_list, name);
  if (!persistent_fn)
    error.SetErrorStringWithFormat(
        "Could not create an expression for language %s",
        Language::GetNameForLanguageType(language));

  return persistent_fn;
}

llvm::Expected<std::unique_ptr<UtilityFunction>>
Target::CreateUtilityFunction(std::string expression, std::string name,
                              lldb::LanguageType language,
                              ExecutionContext &exe_ctx) {
  auto type_system_or_err = GetScratchTypeSystemForLanguage(language);
  if (!type_system_or_err)
    return type_system_or_err.takeError();
  auto ts = *type_system_or_err;
  if (!ts)
    return llvm::createStringError(
        llvm::StringRef("Type system for language ") +
        Language::GetNameForLanguageType(language) +
        llvm::StringRef(" is no longer live"));
  std::unique_ptr<UtilityFunction> utility_fn =
      ts->CreateUtilityFunction(std::move(expression), std::move(name));
  if (!utility_fn)
    return llvm::createStringError(
        llvm::StringRef("Could not create an expression for language") +
        Language::GetNameForLanguageType(language));

  DiagnosticManager diagnostics;
  if (!utility_fn->Install(diagnostics, exe_ctx))
    return llvm::createStringError(diagnostics.GetString());

  return std::move(utility_fn);
}

void Target::SettingsInitialize() { Process::SettingsInitialize(); }

void Target::SettingsTerminate() { Process::SettingsTerminate(); }

FileSpecList Target::GetDefaultExecutableSearchPaths() {
  return Target::GetGlobalProperties().GetExecutableSearchPaths();
}

FileSpecList Target::GetDefaultDebugFileSearchPaths() {
  return Target::GetGlobalProperties().GetDebugFileSearchPaths();
}

ArchSpec Target::GetDefaultArchitecture() {
  return Target::GetGlobalProperties().GetDefaultArchitecture();
}

void Target::SetDefaultArchitecture(const ArchSpec &arch) {
  LLDB_LOG(GetLog(LLDBLog::Target),
           "setting target's default architecture to  {0} ({1})",
           arch.GetArchitectureName(), arch.GetTriple().getTriple());
  Target::GetGlobalProperties().SetDefaultArchitecture(arch);
}

llvm::Error Target::SetLabel(llvm::StringRef label) {
  size_t n = LLDB_INVALID_INDEX32;
  if (llvm::to_integer(label, n))
    return llvm::createStringError("Cannot use integer as target label.");
  TargetList &targets = GetDebugger().GetTargetList();
  for (size_t i = 0; i < targets.GetNumTargets(); i++) {
    TargetSP target_sp = targets.GetTargetAtIndex(i);
    if (target_sp && target_sp->GetLabel() == label) {
        return llvm::make_error<llvm::StringError>(
            llvm::formatv(
                "Cannot use label '{0}' since it's set in target #{1}.", label,
                i),
            llvm::inconvertibleErrorCode());
    }
  }

  m_label = label.str();
  return llvm::Error::success();
}

Target *Target::GetTargetFromContexts(const ExecutionContext *exe_ctx_ptr,
                                      const SymbolContext *sc_ptr) {
  // The target can either exist in the "process" of ExecutionContext, or in
  // the "target_sp" member of SymbolContext. This accessor helper function
  // will get the target from one of these locations.

  Target *target = nullptr;
  if (sc_ptr != nullptr)
    target = sc_ptr->target_sp.get();
  if (target == nullptr && exe_ctx_ptr)
    target = exe_ctx_ptr->GetTargetPtr();
  return target;
}

ExpressionResults Target::EvaluateExpression(
    llvm::StringRef expr, ExecutionContextScope *exe_scope,
    lldb::ValueObjectSP &result_valobj_sp,
    const EvaluateExpressionOptions &options, std::string *fixed_expression,
    ValueObject *ctx_obj) {
  result_valobj_sp.reset();

  ExpressionResults execution_results = eExpressionSetupError;

  if (expr.empty()) {
    m_stats.GetExpressionStats().NotifyFailure();
    return execution_results;
  }

  // We shouldn't run stop hooks in expressions.
  bool old_suppress_value = m_suppress_stop_hooks;
  m_suppress_stop_hooks = true;
  auto on_exit = llvm::make_scope_exit([this, old_suppress_value]() {
    m_suppress_stop_hooks = old_suppress_value;
  });

  ExecutionContext exe_ctx;

  if (exe_scope) {
    exe_scope->CalculateExecutionContext(exe_ctx);
  } else if (m_process_sp) {
    m_process_sp->CalculateExecutionContext(exe_ctx);
  } else {
    CalculateExecutionContext(exe_ctx);
  }

  // Make sure we aren't just trying to see the value of a persistent variable
  // (something like "$0")
  // Only check for persistent variables the expression starts with a '$'
  lldb::ExpressionVariableSP persistent_var_sp;
  if (expr[0] == '$') {
    auto type_system_or_err =
            GetScratchTypeSystemForLanguage(eLanguageTypeC);
    if (auto err = type_system_or_err.takeError()) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Target), std::move(err),
                     "Unable to get scratch type system");
    } else {
      auto ts = *type_system_or_err;
      if (!ts)
        LLDB_LOG_ERROR(GetLog(LLDBLog::Target), std::move(err),
                       "Scratch type system is no longer live: {0}");
      else
        persistent_var_sp =
            ts->GetPersistentExpressionState()->GetVariable(expr);
    }
  }
  if (persistent_var_sp) {
    result_valobj_sp = persistent_var_sp->GetValueObject();
    execution_results = eExpressionCompleted;
  } else {
    llvm::StringRef prefix = GetExpressionPrefixContents();
    Status error;
    execution_results = UserExpression::Evaluate(exe_ctx, options, expr, prefix,
                                                 result_valobj_sp, error,
                                                 fixed_expression, ctx_obj);
    // Pass up the error by wrapping it inside an error result.
    if (error.Fail() && !result_valobj_sp)
      result_valobj_sp = ValueObjectConstResult::Create(
          exe_ctx.GetBestExecutionContextScope(), error);
  }

  if (execution_results == eExpressionCompleted)
    m_stats.GetExpressionStats().NotifySuccess();
  else
    m_stats.GetExpressionStats().NotifyFailure();
  return execution_results;
}

lldb::ExpressionVariableSP Target::GetPersistentVariable(ConstString name) {
  lldb::ExpressionVariableSP variable_sp;
  m_scratch_type_system_map.ForEach(
      [name, &variable_sp](TypeSystemSP type_system) -> bool {
        auto ts = type_system.get();
        if (!ts)
          return true;
        if (PersistentExpressionState *persistent_state =
                ts->GetPersistentExpressionState()) {
          variable_sp = persistent_state->GetVariable(name);

          if (variable_sp)
            return false; // Stop iterating the ForEach
        }
        return true; // Keep iterating the ForEach
      });
  return variable_sp;
}

lldb::addr_t Target::GetPersistentSymbol(ConstString name) {
  lldb::addr_t address = LLDB_INVALID_ADDRESS;

  m_scratch_type_system_map.ForEach(
      [name, &address](lldb::TypeSystemSP type_system) -> bool {
        auto ts = type_system.get();
        if (!ts)
          return true;

        if (PersistentExpressionState *persistent_state =
                ts->GetPersistentExpressionState()) {
          address = persistent_state->LookupSymbol(name);
          if (address != LLDB_INVALID_ADDRESS)
            return false; // Stop iterating the ForEach
        }
        return true; // Keep iterating the ForEach
      });
  return address;
}

llvm::Expected<lldb_private::Address> Target::GetEntryPointAddress() {
  Module *exe_module = GetExecutableModulePointer();

  // Try to find the entry point address in the primary executable.
  const bool has_primary_executable = exe_module && exe_module->GetObjectFile();
  if (has_primary_executable) {
    Address entry_addr = exe_module->GetObjectFile()->GetEntryPointAddress();
    if (entry_addr.IsValid())
      return entry_addr;
  }

  const ModuleList &modules = GetImages();
  const size_t num_images = modules.GetSize();
  for (size_t idx = 0; idx < num_images; ++idx) {
    ModuleSP module_sp(modules.GetModuleAtIndex(idx));
    if (!module_sp || !module_sp->GetObjectFile())
      continue;

    Address entry_addr = module_sp->GetObjectFile()->GetEntryPointAddress();
    if (entry_addr.IsValid())
      return entry_addr;
  }

  // We haven't found the entry point address. Return an appropriate error.
  if (!has_primary_executable)
    return llvm::createStringError(
        "No primary executable found and could not find entry point address in "
        "any executable module");

  return llvm::createStringError(
      "Could not find entry point address for primary executable module \"" +
      exe_module->GetFileSpec().GetFilename().GetStringRef() + "\"");
}

lldb::addr_t Target::GetCallableLoadAddress(lldb::addr_t load_addr,
                                            AddressClass addr_class) const {
  auto arch_plugin = GetArchitecturePlugin();
  return arch_plugin
             ? arch_plugin->GetCallableLoadAddress(load_addr, addr_class)
             : load_addr;
}

lldb::addr_t Target::GetOpcodeLoadAddress(lldb::addr_t load_addr,
                                          AddressClass addr_class) const {
  auto arch_plugin = GetArchitecturePlugin();
  return arch_plugin ? arch_plugin->GetOpcodeLoadAddress(load_addr, addr_class)
                     : load_addr;
}

lldb::addr_t Target::GetBreakableLoadAddress(lldb::addr_t addr) {
  auto arch_plugin = GetArchitecturePlugin();
  return arch_plugin ? arch_plugin->GetBreakableLoadAddress(addr, *this) : addr;
}

SourceManager &Target::GetSourceManager() {
  if (!m_source_manager_up)
    m_source_manager_up = std::make_unique<SourceManager>(shared_from_this());
  return *m_source_manager_up;
}

Target::StopHookSP Target::CreateStopHook(StopHook::StopHookKind kind) {
  lldb::user_id_t new_uid = ++m_stop_hook_next_id;
  Target::StopHookSP stop_hook_sp;
  switch (kind) {
  case StopHook::StopHookKind::CommandBased:
    stop_hook_sp.reset(new StopHookCommandLine(shared_from_this(), new_uid));
    break;
  case StopHook::StopHookKind::ScriptBased:
    stop_hook_sp.reset(new StopHookScripted(shared_from_this(), new_uid));
    break;
  }
  m_stop_hooks[new_uid] = stop_hook_sp;
  return stop_hook_sp;
}

void Target::UndoCreateStopHook(lldb::user_id_t user_id) {
  if (!RemoveStopHookByID(user_id))
    return;
  if (user_id == m_stop_hook_next_id)
    m_stop_hook_next_id--;
}

bool Target::RemoveStopHookByID(lldb::user_id_t user_id) {
  size_t num_removed = m_stop_hooks.erase(user_id);
  return (num_removed != 0);
}

void Target::RemoveAllStopHooks() { m_stop_hooks.clear(); }

Target::StopHookSP Target::GetStopHookByID(lldb::user_id_t user_id) {
  StopHookSP found_hook;

  StopHookCollection::iterator specified_hook_iter;
  specified_hook_iter = m_stop_hooks.find(user_id);
  if (specified_hook_iter != m_stop_hooks.end())
    found_hook = (*specified_hook_iter).second;
  return found_hook;
}

bool Target::SetStopHookActiveStateByID(lldb::user_id_t user_id,
                                        bool active_state) {
  StopHookCollection::iterator specified_hook_iter;
  specified_hook_iter = m_stop_hooks.find(user_id);
  if (specified_hook_iter == m_stop_hooks.end())
    return false;

  (*specified_hook_iter).second->SetIsActive(active_state);
  return true;
}

void Target::SetAllStopHooksActiveState(bool active_state) {
  StopHookCollection::iterator pos, end = m_stop_hooks.end();
  for (pos = m_stop_hooks.begin(); pos != end; pos++) {
    (*pos).second->SetIsActive(active_state);
  }
}

bool Target::RunStopHooks() {
  if (m_suppress_stop_hooks)
    return false;

  if (!m_process_sp)
    return false;

  // Somebody might have restarted the process:
  // Still return false, the return value is about US restarting the target.
  if (m_process_sp->GetState() != eStateStopped)
    return false;

  if (m_stop_hooks.empty())
    return false;

  // If there aren't any active stop hooks, don't bother either.
  bool any_active_hooks = false;
  for (auto hook : m_stop_hooks) {
    if (hook.second->IsActive()) {
      any_active_hooks = true;
      break;
    }
  }
  if (!any_active_hooks)
    return false;

  // Make sure we check that we are not stopped because of us running a user
  // expression since in that case we do not want to run the stop-hooks. Note,
  // you can't just check whether the last stop was for a User Expression,
  // because breakpoint commands get run before stop hooks, and one of them
  // might have run an expression. You have to ensure you run the stop hooks
  // once per natural stop.
  uint32_t last_natural_stop = m_process_sp->GetModIDRef().GetLastNaturalStopID();
  if (last_natural_stop != 0 && m_latest_stop_hook_id == last_natural_stop)
    return false;

  m_latest_stop_hook_id = last_natural_stop;

  std::vector<ExecutionContext> exc_ctx_with_reasons;

  ThreadList &cur_threadlist = m_process_sp->GetThreadList();
  size_t num_threads = cur_threadlist.GetSize();
  for (size_t i = 0; i < num_threads; i++) {
    lldb::ThreadSP cur_thread_sp = cur_threadlist.GetThreadAtIndex(i);
    if (cur_thread_sp->ThreadStoppedForAReason()) {
      lldb::StackFrameSP cur_frame_sp = cur_thread_sp->GetStackFrameAtIndex(0);
      exc_ctx_with_reasons.emplace_back(m_process_sp.get(), cur_thread_sp.get(),
                                        cur_frame_sp.get());
    }
  }

  // If no threads stopped for a reason, don't run the stop-hooks.
  size_t num_exe_ctx = exc_ctx_with_reasons.size();
  if (num_exe_ctx == 0)
    return false;

  StreamSP output_sp = m_debugger.GetAsyncOutputStream();

  bool auto_continue = false;
  bool hooks_ran = false;
  bool print_hook_header = (m_stop_hooks.size() != 1);
  bool print_thread_header = (num_exe_ctx != 1);
  bool should_stop = false;
  bool somebody_restarted = false;

  for (auto stop_entry : m_stop_hooks) {
    StopHookSP cur_hook_sp = stop_entry.second;
    if (!cur_hook_sp->IsActive())
      continue;

    bool any_thread_matched = false;
    for (auto exc_ctx : exc_ctx_with_reasons) {
      // We detect somebody restarted in the stop-hook loop, and broke out of
      // that loop back to here.  So break out of here too.
      if (somebody_restarted)
        break;

      if (!cur_hook_sp->ExecutionContextPasses(exc_ctx))
        continue;

      // We only consult the auto-continue for a stop hook if it matched the
      // specifier.
      auto_continue |= cur_hook_sp->GetAutoContinue();

      if (!hooks_ran)
        hooks_ran = true;

      if (print_hook_header && !any_thread_matched) {
        StreamString s;
        cur_hook_sp->GetDescription(s, eDescriptionLevelBrief);
        if (s.GetSize() != 0)
          output_sp->Printf("\n- Hook %" PRIu64 " (%s)\n", cur_hook_sp->GetID(),
                            s.GetData());
        else
          output_sp->Printf("\n- Hook %" PRIu64 "\n", cur_hook_sp->GetID());
        any_thread_matched = true;
      }

      if (print_thread_header)
        output_sp->Printf("-- Thread %d\n",
                          exc_ctx.GetThreadPtr()->GetIndexID());

      StopHook::StopHookResult this_result =
          cur_hook_sp->HandleStop(exc_ctx, output_sp);
      bool this_should_stop = true;

      switch (this_result) {
      case StopHook::StopHookResult::KeepStopped:
        // If this hook is set to auto-continue that should override the
        // HandleStop result...
        if (cur_hook_sp->GetAutoContinue())
          this_should_stop = false;
        else
          this_should_stop = true;

        break;
      case StopHook::StopHookResult::RequestContinue:
        this_should_stop = false;
        break;
      case StopHook::StopHookResult::AlreadyContinued:
        // We don't have a good way to prohibit people from restarting the
        // target willy nilly in a stop hook.  If the hook did so, give a
        // gentle suggestion here and bag out if the hook processing.
        output_sp->Printf("\nAborting stop hooks, hook %" PRIu64
                          " set the program running.\n"
                          "  Consider using '-G true' to make "
                          "stop hooks auto-continue.\n",
                          cur_hook_sp->GetID());
        somebody_restarted = true;
        break;
      }
      // If we're already restarted, stop processing stop hooks.
      // FIXME: if we are doing non-stop mode for real, we would have to
      // check that OUR thread was restarted, otherwise we should keep
      // processing stop hooks.
      if (somebody_restarted)
        break;

      // If anybody wanted to stop, we should all stop.
      if (!should_stop)
        should_stop = this_should_stop;
    }
  }

  output_sp->Flush();

  // If one of the commands in the stop hook already restarted the target,
  // report that fact.
  if (somebody_restarted)
    return true;

  // Finally, if auto-continue was requested, do it now:
  // We only compute should_stop against the hook results if a hook got to run
  // which is why we have to do this conjoint test.
  if ((hooks_ran && !should_stop) || auto_continue) {
    Log *log = GetLog(LLDBLog::Process);
    Status error = m_process_sp->PrivateResume();
    if (error.Success()) {
      LLDB_LOG(log, "Resuming from RunStopHooks");
      return true;
    } else {
      LLDB_LOG(log, "Resuming from RunStopHooks failed: {0}", error);
      return false;
    }
  }

  return false;
}

TargetProperties &Target::GetGlobalProperties() {
  // NOTE: intentional leak so we don't crash if global destructor chain gets
  // called as other threads still use the result of this function
  static TargetProperties *g_settings_ptr =
      new TargetProperties(nullptr);
  return *g_settings_ptr;
}

Status Target::Install(ProcessLaunchInfo *launch_info) {
  Status error;
  PlatformSP platform_sp(GetPlatform());
  if (platform_sp) {
    if (platform_sp->IsRemote()) {
      if (platform_sp->IsConnected()) {
        // Install all files that have an install path when connected to a
        // remote platform. If target.auto-install-main-executable is set then
        // also install the main executable even if it does not have an explicit
        // install path specified.
        const ModuleList &modules = GetImages();
        const size_t num_images = modules.GetSize();
        for (size_t idx = 0; idx < num_images; ++idx) {
          ModuleSP module_sp(modules.GetModuleAtIndex(idx));
          if (module_sp) {
            const bool is_main_executable = module_sp == GetExecutableModule();
            FileSpec local_file(module_sp->GetFileSpec());
            if (local_file) {
              FileSpec remote_file(module_sp->GetRemoteInstallFileSpec());
              if (!remote_file) {
                if (is_main_executable && GetAutoInstallMainExecutable()) {
                  // Automatically install the main executable.
                  remote_file = platform_sp->GetRemoteWorkingDirectory();
                  remote_file.AppendPathComponent(
                      module_sp->GetFileSpec().GetFilename().GetCString());
                }
              }
              if (remote_file) {
                error = platform_sp->Install(local_file, remote_file);
                if (error.Success()) {
                  module_sp->SetPlatformFileSpec(remote_file);
                  if (is_main_executable) {
                    platform_sp->SetFilePermissions(remote_file, 0700);
                    if (launch_info)
                      launch_info->SetExecutableFile(remote_file, false);
                  }
                } else
                  break;
              }
            }
          }
        }
      }
    }
  }
  return error;
}

bool Target::ResolveLoadAddress(addr_t load_addr, Address &so_addr,
                                uint32_t stop_id) {
  return m_section_load_history.ResolveLoadAddress(stop_id, load_addr, so_addr);
}

bool Target::ResolveFileAddress(lldb::addr_t file_addr,
                                Address &resolved_addr) {
  return m_images.ResolveFileAddress(file_addr, resolved_addr);
}

bool Target::SetSectionLoadAddress(const SectionSP &section_sp,
                                   addr_t new_section_load_addr,
                                   bool warn_multiple) {
  const addr_t old_section_load_addr =
      m_section_load_history.GetSectionLoadAddress(
          SectionLoadHistory::eStopIDNow, section_sp);
  if (old_section_load_addr != new_section_load_addr) {
    uint32_t stop_id = 0;
    ProcessSP process_sp(GetProcessSP());
    if (process_sp)
      stop_id = process_sp->GetStopID();
    else
      stop_id = m_section_load_history.GetLastStopID();
    if (m_section_load_history.SetSectionLoadAddress(
            stop_id, section_sp, new_section_load_addr, warn_multiple))
      return true; // Return true if the section load address was changed...
  }
  return false; // Return false to indicate nothing changed
}

size_t Target::UnloadModuleSections(const ModuleList &module_list) {
  size_t section_unload_count = 0;
  size_t num_modules = module_list.GetSize();
  for (size_t i = 0; i < num_modules; ++i) {
    section_unload_count +=
        UnloadModuleSections(module_list.GetModuleAtIndex(i));
  }
  return section_unload_count;
}

size_t Target::UnloadModuleSections(const lldb::ModuleSP &module_sp) {
  uint32_t stop_id = 0;
  ProcessSP process_sp(GetProcessSP());
  if (process_sp)
    stop_id = process_sp->GetStopID();
  else
    stop_id = m_section_load_history.GetLastStopID();
  SectionList *sections = module_sp->GetSectionList();
  size_t section_unload_count = 0;
  if (sections) {
    const uint32_t num_sections = sections->GetNumSections(0);
    for (uint32_t i = 0; i < num_sections; ++i) {
      section_unload_count += m_section_load_history.SetSectionUnloaded(
          stop_id, sections->GetSectionAtIndex(i));
    }
  }
  return section_unload_count;
}

bool Target::SetSectionUnloaded(const lldb::SectionSP &section_sp) {
  uint32_t stop_id = 0;
  ProcessSP process_sp(GetProcessSP());
  if (process_sp)
    stop_id = process_sp->GetStopID();
  else
    stop_id = m_section_load_history.GetLastStopID();
  return m_section_load_history.SetSectionUnloaded(stop_id, section_sp);
}

bool Target::SetSectionUnloaded(const lldb::SectionSP &section_sp,
                                addr_t load_addr) {
  uint32_t stop_id = 0;
  ProcessSP process_sp(GetProcessSP());
  if (process_sp)
    stop_id = process_sp->GetStopID();
  else
    stop_id = m_section_load_history.GetLastStopID();
  return m_section_load_history.SetSectionUnloaded(stop_id, section_sp,
                                                   load_addr);
}

void Target::ClearAllLoadedSections() { m_section_load_history.Clear(); }

void Target::SaveScriptedLaunchInfo(lldb_private::ProcessInfo &process_info) {
  if (process_info.IsScriptedProcess()) {
    // Only copy scripted process launch options.
    ProcessLaunchInfo &default_launch_info = const_cast<ProcessLaunchInfo &>(
        GetGlobalProperties().GetProcessLaunchInfo());
    default_launch_info.SetProcessPluginName("ScriptedProcess");
    default_launch_info.SetScriptedMetadata(process_info.GetScriptedMetadata());
    SetProcessLaunchInfo(default_launch_info);
  }
}

Status Target::Launch(ProcessLaunchInfo &launch_info, Stream *stream) {
  m_stats.SetLaunchOrAttachTime();
  Status error;
  Log *log = GetLog(LLDBLog::Target);

  LLDB_LOGF(log, "Target::%s() called for %s", __FUNCTION__,
            launch_info.GetExecutableFile().GetPath().c_str());

  StateType state = eStateInvalid;

  // Scope to temporarily get the process state in case someone has manually
  // remotely connected already to a process and we can skip the platform
  // launching.
  {
    ProcessSP process_sp(GetProcessSP());

    if (process_sp) {
      state = process_sp->GetState();
      LLDB_LOGF(log,
                "Target::%s the process exists, and its current state is %s",
                __FUNCTION__, StateAsCString(state));
    } else {
      LLDB_LOGF(log, "Target::%s the process instance doesn't currently exist.",
                __FUNCTION__);
    }
  }

  launch_info.GetFlags().Set(eLaunchFlagDebug);

  SaveScriptedLaunchInfo(launch_info);

  // Get the value of synchronous execution here.  If you wait till after you
  // have started to run, then you could have hit a breakpoint, whose command
  // might switch the value, and then you'll pick up that incorrect value.
  Debugger &debugger = GetDebugger();
  const bool synchronous_execution =
      debugger.GetCommandInterpreter().GetSynchronous();

  PlatformSP platform_sp(GetPlatform());

  FinalizeFileActions(launch_info);

  if (state == eStateConnected) {
    if (launch_info.GetFlags().Test(eLaunchFlagLaunchInTTY)) {
      error.SetErrorString(
          "can't launch in tty when launching through a remote connection");
      return error;
    }
  }

  if (!launch_info.GetArchitecture().IsValid())
    launch_info.GetArchitecture() = GetArchitecture();

  // Hijacking events of the process to be created to be sure that all events
  // until the first stop are intercepted (in case if platform doesn't define
  // its own hijacking listener or if the process is created by the target
  // manually, without the platform).
  if (!launch_info.GetHijackListener())
    launch_info.SetHijackListener(Listener::MakeListener(
        Process::LaunchSynchronousHijackListenerName.data()));

  // If we're not already connected to the process, and if we have a platform
  // that can launch a process for debugging, go ahead and do that here.
  if (state != eStateConnected && platform_sp &&
      platform_sp->CanDebugProcess() && !launch_info.IsScriptedProcess()) {
    LLDB_LOGF(log, "Target::%s asking the platform to debug the process",
              __FUNCTION__);

    // If there was a previous process, delete it before we make the new one.
    // One subtle point, we delete the process before we release the reference
    // to m_process_sp.  That way even if we are the last owner, the process
    // will get Finalized before it gets destroyed.
    DeleteCurrentProcess();

    m_process_sp =
        GetPlatform()->DebugProcess(launch_info, debugger, *this, error);

  } else {
    LLDB_LOGF(log,
              "Target::%s the platform doesn't know how to debug a "
              "process, getting a process plugin to do this for us.",
              __FUNCTION__);

    if (state == eStateConnected) {
      assert(m_process_sp);
    } else {
      // Use a Process plugin to construct the process.
      CreateProcess(launch_info.GetListener(),
                    launch_info.GetProcessPluginName(), nullptr, false);
    }

    // Since we didn't have a platform launch the process, launch it here.
    if (m_process_sp) {
      m_process_sp->HijackProcessEvents(launch_info.GetHijackListener());
      m_process_sp->SetShadowListener(launch_info.GetShadowListener());
      error = m_process_sp->Launch(launch_info);
    }
  }

  if (!m_process_sp && error.Success())
    error.SetErrorString("failed to launch or debug process");

  if (!error.Success())
    return error;

  bool rebroadcast_first_stop =
      !synchronous_execution &&
      launch_info.GetFlags().Test(eLaunchFlagStopAtEntry);

  assert(launch_info.GetHijackListener());

  EventSP first_stop_event_sp;
  state = m_process_sp->WaitForProcessToStop(std::nullopt, &first_stop_event_sp,
                                             rebroadcast_first_stop,
                                             launch_info.GetHijackListener());
  m_process_sp->RestoreProcessEvents();

  if (rebroadcast_first_stop) {
    assert(first_stop_event_sp);
    m_process_sp->BroadcastEvent(first_stop_event_sp);
    return error;
  }

  switch (state) {
  case eStateStopped: {
    if (launch_info.GetFlags().Test(eLaunchFlagStopAtEntry))
      break;
    if (synchronous_execution)
      // Now we have handled the stop-from-attach, and we are just
      // switching to a synchronous resume.  So we should switch to the
      // SyncResume hijacker.
      m_process_sp->ResumeSynchronous(stream);
    else
      error = m_process_sp->Resume();
    if (!error.Success()) {
      Status error2;
      error2.SetErrorStringWithFormat(
          "process resume at entry point failed: %s", error.AsCString());
      error = error2;
    }
  } break;
  case eStateExited: {
    bool with_shell = !!launch_info.GetShell();
    const int exit_status = m_process_sp->GetExitStatus();
    const char *exit_desc = m_process_sp->GetExitDescription();
    std::string desc;
    if (exit_desc && exit_desc[0])
      desc = " (" + std::string(exit_desc) + ')';
    if (with_shell)
      error.SetErrorStringWithFormat(
          "process exited with status %i%s\n"
          "'r' and 'run' are aliases that default to launching through a "
          "shell.\n"
          "Try launching without going through a shell by using "
          "'process launch'.",
          exit_status, desc.c_str());
    else
      error.SetErrorStringWithFormat("process exited with status %i%s",
                                     exit_status, desc.c_str());
  } break;
  default:
    error.SetErrorStringWithFormat("initial process state wasn't stopped: %s",
                                   StateAsCString(state));
    break;
  }
  return error;
}

void Target::SetTrace(const TraceSP &trace_sp) { m_trace_sp = trace_sp; }

TraceSP Target::GetTrace() { return m_trace_sp; }

llvm::Expected<TraceSP> Target::CreateTrace() {
  if (!m_process_sp)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "A process is required for tracing");
  if (m_trace_sp)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "A trace already exists for the target");

  llvm::Expected<TraceSupportedResponse> trace_type =
      m_process_sp->TraceSupported();
  if (!trace_type)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(), "Tracing is not supported. %s",
        llvm::toString(trace_type.takeError()).c_str());
  if (llvm::Expected<TraceSP> trace_sp =
          Trace::FindPluginForLiveProcess(trace_type->name, *m_process_sp))
    m_trace_sp = *trace_sp;
  else
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Couldn't create a Trace object for the process. %s",
        llvm::toString(trace_sp.takeError()).c_str());
  return m_trace_sp;
}

llvm::Expected<TraceSP> Target::GetTraceOrCreate() {
  if (m_trace_sp)
    return m_trace_sp;
  return CreateTrace();
}

Status Target::Attach(ProcessAttachInfo &attach_info, Stream *stream) {
  m_stats.SetLaunchOrAttachTime();
  auto state = eStateInvalid;
  auto process_sp = GetProcessSP();
  if (process_sp) {
    state = process_sp->GetState();
    if (process_sp->IsAlive() && state != eStateConnected) {
      if (state == eStateAttaching)
        return Status("process attach is in progress");
      return Status("a process is already being debugged");
    }
  }

  const ModuleSP old_exec_module_sp = GetExecutableModule();

  // If no process info was specified, then use the target executable name as
  // the process to attach to by default
  if (!attach_info.ProcessInfoSpecified()) {
    if (old_exec_module_sp)
      attach_info.GetExecutableFile().SetFilename(
            old_exec_module_sp->GetPlatformFileSpec().GetFilename());

    if (!attach_info.ProcessInfoSpecified()) {
      return Status("no process specified, create a target with a file, or "
                    "specify the --pid or --name");
    }
  }

  const auto platform_sp =
      GetDebugger().GetPlatformList().GetSelectedPlatform();
  ListenerSP hijack_listener_sp;
  const bool async = attach_info.GetAsync();
  if (!async) {
    hijack_listener_sp = Listener::MakeListener(
        Process::AttachSynchronousHijackListenerName.data());
    attach_info.SetHijackListener(hijack_listener_sp);
  }

  Status error;
  if (state != eStateConnected && platform_sp != nullptr &&
      platform_sp->CanDebugProcess() && !attach_info.IsScriptedProcess()) {
    SetPlatform(platform_sp);
    process_sp = platform_sp->Attach(attach_info, GetDebugger(), this, error);
  } else {
    if (state != eStateConnected) {
      SaveScriptedLaunchInfo(attach_info);
      llvm::StringRef plugin_name = attach_info.GetProcessPluginName();
      process_sp =
          CreateProcess(attach_info.GetListenerForProcess(GetDebugger()),
                        plugin_name, nullptr, false);
      if (!process_sp) {
        error.SetErrorStringWithFormatv(
            "failed to create process using plugin '{0}'",
            plugin_name.empty() ? "<empty>" : plugin_name);
        return error;
      }
    }
    if (hijack_listener_sp)
      process_sp->HijackProcessEvents(hijack_listener_sp);
    error = process_sp->Attach(attach_info);
  }

  if (error.Success() && process_sp) {
    if (async) {
      process_sp->RestoreProcessEvents();
    } else {
      // We are stopping all the way out to the user, so update selected frames.
      state = process_sp->WaitForProcessToStop(
          std::nullopt, nullptr, false, attach_info.GetHijackListener(), stream,
          true, SelectMostRelevantFrame);
      process_sp->RestoreProcessEvents();

      if (state != eStateStopped) {
        const char *exit_desc = process_sp->GetExitDescription();
        if (exit_desc)
          error.SetErrorStringWithFormat("%s", exit_desc);
        else
          error.SetErrorString(
              "process did not stop (no such process or permission problem?)");
        process_sp->Destroy(false);
      }
    }
  }
  return error;
}

void Target::FinalizeFileActions(ProcessLaunchInfo &info) {
  Log *log = GetLog(LLDBLog::Process);

  // Finalize the file actions, and if none were given, default to opening up a
  // pseudo terminal
  PlatformSP platform_sp = GetPlatform();
  const bool default_to_use_pty =
      m_platform_sp ? m_platform_sp->IsHost() : false;
  LLDB_LOG(
      log,
      "have platform={0}, platform_sp->IsHost()={1}, default_to_use_pty={2}",
      bool(platform_sp),
      platform_sp ? (platform_sp->IsHost() ? "true" : "false") : "n/a",
      default_to_use_pty);

  // If nothing for stdin or stdout or stderr was specified, then check the
  // process for any default settings that were set with "settings set"
  if (info.GetFileActionForFD(STDIN_FILENO) == nullptr ||
      info.GetFileActionForFD(STDOUT_FILENO) == nullptr ||
      info.GetFileActionForFD(STDERR_FILENO) == nullptr) {
    LLDB_LOG(log, "at least one of stdin/stdout/stderr was not set, evaluating "
                  "default handling");

    if (info.GetFlags().Test(eLaunchFlagLaunchInTTY)) {
      // Do nothing, if we are launching in a remote terminal no file actions
      // should be done at all.
      return;
    }

    if (info.GetFlags().Test(eLaunchFlagDisableSTDIO)) {
      LLDB_LOG(log, "eLaunchFlagDisableSTDIO set, adding suppression action "
                    "for stdin, stdout and stderr");
      info.AppendSuppressFileAction(STDIN_FILENO, true, false);
      info.AppendSuppressFileAction(STDOUT_FILENO, false, true);
      info.AppendSuppressFileAction(STDERR_FILENO, false, true);
    } else {
      // Check for any values that might have gotten set with any of: (lldb)
      // settings set target.input-path (lldb) settings set target.output-path
      // (lldb) settings set target.error-path
      FileSpec in_file_spec;
      FileSpec out_file_spec;
      FileSpec err_file_spec;
      // Only override with the target settings if we don't already have an
      // action for in, out or error
      if (info.GetFileActionForFD(STDIN_FILENO) == nullptr)
        in_file_spec = GetStandardInputPath();
      if (info.GetFileActionForFD(STDOUT_FILENO) == nullptr)
        out_file_spec = GetStandardOutputPath();
      if (info.GetFileActionForFD(STDERR_FILENO) == nullptr)
        err_file_spec = GetStandardErrorPath();

      LLDB_LOG(log, "target stdin='{0}', target stdout='{1}', stderr='{1}'",
               in_file_spec, out_file_spec, err_file_spec);

      if (in_file_spec) {
        info.AppendOpenFileAction(STDIN_FILENO, in_file_spec, true, false);
        LLDB_LOG(log, "appended stdin open file action for {0}", in_file_spec);
      }

      if (out_file_spec) {
        info.AppendOpenFileAction(STDOUT_FILENO, out_file_spec, false, true);
        LLDB_LOG(log, "appended stdout open file action for {0}",
                 out_file_spec);
      }

      if (err_file_spec) {
        info.AppendOpenFileAction(STDERR_FILENO, err_file_spec, false, true);
        LLDB_LOG(log, "appended stderr open file action for {0}",
                 err_file_spec);
      }

      if (default_to_use_pty) {
        llvm::Error Err = info.SetUpPtyRedirection();
        LLDB_LOG_ERROR(log, std::move(Err), "SetUpPtyRedirection failed: {0}");
      }
    }
  }
}

void Target::AddDummySignal(llvm::StringRef name, LazyBool pass, LazyBool notify,
                            LazyBool stop) {
    if (name.empty())
      return;
    // Don't add a signal if all the actions are trivial:
    if (pass == eLazyBoolCalculate && notify == eLazyBoolCalculate
        && stop == eLazyBoolCalculate)
      return;

    auto& elem = m_dummy_signals[name];
    elem.pass = pass;
    elem.notify = notify;
    elem.stop = stop;
}

bool Target::UpdateSignalFromDummy(UnixSignalsSP signals_sp,
                                          const DummySignalElement &elem) {
  if (!signals_sp)
    return false;

  int32_t signo
      = signals_sp->GetSignalNumberFromName(elem.first().str().c_str());
  if (signo == LLDB_INVALID_SIGNAL_NUMBER)
    return false;

  if (elem.second.pass == eLazyBoolYes)
    signals_sp->SetShouldSuppress(signo, false);
  else if (elem.second.pass == eLazyBoolNo)
    signals_sp->SetShouldSuppress(signo, true);

  if (elem.second.notify == eLazyBoolYes)
    signals_sp->SetShouldNotify(signo, true);
  else if (elem.second.notify == eLazyBoolNo)
    signals_sp->SetShouldNotify(signo, false);

  if (elem.second.stop == eLazyBoolYes)
    signals_sp->SetShouldStop(signo, true);
  else if (elem.second.stop == eLazyBoolNo)
    signals_sp->SetShouldStop(signo, false);
  return true;
}

bool Target::ResetSignalFromDummy(UnixSignalsSP signals_sp,
                                          const DummySignalElement &elem) {
  if (!signals_sp)
    return false;
  int32_t signo
      = signals_sp->GetSignalNumberFromName(elem.first().str().c_str());
  if (signo == LLDB_INVALID_SIGNAL_NUMBER)
    return false;
  bool do_pass = elem.second.pass != eLazyBoolCalculate;
  bool do_stop = elem.second.stop != eLazyBoolCalculate;
  bool do_notify = elem.second.notify != eLazyBoolCalculate;
  signals_sp->ResetSignal(signo, do_stop, do_notify, do_pass);
  return true;
}

void Target::UpdateSignalsFromDummy(UnixSignalsSP signals_sp,
                                    StreamSP warning_stream_sp) {
  if (!signals_sp)
    return;

  for (const auto &elem : m_dummy_signals) {
    if (!UpdateSignalFromDummy(signals_sp, elem))
      warning_stream_sp->Printf("Target signal '%s' not found in process\n",
          elem.first().str().c_str());
  }
}

void Target::ClearDummySignals(Args &signal_names) {
  ProcessSP process_sp = GetProcessSP();
  // The simplest case, delete them all with no process to update.
  if (signal_names.GetArgumentCount() == 0 && !process_sp) {
    m_dummy_signals.clear();
    return;
  }
  UnixSignalsSP signals_sp;
  if (process_sp)
    signals_sp = process_sp->GetUnixSignals();

  for (const Args::ArgEntry &entry : signal_names) {
    const char *signal_name = entry.c_str();
    auto elem = m_dummy_signals.find(signal_name);
    // If we didn't find it go on.
    // FIXME: Should I pipe error handling through here?
    if (elem == m_dummy_signals.end()) {
      continue;
    }
    if (signals_sp)
      ResetSignalFromDummy(signals_sp, *elem);
    m_dummy_signals.erase(elem);
  }
}

void Target::PrintDummySignals(Stream &strm, Args &signal_args) {
  strm.Printf("NAME         PASS     STOP     NOTIFY\n");
  strm.Printf("===========  =======  =======  =======\n");

  auto str_for_lazy = [] (LazyBool lazy) -> const char * {
    switch (lazy) {
      case eLazyBoolCalculate: return "not set";
      case eLazyBoolYes: return "true   ";
      case eLazyBoolNo: return "false  ";
    }
    llvm_unreachable("Fully covered switch above!");
  };
  size_t num_args = signal_args.GetArgumentCount();
  for (const auto &elem : m_dummy_signals) {
    bool print_it = false;
    for (size_t idx = 0; idx < num_args; idx++) {
      if (elem.first() == signal_args.GetArgumentAtIndex(idx)) {
        print_it = true;
        break;
      }
    }
    if (print_it) {
      strm.Printf("%-11s  ", elem.first().str().c_str());
      strm.Printf("%s  %s  %s\n", str_for_lazy(elem.second.pass),
                  str_for_lazy(elem.second.stop),
                  str_for_lazy(elem.second.notify));
    }
  }
}

// Target::StopHook
Target::StopHook::StopHook(lldb::TargetSP target_sp, lldb::user_id_t uid)
    : UserID(uid), m_target_sp(target_sp), m_specifier_sp(),
      m_thread_spec_up() {}

Target::StopHook::StopHook(const StopHook &rhs)
    : UserID(rhs.GetID()), m_target_sp(rhs.m_target_sp),
      m_specifier_sp(rhs.m_specifier_sp), m_thread_spec_up(),
      m_active(rhs.m_active), m_auto_continue(rhs.m_auto_continue) {
  if (rhs.m_thread_spec_up)
    m_thread_spec_up = std::make_unique<ThreadSpec>(*rhs.m_thread_spec_up);
}

void Target::StopHook::SetSpecifier(SymbolContextSpecifier *specifier) {
  m_specifier_sp.reset(specifier);
}

void Target::StopHook::SetThreadSpecifier(ThreadSpec *specifier) {
  m_thread_spec_up.reset(specifier);
}

bool Target::StopHook::ExecutionContextPasses(const ExecutionContext &exc_ctx) {
  SymbolContextSpecifier *specifier = GetSpecifier();
  if (!specifier)
    return true;

  bool will_run = true;
  if (exc_ctx.GetFramePtr())
    will_run = GetSpecifier()->SymbolContextMatches(
        exc_ctx.GetFramePtr()->GetSymbolContext(eSymbolContextEverything));
  if (will_run && GetThreadSpecifier() != nullptr)
    will_run =
        GetThreadSpecifier()->ThreadPassesBasicTests(exc_ctx.GetThreadRef());

  return will_run;
}

void Target::StopHook::GetDescription(Stream &s,
                                      lldb::DescriptionLevel level) const {

  // For brief descriptions, only print the subclass description:
  if (level == eDescriptionLevelBrief) {
    GetSubclassDescription(s, level);
    return;
  }

  unsigned indent_level = s.GetIndentLevel();

  s.SetIndentLevel(indent_level + 2);

  s.Printf("Hook: %" PRIu64 "\n", GetID());
  if (m_active)
    s.Indent("State: enabled\n");
  else
    s.Indent("State: disabled\n");

  if (m_auto_continue)
    s.Indent("AutoContinue on\n");

  if (m_specifier_sp) {
    s.Indent();
    s.PutCString("Specifier:\n");
    s.SetIndentLevel(indent_level + 4);
    m_specifier_sp->GetDescription(&s, level);
    s.SetIndentLevel(indent_level + 2);
  }

  if (m_thread_spec_up) {
    StreamString tmp;
    s.Indent("Thread:\n");
    m_thread_spec_up->GetDescription(&tmp, level);
    s.SetIndentLevel(indent_level + 4);
    s.Indent(tmp.GetString());
    s.PutCString("\n");
    s.SetIndentLevel(indent_level + 2);
  }
  GetSubclassDescription(s, level);
}

void Target::StopHookCommandLine::GetSubclassDescription(
    Stream &s, lldb::DescriptionLevel level) const {
  // The brief description just prints the first command.
  if (level == eDescriptionLevelBrief) {
    if (m_commands.GetSize() == 1)
      s.PutCString(m_commands.GetStringAtIndex(0));
    return;
  }
  s.Indent("Commands: \n");
  s.SetIndentLevel(s.GetIndentLevel() + 4);
  uint32_t num_commands = m_commands.GetSize();
  for (uint32_t i = 0; i < num_commands; i++) {
    s.Indent(m_commands.GetStringAtIndex(i));
    s.PutCString("\n");
  }
  s.SetIndentLevel(s.GetIndentLevel() - 4);
}

// Target::StopHookCommandLine
void Target::StopHookCommandLine::SetActionFromString(const std::string &string) {
  GetCommands().SplitIntoLines(string);
}

void Target::StopHookCommandLine::SetActionFromStrings(
    const std::vector<std::string> &strings) {
  for (auto string : strings)
    GetCommands().AppendString(string.c_str());
}

Target::StopHook::StopHookResult
Target::StopHookCommandLine::HandleStop(ExecutionContext &exc_ctx,
                                        StreamSP output_sp) {
  assert(exc_ctx.GetTargetPtr() && "Can't call PerformAction on a context "
                                   "with no target");

  if (!m_commands.GetSize())
    return StopHookResult::KeepStopped;

  CommandReturnObject result(false);
  result.SetImmediateOutputStream(output_sp);
  result.SetInteractive(false);
  Debugger &debugger = exc_ctx.GetTargetPtr()->GetDebugger();
  CommandInterpreterRunOptions options;
  options.SetStopOnContinue(true);
  options.SetStopOnError(true);
  options.SetEchoCommands(false);
  options.SetPrintResults(true);
  options.SetPrintErrors(true);
  options.SetAddToHistory(false);

  // Force Async:
  bool old_async = debugger.GetAsyncExecution();
  debugger.SetAsyncExecution(true);
  debugger.GetCommandInterpreter().HandleCommands(GetCommands(), exc_ctx,
                                                  options, result);
  debugger.SetAsyncExecution(old_async);
  lldb::ReturnStatus status = result.GetStatus();
  if (status == eReturnStatusSuccessContinuingNoResult ||
      status == eReturnStatusSuccessContinuingResult)
    return StopHookResult::AlreadyContinued;
  return StopHookResult::KeepStopped;
}

// Target::StopHookScripted
Status Target::StopHookScripted::SetScriptCallback(
    std::string class_name, StructuredData::ObjectSP extra_args_sp) {
  Status error;

  ScriptInterpreter *script_interp =
      GetTarget()->GetDebugger().GetScriptInterpreter();
  if (!script_interp) {
    error.SetErrorString("No script interpreter installed.");
    return error;
  }

  m_class_name = class_name;
  m_extra_args.SetObjectSP(extra_args_sp);

  m_implementation_sp = script_interp->CreateScriptedStopHook(
      GetTarget(), m_class_name.c_str(), m_extra_args, error);

  return error;
}

Target::StopHook::StopHookResult
Target::StopHookScripted::HandleStop(ExecutionContext &exc_ctx,
                                     StreamSP output_sp) {
  assert(exc_ctx.GetTargetPtr() && "Can't call HandleStop on a context "
                                   "with no target");

  ScriptInterpreter *script_interp =
      GetTarget()->GetDebugger().GetScriptInterpreter();
  if (!script_interp)
    return StopHookResult::KeepStopped;

  bool should_stop = script_interp->ScriptedStopHookHandleStop(
      m_implementation_sp, exc_ctx, output_sp);

  return should_stop ? StopHookResult::KeepStopped
                     : StopHookResult::RequestContinue;
}

void Target::StopHookScripted::GetSubclassDescription(
    Stream &s, lldb::DescriptionLevel level) const {
  if (level == eDescriptionLevelBrief) {
    s.PutCString(m_class_name);
    return;
  }
  s.Indent("Class:");
  s.Printf("%s\n", m_class_name.c_str());

  // Now print the extra args:
  // FIXME: We should use StructuredData.GetDescription on the m_extra_args
  // but that seems to rely on some printing plugin that doesn't exist.
  if (!m_extra_args.IsValid())
    return;
  StructuredData::ObjectSP object_sp = m_extra_args.GetObjectSP();
  if (!object_sp || !object_sp->IsValid())
    return;

  StructuredData::Dictionary *as_dict = object_sp->GetAsDictionary();
  if (!as_dict || !as_dict->IsValid())
    return;

  uint32_t num_keys = as_dict->GetSize();
  if (num_keys == 0)
    return;

  s.Indent("Args:\n");
  s.SetIndentLevel(s.GetIndentLevel() + 4);

  auto print_one_element = [&s](llvm::StringRef key,
                                StructuredData::Object *object) {
    s.Indent();
    s.Format("{0} : {1}\n", key, object->GetStringValue());
    return true;
  };

  as_dict->ForEach(print_one_element);

  s.SetIndentLevel(s.GetIndentLevel() - 4);
}

static constexpr OptionEnumValueElement g_dynamic_value_types[] = {
    {
        eNoDynamicValues,
        "no-dynamic-values",
        "Don't calculate the dynamic type of values",
    },
    {
        eDynamicCanRunTarget,
        "run-target",
        "Calculate the dynamic type of values "
        "even if you have to run the target.",
    },
    {
        eDynamicDontRunTarget,
        "no-run-target",
        "Calculate the dynamic type of values, but don't run the target.",
    },
};

OptionEnumValues lldb_private::GetDynamicValueTypes() {
  return OptionEnumValues(g_dynamic_value_types);
}

static constexpr OptionEnumValueElement g_inline_breakpoint_enums[] = {
    {
        eInlineBreakpointsNever,
        "never",
        "Never look for inline breakpoint locations (fastest). This setting "
        "should only be used if you know that no inlining occurs in your"
        "programs.",
    },
    {
        eInlineBreakpointsHeaders,
        "headers",
        "Only check for inline breakpoint locations when setting breakpoints "
        "in header files, but not when setting breakpoint in implementation "
        "source files (default).",
    },
    {
        eInlineBreakpointsAlways,
        "always",
        "Always look for inline breakpoint locations when setting file and "
        "line breakpoints (slower but most accurate).",
    },
};

enum x86DisassemblyFlavor {
  eX86DisFlavorDefault,
  eX86DisFlavorIntel,
  eX86DisFlavorATT
};

static constexpr OptionEnumValueElement g_x86_dis_flavor_value_types[] = {
    {
        eX86DisFlavorDefault,
        "default",
        "Disassembler default (currently att).",
    },
    {
        eX86DisFlavorIntel,
        "intel",
        "Intel disassembler flavor.",
    },
    {
        eX86DisFlavorATT,
        "att",
        "AT&T disassembler flavor.",
    },
};

static constexpr OptionEnumValueElement g_import_std_module_value_types[] = {
    {
        eImportStdModuleFalse,
        "false",
        "Never import the 'std' C++ module in the expression parser.",
    },
    {
        eImportStdModuleFallback,
        "fallback",
        "Retry evaluating expressions with an imported 'std' C++ module if they"
        " failed to parse without the module. This allows evaluating more "
        "complex expressions involving C++ standard library types."
    },
    {
        eImportStdModuleTrue,
        "true",
        "Always import the 'std' C++ module. This allows evaluating more "
        "complex expressions involving C++ standard library types. This feature"
        " is experimental."
    },
};

static constexpr OptionEnumValueElement
    g_dynamic_class_info_helper_value_types[] = {
        {
            eDynamicClassInfoHelperAuto,
            "auto",
            "Automatically determine the most appropriate method for the "
            "target OS.",
        },
        {eDynamicClassInfoHelperRealizedClassesStruct, "RealizedClassesStruct",
         "Prefer using the realized classes struct."},
        {eDynamicClassInfoHelperCopyRealizedClassList, "CopyRealizedClassList",
         "Prefer using the CopyRealizedClassList API."},
        {eDynamicClassInfoHelperGetRealizedClassList, "GetRealizedClassList",
         "Prefer using the GetRealizedClassList API."},
};

static constexpr OptionEnumValueElement g_hex_immediate_style_values[] = {
    {
        Disassembler::eHexStyleC,
        "c",
        "C-style (0xffff).",
    },
    {
        Disassembler::eHexStyleAsm,
        "asm",
        "Asm-style (0ffffh).",
    },
};

static constexpr OptionEnumValueElement g_load_script_from_sym_file_values[] = {
    {
        eLoadScriptFromSymFileTrue,
        "true",
        "Load debug scripts inside symbol files",
    },
    {
        eLoadScriptFromSymFileFalse,
        "false",
        "Do not load debug scripts inside symbol files.",
    },
    {
        eLoadScriptFromSymFileWarn,
        "warn",
        "Warn about debug scripts inside symbol files but do not load them.",
    },
};

static constexpr OptionEnumValueElement g_load_cwd_lldbinit_values[] = {
    {
        eLoadCWDlldbinitTrue,
        "true",
        "Load .lldbinit files from current directory",
    },
    {
        eLoadCWDlldbinitFalse,
        "false",
        "Do not load .lldbinit files from current directory",
    },
    {
        eLoadCWDlldbinitWarn,
        "warn",
        "Warn about loading .lldbinit files from current directory",
    },
};

static constexpr OptionEnumValueElement g_memory_module_load_level_values[] = {
    {
        eMemoryModuleLoadLevelMinimal,
        "minimal",
        "Load minimal information when loading modules from memory. Currently "
        "this setting loads sections only.",
    },
    {
        eMemoryModuleLoadLevelPartial,
        "partial",
        "Load partial information when loading modules from memory. Currently "
        "this setting loads sections and function bounds.",
    },
    {
        eMemoryModuleLoadLevelComplete,
        "complete",
        "Load complete information when loading modules from memory. Currently "
        "this setting loads sections and all symbols.",
    },
};

#define LLDB_PROPERTIES_target
#include "TargetProperties.inc"

enum {
#define LLDB_PROPERTIES_target
#include "TargetPropertiesEnum.inc"
  ePropertyExperimental,
};

class TargetOptionValueProperties
    : public Cloneable<TargetOptionValueProperties, OptionValueProperties> {
public:
  TargetOptionValueProperties(llvm::StringRef name) : Cloneable(name) {}

  const Property *
  GetPropertyAtIndex(size_t idx,
                     const ExecutionContext *exe_ctx = nullptr) const override {
    // When getting the value for a key from the target options, we will always
    // try and grab the setting from the current target if there is one. Else
    // we just use the one from this instance.
    if (exe_ctx) {
      Target *target = exe_ctx->GetTargetPtr();
      if (target) {
        TargetOptionValueProperties *target_properties =
            static_cast<TargetOptionValueProperties *>(
                target->GetValueProperties().get());
        if (this != target_properties)
          return target_properties->ProtectedGetPropertyAtIndex(idx);
      }
    }
    return ProtectedGetPropertyAtIndex(idx);
  }
};

// TargetProperties
#define LLDB_PROPERTIES_target_experimental
#include "TargetProperties.inc"

enum {
#define LLDB_PROPERTIES_target_experimental
#include "TargetPropertiesEnum.inc"
};

class TargetExperimentalOptionValueProperties
    : public Cloneable<TargetExperimentalOptionValueProperties,
                       OptionValueProperties> {
public:
  TargetExperimentalOptionValueProperties()
      : Cloneable(Properties::GetExperimentalSettingsName()) {}
};

TargetExperimentalProperties::TargetExperimentalProperties()
    : Properties(OptionValuePropertiesSP(
          new TargetExperimentalOptionValueProperties())) {
  m_collection_sp->Initialize(g_target_experimental_properties);
}

// TargetProperties
TargetProperties::TargetProperties(Target *target)
    : Properties(), m_launch_info(), m_target(target) {
  if (target) {
    m_collection_sp =
        OptionValueProperties::CreateLocalCopy(Target::GetGlobalProperties());

    // Set callbacks to update launch_info whenever "settins set" updated any
    // of these properties
    m_collection_sp->SetValueChangedCallback(
        ePropertyArg0, [this] { Arg0ValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyRunArgs, [this] { RunArgsValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyEnvVars, [this] { EnvVarsValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyUnsetEnvVars, [this] { EnvVarsValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyInheritEnv, [this] { EnvVarsValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyInputPath, [this] { InputPathValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyOutputPath, [this] { OutputPathValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyErrorPath, [this] { ErrorPathValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(ePropertyDetachOnError, [this] {
      DetachOnErrorValueChangedCallback();
    });
    m_collection_sp->SetValueChangedCallback(
        ePropertyDisableASLR, [this] { DisableASLRValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyInheritTCC, [this] { InheritTCCValueChangedCallback(); });
    m_collection_sp->SetValueChangedCallback(
        ePropertyDisableSTDIO, [this] { DisableSTDIOValueChangedCallback(); });

    m_collection_sp->SetValueChangedCallback(
        ePropertySaveObjectsDir, [this] { CheckJITObjectsDir(); });
    m_experimental_properties_up =
        std::make_unique<TargetExperimentalProperties>();
    m_collection_sp->AppendProperty(
        Properties::GetExperimentalSettingsName(),
        "Experimental settings - setting these won't produce "
        "errors if the setting is not present.",
        true, m_experimental_properties_up->GetValueProperties());
  } else {
    m_collection_sp = std::make_shared<TargetOptionValueProperties>("target");
    m_collection_sp->Initialize(g_target_properties);
    m_experimental_properties_up =
        std::make_unique<TargetExperimentalProperties>();
    m_collection_sp->AppendProperty(
        Properties::GetExperimentalSettingsName(),
        "Experimental settings - setting these won't produce "
        "errors if the setting is not present.",
        true, m_experimental_properties_up->GetValueProperties());
    m_collection_sp->AppendProperty(
        "process", "Settings specific to processes.", true,
        Process::GetGlobalProperties().GetValueProperties());
    m_collection_sp->SetValueChangedCallback(
        ePropertySaveObjectsDir, [this] { CheckJITObjectsDir(); });
  }
}

TargetProperties::~TargetProperties() = default;

void TargetProperties::UpdateLaunchInfoFromProperties() {
  Arg0ValueChangedCallback();
  RunArgsValueChangedCallback();
  EnvVarsValueChangedCallback();
  InputPathValueChangedCallback();
  OutputPathValueChangedCallback();
  ErrorPathValueChangedCallback();
  DetachOnErrorValueChangedCallback();
  DisableASLRValueChangedCallback();
  InheritTCCValueChangedCallback();
  DisableSTDIOValueChangedCallback();
}

std::optional<bool> TargetProperties::GetExperimentalPropertyValue(
    size_t prop_idx, ExecutionContext *exe_ctx) const {
  const Property *exp_property =
      m_collection_sp->GetPropertyAtIndex(ePropertyExperimental, exe_ctx);
  OptionValueProperties *exp_values =
      exp_property->GetValue()->GetAsProperties();
  if (exp_values)
    return exp_values->GetPropertyAtIndexAs<bool>(prop_idx, exe_ctx);
  return std::nullopt;
}

bool TargetProperties::GetInjectLocalVariables(
    ExecutionContext *exe_ctx) const {
  return GetExperimentalPropertyValue(ePropertyInjectLocalVars, exe_ctx)
      .value_or(true);
}

ArchSpec TargetProperties::GetDefaultArchitecture() const {
  const uint32_t idx = ePropertyDefaultArch;
  return GetPropertyAtIndexAs<ArchSpec>(idx, {});
}

void TargetProperties::SetDefaultArchitecture(const ArchSpec &arch) {
  const uint32_t idx = ePropertyDefaultArch;
  SetPropertyAtIndex(idx, arch);
}

bool TargetProperties::GetMoveToNearestCode() const {
  const uint32_t idx = ePropertyMoveToNearestCode;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

lldb::DynamicValueType TargetProperties::GetPreferDynamicValue() const {
  const uint32_t idx = ePropertyPreferDynamic;
  return GetPropertyAtIndexAs<lldb::DynamicValueType>(
      idx, static_cast<lldb::DynamicValueType>(
               g_target_properties[idx].default_uint_value));
}

bool TargetProperties::SetPreferDynamicValue(lldb::DynamicValueType d) {
  const uint32_t idx = ePropertyPreferDynamic;
  return SetPropertyAtIndex(idx, d);
}

bool TargetProperties::GetPreloadSymbols() const {
  if (INTERRUPT_REQUESTED(m_target->GetDebugger(),
                          "Interrupted checking preload symbols")) {
    return false;
  }
  const uint32_t idx = ePropertyPreloadSymbols;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetPreloadSymbols(bool b) {
  const uint32_t idx = ePropertyPreloadSymbols;
  SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetDisableASLR() const {
  const uint32_t idx = ePropertyDisableASLR;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDisableASLR(bool b) {
  const uint32_t idx = ePropertyDisableASLR;
  SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetInheritTCC() const {
  const uint32_t idx = ePropertyInheritTCC;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetInheritTCC(bool b) {
  const uint32_t idx = ePropertyInheritTCC;
  SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetDetachOnError() const {
  const uint32_t idx = ePropertyDetachOnError;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDetachOnError(bool b) {
  const uint32_t idx = ePropertyDetachOnError;
  SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetDisableSTDIO() const {
  const uint32_t idx = ePropertyDisableSTDIO;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDisableSTDIO(bool b) {
  const uint32_t idx = ePropertyDisableSTDIO;
  SetPropertyAtIndex(idx, b);
}

const char *TargetProperties::GetDisassemblyFlavor() const {
  const uint32_t idx = ePropertyDisassemblyFlavor;
  const char *return_value;

  x86DisassemblyFlavor flavor_value =
      GetPropertyAtIndexAs<x86DisassemblyFlavor>(
          idx, static_cast<x86DisassemblyFlavor>(
                   g_target_properties[idx].default_uint_value));

  return_value = g_x86_dis_flavor_value_types[flavor_value].string_value;
  return return_value;
}

InlineStrategy TargetProperties::GetInlineStrategy() const {
  const uint32_t idx = ePropertyInlineStrategy;
  return GetPropertyAtIndexAs<InlineStrategy>(
      idx,
      static_cast<InlineStrategy>(g_target_properties[idx].default_uint_value));
}

llvm::StringRef TargetProperties::GetArg0() const {
  const uint32_t idx = ePropertyArg0;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_target_properties[idx].default_cstr_value);
}

void TargetProperties::SetArg0(llvm::StringRef arg) {
  const uint32_t idx = ePropertyArg0;
  SetPropertyAtIndex(idx, arg);
  m_launch_info.SetArg0(arg);
}

bool TargetProperties::GetRunArguments(Args &args) const {
  const uint32_t idx = ePropertyRunArgs;
  return m_collection_sp->GetPropertyAtIndexAsArgs(idx, args);
}

void TargetProperties::SetRunArguments(const Args &args) {
  const uint32_t idx = ePropertyRunArgs;
  m_collection_sp->SetPropertyAtIndexFromArgs(idx, args);
  m_launch_info.GetArguments() = args;
}

Environment TargetProperties::ComputeEnvironment() const {
  Environment env;

  if (m_target &&
      GetPropertyAtIndexAs<bool>(
          ePropertyInheritEnv,
          g_target_properties[ePropertyInheritEnv].default_uint_value != 0)) {
    if (auto platform_sp = m_target->GetPlatform()) {
      Environment platform_env = platform_sp->GetEnvironment();
      for (const auto &KV : platform_env)
        env[KV.first()] = KV.second;
    }
  }

  Args property_unset_env;
  m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyUnsetEnvVars,
                                            property_unset_env);
  for (const auto &var : property_unset_env)
    env.erase(var.ref());

  Args property_env;
  m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyEnvVars, property_env);
  for (const auto &KV : Environment(property_env))
    env[KV.first()] = KV.second;

  return env;
}

Environment TargetProperties::GetEnvironment() const {
  return ComputeEnvironment();
}

Environment TargetProperties::GetInheritedEnvironment() const {
  Environment environment;

  if (m_target == nullptr)
    return environment;

  if (!GetPropertyAtIndexAs<bool>(
          ePropertyInheritEnv,
          g_target_properties[ePropertyInheritEnv].default_uint_value != 0))
    return environment;

  PlatformSP platform_sp = m_target->GetPlatform();
  if (platform_sp == nullptr)
    return environment;

  Environment platform_environment = platform_sp->GetEnvironment();
  for (const auto &KV : platform_environment)
    environment[KV.first()] = KV.second;

  Args property_unset_environment;
  m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyUnsetEnvVars,
                                            property_unset_environment);
  for (const auto &var : property_unset_environment)
    environment.erase(var.ref());

  return environment;
}

Environment TargetProperties::GetTargetEnvironment() const {
  Args property_environment;
  m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyEnvVars,
                                            property_environment);
  Environment environment;
  for (const auto &KV : Environment(property_environment))
    environment[KV.first()] = KV.second;

  return environment;
}

void TargetProperties::SetEnvironment(Environment env) {
  // TODO: Get rid of the Args intermediate step
  const uint32_t idx = ePropertyEnvVars;
  m_collection_sp->SetPropertyAtIndexFromArgs(idx, Args(env));
}

bool TargetProperties::GetSkipPrologue() const {
  const uint32_t idx = ePropertySkipPrologue;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

PathMappingList &TargetProperties::GetSourcePathMap() const {
  const uint32_t idx = ePropertySourceMap;
  OptionValuePathMappings *option_value =
      m_collection_sp->GetPropertyAtIndexAsOptionValuePathMappings(idx);
  assert(option_value);
  return option_value->GetCurrentValue();
}

bool TargetProperties::GetAutoSourceMapRelative() const {
  const uint32_t idx = ePropertyAutoSourceMapRelative;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::AppendExecutableSearchPaths(const FileSpec &dir) {
  const uint32_t idx = ePropertyExecutableSearchPaths;
  OptionValueFileSpecList *option_value =
      m_collection_sp->GetPropertyAtIndexAsOptionValueFileSpecList(idx);
  assert(option_value);
  option_value->AppendCurrentValue(dir);
}

FileSpecList TargetProperties::GetExecutableSearchPaths() {
  const uint32_t idx = ePropertyExecutableSearchPaths;
  return GetPropertyAtIndexAs<FileSpecList>(idx, {});
}

FileSpecList TargetProperties::GetDebugFileSearchPaths() {
  const uint32_t idx = ePropertyDebugFileSearchPaths;
  return GetPropertyAtIndexAs<FileSpecList>(idx, {});
}

FileSpecList TargetProperties::GetClangModuleSearchPaths() {
  const uint32_t idx = ePropertyClangModuleSearchPaths;
  return GetPropertyAtIndexAs<FileSpecList>(idx, {});
}

bool TargetProperties::GetEnableAutoImportClangModules() const {
  const uint32_t idx = ePropertyAutoImportClangModules;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

ImportStdModule TargetProperties::GetImportStdModule() const {
  const uint32_t idx = ePropertyImportStdModule;
  return GetPropertyAtIndexAs<ImportStdModule>(
      idx, static_cast<ImportStdModule>(
               g_target_properties[idx].default_uint_value));
}

DynamicClassInfoHelper TargetProperties::GetDynamicClassInfoHelper() const {
  const uint32_t idx = ePropertyDynamicClassInfoHelper;
  return GetPropertyAtIndexAs<DynamicClassInfoHelper>(
      idx, static_cast<DynamicClassInfoHelper>(
               g_target_properties[idx].default_uint_value));
}

bool TargetProperties::GetEnableAutoApplyFixIts() const {
  const uint32_t idx = ePropertyAutoApplyFixIts;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

uint64_t TargetProperties::GetNumberOfRetriesWithFixits() const {
  const uint32_t idx = ePropertyRetriesWithFixIts;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

bool TargetProperties::GetEnableNotifyAboutFixIts() const {
  const uint32_t idx = ePropertyNotifyAboutFixIts;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

FileSpec TargetProperties::GetSaveJITObjectsDir() const {
  const uint32_t idx = ePropertySaveObjectsDir;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

void TargetProperties::CheckJITObjectsDir() {
  FileSpec new_dir = GetSaveJITObjectsDir();
  if (!new_dir)
    return;

  const FileSystem &instance = FileSystem::Instance();
  bool exists = instance.Exists(new_dir);
  bool is_directory = instance.IsDirectory(new_dir);
  std::string path = new_dir.GetPath(true);
  bool writable = llvm::sys::fs::can_write(path);
  if (exists && is_directory && writable)
    return;

  m_collection_sp->GetPropertyAtIndex(ePropertySaveObjectsDir)
      ->GetValue()
      ->Clear();

  std::string buffer;
  llvm::raw_string_ostream os(buffer);
  os << "JIT object dir '" << path << "' ";
  if (!exists)
    os << "does not exist";
  else if (!is_directory)
    os << "is not a directory";
  else if (!writable)
    os << "is not writable";

  std::optional<lldb::user_id_t> debugger_id;
  if (m_target)
    debugger_id = m_target->GetDebugger().GetID();
  Debugger::ReportError(os.str(), debugger_id);
}

bool TargetProperties::GetEnableSyntheticValue() const {
  const uint32_t idx = ePropertyEnableSynthetic;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

bool TargetProperties::ShowHexVariableValuesWithLeadingZeroes() const {
  const uint32_t idx = ePropertyShowHexVariableValuesWithLeadingZeroes;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

uint32_t TargetProperties::GetMaxZeroPaddingInFloatFormat() const {
  const uint32_t idx = ePropertyMaxZeroPaddingInFloatFormat;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

uint32_t TargetProperties::GetMaximumNumberOfChildrenToDisplay() const {
  const uint32_t idx = ePropertyMaxChildrenCount;
  return GetPropertyAtIndexAs<int64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

std::pair<uint32_t, bool>
TargetProperties::GetMaximumDepthOfChildrenToDisplay() const {
  const uint32_t idx = ePropertyMaxChildrenDepth;
  auto *option_value =
      m_collection_sp->GetPropertyAtIndexAsOptionValueUInt64(idx);
  bool is_default = !option_value->OptionWasSet();
  return {option_value->GetCurrentValue(), is_default};
}

uint32_t TargetProperties::GetMaximumSizeOfStringSummary() const {
  const uint32_t idx = ePropertyMaxSummaryLength;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

uint32_t TargetProperties::GetMaximumMemReadSize() const {
  const uint32_t idx = ePropertyMaxMemReadSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

FileSpec TargetProperties::GetStandardInputPath() const {
  const uint32_t idx = ePropertyInputPath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

void TargetProperties::SetStandardInputPath(llvm::StringRef path) {
  const uint32_t idx = ePropertyInputPath;
  SetPropertyAtIndex(idx, path);
}

FileSpec TargetProperties::GetStandardOutputPath() const {
  const uint32_t idx = ePropertyOutputPath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

void TargetProperties::SetStandardOutputPath(llvm::StringRef path) {
  const uint32_t idx = ePropertyOutputPath;
  SetPropertyAtIndex(idx, path);
}

FileSpec TargetProperties::GetStandardErrorPath() const {
  const uint32_t idx = ePropertyErrorPath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

void TargetProperties::SetStandardErrorPath(llvm::StringRef path) {
  const uint32_t idx = ePropertyErrorPath;
  SetPropertyAtIndex(idx, path);
}

SourceLanguage TargetProperties::GetLanguage() const {
  const uint32_t idx = ePropertyLanguage;
  return {GetPropertyAtIndexAs<LanguageType>(idx, {})};
}

llvm::StringRef TargetProperties::GetExpressionPrefixContents() {
  const uint32_t idx = ePropertyExprPrefix;
  OptionValueFileSpec *file =
      m_collection_sp->GetPropertyAtIndexAsOptionValueFileSpec(idx);
  if (file) {
    DataBufferSP data_sp(file->GetFileContents());
    if (data_sp)
      return llvm::StringRef(
          reinterpret_cast<const char *>(data_sp->GetBytes()),
          data_sp->GetByteSize());
  }
  return "";
}

uint64_t TargetProperties::GetExprErrorLimit() const {
  const uint32_t idx = ePropertyExprErrorLimit;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

uint64_t TargetProperties::GetExprAllocAddress() const {
  const uint32_t idx = ePropertyExprAllocAddress;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

uint64_t TargetProperties::GetExprAllocSize() const {
  const uint32_t idx = ePropertyExprAllocSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

uint64_t TargetProperties::GetExprAllocAlign() const {
  const uint32_t idx = ePropertyExprAllocAlign;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_target_properties[idx].default_uint_value);
}

bool TargetProperties::GetBreakpointsConsultPlatformAvoidList() {
  const uint32_t idx = ePropertyBreakpointUseAvoidList;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

bool TargetProperties::GetUseHexImmediates() const {
  const uint32_t idx = ePropertyUseHexImmediates;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

bool TargetProperties::GetUseFastStepping() const {
  const uint32_t idx = ePropertyUseFastStepping;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

bool TargetProperties::GetDisplayExpressionsInCrashlogs() const {
  const uint32_t idx = ePropertyDisplayExpressionsInCrashlogs;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

LoadScriptFromSymFile TargetProperties::GetLoadScriptFromSymbolFile() const {
  const uint32_t idx = ePropertyLoadScriptFromSymbolFile;
  return GetPropertyAtIndexAs<LoadScriptFromSymFile>(
      idx, static_cast<LoadScriptFromSymFile>(
               g_target_properties[idx].default_uint_value));
}

LoadCWDlldbinitFile TargetProperties::GetLoadCWDlldbinitFile() const {
  const uint32_t idx = ePropertyLoadCWDlldbinitFile;
  return GetPropertyAtIndexAs<LoadCWDlldbinitFile>(
      idx, static_cast<LoadCWDlldbinitFile>(
               g_target_properties[idx].default_uint_value));
}

Disassembler::HexImmediateStyle TargetProperties::GetHexImmediateStyle() const {
  const uint32_t idx = ePropertyHexImmediateStyle;
  return GetPropertyAtIndexAs<Disassembler::HexImmediateStyle>(
      idx, static_cast<Disassembler::HexImmediateStyle>(
               g_target_properties[idx].default_uint_value));
}

MemoryModuleLoadLevel TargetProperties::GetMemoryModuleLoadLevel() const {
  const uint32_t idx = ePropertyMemoryModuleLoadLevel;
  return GetPropertyAtIndexAs<MemoryModuleLoadLevel>(
      idx, static_cast<MemoryModuleLoadLevel>(
               g_target_properties[idx].default_uint_value));
}

bool TargetProperties::GetUserSpecifiedTrapHandlerNames(Args &args) const {
  const uint32_t idx = ePropertyTrapHandlerNames;
  return m_collection_sp->GetPropertyAtIndexAsArgs(idx, args);
}

void TargetProperties::SetUserSpecifiedTrapHandlerNames(const Args &args) {
  const uint32_t idx = ePropertyTrapHandlerNames;
  m_collection_sp->SetPropertyAtIndexFromArgs(idx, args);
}

bool TargetProperties::GetDisplayRuntimeSupportValues() const {
  const uint32_t idx = ePropertyDisplayRuntimeSupportValues;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDisplayRuntimeSupportValues(bool b) {
  const uint32_t idx = ePropertyDisplayRuntimeSupportValues;
  SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetDisplayRecognizedArguments() const {
  const uint32_t idx = ePropertyDisplayRecognizedArguments;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDisplayRecognizedArguments(bool b) {
  const uint32_t idx = ePropertyDisplayRecognizedArguments;
  SetPropertyAtIndex(idx, b);
}

const ProcessLaunchInfo &TargetProperties::GetProcessLaunchInfo() const {
  return m_launch_info;
}

void TargetProperties::SetProcessLaunchInfo(
    const ProcessLaunchInfo &launch_info) {
  m_launch_info = launch_info;
  SetArg0(launch_info.GetArg0());
  SetRunArguments(launch_info.GetArguments());
  SetEnvironment(launch_info.GetEnvironment());
  const FileAction *input_file_action =
      launch_info.GetFileActionForFD(STDIN_FILENO);
  if (input_file_action) {
    SetStandardInputPath(input_file_action->GetPath());
  }
  const FileAction *output_file_action =
      launch_info.GetFileActionForFD(STDOUT_FILENO);
  if (output_file_action) {
    SetStandardOutputPath(output_file_action->GetPath());
  }
  const FileAction *error_file_action =
      launch_info.GetFileActionForFD(STDERR_FILENO);
  if (error_file_action) {
    SetStandardErrorPath(error_file_action->GetPath());
  }
  SetDetachOnError(launch_info.GetFlags().Test(lldb::eLaunchFlagDetachOnError));
  SetDisableASLR(launch_info.GetFlags().Test(lldb::eLaunchFlagDisableASLR));
  SetInheritTCC(
      launch_info.GetFlags().Test(lldb::eLaunchFlagInheritTCCFromParent));
  SetDisableSTDIO(launch_info.GetFlags().Test(lldb::eLaunchFlagDisableSTDIO));
}

bool TargetProperties::GetRequireHardwareBreakpoints() const {
  const uint32_t idx = ePropertyRequireHardwareBreakpoints;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetRequireHardwareBreakpoints(bool b) {
  const uint32_t idx = ePropertyRequireHardwareBreakpoints;
  m_collection_sp->SetPropertyAtIndex(idx, b);
}

bool TargetProperties::GetAutoInstallMainExecutable() const {
  const uint32_t idx = ePropertyAutoInstallMainExecutable;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::Arg0ValueChangedCallback() {
  m_launch_info.SetArg0(GetArg0());
}

void TargetProperties::RunArgsValueChangedCallback() {
  Args args;
  if (GetRunArguments(args))
    m_launch_info.GetArguments() = args;
}

void TargetProperties::EnvVarsValueChangedCallback() {
  m_launch_info.GetEnvironment() = ComputeEnvironment();
}

void TargetProperties::InputPathValueChangedCallback() {
  m_launch_info.AppendOpenFileAction(STDIN_FILENO, GetStandardInputPath(), true,
                                     false);
}

void TargetProperties::OutputPathValueChangedCallback() {
  m_launch_info.AppendOpenFileAction(STDOUT_FILENO, GetStandardOutputPath(),
                                     false, true);
}

void TargetProperties::ErrorPathValueChangedCallback() {
  m_launch_info.AppendOpenFileAction(STDERR_FILENO, GetStandardErrorPath(),
                                     false, true);
}

void TargetProperties::DetachOnErrorValueChangedCallback() {
  if (GetDetachOnError())
    m_launch_info.GetFlags().Set(lldb::eLaunchFlagDetachOnError);
  else
    m_launch_info.GetFlags().Clear(lldb::eLaunchFlagDetachOnError);
}

void TargetProperties::DisableASLRValueChangedCallback() {
  if (GetDisableASLR())
    m_launch_info.GetFlags().Set(lldb::eLaunchFlagDisableASLR);
  else
    m_launch_info.GetFlags().Clear(lldb::eLaunchFlagDisableASLR);
}

void TargetProperties::InheritTCCValueChangedCallback() {
  if (GetInheritTCC())
    m_launch_info.GetFlags().Set(lldb::eLaunchFlagInheritTCCFromParent);
  else
    m_launch_info.GetFlags().Clear(lldb::eLaunchFlagInheritTCCFromParent);
}

void TargetProperties::DisableSTDIOValueChangedCallback() {
  if (GetDisableSTDIO())
    m_launch_info.GetFlags().Set(lldb::eLaunchFlagDisableSTDIO);
  else
    m_launch_info.GetFlags().Clear(lldb::eLaunchFlagDisableSTDIO);
}

bool TargetProperties::GetDebugUtilityExpression() const {
  const uint32_t idx = ePropertyDebugUtilityExpression;
  return GetPropertyAtIndexAs<bool>(
      idx, g_target_properties[idx].default_uint_value != 0);
}

void TargetProperties::SetDebugUtilityExpression(bool debug) {
  const uint32_t idx = ePropertyDebugUtilityExpression;
  SetPropertyAtIndex(idx, debug);
}

// Target::TargetEventData

Target::TargetEventData::TargetEventData(const lldb::TargetSP &target_sp)
    : EventData(), m_target_sp(target_sp), m_module_list() {}

Target::TargetEventData::TargetEventData(const lldb::TargetSP &target_sp,
                                         const ModuleList &module_list)
    : EventData(), m_target_sp(target_sp), m_module_list(module_list) {}

Target::TargetEventData::~TargetEventData() = default;

llvm::StringRef Target::TargetEventData::GetFlavorString() {
  return "Target::TargetEventData";
}

void Target::TargetEventData::Dump(Stream *s) const {
  for (size_t i = 0; i < m_module_list.GetSize(); ++i) {
    if (i != 0)
      *s << ", ";
    m_module_list.GetModuleAtIndex(i)->GetDescription(
        s->AsRawOstream(), lldb::eDescriptionLevelBrief);
  }
}

const Target::TargetEventData *
Target::TargetEventData::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr) {
    const EventData *event_data = event_ptr->GetData();
    if (event_data &&
        event_data->GetFlavor() == TargetEventData::GetFlavorString())
      return static_cast<const TargetEventData *>(event_ptr->GetData());
  }
  return nullptr;
}

TargetSP Target::TargetEventData::GetTargetFromEvent(const Event *event_ptr) {
  TargetSP target_sp;
  const TargetEventData *event_data = GetEventDataFromEvent(event_ptr);
  if (event_data)
    target_sp = event_data->m_target_sp;
  return target_sp;
}

ModuleList
Target::TargetEventData::GetModuleListFromEvent(const Event *event_ptr) {
  ModuleList module_list;
  const TargetEventData *event_data = GetEventDataFromEvent(event_ptr);
  if (event_data)
    module_list = event_data->m_module_list;
  return module_list;
}

std::recursive_mutex &Target::GetAPIMutex() {
  if (GetProcessSP() && GetProcessSP()->CurrentThreadIsPrivateStateThread())
    return m_private_mutex;
  else
    return m_mutex;
}

/// Get metrics associated with this target in JSON format.
llvm::json::Value
Target::ReportStatistics(const lldb_private::StatisticsOptions &options) {
  return m_stats.ToJSON(*this, options);
}
