//===--- CleanupInfo.cpp - Cleanup Control in Sema ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a set of operations on whether generating an
//  ExprWithCleanups in a full expression.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_CLEANUPINFO_H
#define LLVM_CLANG_SEMA_CLEANUPINFO_H

namespace clang {

class CleanupInfo {
  bool ExprNeedsCleanups = false;
  bool CleanupsHaveSideEffects = false;

public:
  bool exprNeedsCleanups() const { return ExprNeedsCleanups; }

  bool cleanupsHaveSideEffects() const { return CleanupsHaveSideEffects; }

  void setExprNeedsCleanups(bool SideEffects) {
    ExprNeedsCleanups = true;
    CleanupsHaveSideEffects |= SideEffects;
  }

  void reset() {
    ExprNeedsCleanups = false;
    CleanupsHaveSideEffects = false;
  }

  void mergeFrom(CleanupInfo Rhs) {
    ExprNeedsCleanups |= Rhs.ExprNeedsCleanups;
    CleanupsHaveSideEffects |= Rhs.CleanupsHaveSideEffects;
  }
};

} // end namespace clang

#endif
