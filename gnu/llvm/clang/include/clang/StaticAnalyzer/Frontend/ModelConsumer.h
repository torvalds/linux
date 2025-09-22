//===-- ModelConsumer.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements clang::ento::ModelConsumer which is an
/// ASTConsumer for model files.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_FRONTEND_MODELCONSUMER_H
#define LLVM_CLANG_STATICANALYZER_FRONTEND_MODELCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "llvm/ADT/StringMap.h"

namespace clang {

class Stmt;

namespace ento {

/// ASTConsumer to consume model files' AST.
///
/// This consumer collects the bodies of function definitions into a StringMap
/// from a model file.
class ModelConsumer : public ASTConsumer {
public:
  ModelConsumer(llvm::StringMap<Stmt *> &Bodies);

  bool HandleTopLevelDecl(DeclGroupRef D) override;

private:
  llvm::StringMap<Stmt *> &Bodies;
};
}
}

#endif
