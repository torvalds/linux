//===-- ClangREPL.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_REPL_CLANG_CLANGREPL_H
#define LLDB_SOURCE_PLUGINS_REPL_CLANG_CLANGREPL_H

#include "lldb/Expression/REPL.h"

namespace lldb_private {
/// Implements a Clang-based REPL for C languages on top of LLDB's REPL
/// framework.
class ClangREPL : public llvm::RTTIExtends<ClangREPL, REPL> {
public:
  // LLVM RTTI support
  static char ID;

  ClangREPL(lldb::LanguageType language, Target &target);

  ~ClangREPL() override;

  static void Initialize();

  static void Terminate();

  static lldb::REPLSP CreateInstance(Status &error, lldb::LanguageType language,
                                     Debugger *debugger, Target *target,
                                     const char *repl_options);

  static llvm::StringRef GetPluginNameStatic() { return "ClangREPL"; }

protected:
  Status DoInitialization() override;

  llvm::StringRef GetSourceFileBasename() override;

  const char *GetAutoIndentCharacters() override;

  bool SourceIsComplete(const std::string &source) override;

  lldb::offset_t GetDesiredIndentation(const StringList &lines,
                                       int cursor_position,
                                       int tab_size) override;

  lldb::LanguageType GetLanguage() override;

  bool PrintOneVariable(Debugger &debugger, lldb::StreamFileSP &output_sp,
                        lldb::ValueObjectSP &valobj_sp,
                        ExpressionVariable *var = nullptr) override;

  void CompleteCode(const std::string &current_code,
                    CompletionRequest &request) override;

private:
  /// The specific C language of this REPL.
  lldb::LanguageType m_language;
  /// A regex matching the implicitly created LLDB result variables.
  lldb_private::RegularExpression m_implicit_expr_result_regex;
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_REPL_CLANG_CLANGREPL_H
