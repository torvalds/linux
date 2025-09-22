//===------- Interp.cpp - Interpreter for the constexpr VM ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Interp.h"
#include "Function.h"
#include "InterpFrame.h"
#include "InterpShared.h"
#include "InterpStack.h"
#include "Opcode.h"
#include "PrimType.h"
#include "Program.h"
#include "State.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringExtras.h"
#include <limits>
#include <vector>

using namespace clang;

using namespace clang;
using namespace clang::interp;

static bool RetValue(InterpState &S, CodePtr &Pt, APValue &Result) {
  llvm::report_fatal_error("Interpreter cannot return values");
}

//===----------------------------------------------------------------------===//
// Jmp, Jt, Jf
//===----------------------------------------------------------------------===//

static bool Jmp(InterpState &S, CodePtr &PC, int32_t Offset) {
  PC += Offset;
  return true;
}

static bool Jt(InterpState &S, CodePtr &PC, int32_t Offset) {
  if (S.Stk.pop<bool>()) {
    PC += Offset;
  }
  return true;
}

static bool Jf(InterpState &S, CodePtr &PC, int32_t Offset) {
  if (!S.Stk.pop<bool>()) {
    PC += Offset;
  }
  return true;
}

static void diagnoseMissingInitializer(InterpState &S, CodePtr OpPC,
                                       const ValueDecl *VD) {
  const SourceInfo &E = S.Current->getSource(OpPC);
  S.FFDiag(E, diag::note_constexpr_var_init_unknown, 1) << VD;
  S.Note(VD->getLocation(), diag::note_declared_at) << VD->getSourceRange();
}

static void diagnoseNonConstVariable(InterpState &S, CodePtr OpPC,
                                     const ValueDecl *VD);
static bool diagnoseUnknownDecl(InterpState &S, CodePtr OpPC,
                                const ValueDecl *D) {
  const SourceInfo &E = S.Current->getSource(OpPC);

  if (isa<ParmVarDecl>(D)) {
    if (S.getLangOpts().CPlusPlus11) {
      S.FFDiag(E, diag::note_constexpr_function_param_value_unknown) << D;
      S.Note(D->getLocation(), diag::note_declared_at) << D->getSourceRange();
    } else {
      S.FFDiag(E);
    }
    return false;
  }

  if (!D->getType().isConstQualified())
    diagnoseNonConstVariable(S, OpPC, D);
  else if (const auto *VD = dyn_cast<VarDecl>(D);
           VD && !VD->getAnyInitializer())
    diagnoseMissingInitializer(S, OpPC, VD);

  return false;
}

static void diagnoseNonConstVariable(InterpState &S, CodePtr OpPC,
                                     const ValueDecl *VD) {
  if (!S.getLangOpts().CPlusPlus)
    return;

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  if (const auto *VarD = dyn_cast<VarDecl>(VD);
      VarD && VarD->getType().isConstQualified() &&
      !VarD->getAnyInitializer()) {
    diagnoseMissingInitializer(S, OpPC, VD);
    return;
  }

  // Rather random, but this is to match the diagnostic output of the current
  // interpreter.
  if (isa<ObjCIvarDecl>(VD))
    return;

  if (VD->getType()->isIntegralOrEnumerationType()) {
    S.FFDiag(Loc, diag::note_constexpr_ltor_non_const_int, 1) << VD;
    S.Note(VD->getLocation(), diag::note_declared_at);
    return;
  }

  S.FFDiag(Loc,
           S.getLangOpts().CPlusPlus11 ? diag::note_constexpr_ltor_non_constexpr
                                       : diag::note_constexpr_ltor_non_integral,
           1)
      << VD << VD->getType();
  S.Note(VD->getLocation(), diag::note_declared_at);
}

static bool CheckActive(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                        AccessKinds AK) {
  if (Ptr.isActive())
    return true;

  // Get the inactive field descriptor.
  const FieldDecl *InactiveField = Ptr.getField();

  // Walk up the pointer chain to find the union which is not active.
  Pointer U = Ptr.getBase();
  while (!U.isActive()) {
    U = U.getBase();
  }

  // Find the active field of the union.
  const Record *R = U.getRecord();
  assert(R && R->isUnion() && "Not a union");
  const FieldDecl *ActiveField = nullptr;
  for (unsigned I = 0, N = R->getNumFields(); I < N; ++I) {
    const Pointer &Field = U.atField(R->getField(I)->Offset);
    if (Field.isActive()) {
      ActiveField = Field.getField();
      break;
    }
  }

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_access_inactive_union_member)
      << AK << InactiveField << !ActiveField << ActiveField;
  return false;
}

static bool CheckTemporary(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                           AccessKinds AK) {
  if (auto ID = Ptr.getDeclID()) {
    if (!Ptr.isStaticTemporary())
      return true;

    if (Ptr.getDeclDesc()->getType().isConstQualified())
      return true;

    if (S.P.getCurrentDecl() == ID)
      return true;

    const SourceInfo &E = S.Current->getSource(OpPC);
    S.FFDiag(E, diag::note_constexpr_access_static_temporary, 1) << AK;
    S.Note(Ptr.getDeclLoc(), diag::note_constexpr_temporary_here);
    return false;
  }
  return true;
}

static bool CheckGlobal(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (auto ID = Ptr.getDeclID()) {
    if (!Ptr.isStatic())
      return true;

    if (S.P.getCurrentDecl() == ID)
      return true;

    S.FFDiag(S.Current->getLocation(OpPC), diag::note_constexpr_modify_global);
    return false;
  }
  return true;
}

namespace clang {
namespace interp {
static void popArg(InterpState &S, const Expr *Arg) {
  PrimType Ty = S.getContext().classify(Arg).value_or(PT_Ptr);
  TYPE_SWITCH(Ty, S.Stk.discard<T>());
}

void cleanupAfterFunctionCall(InterpState &S, CodePtr OpPC) {
  assert(S.Current);
  const Function *CurFunc = S.Current->getFunction();
  assert(CurFunc);

  if (CurFunc->isUnevaluatedBuiltin())
    return;

  // Some builtin functions require us to only look at the call site, since
  // the classified parameter types do not match.
  if (CurFunc->isBuiltin()) {
    const auto *CE =
        cast<CallExpr>(S.Current->Caller->getExpr(S.Current->getRetPC()));
    for (int32_t I = CE->getNumArgs() - 1; I >= 0; --I) {
      const Expr *A = CE->getArg(I);
      popArg(S, A);
    }
    return;
  }

  if (S.Current->Caller && CurFunc->isVariadic()) {
    // CallExpr we're look for is at the return PC of the current function, i.e.
    // in the caller.
    // This code path should be executed very rarely.
    unsigned NumVarArgs;
    const Expr *const *Args = nullptr;
    unsigned NumArgs = 0;
    const Expr *CallSite = S.Current->Caller->getExpr(S.Current->getRetPC());
    if (const auto *CE = dyn_cast<CallExpr>(CallSite)) {
      Args = CE->getArgs();
      NumArgs = CE->getNumArgs();
    } else if (const auto *CE = dyn_cast<CXXConstructExpr>(CallSite)) {
      Args = CE->getArgs();
      NumArgs = CE->getNumArgs();
    } else
      assert(false && "Can't get arguments from that expression type");

    assert(NumArgs >= CurFunc->getNumWrittenParams());
    NumVarArgs = NumArgs - CurFunc->getNumWrittenParams();
    for (unsigned I = 0; I != NumVarArgs; ++I) {
      const Expr *A = Args[NumArgs - 1 - I];
      popArg(S, A);
    }
  }

  // And in any case, remove the fixed parameters (the non-variadic ones)
  // at the end.
  S.Current->popArgs();
}

bool CheckExtern(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (!Ptr.isExtern())
    return true;

  if (Ptr.isInitialized() ||
      (Ptr.getDeclDesc()->asVarDecl() == S.EvaluatingDecl))
    return true;

  if (!S.checkingPotentialConstantExpression() && S.getLangOpts().CPlusPlus) {
    const auto *VD = Ptr.getDeclDesc()->asValueDecl();
    diagnoseNonConstVariable(S, OpPC, VD);
  }
  return false;
}

bool CheckArray(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (!Ptr.isUnknownSizeArray())
    return true;
  const SourceInfo &E = S.Current->getSource(OpPC);
  S.FFDiag(E, diag::note_constexpr_unsized_array_indexed);
  return false;
}

bool CheckLive(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               AccessKinds AK) {
  if (Ptr.isZero()) {
    const auto &Src = S.Current->getSource(OpPC);

    if (Ptr.isField())
      S.FFDiag(Src, diag::note_constexpr_null_subobject) << CSK_Field;
    else
      S.FFDiag(Src, diag::note_constexpr_access_null) << AK;

    return false;
  }

  if (!Ptr.isLive()) {
    const auto &Src = S.Current->getSource(OpPC);
    bool IsTemp = Ptr.isTemporary();

    S.FFDiag(Src, diag::note_constexpr_lifetime_ended, 1) << AK << !IsTemp;

    if (IsTemp)
      S.Note(Ptr.getDeclLoc(), diag::note_constexpr_temporary_here);
    else
      S.Note(Ptr.getDeclLoc(), diag::note_declared_at);

    return false;
  }

  return true;
}

bool CheckConstant(InterpState &S, CodePtr OpPC, const Descriptor *Desc) {
  assert(Desc);

  auto IsConstType = [&S](const VarDecl *VD) -> bool {
    if (VD->isConstexpr())
      return true;

    QualType T = VD->getType();
    if (S.getLangOpts().CPlusPlus && !S.getLangOpts().CPlusPlus11)
      return (T->isSignedIntegerOrEnumerationType() ||
              T->isUnsignedIntegerOrEnumerationType()) &&
             T.isConstQualified();

    if (T.isConstQualified())
      return true;

    if (const auto *RT = T->getAs<ReferenceType>())
      return RT->getPointeeType().isConstQualified();

    if (const auto *PT = T->getAs<PointerType>())
      return PT->getPointeeType().isConstQualified();

    return false;
  };

  if (const auto *D = Desc->asVarDecl();
      D && D->hasGlobalStorage() && D != S.EvaluatingDecl && !IsConstType(D)) {
    diagnoseNonConstVariable(S, OpPC, D);
    return S.inConstantContext();
  }

  return true;
}

static bool CheckConstant(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (Ptr.isIntegralPointer())
    return true;
  return CheckConstant(S, OpPC, Ptr.getDeclDesc());
}

bool CheckNull(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               CheckSubobjectKind CSK) {
  if (!Ptr.isZero())
    return true;
  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_null_subobject)
      << CSK << S.Current->getRange(OpPC);

  return false;
}

bool CheckRange(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                AccessKinds AK) {
  if (!Ptr.isOnePastEnd())
    return true;
  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_access_past_end)
      << AK << S.Current->getRange(OpPC);
  return false;
}

bool CheckRange(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                CheckSubobjectKind CSK) {
  if (!Ptr.isElementPastEnd())
    return true;
  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_past_end_subobject)
      << CSK << S.Current->getRange(OpPC);
  return false;
}

bool CheckSubobject(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                    CheckSubobjectKind CSK) {
  if (!Ptr.isOnePastEnd())
    return true;

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_past_end_subobject)
      << CSK << S.Current->getRange(OpPC);
  return false;
}

bool CheckDowncast(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                   uint32_t Offset) {
  uint32_t MinOffset = Ptr.getDeclDesc()->getMetadataSize();
  uint32_t PtrOffset = Ptr.getByteOffset();

  // We subtract Offset from PtrOffset. The result must be at least
  // MinOffset.
  if (Offset < PtrOffset && (PtrOffset - Offset) >= MinOffset)
    return true;

  const auto *E = cast<CastExpr>(S.Current->getExpr(OpPC));
  QualType TargetQT = E->getType()->getPointeeType();
  QualType MostDerivedQT = Ptr.getDeclPtr().getType();

  S.CCEDiag(E, diag::note_constexpr_invalid_downcast)
      << MostDerivedQT << TargetQT;

  return false;
}

bool CheckConst(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  assert(Ptr.isLive() && "Pointer is not live");
  if (!Ptr.isConst() || Ptr.isMutable())
    return true;

  // The This pointer is writable in constructors and destructors,
  // even if isConst() returns true.
  // TODO(perf): We could be hitting this code path quite a lot in complex
  // constructors. Is there a better way to do this?
  if (S.Current->getFunction()) {
    for (const InterpFrame *Frame = S.Current; Frame; Frame = Frame->Caller) {
      if (const Function *Func = Frame->getFunction();
          Func && (Func->isConstructor() || Func->isDestructor()) &&
          Ptr.block() == Frame->getThis().block()) {
        return true;
      }
    }
  }

  if (!Ptr.isBlockPointer())
    return false;

  const QualType Ty = Ptr.getType();
  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_modify_const_type) << Ty;
  return false;
}

bool CheckMutable(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  assert(Ptr.isLive() && "Pointer is not live");
  if (!Ptr.isMutable())
    return true;

  // In C++14 onwards, it is permitted to read a mutable member whose
  // lifetime began within the evaluation.
  if (S.getLangOpts().CPlusPlus14 &&
      Ptr.block()->getEvalID() == S.Ctx.getEvalID())
    return true;

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  const FieldDecl *Field = Ptr.getField();
  S.FFDiag(Loc, diag::note_constexpr_access_mutable, 1) << AK_Read << Field;
  S.Note(Field->getLocation(), diag::note_declared_at);
  return false;
}

bool CheckVolatile(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                   AccessKinds AK) {
  assert(Ptr.isLive());

  // FIXME: This check here might be kinda expensive. Maybe it would be better
  // to have another field in InlineDescriptor for this?
  if (!Ptr.isBlockPointer())
    return true;

  QualType PtrType = Ptr.getType();
  if (!PtrType.isVolatileQualified())
    return true;

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  if (S.getLangOpts().CPlusPlus)
    S.FFDiag(Loc, diag::note_constexpr_access_volatile_type) << AK << PtrType;
  else
    S.FFDiag(Loc);
  return false;
}

bool CheckInitialized(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                      AccessKinds AK) {
  assert(Ptr.isLive());

  if (Ptr.isInitialized())
    return true;

  if (const auto *VD = Ptr.getDeclDesc()->asVarDecl();
      VD && VD->hasGlobalStorage()) {
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    if (VD->getAnyInitializer()) {
      S.FFDiag(Loc, diag::note_constexpr_var_init_non_constant, 1) << VD;
      S.Note(VD->getLocation(), diag::note_declared_at);
    } else {
      diagnoseMissingInitializer(S, OpPC, VD);
    }
    return false;
  }

  if (!S.checkingPotentialConstantExpression()) {
    S.FFDiag(S.Current->getSource(OpPC), diag::note_constexpr_access_uninit)
        << AK << /*uninitialized=*/true << S.Current->getRange(OpPC);
  }
  return false;
}

bool CheckGlobalInitialized(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (Ptr.isInitialized())
    return true;

  assert(S.getLangOpts().CPlusPlus);
  const auto *VD = cast<VarDecl>(Ptr.getDeclDesc()->asValueDecl());
  if ((!VD->hasConstantInitialization() &&
       VD->mightBeUsableInConstantExpressions(S.getCtx())) ||
      (S.getLangOpts().OpenCL && !S.getLangOpts().CPlusPlus11 &&
       !VD->hasICEInitializer(S.getCtx()))) {
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.FFDiag(Loc, diag::note_constexpr_var_init_non_constant, 1) << VD;
    S.Note(VD->getLocation(), diag::note_declared_at);
  }
  return false;
}

bool CheckLoad(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               AccessKinds AK) {
  if (!CheckLive(S, OpPC, Ptr, AK))
    return false;
  if (!CheckConstant(S, OpPC, Ptr))
    return false;

  if (!CheckDummy(S, OpPC, Ptr, AK))
    return false;
  if (!CheckExtern(S, OpPC, Ptr))
    return false;
  if (!CheckRange(S, OpPC, Ptr, AK))
    return false;
  if (!CheckActive(S, OpPC, Ptr, AK))
    return false;
  if (!CheckInitialized(S, OpPC, Ptr, AK))
    return false;
  if (!CheckTemporary(S, OpPC, Ptr, AK))
    return false;
  if (!CheckMutable(S, OpPC, Ptr))
    return false;
  if (!CheckVolatile(S, OpPC, Ptr, AK))
    return false;
  return true;
}

bool CheckStore(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (!CheckLive(S, OpPC, Ptr, AK_Assign))
    return false;
  if (!CheckDummy(S, OpPC, Ptr, AK_Assign))
    return false;
  if (!CheckExtern(S, OpPC, Ptr))
    return false;
  if (!CheckRange(S, OpPC, Ptr, AK_Assign))
    return false;
  if (!CheckGlobal(S, OpPC, Ptr))
    return false;
  if (!CheckConst(S, OpPC, Ptr))
    return false;
  return true;
}

bool CheckInvoke(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (!CheckLive(S, OpPC, Ptr, AK_MemberCall))
    return false;
  if (!Ptr.isDummy()) {
    if (!CheckExtern(S, OpPC, Ptr))
      return false;
    if (!CheckRange(S, OpPC, Ptr, AK_MemberCall))
      return false;
  }
  return true;
}

bool CheckInit(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  if (!CheckLive(S, OpPC, Ptr, AK_Assign))
    return false;
  if (!CheckRange(S, OpPC, Ptr, AK_Assign))
    return false;
  return true;
}

bool CheckCallable(InterpState &S, CodePtr OpPC, const Function *F) {

  if (F->isVirtual() && !S.getLangOpts().CPlusPlus20) {
    const SourceLocation &Loc = S.Current->getLocation(OpPC);
    S.CCEDiag(Loc, diag::note_constexpr_virtual_call);
    return false;
  }

  if (F->isConstexpr() && F->hasBody() &&
      (F->getDecl()->isConstexpr() || F->getDecl()->hasAttr<MSConstexprAttr>()))
    return true;

  // Implicitly constexpr.
  if (F->isLambdaStaticInvoker())
    return true;

  const SourceLocation &Loc = S.Current->getLocation(OpPC);
  if (S.getLangOpts().CPlusPlus11) {
    const FunctionDecl *DiagDecl = F->getDecl();

    // Invalid decls have been diagnosed before.
    if (DiagDecl->isInvalidDecl())
      return false;

    // If this function is not constexpr because it is an inherited
    // non-constexpr constructor, diagnose that directly.
    const auto *CD = dyn_cast<CXXConstructorDecl>(DiagDecl);
    if (CD && CD->isInheritingConstructor()) {
      const auto *Inherited = CD->getInheritedConstructor().getConstructor();
      if (!Inherited->isConstexpr())
        DiagDecl = CD = Inherited;
    }

    // FIXME: If DiagDecl is an implicitly-declared special member function
    // or an inheriting constructor, we should be much more explicit about why
    // it's not constexpr.
    if (CD && CD->isInheritingConstructor()) {
      S.FFDiag(Loc, diag::note_constexpr_invalid_inhctor, 1)
          << CD->getInheritedConstructor().getConstructor()->getParent();
      S.Note(DiagDecl->getLocation(), diag::note_declared_at);
    } else {
      // Don't emit anything if the function isn't defined and we're checking
      // for a constant expression. It might be defined at the point we're
      // actually calling it.
      bool IsExtern = DiagDecl->getStorageClass() == SC_Extern;
      if (!DiagDecl->isDefined() && !IsExtern && DiagDecl->isConstexpr() &&
          S.checkingPotentialConstantExpression())
        return false;

      // If the declaration is defined, declared 'constexpr' _and_ has a body,
      // the below diagnostic doesn't add anything useful.
      if (DiagDecl->isDefined() && DiagDecl->isConstexpr() &&
          DiagDecl->hasBody())
        return false;

      S.FFDiag(Loc, diag::note_constexpr_invalid_function, 1)
          << DiagDecl->isConstexpr() << (bool)CD << DiagDecl;
      S.Note(DiagDecl->getLocation(), diag::note_declared_at);
    }
  } else {
    S.FFDiag(Loc, diag::note_invalid_subexpr_in_const_expr);
  }

  return false;
}

bool CheckCallDepth(InterpState &S, CodePtr OpPC) {
  if ((S.Current->getDepth() + 1) > S.getLangOpts().ConstexprCallDepth) {
    S.FFDiag(S.Current->getSource(OpPC),
             diag::note_constexpr_depth_limit_exceeded)
        << S.getLangOpts().ConstexprCallDepth;
    return false;
  }

  return true;
}

bool CheckThis(InterpState &S, CodePtr OpPC, const Pointer &This) {
  if (!This.isZero())
    return true;

  const SourceInfo &Loc = S.Current->getSource(OpPC);

  bool IsImplicit = false;
  if (const auto *E = dyn_cast_if_present<CXXThisExpr>(Loc.asExpr()))
    IsImplicit = E->isImplicit();

  if (S.getLangOpts().CPlusPlus11)
    S.FFDiag(Loc, diag::note_constexpr_this) << IsImplicit;
  else
    S.FFDiag(Loc);

  return false;
}

bool CheckPure(InterpState &S, CodePtr OpPC, const CXXMethodDecl *MD) {
  if (!MD->isPureVirtual())
    return true;
  const SourceInfo &E = S.Current->getSource(OpPC);
  S.FFDiag(E, diag::note_constexpr_pure_virtual_call, 1) << MD;
  S.Note(MD->getLocation(), diag::note_declared_at);
  return false;
}

bool CheckFloatResult(InterpState &S, CodePtr OpPC, const Floating &Result,
                      APFloat::opStatus Status) {
  const SourceInfo &E = S.Current->getSource(OpPC);

  // [expr.pre]p4:
  //   If during the evaluation of an expression, the result is not
  //   mathematically defined [...], the behavior is undefined.
  // FIXME: C++ rules require us to not conform to IEEE 754 here.
  if (Result.isNan()) {
    S.CCEDiag(E, diag::note_constexpr_float_arithmetic)
        << /*NaN=*/true << S.Current->getRange(OpPC);
    return S.noteUndefinedBehavior();
  }

  // In a constant context, assume that any dynamic rounding mode or FP
  // exception state matches the default floating-point environment.
  if (S.inConstantContext())
    return true;

  FPOptions FPO = E.asExpr()->getFPFeaturesInEffect(S.Ctx.getLangOpts());

  if ((Status & APFloat::opInexact) &&
      FPO.getRoundingMode() == llvm::RoundingMode::Dynamic) {
    // Inexact result means that it depends on rounding mode. If the requested
    // mode is dynamic, the evaluation cannot be made in compile time.
    S.FFDiag(E, diag::note_constexpr_dynamic_rounding);
    return false;
  }

  if ((Status != APFloat::opOK) &&
      (FPO.getRoundingMode() == llvm::RoundingMode::Dynamic ||
       FPO.getExceptionMode() != LangOptions::FPE_Ignore ||
       FPO.getAllowFEnvAccess())) {
    S.FFDiag(E, diag::note_constexpr_float_arithmetic_strict);
    return false;
  }

  if ((Status & APFloat::opStatus::opInvalidOp) &&
      FPO.getExceptionMode() != LangOptions::FPE_Ignore) {
    // There is no usefully definable result.
    S.FFDiag(E);
    return false;
  }

  return true;
}

bool CheckDynamicMemoryAllocation(InterpState &S, CodePtr OpPC) {
  if (S.getLangOpts().CPlusPlus20)
    return true;

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.CCEDiag(E, diag::note_constexpr_new);
  return true;
}

bool CheckNewDeleteForms(InterpState &S, CodePtr OpPC, bool NewWasArray,
                         bool DeleteIsArray, const Descriptor *D,
                         const Expr *NewExpr) {
  if (NewWasArray == DeleteIsArray)
    return true;

  QualType TypeToDiagnose;
  // We need to shuffle things around a bit here to get a better diagnostic,
  // because the expression we allocated the block for was of type int*,
  // but we want to get the array size right.
  if (D->isArray()) {
    QualType ElemQT = D->getType()->getPointeeType();
    TypeToDiagnose = S.getCtx().getConstantArrayType(
        ElemQT, APInt(64, static_cast<uint64_t>(D->getNumElems()), false),
        nullptr, ArraySizeModifier::Normal, 0);
  } else
    TypeToDiagnose = D->getType()->getPointeeType();

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.FFDiag(E, diag::note_constexpr_new_delete_mismatch)
      << DeleteIsArray << 0 << TypeToDiagnose;
  S.Note(NewExpr->getExprLoc(), diag::note_constexpr_dynamic_alloc_here)
      << NewExpr->getSourceRange();
  return false;
}

bool CheckDeleteSource(InterpState &S, CodePtr OpPC, const Expr *Source,
                       const Pointer &Ptr) {
  if (Source && isa<CXXNewExpr>(Source))
    return true;

  // Whatever this is, we didn't heap allocate it.
  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_delete_not_heap_alloc)
      << Ptr.toDiagnosticString(S.getCtx());

  if (Ptr.isTemporary())
    S.Note(Ptr.getDeclLoc(), diag::note_constexpr_temporary_here);
  else
    S.Note(Ptr.getDeclLoc(), diag::note_declared_at);
  return false;
}

/// We aleady know the given DeclRefExpr is invalid for some reason,
/// now figure out why and print appropriate diagnostics.
bool CheckDeclRef(InterpState &S, CodePtr OpPC, const DeclRefExpr *DR) {
  const ValueDecl *D = DR->getDecl();
  return diagnoseUnknownDecl(S, OpPC, D);
}

bool CheckDummy(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                AccessKinds AK) {
  if (!Ptr.isDummy())
    return true;

  const Descriptor *Desc = Ptr.getDeclDesc();
  const ValueDecl *D = Desc->asValueDecl();
  if (!D)
    return false;

  if (AK == AK_Read || AK == AK_Increment || AK == AK_Decrement)
    return diagnoseUnknownDecl(S, OpPC, D);

  assert(AK == AK_Assign);
  if (S.getLangOpts().CPlusPlus11) {
    const SourceInfo &E = S.Current->getSource(OpPC);
    S.FFDiag(E, diag::note_constexpr_modify_global);
  }
  return false;
}

bool CheckNonNullArgs(InterpState &S, CodePtr OpPC, const Function *F,
                      const CallExpr *CE, unsigned ArgSize) {
  auto Args = llvm::ArrayRef(CE->getArgs(), CE->getNumArgs());
  auto NonNullArgs = collectNonNullArgs(F->getDecl(), Args);
  unsigned Offset = 0;
  unsigned Index = 0;
  for (const Expr *Arg : Args) {
    if (NonNullArgs[Index] && Arg->getType()->isPointerType()) {
      const Pointer &ArgPtr = S.Stk.peek<Pointer>(ArgSize - Offset);
      if (ArgPtr.isZero()) {
        const SourceLocation &Loc = S.Current->getLocation(OpPC);
        S.CCEDiag(Loc, diag::note_non_null_attribute_failed);
        return false;
      }
    }

    Offset += align(primSize(S.Ctx.classify(Arg).value_or(PT_Ptr)));
    ++Index;
  }
  return true;
}

// FIXME: This is similar to code we already have in Compiler.cpp.
// I think it makes sense to instead add the field and base destruction stuff
// to the destructor Function itself. Then destroying a record would really
// _just_ be calling its destructor. That would also help with the diagnostic
// difference when the destructor or a field/base fails.
static bool runRecordDestructor(InterpState &S, CodePtr OpPC,
                                const Pointer &BasePtr,
                                const Descriptor *Desc) {
  assert(Desc->isRecord());
  const Record *R = Desc->ElemRecord;
  assert(R);

  // Fields.
  for (const Record::Field &Field : llvm::reverse(R->fields())) {
    const Descriptor *D = Field.Desc;
    if (D->isRecord()) {
      if (!runRecordDestructor(S, OpPC, BasePtr.atField(Field.Offset), D))
        return false;
    } else if (D->isCompositeArray()) {
      const Descriptor *ElemDesc = Desc->ElemDesc;
      assert(ElemDesc->isRecord());
      for (unsigned I = 0; I != Desc->getNumElems(); ++I) {
        if (!runRecordDestructor(S, OpPC, BasePtr.atIndex(I).narrow(),
                                 ElemDesc))
          return false;
      }
    }
  }

  // Destructor of this record.
  if (const CXXDestructorDecl *Dtor = R->getDestructor();
      Dtor && !Dtor->isTrivial()) {
    const Function *DtorFunc = S.getContext().getOrCreateFunction(Dtor);
    if (!DtorFunc)
      return false;

    S.Stk.push<Pointer>(BasePtr);
    if (!Call(S, OpPC, DtorFunc, 0))
      return false;
  }

  // Bases.
  for (const Record::Base &Base : llvm::reverse(R->bases())) {
    if (!runRecordDestructor(S, OpPC, BasePtr.atField(Base.Offset), Base.Desc))
      return false;
  }

  return true;
}

bool RunDestructors(InterpState &S, CodePtr OpPC, const Block *B) {
  assert(B);
  const Descriptor *Desc = B->getDescriptor();

  if (Desc->isPrimitive() || Desc->isPrimitiveArray())
    return true;

  assert(Desc->isRecord() || Desc->isCompositeArray());

  if (Desc->isCompositeArray()) {
    const Descriptor *ElemDesc = Desc->ElemDesc;
    assert(ElemDesc->isRecord());

    Pointer RP(const_cast<Block *>(B));
    for (unsigned I = 0; I != Desc->getNumElems(); ++I) {
      if (!runRecordDestructor(S, OpPC, RP.atIndex(I).narrow(), ElemDesc))
        return false;
    }
    return true;
  }

  assert(Desc->isRecord());
  return runRecordDestructor(S, OpPC, Pointer(const_cast<Block *>(B)), Desc);
}

void diagnoseEnumValue(InterpState &S, CodePtr OpPC, const EnumDecl *ED,
                       const APSInt &Value) {
  llvm::APInt Min;
  llvm::APInt Max;

  if (S.EvaluatingDecl && !S.EvaluatingDecl->isConstexpr())
    return;

  ED->getValueRange(Max, Min);
  --Max;

  if (ED->getNumNegativeBits() &&
      (Max.slt(Value.getSExtValue()) || Min.sgt(Value.getSExtValue()))) {
    const SourceLocation &Loc = S.Current->getLocation(OpPC);
    S.report(Loc, diag::warn_constexpr_unscoped_enum_out_of_range)
        << llvm::toString(Value, 10) << Min.getSExtValue() << Max.getSExtValue()
        << ED;
  } else if (!ED->getNumNegativeBits() && Max.ult(Value.getZExtValue())) {
    const SourceLocation &Loc = S.Current->getLocation(OpPC);
    S.report(Loc, diag::warn_constexpr_unscoped_enum_out_of_range)
        << llvm::toString(Value, 10) << Min.getZExtValue() << Max.getZExtValue()
        << ED;
  }
}

// https://github.com/llvm/llvm-project/issues/102513
#if defined(_WIN32) && !defined(__clang__) && !defined(NDEBUG)
#pragma optimize("", off)
#endif
bool Interpret(InterpState &S, APValue &Result) {
  // The current stack frame when we started Interpret().
  // This is being used by the ops to determine wheter
  // to return from this function and thus terminate
  // interpretation.
  const InterpFrame *StartFrame = S.Current;
  assert(!S.Current->isRoot());
  CodePtr PC = S.Current->getPC();

  // Empty program.
  if (!PC)
    return true;

  for (;;) {
    auto Op = PC.read<Opcode>();
    CodePtr OpPC = PC;

    switch (Op) {
#define GET_INTERP
#include "Opcodes.inc"
#undef GET_INTERP
    }
  }
}
// https://github.com/llvm/llvm-project/issues/102513
#if defined(_WIN32) && !defined(__clang__) && !defined(NDEBUG)
#pragma optimize("", on)
#endif

} // namespace interp
} // namespace clang
