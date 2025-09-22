//===----- SemaMIPS.h ------ MIPS target-specific routines ----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to MIPS.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAMIPS_H
#define LLVM_CLANG_SEMA_SEMAMIPS_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class ParsedAttr;

class SemaMIPS : public SemaBase {
public:
  SemaMIPS(Sema &S);

  bool CheckMipsBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                    CallExpr *TheCall);
  bool CheckMipsBuiltinCpu(const TargetInfo &TI, unsigned BuiltinID,
                           CallExpr *TheCall);
  bool CheckMipsBuiltinArgument(unsigned BuiltinID, CallExpr *TheCall);
  void handleInterruptAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAMIPS_H
