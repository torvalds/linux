//===----- SemaSwift.h --- Swift language-specific routines ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to Swift.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMASWIFT_H
#define LLVM_CLANG_SEMA_SEMASWIFT_H

#include "clang/AST/Attr.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class AttributeCommonInfo;
class Decl;
class ParsedAttr;
class SwiftNameAttr;

class SemaSwift : public SemaBase {
public:
  SemaSwift(Sema &S);

  SwiftNameAttr *mergeNameAttr(Decl *D, const SwiftNameAttr &SNA,
                               StringRef Name);

  void handleAttrAttr(Decl *D, const ParsedAttr &AL);
  void handleAsyncAttr(Decl *D, const ParsedAttr &AL);
  void handleBridge(Decl *D, const ParsedAttr &AL);
  void handleError(Decl *D, const ParsedAttr &AL);
  void handleAsyncError(Decl *D, const ParsedAttr &AL);
  void handleName(Decl *D, const ParsedAttr &AL);
  void handleAsyncName(Decl *D, const ParsedAttr &AL);
  void handleNewType(Decl *D, const ParsedAttr &AL);

  /// Do a check to make sure \p Name looks like a legal argument for the
  /// swift_name attribute applied to decl \p D.  Raise a diagnostic if the name
  /// is invalid for the given declaration.
  ///
  /// \p AL is used to provide caret diagnostics in case of a malformed name.
  ///
  /// \returns true if the name is a valid swift name for \p D, false otherwise.
  bool DiagnoseName(Decl *D, StringRef Name, SourceLocation Loc,
                    const ParsedAttr &AL, bool IsAsync);
  void AddParameterABIAttr(Decl *D, const AttributeCommonInfo &CI,
                           ParameterABI abi);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMASWIFT_H
