//===----- SemaPPC.h ------- PPC target-specific routines -----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to PowerPC.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAPPC_H
#define LLVM_CLANG_SEMA_SEMAPPC_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/SemaBase.h"

namespace clang {
class SemaPPC : public SemaBase {
public:
  SemaPPC(Sema &S);

  bool CheckPPCBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                   CallExpr *TheCall);
  // 16 byte ByVal alignment not due to a vector member is not honoured by XL
  // on AIX. Emit a warning here that users are generating binary incompatible
  // code to be safe.
  // Here we try to get information about the alignment of the struct member
  // from the struct passed to the caller function. We only warn when the struct
  // is passed byval, hence the series of checks and early returns if we are a
  // not passing a struct byval.
  void checkAIXMemberAlignment(SourceLocation Loc, const Expr *Arg);

  /// BuiltinPPCMMACall - Check the call to a PPC MMA builtin for validity.
  /// Emit an error and return true on failure; return false on success.
  /// TypeStr is a string containing the type descriptor of the value returned
  /// by the builtin and the descriptors of the expected type of the arguments.
  bool BuiltinPPCMMACall(CallExpr *TheCall, unsigned BuiltinID,
                         const char *TypeDesc);

  bool CheckPPCMMAType(QualType Type, SourceLocation TypeLoc);

  // Customized Sema Checking for VSX builtins that have the following
  // signature: vector [...] builtinName(vector [...], vector [...], const int);
  // Which takes the same type of vectors (any legal vector type) for the first
  // two arguments and takes compile time constant for the third argument.
  // Example builtins are :
  // vector double vec_xxpermdi(vector double, vector double, int);
  // vector short vec_xxsldwi(vector short, vector short, int);
  bool BuiltinVSX(CallExpr *TheCall);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAPPC_H
