//===--- HLSLExternalSemaSource.h - HLSL Sema Source ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the HLSLExternalSemaSource interface.
//
//===----------------------------------------------------------------------===//
#ifndef CLANG_SEMA_HLSLEXTERNALSEMASOURCE_H
#define CLANG_SEMA_HLSLEXTERNALSEMASOURCE_H

#include "llvm/ADT/DenseMap.h"

#include "clang/Sema/ExternalSemaSource.h"

namespace clang {
class NamespaceDecl;
class Sema;

class HLSLExternalSemaSource : public ExternalSemaSource {
  Sema *SemaPtr = nullptr;
  NamespaceDecl *HLSLNamespace = nullptr;

  using CompletionFunction = std::function<void(CXXRecordDecl *)>;
  llvm::DenseMap<CXXRecordDecl *, CompletionFunction> Completions;

  void defineHLSLVectorAlias();
  void defineTrivialHLSLTypes();
  void defineHLSLTypesWithForwardDeclarations();

  void onCompletion(CXXRecordDecl *Record, CompletionFunction Fn);

public:
  ~HLSLExternalSemaSource() override;

  /// Initialize the semantic source with the Sema instance
  /// being used to perform semantic analysis on the abstract syntax
  /// tree.
  void InitializeSema(Sema &S) override;

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override { SemaPtr = nullptr; }

  using ExternalASTSource::CompleteType;
  /// Complete an incomplete HLSL builtin type
  void CompleteType(TagDecl *Tag) override;
};

} // namespace clang

#endif // CLANG_SEMA_HLSLEXTERNALSEMASOURCE_H
