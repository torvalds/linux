//== BodyFarm.h - Factory for conjuring up fake bodies -------------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BodyFarm is a factory for creating faux implementations for functions/methods
// for analysis purposes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_BODYFARM_H
#define LLVM_CLANG_ANALYSIS_BODYFARM_H

#include "clang/AST/DeclBase.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include <optional>

namespace clang {

class ASTContext;
class FunctionDecl;
class ObjCMethodDecl;
class Stmt;
class CodeInjector;

class BodyFarm {
public:
  BodyFarm(ASTContext &C, CodeInjector *injector) : C(C), Injector(injector) {}

  /// Factory method for creating bodies for ordinary functions.
  Stmt *getBody(const FunctionDecl *D);

  /// Factory method for creating bodies for Objective-C properties.
  Stmt *getBody(const ObjCMethodDecl *D);

  /// Remove copy constructor to avoid accidental copying.
  BodyFarm(const BodyFarm &other) = delete;

  /// Delete copy assignment operator.
  BodyFarm &operator=(const BodyFarm &other) = delete;

private:
  typedef llvm::DenseMap<const Decl *, std::optional<Stmt *>> BodyMap;

  ASTContext &C;
  BodyMap Bodies;
  CodeInjector *Injector;
};
} // namespace clang

#endif
