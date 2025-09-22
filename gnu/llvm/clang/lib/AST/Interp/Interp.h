//===--- Interp.h - Interpreter for the constexpr VM ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of the interpreter state and entry point.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_INTERP_H
#define LLVM_CLANG_AST_INTERP_INTERP_H

#include "../ExprConstShared.h"
#include "Boolean.h"
#include "DynamicAllocator.h"
#include "Floating.h"
#include "Function.h"
#include "FunctionPointer.h"
#include "InterpFrame.h"
#include "InterpStack.h"
#include "InterpState.h"
#include "MemberPointer.h"
#include "Opcode.h"
#include "PrimType.h"
#include "Program.h"
#include "State.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include <type_traits>

namespace clang {
namespace interp {

using APSInt = llvm::APSInt;

/// Convert a value to an APValue.
template <typename T>
bool ReturnValue(const InterpState &S, const T &V, APValue &R) {
  R = V.toAPValue(S.getCtx());
  return true;
}

/// Checks if the variable has externally defined storage.
bool CheckExtern(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if the array is offsetable.
bool CheckArray(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a pointer is live and accessible.
bool CheckLive(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               AccessKinds AK);

/// Checks if a pointer is a dummy pointer.
bool CheckDummy(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                AccessKinds AK);

/// Checks if a pointer is null.
bool CheckNull(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               CheckSubobjectKind CSK);

/// Checks if a pointer is in range.
bool CheckRange(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                AccessKinds AK);

/// Checks if a field from which a pointer is going to be derived is valid.
bool CheckRange(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                CheckSubobjectKind CSK);

/// Checks if Ptr is a one-past-the-end pointer.
bool CheckSubobject(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                    CheckSubobjectKind CSK);

/// Checks if the dowcast using the given offset is possible with the given
/// pointer.
bool CheckDowncast(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                   uint32_t Offset);

/// Checks if a pointer points to const storage.
bool CheckConst(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if the Descriptor is of a constexpr or const global variable.
bool CheckConstant(InterpState &S, CodePtr OpPC, const Descriptor *Desc);

/// Checks if a pointer points to a mutable field.
bool CheckMutable(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a value can be loaded from a block.
bool CheckLoad(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
               AccessKinds AK = AK_Read);

bool CheckInitialized(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                      AccessKinds AK);
/// Check if a global variable is initialized.
bool CheckGlobalInitialized(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a value can be stored in a block.
bool CheckStore(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a method can be invoked on an object.
bool CheckInvoke(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a value can be initialized.
bool CheckInit(InterpState &S, CodePtr OpPC, const Pointer &Ptr);

/// Checks if a method can be called.
bool CheckCallable(InterpState &S, CodePtr OpPC, const Function *F);

/// Checks if calling the currently active function would exceed
/// the allowed call depth.
bool CheckCallDepth(InterpState &S, CodePtr OpPC);

/// Checks the 'this' pointer.
bool CheckThis(InterpState &S, CodePtr OpPC, const Pointer &This);

/// Checks if a method is pure virtual.
bool CheckPure(InterpState &S, CodePtr OpPC, const CXXMethodDecl *MD);

/// Checks if all the arguments annotated as 'nonnull' are in fact not null.
bool CheckNonNullArgs(InterpState &S, CodePtr OpPC, const Function *F,
                      const CallExpr *CE, unsigned ArgSize);

/// Checks if dynamic memory allocation is available in the current
/// language mode.
bool CheckDynamicMemoryAllocation(InterpState &S, CodePtr OpPC);

/// Diagnose mismatched new[]/delete or new/delete[] pairs.
bool CheckNewDeleteForms(InterpState &S, CodePtr OpPC, bool NewWasArray,
                         bool DeleteIsArray, const Descriptor *D,
                         const Expr *NewExpr);

/// Check the source of the pointer passed to delete/delete[] has actually
/// been heap allocated by us.
bool CheckDeleteSource(InterpState &S, CodePtr OpPC, const Expr *Source,
                       const Pointer &Ptr);

/// Sets the given integral value to the pointer, which is of
/// a std::{weak,partial,strong}_ordering type.
bool SetThreeWayComparisonField(InterpState &S, CodePtr OpPC,
                                const Pointer &Ptr, const APSInt &IntValue);

/// Copy the contents of Src into Dest.
bool DoMemcpy(InterpState &S, CodePtr OpPC, const Pointer &Src, Pointer &Dest);

/// Checks if the shift operation is legal.
template <typename LT, typename RT>
bool CheckShift(InterpState &S, CodePtr OpPC, const LT &LHS, const RT &RHS,
                unsigned Bits) {
  if (RHS.isNegative()) {
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.CCEDiag(Loc, diag::note_constexpr_negative_shift) << RHS.toAPSInt();
    if (!S.noteUndefinedBehavior())
      return false;
  }

  // C++11 [expr.shift]p1: Shift width must be less than the bit width of
  // the shifted type.
  if (Bits > 1 && RHS >= RT::from(Bits, RHS.bitWidth())) {
    const Expr *E = S.Current->getExpr(OpPC);
    const APSInt Val = RHS.toAPSInt();
    QualType Ty = E->getType();
    S.CCEDiag(E, diag::note_constexpr_large_shift) << Val << Ty << Bits;
    if (!S.noteUndefinedBehavior())
      return false;
  }

  if (LHS.isSigned() && !S.getLangOpts().CPlusPlus20) {
    const Expr *E = S.Current->getExpr(OpPC);
    // C++11 [expr.shift]p2: A signed left shift must have a non-negative
    // operand, and must not overflow the corresponding unsigned type.
    if (LHS.isNegative()) {
      S.CCEDiag(E, diag::note_constexpr_lshift_of_negative) << LHS.toAPSInt();
      if (!S.noteUndefinedBehavior())
        return false;
    } else if (LHS.toUnsigned().countLeadingZeros() <
               static_cast<unsigned>(RHS)) {
      S.CCEDiag(E, diag::note_constexpr_lshift_discards);
      if (!S.noteUndefinedBehavior())
        return false;
    }
  }

  // C++2a [expr.shift]p2: [P0907R4]:
  //    E1 << E2 is the unique value congruent to
  //    E1 x 2^E2 module 2^N.
  return true;
}

/// Checks if Div/Rem operation on LHS and RHS is valid.
template <typename T>
bool CheckDivRem(InterpState &S, CodePtr OpPC, const T &LHS, const T &RHS) {
  if (RHS.isZero()) {
    const auto *Op = cast<BinaryOperator>(S.Current->getExpr(OpPC));
    if constexpr (std::is_same_v<T, Floating>) {
      S.CCEDiag(Op, diag::note_expr_divide_by_zero)
          << Op->getRHS()->getSourceRange();
      return true;
    }

    S.FFDiag(Op, diag::note_expr_divide_by_zero)
        << Op->getRHS()->getSourceRange();
    return false;
  }

  if (LHS.isSigned() && LHS.isMin() && RHS.isNegative() && RHS.isMinusOne()) {
    APSInt LHSInt = LHS.toAPSInt();
    SmallString<32> Trunc;
    (-LHSInt.extend(LHSInt.getBitWidth() + 1)).toString(Trunc, 10);
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    const Expr *E = S.Current->getExpr(OpPC);
    S.CCEDiag(Loc, diag::note_constexpr_overflow) << Trunc << E->getType();
    return false;
  }
  return true;
}

template <typename SizeT>
bool CheckArraySize(InterpState &S, CodePtr OpPC, SizeT *NumElements,
                    unsigned ElemSize, bool IsNoThrow) {
  // FIXME: Both the SizeT::from() as well as the
  // NumElements.toAPSInt() in this function are rather expensive.

  // FIXME: GH63562
  // APValue stores array extents as unsigned,
  // so anything that is greater that unsigned would overflow when
  // constructing the array, we catch this here.
  SizeT MaxElements = SizeT::from(Descriptor::MaxArrayElemBytes / ElemSize);
  if (NumElements->toAPSInt().getActiveBits() >
          ConstantArrayType::getMaxSizeBits(S.getCtx()) ||
      *NumElements > MaxElements) {
    if (!IsNoThrow) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_new_too_large)
          << NumElements->toDiagnosticString(S.getCtx());
    }
    return false;
  }
  return true;
}

/// Checks if the result of a floating-point operation is valid
/// in the current context.
bool CheckFloatResult(InterpState &S, CodePtr OpPC, const Floating &Result,
                      APFloat::opStatus Status);

/// Checks why the given DeclRefExpr is invalid.
bool CheckDeclRef(InterpState &S, CodePtr OpPC, const DeclRefExpr *DR);

/// Interpreter entry point.
bool Interpret(InterpState &S, APValue &Result);

/// Interpret a builtin function.
bool InterpretBuiltin(InterpState &S, CodePtr OpPC, const Function *F,
                      const CallExpr *Call);

/// Interpret an offsetof operation.
bool InterpretOffsetOf(InterpState &S, CodePtr OpPC, const OffsetOfExpr *E,
                       llvm::ArrayRef<int64_t> ArrayIndices, int64_t &Result);

inline bool Invalid(InterpState &S, CodePtr OpPC);

enum class ArithOp { Add, Sub };

//===----------------------------------------------------------------------===//
// Returning values
//===----------------------------------------------------------------------===//

void cleanupAfterFunctionCall(InterpState &S, CodePtr OpPC);

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Ret(InterpState &S, CodePtr &PC, APValue &Result) {
  const T &Ret = S.Stk.pop<T>();

  // Make sure returned pointers are live. We might be trying to return a
  // pointer or reference to a local variable.
  // Just return false, since a diagnostic has already been emitted in Sema.
  if constexpr (std::is_same_v<T, Pointer>) {
    // FIXME: We could be calling isLive() here, but the emitted diagnostics
    // seem a little weird, at least if the returned expression is of
    // pointer type.
    // Null pointers are considered live here.
    if (!Ret.isZero() && !Ret.isLive())
      return false;
  }

  assert(S.Current);
  assert(S.Current->getFrameOffset() == S.Stk.size() && "Invalid frame");
  if (!S.checkingPotentialConstantExpression() || S.Current->Caller)
    cleanupAfterFunctionCall(S, PC);

  if (InterpFrame *Caller = S.Current->Caller) {
    PC = S.Current->getRetPC();
    delete S.Current;
    S.Current = Caller;
    S.Stk.push<T>(Ret);
  } else {
    delete S.Current;
    S.Current = nullptr;
    if (!ReturnValue<T>(S, Ret, Result))
      return false;
  }
  return true;
}

inline bool RetVoid(InterpState &S, CodePtr &PC, APValue &Result) {
  assert(S.Current->getFrameOffset() == S.Stk.size() && "Invalid frame");

  if (!S.checkingPotentialConstantExpression() || S.Current->Caller)
    cleanupAfterFunctionCall(S, PC);

  if (InterpFrame *Caller = S.Current->Caller) {
    PC = S.Current->getRetPC();
    delete S.Current;
    S.Current = Caller;
  } else {
    delete S.Current;
    S.Current = nullptr;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Add, Sub, Mul
//===----------------------------------------------------------------------===//

template <typename T, bool (*OpFW)(T, T, unsigned, T *),
          template <typename U> class OpAP>
bool AddSubMulHelper(InterpState &S, CodePtr OpPC, unsigned Bits, const T &LHS,
                     const T &RHS) {
  // Fast path - add the numbers with fixed width.
  T Result;
  if (!OpFW(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }

  // If for some reason evaluation continues, use the truncated results.
  S.Stk.push<T>(Result);

  // Slow path - compute the result using another bit of precision.
  APSInt Value = OpAP<APSInt>()(LHS.toAPSInt(Bits), RHS.toAPSInt(Bits));

  // Report undefined behaviour, stopping if required.
  const Expr *E = S.Current->getExpr(OpPC);
  QualType Type = E->getType();
  if (S.checkingForUndefinedBehavior()) {
    SmallString<32> Trunc;
    Value.trunc(Result.bitWidth())
        .toString(Trunc, 10, Result.isSigned(), /*formatAsCLiteral=*/false,
                  /*UpperCase=*/true, /*InsertSeparators=*/true);
    auto Loc = E->getExprLoc();
    S.report(Loc, diag::warn_integer_constant_overflow)
        << Trunc << Type << E->getSourceRange();
  }

  S.CCEDiag(E, diag::note_constexpr_overflow) << Value << Type;

  if (!S.noteUndefinedBehavior()) {
    S.Stk.pop<T>();
    return false;
  }

  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Add(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();
  const unsigned Bits = RHS.bitWidth() + 1;
  return AddSubMulHelper<T, T::add, std::plus>(S, OpPC, Bits, LHS, RHS);
}

inline bool Addf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Floating &RHS = S.Stk.pop<Floating>();
  const Floating &LHS = S.Stk.pop<Floating>();

  Floating Result;
  auto Status = Floating::add(LHS, RHS, RM, &Result);
  S.Stk.push<Floating>(Result);
  return CheckFloatResult(S, OpPC, Result, Status);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Sub(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();
  const unsigned Bits = RHS.bitWidth() + 1;
  return AddSubMulHelper<T, T::sub, std::minus>(S, OpPC, Bits, LHS, RHS);
}

inline bool Subf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Floating &RHS = S.Stk.pop<Floating>();
  const Floating &LHS = S.Stk.pop<Floating>();

  Floating Result;
  auto Status = Floating::sub(LHS, RHS, RM, &Result);
  S.Stk.push<Floating>(Result);
  return CheckFloatResult(S, OpPC, Result, Status);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Mul(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();
  const unsigned Bits = RHS.bitWidth() * 2;
  return AddSubMulHelper<T, T::mul, std::multiplies>(S, OpPC, Bits, LHS, RHS);
}

inline bool Mulf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Floating &RHS = S.Stk.pop<Floating>();
  const Floating &LHS = S.Stk.pop<Floating>();

  Floating Result;
  auto Status = Floating::mul(LHS, RHS, RM, &Result);
  S.Stk.push<Floating>(Result);
  return CheckFloatResult(S, OpPC, Result, Status);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool Mulc(InterpState &S, CodePtr OpPC) {
  const Pointer &RHS = S.Stk.pop<Pointer>();
  const Pointer &LHS = S.Stk.pop<Pointer>();
  const Pointer &Result = S.Stk.peek<Pointer>();

  if constexpr (std::is_same_v<T, Floating>) {
    APFloat A = LHS.atIndex(0).deref<Floating>().getAPFloat();
    APFloat B = LHS.atIndex(1).deref<Floating>().getAPFloat();
    APFloat C = RHS.atIndex(0).deref<Floating>().getAPFloat();
    APFloat D = RHS.atIndex(1).deref<Floating>().getAPFloat();

    APFloat ResR(A.getSemantics());
    APFloat ResI(A.getSemantics());
    HandleComplexComplexMul(A, B, C, D, ResR, ResI);

    // Copy into the result.
    Result.atIndex(0).deref<Floating>() = Floating(ResR);
    Result.atIndex(0).initialize();
    Result.atIndex(1).deref<Floating>() = Floating(ResI);
    Result.atIndex(1).initialize();
    Result.initialize();
  } else {
    // Integer element type.
    const T &LHSR = LHS.atIndex(0).deref<T>();
    const T &LHSI = LHS.atIndex(1).deref<T>();
    const T &RHSR = RHS.atIndex(0).deref<T>();
    const T &RHSI = RHS.atIndex(1).deref<T>();
    unsigned Bits = LHSR.bitWidth();

    // real(Result) = (real(LHS) * real(RHS)) - (imag(LHS) * imag(RHS))
    T A;
    if (T::mul(LHSR, RHSR, Bits, &A))
      return false;
    T B;
    if (T::mul(LHSI, RHSI, Bits, &B))
      return false;
    if (T::sub(A, B, Bits, &Result.atIndex(0).deref<T>()))
      return false;
    Result.atIndex(0).initialize();

    // imag(Result) = (real(LHS) * imag(RHS)) + (imag(LHS) * real(RHS))
    if (T::mul(LHSR, RHSI, Bits, &A))
      return false;
    if (T::mul(LHSI, RHSR, Bits, &B))
      return false;
    if (T::add(A, B, Bits, &Result.atIndex(1).deref<T>()))
      return false;
    Result.atIndex(1).initialize();
    Result.initialize();
  }

  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool Divc(InterpState &S, CodePtr OpPC) {
  const Pointer &RHS = S.Stk.pop<Pointer>();
  const Pointer &LHS = S.Stk.pop<Pointer>();
  const Pointer &Result = S.Stk.peek<Pointer>();

  if constexpr (std::is_same_v<T, Floating>) {
    APFloat A = LHS.atIndex(0).deref<Floating>().getAPFloat();
    APFloat B = LHS.atIndex(1).deref<Floating>().getAPFloat();
    APFloat C = RHS.atIndex(0).deref<Floating>().getAPFloat();
    APFloat D = RHS.atIndex(1).deref<Floating>().getAPFloat();

    APFloat ResR(A.getSemantics());
    APFloat ResI(A.getSemantics());
    HandleComplexComplexDiv(A, B, C, D, ResR, ResI);

    // Copy into the result.
    Result.atIndex(0).deref<Floating>() = Floating(ResR);
    Result.atIndex(0).initialize();
    Result.atIndex(1).deref<Floating>() = Floating(ResI);
    Result.atIndex(1).initialize();
    Result.initialize();
  } else {
    // Integer element type.
    const T &LHSR = LHS.atIndex(0).deref<T>();
    const T &LHSI = LHS.atIndex(1).deref<T>();
    const T &RHSR = RHS.atIndex(0).deref<T>();
    const T &RHSI = RHS.atIndex(1).deref<T>();
    unsigned Bits = LHSR.bitWidth();
    const T Zero = T::from(0, Bits);

    if (Compare(RHSR, Zero) == ComparisonCategoryResult::Equal &&
        Compare(RHSI, Zero) == ComparisonCategoryResult::Equal) {
      const SourceInfo &E = S.Current->getSource(OpPC);
      S.FFDiag(E, diag::note_expr_divide_by_zero);
      return false;
    }

    // Den = real(RHS)² + imag(RHS)²
    T A, B;
    if (T::mul(RHSR, RHSR, Bits, &A) || T::mul(RHSI, RHSI, Bits, &B))
      return false;
    T Den;
    if (T::add(A, B, Bits, &Den))
      return false;

    // real(Result) = ((real(LHS) * real(RHS)) + (imag(LHS) * imag(RHS))) / Den
    T &ResultR = Result.atIndex(0).deref<T>();
    T &ResultI = Result.atIndex(1).deref<T>();

    if (T::mul(LHSR, RHSR, Bits, &A) || T::mul(LHSI, RHSI, Bits, &B))
      return false;
    if (T::add(A, B, Bits, &ResultR))
      return false;
    if (T::div(ResultR, Den, Bits, &ResultR))
      return false;
    Result.atIndex(0).initialize();

    // imag(Result) = ((imag(LHS) * real(RHS)) - (real(LHS) * imag(RHS))) / Den
    if (T::mul(LHSI, RHSR, Bits, &A) || T::mul(LHSR, RHSI, Bits, &B))
      return false;
    if (T::sub(A, B, Bits, &ResultI))
      return false;
    if (T::div(ResultI, Den, Bits, &ResultI))
      return false;
    Result.atIndex(1).initialize();
    Result.initialize();
  }

  return true;
}

/// 1) Pops the RHS from the stack.
/// 2) Pops the LHS from the stack.
/// 3) Pushes 'LHS & RHS' on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool BitAnd(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();

  unsigned Bits = RHS.bitWidth();
  T Result;
  if (!T::bitAnd(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }
  return false;
}

/// 1) Pops the RHS from the stack.
/// 2) Pops the LHS from the stack.
/// 3) Pushes 'LHS | RHS' on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool BitOr(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();

  unsigned Bits = RHS.bitWidth();
  T Result;
  if (!T::bitOr(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }
  return false;
}

/// 1) Pops the RHS from the stack.
/// 2) Pops the LHS from the stack.
/// 3) Pushes 'LHS ^ RHS' on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool BitXor(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();

  unsigned Bits = RHS.bitWidth();
  T Result;
  if (!T::bitXor(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }
  return false;
}

/// 1) Pops the RHS from the stack.
/// 2) Pops the LHS from the stack.
/// 3) Pushes 'LHS % RHS' on the stack (the remainder of dividing LHS by RHS).
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Rem(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();

  if (!CheckDivRem(S, OpPC, LHS, RHS))
    return false;

  const unsigned Bits = RHS.bitWidth() * 2;
  T Result;
  if (!T::rem(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }
  return false;
}

/// 1) Pops the RHS from the stack.
/// 2) Pops the LHS from the stack.
/// 3) Pushes 'LHS / RHS' on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Div(InterpState &S, CodePtr OpPC) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();

  if (!CheckDivRem(S, OpPC, LHS, RHS))
    return false;

  const unsigned Bits = RHS.bitWidth() * 2;
  T Result;
  if (!T::div(LHS, RHS, Bits, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }
  return false;
}

inline bool Divf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Floating &RHS = S.Stk.pop<Floating>();
  const Floating &LHS = S.Stk.pop<Floating>();

  if (!CheckDivRem(S, OpPC, LHS, RHS))
    return false;

  Floating Result;
  auto Status = Floating::div(LHS, RHS, RM, &Result);
  S.Stk.push<Floating>(Result);
  return CheckFloatResult(S, OpPC, Result, Status);
}

//===----------------------------------------------------------------------===//
// Inv
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Inv(InterpState &S, CodePtr OpPC) {
  using BoolT = PrimConv<PT_Bool>::T;
  const T &Val = S.Stk.pop<T>();
  const unsigned Bits = Val.bitWidth();
  Boolean R;
  Boolean::inv(BoolT::from(Val, Bits), &R);

  S.Stk.push<BoolT>(R);
  return true;
}

//===----------------------------------------------------------------------===//
// Neg
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Neg(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  T Result;

  if (!T::neg(Value, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }

  assert(isIntegralType(Name) &&
         "don't expect other types to fail at constexpr negation");
  S.Stk.push<T>(Result);

  APSInt NegatedValue = -Value.toAPSInt(Value.bitWidth() + 1);
  const Expr *E = S.Current->getExpr(OpPC);
  QualType Type = E->getType();

  if (S.checkingForUndefinedBehavior()) {
    SmallString<32> Trunc;
    NegatedValue.trunc(Result.bitWidth())
        .toString(Trunc, 10, Result.isSigned(), /*formatAsCLiteral=*/false,
                  /*UpperCase=*/true, /*InsertSeparators=*/true);
    auto Loc = E->getExprLoc();
    S.report(Loc, diag::warn_integer_constant_overflow)
        << Trunc << Type << E->getSourceRange();
    return true;
  }

  S.CCEDiag(E, diag::note_constexpr_overflow) << NegatedValue << Type;
  return S.noteUndefinedBehavior();
}

enum class PushVal : bool {
  No,
  Yes,
};
enum class IncDecOp {
  Inc,
  Dec,
};

template <typename T, IncDecOp Op, PushVal DoPush>
bool IncDecHelper(InterpState &S, CodePtr OpPC, const Pointer &Ptr) {
  assert(!Ptr.isDummy());

  if constexpr (std::is_same_v<T, Boolean>) {
    if (!S.getLangOpts().CPlusPlus14)
      return Invalid(S, OpPC);
  }

  const T &Value = Ptr.deref<T>();
  T Result;

  if constexpr (DoPush == PushVal::Yes)
    S.Stk.push<T>(Value);

  if constexpr (Op == IncDecOp::Inc) {
    if (!T::increment(Value, &Result)) {
      Ptr.deref<T>() = Result;
      return true;
    }
  } else {
    if (!T::decrement(Value, &Result)) {
      Ptr.deref<T>() = Result;
      return true;
    }
  }

  // Something went wrong with the previous operation. Compute the
  // result with another bit of precision.
  unsigned Bits = Value.bitWidth() + 1;
  APSInt APResult;
  if constexpr (Op == IncDecOp::Inc)
    APResult = ++Value.toAPSInt(Bits);
  else
    APResult = --Value.toAPSInt(Bits);

  // Report undefined behaviour, stopping if required.
  const Expr *E = S.Current->getExpr(OpPC);
  QualType Type = E->getType();
  if (S.checkingForUndefinedBehavior()) {
    SmallString<32> Trunc;
    APResult.trunc(Result.bitWidth())
        .toString(Trunc, 10, Result.isSigned(), /*formatAsCLiteral=*/false,
                  /*UpperCase=*/true, /*InsertSeparators=*/true);
    auto Loc = E->getExprLoc();
    S.report(Loc, diag::warn_integer_constant_overflow)
        << Trunc << Type << E->getSourceRange();
    return true;
  }

  S.CCEDiag(E, diag::note_constexpr_overflow) << APResult << Type;
  return S.noteUndefinedBehavior();
}

/// 1) Pops a pointer from the stack
/// 2) Load the value from the pointer
/// 3) Writes the value increased by one back to the pointer
/// 4) Pushes the original (pre-inc) value on the stack.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Inc(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Increment))
    return false;

  return IncDecHelper<T, IncDecOp::Inc, PushVal::Yes>(S, OpPC, Ptr);
}

/// 1) Pops a pointer from the stack
/// 2) Load the value from the pointer
/// 3) Writes the value increased by one back to the pointer
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool IncPop(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Increment))
    return false;

  return IncDecHelper<T, IncDecOp::Inc, PushVal::No>(S, OpPC, Ptr);
}

/// 1) Pops a pointer from the stack
/// 2) Load the value from the pointer
/// 3) Writes the value decreased by one back to the pointer
/// 4) Pushes the original (pre-dec) value on the stack.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Dec(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Decrement))
    return false;

  return IncDecHelper<T, IncDecOp::Dec, PushVal::Yes>(S, OpPC, Ptr);
}

/// 1) Pops a pointer from the stack
/// 2) Load the value from the pointer
/// 3) Writes the value decreased by one back to the pointer
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool DecPop(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Decrement))
    return false;

  return IncDecHelper<T, IncDecOp::Dec, PushVal::No>(S, OpPC, Ptr);
}

template <IncDecOp Op, PushVal DoPush>
bool IncDecFloatHelper(InterpState &S, CodePtr OpPC, const Pointer &Ptr,
                       llvm::RoundingMode RM) {
  Floating Value = Ptr.deref<Floating>();
  Floating Result;

  if constexpr (DoPush == PushVal::Yes)
    S.Stk.push<Floating>(Value);

  llvm::APFloat::opStatus Status;
  if constexpr (Op == IncDecOp::Inc)
    Status = Floating::increment(Value, RM, &Result);
  else
    Status = Floating::decrement(Value, RM, &Result);

  Ptr.deref<Floating>() = Result;

  return CheckFloatResult(S, OpPC, Result, Status);
}

inline bool Incf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Increment))
    return false;

  return IncDecFloatHelper<IncDecOp::Inc, PushVal::Yes>(S, OpPC, Ptr, RM);
}

inline bool IncfPop(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Increment))
    return false;

  return IncDecFloatHelper<IncDecOp::Inc, PushVal::No>(S, OpPC, Ptr, RM);
}

inline bool Decf(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Decrement))
    return false;

  return IncDecFloatHelper<IncDecOp::Dec, PushVal::Yes>(S, OpPC, Ptr, RM);
}

inline bool DecfPop(InterpState &S, CodePtr OpPC, llvm::RoundingMode RM) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr, AK_Decrement))
    return false;

  return IncDecFloatHelper<IncDecOp::Dec, PushVal::No>(S, OpPC, Ptr, RM);
}

/// 1) Pops the value from the stack.
/// 2) Pushes the bitwise complemented value on the stack (~V).
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Comp(InterpState &S, CodePtr OpPC) {
  const T &Val = S.Stk.pop<T>();
  T Result;
  if (!T::comp(Val, &Result)) {
    S.Stk.push<T>(Result);
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// EQ, NE, GT, GE, LT, LE
//===----------------------------------------------------------------------===//

using CompareFn = llvm::function_ref<bool(ComparisonCategoryResult)>;

template <typename T>
bool CmpHelper(InterpState &S, CodePtr OpPC, CompareFn Fn) {
  assert((!std::is_same_v<T, MemberPointer>) &&
         "Non-equality comparisons on member pointer types should already be "
         "rejected in Sema.");
  using BoolT = PrimConv<PT_Bool>::T;
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();
  S.Stk.push<BoolT>(BoolT::from(Fn(LHS.compare(RHS))));
  return true;
}

template <typename T>
bool CmpHelperEQ(InterpState &S, CodePtr OpPC, CompareFn Fn) {
  return CmpHelper<T>(S, OpPC, Fn);
}

/// Function pointers cannot be compared in an ordered way.
template <>
inline bool CmpHelper<FunctionPointer>(InterpState &S, CodePtr OpPC,
                                       CompareFn Fn) {
  const auto &RHS = S.Stk.pop<FunctionPointer>();
  const auto &LHS = S.Stk.pop<FunctionPointer>();

  const SourceInfo &Loc = S.Current->getSource(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_pointer_comparison_unspecified)
      << LHS.toDiagnosticString(S.getCtx())
      << RHS.toDiagnosticString(S.getCtx());
  return false;
}

template <>
inline bool CmpHelperEQ<FunctionPointer>(InterpState &S, CodePtr OpPC,
                                         CompareFn Fn) {
  const auto &RHS = S.Stk.pop<FunctionPointer>();
  const auto &LHS = S.Stk.pop<FunctionPointer>();

  // We cannot compare against weak declarations at compile time.
  for (const auto &FP : {LHS, RHS}) {
    if (FP.isWeak()) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_pointer_weak_comparison)
          << FP.toDiagnosticString(S.getCtx());
      return false;
    }
  }

  S.Stk.push<Boolean>(Boolean::from(Fn(LHS.compare(RHS))));
  return true;
}

template <>
inline bool CmpHelper<Pointer>(InterpState &S, CodePtr OpPC, CompareFn Fn) {
  using BoolT = PrimConv<PT_Bool>::T;
  const Pointer &RHS = S.Stk.pop<Pointer>();
  const Pointer &LHS = S.Stk.pop<Pointer>();

  if (!Pointer::hasSameBase(LHS, RHS)) {
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.FFDiag(Loc, diag::note_constexpr_pointer_comparison_unspecified)
        << LHS.toDiagnosticString(S.getCtx())
        << RHS.toDiagnosticString(S.getCtx());
    return false;
  } else {
    unsigned VL = LHS.getByteOffset();
    unsigned VR = RHS.getByteOffset();
    S.Stk.push<BoolT>(BoolT::from(Fn(Compare(VL, VR))));
    return true;
  }
}

template <>
inline bool CmpHelperEQ<Pointer>(InterpState &S, CodePtr OpPC, CompareFn Fn) {
  using BoolT = PrimConv<PT_Bool>::T;
  const Pointer &RHS = S.Stk.pop<Pointer>();
  const Pointer &LHS = S.Stk.pop<Pointer>();

  if (LHS.isZero() && RHS.isZero()) {
    S.Stk.push<BoolT>(BoolT::from(Fn(ComparisonCategoryResult::Equal)));
    return true;
  }

  // Reject comparisons to weak pointers.
  for (const auto &P : {LHS, RHS}) {
    if (P.isZero())
      continue;
    if (P.isWeak()) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_pointer_weak_comparison)
          << P.toDiagnosticString(S.getCtx());
      return false;
    }
  }

  if (!Pointer::hasSameBase(LHS, RHS)) {
    if (LHS.isOnePastEnd() && !RHS.isOnePastEnd() && !RHS.isZero() &&
        RHS.getOffset() == 0) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_pointer_comparison_past_end)
          << LHS.toDiagnosticString(S.getCtx());
      return false;
    } else if (RHS.isOnePastEnd() && !LHS.isOnePastEnd() && !LHS.isZero() &&
               LHS.getOffset() == 0) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_pointer_comparison_past_end)
          << RHS.toDiagnosticString(S.getCtx());
      return false;
    }

    S.Stk.push<BoolT>(BoolT::from(Fn(ComparisonCategoryResult::Unordered)));
    return true;
  } else {
    unsigned VL = LHS.getByteOffset();
    unsigned VR = RHS.getByteOffset();

    // In our Pointer class, a pointer to an array and a pointer to the first
    // element in the same array are NOT equal. They have the same Base value,
    // but a different Offset. This is a pretty rare case, so we fix this here
    // by comparing pointers to the first elements.
    if (!LHS.isZero() && LHS.isArrayRoot())
      VL = LHS.atIndex(0).getByteOffset();
    if (!RHS.isZero() && RHS.isArrayRoot())
      VR = RHS.atIndex(0).getByteOffset();

    S.Stk.push<BoolT>(BoolT::from(Fn(Compare(VL, VR))));
    return true;
  }
}

template <>
inline bool CmpHelperEQ<MemberPointer>(InterpState &S, CodePtr OpPC,
                                       CompareFn Fn) {
  const auto &RHS = S.Stk.pop<MemberPointer>();
  const auto &LHS = S.Stk.pop<MemberPointer>();

  // If either operand is a pointer to a weak function, the comparison is not
  // constant.
  for (const auto &MP : {LHS, RHS}) {
    if (const CXXMethodDecl *MD = MP.getMemberFunction(); MD && MD->isWeak()) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_mem_pointer_weak_comparison) << MD;
      return false;
    }
  }

  // C++11 [expr.eq]p2:
  //   If both operands are null, they compare equal. Otherwise if only one is
  //   null, they compare unequal.
  if (LHS.isZero() && RHS.isZero()) {
    S.Stk.push<Boolean>(Fn(ComparisonCategoryResult::Equal));
    return true;
  }
  if (LHS.isZero() || RHS.isZero()) {
    S.Stk.push<Boolean>(Fn(ComparisonCategoryResult::Unordered));
    return true;
  }

  // We cannot compare against virtual declarations at compile time.
  for (const auto &MP : {LHS, RHS}) {
    if (const CXXMethodDecl *MD = MP.getMemberFunction();
        MD && MD->isVirtual()) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.CCEDiag(Loc, diag::note_constexpr_compare_virtual_mem_ptr) << MD;
    }
  }

  S.Stk.push<Boolean>(Boolean::from(Fn(LHS.compare(RHS))));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool EQ(InterpState &S, CodePtr OpPC) {
  return CmpHelperEQ<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R == ComparisonCategoryResult::Equal;
  });
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CMP3(InterpState &S, CodePtr OpPC, const ComparisonCategoryInfo *CmpInfo) {
  const T &RHS = S.Stk.pop<T>();
  const T &LHS = S.Stk.pop<T>();
  const Pointer &P = S.Stk.peek<Pointer>();

  ComparisonCategoryResult CmpResult = LHS.compare(RHS);
  if (CmpResult == ComparisonCategoryResult::Unordered) {
    // This should only happen with pointers.
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.FFDiag(Loc, diag::note_constexpr_pointer_comparison_unspecified)
        << LHS.toDiagnosticString(S.getCtx())
        << RHS.toDiagnosticString(S.getCtx());
    return false;
  }

  assert(CmpInfo);
  const auto *CmpValueInfo =
      CmpInfo->getValueInfo(CmpInfo->makeWeakResult(CmpResult));
  assert(CmpValueInfo);
  assert(CmpValueInfo->hasValidIntValue());
  return SetThreeWayComparisonField(S, OpPC, P, CmpValueInfo->getIntValue());
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool NE(InterpState &S, CodePtr OpPC) {
  return CmpHelperEQ<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R != ComparisonCategoryResult::Equal;
  });
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool LT(InterpState &S, CodePtr OpPC) {
  return CmpHelper<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R == ComparisonCategoryResult::Less;
  });
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool LE(InterpState &S, CodePtr OpPC) {
  return CmpHelper<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R == ComparisonCategoryResult::Less ||
           R == ComparisonCategoryResult::Equal;
  });
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GT(InterpState &S, CodePtr OpPC) {
  return CmpHelper<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R == ComparisonCategoryResult::Greater;
  });
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GE(InterpState &S, CodePtr OpPC) {
  return CmpHelper<T>(S, OpPC, [](ComparisonCategoryResult R) {
    return R == ComparisonCategoryResult::Greater ||
           R == ComparisonCategoryResult::Equal;
  });
}

//===----------------------------------------------------------------------===//
// InRange
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InRange(InterpState &S, CodePtr OpPC) {
  const T RHS = S.Stk.pop<T>();
  const T LHS = S.Stk.pop<T>();
  const T Value = S.Stk.pop<T>();

  S.Stk.push<bool>(LHS <= Value && Value <= RHS);
  return true;
}

//===----------------------------------------------------------------------===//
// Dup, Pop, Test
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Dup(InterpState &S, CodePtr OpPC) {
  S.Stk.push<T>(S.Stk.peek<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Pop(InterpState &S, CodePtr OpPC) {
  S.Stk.pop<T>();
  return true;
}

//===----------------------------------------------------------------------===//
// Const
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Const(InterpState &S, CodePtr OpPC, const T &Arg) {
  S.Stk.push<T>(Arg);
  return true;
}

//===----------------------------------------------------------------------===//
// Get/Set Local/Param/Global/This
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetLocal(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &Ptr = S.Current->getLocalPointer(I);
  if (!CheckLoad(S, OpPC, Ptr))
    return false;
  S.Stk.push<T>(Ptr.deref<T>());
  return true;
}

/// 1) Pops the value from the stack.
/// 2) Writes the value to the local variable with the
///    given offset.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SetLocal(InterpState &S, CodePtr OpPC, uint32_t I) {
  S.Current->setLocal<T>(I, S.Stk.pop<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetParam(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression()) {
    return false;
  }
  S.Stk.push<T>(S.Current->getParam<T>(I));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SetParam(InterpState &S, CodePtr OpPC, uint32_t I) {
  S.Current->setParam<T>(I, S.Stk.pop<T>());
  return true;
}

/// 1) Peeks a pointer on the stack
/// 2) Pushes the value of the pointer's field on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetField(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &Obj = S.Stk.peek<Pointer>();
  if (!CheckNull(S, OpPC, Obj, CSK_Field))
      return false;
  if (!CheckRange(S, OpPC, Obj, CSK_Field))
    return false;
  const Pointer &Field = Obj.atField(I);
  if (!CheckLoad(S, OpPC, Field))
    return false;
  S.Stk.push<T>(Field.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SetField(InterpState &S, CodePtr OpPC, uint32_t I) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Obj = S.Stk.peek<Pointer>();
  if (!CheckNull(S, OpPC, Obj, CSK_Field))
    return false;
  if (!CheckRange(S, OpPC, Obj, CSK_Field))
    return false;
  const Pointer &Field = Obj.atField(I);
  if (!CheckStore(S, OpPC, Field))
    return false;
  Field.initialize();
  Field.deref<T>() = Value;
  return true;
}

/// 1) Pops a pointer from the stack
/// 2) Pushes the value of the pointer's field on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetFieldPop(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &Obj = S.Stk.pop<Pointer>();
  if (!CheckNull(S, OpPC, Obj, CSK_Field))
    return false;
  if (!CheckRange(S, OpPC, Obj, CSK_Field))
    return false;
  const Pointer &Field = Obj.atField(I);
  if (!CheckLoad(S, OpPC, Field))
    return false;
  S.Stk.push<T>(Field.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetThisField(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  const Pointer &Field = This.atField(I);
  if (!CheckLoad(S, OpPC, Field))
    return false;
  S.Stk.push<T>(Field.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SetThisField(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const T &Value = S.Stk.pop<T>();
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  const Pointer &Field = This.atField(I);
  if (!CheckStore(S, OpPC, Field))
    return false;
  Field.deref<T>() = Value;
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetGlobal(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &Ptr = S.P.getPtrGlobal(I);
  if (!CheckConstant(S, OpPC, Ptr.getFieldDesc()))
    return false;
  if (Ptr.isExtern())
    return false;

  // If a global variable is uninitialized, that means the initializer we've
  // compiled for it wasn't a constant expression. Diagnose that.
  if (!CheckGlobalInitialized(S, OpPC, Ptr))
    return false;

  S.Stk.push<T>(Ptr.deref<T>());
  return true;
}

/// Same as GetGlobal, but without the checks.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool GetGlobalUnchecked(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &Ptr = S.P.getPtrGlobal(I);
  if (!Ptr.isInitialized())
    return false;
  S.Stk.push<T>(Ptr.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SetGlobal(InterpState &S, CodePtr OpPC, uint32_t I) {
  // TODO: emit warning.
  return false;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitGlobal(InterpState &S, CodePtr OpPC, uint32_t I) {
  const Pointer &P = S.P.getGlobal(I);
  P.deref<T>() = S.Stk.pop<T>();
  P.initialize();
  return true;
}

/// 1) Converts the value on top of the stack to an APValue
/// 2) Sets that APValue on \Temp
/// 3) Initializes global with index \I with that
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitGlobalTemp(InterpState &S, CodePtr OpPC, uint32_t I,
                    const LifetimeExtendedTemporaryDecl *Temp) {
  const Pointer &Ptr = S.P.getGlobal(I);

  const T Value = S.Stk.peek<T>();
  APValue APV = Value.toAPValue(S.getCtx());
  APValue *Cached = Temp->getOrCreateValue(true);
  *Cached = APV;

  assert(Ptr.getDeclDesc()->asExpr());

  S.SeenGlobalTemporaries.push_back(
      std::make_pair(Ptr.getDeclDesc()->asExpr(), Temp));

  Ptr.deref<T>() = S.Stk.pop<T>();
  Ptr.initialize();
  return true;
}

/// 1) Converts the value on top of the stack to an APValue
/// 2) Sets that APValue on \Temp
/// 3) Initialized global with index \I with that
inline bool InitGlobalTempComp(InterpState &S, CodePtr OpPC,
                               const LifetimeExtendedTemporaryDecl *Temp) {
  assert(Temp);
  const Pointer &P = S.Stk.peek<Pointer>();
  APValue *Cached = Temp->getOrCreateValue(true);

  S.SeenGlobalTemporaries.push_back(
      std::make_pair(P.getDeclDesc()->asExpr(), Temp));

  if (std::optional<APValue> APV =
          P.toRValue(S.getCtx(), Temp->getTemporaryExpr()->getType())) {
    *Cached = *APV;
    return true;
  }

  return false;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitThisField(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  const Pointer &Field = This.atField(I);
  Field.deref<T>() = S.Stk.pop<T>();
  Field.initialize();
  return true;
}

// FIXME: The Field pointer here is too much IMO and we could instead just
// pass an Offset + BitWidth pair.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitThisBitField(InterpState &S, CodePtr OpPC, const Record::Field *F,
                      uint32_t FieldOffset) {
  assert(F->isBitField());
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  const Pointer &Field = This.atField(FieldOffset);
  const auto &Value = S.Stk.pop<T>();
  Field.deref<T>() = Value.truncate(F->Decl->getBitWidthValue(S.getCtx()));
  Field.initialize();
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitThisFieldActive(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  const Pointer &Field = This.atField(I);
  Field.deref<T>() = S.Stk.pop<T>();
  Field.activate();
  Field.initialize();
  return true;
}

/// 1) Pops the value from the stack
/// 2) Peeks a pointer from the stack
/// 3) Pushes the value to field I of the pointer on the stack
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitField(InterpState &S, CodePtr OpPC, uint32_t I) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Field = S.Stk.peek<Pointer>().atField(I);
  Field.deref<T>() = Value;
  Field.activate();
  Field.initialize();
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitBitField(InterpState &S, CodePtr OpPC, const Record::Field *F) {
  assert(F->isBitField());
  const T &Value = S.Stk.pop<T>();
  const Pointer &Field = S.Stk.peek<Pointer>().atField(F->Offset);
  Field.deref<T>() = Value.truncate(F->Decl->getBitWidthValue(S.getCtx()));
  Field.activate();
  Field.initialize();
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitFieldActive(InterpState &S, CodePtr OpPC, uint32_t I) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  const Pointer &Field = Ptr.atField(I);
  Field.deref<T>() = Value;
  Field.activate();
  Field.initialize();
  return true;
}

//===----------------------------------------------------------------------===//
// GetPtr Local/Param/Global/Field/This
//===----------------------------------------------------------------------===//

inline bool GetPtrLocal(InterpState &S, CodePtr OpPC, uint32_t I) {
  S.Stk.push<Pointer>(S.Current->getLocalPointer(I));
  return true;
}

inline bool GetPtrParam(InterpState &S, CodePtr OpPC, uint32_t I) {
  if (S.checkingPotentialConstantExpression()) {
    return false;
  }
  S.Stk.push<Pointer>(S.Current->getParamPointer(I));
  return true;
}

inline bool GetPtrGlobal(InterpState &S, CodePtr OpPC, uint32_t I) {
  S.Stk.push<Pointer>(S.P.getPtrGlobal(I));
  return true;
}

/// 1) Peeks a Pointer
/// 2) Pushes Pointer.atField(Off) on the stack
inline bool GetPtrField(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.peek<Pointer>();

  if (S.getLangOpts().CPlusPlus && S.inConstantContext() &&
      !CheckNull(S, OpPC, Ptr, CSK_Field))
    return false;

  if (!CheckExtern(S, OpPC, Ptr))
    return false;
  if (!CheckRange(S, OpPC, Ptr, CSK_Field))
    return false;
  if (!CheckArray(S, OpPC, Ptr))
    return false;
  if (!CheckSubobject(S, OpPC, Ptr, CSK_Field))
    return false;

  if (Ptr.isBlockPointer() && Off > Ptr.block()->getSize())
    return false;
  S.Stk.push<Pointer>(Ptr.atField(Off));
  return true;
}

inline bool GetPtrFieldPop(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (S.getLangOpts().CPlusPlus && S.inConstantContext() &&
      !CheckNull(S, OpPC, Ptr, CSK_Field))
    return false;

  if (!CheckExtern(S, OpPC, Ptr))
    return false;
  if (!CheckRange(S, OpPC, Ptr, CSK_Field))
    return false;
  if (!CheckArray(S, OpPC, Ptr))
    return false;
  if (!CheckSubobject(S, OpPC, Ptr, CSK_Field))
    return false;

  if (Ptr.isBlockPointer() && Off > Ptr.block()->getSize())
    return false;

  S.Stk.push<Pointer>(Ptr.atField(Off));
  return true;
}

inline bool GetPtrThisField(InterpState &S, CodePtr OpPC, uint32_t Off) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  S.Stk.push<Pointer>(This.atField(Off));
  return true;
}

inline bool GetPtrActiveField(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckNull(S, OpPC, Ptr, CSK_Field))
    return false;
  if (!CheckRange(S, OpPC, Ptr, CSK_Field))
    return false;
  Pointer Field = Ptr.atField(Off);
  Ptr.deactivate();
  Field.activate();
  S.Stk.push<Pointer>(std::move(Field));
  return true;
}

inline bool GetPtrActiveThisField(InterpState &S, CodePtr OpPC, uint32_t Off) {
 if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  Pointer Field = This.atField(Off);
  This.deactivate();
  Field.activate();
  S.Stk.push<Pointer>(std::move(Field));
  return true;
}

inline bool GetPtrDerivedPop(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckNull(S, OpPC, Ptr, CSK_Derived))
    return false;
  if (!CheckSubobject(S, OpPC, Ptr, CSK_Derived))
    return false;
  if (!CheckDowncast(S, OpPC, Ptr, Off))
    return false;

  S.Stk.push<Pointer>(Ptr.atFieldSub(Off));
  return true;
}

inline bool GetPtrBase(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (!CheckNull(S, OpPC, Ptr, CSK_Base))
    return false;
  if (!CheckSubobject(S, OpPC, Ptr, CSK_Base))
    return false;
  S.Stk.push<Pointer>(Ptr.atField(Off));
  return true;
}

inline bool GetPtrBasePop(InterpState &S, CodePtr OpPC, uint32_t Off) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckNull(S, OpPC, Ptr, CSK_Base))
    return false;
  if (!CheckSubobject(S, OpPC, Ptr, CSK_Base))
    return false;
  S.Stk.push<Pointer>(Ptr.atField(Off));
  return true;
}

inline bool GetMemberPtrBasePop(InterpState &S, CodePtr OpPC, int32_t Off) {
  const auto &Ptr = S.Stk.pop<MemberPointer>();
  S.Stk.push<MemberPointer>(Ptr.atInstanceBase(Off));
  return true;
}

inline bool GetPtrThisBase(InterpState &S, CodePtr OpPC, uint32_t Off) {
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  S.Stk.push<Pointer>(This.atField(Off));
  return true;
}

inline bool FinishInitPop(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (Ptr.canBeInitialized()) {
    Ptr.initialize();
    Ptr.activate();
  }
  return true;
}

inline bool FinishInit(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (Ptr.canBeInitialized()) {
    Ptr.initialize();
    Ptr.activate();
  }
  return true;
}

inline bool Dump(InterpState &S, CodePtr OpPC) {
  S.Stk.dump();
  return true;
}

inline bool VirtBaseHelper(InterpState &S, CodePtr OpPC, const RecordDecl *Decl,
                           const Pointer &Ptr) {
  Pointer Base = Ptr;
  while (Base.isBaseClass())
    Base = Base.getBase();

  const Record::Base *VirtBase = Base.getRecord()->getVirtualBase(Decl);
  S.Stk.push<Pointer>(Base.atField(VirtBase->Offset));
  return true;
}

inline bool GetPtrVirtBasePop(InterpState &S, CodePtr OpPC,
                              const RecordDecl *D) {
  assert(D);
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckNull(S, OpPC, Ptr, CSK_Base))
    return false;
  return VirtBaseHelper(S, OpPC, D, Ptr);
}

inline bool GetPtrThisVirtBase(InterpState &S, CodePtr OpPC,
                               const RecordDecl *D) {
  assert(D);
  if (S.checkingPotentialConstantExpression())
    return false;
  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;
  return VirtBaseHelper(S, OpPC, D, S.Current->getThis());
}

//===----------------------------------------------------------------------===//
// Load, Store, Init
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Load(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr))
    return false;
  if (!Ptr.isBlockPointer())
    return false;
  S.Stk.push<T>(Ptr.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool LoadPop(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckLoad(S, OpPC, Ptr))
    return false;
  if (!Ptr.isBlockPointer())
    return false;
  S.Stk.push<T>(Ptr.deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Store(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (!CheckStore(S, OpPC, Ptr))
    return false;
  if (Ptr.canBeInitialized())
    Ptr.initialize();
  Ptr.deref<T>() = Value;
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool StorePop(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckStore(S, OpPC, Ptr))
    return false;
  if (Ptr.canBeInitialized())
    Ptr.initialize();
  Ptr.deref<T>() = Value;
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool StoreBitField(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (!CheckStore(S, OpPC, Ptr))
    return false;
  if (Ptr.canBeInitialized())
    Ptr.initialize();
  if (const auto *FD = Ptr.getField())
    Ptr.deref<T>() = Value.truncate(FD->getBitWidthValue(S.getCtx()));
  else
    Ptr.deref<T>() = Value;
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool StoreBitFieldPop(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckStore(S, OpPC, Ptr))
    return false;
  if (Ptr.canBeInitialized())
    Ptr.initialize();
  if (const auto *FD = Ptr.getField())
    Ptr.deref<T>() = Value.truncate(FD->getBitWidthValue(S.getCtx()));
  else
    Ptr.deref<T>() = Value;
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Init(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.peek<Pointer>();
  if (!CheckInit(S, OpPC, Ptr)) {
    assert(false);
    return false;
  }
  Ptr.initialize();
  new (&Ptr.deref<T>()) T(Value);
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitPop(InterpState &S, CodePtr OpPC) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  if (!CheckInit(S, OpPC, Ptr))
    return false;
  Ptr.initialize();
  new (&Ptr.deref<T>()) T(Value);
  return true;
}

/// 1) Pops the value from the stack
/// 2) Peeks a pointer and gets its index \Idx
/// 3) Sets the value on the pointer, leaving the pointer on the stack.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitElem(InterpState &S, CodePtr OpPC, uint32_t Idx) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.peek<Pointer>().atIndex(Idx);
  if (Ptr.isUnknownSizeArray())
    return false;
  if (!CheckInit(S, OpPC, Ptr))
    return false;
  Ptr.initialize();
  new (&Ptr.deref<T>()) T(Value);
  return true;
}

/// The same as InitElem, but pops the pointer as well.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool InitElemPop(InterpState &S, CodePtr OpPC, uint32_t Idx) {
  const T &Value = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>().atIndex(Idx);
  if (Ptr.isUnknownSizeArray())
    return false;
  if (!CheckInit(S, OpPC, Ptr))
    return false;
  Ptr.initialize();
  new (&Ptr.deref<T>()) T(Value);
  return true;
}

inline bool Memcpy(InterpState &S, CodePtr OpPC) {
  const Pointer &Src = S.Stk.pop<Pointer>();
  Pointer &Dest = S.Stk.peek<Pointer>();

  if (!CheckLoad(S, OpPC, Src))
    return false;

  return DoMemcpy(S, OpPC, Src, Dest);
}

inline bool ToMemberPtr(InterpState &S, CodePtr OpPC) {
  const auto &Member = S.Stk.pop<MemberPointer>();
  const auto &Base = S.Stk.pop<Pointer>();

  S.Stk.push<MemberPointer>(Member.takeInstance(Base));
  return true;
}

inline bool CastMemberPtrPtr(InterpState &S, CodePtr OpPC) {
  const auto &MP = S.Stk.pop<MemberPointer>();

  if (std::optional<Pointer> Ptr = MP.toPointer(S.Ctx)) {
    S.Stk.push<Pointer>(*Ptr);
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// AddOffset, SubOffset
//===----------------------------------------------------------------------===//

template <class T, ArithOp Op>
bool OffsetHelper(InterpState &S, CodePtr OpPC, const T &Offset,
                  const Pointer &Ptr) {
  // A zero offset does not change the pointer.
  if (Offset.isZero()) {
    S.Stk.push<Pointer>(Ptr);
    return true;
  }

  if (!CheckNull(S, OpPC, Ptr, CSK_ArrayIndex)) {
    // The CheckNull will have emitted a note already, but we only
    // abort in C++, since this is fine in C.
    if (S.getLangOpts().CPlusPlus)
      return false;
  }

  // Arrays of unknown bounds cannot have pointers into them.
  if (!CheckArray(S, OpPC, Ptr))
    return false;

  uint64_t MaxIndex = static_cast<uint64_t>(Ptr.getNumElems());
  uint64_t Index;
  if (Ptr.isOnePastEnd())
    Index = MaxIndex;
  else
    Index = Ptr.getIndex();

  bool Invalid = false;
  // Helper to report an invalid offset, computed as APSInt.
  auto DiagInvalidOffset = [&]() -> void {
    const unsigned Bits = Offset.bitWidth();
    APSInt APOffset(Offset.toAPSInt().extend(Bits + 2), /*IsUnsigend=*/false);
    APSInt APIndex(APInt(Bits + 2, Index, /*IsSigned=*/true),
                   /*IsUnsigned=*/false);
    APSInt NewIndex =
        (Op == ArithOp::Add) ? (APIndex + APOffset) : (APIndex - APOffset);
    S.CCEDiag(S.Current->getSource(OpPC), diag::note_constexpr_array_index)
        << NewIndex << /*array*/ static_cast<int>(!Ptr.inArray()) << MaxIndex;
    Invalid = true;
  };

  if (Ptr.isBlockPointer()) {
    uint64_t IOffset = static_cast<uint64_t>(Offset);
    uint64_t MaxOffset = MaxIndex - Index;

    if constexpr (Op == ArithOp::Add) {
      // If the new offset would be negative, bail out.
      if (Offset.isNegative() && (Offset.isMin() || -IOffset > Index))
        DiagInvalidOffset();

      // If the new offset would be out of bounds, bail out.
      if (Offset.isPositive() && IOffset > MaxOffset)
        DiagInvalidOffset();
    } else {
      // If the new offset would be negative, bail out.
      if (Offset.isPositive() && Index < IOffset)
        DiagInvalidOffset();

      // If the new offset would be out of bounds, bail out.
      if (Offset.isNegative() && (Offset.isMin() || -IOffset > MaxOffset))
        DiagInvalidOffset();
    }
  }

  if (Invalid && S.getLangOpts().CPlusPlus)
    return false;

  // Offset is valid - compute it on unsigned.
  int64_t WideIndex = static_cast<int64_t>(Index);
  int64_t WideOffset = static_cast<int64_t>(Offset);
  int64_t Result;
  if constexpr (Op == ArithOp::Add)
    Result = WideIndex + WideOffset;
  else
    Result = WideIndex - WideOffset;

  // When the pointer is one-past-end, going back to index 0 is the only
  // useful thing we can do. Any other index has been diagnosed before and
  // we don't get here.
  if (Result == 0 && Ptr.isOnePastEnd()) {
    S.Stk.push<Pointer>(Ptr.asBlockPointer().Pointee,
                        Ptr.asBlockPointer().Base);
    return true;
  }

  S.Stk.push<Pointer>(Ptr.atIndex(static_cast<uint64_t>(Result)));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool AddOffset(InterpState &S, CodePtr OpPC) {
  const T &Offset = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  return OffsetHelper<T, ArithOp::Add>(S, OpPC, Offset, Ptr);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool SubOffset(InterpState &S, CodePtr OpPC) {
  const T &Offset = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  return OffsetHelper<T, ArithOp::Sub>(S, OpPC, Offset, Ptr);
}

template <ArithOp Op>
static inline bool IncDecPtrHelper(InterpState &S, CodePtr OpPC,
                                   const Pointer &Ptr) {
  if (Ptr.isDummy())
    return false;

  using OneT = Integral<8, false>;

  const Pointer &P = Ptr.deref<Pointer>();
  if (!CheckNull(S, OpPC, P, CSK_ArrayIndex))
    return false;

  // Get the current value on the stack.
  S.Stk.push<Pointer>(P);

  // Now the current Ptr again and a constant 1.
  OneT One = OneT::from(1);
  if (!OffsetHelper<OneT, Op>(S, OpPC, One, P))
    return false;

  // Store the new value.
  Ptr.deref<Pointer>() = S.Stk.pop<Pointer>();
  return true;
}

static inline bool IncPtr(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (!CheckInitialized(S, OpPC, Ptr, AK_Increment))
    return false;

  return IncDecPtrHelper<ArithOp::Add>(S, OpPC, Ptr);
}

static inline bool DecPtr(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (!CheckInitialized(S, OpPC, Ptr, AK_Decrement))
    return false;

  return IncDecPtrHelper<ArithOp::Sub>(S, OpPC, Ptr);
}

/// 1) Pops a Pointer from the stack.
/// 2) Pops another Pointer from the stack.
/// 3) Pushes the different of the indices of the two pointers on the stack.
template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool SubPtr(InterpState &S, CodePtr OpPC) {
  const Pointer &LHS = S.Stk.pop<Pointer>();
  const Pointer &RHS = S.Stk.pop<Pointer>();

  if (RHS.isZero()) {
    S.Stk.push<T>(T::from(LHS.getIndex()));
    return true;
  }

  if (!Pointer::hasSameBase(LHS, RHS) && S.getLangOpts().CPlusPlus) {
    // TODO: Diagnose.
    return false;
  }

  if (LHS.isZero() && RHS.isZero()) {
    S.Stk.push<T>();
    return true;
  }

  T A = LHS.isElementPastEnd() ? T::from(LHS.getNumElems())
                               : T::from(LHS.getIndex());
  T B = RHS.isElementPastEnd() ? T::from(RHS.getNumElems())
                               : T::from(RHS.getIndex());
  return AddSubMulHelper<T, T::sub, std::minus>(S, OpPC, A.bitWidth(), A, B);
}

//===----------------------------------------------------------------------===//
// Destroy
//===----------------------------------------------------------------------===//

inline bool Destroy(InterpState &S, CodePtr OpPC, uint32_t I) {
  S.Current->destroy(I);
  return true;
}

//===----------------------------------------------------------------------===//
// Cast, CastFP
//===----------------------------------------------------------------------===//

template <PrimType TIn, PrimType TOut> bool Cast(InterpState &S, CodePtr OpPC) {
  using T = typename PrimConv<TIn>::T;
  using U = typename PrimConv<TOut>::T;
  S.Stk.push<U>(U::from(S.Stk.pop<T>()));
  return true;
}

/// 1) Pops a Floating from the stack.
/// 2) Pushes a new floating on the stack that uses the given semantics.
inline bool CastFP(InterpState &S, CodePtr OpPC, const llvm::fltSemantics *Sem,
            llvm::RoundingMode RM) {
  Floating F = S.Stk.pop<Floating>();
  Floating Result = F.toSemantics(Sem, RM);
  S.Stk.push<Floating>(Result);
  return true;
}

/// Like Cast(), but we cast to an arbitrary-bitwidth integral, so we need
/// to know what bitwidth the result should be.
template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CastAP(InterpState &S, CodePtr OpPC, uint32_t BitWidth) {
  S.Stk.push<IntegralAP<false>>(
      IntegralAP<false>::from(S.Stk.pop<T>(), BitWidth));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CastAPS(InterpState &S, CodePtr OpPC, uint32_t BitWidth) {
  S.Stk.push<IntegralAP<true>>(
      IntegralAP<true>::from(S.Stk.pop<T>(), BitWidth));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CastIntegralFloating(InterpState &S, CodePtr OpPC,
                          const llvm::fltSemantics *Sem,
                          llvm::RoundingMode RM) {
  const T &From = S.Stk.pop<T>();
  APSInt FromAP = From.toAPSInt();
  Floating Result;

  auto Status = Floating::fromIntegral(FromAP, *Sem, RM, Result);
  S.Stk.push<Floating>(Result);

  return CheckFloatResult(S, OpPC, Result, Status);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CastFloatingIntegral(InterpState &S, CodePtr OpPC) {
  const Floating &F = S.Stk.pop<Floating>();

  if constexpr (std::is_same_v<T, Boolean>) {
    S.Stk.push<T>(T(F.isNonZero()));
    return true;
  } else {
    APSInt Result(std::max(8u, T::bitWidth()),
                  /*IsUnsigned=*/!T::isSigned());
    auto Status = F.convertToInteger(Result);

    // Float-to-Integral overflow check.
    if ((Status & APFloat::opStatus::opInvalidOp)) {
      const Expr *E = S.Current->getExpr(OpPC);
      QualType Type = E->getType();

      S.CCEDiag(E, diag::note_constexpr_overflow) << F.getAPFloat() << Type;
      if (S.noteUndefinedBehavior()) {
        S.Stk.push<T>(T(Result));
        return true;
      }
      return false;
    }

    S.Stk.push<T>(T(Result));
    return CheckFloatResult(S, OpPC, F, Status);
  }
}

static inline bool CastFloatingIntegralAP(InterpState &S, CodePtr OpPC,
                                          uint32_t BitWidth) {
  const Floating &F = S.Stk.pop<Floating>();

  APSInt Result(BitWidth, /*IsUnsigned=*/true);
  auto Status = F.convertToInteger(Result);

  // Float-to-Integral overflow check.
  if ((Status & APFloat::opStatus::opInvalidOp) && F.isFinite()) {
    const Expr *E = S.Current->getExpr(OpPC);
    QualType Type = E->getType();

    S.CCEDiag(E, diag::note_constexpr_overflow) << F.getAPFloat() << Type;
    return S.noteUndefinedBehavior();
  }

  S.Stk.push<IntegralAP<true>>(IntegralAP<true>(Result));
  return CheckFloatResult(S, OpPC, F, Status);
}

static inline bool CastFloatingIntegralAPS(InterpState &S, CodePtr OpPC,
                                           uint32_t BitWidth) {
  const Floating &F = S.Stk.pop<Floating>();

  APSInt Result(BitWidth, /*IsUnsigned=*/false);
  auto Status = F.convertToInteger(Result);

  // Float-to-Integral overflow check.
  if ((Status & APFloat::opStatus::opInvalidOp) && F.isFinite()) {
    const Expr *E = S.Current->getExpr(OpPC);
    QualType Type = E->getType();

    S.CCEDiag(E, diag::note_constexpr_overflow) << F.getAPFloat() << Type;
    return S.noteUndefinedBehavior();
  }

  S.Stk.push<IntegralAP<true>>(IntegralAP<true>(Result));
  return CheckFloatResult(S, OpPC, F, Status);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool CastPointerIntegral(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (Ptr.isDummy())
    return false;

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.CCEDiag(E, diag::note_constexpr_invalid_cast)
      << 2 << S.getLangOpts().CPlusPlus << S.Current->getRange(OpPC);

  S.Stk.push<T>(T::from(Ptr.getIntegerRepresentation()));
  return true;
}

static inline bool CastPointerIntegralAP(InterpState &S, CodePtr OpPC,
                                         uint32_t BitWidth) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (Ptr.isDummy())
    return false;

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.CCEDiag(E, diag::note_constexpr_invalid_cast)
      << 2 << S.getLangOpts().CPlusPlus << S.Current->getRange(OpPC);

  S.Stk.push<IntegralAP<false>>(
      IntegralAP<false>::from(Ptr.getIntegerRepresentation(), BitWidth));
  return true;
}

static inline bool CastPointerIntegralAPS(InterpState &S, CodePtr OpPC,
                                          uint32_t BitWidth) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (Ptr.isDummy())
    return false;

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.CCEDiag(E, diag::note_constexpr_invalid_cast)
      << 2 << S.getLangOpts().CPlusPlus << S.Current->getRange(OpPC);

  S.Stk.push<IntegralAP<true>>(
      IntegralAP<true>::from(Ptr.getIntegerRepresentation(), BitWidth));
  return true;
}

static inline bool PtrPtrCast(InterpState &S, CodePtr OpPC, bool SrcIsVoidPtr) {
  const auto &Ptr = S.Stk.peek<Pointer>();

  if (SrcIsVoidPtr && S.getLangOpts().CPlusPlus) {
    bool HasValidResult = !Ptr.isZero();

    if (HasValidResult) {
      // FIXME: note_constexpr_invalid_void_star_cast
    } else if (!S.getLangOpts().CPlusPlus26) {
      const SourceInfo &E = S.Current->getSource(OpPC);
      S.CCEDiag(E, diag::note_constexpr_invalid_cast)
          << 3 << "'void *'" << S.Current->getRange(OpPC);
    }
  } else {
    const SourceInfo &E = S.Current->getSource(OpPC);
    S.CCEDiag(E, diag::note_constexpr_invalid_cast)
        << 2 << S.getLangOpts().CPlusPlus << S.Current->getRange(OpPC);
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Zero, Nullptr
//===----------------------------------------------------------------------===//

template <PrimType Name, class T = typename PrimConv<Name>::T>
bool Zero(InterpState &S, CodePtr OpPC) {
  S.Stk.push<T>(T::zero());
  return true;
}

static inline bool ZeroIntAP(InterpState &S, CodePtr OpPC, uint32_t BitWidth) {
  S.Stk.push<IntegralAP<false>>(IntegralAP<false>::zero(BitWidth));
  return true;
}

static inline bool ZeroIntAPS(InterpState &S, CodePtr OpPC, uint32_t BitWidth) {
  S.Stk.push<IntegralAP<true>>(IntegralAP<true>::zero(BitWidth));
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool Null(InterpState &S, CodePtr OpPC, const Descriptor *Desc) {
  // Note: Desc can be null.
  S.Stk.push<T>(0, Desc);
  return true;
}

//===----------------------------------------------------------------------===//
// This, ImplicitThis
//===----------------------------------------------------------------------===//

inline bool This(InterpState &S, CodePtr OpPC) {
  // Cannot read 'this' in this mode.
  if (S.checkingPotentialConstantExpression()) {
    return false;
  }

  const Pointer &This = S.Current->getThis();
  if (!CheckThis(S, OpPC, This))
    return false;

  // Ensure the This pointer has been cast to the correct base.
  if (!This.isDummy()) {
    assert(isa<CXXMethodDecl>(S.Current->getFunction()->getDecl()));
    assert(This.getRecord());
    assert(
        This.getRecord()->getDecl() ==
        cast<CXXMethodDecl>(S.Current->getFunction()->getDecl())->getParent());
  }

  S.Stk.push<Pointer>(This);
  return true;
}

inline bool RVOPtr(InterpState &S, CodePtr OpPC) {
  assert(S.Current->getFunction()->hasRVO());
  if (S.checkingPotentialConstantExpression())
    return false;
  S.Stk.push<Pointer>(S.Current->getRVOPtr());
  return true;
}

//===----------------------------------------------------------------------===//
// Shr, Shl
//===----------------------------------------------------------------------===//
enum class ShiftDir { Left, Right };

template <class LT, class RT, ShiftDir Dir>
inline bool DoShift(InterpState &S, CodePtr OpPC, LT &LHS, RT &RHS) {
  const unsigned Bits = LHS.bitWidth();

  // OpenCL 6.3j: shift values are effectively % word size of LHS.
  if (S.getLangOpts().OpenCL)
    RT::bitAnd(RHS, RT::from(LHS.bitWidth() - 1, RHS.bitWidth()),
               RHS.bitWidth(), &RHS);

  if (RHS.isNegative()) {
    // During constant-folding, a negative shift is an opposite shift. Such a
    // shift is not a constant expression.
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.CCEDiag(Loc, diag::note_constexpr_negative_shift) << RHS.toAPSInt();
    if (!S.noteUndefinedBehavior())
      return false;
    RHS = -RHS;
    return DoShift < LT, RT,
           Dir == ShiftDir::Left ? ShiftDir::Right
                                 : ShiftDir::Left > (S, OpPC, LHS, RHS);
  }

  if constexpr (Dir == ShiftDir::Left) {
    if (LHS.isNegative() && !S.getLangOpts().CPlusPlus20) {
      // C++11 [expr.shift]p2: A signed left shift must have a non-negative
      // operand, and must not overflow the corresponding unsigned type.
      // C++2a [expr.shift]p2: E1 << E2 is the unique value congruent to
      // E1 x 2^E2 module 2^N.
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.CCEDiag(Loc, diag::note_constexpr_lshift_of_negative) << LHS.toAPSInt();
      if (!S.noteUndefinedBehavior())
        return false;
    }
  }

  if (!CheckShift(S, OpPC, LHS, RHS, Bits))
    return false;

  // Limit the shift amount to Bits - 1. If this happened,
  // it has already been diagnosed by CheckShift() above,
  // but we still need to handle it.
  typename LT::AsUnsigned R;
  if constexpr (Dir == ShiftDir::Left) {
    if (RHS > RT::from(Bits - 1, RHS.bitWidth()))
      LT::AsUnsigned::shiftLeft(LT::AsUnsigned::from(LHS),
                                LT::AsUnsigned::from(Bits - 1), Bits, &R);
    else
      LT::AsUnsigned::shiftLeft(LT::AsUnsigned::from(LHS),
                                LT::AsUnsigned::from(RHS, Bits), Bits, &R);
  } else {
    if (RHS > RT::from(Bits - 1, RHS.bitWidth()))
      LT::AsUnsigned::shiftRight(LT::AsUnsigned::from(LHS),
                                 LT::AsUnsigned::from(Bits - 1), Bits, &R);
    else
      LT::AsUnsigned::shiftRight(LT::AsUnsigned::from(LHS),
                                 LT::AsUnsigned::from(RHS, Bits), Bits, &R);
  }

  S.Stk.push<LT>(LT::from(R));
  return true;
}

template <PrimType NameL, PrimType NameR>
inline bool Shr(InterpState &S, CodePtr OpPC) {
  using LT = typename PrimConv<NameL>::T;
  using RT = typename PrimConv<NameR>::T;
  auto RHS = S.Stk.pop<RT>();
  auto LHS = S.Stk.pop<LT>();

  return DoShift<LT, RT, ShiftDir::Right>(S, OpPC, LHS, RHS);
}

template <PrimType NameL, PrimType NameR>
inline bool Shl(InterpState &S, CodePtr OpPC) {
  using LT = typename PrimConv<NameL>::T;
  using RT = typename PrimConv<NameR>::T;
  auto RHS = S.Stk.pop<RT>();
  auto LHS = S.Stk.pop<LT>();

  return DoShift<LT, RT, ShiftDir::Left>(S, OpPC, LHS, RHS);
}

//===----------------------------------------------------------------------===//
// NoRet
//===----------------------------------------------------------------------===//

inline bool NoRet(InterpState &S, CodePtr OpPC) {
  SourceLocation EndLoc = S.Current->getCallee()->getEndLoc();
  S.FFDiag(EndLoc, diag::note_constexpr_no_return);
  return false;
}

//===----------------------------------------------------------------------===//
// NarrowPtr, ExpandPtr
//===----------------------------------------------------------------------===//

inline bool NarrowPtr(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  S.Stk.push<Pointer>(Ptr.narrow());
  return true;
}

inline bool ExpandPtr(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();
  S.Stk.push<Pointer>(Ptr.expand());
  return true;
}

// 1) Pops an integral value from the stack
// 2) Peeks a pointer
// 3) Pushes a new pointer that's a narrowed array
//   element of the peeked pointer with the value
//   from 1) added as offset.
//
// This leaves the original pointer on the stack and pushes a new one
// with the offset applied and narrowed.
template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool ArrayElemPtr(InterpState &S, CodePtr OpPC) {
  const T &Offset = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.peek<Pointer>();

  if (!Ptr.isZero()) {
    if (!CheckArray(S, OpPC, Ptr))
      return false;
  }

  if (!OffsetHelper<T, ArithOp::Add>(S, OpPC, Offset, Ptr))
    return false;

  return NarrowPtr(S, OpPC);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool ArrayElemPtrPop(InterpState &S, CodePtr OpPC) {
  const T &Offset = S.Stk.pop<T>();
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (!Ptr.isZero()) {
    if (!CheckArray(S, OpPC, Ptr))
      return false;
  }

  if (!OffsetHelper<T, ArithOp::Add>(S, OpPC, Offset, Ptr))
    return false;

  return NarrowPtr(S, OpPC);
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool ArrayElem(InterpState &S, CodePtr OpPC, uint32_t Index) {
  const Pointer &Ptr = S.Stk.peek<Pointer>();

  if (!CheckLoad(S, OpPC, Ptr))
    return false;

  S.Stk.push<T>(Ptr.atIndex(Index).deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool ArrayElemPop(InterpState &S, CodePtr OpPC, uint32_t Index) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (!CheckLoad(S, OpPC, Ptr))
    return false;

  S.Stk.push<T>(Ptr.atIndex(Index).deref<T>());
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool CopyArray(InterpState &S, CodePtr OpPC, uint32_t SrcIndex, uint32_t DestIndex, uint32_t Size) {
  const auto &SrcPtr = S.Stk.pop<Pointer>();
  const auto &DestPtr = S.Stk.peek<Pointer>();

  for (uint32_t I = 0; I != Size; ++I) {
    const Pointer &SP = SrcPtr.atIndex(SrcIndex + I);

    if (!CheckLoad(S, OpPC, SP))
      return false;

    const Pointer &DP = DestPtr.atIndex(DestIndex + I);
    DP.deref<T>() = SP.deref<T>();
    DP.initialize();
  }
  return true;
}

/// Just takes a pointer and checks if it's an incomplete
/// array type.
inline bool ArrayDecay(InterpState &S, CodePtr OpPC) {
  const Pointer &Ptr = S.Stk.pop<Pointer>();

  if (Ptr.isZero()) {
    S.Stk.push<Pointer>(Ptr);
    return true;
  }

  if (!CheckRange(S, OpPC, Ptr, CSK_ArrayToPointer))
    return false;

  if (Ptr.isRoot() || !Ptr.isUnknownSizeArray() || Ptr.isDummy()) {
    S.Stk.push<Pointer>(Ptr.atIndex(0));
    return true;
  }

  const SourceInfo &E = S.Current->getSource(OpPC);
  S.FFDiag(E, diag::note_constexpr_unsupported_unsized_array);

  return false;
}

inline bool CallVar(InterpState &S, CodePtr OpPC, const Function *Func,
                    uint32_t VarArgSize) {
  if (Func->hasThisPointer()) {
    size_t ArgSize = Func->getArgSize() + VarArgSize;
    size_t ThisOffset = ArgSize - (Func->hasRVO() ? primSize(PT_Ptr) : 0);
    const Pointer &ThisPtr = S.Stk.peek<Pointer>(ThisOffset);

    // If the current function is a lambda static invoker and
    // the function we're about to call is a lambda call operator,
    // skip the CheckInvoke, since the ThisPtr is a null pointer
    // anyway.
    if (!(S.Current->getFunction() &&
          S.Current->getFunction()->isLambdaStaticInvoker() &&
          Func->isLambdaCallOperator())) {
      if (!CheckInvoke(S, OpPC, ThisPtr))
        return false;
    }

    if (S.checkingPotentialConstantExpression())
      return false;
  }

  if (!CheckCallable(S, OpPC, Func))
    return false;

  if (!CheckCallDepth(S, OpPC))
    return false;

  auto NewFrame = std::make_unique<InterpFrame>(S, Func, OpPC, VarArgSize);
  InterpFrame *FrameBefore = S.Current;
  S.Current = NewFrame.get();

  APValue CallResult;
  // Note that we cannot assert(CallResult.hasValue()) here since
  // Ret() above only sets the APValue if the curent frame doesn't
  // have a caller set.
  if (Interpret(S, CallResult)) {
    NewFrame.release(); // Frame was delete'd already.
    assert(S.Current == FrameBefore);
    return true;
  }

  // Interpreting the function failed somehow. Reset to
  // previous state.
  S.Current = FrameBefore;
  return false;

  return false;
}

inline bool Call(InterpState &S, CodePtr OpPC, const Function *Func,
                 uint32_t VarArgSize) {
  if (Func->hasThisPointer()) {
    size_t ArgSize = Func->getArgSize() + VarArgSize;
    size_t ThisOffset = ArgSize - (Func->hasRVO() ? primSize(PT_Ptr) : 0);

    const Pointer &ThisPtr = S.Stk.peek<Pointer>(ThisOffset);

    // If the current function is a lambda static invoker and
    // the function we're about to call is a lambda call operator,
    // skip the CheckInvoke, since the ThisPtr is a null pointer
    // anyway.
    if (!(S.Current->getFunction() &&
          S.Current->getFunction()->isLambdaStaticInvoker() &&
          Func->isLambdaCallOperator())) {
      if (!CheckInvoke(S, OpPC, ThisPtr))
        return false;
    }
  }

  if (!CheckCallable(S, OpPC, Func))
    return false;

  if (Func->hasThisPointer() && S.checkingPotentialConstantExpression())
    return false;

  if (!CheckCallDepth(S, OpPC))
    return false;

  auto NewFrame = std::make_unique<InterpFrame>(S, Func, OpPC, VarArgSize);
  InterpFrame *FrameBefore = S.Current;
  S.Current = NewFrame.get();

  APValue CallResult;
  // Note that we cannot assert(CallResult.hasValue()) here since
  // Ret() above only sets the APValue if the curent frame doesn't
  // have a caller set.
  if (Interpret(S, CallResult)) {
    NewFrame.release(); // Frame was delete'd already.
    assert(S.Current == FrameBefore);
    return true;
  }

  // Interpreting the function failed somehow. Reset to
  // previous state.
  S.Current = FrameBefore;
  return false;
}

inline bool CallVirt(InterpState &S, CodePtr OpPC, const Function *Func,
                     uint32_t VarArgSize) {
  assert(Func->hasThisPointer());
  assert(Func->isVirtual());
  size_t ArgSize = Func->getArgSize() + VarArgSize;
  size_t ThisOffset = ArgSize - (Func->hasRVO() ? primSize(PT_Ptr) : 0);
  Pointer &ThisPtr = S.Stk.peek<Pointer>(ThisOffset);

  QualType DynamicType = ThisPtr.getDeclDesc()->getType();
  const CXXRecordDecl *DynamicDecl;
  if (DynamicType->isPointerType() || DynamicType->isReferenceType())
    DynamicDecl = DynamicType->getPointeeCXXRecordDecl();
  else
    DynamicDecl = ThisPtr.getDeclDesc()->getType()->getAsCXXRecordDecl();
  const auto *StaticDecl = cast<CXXRecordDecl>(Func->getParentDecl());
  const auto *InitialFunction = cast<CXXMethodDecl>(Func->getDecl());
  const CXXMethodDecl *Overrider = S.getContext().getOverridingFunction(
      DynamicDecl, StaticDecl, InitialFunction);

  if (Overrider != InitialFunction) {
    // DR1872: An instantiated virtual constexpr function can't be called in a
    // constant expression (prior to C++20). We can still constant-fold such a
    // call.
    if (!S.getLangOpts().CPlusPlus20 && Overrider->isVirtual()) {
      const Expr *E = S.Current->getExpr(OpPC);
      S.CCEDiag(E, diag::note_constexpr_virtual_call) << E->getSourceRange();
    }

    Func = S.getContext().getOrCreateFunction(Overrider);

    const CXXRecordDecl *ThisFieldDecl =
        ThisPtr.getFieldDesc()->getType()->getAsCXXRecordDecl();
    if (Func->getParentDecl()->isDerivedFrom(ThisFieldDecl)) {
      // If the function we call is further DOWN the hierarchy than the
      // FieldDesc of our pointer, just get the DeclDesc instead, which
      // is the furthest we might go up in the hierarchy.
      ThisPtr = ThisPtr.getDeclPtr();
    }
  }

  return Call(S, OpPC, Func, VarArgSize);
}

inline bool CallBI(InterpState &S, CodePtr &PC, const Function *Func,
                   const CallExpr *CE) {
  auto NewFrame = std::make_unique<InterpFrame>(S, Func, PC);

  InterpFrame *FrameBefore = S.Current;
  S.Current = NewFrame.get();

  if (InterpretBuiltin(S, PC, Func, CE)) {
    NewFrame.release();
    return true;
  }
  S.Current = FrameBefore;
  return false;
}

inline bool CallPtr(InterpState &S, CodePtr OpPC, uint32_t ArgSize,
                    const CallExpr *CE) {
  const FunctionPointer &FuncPtr = S.Stk.pop<FunctionPointer>();

  const Function *F = FuncPtr.getFunction();
  if (!F) {
    const Expr *E = S.Current->getExpr(OpPC);
    S.FFDiag(E, diag::note_constexpr_null_callee)
        << const_cast<Expr *>(E) << E->getSourceRange();
    return false;
  }

  if (!FuncPtr.isValid())
    return false;

  assert(F);

  // This happens when the call expression has been cast to
  // something else, but we don't support that.
  if (S.Ctx.classify(F->getDecl()->getReturnType()) !=
      S.Ctx.classify(CE->getType()))
    return false;

  // Check argument nullability state.
  if (F->hasNonNullAttr()) {
    if (!CheckNonNullArgs(S, OpPC, F, CE, ArgSize))
      return false;
  }

  assert(ArgSize >= F->getWrittenArgSize());
  uint32_t VarArgSize = ArgSize - F->getWrittenArgSize();

  // We need to do this explicitly here since we don't have the necessary
  // information to do it automatically.
  if (F->isThisPointerExplicit())
    VarArgSize -= align(primSize(PT_Ptr));

  if (F->isVirtual())
    return CallVirt(S, OpPC, F, VarArgSize);

  return Call(S, OpPC, F, VarArgSize);
}

inline bool GetFnPtr(InterpState &S, CodePtr OpPC, const Function *Func) {
  assert(Func);
  S.Stk.push<FunctionPointer>(Func);
  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool GetIntPtr(InterpState &S, CodePtr OpPC, const Descriptor *Desc) {
  const T &IntVal = S.Stk.pop<T>();

  S.Stk.push<Pointer>(static_cast<uint64_t>(IntVal), Desc);
  return true;
}

inline bool GetMemberPtr(InterpState &S, CodePtr OpPC, const Decl *D) {
  S.Stk.push<MemberPointer>(D);
  return true;
}

inline bool GetMemberPtrBase(InterpState &S, CodePtr OpPC) {
  const auto &MP = S.Stk.pop<MemberPointer>();

  S.Stk.push<Pointer>(MP.getBase());
  return true;
}

inline bool GetMemberPtrDecl(InterpState &S, CodePtr OpPC) {
  const auto &MP = S.Stk.pop<MemberPointer>();

  const auto *FD = cast<FunctionDecl>(MP.getDecl());
  const auto *Func = S.getContext().getOrCreateFunction(FD);

  S.Stk.push<FunctionPointer>(Func);
  return true;
}

/// Just emit a diagnostic. The expression that caused emission of this
/// op is not valid in a constant context.
inline bool Invalid(InterpState &S, CodePtr OpPC) {
  const SourceLocation &Loc = S.Current->getLocation(OpPC);
  S.FFDiag(Loc, diag::note_invalid_subexpr_in_const_expr)
      << S.Current->getRange(OpPC);
  return false;
}

inline bool Unsupported(InterpState &S, CodePtr OpPC) {
  const SourceLocation &Loc = S.Current->getLocation(OpPC);
  S.FFDiag(Loc, diag::note_constexpr_stmt_expr_unsupported)
      << S.Current->getRange(OpPC);
  return false;
}

/// Do nothing and just abort execution.
inline bool Error(InterpState &S, CodePtr OpPC) { return false; }

/// Same here, but only for casts.
inline bool InvalidCast(InterpState &S, CodePtr OpPC, CastKind Kind) {
  const SourceLocation &Loc = S.Current->getLocation(OpPC);

  // FIXME: Support diagnosing other invalid cast kinds.
  if (Kind == CastKind::Reinterpret)
    S.FFDiag(Loc, diag::note_constexpr_invalid_cast)
        << static_cast<unsigned>(Kind) << S.Current->getRange(OpPC);
  return false;
}

inline bool InvalidDeclRef(InterpState &S, CodePtr OpPC,
                           const DeclRefExpr *DR) {
  assert(DR);
  return CheckDeclRef(S, OpPC, DR);
}

inline bool SizelessVectorElementSize(InterpState &S, CodePtr OpPC) {
  if (S.inConstantContext()) {
    const SourceRange &ArgRange = S.Current->getRange(OpPC);
    const Expr *E = S.Current->getExpr(OpPC);
    S.CCEDiag(E, diag::note_constexpr_non_const_vectorelements) << ArgRange;
  }
  return false;
}

inline bool Assume(InterpState &S, CodePtr OpPC) {
  const auto Val = S.Stk.pop<Boolean>();

  if (Val)
    return true;

  // Else, diagnose.
  const SourceLocation &Loc = S.Current->getLocation(OpPC);
  S.CCEDiag(Loc, diag::note_constexpr_assumption_failed);
  return false;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool OffsetOf(InterpState &S, CodePtr OpPC, const OffsetOfExpr *E) {
  llvm::SmallVector<int64_t> ArrayIndices;
  for (size_t I = 0; I != E->getNumExpressions(); ++I)
    ArrayIndices.emplace_back(S.Stk.pop<int64_t>());

  int64_t Result;
  if (!InterpretOffsetOf(S, OpPC, E, ArrayIndices, Result))
    return false;

  S.Stk.push<T>(T::from(Result));

  return true;
}

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool CheckNonNullArg(InterpState &S, CodePtr OpPC) {
  const T &Arg = S.Stk.peek<T>();
  if (!Arg.isZero())
    return true;

  const SourceLocation &Loc = S.Current->getLocation(OpPC);
  S.CCEDiag(Loc, diag::note_non_null_attribute_failed);

  return false;
}

void diagnoseEnumValue(InterpState &S, CodePtr OpPC, const EnumDecl *ED,
                       const APSInt &Value);

template <PrimType Name, class T = typename PrimConv<Name>::T>
inline bool CheckEnumValue(InterpState &S, CodePtr OpPC, const EnumDecl *ED) {
  assert(ED);
  assert(!ED->isFixed());
  const APSInt Val = S.Stk.peek<T>().toAPSInt();

  if (S.inConstantContext())
    diagnoseEnumValue(S, OpPC, ED, Val);
  return true;
}

/// OldPtr -> Integer -> NewPtr.
template <PrimType TIn, PrimType TOut>
inline bool DecayPtr(InterpState &S, CodePtr OpPC) {
  static_assert(isPtrType(TIn) && isPtrType(TOut));
  using FromT = typename PrimConv<TIn>::T;
  using ToT = typename PrimConv<TOut>::T;

  const FromT &OldPtr = S.Stk.pop<FromT>();
  S.Stk.push<ToT>(ToT(OldPtr.getIntegerRepresentation(), nullptr));
  return true;
}

inline bool CheckDecl(InterpState &S, CodePtr OpPC, const VarDecl *VD) {
  // An expression E is a core constant expression unless the evaluation of E
  // would evaluate one of the following: [C++23] - a control flow that passes
  // through a declaration of a variable with static or thread storage duration
  // unless that variable is usable in constant expressions.
  assert(VD->isLocalVarDecl() &&
         VD->isStaticLocal()); // Checked before emitting this.

  if (VD == S.EvaluatingDecl)
    return true;

  if (!VD->isUsableInConstantExpressions(S.getCtx())) {
    S.CCEDiag(VD->getLocation(), diag::note_constexpr_static_local)
        << (VD->getTSCSpec() == TSCS_unspecified ? 0 : 1) << VD;
    return false;
  }
  return true;
}

inline bool Alloc(InterpState &S, CodePtr OpPC, const Descriptor *Desc) {
  assert(Desc);

  if (!CheckDynamicMemoryAllocation(S, OpPC))
    return false;

  DynamicAllocator &Allocator = S.getAllocator();
  Block *B = Allocator.allocate(Desc, S.Ctx.getEvalID());
  assert(B);

  S.Stk.push<Pointer>(B, sizeof(InlineDescriptor));

  return true;
}

template <PrimType Name, class SizeT = typename PrimConv<Name>::T>
inline bool AllocN(InterpState &S, CodePtr OpPC, PrimType T, const Expr *Source,
                   bool IsNoThrow) {
  if (!CheckDynamicMemoryAllocation(S, OpPC))
    return false;

  SizeT NumElements = S.Stk.pop<SizeT>();
  if (!CheckArraySize(S, OpPC, &NumElements, primSize(T), IsNoThrow)) {
    if (!IsNoThrow)
      return false;

    // If this failed and is nothrow, just return a null ptr.
    S.Stk.push<Pointer>(0, nullptr);
    return true;
  }

  DynamicAllocator &Allocator = S.getAllocator();
  Block *B = Allocator.allocate(Source, T, static_cast<size_t>(NumElements),
                                S.Ctx.getEvalID());
  assert(B);
  S.Stk.push<Pointer>(B, sizeof(InlineDescriptor));

  return true;
}

template <PrimType Name, class SizeT = typename PrimConv<Name>::T>
inline bool AllocCN(InterpState &S, CodePtr OpPC, const Descriptor *ElementDesc,
                    bool IsNoThrow) {
  if (!CheckDynamicMemoryAllocation(S, OpPC))
    return false;

  SizeT NumElements = S.Stk.pop<SizeT>();
  if (!CheckArraySize(S, OpPC, &NumElements, ElementDesc->getSize(),
                      IsNoThrow)) {
    if (!IsNoThrow)
      return false;

    // If this failed and is nothrow, just return a null ptr.
    S.Stk.push<Pointer>(0, ElementDesc);
    return true;
  }

  DynamicAllocator &Allocator = S.getAllocator();
  Block *B = Allocator.allocate(ElementDesc, static_cast<size_t>(NumElements),
                                S.Ctx.getEvalID());
  assert(B);

  S.Stk.push<Pointer>(B, sizeof(InlineDescriptor));

  return true;
}

bool RunDestructors(InterpState &S, CodePtr OpPC, const Block *B);
static inline bool Free(InterpState &S, CodePtr OpPC, bool DeleteIsArrayForm) {
  if (!CheckDynamicMemoryAllocation(S, OpPC))
    return false;

  const Expr *Source = nullptr;
  const Block *BlockToDelete = nullptr;
  {
    // Extra scope for this so the block doesn't have this pointer
    // pointing to it when we destroy it.
    const Pointer &Ptr = S.Stk.pop<Pointer>();

    // Deleteing nullptr is always fine.
    if (Ptr.isZero())
      return true;

    if (!Ptr.isRoot() || Ptr.isOnePastEnd() || Ptr.isArrayElement()) {
      const SourceInfo &Loc = S.Current->getSource(OpPC);
      S.FFDiag(Loc, diag::note_constexpr_delete_subobject)
          << Ptr.toDiagnosticString(S.getCtx()) << Ptr.isOnePastEnd();
      return false;
    }

    Source = Ptr.getDeclDesc()->asExpr();
    BlockToDelete = Ptr.block();

    if (!CheckDeleteSource(S, OpPC, Source, Ptr))
      return false;
  }
  assert(Source);
  assert(BlockToDelete);

  // Invoke destructors before deallocating the memory.
  if (!RunDestructors(S, OpPC, BlockToDelete))
    return false;

  DynamicAllocator &Allocator = S.getAllocator();
  bool WasArrayAlloc = Allocator.isArrayAllocation(Source);
  const Descriptor *BlockDesc = BlockToDelete->getDescriptor();

  if (!Allocator.deallocate(Source, BlockToDelete, S)) {
    // Nothing has been deallocated, this must be a double-delete.
    const SourceInfo &Loc = S.Current->getSource(OpPC);
    S.FFDiag(Loc, diag::note_constexpr_double_delete);
    return false;
  }
  return CheckNewDeleteForms(S, OpPC, WasArrayAlloc, DeleteIsArrayForm,
                             BlockDesc, Source);
}

//===----------------------------------------------------------------------===//
// Read opcode arguments
//===----------------------------------------------------------------------===//

template <typename T> inline T ReadArg(InterpState &S, CodePtr &OpPC) {
  if constexpr (std::is_pointer<T>::value) {
    uint32_t ID = OpPC.read<uint32_t>();
    return reinterpret_cast<T>(S.P.getNativePointer(ID));
  } else {
    return OpPC.read<T>();
  }
}

template <> inline Floating ReadArg<Floating>(InterpState &S, CodePtr &OpPC) {
  Floating F = Floating::deserialize(*OpPC);
  OpPC += align(F.bytesToSerialize());
  return F;
}

template <>
inline IntegralAP<false> ReadArg<IntegralAP<false>>(InterpState &S,
                                                    CodePtr &OpPC) {
  IntegralAP<false> I = IntegralAP<false>::deserialize(*OpPC);
  OpPC += align(I.bytesToSerialize());
  return I;
}

template <>
inline IntegralAP<true> ReadArg<IntegralAP<true>>(InterpState &S,
                                                  CodePtr &OpPC) {
  IntegralAP<true> I = IntegralAP<true>::deserialize(*OpPC);
  OpPC += align(I.bytesToSerialize());
  return I;
}

} // namespace interp
} // namespace clang

#endif
