//=== MangleNumberingContext.h - Context for mangling numbers ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the LambdaBlockMangleContext interface, which keeps track
//  of the Itanium C++ ABI mangling numbers for lambda expressions and block
//  literals.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_MANGLENUMBERINGCONTEXT_H
#define LLVM_CLANG_AST_MANGLENUMBERINGCONTEXT_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace clang {

class BlockDecl;
class CXXMethodDecl;
class TagDecl;
class VarDecl;

/// Keeps track of the mangled names of lambda expressions and block
/// literals within a particular context.
class MangleNumberingContext {
  // The index of the next lambda we encounter in this context.
  unsigned LambdaIndex = 0;

public:
  virtual ~MangleNumberingContext() {}

  /// Retrieve the mangling number of a new lambda expression with the
  /// given call operator within this context.
  virtual unsigned getManglingNumber(const CXXMethodDecl *CallOperator) = 0;

  /// Retrieve the mangling number of a new block literal within this
  /// context.
  virtual unsigned getManglingNumber(const BlockDecl *BD) = 0;

  /// Static locals are numbered by source order.
  virtual unsigned getStaticLocalNumber(const VarDecl *VD) = 0;

  /// Retrieve the mangling number of a static local variable within
  /// this context.
  virtual unsigned getManglingNumber(const VarDecl *VD,
                                     unsigned MSLocalManglingNumber) = 0;

  /// Retrieve the mangling number of a static local variable within
  /// this context.
  virtual unsigned getManglingNumber(const TagDecl *TD,
                                     unsigned MSLocalManglingNumber) = 0;

  /// Retrieve the mangling number of a new lambda expression with the
  /// given call operator within the device context. No device number is
  /// assigned if there's no device numbering context is associated.
  virtual unsigned getDeviceManglingNumber(const CXXMethodDecl *) { return 0; }

  // Retrieve the index of the next lambda appearing in this context, which is
  // used for deduplicating lambdas across modules. Note that this is a simple
  // sequence number and is not ABI-dependent.
  unsigned getNextLambdaIndex() { return LambdaIndex++; }
};

} // end namespace clang
#endif
