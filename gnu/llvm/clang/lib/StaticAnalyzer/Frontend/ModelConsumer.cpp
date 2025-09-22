//===--- ModelConsumer.cpp - ASTConsumer for consuming model files --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an ASTConsumer for consuming model files.
///
/// This ASTConsumer handles the AST of a parsed model file. All top level
/// function definitions will be collected from that model file for later
/// retrieval during the static analysis. The body of these functions will not
/// be injected into the ASTUnit of the analyzed translation unit. It will be
/// available through the BodyFarm which is utilized by the AnalysisDeclContext
/// class.
///
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/ModelConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"

using namespace clang;
using namespace ento;

ModelConsumer::ModelConsumer(llvm::StringMap<Stmt *> &Bodies)
    : Bodies(Bodies) {}

bool ModelConsumer::HandleTopLevelDecl(DeclGroupRef DeclGroup) {
  for (const Decl *D : DeclGroup) {
    // Only interested in definitions.
    const auto *func = llvm::dyn_cast<FunctionDecl>(D);
    if (func && func->hasBody()) {
      Bodies.insert(std::make_pair(func->getName(), func->getBody()));
    }
  }
  return true;
}
