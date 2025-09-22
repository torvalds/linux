//===------ SemaSystemZ.cpp ------ SystemZ target-specific routines -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to SystemZ.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaSystemZ.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"
#include <optional>

namespace clang {

SemaSystemZ::SemaSystemZ(Sema &S) : SemaBase(S) {}

bool SemaSystemZ::CheckSystemZBuiltinFunctionCall(unsigned BuiltinID,
                                                  CallExpr *TheCall) {
  if (BuiltinID == SystemZ::BI__builtin_tabort) {
    Expr *Arg = TheCall->getArg(0);
    if (std::optional<llvm::APSInt> AbortCode =
            Arg->getIntegerConstantExpr(getASTContext()))
      if (AbortCode->getSExtValue() >= 0 && AbortCode->getSExtValue() < 256)
        return Diag(Arg->getBeginLoc(), diag::err_systemz_invalid_tabort_code)
               << Arg->getSourceRange();
  }

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default: return false;
  case SystemZ::BI__builtin_s390_lcbb: i = 1; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_verimb:
  case SystemZ::BI__builtin_s390_verimh:
  case SystemZ::BI__builtin_s390_verimf:
  case SystemZ::BI__builtin_s390_verimg: i = 3; l = 0; u = 255; break;
  case SystemZ::BI__builtin_s390_vfaeb:
  case SystemZ::BI__builtin_s390_vfaeh:
  case SystemZ::BI__builtin_s390_vfaef:
  case SystemZ::BI__builtin_s390_vfaebs:
  case SystemZ::BI__builtin_s390_vfaehs:
  case SystemZ::BI__builtin_s390_vfaefs:
  case SystemZ::BI__builtin_s390_vfaezb:
  case SystemZ::BI__builtin_s390_vfaezh:
  case SystemZ::BI__builtin_s390_vfaezf:
  case SystemZ::BI__builtin_s390_vfaezbs:
  case SystemZ::BI__builtin_s390_vfaezhs:
  case SystemZ::BI__builtin_s390_vfaezfs: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vfisb:
  case SystemZ::BI__builtin_s390_vfidb:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 15);
  case SystemZ::BI__builtin_s390_vftcisb:
  case SystemZ::BI__builtin_s390_vftcidb: i = 1; l = 0; u = 4095; break;
  case SystemZ::BI__builtin_s390_vlbb: i = 1; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vpdi: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vsldb: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vstrcb:
  case SystemZ::BI__builtin_s390_vstrch:
  case SystemZ::BI__builtin_s390_vstrcf:
  case SystemZ::BI__builtin_s390_vstrczb:
  case SystemZ::BI__builtin_s390_vstrczh:
  case SystemZ::BI__builtin_s390_vstrczf:
  case SystemZ::BI__builtin_s390_vstrcbs:
  case SystemZ::BI__builtin_s390_vstrchs:
  case SystemZ::BI__builtin_s390_vstrcfs:
  case SystemZ::BI__builtin_s390_vstrczbs:
  case SystemZ::BI__builtin_s390_vstrczhs:
  case SystemZ::BI__builtin_s390_vstrczfs: i = 3; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vmslg: i = 3; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vfminsb:
  case SystemZ::BI__builtin_s390_vfmaxsb:
  case SystemZ::BI__builtin_s390_vfmindb:
  case SystemZ::BI__builtin_s390_vfmaxdb: i = 2; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vsld: i = 2; l = 0; u = 7; break;
  case SystemZ::BI__builtin_s390_vsrd: i = 2; l = 0; u = 7; break;
  case SystemZ::BI__builtin_s390_vclfnhs:
  case SystemZ::BI__builtin_s390_vclfnls:
  case SystemZ::BI__builtin_s390_vcfn:
  case SystemZ::BI__builtin_s390_vcnf: i = 1; l = 0; u = 15; break;
  case SystemZ::BI__builtin_s390_vcrnfs: i = 2; l = 0; u = 15; break;
  }
  return SemaRef.BuiltinConstantArgRange(TheCall, i, l, u);
}

} // namespace clang
