//===--- ToolRefactoringResultConsumer.h - ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_CLANG_REFACTOR_TOOL_REFACTORING_RESULT_CONSUMER_H
#define LLVM_CLANG_TOOLS_CLANG_REFACTOR_TOOL_REFACTORING_RESULT_CONSUMER_H

#include "clang/AST/ASTContext.h"
#include "clang/Tooling/Refactoring/RefactoringResultConsumer.h"

namespace clang {
namespace refactor {

/// An interface that subclasses the \c RefactoringResultConsumer interface
/// that stores the reference to the TU-specific diagnostics engine.
class ClangRefactorToolConsumerInterface
    : public tooling::RefactoringResultConsumer {
public:
  /// Called when a TU is entered.
  void beginTU(ASTContext &Context) {
    assert(!Diags && "Diags has been set");
    Diags = &Context.getDiagnostics();
  }

  /// Called when the tool is done with a TU.
  void endTU() {
    assert(Diags && "Diags unset");
    Diags = nullptr;
  }

  DiagnosticsEngine &getDiags() const {
    assert(Diags && "no diags");
    return *Diags;
  }

private:
  DiagnosticsEngine *Diags = nullptr;
};

} // end namespace refactor
} // end namespace clang

#endif // LLVM_CLANG_TOOLS_CLANG_REFACTOR_TOOL_REFACTORING_RESULT_CONSUMER_H
