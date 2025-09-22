//===-- ClangREPL.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangREPL.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Expression/ExpressionVariable.h"

using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ClangREPL)

char ClangREPL::ID;

ClangREPL::ClangREPL(lldb::LanguageType language, Target &target)
    : llvm::RTTIExtends<ClangREPL, REPL>(target), m_language(language),
      m_implicit_expr_result_regex("\\$[0-9]+") {}

ClangREPL::~ClangREPL() = default;

void ClangREPL::Initialize() {
  LanguageSet languages;
  // FIXME: There isn't a way to ask CPlusPlusLanguage and ObjCLanguage for
  // a list of languages they support.
  languages.Insert(lldb::LanguageType::eLanguageTypeC);
  languages.Insert(lldb::LanguageType::eLanguageTypeC89);
  languages.Insert(lldb::LanguageType::eLanguageTypeC99);
  languages.Insert(lldb::LanguageType::eLanguageTypeC11);
  languages.Insert(lldb::LanguageType::eLanguageTypeC_plus_plus);
  languages.Insert(lldb::LanguageType::eLanguageTypeC_plus_plus_03);
  languages.Insert(lldb::LanguageType::eLanguageTypeC_plus_plus_11);
  languages.Insert(lldb::LanguageType::eLanguageTypeC_plus_plus_14);
  languages.Insert(lldb::LanguageType::eLanguageTypeObjC);
  languages.Insert(lldb::LanguageType::eLanguageTypeObjC_plus_plus);
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "C language REPL",
                                &CreateInstance, languages);
}

void ClangREPL::Terminate() {
  PluginManager::UnregisterPlugin(&CreateInstance);
}

lldb::REPLSP ClangREPL::CreateInstance(Status &error,
                                       lldb::LanguageType language,
                                       Debugger *debugger, Target *target,
                                       const char *repl_options) {
  // Creating a dummy target if only a debugger is given isn't implemented yet.
  if (!target) {
    error.SetErrorString("must have a target to create a REPL");
    return nullptr;
  }
  lldb::REPLSP result = std::make_shared<ClangREPL>(language, *target);
  target->SetREPL(language, result);
  error = Status();
  return result;
}

Status ClangREPL::DoInitialization() { return Status(); }

llvm::StringRef ClangREPL::GetSourceFileBasename() {
  static constexpr llvm::StringLiteral g_repl("repl.c");
  return g_repl;
}

const char *ClangREPL::GetAutoIndentCharacters() { return "  "; }

bool ClangREPL::SourceIsComplete(const std::string &source) {
  // FIXME: There isn't a good way to know if the input source is complete or
  // not, so just say that every single REPL line is ready to be parsed.
  return !source.empty();
}

lldb::offset_t ClangREPL::GetDesiredIndentation(const StringList &lines,
                                                int cursor_position,
                                                int tab_size) {
  // FIXME: Not implemented.
  return LLDB_INVALID_OFFSET;
}

lldb::LanguageType ClangREPL::GetLanguage() { return m_language; }

bool ClangREPL::PrintOneVariable(Debugger &debugger,
                                 lldb::StreamFileSP &output_sp,
                                 lldb::ValueObjectSP &valobj_sp,
                                 ExpressionVariable *var) {
  // If a ExpressionVariable was passed, check first if that variable is just
  // an automatically created expression result. These variables are already
  // printed by the REPL so this is done to prevent printing the variable twice.
  if (var) {
    if (m_implicit_expr_result_regex.Execute(var->GetName().GetStringRef()))
      return true;
  }
  if (llvm::Error error = valobj_sp->Dump(*output_sp))
    *output_sp << "error: " << toString(std::move(error));

  return true;
}

void ClangREPL::CompleteCode(const std::string &current_code,
                             CompletionRequest &request) {
  // Not implemented.
}
