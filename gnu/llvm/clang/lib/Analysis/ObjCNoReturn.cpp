//= ObjCNoReturn.cpp - Handling of Cocoa APIs known not to return --*- C++ -*---
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

#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Analysis/DomainSpecific/ObjCNoReturn.h"

using namespace clang;

static bool isSubclass(const ObjCInterfaceDecl *Class,
                       const IdentifierInfo *II) {
  if (!Class)
    return false;
  if (Class->getIdentifier() == II)
    return true;
  return isSubclass(Class->getSuperClass(), II);
}

ObjCNoReturn::ObjCNoReturn(ASTContext &C)
  : RaiseSel(GetNullarySelector("raise", C)),
    NSExceptionII(&C.Idents.get("NSException"))
{
  // Generate selectors.
  SmallVector<const IdentifierInfo *, 3> II;

  // raise:format:
  II.push_back(&C.Idents.get("raise"));
  II.push_back(&C.Idents.get("format"));
  NSExceptionInstanceRaiseSelectors[0] =
    C.Selectors.getSelector(II.size(), &II[0]);

  // raise:format:arguments:
  II.push_back(&C.Idents.get("arguments"));
  NSExceptionInstanceRaiseSelectors[1] =
    C.Selectors.getSelector(II.size(), &II[0]);
}


bool ObjCNoReturn::isImplicitNoReturn(const ObjCMessageExpr *ME) {
  Selector S = ME->getSelector();

  if (ME->isInstanceMessage()) {
    // Check for the "raise" message.
    return S == RaiseSel;
  }

  if (const ObjCInterfaceDecl *ID = ME->getReceiverInterface()) {
    if (isSubclass(ID, NSExceptionII) &&
        llvm::is_contained(NSExceptionInstanceRaiseSelectors, S))
      return true;
  }

  return false;
}
