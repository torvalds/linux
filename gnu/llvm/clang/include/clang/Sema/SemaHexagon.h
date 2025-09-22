//===----- SemaHexagon.h -- Hexagon target-specific routines --*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to Hexagon.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAHEXAGON_H
#define LLVM_CLANG_SEMA_SEMAHEXAGON_H

#include "clang/AST/Expr.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class SemaHexagon : public SemaBase {
public:
  SemaHexagon(Sema &S);

  bool CheckHexagonBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckHexagonBuiltinArgument(unsigned BuiltinID, CallExpr *TheCall);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAHEXAGON_H
