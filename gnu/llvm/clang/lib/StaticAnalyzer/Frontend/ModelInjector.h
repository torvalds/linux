//===-- ModelInjector.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the clang::ento::ModelInjector class which implements the
/// clang::CodeInjector interface. This class is responsible for injecting
/// function definitions that were synthesized from model files.
///
/// Model files allow definitions of functions to be lazily constituted for functions
/// which lack bodies in the original source code.  This allows the analyzer
/// to more precisely analyze code that calls such functions, analyzing the
/// artificial definitions (which typically approximate the semantics of the
/// called function) when called by client code.  These definitions are
/// reconstituted lazily, on-demand, by the static analyzer engine.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SA_FRONTEND_MODELINJECTOR_H
#define LLVM_CLANG_SA_FRONTEND_MODELINJECTOR_H

#include "clang/Analysis/CodeInjector.h"
#include "llvm/ADT/StringMap.h"

namespace clang {

class CompilerInstance;
class NamedDecl;

namespace ento {
class ModelInjector : public CodeInjector {
public:
  ModelInjector(CompilerInstance &CI);
  Stmt *getBody(const FunctionDecl *D) override;
  Stmt *getBody(const ObjCMethodDecl *D) override;

private:
  /// Synthesize a body for a declaration
  ///
  /// This method first looks up the appropriate model file based on the
  /// model-path configuration option and the name of the declaration that is
  /// looked up. If no model were synthesized yet for a function with that name
  /// it will create a new compiler instance to parse the model file using the
  /// ASTContext, Preprocessor, SourceManager of the original compiler instance.
  /// The former resources are shared between the two compiler instance, so the
  /// newly created instance have to "leak" these objects, since they are owned
  /// by the original instance.
  ///
  /// The model-path should be either an absolute path or relative to the
  /// working directory of the compiler.
  void onBodySynthesis(const NamedDecl *D);

  CompilerInstance &CI;

  // FIXME: double memoization is redundant, with memoization both here and in
  // BodyFarm.
  llvm::StringMap<Stmt *> Bodies;
};
}
}

#endif
