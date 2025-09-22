//===------ SemaAMDGPU.cpp ------- AMDGPU target-specific routines --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to AMDGPU.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaAMDGPU.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/AtomicOrdering.h"
#include <cstdint>

namespace clang {

SemaAMDGPU::SemaAMDGPU(Sema &S) : SemaBase(S) {}

bool SemaAMDGPU::CheckAMDGCNBuiltinFunctionCall(unsigned BuiltinID,
                                                CallExpr *TheCall) {
  // position of memory order and scope arguments in the builtin
  unsigned OrderIndex, ScopeIndex;
  switch (BuiltinID) {
  case AMDGPU::BI__builtin_amdgcn_global_load_lds: {
    constexpr const int SizeIdx = 2;
    llvm::APSInt Size;
    Expr *ArgExpr = TheCall->getArg(SizeIdx);
    [[maybe_unused]] ExprResult R =
        SemaRef.VerifyIntegerConstantExpression(ArgExpr, &Size);
    assert(!R.isInvalid());
    switch (Size.getSExtValue()) {
    case 1:
    case 2:
    case 4:
      return false;
    default:
      Diag(ArgExpr->getExprLoc(),
           diag::err_amdgcn_global_load_lds_size_invalid_value)
          << ArgExpr->getSourceRange();
      Diag(ArgExpr->getExprLoc(),
           diag::note_amdgcn_global_load_lds_size_valid_value)
          << ArgExpr->getSourceRange();
      return true;
    }
  }
  case AMDGPU::BI__builtin_amdgcn_get_fpenv:
  case AMDGPU::BI__builtin_amdgcn_set_fpenv:
    return false;
  case AMDGPU::BI__builtin_amdgcn_atomic_inc32:
  case AMDGPU::BI__builtin_amdgcn_atomic_inc64:
  case AMDGPU::BI__builtin_amdgcn_atomic_dec32:
  case AMDGPU::BI__builtin_amdgcn_atomic_dec64:
    OrderIndex = 2;
    ScopeIndex = 3;
    break;
  case AMDGPU::BI__builtin_amdgcn_fence:
    OrderIndex = 0;
    ScopeIndex = 1;
    break;
  default:
    return false;
  }

  ExprResult Arg = TheCall->getArg(OrderIndex);
  auto ArgExpr = Arg.get();
  Expr::EvalResult ArgResult;

  if (!ArgExpr->EvaluateAsInt(ArgResult, getASTContext()))
    return Diag(ArgExpr->getExprLoc(), diag::err_typecheck_expect_int)
           << ArgExpr->getType();
  auto Ord = ArgResult.Val.getInt().getZExtValue();

  // Check validity of memory ordering as per C11 / C++11's memody model.
  // Only fence needs check. Atomic dec/inc allow all memory orders.
  if (!llvm::isValidAtomicOrderingCABI(Ord))
    return Diag(ArgExpr->getBeginLoc(),
                diag::warn_atomic_op_has_invalid_memory_order)
           << 0 << ArgExpr->getSourceRange();
  switch (static_cast<llvm::AtomicOrderingCABI>(Ord)) {
  case llvm::AtomicOrderingCABI::relaxed:
  case llvm::AtomicOrderingCABI::consume:
    if (BuiltinID == AMDGPU::BI__builtin_amdgcn_fence)
      return Diag(ArgExpr->getBeginLoc(),
                  diag::warn_atomic_op_has_invalid_memory_order)
             << 0 << ArgExpr->getSourceRange();
    break;
  case llvm::AtomicOrderingCABI::acquire:
  case llvm::AtomicOrderingCABI::release:
  case llvm::AtomicOrderingCABI::acq_rel:
  case llvm::AtomicOrderingCABI::seq_cst:
    break;
  }

  Arg = TheCall->getArg(ScopeIndex);
  ArgExpr = Arg.get();
  Expr::EvalResult ArgResult1;
  // Check that sync scope is a constant literal
  if (!ArgExpr->EvaluateAsConstantExpr(ArgResult1, getASTContext()))
    return Diag(ArgExpr->getExprLoc(), diag::err_expr_not_string_literal)
           << ArgExpr->getType();

  return false;
}

static bool
checkAMDGPUFlatWorkGroupSizeArguments(Sema &S, Expr *MinExpr, Expr *MaxExpr,
                                      const AMDGPUFlatWorkGroupSizeAttr &Attr) {
  // Accept template arguments for now as they depend on something else.
  // We'll get to check them when they eventually get instantiated.
  if (MinExpr->isValueDependent() || MaxExpr->isValueDependent())
    return false;

  uint32_t Min = 0;
  if (!S.checkUInt32Argument(Attr, MinExpr, Min, 0))
    return true;

  uint32_t Max = 0;
  if (!S.checkUInt32Argument(Attr, MaxExpr, Max, 1))
    return true;

  if (Min == 0 && Max != 0) {
    S.Diag(Attr.getLocation(), diag::err_attribute_argument_invalid)
        << &Attr << 0;
    return true;
  }
  if (Min > Max) {
    S.Diag(Attr.getLocation(), diag::err_attribute_argument_invalid)
        << &Attr << 1;
    return true;
  }

  return false;
}

AMDGPUFlatWorkGroupSizeAttr *
SemaAMDGPU::CreateAMDGPUFlatWorkGroupSizeAttr(const AttributeCommonInfo &CI,
                                              Expr *MinExpr, Expr *MaxExpr) {
  ASTContext &Context = getASTContext();
  AMDGPUFlatWorkGroupSizeAttr TmpAttr(Context, CI, MinExpr, MaxExpr);

  if (checkAMDGPUFlatWorkGroupSizeArguments(SemaRef, MinExpr, MaxExpr, TmpAttr))
    return nullptr;
  return ::new (Context)
      AMDGPUFlatWorkGroupSizeAttr(Context, CI, MinExpr, MaxExpr);
}

void SemaAMDGPU::addAMDGPUFlatWorkGroupSizeAttr(Decl *D,
                                                const AttributeCommonInfo &CI,
                                                Expr *MinExpr, Expr *MaxExpr) {
  if (auto *Attr = CreateAMDGPUFlatWorkGroupSizeAttr(CI, MinExpr, MaxExpr))
    D->addAttr(Attr);
}

void SemaAMDGPU::handleAMDGPUFlatWorkGroupSizeAttr(Decl *D,
                                                   const ParsedAttr &AL) {
  Expr *MinExpr = AL.getArgAsExpr(0);
  Expr *MaxExpr = AL.getArgAsExpr(1);

  addAMDGPUFlatWorkGroupSizeAttr(D, AL, MinExpr, MaxExpr);
}

static bool checkAMDGPUWavesPerEUArguments(Sema &S, Expr *MinExpr,
                                           Expr *MaxExpr,
                                           const AMDGPUWavesPerEUAttr &Attr) {
  if (S.DiagnoseUnexpandedParameterPack(MinExpr) ||
      (MaxExpr && S.DiagnoseUnexpandedParameterPack(MaxExpr)))
    return true;

  // Accept template arguments for now as they depend on something else.
  // We'll get to check them when they eventually get instantiated.
  if (MinExpr->isValueDependent() || (MaxExpr && MaxExpr->isValueDependent()))
    return false;

  uint32_t Min = 0;
  if (!S.checkUInt32Argument(Attr, MinExpr, Min, 0))
    return true;

  uint32_t Max = 0;
  if (MaxExpr && !S.checkUInt32Argument(Attr, MaxExpr, Max, 1))
    return true;

  if (Min == 0 && Max != 0) {
    S.Diag(Attr.getLocation(), diag::err_attribute_argument_invalid)
        << &Attr << 0;
    return true;
  }
  if (Max != 0 && Min > Max) {
    S.Diag(Attr.getLocation(), diag::err_attribute_argument_invalid)
        << &Attr << 1;
    return true;
  }

  return false;
}

AMDGPUWavesPerEUAttr *
SemaAMDGPU::CreateAMDGPUWavesPerEUAttr(const AttributeCommonInfo &CI,
                                       Expr *MinExpr, Expr *MaxExpr) {
  ASTContext &Context = getASTContext();
  AMDGPUWavesPerEUAttr TmpAttr(Context, CI, MinExpr, MaxExpr);

  if (checkAMDGPUWavesPerEUArguments(SemaRef, MinExpr, MaxExpr, TmpAttr))
    return nullptr;

  return ::new (Context) AMDGPUWavesPerEUAttr(Context, CI, MinExpr, MaxExpr);
}

void SemaAMDGPU::addAMDGPUWavesPerEUAttr(Decl *D, const AttributeCommonInfo &CI,
                                         Expr *MinExpr, Expr *MaxExpr) {
  if (auto *Attr = CreateAMDGPUWavesPerEUAttr(CI, MinExpr, MaxExpr))
    D->addAttr(Attr);
}

void SemaAMDGPU::handleAMDGPUWavesPerEUAttr(Decl *D, const ParsedAttr &AL) {
  if (!AL.checkAtLeastNumArgs(SemaRef, 1) || !AL.checkAtMostNumArgs(SemaRef, 2))
    return;

  Expr *MinExpr = AL.getArgAsExpr(0);
  Expr *MaxExpr = (AL.getNumArgs() > 1) ? AL.getArgAsExpr(1) : nullptr;

  addAMDGPUWavesPerEUAttr(D, AL, MinExpr, MaxExpr);
}

void SemaAMDGPU::handleAMDGPUNumSGPRAttr(Decl *D, const ParsedAttr &AL) {
  uint32_t NumSGPR = 0;
  Expr *NumSGPRExpr = AL.getArgAsExpr(0);
  if (!SemaRef.checkUInt32Argument(AL, NumSGPRExpr, NumSGPR))
    return;

  D->addAttr(::new (getASTContext())
                 AMDGPUNumSGPRAttr(getASTContext(), AL, NumSGPR));
}

void SemaAMDGPU::handleAMDGPUNumVGPRAttr(Decl *D, const ParsedAttr &AL) {
  uint32_t NumVGPR = 0;
  Expr *NumVGPRExpr = AL.getArgAsExpr(0);
  if (!SemaRef.checkUInt32Argument(AL, NumVGPRExpr, NumVGPR))
    return;

  D->addAttr(::new (getASTContext())
                 AMDGPUNumVGPRAttr(getASTContext(), AL, NumVGPR));
}

static bool
checkAMDGPUMaxNumWorkGroupsArguments(Sema &S, Expr *XExpr, Expr *YExpr,
                                     Expr *ZExpr,
                                     const AMDGPUMaxNumWorkGroupsAttr &Attr) {
  if (S.DiagnoseUnexpandedParameterPack(XExpr) ||
      (YExpr && S.DiagnoseUnexpandedParameterPack(YExpr)) ||
      (ZExpr && S.DiagnoseUnexpandedParameterPack(ZExpr)))
    return true;

  // Accept template arguments for now as they depend on something else.
  // We'll get to check them when they eventually get instantiated.
  if (XExpr->isValueDependent() || (YExpr && YExpr->isValueDependent()) ||
      (ZExpr && ZExpr->isValueDependent()))
    return false;

  uint32_t NumWG = 0;
  Expr *Exprs[3] = {XExpr, YExpr, ZExpr};
  for (int i = 0; i < 3; i++) {
    if (Exprs[i]) {
      if (!S.checkUInt32Argument(Attr, Exprs[i], NumWG, i,
                                 /*StrictlyUnsigned=*/true))
        return true;
      if (NumWG == 0) {
        S.Diag(Attr.getLoc(), diag::err_attribute_argument_is_zero)
            << &Attr << Exprs[i]->getSourceRange();
        return true;
      }
    }
  }

  return false;
}

AMDGPUMaxNumWorkGroupsAttr *SemaAMDGPU::CreateAMDGPUMaxNumWorkGroupsAttr(
    const AttributeCommonInfo &CI, Expr *XExpr, Expr *YExpr, Expr *ZExpr) {
  ASTContext &Context = getASTContext();
  AMDGPUMaxNumWorkGroupsAttr TmpAttr(Context, CI, XExpr, YExpr, ZExpr);

  if (checkAMDGPUMaxNumWorkGroupsArguments(SemaRef, XExpr, YExpr, ZExpr,
                                           TmpAttr))
    return nullptr;

  return ::new (Context)
      AMDGPUMaxNumWorkGroupsAttr(Context, CI, XExpr, YExpr, ZExpr);
}

void SemaAMDGPU::addAMDGPUMaxNumWorkGroupsAttr(Decl *D,
                                               const AttributeCommonInfo &CI,
                                               Expr *XExpr, Expr *YExpr,
                                               Expr *ZExpr) {
  if (auto *Attr = CreateAMDGPUMaxNumWorkGroupsAttr(CI, XExpr, YExpr, ZExpr))
    D->addAttr(Attr);
}

void SemaAMDGPU::handleAMDGPUMaxNumWorkGroupsAttr(Decl *D,
                                                  const ParsedAttr &AL) {
  Expr *YExpr = (AL.getNumArgs() > 1) ? AL.getArgAsExpr(1) : nullptr;
  Expr *ZExpr = (AL.getNumArgs() > 2) ? AL.getArgAsExpr(2) : nullptr;
  addAMDGPUMaxNumWorkGroupsAttr(D, AL, AL.getArgAsExpr(0), YExpr, ZExpr);
}

} // namespace clang
