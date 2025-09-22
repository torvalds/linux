//===--- InterpBuiltin.cpp - Interpreter for the constexpr VM ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "../ExprConstShared.h"
#include "Boolean.h"
#include "Interp.h"
#include "PrimType.h"
#include "clang/AST/OSLog.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Support/SipHash.h"

namespace clang {
namespace interp {

static unsigned callArgSize(const InterpState &S, const CallExpr *C) {
  unsigned O = 0;

  for (const Expr *E : C->arguments()) {
    O += align(primSize(*S.getContext().classify(E)));
  }

  return O;
}

template <typename T>
static T getParam(const InterpFrame *Frame, unsigned Index) {
  assert(Frame->getFunction()->getNumParams() > Index);
  unsigned Offset = Frame->getFunction()->getParamOffset(Index);
  return Frame->getParam<T>(Offset);
}

PrimType getIntPrimType(const InterpState &S) {
  const TargetInfo &TI = S.getCtx().getTargetInfo();
  unsigned IntWidth = TI.getIntWidth();

  if (IntWidth == 32)
    return PT_Sint32;
  else if (IntWidth == 16)
    return PT_Sint16;
  llvm_unreachable("Int isn't 16 or 32 bit?");
}

PrimType getLongPrimType(const InterpState &S) {
  const TargetInfo &TI = S.getCtx().getTargetInfo();
  unsigned LongWidth = TI.getLongWidth();

  if (LongWidth == 64)
    return PT_Sint64;
  else if (LongWidth == 32)
    return PT_Sint32;
  else if (LongWidth == 16)
    return PT_Sint16;
  llvm_unreachable("long isn't 16, 32 or 64 bit?");
}

/// Peek an integer value from the stack into an APSInt.
static APSInt peekToAPSInt(InterpStack &Stk, PrimType T, size_t Offset = 0) {
  if (Offset == 0)
    Offset = align(primSize(T));

  APSInt R;
  INT_TYPE_SWITCH(T, R = Stk.peek<T>(Offset).toAPSInt());

  return R;
}

/// Pushes \p Val on the stack as the type given by \p QT.
static void pushInteger(InterpState &S, const APSInt &Val, QualType QT) {
  assert(QT->isSignedIntegerOrEnumerationType() ||
         QT->isUnsignedIntegerOrEnumerationType());
  std::optional<PrimType> T = S.getContext().classify(QT);
  assert(T);

  if (QT->isSignedIntegerOrEnumerationType()) {
    int64_t V = Val.getSExtValue();
    INT_TYPE_SWITCH(*T, { S.Stk.push<T>(T::from(V)); });
  } else {
    assert(QT->isUnsignedIntegerOrEnumerationType());
    uint64_t V = Val.getZExtValue();
    INT_TYPE_SWITCH(*T, { S.Stk.push<T>(T::from(V)); });
  }
}

template <typename T>
static void pushInteger(InterpState &S, T Val, QualType QT) {
  if constexpr (std::is_same_v<T, APInt>)
    pushInteger(S, APSInt(Val, !std::is_signed_v<T>), QT);
  else
    pushInteger(S,
                APSInt(APInt(sizeof(T) * 8, static_cast<uint64_t>(Val),
                             std::is_signed_v<T>),
                       !std::is_signed_v<T>),
                QT);
}

static void assignInteger(Pointer &Dest, PrimType ValueT, const APSInt &Value) {
  INT_TYPE_SWITCH_NO_BOOL(
      ValueT, { Dest.deref<T>() = T::from(static_cast<T>(Value)); });
}

static bool retPrimValue(InterpState &S, CodePtr OpPC, APValue &Result,
                         std::optional<PrimType> &T) {
  if (!T)
    return RetVoid(S, OpPC, Result);

#define RET_CASE(X)                                                            \
  case X:                                                                      \
    return Ret<X>(S, OpPC, Result);
  switch (*T) {
    RET_CASE(PT_Ptr);
    RET_CASE(PT_FnPtr);
    RET_CASE(PT_Float);
    RET_CASE(PT_Bool);
    RET_CASE(PT_Sint8);
    RET_CASE(PT_Uint8);
    RET_CASE(PT_Sint16);
    RET_CASE(PT_Uint16);
    RET_CASE(PT_Sint32);
    RET_CASE(PT_Uint32);
    RET_CASE(PT_Sint64);
    RET_CASE(PT_Uint64);
  default:
    llvm_unreachable("Unsupported return type for builtin function");
  }
#undef RET_CASE
}

static bool interp__builtin_is_constant_evaluated(InterpState &S, CodePtr OpPC,
                                                  const InterpFrame *Frame,
                                                  const CallExpr *Call) {
  // The current frame is the one for __builtin_is_constant_evaluated.
  // The one above that, potentially the one for std::is_constant_evaluated().
  if (S.inConstantContext() && !S.checkingPotentialConstantExpression() &&
      Frame->Caller && S.getEvalStatus().Diag) {
    auto isStdCall = [](const FunctionDecl *F) -> bool {
      return F && F->isInStdNamespace() && F->getIdentifier() &&
             F->getIdentifier()->isStr("is_constant_evaluated");
    };
    const InterpFrame *Caller = Frame->Caller;

    if (Caller->Caller && isStdCall(Caller->getCallee())) {
      const Expr *E = Caller->Caller->getExpr(Caller->getRetPC());
      S.report(E->getExprLoc(),
               diag::warn_is_constant_evaluated_always_true_constexpr)
          << "std::is_constant_evaluated" << E->getSourceRange();
    } else {
      const Expr *E = Frame->Caller->getExpr(Frame->getRetPC());
      S.report(E->getExprLoc(),
               diag::warn_is_constant_evaluated_always_true_constexpr)
          << "__builtin_is_constant_evaluated" << E->getSourceRange();
    }
  }

  S.Stk.push<Boolean>(Boolean::from(S.inConstantContext()));
  return true;
}

static bool interp__builtin_strcmp(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame,
                                   const CallExpr *Call) {
  const Pointer &A = getParam<Pointer>(Frame, 0);
  const Pointer &B = getParam<Pointer>(Frame, 1);

  if (!CheckLive(S, OpPC, A, AK_Read) || !CheckLive(S, OpPC, B, AK_Read))
    return false;

  if (A.isDummy() || B.isDummy())
    return false;

  assert(A.getFieldDesc()->isPrimitiveArray());
  assert(B.getFieldDesc()->isPrimitiveArray());

  unsigned IndexA = A.getIndex();
  unsigned IndexB = B.getIndex();
  int32_t Result = 0;
  for (;; ++IndexA, ++IndexB) {
    const Pointer &PA = A.atIndex(IndexA);
    const Pointer &PB = B.atIndex(IndexB);
    if (!CheckRange(S, OpPC, PA, AK_Read) ||
        !CheckRange(S, OpPC, PB, AK_Read)) {
      return false;
    }
    uint8_t CA = PA.deref<uint8_t>();
    uint8_t CB = PB.deref<uint8_t>();

    if (CA > CB) {
      Result = 1;
      break;
    } else if (CA < CB) {
      Result = -1;
      break;
    }
    if (CA == 0 || CB == 0)
      break;
  }

  pushInteger(S, Result, Call->getType());
  return true;
}

static bool interp__builtin_strlen(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame,
                                   const CallExpr *Call) {
  const Pointer &StrPtr = getParam<Pointer>(Frame, 0);

  if (!CheckArray(S, OpPC, StrPtr))
    return false;

  if (!CheckLive(S, OpPC, StrPtr, AK_Read))
    return false;

  if (!CheckDummy(S, OpPC, StrPtr, AK_Read))
    return false;

  assert(StrPtr.getFieldDesc()->isPrimitiveArray());

  size_t Len = 0;
  for (size_t I = StrPtr.getIndex();; ++I, ++Len) {
    const Pointer &ElemPtr = StrPtr.atIndex(I);

    if (!CheckRange(S, OpPC, ElemPtr, AK_Read))
      return false;

    uint8_t Val = ElemPtr.deref<uint8_t>();
    if (Val == 0)
      break;
  }

  pushInteger(S, Len, Call->getType());

  return true;
}

static bool interp__builtin_nan(InterpState &S, CodePtr OpPC,
                                const InterpFrame *Frame, const Function *F,
                                bool Signaling) {
  const Pointer &Arg = getParam<Pointer>(Frame, 0);

  if (!CheckLoad(S, OpPC, Arg))
    return false;

  assert(Arg.getFieldDesc()->isPrimitiveArray());

  // Convert the given string to an integer using StringRef's API.
  llvm::APInt Fill;
  std::string Str;
  assert(Arg.getNumElems() >= 1);
  for (unsigned I = 0;; ++I) {
    const Pointer &Elem = Arg.atIndex(I);

    if (!CheckLoad(S, OpPC, Elem))
      return false;

    if (Elem.deref<int8_t>() == 0)
      break;

    Str += Elem.deref<char>();
  }

  // Treat empty strings as if they were zero.
  if (Str.empty())
    Fill = llvm::APInt(32, 0);
  else if (StringRef(Str).getAsInteger(0, Fill))
    return false;

  const llvm::fltSemantics &TargetSemantics =
      S.getCtx().getFloatTypeSemantics(F->getDecl()->getReturnType());

  Floating Result;
  if (S.getCtx().getTargetInfo().isNan2008()) {
    if (Signaling)
      Result = Floating(
          llvm::APFloat::getSNaN(TargetSemantics, /*Negative=*/false, &Fill));
    else
      Result = Floating(
          llvm::APFloat::getQNaN(TargetSemantics, /*Negative=*/false, &Fill));
  } else {
    // Prior to IEEE 754-2008, architectures were allowed to choose whether
    // the first bit of their significand was set for qNaN or sNaN. MIPS chose
    // a different encoding to what became a standard in 2008, and for pre-
    // 2008 revisions, MIPS interpreted sNaN-2008 as qNan and qNaN-2008 as
    // sNaN. This is now known as "legacy NaN" encoding.
    if (Signaling)
      Result = Floating(
          llvm::APFloat::getQNaN(TargetSemantics, /*Negative=*/false, &Fill));
    else
      Result = Floating(
          llvm::APFloat::getSNaN(TargetSemantics, /*Negative=*/false, &Fill));
  }

  S.Stk.push<Floating>(Result);
  return true;
}

static bool interp__builtin_inf(InterpState &S, CodePtr OpPC,
                                const InterpFrame *Frame, const Function *F) {
  const llvm::fltSemantics &TargetSemantics =
      S.getCtx().getFloatTypeSemantics(F->getDecl()->getReturnType());

  S.Stk.push<Floating>(Floating::getInf(TargetSemantics));
  return true;
}

static bool interp__builtin_copysign(InterpState &S, CodePtr OpPC,
                                     const InterpFrame *Frame,
                                     const Function *F) {
  const Floating &Arg1 = getParam<Floating>(Frame, 0);
  const Floating &Arg2 = getParam<Floating>(Frame, 1);

  APFloat Copy = Arg1.getAPFloat();
  Copy.copySign(Arg2.getAPFloat());
  S.Stk.push<Floating>(Floating(Copy));

  return true;
}

static bool interp__builtin_fmin(InterpState &S, CodePtr OpPC,
                                 const InterpFrame *Frame, const Function *F) {
  const Floating &LHS = getParam<Floating>(Frame, 0);
  const Floating &RHS = getParam<Floating>(Frame, 1);

  Floating Result;

  // When comparing zeroes, return -0.0 if one of the zeroes is negative.
  if (LHS.isZero() && RHS.isZero() && RHS.isNegative())
    Result = RHS;
  else if (LHS.isNan() || RHS < LHS)
    Result = RHS;
  else
    Result = LHS;

  S.Stk.push<Floating>(Result);
  return true;
}

static bool interp__builtin_fmax(InterpState &S, CodePtr OpPC,
                                 const InterpFrame *Frame,
                                 const Function *Func) {
  const Floating &LHS = getParam<Floating>(Frame, 0);
  const Floating &RHS = getParam<Floating>(Frame, 1);

  Floating Result;

  // When comparing zeroes, return +0.0 if one of the zeroes is positive.
  if (LHS.isZero() && RHS.isZero() && LHS.isNegative())
    Result = RHS;
  else if (LHS.isNan() || RHS > LHS)
    Result = RHS;
  else
    Result = LHS;

  S.Stk.push<Floating>(Result);
  return true;
}

/// Defined as __builtin_isnan(...), to accommodate the fact that it can
/// take a float, double, long double, etc.
/// But for us, that's all a Floating anyway.
static bool interp__builtin_isnan(InterpState &S, CodePtr OpPC,
                                  const InterpFrame *Frame, const Function *F,
                                  const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isNan(), Call->getType());
  return true;
}

static bool interp__builtin_issignaling(InterpState &S, CodePtr OpPC,
                                        const InterpFrame *Frame,
                                        const Function *F,
                                        const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isSignaling(), Call->getType());
  return true;
}

static bool interp__builtin_isinf(InterpState &S, CodePtr OpPC,
                                  const InterpFrame *Frame, const Function *F,
                                  bool CheckSign, const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();
  bool IsInf = Arg.isInf();

  if (CheckSign)
    pushInteger(S, IsInf ? (Arg.isNegative() ? -1 : 1) : 0, Call->getType());
  else
    pushInteger(S, Arg.isInf(), Call->getType());
  return true;
}

static bool interp__builtin_isfinite(InterpState &S, CodePtr OpPC,
                                     const InterpFrame *Frame,
                                     const Function *F, const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isFinite(), Call->getType());
  return true;
}

static bool interp__builtin_isnormal(InterpState &S, CodePtr OpPC,
                                     const InterpFrame *Frame,
                                     const Function *F, const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isNormal(), Call->getType());
  return true;
}

static bool interp__builtin_issubnormal(InterpState &S, CodePtr OpPC,
                                        const InterpFrame *Frame,
                                        const Function *F,
                                        const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isDenormal(), Call->getType());
  return true;
}

static bool interp__builtin_iszero(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame, const Function *F,
                                   const CallExpr *Call) {
  const Floating &Arg = S.Stk.peek<Floating>();

  pushInteger(S, Arg.isZero(), Call->getType());
  return true;
}

/// First parameter to __builtin_isfpclass is the floating value, the
/// second one is an integral value.
static bool interp__builtin_isfpclass(InterpState &S, CodePtr OpPC,
                                      const InterpFrame *Frame,
                                      const Function *Func,
                                      const CallExpr *Call) {
  PrimType FPClassArgT = *S.getContext().classify(Call->getArg(1)->getType());
  APSInt FPClassArg = peekToAPSInt(S.Stk, FPClassArgT);
  const Floating &F =
      S.Stk.peek<Floating>(align(primSize(FPClassArgT) + primSize(PT_Float)));

  int32_t Result =
      static_cast<int32_t>((F.classify() & FPClassArg).getZExtValue());
  pushInteger(S, Result, Call->getType());

  return true;
}

/// Five int values followed by one floating value.
static bool interp__builtin_fpclassify(InterpState &S, CodePtr OpPC,
                                       const InterpFrame *Frame,
                                       const Function *Func,
                                       const CallExpr *Call) {
  const Floating &Val = S.Stk.peek<Floating>();

  unsigned Index;
  switch (Val.getCategory()) {
  case APFloat::fcNaN:
    Index = 0;
    break;
  case APFloat::fcInfinity:
    Index = 1;
    break;
  case APFloat::fcNormal:
    Index = Val.isDenormal() ? 3 : 2;
    break;
  case APFloat::fcZero:
    Index = 4;
    break;
  }

  // The last argument is first on the stack.
  assert(Index <= 4);
  unsigned IntSize = primSize(getIntPrimType(S));
  unsigned Offset =
      align(primSize(PT_Float)) + ((1 + (4 - Index)) * align(IntSize));

  APSInt I = peekToAPSInt(S.Stk, getIntPrimType(S), Offset);
  pushInteger(S, I, Call->getType());
  return true;
}

// The C standard says "fabs raises no floating-point exceptions,
// even if x is a signaling NaN. The returned value is independent of
// the current rounding direction mode."  Therefore constant folding can
// proceed without regard to the floating point settings.
// Reference, WG14 N2478 F.10.4.3
static bool interp__builtin_fabs(InterpState &S, CodePtr OpPC,
                                 const InterpFrame *Frame,
                                 const Function *Func) {
  const Floating &Val = getParam<Floating>(Frame, 0);

  S.Stk.push<Floating>(Floating::abs(Val));
  return true;
}

static bool interp__builtin_popcount(InterpState &S, CodePtr OpPC,
                                     const InterpFrame *Frame,
                                     const Function *Func,
                                     const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Val = peekToAPSInt(S.Stk, ArgT);
  pushInteger(S, Val.popcount(), Call->getType());
  return true;
}

static bool interp__builtin_parity(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame,
                                   const Function *Func, const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Val = peekToAPSInt(S.Stk, ArgT);
  pushInteger(S, Val.popcount() % 2, Call->getType());
  return true;
}

static bool interp__builtin_clrsb(InterpState &S, CodePtr OpPC,
                                  const InterpFrame *Frame,
                                  const Function *Func, const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Val = peekToAPSInt(S.Stk, ArgT);
  pushInteger(S, Val.getBitWidth() - Val.getSignificantBits(), Call->getType());
  return true;
}

static bool interp__builtin_bitreverse(InterpState &S, CodePtr OpPC,
                                       const InterpFrame *Frame,
                                       const Function *Func,
                                       const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Val = peekToAPSInt(S.Stk, ArgT);
  pushInteger(S, Val.reverseBits(), Call->getType());
  return true;
}

static bool interp__builtin_classify_type(InterpState &S, CodePtr OpPC,
                                          const InterpFrame *Frame,
                                          const Function *Func,
                                          const CallExpr *Call) {
  // This is an unevaluated call, so there are no arguments on the stack.
  assert(Call->getNumArgs() == 1);
  const Expr *Arg = Call->getArg(0);

  GCCTypeClass ResultClass =
      EvaluateBuiltinClassifyType(Arg->getType(), S.getLangOpts());
  int32_t ReturnVal = static_cast<int32_t>(ResultClass);
  pushInteger(S, ReturnVal, Call->getType());
  return true;
}

// __builtin_expect(long, long)
// __builtin_expect_with_probability(long, long, double)
static bool interp__builtin_expect(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame,
                                   const Function *Func, const CallExpr *Call) {
  // The return value is simply the value of the first parameter.
  // We ignore the probability.
  unsigned NumArgs = Call->getNumArgs();
  assert(NumArgs == 2 || NumArgs == 3);

  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  unsigned Offset = align(primSize(getLongPrimType(S))) * 2;
  if (NumArgs == 3)
    Offset += align(primSize(PT_Float));

  APSInt Val = peekToAPSInt(S.Stk, ArgT, Offset);
  pushInteger(S, Val, Call->getType());
  return true;
}

/// rotateleft(value, amount)
static bool interp__builtin_rotate(InterpState &S, CodePtr OpPC,
                                   const InterpFrame *Frame,
                                   const Function *Func, const CallExpr *Call,
                                   bool Right) {
  PrimType AmountT = *S.getContext().classify(Call->getArg(1)->getType());
  PrimType ValueT = *S.getContext().classify(Call->getArg(0)->getType());

  APSInt Amount = peekToAPSInt(S.Stk, AmountT);
  APSInt Value = peekToAPSInt(
      S.Stk, ValueT, align(primSize(AmountT)) + align(primSize(ValueT)));

  APSInt Result;
  if (Right)
    Result = APSInt(Value.rotr(Amount.urem(Value.getBitWidth())),
                    /*IsUnsigned=*/true);
  else // Left.
    Result = APSInt(Value.rotl(Amount.urem(Value.getBitWidth())),
                    /*IsUnsigned=*/true);

  pushInteger(S, Result, Call->getType());
  return true;
}

static bool interp__builtin_ffs(InterpState &S, CodePtr OpPC,
                                const InterpFrame *Frame, const Function *Func,
                                const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Value = peekToAPSInt(S.Stk, ArgT);

  uint64_t N = Value.countr_zero();
  pushInteger(S, N == Value.getBitWidth() ? 0 : N + 1, Call->getType());
  return true;
}

static bool interp__builtin_addressof(InterpState &S, CodePtr OpPC,
                                      const InterpFrame *Frame,
                                      const Function *Func,
                                      const CallExpr *Call) {
  assert(Call->getArg(0)->isLValue());
  PrimType PtrT = S.getContext().classify(Call->getArg(0)).value_or(PT_Ptr);

  if (PtrT == PT_FnPtr) {
    const FunctionPointer &Arg = S.Stk.peek<FunctionPointer>();
    S.Stk.push<FunctionPointer>(Arg);
  } else if (PtrT == PT_Ptr) {
    const Pointer &Arg = S.Stk.peek<Pointer>();
    S.Stk.push<Pointer>(Arg);
  } else {
    assert(false && "Unsupported pointer type passed to __builtin_addressof()");
  }
  return true;
}

static bool interp__builtin_move(InterpState &S, CodePtr OpPC,
                                 const InterpFrame *Frame, const Function *Func,
                                 const CallExpr *Call) {

  PrimType ArgT = S.getContext().classify(Call->getArg(0)).value_or(PT_Ptr);

  TYPE_SWITCH(ArgT, const T &Arg = S.Stk.peek<T>(); S.Stk.push<T>(Arg););

  return Func->getDecl()->isConstexpr();
}

static bool interp__builtin_eh_return_data_regno(InterpState &S, CodePtr OpPC,
                                                 const InterpFrame *Frame,
                                                 const Function *Func,
                                                 const CallExpr *Call) {
  PrimType ArgT = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt Arg = peekToAPSInt(S.Stk, ArgT);

  int Result =
      S.getCtx().getTargetInfo().getEHDataRegisterNumber(Arg.getZExtValue());
  pushInteger(S, Result, Call->getType());
  return true;
}

/// Just takes the first Argument to the call and puts it on the stack.
static bool noopPointer(InterpState &S, CodePtr OpPC, const InterpFrame *Frame,
                        const Function *Func, const CallExpr *Call) {
  const Pointer &Arg = S.Stk.peek<Pointer>();
  S.Stk.push<Pointer>(Arg);
  return true;
}

// Two integral values followed by a pointer (lhs, rhs, resultOut)
static bool interp__builtin_overflowop(InterpState &S, CodePtr OpPC,
                                       const InterpFrame *Frame,
                                       const Function *Func,
                                       const CallExpr *Call) {
  Pointer &ResultPtr = S.Stk.peek<Pointer>();
  if (ResultPtr.isDummy())
    return false;

  unsigned BuiltinOp = Func->getBuiltinID();
  PrimType RHST = *S.getContext().classify(Call->getArg(1)->getType());
  PrimType LHST = *S.getContext().classify(Call->getArg(0)->getType());
  APSInt RHS = peekToAPSInt(S.Stk, RHST,
                            align(primSize(PT_Ptr)) + align(primSize(RHST)));
  APSInt LHS = peekToAPSInt(S.Stk, LHST,
                            align(primSize(PT_Ptr)) + align(primSize(RHST)) +
                                align(primSize(LHST)));
  QualType ResultType = Call->getArg(2)->getType()->getPointeeType();
  PrimType ResultT = *S.getContext().classify(ResultType);
  bool Overflow;

  APSInt Result;
  if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
      BuiltinOp == Builtin::BI__builtin_sub_overflow ||
      BuiltinOp == Builtin::BI__builtin_mul_overflow) {
    bool IsSigned = LHS.isSigned() || RHS.isSigned() ||
                    ResultType->isSignedIntegerOrEnumerationType();
    bool AllSigned = LHS.isSigned() && RHS.isSigned() &&
                     ResultType->isSignedIntegerOrEnumerationType();
    uint64_t LHSSize = LHS.getBitWidth();
    uint64_t RHSSize = RHS.getBitWidth();
    uint64_t ResultSize = S.getCtx().getTypeSize(ResultType);
    uint64_t MaxBits = std::max(std::max(LHSSize, RHSSize), ResultSize);

    // Add an additional bit if the signedness isn't uniformly agreed to. We
    // could do this ONLY if there is a signed and an unsigned that both have
    // MaxBits, but the code to check that is pretty nasty.  The issue will be
    // caught in the shrink-to-result later anyway.
    if (IsSigned && !AllSigned)
      ++MaxBits;

    LHS = APSInt(LHS.extOrTrunc(MaxBits), !IsSigned);
    RHS = APSInt(RHS.extOrTrunc(MaxBits), !IsSigned);
    Result = APSInt(MaxBits, !IsSigned);
  }

  // Find largest int.
  switch (BuiltinOp) {
  default:
    llvm_unreachable("Invalid value for BuiltinOp");
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
    Result = LHS.isSigned() ? LHS.sadd_ov(RHS, Overflow)
                            : LHS.uadd_ov(RHS, Overflow);
    break;
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
    Result = LHS.isSigned() ? LHS.ssub_ov(RHS, Overflow)
                            : LHS.usub_ov(RHS, Overflow);
    break;
  case Builtin::BI__builtin_mul_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
    Result = LHS.isSigned() ? LHS.smul_ov(RHS, Overflow)
                            : LHS.umul_ov(RHS, Overflow);
    break;
  }

  // In the case where multiple sizes are allowed, truncate and see if
  // the values are the same.
  if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
      BuiltinOp == Builtin::BI__builtin_sub_overflow ||
      BuiltinOp == Builtin::BI__builtin_mul_overflow) {
    // APSInt doesn't have a TruncOrSelf, so we use extOrTrunc instead,
    // since it will give us the behavior of a TruncOrSelf in the case where
    // its parameter <= its size.  We previously set Result to be at least the
    // type-size of the result, so getTypeSize(ResultType) <= Resu
    APSInt Temp = Result.extOrTrunc(S.getCtx().getTypeSize(ResultType));
    Temp.setIsSigned(ResultType->isSignedIntegerOrEnumerationType());

    if (!APSInt::isSameValue(Temp, Result))
      Overflow = true;
    Result = Temp;
  }

  // Write Result to ResultPtr and put Overflow on the stacl.
  assignInteger(ResultPtr, ResultT, Result);
  ResultPtr.initialize();
  assert(Func->getDecl()->getReturnType()->isBooleanType());
  S.Stk.push<Boolean>(Overflow);
  return true;
}

/// Three integral values followed by a pointer (lhs, rhs, carry, carryOut).
static bool interp__builtin_carryop(InterpState &S, CodePtr OpPC,
                                    const InterpFrame *Frame,
                                    const Function *Func,
                                    const CallExpr *Call) {
  unsigned BuiltinOp = Func->getBuiltinID();
  PrimType LHST = *S.getContext().classify(Call->getArg(0)->getType());
  PrimType RHST = *S.getContext().classify(Call->getArg(1)->getType());
  PrimType CarryT = *S.getContext().classify(Call->getArg(2)->getType());
  APSInt RHS = peekToAPSInt(S.Stk, RHST,
                            align(primSize(PT_Ptr)) + align(primSize(CarryT)) +
                                align(primSize(RHST)));
  APSInt LHS =
      peekToAPSInt(S.Stk, LHST,
                   align(primSize(PT_Ptr)) + align(primSize(RHST)) +
                       align(primSize(CarryT)) + align(primSize(LHST)));
  APSInt CarryIn = peekToAPSInt(
      S.Stk, LHST, align(primSize(PT_Ptr)) + align(primSize(CarryT)));
  APSInt CarryOut;

  APSInt Result;
  // Copy the number of bits and sign.
  Result = LHS;
  CarryOut = LHS;

  bool FirstOverflowed = false;
  bool SecondOverflowed = false;
  switch (BuiltinOp) {
  default:
    llvm_unreachable("Invalid value for BuiltinOp");
  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
    Result =
        LHS.uadd_ov(RHS, FirstOverflowed).uadd_ov(CarryIn, SecondOverflowed);
    break;
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll:
    Result =
        LHS.usub_ov(RHS, FirstOverflowed).usub_ov(CarryIn, SecondOverflowed);
    break;
  }
  // It is possible for both overflows to happen but CGBuiltin uses an OR so
  // this is consistent.
  CarryOut = (uint64_t)(FirstOverflowed | SecondOverflowed);

  Pointer &CarryOutPtr = S.Stk.peek<Pointer>();
  QualType CarryOutType = Call->getArg(3)->getType()->getPointeeType();
  PrimType CarryOutT = *S.getContext().classify(CarryOutType);
  assignInteger(CarryOutPtr, CarryOutT, CarryOut);
  CarryOutPtr.initialize();

  assert(Call->getType() == Call->getArg(0)->getType());
  pushInteger(S, Result, Call->getType());
  return true;
}

static bool interp__builtin_clz(InterpState &S, CodePtr OpPC,
                                const InterpFrame *Frame, const Function *Func,
                                const CallExpr *Call) {
  unsigned CallSize = callArgSize(S, Call);
  unsigned BuiltinOp = Func->getBuiltinID();
  PrimType ValT = *S.getContext().classify(Call->getArg(0));
  const APSInt &Val = peekToAPSInt(S.Stk, ValT, CallSize);

  // When the argument is 0, the result of GCC builtins is undefined, whereas
  // for Microsoft intrinsics, the result is the bit-width of the argument.
  bool ZeroIsUndefined = BuiltinOp != Builtin::BI__lzcnt16 &&
                         BuiltinOp != Builtin::BI__lzcnt &&
                         BuiltinOp != Builtin::BI__lzcnt64;

  if (Val == 0) {
    if (Func->getBuiltinID() == Builtin::BI__builtin_clzg &&
        Call->getNumArgs() == 2) {
      // We have a fallback parameter.
      PrimType FallbackT = *S.getContext().classify(Call->getArg(1));
      const APSInt &Fallback = peekToAPSInt(S.Stk, FallbackT);
      pushInteger(S, Fallback, Call->getType());
      return true;
    }

    if (ZeroIsUndefined)
      return false;
  }

  pushInteger(S, Val.countl_zero(), Call->getType());
  return true;
}

static bool interp__builtin_ctz(InterpState &S, CodePtr OpPC,
                                const InterpFrame *Frame, const Function *Func,
                                const CallExpr *Call) {
  unsigned CallSize = callArgSize(S, Call);
  PrimType ValT = *S.getContext().classify(Call->getArg(0));
  const APSInt &Val = peekToAPSInt(S.Stk, ValT, CallSize);

  if (Val == 0) {
    if (Func->getBuiltinID() == Builtin::BI__builtin_ctzg &&
        Call->getNumArgs() == 2) {
      // We have a fallback parameter.
      PrimType FallbackT = *S.getContext().classify(Call->getArg(1));
      const APSInt &Fallback = peekToAPSInt(S.Stk, FallbackT);
      pushInteger(S, Fallback, Call->getType());
      return true;
    }
    return false;
  }

  pushInteger(S, Val.countr_zero(), Call->getType());
  return true;
}

static bool interp__builtin_bswap(InterpState &S, CodePtr OpPC,
                                  const InterpFrame *Frame,
                                  const Function *Func, const CallExpr *Call) {
  PrimType ReturnT = *S.getContext().classify(Call->getType());
  PrimType ValT = *S.getContext().classify(Call->getArg(0));
  const APSInt &Val = peekToAPSInt(S.Stk, ValT);
  assert(Val.getActiveBits() <= 64);

  INT_TYPE_SWITCH(ReturnT,
                  { S.Stk.push<T>(T::from(Val.byteSwap().getZExtValue())); });
  return true;
}

/// bool __atomic_always_lock_free(size_t, void const volatile*)
/// bool __atomic_is_lock_free(size_t, void const volatile*)
/// bool __c11_atomic_is_lock_free(size_t)
static bool interp__builtin_atomic_lock_free(InterpState &S, CodePtr OpPC,
                                             const InterpFrame *Frame,
                                             const Function *Func,
                                             const CallExpr *Call) {
  unsigned BuiltinOp = Func->getBuiltinID();

  PrimType ValT = *S.getContext().classify(Call->getArg(0));
  unsigned SizeValOffset = 0;
  if (BuiltinOp != Builtin::BI__c11_atomic_is_lock_free)
    SizeValOffset = align(primSize(ValT)) + align(primSize(PT_Ptr));
  const APSInt &SizeVal = peekToAPSInt(S.Stk, ValT, SizeValOffset);

  auto returnBool = [&S](bool Value) -> bool {
    S.Stk.push<Boolean>(Value);
    return true;
  };

  // For __atomic_is_lock_free(sizeof(_Atomic(T))), if the size is a power
  // of two less than or equal to the maximum inline atomic width, we know it
  // is lock-free.  If the size isn't a power of two, or greater than the
  // maximum alignment where we promote atomics, we know it is not lock-free
  // (at least not in the sense of atomic_is_lock_free).  Otherwise,
  // the answer can only be determined at runtime; for example, 16-byte
  // atomics have lock-free implementations on some, but not all,
  // x86-64 processors.

  // Check power-of-two.
  CharUnits Size = CharUnits::fromQuantity(SizeVal.getZExtValue());
  if (Size.isPowerOfTwo()) {
    // Check against inlining width.
    unsigned InlineWidthBits =
        S.getCtx().getTargetInfo().getMaxAtomicInlineWidth();
    if (Size <= S.getCtx().toCharUnitsFromBits(InlineWidthBits)) {

      // OK, we will inline appropriately-aligned operations of this size,
      // and _Atomic(T) is appropriately-aligned.
      if (BuiltinOp == Builtin::BI__c11_atomic_is_lock_free ||
          Size == CharUnits::One())
        return returnBool(true);

      // Same for null pointers.
      assert(BuiltinOp != Builtin::BI__c11_atomic_is_lock_free);
      const Pointer &Ptr = S.Stk.peek<Pointer>();
      if (Ptr.isZero())
        return returnBool(true);

      QualType PointeeType = Call->getArg(1)
                                 ->IgnoreImpCasts()
                                 ->getType()
                                 ->castAs<PointerType>()
                                 ->getPointeeType();
      // OK, we will inline operations on this object.
      if (!PointeeType->isIncompleteType() &&
          S.getCtx().getTypeAlignInChars(PointeeType) >= Size)
        return returnBool(true);
    }
  }

  if (BuiltinOp == Builtin::BI__atomic_always_lock_free)
    return returnBool(false);

  return false;
}

/// __builtin_complex(Float A, float B);
static bool interp__builtin_complex(InterpState &S, CodePtr OpPC,
                                    const InterpFrame *Frame,
                                    const Function *Func,
                                    const CallExpr *Call) {
  const Floating &Arg2 = S.Stk.peek<Floating>();
  const Floating &Arg1 = S.Stk.peek<Floating>(align(primSize(PT_Float)) * 2);
  Pointer &Result = S.Stk.peek<Pointer>(align(primSize(PT_Float)) * 2 +
                                        align(primSize(PT_Ptr)));

  Result.atIndex(0).deref<Floating>() = Arg1;
  Result.atIndex(0).initialize();
  Result.atIndex(1).deref<Floating>() = Arg2;
  Result.atIndex(1).initialize();
  Result.initialize();

  return true;
}

/// __builtin_is_aligned()
/// __builtin_align_up()
/// __builtin_align_down()
/// The first parameter is either an integer or a pointer.
/// The second parameter is the requested alignment as an integer.
static bool interp__builtin_is_aligned_up_down(InterpState &S, CodePtr OpPC,
                                               const InterpFrame *Frame,
                                               const Function *Func,
                                               const CallExpr *Call) {
  unsigned BuiltinOp = Func->getBuiltinID();
  unsigned CallSize = callArgSize(S, Call);

  PrimType AlignmentT = *S.Ctx.classify(Call->getArg(1));
  const APSInt &Alignment = peekToAPSInt(S.Stk, AlignmentT);

  if (Alignment < 0 || !Alignment.isPowerOf2()) {
    S.FFDiag(Call, diag::note_constexpr_invalid_alignment) << Alignment;
    return false;
  }
  unsigned SrcWidth = S.getCtx().getIntWidth(Call->getArg(0)->getType());
  APSInt MaxValue(APInt::getOneBitSet(SrcWidth, SrcWidth - 1));
  if (APSInt::compareValues(Alignment, MaxValue) > 0) {
    S.FFDiag(Call, diag::note_constexpr_alignment_too_big)
        << MaxValue << Call->getArg(0)->getType() << Alignment;
    return false;
  }

  // The first parameter is either an integer or a pointer (but not a function
  // pointer).
  PrimType FirstArgT = *S.Ctx.classify(Call->getArg(0));

  if (isIntegralType(FirstArgT)) {
    const APSInt &Src = peekToAPSInt(S.Stk, FirstArgT, CallSize);
    APSInt Align = Alignment.extOrTrunc(Src.getBitWidth());
    if (BuiltinOp == Builtin::BI__builtin_align_up) {
      APSInt AlignedVal =
          APSInt((Src + (Align - 1)) & ~(Align - 1), Src.isUnsigned());
      pushInteger(S, AlignedVal, Call->getType());
    } else if (BuiltinOp == Builtin::BI__builtin_align_down) {
      APSInt AlignedVal = APSInt(Src & ~(Align - 1), Src.isUnsigned());
      pushInteger(S, AlignedVal, Call->getType());
    } else {
      assert(*S.Ctx.classify(Call->getType()) == PT_Bool);
      S.Stk.push<Boolean>((Src & (Align - 1)) == 0);
    }
    return true;
  }

  assert(FirstArgT == PT_Ptr);
  const Pointer &Ptr = S.Stk.peek<Pointer>(CallSize);

  unsigned PtrOffset = Ptr.getByteOffset();
  PtrOffset = Ptr.getIndex();
  CharUnits BaseAlignment =
      S.getCtx().getDeclAlign(Ptr.getDeclDesc()->asValueDecl());
  CharUnits PtrAlign =
      BaseAlignment.alignmentAtOffset(CharUnits::fromQuantity(PtrOffset));

  if (BuiltinOp == Builtin::BI__builtin_is_aligned) {
    if (PtrAlign.getQuantity() >= Alignment) {
      S.Stk.push<Boolean>(true);
      return true;
    }
    // If the alignment is not known to be sufficient, some cases could still
    // be aligned at run time. However, if the requested alignment is less or
    // equal to the base alignment and the offset is not aligned, we know that
    // the run-time value can never be aligned.
    if (BaseAlignment.getQuantity() >= Alignment &&
        PtrAlign.getQuantity() < Alignment) {
      S.Stk.push<Boolean>(false);
      return true;
    }

    S.FFDiag(Call->getArg(0), diag::note_constexpr_alignment_compute)
        << Alignment;
    return false;
  }

  assert(BuiltinOp == Builtin::BI__builtin_align_down ||
         BuiltinOp == Builtin::BI__builtin_align_up);

  // For align_up/align_down, we can return the same value if the alignment
  // is known to be greater or equal to the requested value.
  if (PtrAlign.getQuantity() >= Alignment) {
    S.Stk.push<Pointer>(Ptr);
    return true;
  }

  // The alignment could be greater than the minimum at run-time, so we cannot
  // infer much about the resulting pointer value. One case is possible:
  // For `_Alignas(32) char buf[N]; __builtin_align_down(&buf[idx], 32)` we
  // can infer the correct index if the requested alignment is smaller than
  // the base alignment so we can perform the computation on the offset.
  if (BaseAlignment.getQuantity() >= Alignment) {
    assert(Alignment.getBitWidth() <= 64 &&
           "Cannot handle > 64-bit address-space");
    uint64_t Alignment64 = Alignment.getZExtValue();
    CharUnits NewOffset =
        CharUnits::fromQuantity(BuiltinOp == Builtin::BI__builtin_align_down
                                    ? llvm::alignDown(PtrOffset, Alignment64)
                                    : llvm::alignTo(PtrOffset, Alignment64));

    S.Stk.push<Pointer>(Ptr.atIndex(NewOffset.getQuantity()));
    return true;
  }

  // Otherwise, we cannot constant-evaluate the result.
  S.FFDiag(Call->getArg(0), diag::note_constexpr_alignment_adjust) << Alignment;
  return false;
}

static bool interp__builtin_os_log_format_buffer_size(InterpState &S,
                                                      CodePtr OpPC,
                                                      const InterpFrame *Frame,
                                                      const Function *Func,
                                                      const CallExpr *Call) {
  analyze_os_log::OSLogBufferLayout Layout;
  analyze_os_log::computeOSLogBufferLayout(S.getCtx(), Call, Layout);
  pushInteger(S, Layout.size().getQuantity(), Call->getType());
  return true;
}

static bool interp__builtin_ptrauth_string_discriminator(
    InterpState &S, CodePtr OpPC, const InterpFrame *Frame,
    const Function *Func, const CallExpr *Call) {
  const auto &Ptr = S.Stk.peek<Pointer>();
  assert(Ptr.getFieldDesc()->isPrimitiveArray());

  StringRef R(&Ptr.deref<char>(), Ptr.getFieldDesc()->getNumElems() - 1);
  uint64_t Result = getPointerAuthStableSipHash(R);
  pushInteger(S, Result, Call->getType());
  return true;
}

bool InterpretBuiltin(InterpState &S, CodePtr OpPC, const Function *F,
                      const CallExpr *Call) {
  const InterpFrame *Frame = S.Current;
  APValue Dummy;

  std::optional<PrimType> ReturnT = S.getContext().classify(Call);

  switch (F->getBuiltinID()) {
  case Builtin::BI__builtin_is_constant_evaluated:
    if (!interp__builtin_is_constant_evaluated(S, OpPC, Frame, Call))
      return false;
    break;
  case Builtin::BI__builtin_assume:
  case Builtin::BI__assume:
    break;
  case Builtin::BI__builtin_strcmp:
    if (!interp__builtin_strcmp(S, OpPC, Frame, Call))
      return false;
    break;
  case Builtin::BI__builtin_strlen:
    if (!interp__builtin_strlen(S, OpPC, Frame, Call))
      return false;
    break;
  case Builtin::BI__builtin_nan:
  case Builtin::BI__builtin_nanf:
  case Builtin::BI__builtin_nanl:
  case Builtin::BI__builtin_nanf16:
  case Builtin::BI__builtin_nanf128:
    if (!interp__builtin_nan(S, OpPC, Frame, F, /*Signaling=*/false))
      return false;
    break;
  case Builtin::BI__builtin_nans:
  case Builtin::BI__builtin_nansf:
  case Builtin::BI__builtin_nansl:
  case Builtin::BI__builtin_nansf16:
  case Builtin::BI__builtin_nansf128:
    if (!interp__builtin_nan(S, OpPC, Frame, F, /*Signaling=*/true))
      return false;
    break;

  case Builtin::BI__builtin_huge_val:
  case Builtin::BI__builtin_huge_valf:
  case Builtin::BI__builtin_huge_vall:
  case Builtin::BI__builtin_huge_valf16:
  case Builtin::BI__builtin_huge_valf128:
  case Builtin::BI__builtin_inf:
  case Builtin::BI__builtin_inff:
  case Builtin::BI__builtin_infl:
  case Builtin::BI__builtin_inff16:
  case Builtin::BI__builtin_inff128:
    if (!interp__builtin_inf(S, OpPC, Frame, F))
      return false;
    break;
  case Builtin::BI__builtin_copysign:
  case Builtin::BI__builtin_copysignf:
  case Builtin::BI__builtin_copysignl:
  case Builtin::BI__builtin_copysignf128:
    if (!interp__builtin_copysign(S, OpPC, Frame, F))
      return false;
    break;

  case Builtin::BI__builtin_fmin:
  case Builtin::BI__builtin_fminf:
  case Builtin::BI__builtin_fminl:
  case Builtin::BI__builtin_fminf16:
  case Builtin::BI__builtin_fminf128:
    if (!interp__builtin_fmin(S, OpPC, Frame, F))
      return false;
    break;

  case Builtin::BI__builtin_fmax:
  case Builtin::BI__builtin_fmaxf:
  case Builtin::BI__builtin_fmaxl:
  case Builtin::BI__builtin_fmaxf16:
  case Builtin::BI__builtin_fmaxf128:
    if (!interp__builtin_fmax(S, OpPC, Frame, F))
      return false;
    break;

  case Builtin::BI__builtin_isnan:
    if (!interp__builtin_isnan(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_issignaling:
    if (!interp__builtin_issignaling(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_isinf:
    if (!interp__builtin_isinf(S, OpPC, Frame, F, /*Sign=*/false, Call))
      return false;
    break;

  case Builtin::BI__builtin_isinf_sign:
    if (!interp__builtin_isinf(S, OpPC, Frame, F, /*Sign=*/true, Call))
      return false;
    break;

  case Builtin::BI__builtin_isfinite:
    if (!interp__builtin_isfinite(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_isnormal:
    if (!interp__builtin_isnormal(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_issubnormal:
    if (!interp__builtin_issubnormal(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_iszero:
    if (!interp__builtin_iszero(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_isfpclass:
    if (!interp__builtin_isfpclass(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BI__builtin_fpclassify:
    if (!interp__builtin_fpclassify(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_fabsf128:
    if (!interp__builtin_fabs(S, OpPC, Frame, F))
      return false;
    break;

  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll:
  case Builtin::BI__builtin_popcountg:
  case Builtin::BI__popcnt16: // Microsoft variants of popcount
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64:
    if (!interp__builtin_popcount(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll:
    if (!interp__builtin_parity(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll:
    if (!interp__builtin_clrsb(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64:
    if (!interp__builtin_bitreverse(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_classify_type:
    if (!interp__builtin_classify_type(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_expect_with_probability:
    if (!interp__builtin_expect(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
  case Builtin::BI_rotl8: // Microsoft variants of rotate left
  case Builtin::BI_rotl16:
  case Builtin::BI_rotl:
  case Builtin::BI_lrotl:
  case Builtin::BI_rotl64:
    if (!interp__builtin_rotate(S, OpPC, Frame, F, Call, /*Right=*/false))
      return false;
    break;

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
  case Builtin::BI_rotr8: // Microsoft variants of rotate right
  case Builtin::BI_rotr16:
  case Builtin::BI_rotr:
  case Builtin::BI_lrotr:
  case Builtin::BI_rotr64:
    if (!interp__builtin_rotate(S, OpPC, Frame, F, Call, /*Right=*/true))
      return false;
    break;

  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll:
    if (!interp__builtin_ffs(S, OpPC, Frame, F, Call))
      return false;
    break;
  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
  case Builtin::BI__builtin_addressof:
    if (!interp__builtin_addressof(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BIas_const:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
    if (!interp__builtin_move(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_eh_return_data_regno:
    if (!interp__builtin_eh_return_data_regno(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_launder:
    if (!noopPointer(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow:
    if (!interp__builtin_overflowop(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll:
    if (!interp__builtin_carryop(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll:
  case Builtin::BI__builtin_clzs:
  case Builtin::BI__builtin_clzg:
  case Builtin::BI__lzcnt16: // Microsoft variants of count leading-zeroes
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64:
    if (!interp__builtin_clz(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll:
  case Builtin::BI__builtin_ctzs:
  case Builtin::BI__builtin_ctzg:
    if (!interp__builtin_ctz(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64:
    if (!interp__builtin_bswap(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__atomic_always_lock_free:
  case Builtin::BI__atomic_is_lock_free:
  case Builtin::BI__c11_atomic_is_lock_free:
    if (!interp__builtin_atomic_lock_free(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_complex:
    if (!interp__builtin_complex(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_is_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down:
    if (!interp__builtin_is_aligned_up_down(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_os_log_format_buffer_size:
    if (!interp__builtin_os_log_format_buffer_size(S, OpPC, Frame, F, Call))
      return false;
    break;

  case Builtin::BI__builtin_ptrauth_string_discriminator:
    if (!interp__builtin_ptrauth_string_discriminator(S, OpPC, Frame, F, Call))
      return false;
    break;

  default:
    S.FFDiag(S.Current->getLocation(OpPC),
             diag::note_invalid_subexpr_in_const_expr)
        << S.Current->getRange(OpPC);

    return false;
  }

  return retPrimValue(S, OpPC, Dummy, ReturnT);
}

bool InterpretOffsetOf(InterpState &S, CodePtr OpPC, const OffsetOfExpr *E,
                       llvm::ArrayRef<int64_t> ArrayIndices,
                       int64_t &IntResult) {
  CharUnits Result;
  unsigned N = E->getNumComponents();
  assert(N > 0);

  unsigned ArrayIndex = 0;
  QualType CurrentType = E->getTypeSourceInfo()->getType();
  for (unsigned I = 0; I != N; ++I) {
    const OffsetOfNode &Node = E->getComponent(I);
    switch (Node.getKind()) {
    case OffsetOfNode::Field: {
      const FieldDecl *MemberDecl = Node.getField();
      const RecordType *RT = CurrentType->getAs<RecordType>();
      if (!RT)
        return false;
      const RecordDecl *RD = RT->getDecl();
      if (RD->isInvalidDecl())
        return false;
      const ASTRecordLayout &RL = S.getCtx().getASTRecordLayout(RD);
      unsigned FieldIndex = MemberDecl->getFieldIndex();
      assert(FieldIndex < RL.getFieldCount() && "offsetof field in wrong type");
      Result += S.getCtx().toCharUnitsFromBits(RL.getFieldOffset(FieldIndex));
      CurrentType = MemberDecl->getType().getNonReferenceType();
      break;
    }
    case OffsetOfNode::Array: {
      // When generating bytecode, we put all the index expressions as Sint64 on
      // the stack.
      int64_t Index = ArrayIndices[ArrayIndex];
      const ArrayType *AT = S.getCtx().getAsArrayType(CurrentType);
      if (!AT)
        return false;
      CurrentType = AT->getElementType();
      CharUnits ElementSize = S.getCtx().getTypeSizeInChars(CurrentType);
      Result += Index * ElementSize;
      ++ArrayIndex;
      break;
    }
    case OffsetOfNode::Base: {
      const CXXBaseSpecifier *BaseSpec = Node.getBase();
      if (BaseSpec->isVirtual())
        return false;

      // Find the layout of the class whose base we are looking into.
      const RecordType *RT = CurrentType->getAs<RecordType>();
      if (!RT)
        return false;
      const RecordDecl *RD = RT->getDecl();
      if (RD->isInvalidDecl())
        return false;
      const ASTRecordLayout &RL = S.getCtx().getASTRecordLayout(RD);

      // Find the base class itself.
      CurrentType = BaseSpec->getType();
      const RecordType *BaseRT = CurrentType->getAs<RecordType>();
      if (!BaseRT)
        return false;

      // Add the offset to the base.
      Result += RL.getBaseClassOffset(cast<CXXRecordDecl>(BaseRT->getDecl()));
      break;
    }
    case OffsetOfNode::Identifier:
      llvm_unreachable("Dependent OffsetOfExpr?");
    }
  }

  IntResult = Result.getQuantity();

  return true;
}

bool SetThreeWayComparisonField(InterpState &S, CodePtr OpPC,
                                const Pointer &Ptr, const APSInt &IntValue) {

  const Record *R = Ptr.getRecord();
  assert(R);
  assert(R->getNumFields() == 1);

  unsigned FieldOffset = R->getField(0u)->Offset;
  const Pointer &FieldPtr = Ptr.atField(FieldOffset);
  PrimType FieldT = *S.getContext().classify(FieldPtr.getType());

  INT_TYPE_SWITCH(FieldT,
                  FieldPtr.deref<T>() = T::from(IntValue.getSExtValue()));
  FieldPtr.initialize();
  return true;
}

bool DoMemcpy(InterpState &S, CodePtr OpPC, const Pointer &Src, Pointer &Dest) {
  assert(Src.isLive() && Dest.isLive());

  [[maybe_unused]] const Descriptor *SrcDesc = Src.getFieldDesc();
  const Descriptor *DestDesc = Dest.getFieldDesc();

  assert(!DestDesc->isPrimitive() && !SrcDesc->isPrimitive());

  if (DestDesc->isPrimitiveArray()) {
    assert(SrcDesc->isPrimitiveArray());
    assert(SrcDesc->getNumElems() == DestDesc->getNumElems());
    PrimType ET = DestDesc->getPrimType();
    for (unsigned I = 0, N = DestDesc->getNumElems(); I != N; ++I) {
      Pointer DestElem = Dest.atIndex(I);
      TYPE_SWITCH(ET, {
        DestElem.deref<T>() = Src.atIndex(I).deref<T>();
        DestElem.initialize();
      });
    }
    return true;
  }

  if (DestDesc->isRecord()) {
    assert(SrcDesc->isRecord());
    assert(SrcDesc->ElemRecord == DestDesc->ElemRecord);
    const Record *R = DestDesc->ElemRecord;
    for (const Record::Field &F : R->fields()) {
      Pointer DestField = Dest.atField(F.Offset);
      if (std::optional<PrimType> FT = S.Ctx.classify(F.Decl->getType())) {
        TYPE_SWITCH(*FT, {
          DestField.deref<T>() = Src.atField(F.Offset).deref<T>();
          DestField.initialize();
        });
      } else {
        return Invalid(S, OpPC);
      }
    }
    return true;
  }

  // FIXME: Composite types.

  return Invalid(S, OpPC);
}

} // namespace interp
} // namespace clang
