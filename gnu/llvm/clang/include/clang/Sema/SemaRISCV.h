//===----- SemaRISCV.h ---- RISC-V target-specific routines ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to RISC-V.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMARISCV_H
#define LLVM_CLANG_SEMA_SEMARISCV_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/RISCVIntrinsicManager.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/StringMap.h"
#include <memory>

namespace clang {
class ParsedAttr;

class SemaRISCV : public SemaBase {
public:
  SemaRISCV(Sema &S);

  bool CheckLMUL(CallExpr *TheCall, unsigned ArgNum);
  bool CheckBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                CallExpr *TheCall);
  void checkRVVTypeSupport(QualType Ty, SourceLocation Loc, Decl *D,
                           const llvm::StringMap<bool> &FeatureMap);

  bool isValidRVVBitcast(QualType srcType, QualType destType);

  void handleInterruptAttr(Decl *D, const ParsedAttr &AL);
  bool isAliasValid(unsigned BuiltinID, StringRef AliasName);

  /// Indicate RISC-V vector builtin functions enabled or not.
  bool DeclareRVVBuiltins = false;

  /// Indicate RISC-V SiFive vector builtin functions enabled or not.
  bool DeclareSiFiveVectorBuiltins = false;

  std::unique_ptr<sema::RISCVIntrinsicManager> IntrinsicManager;
};

std::unique_ptr<sema::RISCVIntrinsicManager>
CreateRISCVIntrinsicManager(Sema &S);
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMARISCV_H
