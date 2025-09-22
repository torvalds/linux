//===--- SemaConsumer.h - Abstract interface for AST semantics --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SemaConsumer class, a subclass of
//  ASTConsumer that is used by AST clients that also require
//  additional semantic analysis.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SEMA_SEMACONSUMER_H
#define LLVM_CLANG_SEMA_SEMACONSUMER_H

#include "clang/AST/ASTConsumer.h"

namespace clang {
  class Sema;

  /// An abstract interface that should be implemented by
  /// clients that read ASTs and then require further semantic
  /// analysis of the entities in those ASTs.
  class SemaConsumer : public ASTConsumer {
    virtual void anchor();
  public:
    SemaConsumer() {
      ASTConsumer::SemaConsumer = true;
    }

    /// Initialize the semantic consumer with the Sema instance
    /// being used to perform semantic analysis on the abstract syntax
    /// tree.
    virtual void InitializeSema(Sema &S) {}

    /// Inform the semantic consumer that Sema is no longer available.
    virtual void ForgetSema() {}

    // isa/cast/dyn_cast support
    static bool classof(const ASTConsumer *Consumer) {
      return Consumer->SemaConsumer;
    }
  };
}

#endif
