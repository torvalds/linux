//===--- FrontendActions.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/AnalysisConsumer.h"
#include "clang/StaticAnalyzer/Frontend/ModelConsumer.h"
using namespace clang;
using namespace ento;

std::unique_ptr<ASTConsumer>
AnalysisAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateAnalysisConsumer(CI);
}

ParseModelFileAction::ParseModelFileAction(llvm::StringMap<Stmt *> &Bodies)
    : Bodies(Bodies) {}

std::unique_ptr<ASTConsumer>
ParseModelFileAction::CreateASTConsumer(CompilerInstance &CI,
                                        StringRef InFile) {
  return std::make_unique<ModelConsumer>(Bodies);
}
