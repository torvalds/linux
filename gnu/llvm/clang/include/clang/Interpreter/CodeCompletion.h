//===----- CodeCompletion.h - Code Completion for ClangRepl ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes which performs code completion at the REPL.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INTERPRETER_CODE_COMPLETION_H
#define LLVM_CLANG_INTERPRETER_CODE_COMPLETION_H
#include <string>
#include <vector>

namespace llvm {
class StringRef;
} // namespace llvm

namespace clang {
class CodeCompletionResult;
class CompilerInstance;

struct ReplCodeCompleter {
  ReplCodeCompleter() = default;
  std::string Prefix;

  /// \param InterpCI [in] The compiler instance that is used to trigger code
  /// completion

  /// \param Content [in] The string where code completion is triggered.

  /// \param Line [in] The line number of the code completion point.

  /// \param Col [in] The column number of the code completion point.

  /// \param ParentCI [in] The running interpreter compiler instance that
  /// provides ASTContexts.

  /// \param CCResults [out] The completion results.
  void codeComplete(CompilerInstance *InterpCI, llvm::StringRef Content,
                    unsigned Line, unsigned Col,
                    const CompilerInstance *ParentCI,
                    std::vector<std::string> &CCResults);
};
} // namespace clang
#endif
