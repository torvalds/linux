//===----- SemaX86.h ------- X86 target-specific routines -----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to X86.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAX86_H
#define LLVM_CLANG_SEMA_SEMAX86_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class ParsedAttr;

class SemaX86 : public SemaBase {
public:
  SemaX86(Sema &S);

  bool CheckBuiltinRoundingOrSAE(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckBuiltinGatherScatterScale(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckBuiltinTileArguments(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckBuiltinTileArgumentsRange(CallExpr *TheCall, ArrayRef<int> ArgNums);
  bool CheckBuiltinTileDuplicate(CallExpr *TheCall, ArrayRef<int> ArgNums);
  bool CheckBuiltinTileRangeAndDuplicate(CallExpr *TheCall,
                                         ArrayRef<int> ArgNums);
  bool CheckBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                CallExpr *TheCall);

  void handleAnyInterruptAttr(Decl *D, const ParsedAttr &AL);
  void handleForceAlignArgPointerAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAX86_H
