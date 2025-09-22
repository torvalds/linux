//===-- FrontendActions.h - Useful Frontend Actions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_FRONTEND_FRONTENDACTIONS_H
#define LLVM_CLANG_STATICANALYZER_FRONTEND_FRONTENDACTIONS_H

#include "clang/Frontend/FrontendAction.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class Stmt;

namespace ento {

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

class AnalysisAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;
};

/// Frontend action to parse model files.
///
/// This frontend action is responsible for parsing model files. Model files can
/// not be parsed on their own, they rely on type information that is available
/// in another translation unit. The parsing of model files is done by a
/// separate compiler instance that reuses the ASTContext and othen information
/// from the main translation unit that is being compiled. After a model file is
/// parsed, the function definitions will be collected into a StringMap.
class ParseModelFileAction : public ASTFrontendAction {
public:
  ParseModelFileAction(llvm::StringMap<Stmt *> &Bodies);
  bool isModelParsingAction() const override { return true; }

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

private:
  llvm::StringMap<Stmt *> &Bodies;
};

} // namespace ento
} // end namespace clang

#endif
