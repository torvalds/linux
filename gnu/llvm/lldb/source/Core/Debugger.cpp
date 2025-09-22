//===-- Debugger.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Debugger.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Core/DebuggerEvents.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/StreamAsynchronousIO.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Expression/REPL.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Interpreter/OptionValueLanguage.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/OptionValueSInt64.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StructuredDataPlugin.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#include "lldb/Host/windows/windows.h"
#endif

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>

// Includes for pipe()
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace lldb_private {
class Address;
}

using namespace lldb;
using namespace lldb_private;

static lldb::user_id_t g_unique_id = 1;
static size_t g_debugger_event_thread_stack_bytes = 8 * 1024 * 1024;

#pragma mark Static Functions

static std::recursive_mutex *g_debugger_list_mutex_ptr =
    nullptr; // NOTE: intentional leak to avoid issues with C++ destructor chain
static Debugger::DebuggerList *g_debugger_list_ptr =
    nullptr; // NOTE: intentional leak to avoid issues with C++ destructor chain
static llvm::DefaultThreadPool *g_thread_pool = nullptr;

static constexpr OptionEnumValueElement g_show_disassembly_enum_values[] = {
    {
        Debugger::eStopDisassemblyTypeNever,
        "never",
        "Never show disassembly when displaying a stop context.",
    },
    {
        Debugger::eStopDisassemblyTypeNoDebugInfo,
        "no-debuginfo",
        "Show disassembly when there is no debug information.",
    },
    {
        Debugger::eStopDisassemblyTypeNoSource,
        "no-source",
        "Show disassembly when there is no source information, or the source "
        "file "
        "is missing when displaying a stop context.",
    },
    {
        Debugger::eStopDisassemblyTypeAlways,
        "always",
        "Always show disassembly when displaying a stop context.",
    },
};

static constexpr OptionEnumValueElement g_language_enumerators[] = {
    {
        eScriptLanguageNone,
        "none",
        "Disable scripting languages.",
    },
    {
        eScriptLanguagePython,
        "python",
        "Select python as the default scripting language.",
    },
    {
        eScriptLanguageDefault,
        "default",
        "Select the lldb default as the default scripting language.",
    },
};

static constexpr OptionEnumValueElement g_dwim_print_verbosities[] = {
    {eDWIMPrintVerbosityNone, "none",
     "Use no verbosity when running dwim-print."},
    {eDWIMPrintVerbosityExpression, "expression",
     "Use partial verbosity when running dwim-print - display a message when "
     "`expression` evaluation is used."},
    {eDWIMPrintVerbosityFull, "full",
     "Use full verbosity when running dwim-print."},
};

static constexpr OptionEnumValueElement s_stop_show_column_values[] = {
    {
        eStopShowColumnAnsiOrCaret,
        "ansi-or-caret",
        "Highlight the stop column with ANSI terminal codes when color/ANSI "
        "mode is enabled; otherwise, fall back to using a text-only caret (^) "
        "as if \"caret-only\" mode was selected.",
    },
    {
        eStopShowColumnAnsi,
        "ansi",
        "Highlight the stop column with ANSI terminal codes when running LLDB "
        "with color/ANSI enabled.",
    },
    {
        eStopShowColumnCaret,
        "caret",
        "Highlight the stop column with a caret character (^) underneath the "
        "stop column. This method introduces a new line in source listings "
        "that display thread stop locations.",
    },
    {
        eStopShowColumnNone,
        "none",
        "Do not highlight the stop column.",
    },
};

#define LLDB_PROPERTIES_debugger
#include "CoreProperties.inc"

enum {
#define LLDB_PROPERTIES_debugger
#include "CorePropertiesEnum.inc"
};

LoadPluginCallbackType Debugger::g_load_plugin_callback = nullptr;

Status Debugger::SetPropertyValue(const ExecutionContext *exe_ctx,
                                  VarSetOperationType op,
                                  llvm::StringRef property_path,
                                  llvm::StringRef value) {
  bool is_load_script =
      (property_path == "target.load-script-from-symbol-file");
  // These properties might change how we visualize data.
  bool invalidate_data_vis = (property_path == "escape-non-printables");
  invalidate_data_vis |=
      (property_path == "target.max-zero-padding-in-float-format");
  if (invalidate_data_vis) {
    DataVisualization::ForceUpdate();
  }

  TargetSP target_sp;
  LoadScriptFromSymFile load_script_old_value = eLoadScriptFromSymFileFalse;
  if (is_load_script && exe_ctx && exe_ctx->GetTargetSP()) {
    target_sp = exe_ctx->GetTargetSP();
    load_script_old_value =
        target_sp->TargetProperties::GetLoadScriptFromSymbolFile();
  }
  Status error(Properties::SetPropertyValue(exe_ctx, op, property_path, value));
  if (error.Success()) {
    // FIXME it would be nice to have "on-change" callbacks for properties
    if (property_path == g_debugger_properties[ePropertyPrompt].name) {
      llvm::StringRef new_prompt = GetPrompt();
      std::string str = lldb_private::ansi::FormatAnsiTerminalCodes(
          new_prompt, GetUseColor());
      if (str.length())
        new_prompt = str;
      GetCommandInterpreter().UpdatePrompt(new_prompt);
      auto bytes = std::make_unique<EventDataBytes>(new_prompt);
      auto prompt_change_event_sp = std::make_shared<Event>(
          CommandInterpreter::eBroadcastBitResetPrompt, bytes.release());
      GetCommandInterpreter().BroadcastEvent(prompt_change_event_sp);
    } else if (property_path == g_debugger_properties[ePropertyUseColor].name) {
      // use-color changed. Ping the prompt so it can reset the ansi terminal
      // codes.
      SetPrompt(GetPrompt());
    } else if (property_path ==
                   g_debugger_properties[ePropertyPromptAnsiPrefix].name ||
               property_path ==
                   g_debugger_properties[ePropertyPromptAnsiSuffix].name) {
      // Prompt colors changed. Ping the prompt so it can reset the ansi
      // terminal codes.
      SetPrompt(GetPrompt());
    } else if (property_path ==
               g_debugger_properties[ePropertyUseSourceCache].name) {
      // use-source-cache changed. Wipe out the cache contents if it was
      // disabled.
      if (!GetUseSourceCache()) {
        m_source_file_cache.Clear();
      }
    } else if (is_load_script && target_sp &&
               load_script_old_value == eLoadScriptFromSymFileWarn) {
      if (target_sp->TargetProperties::GetLoadScriptFromSymbolFile() ==
          eLoadScriptFromSymFileTrue) {
        std::list<Status> errors;
        StreamString feedback_stream;
        if (!target_sp->LoadScriptingResources(errors, feedback_stream)) {
          Stream &s = GetErrorStream();
          for (auto error : errors) {
            s.Printf("%s\n", error.AsCString());
          }
          if (feedback_stream.GetSize())
            s.PutCString(feedback_stream.GetString());
        }
      }
    }
  }
  return error;
}

bool Debugger::GetAutoConfirm() const {
  constexpr uint32_t idx = ePropertyAutoConfirm;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

const FormatEntity::Entry *Debugger::GetDisassemblyFormat() const {
  constexpr uint32_t idx = ePropertyDisassemblyFormat;
  return GetPropertyAtIndexAs<const FormatEntity::Entry *>(idx);
}

const FormatEntity::Entry *Debugger::GetFrameFormat() const {
  constexpr uint32_t idx = ePropertyFrameFormat;
  return GetPropertyAtIndexAs<const FormatEntity::Entry *>(idx);
}

const FormatEntity::Entry *Debugger::GetFrameFormatUnique() const {
  constexpr uint32_t idx = ePropertyFrameFormatUnique;
  return GetPropertyAtIndexAs<const FormatEntity::Entry *>(idx);
}

uint64_t Debugger::GetStopDisassemblyMaxSize() const {
  constexpr uint32_t idx = ePropertyStopDisassemblyMaxSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value);
}

bool Debugger::GetNotifyVoid() const {
  constexpr uint32_t idx = ePropertyNotiftVoid;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

llvm::StringRef Debugger::GetPrompt() const {
  constexpr uint32_t idx = ePropertyPrompt;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetPromptAnsiPrefix() const {
  const uint32_t idx = ePropertyPromptAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetPromptAnsiSuffix() const {
  const uint32_t idx = ePropertyPromptAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

void Debugger::SetPrompt(llvm::StringRef p) {
  constexpr uint32_t idx = ePropertyPrompt;
  SetPropertyAtIndex(idx, p);
  llvm::StringRef new_prompt = GetPrompt();
  std::string str =
      lldb_private::ansi::FormatAnsiTerminalCodes(new_prompt, GetUseColor());
  if (str.length())
    new_prompt = str;
  GetCommandInterpreter().UpdatePrompt(new_prompt);
}

const FormatEntity::Entry *Debugger::GetThreadFormat() const {
  constexpr uint32_t idx = ePropertyThreadFormat;
  return GetPropertyAtIndexAs<const FormatEntity::Entry *>(idx);
}

const FormatEntity::Entry *Debugger::GetThreadStopFormat() const {
  constexpr uint32_t idx = ePropertyThreadStopFormat;
  return GetPropertyAtIndexAs<const FormatEntity::Entry *>(idx);
}

lldb::ScriptLanguage Debugger::GetScriptLanguage() const {
  const uint32_t idx = ePropertyScriptLanguage;
  return GetPropertyAtIndexAs<lldb::ScriptLanguage>(
      idx, static_cast<lldb::ScriptLanguage>(
               g_debugger_properties[idx].default_uint_value));
}

bool Debugger::SetScriptLanguage(lldb::ScriptLanguage script_lang) {
  const uint32_t idx = ePropertyScriptLanguage;
  return SetPropertyAtIndex(idx, script_lang);
}

lldb::LanguageType Debugger::GetREPLLanguage() const {
  const uint32_t idx = ePropertyREPLLanguage;
  return GetPropertyAtIndexAs<LanguageType>(idx, {});
}

bool Debugger::SetREPLLanguage(lldb::LanguageType repl_lang) {
  const uint32_t idx = ePropertyREPLLanguage;
  return SetPropertyAtIndex(idx, repl_lang);
}

uint64_t Debugger::GetTerminalWidth() const {
  const uint32_t idx = ePropertyTerminalWidth;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value);
}

bool Debugger::SetTerminalWidth(uint64_t term_width) {
  if (auto handler_sp = m_io_handler_stack.Top())
    handler_sp->TerminalSizeChanged();

  const uint32_t idx = ePropertyTerminalWidth;
  return SetPropertyAtIndex(idx, term_width);
}

bool Debugger::GetUseExternalEditor() const {
  const uint32_t idx = ePropertyUseExternalEditor;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetUseExternalEditor(bool b) {
  const uint32_t idx = ePropertyUseExternalEditor;
  return SetPropertyAtIndex(idx, b);
}

llvm::StringRef Debugger::GetExternalEditor() const {
  const uint32_t idx = ePropertyExternalEditor;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

bool Debugger::SetExternalEditor(llvm::StringRef editor) {
  const uint32_t idx = ePropertyExternalEditor;
  return SetPropertyAtIndex(idx, editor);
}

bool Debugger::GetUseColor() const {
  const uint32_t idx = ePropertyUseColor;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetUseColor(bool b) {
  const uint32_t idx = ePropertyUseColor;
  bool ret = SetPropertyAtIndex(idx, b);
  SetPrompt(GetPrompt());
  return ret;
}

bool Debugger::GetShowProgress() const {
  const uint32_t idx = ePropertyShowProgress;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetShowProgress(bool show_progress) {
  const uint32_t idx = ePropertyShowProgress;
  return SetPropertyAtIndex(idx, show_progress);
}

llvm::StringRef Debugger::GetShowProgressAnsiPrefix() const {
  const uint32_t idx = ePropertyShowProgressAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetShowProgressAnsiSuffix() const {
  const uint32_t idx = ePropertyShowProgressAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

bool Debugger::GetUseAutosuggestion() const {
  const uint32_t idx = ePropertyShowAutosuggestion;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

llvm::StringRef Debugger::GetAutosuggestionAnsiPrefix() const {
  const uint32_t idx = ePropertyShowAutosuggestionAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetAutosuggestionAnsiSuffix() const {
  const uint32_t idx = ePropertyShowAutosuggestionAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetRegexMatchAnsiPrefix() const {
  const uint32_t idx = ePropertyShowRegexMatchAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetRegexMatchAnsiSuffix() const {
  const uint32_t idx = ePropertyShowRegexMatchAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

bool Debugger::GetShowDontUsePoHint() const {
  const uint32_t idx = ePropertyShowDontUsePoHint;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::GetUseSourceCache() const {
  const uint32_t idx = ePropertyUseSourceCache;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetUseSourceCache(bool b) {
  const uint32_t idx = ePropertyUseSourceCache;
  bool ret = SetPropertyAtIndex(idx, b);
  if (!ret) {
    m_source_file_cache.Clear();
  }
  return ret;
}
bool Debugger::GetHighlightSource() const {
  const uint32_t idx = ePropertyHighlightSource;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

StopShowColumn Debugger::GetStopShowColumn() const {
  const uint32_t idx = ePropertyStopShowColumn;
  return GetPropertyAtIndexAs<lldb::StopShowColumn>(
      idx, static_cast<lldb::StopShowColumn>(
               g_debugger_properties[idx].default_uint_value));
}

llvm::StringRef Debugger::GetStopShowColumnAnsiPrefix() const {
  const uint32_t idx = ePropertyStopShowColumnAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetStopShowColumnAnsiSuffix() const {
  const uint32_t idx = ePropertyStopShowColumnAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetStopShowLineMarkerAnsiPrefix() const {
  const uint32_t idx = ePropertyStopShowLineMarkerAnsiPrefix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

llvm::StringRef Debugger::GetStopShowLineMarkerAnsiSuffix() const {
  const uint32_t idx = ePropertyStopShowLineMarkerAnsiSuffix;
  return GetPropertyAtIndexAs<llvm::StringRef>(
      idx, g_debugger_properties[idx].default_cstr_value);
}

uint64_t Debugger::GetStopSourceLineCount(bool before) const {
  const uint32_t idx =
      before ? ePropertyStopLineCountBefore : ePropertyStopLineCountAfter;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value);
}

Debugger::StopDisassemblyType Debugger::GetStopDisassemblyDisplay() const {
  const uint32_t idx = ePropertyStopDisassemblyDisplay;
  return GetPropertyAtIndexAs<Debugger::StopDisassemblyType>(
      idx, static_cast<Debugger::StopDisassemblyType>(
               g_debugger_properties[idx].default_uint_value));
}

uint64_t Debugger::GetDisassemblyLineCount() const {
  const uint32_t idx = ePropertyStopDisassemblyCount;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value);
}

bool Debugger::GetAutoOneLineSummaries() const {
  const uint32_t idx = ePropertyAutoOneLineSummaries;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::GetEscapeNonPrintables() const {
  const uint32_t idx = ePropertyEscapeNonPrintables;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::GetAutoIndent() const {
  const uint32_t idx = ePropertyAutoIndent;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetAutoIndent(bool b) {
  const uint32_t idx = ePropertyAutoIndent;
  return SetPropertyAtIndex(idx, b);
}

bool Debugger::GetPrintDecls() const {
  const uint32_t idx = ePropertyPrintDecls;
  return GetPropertyAtIndexAs<bool>(
      idx, g_debugger_properties[idx].default_uint_value != 0);
}

bool Debugger::SetPrintDecls(bool b) {
  const uint32_t idx = ePropertyPrintDecls;
  return SetPropertyAtIndex(idx, b);
}

uint64_t Debugger::GetTabSize() const {
  const uint32_t idx = ePropertyTabSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_debugger_properties[idx].default_uint_value);
}

bool Debugger::SetTabSize(uint64_t tab_size) {
  const uint32_t idx = ePropertyTabSize;
  return SetPropertyAtIndex(idx, tab_size);
}

lldb::DWIMPrintVerbosity Debugger::GetDWIMPrintVerbosity() const {
  const uint32_t idx = ePropertyDWIMPrintVerbosity;
  return GetPropertyAtIndexAs<lldb::DWIMPrintVerbosity>(
      idx, static_cast<lldb::DWIMPrintVerbosity>(
               g_debugger_properties[idx].default_uint_value));
}

#pragma mark Debugger

// const DebuggerPropertiesSP &
// Debugger::GetSettings() const
//{
//    return m_properties_sp;
//}
//

void Debugger::Initialize(LoadPluginCallbackType load_plugin_callback) {
  assert(g_debugger_list_ptr == nullptr &&
         "Debugger::Initialize called more than once!");
  g_debugger_list_mutex_ptr = new std::recursive_mutex();
  g_debugger_list_ptr = new DebuggerList();
  g_thread_pool = new llvm::DefaultThreadPool(llvm::optimal_concurrency());
  g_load_plugin_callback = load_plugin_callback;
}

void Debugger::Terminate() {
  assert(g_debugger_list_ptr &&
         "Debugger::Terminate called without a matching Debugger::Initialize!");

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    for (const auto &debugger : *g_debugger_list_ptr)
      debugger->HandleDestroyCallback();
  }

  if (g_thread_pool) {
    // The destructor will wait for all the threads to complete.
    delete g_thread_pool;
  }

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    // Clear our global list of debugger objects
    {
      std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
      for (const auto &debugger : *g_debugger_list_ptr)
        debugger->Clear();
      g_debugger_list_ptr->clear();
    }
  }
}

void Debugger::SettingsInitialize() { Target::SettingsInitialize(); }

void Debugger::SettingsTerminate() { Target::SettingsTerminate(); }

bool Debugger::LoadPlugin(const FileSpec &spec, Status &error) {
  if (g_load_plugin_callback) {
    llvm::sys::DynamicLibrary dynlib =
        g_load_plugin_callback(shared_from_this(), spec, error);
    if (dynlib.isValid()) {
      m_loaded_plugins.push_back(dynlib);
      return true;
    }
  } else {
    // The g_load_plugin_callback is registered in SBDebugger::Initialize() and
    // if the public API layer isn't available (code is linking against all of
    // the internal LLDB static libraries), then we can't load plugins
    error.SetErrorString("Public API layer is not available");
  }
  return false;
}

static FileSystem::EnumerateDirectoryResult
LoadPluginCallback(void *baton, llvm::sys::fs::file_type ft,
                   llvm::StringRef path) {
  Status error;

  static constexpr llvm::StringLiteral g_dylibext(".dylib");
  static constexpr llvm::StringLiteral g_solibext(".so");

  if (!baton)
    return FileSystem::eEnumerateDirectoryResultQuit;

  Debugger *debugger = (Debugger *)baton;

  namespace fs = llvm::sys::fs;
  // If we have a regular file, a symbolic link or unknown file type, try and
  // process the file. We must handle unknown as sometimes the directory
  // enumeration might be enumerating a file system that doesn't have correct
  // file type information.
  if (ft == fs::file_type::regular_file || ft == fs::file_type::symlink_file ||
      ft == fs::file_type::type_unknown) {
    FileSpec plugin_file_spec(path);
    FileSystem::Instance().Resolve(plugin_file_spec);

    if (plugin_file_spec.GetFileNameExtension() != g_dylibext &&
        plugin_file_spec.GetFileNameExtension() != g_solibext) {
      return FileSystem::eEnumerateDirectoryResultNext;
    }

    Status plugin_load_error;
    debugger->LoadPlugin(plugin_file_spec, plugin_load_error);

    return FileSystem::eEnumerateDirectoryResultNext;
  } else if (ft == fs::file_type::directory_file ||
             ft == fs::file_type::symlink_file ||
             ft == fs::file_type::type_unknown) {
    // Try and recurse into anything that a directory or symbolic link. We must
    // also do this for unknown as sometimes the directory enumeration might be
    // enumerating a file system that doesn't have correct file type
    // information.
    return FileSystem::eEnumerateDirectoryResultEnter;
  }

  return FileSystem::eEnumerateDirectoryResultNext;
}

void Debugger::InstanceInitialize() {
  const bool find_directories = true;
  const bool find_files = true;
  const bool find_other = true;
  char dir_path[PATH_MAX];
  if (FileSpec dir_spec = HostInfo::GetSystemPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, this);
    }
  }

  if (FileSpec dir_spec = HostInfo::GetUserPluginDir()) {
    if (FileSystem::Instance().Exists(dir_spec) &&
        dir_spec.GetPath(dir_path, sizeof(dir_path))) {
      FileSystem::Instance().EnumerateDirectory(dir_path, find_directories,
                                                find_files, find_other,
                                                LoadPluginCallback, this);
    }
  }

  PluginManager::DebuggerInitialize(*this);
}

DebuggerSP Debugger::CreateInstance(lldb::LogOutputCallback log_callback,
                                    void *baton) {
  DebuggerSP debugger_sp(new Debugger(log_callback, baton));
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    g_debugger_list_ptr->push_back(debugger_sp);
  }
  debugger_sp->InstanceInitialize();
  return debugger_sp;
}

void Debugger::HandleDestroyCallback() {
  const lldb::user_id_t user_id = GetID();
  // Invoke and remove all the callbacks in an FIFO order. Callbacks which are
  // added during this loop will be appended, invoked and then removed last.
  // Callbacks which are removed during this loop will not be invoked.
  while (true) {
    DestroyCallbackInfo callback_info;
    {
      std::lock_guard<std::mutex> guard(m_destroy_callback_mutex);
      if (m_destroy_callbacks.empty())
        break;
      // Pop the first item in the list
      callback_info = m_destroy_callbacks.front();
      m_destroy_callbacks.erase(m_destroy_callbacks.begin());
    }
    // Call the destroy callback with user id and baton
    callback_info.callback(user_id, callback_info.baton);
  }
}

void Debugger::Destroy(DebuggerSP &debugger_sp) {
  if (!debugger_sp)
    return;

  debugger_sp->HandleDestroyCallback();
  CommandInterpreter &cmd_interpreter = debugger_sp->GetCommandInterpreter();

  if (cmd_interpreter.GetSaveSessionOnQuit()) {
    CommandReturnObject result(debugger_sp->GetUseColor());
    cmd_interpreter.SaveTranscript(result);
    if (result.Succeeded())
      (*debugger_sp->GetAsyncOutputStream()) << result.GetOutputData() << '\n';
    else
      (*debugger_sp->GetAsyncErrorStream()) << result.GetErrorData() << '\n';
  }

  debugger_sp->Clear();

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      if ((*pos).get() == debugger_sp.get()) {
        g_debugger_list_ptr->erase(pos);
        return;
      }
    }
  }
}

DebuggerSP
Debugger::FindDebuggerWithInstanceName(llvm::StringRef instance_name) {
  if (!g_debugger_list_ptr || !g_debugger_list_mutex_ptr)
    return DebuggerSP();

  std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
  for (const DebuggerSP &debugger_sp : *g_debugger_list_ptr) {
    if (!debugger_sp)
      continue;

    if (llvm::StringRef(debugger_sp->GetInstanceName()) == instance_name)
      return debugger_sp;
  }
  return DebuggerSP();
}

TargetSP Debugger::FindTargetWithProcessID(lldb::pid_t pid) {
  TargetSP target_sp;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      target_sp = (*pos)->GetTargetList().FindTargetWithProcessID(pid);
      if (target_sp)
        break;
    }
  }
  return target_sp;
}

TargetSP Debugger::FindTargetWithProcess(Process *process) {
  TargetSP target_sp;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      target_sp = (*pos)->GetTargetList().FindTargetWithProcess(process);
      if (target_sp)
        break;
    }
  }
  return target_sp;
}

llvm::StringRef Debugger::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.debugger");
  return class_name;
}

Debugger::Debugger(lldb::LogOutputCallback log_callback, void *baton)
    : UserID(g_unique_id++),
      Properties(std::make_shared<OptionValueProperties>()),
      m_input_file_sp(std::make_shared<NativeFile>(stdin, false)),
      m_output_stream_sp(std::make_shared<StreamFile>(stdout, false)),
      m_error_stream_sp(std::make_shared<StreamFile>(stderr, false)),
      m_input_recorder(nullptr),
      m_broadcaster_manager_sp(BroadcasterManager::MakeBroadcasterManager()),
      m_terminal_state(), m_target_list(*this), m_platform_list(),
      m_listener_sp(Listener::MakeListener("lldb.Debugger")),
      m_source_manager_up(), m_source_file_cache(),
      m_command_interpreter_up(
          std::make_unique<CommandInterpreter>(*this, false)),
      m_io_handler_stack(),
      m_instance_name(llvm::formatv("debugger_{0}", GetID()).str()),
      m_loaded_plugins(), m_event_handler_thread(), m_io_handler_thread(),
      m_sync_broadcaster(nullptr, "lldb.debugger.sync"),
      m_broadcaster(m_broadcaster_manager_sp,
                    GetStaticBroadcasterClass().str()),
      m_forward_listener_sp(), m_clear_once() {
  // Initialize the debugger properties as early as possible as other parts of
  // LLDB will start querying them during construction.
  m_collection_sp->Initialize(g_debugger_properties);
  m_collection_sp->AppendProperty(
      "target", "Settings specify to debugging targets.", true,
      Target::GetGlobalProperties().GetValueProperties());
  m_collection_sp->AppendProperty(
      "platform", "Platform settings.", true,
      Platform::GetGlobalPlatformProperties().GetValueProperties());
  m_collection_sp->AppendProperty(
      "symbols", "Symbol lookup and cache settings.", true,
      ModuleList::GetGlobalModuleListProperties().GetValueProperties());
  m_collection_sp->AppendProperty(
      LanguageProperties::GetSettingName(), "Language settings.", true,
      Language::GetGlobalLanguageProperties().GetValueProperties());
  if (m_command_interpreter_up) {
    m_collection_sp->AppendProperty(
        "interpreter",
        "Settings specify to the debugger's command interpreter.", true,
        m_command_interpreter_up->GetValueProperties());
  }
  if (log_callback)
    m_callback_handler_sp =
        std::make_shared<CallbackLogHandler>(log_callback, baton);
  m_command_interpreter_up->Initialize();
  // Always add our default platform to the platform list
  PlatformSP default_platform_sp(Platform::GetHostPlatform());
  assert(default_platform_sp);
  m_platform_list.Append(default_platform_sp, true);

  // Create the dummy target.
  {
    ArchSpec arch(Target::GetDefaultArchitecture());
    if (!arch.IsValid())
      arch = HostInfo::GetArchitecture();
    assert(arch.IsValid() && "No valid default or host archspec");
    const bool is_dummy_target = true;
    m_dummy_target_sp.reset(
        new Target(*this, arch, default_platform_sp, is_dummy_target));
  }
  assert(m_dummy_target_sp.get() && "Couldn't construct dummy target?");

  OptionValueUInt64 *term_width =
      m_collection_sp->GetPropertyAtIndexAsOptionValueUInt64(
          ePropertyTerminalWidth);
  term_width->SetMinimumValue(10);
  term_width->SetMaximumValue(1024);

  // Turn off use-color if this is a dumb terminal.
  const char *term = getenv("TERM");
  if (term && !strcmp(term, "dumb"))
    SetUseColor(false);
  // Turn off use-color if we don't write to a terminal with color support.
  if (!GetOutputFile().GetIsTerminalWithColors())
    SetUseColor(false);

  if (Diagnostics::Enabled()) {
    m_diagnostics_callback_id = Diagnostics::Instance().AddCallback(
        [this](const FileSpec &dir) -> llvm::Error {
          for (auto &entry : m_stream_handlers) {
            llvm::StringRef log_path = entry.first();
            llvm::StringRef file_name = llvm::sys::path::filename(log_path);
            FileSpec destination = dir.CopyByAppendingPathComponent(file_name);
            std::error_code ec =
                llvm::sys::fs::copy_file(log_path, destination.GetPath());
            if (ec)
              return llvm::errorCodeToError(ec);
          }
          return llvm::Error::success();
        });
  }

#if defined(_WIN32) && defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
  // Enabling use of ANSI color codes because LLDB is using them to highlight
  // text.
  llvm::sys::Process::UseANSIEscapeCodes(true);
#endif
}

Debugger::~Debugger() { Clear(); }

void Debugger::Clear() {
  // Make sure we call this function only once. With the C++ global destructor
  // chain having a list of debuggers and with code that can be running on
  // other threads, we need to ensure this doesn't happen multiple times.
  //
  // The following functions call Debugger::Clear():
  //     Debugger::~Debugger();
  //     static void Debugger::Destroy(lldb::DebuggerSP &debugger_sp);
  //     static void Debugger::Terminate();
  llvm::call_once(m_clear_once, [this]() {
    ClearIOHandlers();
    StopIOHandlerThread();
    StopEventHandlerThread();
    m_listener_sp->Clear();
    for (TargetSP target_sp : m_target_list.Targets()) {
      if (target_sp) {
        if (ProcessSP process_sp = target_sp->GetProcessSP())
          process_sp->Finalize(false /* not destructing */);
        target_sp->Destroy();
      }
    }
    m_broadcaster_manager_sp->Clear();

    // Close the input file _before_ we close the input read communications
    // class as it does NOT own the input file, our m_input_file does.
    m_terminal_state.Clear();
    GetInputFile().Close();

    m_command_interpreter_up->Clear();

    if (Diagnostics::Enabled())
      Diagnostics::Instance().RemoveCallback(m_diagnostics_callback_id);
  });
}

bool Debugger::GetAsyncExecution() {
  return !m_command_interpreter_up->GetSynchronous();
}

void Debugger::SetAsyncExecution(bool async_execution) {
  m_command_interpreter_up->SetSynchronous(!async_execution);
}

repro::DataRecorder *Debugger::GetInputRecorder() { return m_input_recorder; }

static inline int OpenPipe(int fds[2], std::size_t size) {
#ifdef _WIN32
  return _pipe(fds, size, O_BINARY);
#else
  (void)size;
  return pipe(fds);
#endif
}

Status Debugger::SetInputString(const char *data) {
  Status result;
  enum PIPES { READ, WRITE }; // Indexes for the read and write fds
  int fds[2] = {-1, -1};

  if (data == nullptr) {
    result.SetErrorString("String data is null");
    return result;
  }

  size_t size = strlen(data);
  if (size == 0) {
    result.SetErrorString("String data is empty");
    return result;
  }

  if (OpenPipe(fds, size) != 0) {
    result.SetErrorString(
        "can't create pipe file descriptors for LLDB commands");
    return result;
  }

  int r = write(fds[WRITE], data, size);
  (void)r;
  // Close the write end of the pipe, so that the command interpreter will exit
  // when it consumes all the data.
  llvm::sys::Process::SafelyCloseFileDescriptor(fds[WRITE]);

  // Open the read file descriptor as a FILE * that we can return as an input
  // handle.
  FILE *commands_file = fdopen(fds[READ], "rb");
  if (commands_file == nullptr) {
    result.SetErrorStringWithFormat("fdopen(%i, \"rb\") failed (errno = %i) "
                                    "when trying to open LLDB commands pipe",
                                    fds[READ], errno);
    llvm::sys::Process::SafelyCloseFileDescriptor(fds[READ]);
    return result;
  }

  SetInputFile((FileSP)std::make_shared<NativeFile>(commands_file, true));
  return result;
}

void Debugger::SetInputFile(FileSP file_sp) {
  assert(file_sp && file_sp->IsValid());
  m_input_file_sp = std::move(file_sp);
  // Save away the terminal state if that is relevant, so that we can restore
  // it in RestoreInputState.
  SaveInputTerminalState();
}

void Debugger::SetOutputFile(FileSP file_sp) {
  assert(file_sp && file_sp->IsValid());
  m_output_stream_sp = std::make_shared<StreamFile>(file_sp);
}

void Debugger::SetErrorFile(FileSP file_sp) {
  assert(file_sp && file_sp->IsValid());
  m_error_stream_sp = std::make_shared<StreamFile>(file_sp);
}

void Debugger::SaveInputTerminalState() {
  int fd = GetInputFile().GetDescriptor();
  if (fd != File::kInvalidDescriptor)
    m_terminal_state.Save(fd, true);
}

void Debugger::RestoreInputTerminalState() { m_terminal_state.Restore(); }

ExecutionContext Debugger::GetSelectedExecutionContext() {
  bool adopt_selected = true;
  ExecutionContextRef exe_ctx_ref(GetSelectedTarget().get(), adopt_selected);
  return ExecutionContext(exe_ctx_ref);
}

void Debugger::DispatchInputInterrupt() {
  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());
  IOHandlerSP reader_sp(m_io_handler_stack.Top());
  if (reader_sp)
    reader_sp->Interrupt();
}

void Debugger::DispatchInputEndOfFile() {
  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());
  IOHandlerSP reader_sp(m_io_handler_stack.Top());
  if (reader_sp)
    reader_sp->GotEOF();
}

void Debugger::ClearIOHandlers() {
  // The bottom input reader should be the main debugger input reader.  We do
  // not want to close that one here.
  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());
  while (m_io_handler_stack.GetSize() > 1) {
    IOHandlerSP reader_sp(m_io_handler_stack.Top());
    if (reader_sp)
      PopIOHandler(reader_sp);
  }
}

void Debugger::RunIOHandlers() {
  IOHandlerSP reader_sp = m_io_handler_stack.Top();
  while (true) {
    if (!reader_sp)
      break;

    reader_sp->Run();
    {
      std::lock_guard<std::recursive_mutex> guard(
          m_io_handler_synchronous_mutex);

      // Remove all input readers that are done from the top of the stack
      while (true) {
        IOHandlerSP top_reader_sp = m_io_handler_stack.Top();
        if (top_reader_sp && top_reader_sp->GetIsDone())
          PopIOHandler(top_reader_sp);
        else
          break;
      }
      reader_sp = m_io_handler_stack.Top();
    }
  }
  ClearIOHandlers();
}

void Debugger::RunIOHandlerSync(const IOHandlerSP &reader_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_io_handler_synchronous_mutex);

  PushIOHandler(reader_sp);
  IOHandlerSP top_reader_sp = reader_sp;

  while (top_reader_sp) {
    top_reader_sp->Run();

    // Don't unwind past the starting point.
    if (top_reader_sp.get() == reader_sp.get()) {
      if (PopIOHandler(reader_sp))
        break;
    }

    // If we pushed new IO handlers, pop them if they're done or restart the
    // loop to run them if they're not.
    while (true) {
      top_reader_sp = m_io_handler_stack.Top();
      if (top_reader_sp && top_reader_sp->GetIsDone()) {
        PopIOHandler(top_reader_sp);
        // Don't unwind past the starting point.
        if (top_reader_sp.get() == reader_sp.get())
          return;
      } else {
        break;
      }
    }
  }
}

bool Debugger::IsTopIOHandler(const lldb::IOHandlerSP &reader_sp) {
  return m_io_handler_stack.IsTop(reader_sp);
}

bool Debugger::CheckTopIOHandlerTypes(IOHandler::Type top_type,
                                      IOHandler::Type second_top_type) {
  return m_io_handler_stack.CheckTopIOHandlerTypes(top_type, second_top_type);
}

void Debugger::PrintAsync(const char *s, size_t len, bool is_stdout) {
  bool printed = m_io_handler_stack.PrintAsync(s, len, is_stdout);
  if (!printed) {
    lldb::StreamFileSP stream =
        is_stdout ? m_output_stream_sp : m_error_stream_sp;
    stream->Write(s, len);
  }
}

llvm::StringRef Debugger::GetTopIOHandlerControlSequence(char ch) {
  return m_io_handler_stack.GetTopIOHandlerControlSequence(ch);
}

const char *Debugger::GetIOHandlerCommandPrefix() {
  return m_io_handler_stack.GetTopIOHandlerCommandPrefix();
}

const char *Debugger::GetIOHandlerHelpPrologue() {
  return m_io_handler_stack.GetTopIOHandlerHelpPrologue();
}

bool Debugger::RemoveIOHandler(const IOHandlerSP &reader_sp) {
  return PopIOHandler(reader_sp);
}

void Debugger::RunIOHandlerAsync(const IOHandlerSP &reader_sp,
                                 bool cancel_top_handler) {
  PushIOHandler(reader_sp, cancel_top_handler);
}

void Debugger::AdoptTopIOHandlerFilesIfInvalid(FileSP &in, StreamFileSP &out,
                                               StreamFileSP &err) {
  // Before an IOHandler runs, it must have in/out/err streams. This function
  // is called when one ore more of the streams are nullptr. We use the top
  // input reader's in/out/err streams, or fall back to the debugger file
  // handles, or we fall back onto stdin/stdout/stderr as a last resort.

  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());
  IOHandlerSP top_reader_sp(m_io_handler_stack.Top());
  // If no STDIN has been set, then set it appropriately
  if (!in || !in->IsValid()) {
    if (top_reader_sp)
      in = top_reader_sp->GetInputFileSP();
    else
      in = GetInputFileSP();
    // If there is nothing, use stdin
    if (!in)
      in = std::make_shared<NativeFile>(stdin, false);
  }
  // If no STDOUT has been set, then set it appropriately
  if (!out || !out->GetFile().IsValid()) {
    if (top_reader_sp)
      out = top_reader_sp->GetOutputStreamFileSP();
    else
      out = GetOutputStreamSP();
    // If there is nothing, use stdout
    if (!out)
      out = std::make_shared<StreamFile>(stdout, false);
  }
  // If no STDERR has been set, then set it appropriately
  if (!err || !err->GetFile().IsValid()) {
    if (top_reader_sp)
      err = top_reader_sp->GetErrorStreamFileSP();
    else
      err = GetErrorStreamSP();
    // If there is nothing, use stderr
    if (!err)
      err = std::make_shared<StreamFile>(stderr, false);
  }
}

void Debugger::PushIOHandler(const IOHandlerSP &reader_sp,
                             bool cancel_top_handler) {
  if (!reader_sp)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());

  // Get the current top input reader...
  IOHandlerSP top_reader_sp(m_io_handler_stack.Top());

  // Don't push the same IO handler twice...
  if (reader_sp == top_reader_sp)
    return;

  // Push our new input reader
  m_io_handler_stack.Push(reader_sp);
  reader_sp->Activate();

  // Interrupt the top input reader to it will exit its Run() function and let
  // this new input reader take over
  if (top_reader_sp) {
    top_reader_sp->Deactivate();
    if (cancel_top_handler)
      top_reader_sp->Cancel();
  }
}

bool Debugger::PopIOHandler(const IOHandlerSP &pop_reader_sp) {
  if (!pop_reader_sp)
    return false;

  std::lock_guard<std::recursive_mutex> guard(m_io_handler_stack.GetMutex());

  // The reader on the stop of the stack is done, so let the next read on the
  // stack refresh its prompt and if there is one...
  if (m_io_handler_stack.IsEmpty())
    return false;

  IOHandlerSP reader_sp(m_io_handler_stack.Top());

  if (pop_reader_sp != reader_sp)
    return false;

  reader_sp->Deactivate();
  reader_sp->Cancel();
  m_io_handler_stack.Pop();

  reader_sp = m_io_handler_stack.Top();
  if (reader_sp)
    reader_sp->Activate();

  return true;
}

StreamSP Debugger::GetAsyncOutputStream() {
  return std::make_shared<StreamAsynchronousIO>(*this, true, GetUseColor());
}

StreamSP Debugger::GetAsyncErrorStream() {
  return std::make_shared<StreamAsynchronousIO>(*this, false, GetUseColor());
}

void Debugger::RequestInterrupt() {
  std::lock_guard<std::mutex> guard(m_interrupt_mutex);
  m_interrupt_requested++;
}

void Debugger::CancelInterruptRequest() {
  std::lock_guard<std::mutex> guard(m_interrupt_mutex);
  if (m_interrupt_requested > 0)
    m_interrupt_requested--;
}

bool Debugger::InterruptRequested() {
  // This is the one we should call internally.  This will return true either
  // if there's a debugger interrupt and we aren't on the IOHandler thread,
  // or if we are on the IOHandler thread and there's a CommandInterpreter
  // interrupt.
  if (!IsIOHandlerThreadCurrentThread()) {
    std::lock_guard<std::mutex> guard(m_interrupt_mutex);
    return m_interrupt_requested != 0;
  }
  return GetCommandInterpreter().WasInterrupted();
}

Debugger::InterruptionReport::InterruptionReport(
    std::string function_name, const llvm::formatv_object_base &payload)
    : m_function_name(std::move(function_name)),
      m_interrupt_time(std::chrono::system_clock::now()),
      m_thread_id(llvm::get_threadid()) {
  llvm::raw_string_ostream desc(m_description);
  desc << payload << "\n";
}

void Debugger::ReportInterruption(const InterruptionReport &report) {
  // For now, just log the description:
  Log *log = GetLog(LLDBLog::Host);
  LLDB_LOG(log, "Interruption: {0}", report.m_description);
}

Debugger::DebuggerList Debugger::DebuggersRequestingInterruption() {
  DebuggerList result;
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    for (auto debugger_sp : *g_debugger_list_ptr) {
      if (debugger_sp->InterruptRequested())
        result.push_back(debugger_sp);
    }
  }
  return result;
}

size_t Debugger::GetNumDebuggers() {
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    return g_debugger_list_ptr->size();
  }
  return 0;
}

lldb::DebuggerSP Debugger::GetDebuggerAtIndex(size_t index) {
  DebuggerSP debugger_sp;

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    if (index < g_debugger_list_ptr->size())
      debugger_sp = g_debugger_list_ptr->at(index);
  }

  return debugger_sp;
}

DebuggerSP Debugger::FindDebuggerWithID(lldb::user_id_t id) {
  DebuggerSP debugger_sp;

  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos) {
      if ((*pos)->GetID() == id) {
        debugger_sp = *pos;
        break;
      }
    }
  }
  return debugger_sp;
}

bool Debugger::FormatDisassemblerAddress(const FormatEntity::Entry *format,
                                         const SymbolContext *sc,
                                         const SymbolContext *prev_sc,
                                         const ExecutionContext *exe_ctx,
                                         const Address *addr, Stream &s) {
  FormatEntity::Entry format_entry;

  if (format == nullptr) {
    if (exe_ctx != nullptr && exe_ctx->HasTargetScope())
      format = exe_ctx->GetTargetRef().GetDebugger().GetDisassemblyFormat();
    if (format == nullptr) {
      FormatEntity::Parse("${addr}: ", format_entry);
      format = &format_entry;
    }
  }
  bool function_changed = false;
  bool initial_function = false;
  if (prev_sc && (prev_sc->function || prev_sc->symbol)) {
    if (sc && (sc->function || sc->symbol)) {
      if (prev_sc->symbol && sc->symbol) {
        if (!sc->symbol->Compare(prev_sc->symbol->GetName(),
                                 prev_sc->symbol->GetType())) {
          function_changed = true;
        }
      } else if (prev_sc->function && sc->function) {
        if (prev_sc->function->GetMangled() != sc->function->GetMangled()) {
          function_changed = true;
        }
      }
    }
  }
  // The first context on a list of instructions will have a prev_sc that has
  // no Function or Symbol -- if SymbolContext had an IsValid() method, it
  // would return false.  But we do get a prev_sc pointer.
  if ((sc && (sc->function || sc->symbol)) && prev_sc &&
      (prev_sc->function == nullptr && prev_sc->symbol == nullptr)) {
    initial_function = true;
  }
  return FormatEntity::Format(*format, s, sc, exe_ctx, addr, nullptr,
                              function_changed, initial_function);
}

void Debugger::AssertCallback(llvm::StringRef message,
                              llvm::StringRef backtrace,
                              llvm::StringRef prompt) {
  Debugger::ReportError(
      llvm::formatv("{0}\n{1}{2}", message, backtrace, prompt).str());
}

void Debugger::SetLoggingCallback(lldb::LogOutputCallback log_callback,
                                  void *baton) {
  // For simplicity's sake, I am not going to deal with how to close down any
  // open logging streams, I just redirect everything from here on out to the
  // callback.
  m_callback_handler_sp =
      std::make_shared<CallbackLogHandler>(log_callback, baton);
}

void Debugger::SetDestroyCallback(
    lldb_private::DebuggerDestroyCallback destroy_callback, void *baton) {
  std::lock_guard<std::mutex> guard(m_destroy_callback_mutex);
  m_destroy_callbacks.clear();
  const lldb::callback_token_t token = m_destroy_callback_next_token++;
  m_destroy_callbacks.emplace_back(token, destroy_callback, baton);
}

lldb::callback_token_t Debugger::AddDestroyCallback(
    lldb_private::DebuggerDestroyCallback destroy_callback, void *baton) {
  std::lock_guard<std::mutex> guard(m_destroy_callback_mutex);
  const lldb::callback_token_t token = m_destroy_callback_next_token++;
  m_destroy_callbacks.emplace_back(token, destroy_callback, baton);
  return token;
}

bool Debugger::RemoveDestroyCallback(lldb::callback_token_t token) {
  std::lock_guard<std::mutex> guard(m_destroy_callback_mutex);
  for (auto it = m_destroy_callbacks.begin(); it != m_destroy_callbacks.end();
       ++it) {
    if (it->token == token) {
      m_destroy_callbacks.erase(it);
      return true;
    }
  }
  return false;
}

static void PrivateReportProgress(Debugger &debugger, uint64_t progress_id,
                                  std::string title, std::string details,
                                  uint64_t completed, uint64_t total,
                                  bool is_debugger_specific,
                                  uint32_t progress_broadcast_bit) {
  // Only deliver progress events if we have any progress listeners.
  if (!debugger.GetBroadcaster().EventTypeHasListeners(progress_broadcast_bit))
    return;

  EventSP event_sp(new Event(
      progress_broadcast_bit,
      new ProgressEventData(progress_id, std::move(title), std::move(details),
                            completed, total, is_debugger_specific)));
  debugger.GetBroadcaster().BroadcastEvent(event_sp);
}

void Debugger::ReportProgress(uint64_t progress_id, std::string title,
                              std::string details, uint64_t completed,
                              uint64_t total,
                              std::optional<lldb::user_id_t> debugger_id,
                              uint32_t progress_category_bit) {
  // Check if this progress is for a specific debugger.
  if (debugger_id) {
    // It is debugger specific, grab it and deliver the event if the debugger
    // still exists.
    DebuggerSP debugger_sp = FindDebuggerWithID(*debugger_id);
    if (debugger_sp)
      PrivateReportProgress(*debugger_sp, progress_id, std::move(title),
                            std::move(details), completed, total,
                            /*is_debugger_specific*/ true,
                            progress_category_bit);
    return;
  }
  // The progress event is not debugger specific, iterate over all debuggers
  // and deliver a progress event to each one.
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    DebuggerList::iterator pos, end = g_debugger_list_ptr->end();
    for (pos = g_debugger_list_ptr->begin(); pos != end; ++pos)
      PrivateReportProgress(*(*pos), progress_id, title, details, completed,
                            total, /*is_debugger_specific*/ false,
                            progress_category_bit);
  }
}

static void PrivateReportDiagnostic(Debugger &debugger, Severity severity,
                                    std::string message,
                                    bool debugger_specific) {
  uint32_t event_type = 0;
  switch (severity) {
  case eSeverityInfo:
    assert(false && "eSeverityInfo should not be broadcast");
    return;
  case eSeverityWarning:
    event_type = lldb::eBroadcastBitWarning;
    break;
  case eSeverityError:
    event_type = lldb::eBroadcastBitError;
    break;
  }

  Broadcaster &broadcaster = debugger.GetBroadcaster();
  if (!broadcaster.EventTypeHasListeners(event_type)) {
    // Diagnostics are too important to drop. If nobody is listening, print the
    // diagnostic directly to the debugger's error stream.
    DiagnosticEventData event_data(severity, std::move(message),
                                   debugger_specific);
    StreamSP stream = debugger.GetAsyncErrorStream();
    event_data.Dump(stream.get());
    return;
  }
  EventSP event_sp = std::make_shared<Event>(
      event_type,
      new DiagnosticEventData(severity, std::move(message), debugger_specific));
  broadcaster.BroadcastEvent(event_sp);
}

void Debugger::ReportDiagnosticImpl(Severity severity, std::string message,
                                    std::optional<lldb::user_id_t> debugger_id,
                                    std::once_flag *once) {
  auto ReportDiagnosticLambda = [&]() {
    // Always log diagnostics to the system log.
    Host::SystemLog(severity, message);

    // The diagnostic subsystem is optional but we still want to broadcast
    // events when it's disabled.
    if (Diagnostics::Enabled())
      Diagnostics::Instance().Report(message);

    // We don't broadcast info events.
    if (severity == lldb::eSeverityInfo)
      return;

    // Check if this diagnostic is for a specific debugger.
    if (debugger_id) {
      // It is debugger specific, grab it and deliver the event if the debugger
      // still exists.
      DebuggerSP debugger_sp = FindDebuggerWithID(*debugger_id);
      if (debugger_sp)
        PrivateReportDiagnostic(*debugger_sp, severity, std::move(message),
                                true);
      return;
    }
    // The diagnostic event is not debugger specific, iterate over all debuggers
    // and deliver a diagnostic event to each one.
    if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
      std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
      for (const auto &debugger : *g_debugger_list_ptr)
        PrivateReportDiagnostic(*debugger, severity, message, false);
    }
  };

  if (once)
    std::call_once(*once, ReportDiagnosticLambda);
  else
    ReportDiagnosticLambda();
}

void Debugger::ReportWarning(std::string message,
                             std::optional<lldb::user_id_t> debugger_id,
                             std::once_flag *once) {
  ReportDiagnosticImpl(eSeverityWarning, std::move(message), debugger_id, once);
}

void Debugger::ReportError(std::string message,
                           std::optional<lldb::user_id_t> debugger_id,
                           std::once_flag *once) {
  ReportDiagnosticImpl(eSeverityError, std::move(message), debugger_id, once);
}

void Debugger::ReportInfo(std::string message,
                          std::optional<lldb::user_id_t> debugger_id,
                          std::once_flag *once) {
  ReportDiagnosticImpl(eSeverityInfo, std::move(message), debugger_id, once);
}

void Debugger::ReportSymbolChange(const ModuleSpec &module_spec) {
  if (g_debugger_list_ptr && g_debugger_list_mutex_ptr) {
    std::lock_guard<std::recursive_mutex> guard(*g_debugger_list_mutex_ptr);
    for (DebuggerSP debugger_sp : *g_debugger_list_ptr) {
      EventSP event_sp = std::make_shared<Event>(
          lldb::eBroadcastSymbolChange,
          new SymbolChangeEventData(debugger_sp, module_spec));
      debugger_sp->GetBroadcaster().BroadcastEvent(event_sp);
    }
  }
}

static std::shared_ptr<LogHandler>
CreateLogHandler(LogHandlerKind log_handler_kind, int fd, bool should_close,
                 size_t buffer_size) {
  switch (log_handler_kind) {
  case eLogHandlerStream:
    return std::make_shared<StreamLogHandler>(fd, should_close, buffer_size);
  case eLogHandlerCircular:
    return std::make_shared<RotatingLogHandler>(buffer_size);
  case eLogHandlerSystem:
    return std::make_shared<SystemLogHandler>();
  case eLogHandlerCallback:
    return {};
  }
  return {};
}

bool Debugger::EnableLog(llvm::StringRef channel,
                         llvm::ArrayRef<const char *> categories,
                         llvm::StringRef log_file, uint32_t log_options,
                         size_t buffer_size, LogHandlerKind log_handler_kind,
                         llvm::raw_ostream &error_stream) {

  std::shared_ptr<LogHandler> log_handler_sp;
  if (m_callback_handler_sp) {
    log_handler_sp = m_callback_handler_sp;
    // For now when using the callback mode you always get thread & timestamp.
    log_options |=
        LLDB_LOG_OPTION_PREPEND_TIMESTAMP | LLDB_LOG_OPTION_PREPEND_THREAD_NAME;
  } else if (log_file.empty()) {
    log_handler_sp =
        CreateLogHandler(log_handler_kind, GetOutputFile().GetDescriptor(),
                         /*should_close=*/false, buffer_size);
  } else {
    auto pos = m_stream_handlers.find(log_file);
    if (pos != m_stream_handlers.end())
      log_handler_sp = pos->second.lock();
    if (!log_handler_sp) {
      File::OpenOptions flags =
          File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate;
      if (log_options & LLDB_LOG_OPTION_APPEND)
        flags |= File::eOpenOptionAppend;
      else
        flags |= File::eOpenOptionTruncate;
      llvm::Expected<FileUP> file = FileSystem::Instance().Open(
          FileSpec(log_file), flags, lldb::eFilePermissionsFileDefault, false);
      if (!file) {
        error_stream << "Unable to open log file '" << log_file
                     << "': " << llvm::toString(file.takeError()) << "\n";
        return false;
      }

      log_handler_sp =
          CreateLogHandler(log_handler_kind, (*file)->GetDescriptor(),
                           /*should_close=*/true, buffer_size);
      m_stream_handlers[log_file] = log_handler_sp;
    }
  }
  assert(log_handler_sp);

  if (log_options == 0)
    log_options = LLDB_LOG_OPTION_PREPEND_THREAD_NAME;

  return Log::EnableLogChannel(log_handler_sp, log_options, channel, categories,
                               error_stream);
}

ScriptInterpreter *
Debugger::GetScriptInterpreter(bool can_create,
                               std::optional<lldb::ScriptLanguage> language) {
  std::lock_guard<std::recursive_mutex> locker(m_script_interpreter_mutex);
  lldb::ScriptLanguage script_language =
      language ? *language : GetScriptLanguage();

  if (!m_script_interpreters[script_language]) {
    if (!can_create)
      return nullptr;
    m_script_interpreters[script_language] =
        PluginManager::GetScriptInterpreterForLanguage(script_language, *this);
  }

  return m_script_interpreters[script_language].get();
}

SourceManager &Debugger::GetSourceManager() {
  if (!m_source_manager_up)
    m_source_manager_up = std::make_unique<SourceManager>(shared_from_this());
  return *m_source_manager_up;
}

// This function handles events that were broadcast by the process.
void Debugger::HandleBreakpointEvent(const EventSP &event_sp) {
  using namespace lldb;
  const uint32_t event_type =
      Breakpoint::BreakpointEventData::GetBreakpointEventTypeFromEvent(
          event_sp);

  //    if (event_type & eBreakpointEventTypeAdded
  //        || event_type & eBreakpointEventTypeRemoved
  //        || event_type & eBreakpointEventTypeEnabled
  //        || event_type & eBreakpointEventTypeDisabled
  //        || event_type & eBreakpointEventTypeCommandChanged
  //        || event_type & eBreakpointEventTypeConditionChanged
  //        || event_type & eBreakpointEventTypeIgnoreChanged
  //        || event_type & eBreakpointEventTypeLocationsResolved)
  //    {
  //        // Don't do anything about these events, since the breakpoint
  //        commands already echo these actions.
  //    }
  //
  if (event_type & eBreakpointEventTypeLocationsAdded) {
    uint32_t num_new_locations =
        Breakpoint::BreakpointEventData::GetNumBreakpointLocationsFromEvent(
            event_sp);
    if (num_new_locations > 0) {
      BreakpointSP breakpoint =
          Breakpoint::BreakpointEventData::GetBreakpointFromEvent(event_sp);
      StreamSP output_sp(GetAsyncOutputStream());
      if (output_sp) {
        output_sp->Printf("%d location%s added to breakpoint %d\n",
                          num_new_locations, num_new_locations == 1 ? "" : "s",
                          breakpoint->GetID());
        output_sp->Flush();
      }
    }
  }
  //    else if (event_type & eBreakpointEventTypeLocationsRemoved)
  //    {
  //        // These locations just get disabled, not sure it is worth spamming
  //        folks about this on the command line.
  //    }
  //    else if (event_type & eBreakpointEventTypeLocationsResolved)
  //    {
  //        // This might be an interesting thing to note, but I'm going to
  //        leave it quiet for now, it just looked noisy.
  //    }
}

void Debugger::FlushProcessOutput(Process &process, bool flush_stdout,
                                  bool flush_stderr) {
  const auto &flush = [&](Stream &stream,
                          size_t (Process::*get)(char *, size_t, Status &)) {
    Status error;
    size_t len;
    char buffer[1024];
    while ((len = (process.*get)(buffer, sizeof(buffer), error)) > 0)
      stream.Write(buffer, len);
    stream.Flush();
  };

  std::lock_guard<std::mutex> guard(m_output_flush_mutex);
  if (flush_stdout)
    flush(*GetAsyncOutputStream(), &Process::GetSTDOUT);
  if (flush_stderr)
    flush(*GetAsyncErrorStream(), &Process::GetSTDERR);
}

// This function handles events that were broadcast by the process.
void Debugger::HandleProcessEvent(const EventSP &event_sp) {
  using namespace lldb;
  const uint32_t event_type = event_sp->GetType();
  ProcessSP process_sp =
      (event_type == Process::eBroadcastBitStructuredData)
          ? EventDataStructuredData::GetProcessFromEvent(event_sp.get())
          : Process::ProcessEventData::GetProcessFromEvent(event_sp.get());

  StreamSP output_stream_sp = GetAsyncOutputStream();
  StreamSP error_stream_sp = GetAsyncErrorStream();
  const bool gui_enabled = IsForwardingEvents();

  if (!gui_enabled) {
    bool pop_process_io_handler = false;
    assert(process_sp);

    bool state_is_stopped = false;
    const bool got_state_changed =
        (event_type & Process::eBroadcastBitStateChanged) != 0;
    const bool got_stdout = (event_type & Process::eBroadcastBitSTDOUT) != 0;
    const bool got_stderr = (event_type & Process::eBroadcastBitSTDERR) != 0;
    const bool got_structured_data =
        (event_type & Process::eBroadcastBitStructuredData) != 0;

    if (got_state_changed) {
      StateType event_state =
          Process::ProcessEventData::GetStateFromEvent(event_sp.get());
      state_is_stopped = StateIsStoppedState(event_state, false);
    }

    // Display running state changes first before any STDIO
    if (got_state_changed && !state_is_stopped) {
      // This is a public stop which we are going to announce to the user, so
      // we should force the most relevant frame selection here.
      Process::HandleProcessStateChangedEvent(event_sp, output_stream_sp.get(),
                                              SelectMostRelevantFrame,
                                              pop_process_io_handler);
    }

    // Now display STDOUT and STDERR
    FlushProcessOutput(*process_sp, got_stdout || got_state_changed,
                       got_stderr || got_state_changed);

    // Give structured data events an opportunity to display.
    if (got_structured_data) {
      StructuredDataPluginSP plugin_sp =
          EventDataStructuredData::GetPluginFromEvent(event_sp.get());
      if (plugin_sp) {
        auto structured_data_sp =
            EventDataStructuredData::GetObjectFromEvent(event_sp.get());
        if (output_stream_sp) {
          StreamString content_stream;
          Status error =
              plugin_sp->GetDescription(structured_data_sp, content_stream);
          if (error.Success()) {
            if (!content_stream.GetString().empty()) {
              // Add newline.
              content_stream.PutChar('\n');
              content_stream.Flush();

              // Print it.
              output_stream_sp->PutCString(content_stream.GetString());
            }
          } else {
            error_stream_sp->Format("Failed to print structured "
                                    "data with plugin {0}: {1}",
                                    plugin_sp->GetPluginName(), error);
          }
        }
      }
    }

    // Now display any stopped state changes after any STDIO
    if (got_state_changed && state_is_stopped) {
      Process::HandleProcessStateChangedEvent(event_sp, output_stream_sp.get(),
                                              SelectMostRelevantFrame,
                                              pop_process_io_handler);
    }

    output_stream_sp->Flush();
    error_stream_sp->Flush();

    if (pop_process_io_handler)
      process_sp->PopProcessIOHandler();
  }
}

void Debugger::HandleThreadEvent(const EventSP &event_sp) {
  // At present the only thread event we handle is the Frame Changed event, and
  // all we do for that is just reprint the thread status for that thread.
  using namespace lldb;
  const uint32_t event_type = event_sp->GetType();
  const bool stop_format = true;
  if (event_type == Thread::eBroadcastBitStackChanged ||
      event_type == Thread::eBroadcastBitThreadSelected) {
    ThreadSP thread_sp(
        Thread::ThreadEventData::GetThreadFromEvent(event_sp.get()));
    if (thread_sp) {
      thread_sp->GetStatus(*GetAsyncOutputStream(), 0, 1, 1, stop_format);
    }
  }
}

bool Debugger::IsForwardingEvents() { return (bool)m_forward_listener_sp; }

void Debugger::EnableForwardEvents(const ListenerSP &listener_sp) {
  m_forward_listener_sp = listener_sp;
}

void Debugger::CancelForwardEvents(const ListenerSP &listener_sp) {
  m_forward_listener_sp.reset();
}

lldb::thread_result_t Debugger::DefaultEventHandler() {
  ListenerSP listener_sp(GetListener());
  ConstString broadcaster_class_target(Target::GetStaticBroadcasterClass());
  ConstString broadcaster_class_process(Process::GetStaticBroadcasterClass());
  ConstString broadcaster_class_thread(Thread::GetStaticBroadcasterClass());
  BroadcastEventSpec target_event_spec(broadcaster_class_target,
                                       Target::eBroadcastBitBreakpointChanged);

  BroadcastEventSpec process_event_spec(
      broadcaster_class_process,
      Process::eBroadcastBitStateChanged | Process::eBroadcastBitSTDOUT |
          Process::eBroadcastBitSTDERR | Process::eBroadcastBitStructuredData);

  BroadcastEventSpec thread_event_spec(broadcaster_class_thread,
                                       Thread::eBroadcastBitStackChanged |
                                           Thread::eBroadcastBitThreadSelected);

  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          target_event_spec);
  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          process_event_spec);
  listener_sp->StartListeningForEventSpec(m_broadcaster_manager_sp,
                                          thread_event_spec);
  listener_sp->StartListeningForEvents(
      m_command_interpreter_up.get(),
      CommandInterpreter::eBroadcastBitQuitCommandReceived |
          CommandInterpreter::eBroadcastBitAsynchronousOutputData |
          CommandInterpreter::eBroadcastBitAsynchronousErrorData);

  listener_sp->StartListeningForEvents(
      &m_broadcaster, lldb::eBroadcastBitProgress | lldb::eBroadcastBitWarning |
                          lldb::eBroadcastBitError |
                          lldb::eBroadcastSymbolChange);

  // Let the thread that spawned us know that we have started up and that we
  // are now listening to all required events so no events get missed
  m_sync_broadcaster.BroadcastEvent(eBroadcastBitEventThreadIsListening);

  bool done = false;
  while (!done) {
    EventSP event_sp;
    if (listener_sp->GetEvent(event_sp, std::nullopt)) {
      if (event_sp) {
        Broadcaster *broadcaster = event_sp->GetBroadcaster();
        if (broadcaster) {
          uint32_t event_type = event_sp->GetType();
          ConstString broadcaster_class(broadcaster->GetBroadcasterClass());
          if (broadcaster_class == broadcaster_class_process) {
            HandleProcessEvent(event_sp);
          } else if (broadcaster_class == broadcaster_class_target) {
            if (Breakpoint::BreakpointEventData::GetEventDataFromEvent(
                    event_sp.get())) {
              HandleBreakpointEvent(event_sp);
            }
          } else if (broadcaster_class == broadcaster_class_thread) {
            HandleThreadEvent(event_sp);
          } else if (broadcaster == m_command_interpreter_up.get()) {
            if (event_type &
                CommandInterpreter::eBroadcastBitQuitCommandReceived) {
              done = true;
            } else if (event_type &
                       CommandInterpreter::eBroadcastBitAsynchronousErrorData) {
              const char *data = static_cast<const char *>(
                  EventDataBytes::GetBytesFromEvent(event_sp.get()));
              if (data && data[0]) {
                StreamSP error_sp(GetAsyncErrorStream());
                if (error_sp) {
                  error_sp->PutCString(data);
                  error_sp->Flush();
                }
              }
            } else if (event_type & CommandInterpreter::
                                        eBroadcastBitAsynchronousOutputData) {
              const char *data = static_cast<const char *>(
                  EventDataBytes::GetBytesFromEvent(event_sp.get()));
              if (data && data[0]) {
                StreamSP output_sp(GetAsyncOutputStream());
                if (output_sp) {
                  output_sp->PutCString(data);
                  output_sp->Flush();
                }
              }
            }
          } else if (broadcaster == &m_broadcaster) {
            if (event_type & lldb::eBroadcastBitProgress)
              HandleProgressEvent(event_sp);
            else if (event_type & lldb::eBroadcastBitWarning)
              HandleDiagnosticEvent(event_sp);
            else if (event_type & lldb::eBroadcastBitError)
              HandleDiagnosticEvent(event_sp);
          }
        }

        if (m_forward_listener_sp)
          m_forward_listener_sp->AddEvent(event_sp);
      }
    }
  }
  return {};
}

bool Debugger::StartEventHandlerThread() {
  if (!m_event_handler_thread.IsJoinable()) {
    // We must synchronize with the DefaultEventHandler() thread to ensure it
    // is up and running and listening to events before we return from this
    // function. We do this by listening to events for the
    // eBroadcastBitEventThreadIsListening from the m_sync_broadcaster
    ConstString full_name("lldb.debugger.event-handler");
    ListenerSP listener_sp(Listener::MakeListener(full_name.AsCString()));
    listener_sp->StartListeningForEvents(&m_sync_broadcaster,
                                         eBroadcastBitEventThreadIsListening);

    llvm::StringRef thread_name =
        full_name.GetLength() < llvm::get_max_thread_name_length()
            ? full_name.GetStringRef()
            : "dbg.evt-handler";

    // Use larger 8MB stack for this thread
    llvm::Expected<HostThread> event_handler_thread =
        ThreadLauncher::LaunchThread(
            thread_name, [this] { return DefaultEventHandler(); },
            g_debugger_event_thread_stack_bytes);

    if (event_handler_thread) {
      m_event_handler_thread = *event_handler_thread;
    } else {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), event_handler_thread.takeError(),
                     "failed to launch host thread: {0}");
    }

    // Make sure DefaultEventHandler() is running and listening to events
    // before we return from this function. We are only listening for events of
    // type eBroadcastBitEventThreadIsListening so we don't need to check the
    // event, we just need to wait an infinite amount of time for it (nullptr
    // timeout as the first parameter)
    lldb::EventSP event_sp;
    listener_sp->GetEvent(event_sp, std::nullopt);
  }
  return m_event_handler_thread.IsJoinable();
}

void Debugger::StopEventHandlerThread() {
  if (m_event_handler_thread.IsJoinable()) {
    GetCommandInterpreter().BroadcastEvent(
        CommandInterpreter::eBroadcastBitQuitCommandReceived);
    m_event_handler_thread.Join(nullptr);
  }
}

lldb::thread_result_t Debugger::IOHandlerThread() {
  RunIOHandlers();
  StopEventHandlerThread();
  return {};
}

void Debugger::HandleProgressEvent(const lldb::EventSP &event_sp) {
  auto *data = ProgressEventData::GetEventDataFromEvent(event_sp.get());
  if (!data)
    return;

  // Do some bookkeeping for the current event, regardless of whether we're
  // going to show the progress.
  const uint64_t id = data->GetID();
  if (m_current_event_id) {
    Log *log = GetLog(LLDBLog::Events);
    if (log && log->GetVerbose()) {
      StreamString log_stream;
      log_stream.AsRawOstream()
          << static_cast<void *>(this) << " Debugger(" << GetID()
          << ")::HandleProgressEvent( m_current_event_id = "
          << *m_current_event_id << ", data = { ";
      data->Dump(&log_stream);
      log_stream << " } )";
      log->PutString(log_stream.GetString());
    }
    if (id != *m_current_event_id)
      return;
    if (data->GetCompleted() == data->GetTotal())
      m_current_event_id.reset();
  } else {
    m_current_event_id = id;
  }

  // Decide whether we actually are going to show the progress. This decision
  // can change between iterations so check it inside the loop.
  if (!GetShowProgress())
    return;

  // Determine whether the current output file is an interactive terminal with
  // color support. We assume that if we support ANSI escape codes we support
  // vt100 escape codes.
  File &file = GetOutputFile();
  if (!file.GetIsInteractive() || !file.GetIsTerminalWithColors())
    return;

  StreamSP output = GetAsyncOutputStream();

  // Print over previous line, if any.
  output->Printf("\r");

  if (data->GetCompleted() == data->GetTotal()) {
    // Clear the current line.
    output->Printf("\x1B[2K");
    output->Flush();
    return;
  }

  // Trim the progress message if it exceeds the window's width and print it.
  std::string message = data->GetMessage();
  if (data->IsFinite())
    message = llvm::formatv("[{0}/{1}] {2}", data->GetCompleted(),
                            data->GetTotal(), message)
                  .str();

  // Trim the progress message if it exceeds the window's width and print it.
  const uint32_t term_width = GetTerminalWidth();
  const uint32_t ellipsis = 3;
  if (message.size() + ellipsis >= term_width)
    message = message.substr(0, term_width - ellipsis);

  const bool use_color = GetUseColor();
  llvm::StringRef ansi_prefix = GetShowProgressAnsiPrefix();
  if (!ansi_prefix.empty())
    output->Printf(
        "%s", ansi::FormatAnsiTerminalCodes(ansi_prefix, use_color).c_str());

  output->Printf("%s...", message.c_str());

  llvm::StringRef ansi_suffix = GetShowProgressAnsiSuffix();
  if (!ansi_suffix.empty())
    output->Printf(
        "%s", ansi::FormatAnsiTerminalCodes(ansi_suffix, use_color).c_str());

  // Clear until the end of the line.
  output->Printf("\x1B[K\r");

  // Flush the output.
  output->Flush();
}

void Debugger::HandleDiagnosticEvent(const lldb::EventSP &event_sp) {
  auto *data = DiagnosticEventData::GetEventDataFromEvent(event_sp.get());
  if (!data)
    return;

  StreamSP stream = GetAsyncErrorStream();
  data->Dump(stream.get());
}

bool Debugger::HasIOHandlerThread() const {
  return m_io_handler_thread.IsJoinable();
}

HostThread Debugger::SetIOHandlerThread(HostThread &new_thread) {
  HostThread old_host = m_io_handler_thread;
  m_io_handler_thread = new_thread;
  return old_host;
}

bool Debugger::StartIOHandlerThread() {
  if (!m_io_handler_thread.IsJoinable()) {
    llvm::Expected<HostThread> io_handler_thread = ThreadLauncher::LaunchThread(
        "lldb.debugger.io-handler", [this] { return IOHandlerThread(); },
        8 * 1024 * 1024); // Use larger 8MB stack for this thread
    if (io_handler_thread) {
      m_io_handler_thread = *io_handler_thread;
    } else {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), io_handler_thread.takeError(),
                     "failed to launch host thread: {0}");
    }
  }
  return m_io_handler_thread.IsJoinable();
}

void Debugger::StopIOHandlerThread() {
  if (m_io_handler_thread.IsJoinable()) {
    GetInputFile().Close();
    m_io_handler_thread.Join(nullptr);
  }
}

void Debugger::JoinIOHandlerThread() {
  if (HasIOHandlerThread()) {
    thread_result_t result;
    m_io_handler_thread.Join(&result);
    m_io_handler_thread = LLDB_INVALID_HOST_THREAD;
  }
}

bool Debugger::IsIOHandlerThreadCurrentThread() const {
  if (!HasIOHandlerThread())
    return false;
  return m_io_handler_thread.EqualsThread(Host::GetCurrentThread());
}

Target &Debugger::GetSelectedOrDummyTarget(bool prefer_dummy) {
  if (!prefer_dummy) {
    if (TargetSP target = m_target_list.GetSelectedTarget())
      return *target;
  }
  return GetDummyTarget();
}

Status Debugger::RunREPL(LanguageType language, const char *repl_options) {
  Status err;
  FileSpec repl_executable;

  if (language == eLanguageTypeUnknown)
    language = GetREPLLanguage();

  if (language == eLanguageTypeUnknown) {
    LanguageSet repl_languages = Language::GetLanguagesSupportingREPLs();

    if (auto single_lang = repl_languages.GetSingularLanguage()) {
      language = *single_lang;
    } else if (repl_languages.Empty()) {
      err.SetErrorString(
          "LLDB isn't configured with REPL support for any languages.");
      return err;
    } else {
      err.SetErrorString(
          "Multiple possible REPL languages.  Please specify a language.");
      return err;
    }
  }

  Target *const target =
      nullptr; // passing in an empty target means the REPL must create one

  REPLSP repl_sp(REPL::Create(err, language, this, target, repl_options));

  if (!err.Success()) {
    return err;
  }

  if (!repl_sp) {
    err.SetErrorStringWithFormat("couldn't find a REPL for %s",
                                 Language::GetNameForLanguageType(language));
    return err;
  }

  repl_sp->SetCompilerOptions(repl_options);
  repl_sp->RunLoop();

  return err;
}

llvm::ThreadPoolInterface &Debugger::GetThreadPool() {
  assert(g_thread_pool &&
         "Debugger::GetThreadPool called before Debugger::Initialize");
  return *g_thread_pool;
}
