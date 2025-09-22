//= ObjCNoReturn.h - Handling of Cocoa APIs known not to return --*- C++ -*---//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements special handling of recognizing ObjC API hooks that
// do not return but aren't marked as such in API headers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_DOMAINSPECIFIC_OBJCNORETURN_H
#define LLVM_CLANG_ANALYSIS_DOMAINSPECIFIC_OBJCNORETURN_H

#include "clang/Basic/IdentifierTable.h"

namespace clang {

class ASTContext;
class ObjCMessageExpr;

class ObjCNoReturn {
  /// Cached "raise" selector.
  Selector RaiseSel;

  /// Cached identifier for "NSException".
  IdentifierInfo *NSExceptionII;

  enum { NUM_RAISE_SELECTORS = 2 };

  /// Cached set of selectors in NSException that are 'noreturn'.
  Selector NSExceptionInstanceRaiseSelectors[NUM_RAISE_SELECTORS];

public:
  ObjCNoReturn(ASTContext &C);

  /// Return true if the given message expression is known to never
  /// return.
  bool isImplicitNoReturn(const ObjCMessageExpr *ME);
};
}

#endif
