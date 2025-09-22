//===----- SemaNVPTX.h ----- NVPTX target-specific routines ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to NVPTX.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMANVPTX_H
#define LLVM_CLANG_SEMA_SEMANVPTX_H

#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class SemaNVPTX : public SemaBase {
public:
  SemaNVPTX(Sema &S);

  bool CheckNVPTXBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                     CallExpr *TheCall);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMANVPTX_H
