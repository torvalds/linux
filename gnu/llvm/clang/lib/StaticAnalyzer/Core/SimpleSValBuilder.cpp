// SimpleSValBuilder.cpp - A basic SValBuilder -----------------------*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines SimpleSValBuilder, a basic implementation of SValBuilder.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValVisitor.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
class SimpleSValBuilder : public SValBuilder {

  // Query the constraint manager whether the SVal has only one possible
  // (integer) value. If that is the case, the value is returned. Otherwise,
  // returns NULL.
  // This is an implementation detail. Checkers should use `getKnownValue()`
  // instead.
  static const llvm::APSInt *getConstValue(ProgramStateRef state, SVal V);

  // Helper function that returns the value stored in a nonloc::ConcreteInt or
  // loc::ConcreteInt.
  static const llvm::APSInt *getConcreteValue(SVal V);

  // With one `simplifySValOnce` call, a compound symbols might collapse to
  // simpler symbol tree that is still possible to further simplify. Thus, we
  // do the simplification on a new symbol tree until we reach the simplest
  // form, i.e. the fixpoint.
  // Consider the following symbol `(b * b) * b * b` which has this tree:
  //       *
  //      / \
  //     *   b
  //    /  \
  //   /    b
  // (b * b)
  // Now, if the `b * b == 1` new constraint is added then during the first
  // iteration we have the following transformations:
  //       *                  *
  //      / \                / \
  //     *   b     -->      b   b
  //    /  \
  //   /    b
  //  1
  // We need another iteration to reach the final result `1`.
  SVal simplifyUntilFixpoint(ProgramStateRef State, SVal Val);

  // Recursively descends into symbolic expressions and replaces symbols
  // with their known values (in the sense of the getConstValue() method).
  // We traverse the symbol tree and query the constraint values for the
  // sub-trees and if a value is a constant we do the constant folding.
  SVal simplifySValOnce(ProgramStateRef State, SVal V);

public:
  SimpleSValBuilder(llvm::BumpPtrAllocator &alloc, ASTContext &context,
                    ProgramStateManager &stateMgr)
      : SValBuilder(alloc, context, stateMgr) {}
  ~SimpleSValBuilder() override {}

  SVal evalBinOpNN(ProgramStateRef state, BinaryOperator::Opcode op,
                   NonLoc lhs, NonLoc rhs, QualType resultTy) override;
  SVal evalBinOpLL(ProgramStateRef state, BinaryOperator::Opcode op,
                   Loc lhs, Loc rhs, QualType resultTy) override;
  SVal evalBinOpLN(ProgramStateRef state, BinaryOperator::Opcode op,
                   Loc lhs, NonLoc rhs, QualType resultTy) override;

  /// Evaluates a given SVal by recursively evaluating and
  /// simplifying the children SVals. If the SVal has only one possible
  /// (integer) value, that value is returned. Otherwise, returns NULL.
  const llvm::APSInt *getKnownValue(ProgramStateRef state, SVal V) override;

  /// Evaluates a given SVal by recursively evaluating and simplifying the
  /// children SVals, then returns its minimal possible (integer) value. If the
  /// constraint manager cannot provide a meaningful answer, this returns NULL.
  const llvm::APSInt *getMinValue(ProgramStateRef state, SVal V) override;

  /// Evaluates a given SVal by recursively evaluating and simplifying the
  /// children SVals, then returns its maximal possible (integer) value. If the
  /// constraint manager cannot provide a meaningful answer, this returns NULL.
  const llvm::APSInt *getMaxValue(ProgramStateRef state, SVal V) override;

  SVal simplifySVal(ProgramStateRef State, SVal V) override;

  SVal MakeSymIntVal(const SymExpr *LHS, BinaryOperator::Opcode op,
                     const llvm::APSInt &RHS, QualType resultTy);
};
} // end anonymous namespace

SValBuilder *ento::createSimpleSValBuilder(llvm::BumpPtrAllocator &alloc,
                                           ASTContext &context,
                                           ProgramStateManager &stateMgr) {
  return new SimpleSValBuilder(alloc, context, stateMgr);
}

// Checks if the negation the value and flipping sign preserve
// the semantics on the operation in the resultType
static bool isNegationValuePreserving(const llvm::APSInt &Value,
                                      APSIntType ResultType) {
  const unsigned ValueBits = Value.getSignificantBits();
  if (ValueBits == ResultType.getBitWidth()) {
    // The value is the lowest negative value that is representable
    // in signed integer with bitWith of result type. The
    // negation is representable if resultType is unsigned.
    return ResultType.isUnsigned();
  }

  // If resultType bitWith is higher that number of bits required
  // to represent RHS, the sign flip produce same value.
  return ValueBits < ResultType.getBitWidth();
}

//===----------------------------------------------------------------------===//
// Transfer function for binary operators.
//===----------------------------------------------------------------------===//

SVal SimpleSValBuilder::MakeSymIntVal(const SymExpr *LHS,
                                    BinaryOperator::Opcode op,
                                    const llvm::APSInt &RHS,
                                    QualType resultTy) {
  bool isIdempotent = false;

  // Check for a few special cases with known reductions first.
  switch (op) {
  default:
    // We can't reduce this case; just treat it normally.
    break;
  case BO_Mul:
    // a*0 and a*1
    if (RHS == 0)
      return makeIntVal(0, resultTy);
    else if (RHS == 1)
      isIdempotent = true;
    break;
  case BO_Div:
    // a/0 and a/1
    if (RHS == 0)
      // This is also handled elsewhere.
      return UndefinedVal();
    else if (RHS == 1)
      isIdempotent = true;
    break;
  case BO_Rem:
    // a%0 and a%1
    if (RHS == 0)
      // This is also handled elsewhere.
      return UndefinedVal();
    else if (RHS == 1)
      return makeIntVal(0, resultTy);
    break;
  case BO_Add:
  case BO_Sub:
  case BO_Shl:
  case BO_Shr:
  case BO_Xor:
    // a+0, a-0, a<<0, a>>0, a^0
    if (RHS == 0)
      isIdempotent = true;
    break;
  case BO_And:
    // a&0 and a&(~0)
    if (RHS == 0)
      return makeIntVal(0, resultTy);
    else if (RHS.isAllOnes())
      isIdempotent = true;
    break;
  case BO_Or:
    // a|0 and a|(~0)
    if (RHS == 0)
      isIdempotent = true;
    else if (RHS.isAllOnes()) {
      const llvm::APSInt &Result = BasicVals.Convert(resultTy, RHS);
      return nonloc::ConcreteInt(Result);
    }
    break;
  }

  // Idempotent ops (like a*1) can still change the type of an expression.
  // Wrap the LHS up in a NonLoc again and let evalCast do the
  // dirty work.
  if (isIdempotent)
    return evalCast(nonloc::SymbolVal(LHS), resultTy, QualType{});

  // If we reach this point, the expression cannot be simplified.
  // Make a SymbolVal for the entire expression, after converting the RHS.
  const llvm::APSInt *ConvertedRHS = &RHS;
  if (BinaryOperator::isComparisonOp(op)) {
    // We're looking for a type big enough to compare the symbolic value
    // with the given constant.
    // FIXME: This is an approximation of Sema::UsualArithmeticConversions.
    ASTContext &Ctx = getContext();
    QualType SymbolType = LHS->getType();
    uint64_t ValWidth = RHS.getBitWidth();
    uint64_t TypeWidth = Ctx.getTypeSize(SymbolType);

    if (ValWidth < TypeWidth) {
      // If the value is too small, extend it.
      ConvertedRHS = &BasicVals.Convert(SymbolType, RHS);
    } else if (ValWidth == TypeWidth) {
      // If the value is signed but the symbol is unsigned, do the comparison
      // in unsigned space. [C99 6.3.1.8]
      // (For the opposite case, the value is already unsigned.)
      if (RHS.isSigned() && !SymbolType->isSignedIntegerOrEnumerationType())
        ConvertedRHS = &BasicVals.Convert(SymbolType, RHS);
    }
  } else if (BinaryOperator::isAdditiveOp(op) && RHS.isNegative()) {
    // Change a+(-N) into a-N, and a-(-N) into a+N
    // Adjust addition/subtraction of negative value, to
    // subtraction/addition of the negated value.
    APSIntType resultIntTy = BasicVals.getAPSIntType(resultTy);
    if (isNegationValuePreserving(RHS, resultIntTy)) {
      ConvertedRHS = &BasicVals.getValue(-resultIntTy.convert(RHS));
      op = (op == BO_Add) ? BO_Sub : BO_Add;
    } else {
      ConvertedRHS = &BasicVals.Convert(resultTy, RHS);
    }
  } else
    ConvertedRHS = &BasicVals.Convert(resultTy, RHS);

  return makeNonLoc(LHS, op, *ConvertedRHS, resultTy);
}

// See if Sym is known to be a relation Rel with Bound.
static bool isInRelation(BinaryOperator::Opcode Rel, SymbolRef Sym,
                         llvm::APSInt Bound, ProgramStateRef State) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  SVal Result =
      SVB.evalBinOpNN(State, Rel, nonloc::SymbolVal(Sym),
                      nonloc::ConcreteInt(Bound), SVB.getConditionType());
  if (auto DV = Result.getAs<DefinedSVal>()) {
    return !State->assume(*DV, false);
  }
  return false;
}

// See if Sym is known to be within [min/4, max/4], where min and max
// are the bounds of the symbol's integral type. With such symbols,
// some manipulations can be performed without the risk of overflow.
// assume() doesn't cause infinite recursion because we should be dealing
// with simpler symbols on every recursive call.
static bool isWithinConstantOverflowBounds(SymbolRef Sym,
                                           ProgramStateRef State) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  BasicValueFactory &BV = SVB.getBasicValueFactory();

  QualType T = Sym->getType();
  assert(T->isSignedIntegerOrEnumerationType() &&
         "This only works with signed integers!");
  APSIntType AT = BV.getAPSIntType(T);

  llvm::APSInt Max = AT.getMaxValue() / AT.getValue(4), Min = -Max;
  return isInRelation(BO_LE, Sym, Max, State) &&
         isInRelation(BO_GE, Sym, Min, State);
}

// Same for the concrete integers: see if I is within [min/4, max/4].
static bool isWithinConstantOverflowBounds(llvm::APSInt I) {
  APSIntType AT(I);
  assert(!AT.isUnsigned() &&
         "This only works with signed integers!");

  llvm::APSInt Max = AT.getMaxValue() / AT.getValue(4), Min = -Max;
  return (I <= Max) && (I >= -Max);
}

static std::pair<SymbolRef, llvm::APSInt>
decomposeSymbol(SymbolRef Sym, BasicValueFactory &BV) {
  if (const auto *SymInt = dyn_cast<SymIntExpr>(Sym))
    if (BinaryOperator::isAdditiveOp(SymInt->getOpcode()))
      return std::make_pair(SymInt->getLHS(),
                            (SymInt->getOpcode() == BO_Add) ?
                            (SymInt->getRHS()) :
                            (-SymInt->getRHS()));

  // Fail to decompose: "reduce" the problem to the "$x + 0" case.
  return std::make_pair(Sym, BV.getValue(0, Sym->getType()));
}

// Simplify "(LSym + LInt) Op (RSym + RInt)" assuming all values are of the
// same signed integral type and no overflows occur (which should be checked
// by the caller).
static NonLoc doRearrangeUnchecked(ProgramStateRef State,
                                   BinaryOperator::Opcode Op,
                                   SymbolRef LSym, llvm::APSInt LInt,
                                   SymbolRef RSym, llvm::APSInt RInt) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  BasicValueFactory &BV = SVB.getBasicValueFactory();
  SymbolManager &SymMgr = SVB.getSymbolManager();

  QualType SymTy = LSym->getType();
  assert(SymTy == RSym->getType() &&
         "Symbols are not of the same type!");
  assert(APSIntType(LInt) == BV.getAPSIntType(SymTy) &&
         "Integers are not of the same type as symbols!");
  assert(APSIntType(RInt) == BV.getAPSIntType(SymTy) &&
         "Integers are not of the same type as symbols!");

  QualType ResultTy;
  if (BinaryOperator::isComparisonOp(Op))
    ResultTy = SVB.getConditionType();
  else if (BinaryOperator::isAdditiveOp(Op))
    ResultTy = SymTy;
  else
    llvm_unreachable("Operation not suitable for unchecked rearrangement!");

  if (LSym == RSym)
    return SVB.evalBinOpNN(State, Op, nonloc::ConcreteInt(LInt),
                           nonloc::ConcreteInt(RInt), ResultTy)
        .castAs<NonLoc>();

  SymbolRef ResultSym = nullptr;
  BinaryOperator::Opcode ResultOp;
  llvm::APSInt ResultInt;
  if (BinaryOperator::isComparisonOp(Op)) {
    // Prefer comparing to a non-negative number.
    // FIXME: Maybe it'd be better to have consistency in
    // "$x - $y" vs. "$y - $x" because those are solver's keys.
    if (LInt > RInt) {
      ResultSym = SymMgr.getSymSymExpr(RSym, BO_Sub, LSym, SymTy);
      ResultOp = BinaryOperator::reverseComparisonOp(Op);
      ResultInt = LInt - RInt; // Opposite order!
    } else {
      ResultSym = SymMgr.getSymSymExpr(LSym, BO_Sub, RSym, SymTy);
      ResultOp = Op;
      ResultInt = RInt - LInt; // Opposite order!
    }
  } else {
    ResultSym = SymMgr.getSymSymExpr(LSym, Op, RSym, SymTy);
    ResultInt = (Op == BO_Add) ? (LInt + RInt) : (LInt - RInt);
    ResultOp = BO_Add;
    // Bring back the cosmetic difference.
    if (ResultInt < 0) {
      ResultInt = -ResultInt;
      ResultOp = BO_Sub;
    } else if (ResultInt == 0) {
      // Shortcut: Simplify "$x + 0" to "$x".
      return nonloc::SymbolVal(ResultSym);
    }
  }
  const llvm::APSInt &PersistentResultInt = BV.getValue(ResultInt);
  return nonloc::SymbolVal(
      SymMgr.getSymIntExpr(ResultSym, ResultOp, PersistentResultInt, ResultTy));
}

// Rearrange if symbol type matches the result type and if the operator is a
// comparison operator, both symbol and constant must be within constant
// overflow bounds.
static bool shouldRearrange(ProgramStateRef State, BinaryOperator::Opcode Op,
                            SymbolRef Sym, llvm::APSInt Int, QualType Ty) {
  return Sym->getType() == Ty &&
    (!BinaryOperator::isComparisonOp(Op) ||
     (isWithinConstantOverflowBounds(Sym, State) &&
      isWithinConstantOverflowBounds(Int)));
}

static std::optional<NonLoc> tryRearrange(ProgramStateRef State,
                                          BinaryOperator::Opcode Op, NonLoc Lhs,
                                          NonLoc Rhs, QualType ResultTy) {
  ProgramStateManager &StateMgr = State->getStateManager();
  SValBuilder &SVB = StateMgr.getSValBuilder();

  // We expect everything to be of the same type - this type.
  QualType SingleTy;

  // FIXME: After putting complexity threshold to the symbols we can always
  //        rearrange additive operations but rearrange comparisons only if
  //        option is set.
  if (!SVB.getAnalyzerOptions().ShouldAggressivelySimplifyBinaryOperation)
    return std::nullopt;

  SymbolRef LSym = Lhs.getAsSymbol();
  if (!LSym)
    return std::nullopt;

  if (BinaryOperator::isComparisonOp(Op)) {
    SingleTy = LSym->getType();
    if (ResultTy != SVB.getConditionType())
      return std::nullopt;
    // Initialize SingleTy later with a symbol's type.
  } else if (BinaryOperator::isAdditiveOp(Op)) {
    SingleTy = ResultTy;
    if (LSym->getType() != SingleTy)
      return std::nullopt;
  } else {
    // Don't rearrange other operations.
    return std::nullopt;
  }

  assert(!SingleTy.isNull() && "We should have figured out the type by now!");

  // Rearrange signed symbolic expressions only
  if (!SingleTy->isSignedIntegerOrEnumerationType())
    return std::nullopt;

  SymbolRef RSym = Rhs.getAsSymbol();
  if (!RSym || RSym->getType() != SingleTy)
    return std::nullopt;

  BasicValueFactory &BV = State->getBasicVals();
  llvm::APSInt LInt, RInt;
  std::tie(LSym, LInt) = decomposeSymbol(LSym, BV);
  std::tie(RSym, RInt) = decomposeSymbol(RSym, BV);
  if (!shouldRearrange(State, Op, LSym, LInt, SingleTy) ||
      !shouldRearrange(State, Op, RSym, RInt, SingleTy))
    return std::nullopt;

  // We know that no overflows can occur anymore.
  return doRearrangeUnchecked(State, Op, LSym, LInt, RSym, RInt);
}

SVal SimpleSValBuilder::evalBinOpNN(ProgramStateRef state,
                                  BinaryOperator::Opcode op,
                                  NonLoc lhs, NonLoc rhs,
                                  QualType resultTy)  {
  NonLoc InputLHS = lhs;
  NonLoc InputRHS = rhs;

  // Constraints may have changed since the creation of a bound SVal. Check if
  // the values can be simplified based on those new constraints.
  SVal simplifiedLhs = simplifySVal(state, lhs);
  SVal simplifiedRhs = simplifySVal(state, rhs);
  if (auto simplifiedLhsAsNonLoc = simplifiedLhs.getAs<NonLoc>())
    lhs = *simplifiedLhsAsNonLoc;
  if (auto simplifiedRhsAsNonLoc = simplifiedRhs.getAs<NonLoc>())
    rhs = *simplifiedRhsAsNonLoc;

  // Handle trivial case where left-side and right-side are the same.
  if (lhs == rhs)
    switch (op) {
      default:
        break;
      case BO_EQ:
      case BO_LE:
      case BO_GE:
        return makeTruthVal(true, resultTy);
      case BO_LT:
      case BO_GT:
      case BO_NE:
        return makeTruthVal(false, resultTy);
      case BO_Xor:
      case BO_Sub:
        if (resultTy->isIntegralOrEnumerationType())
          return makeIntVal(0, resultTy);
        return evalCast(makeIntVal(0, /*isUnsigned=*/false), resultTy,
                        QualType{});
      case BO_Or:
      case BO_And:
        return evalCast(lhs, resultTy, QualType{});
    }

  while (true) {
    switch (lhs.getKind()) {
    default:
      return makeSymExprValNN(op, lhs, rhs, resultTy);
    case nonloc::PointerToMemberKind: {
      assert(rhs.getKind() == nonloc::PointerToMemberKind &&
             "Both SVals should have pointer-to-member-type");
      auto LPTM = lhs.castAs<nonloc::PointerToMember>(),
           RPTM = rhs.castAs<nonloc::PointerToMember>();
      auto LPTMD = LPTM.getPTMData(), RPTMD = RPTM.getPTMData();
      switch (op) {
        case BO_EQ:
          return makeTruthVal(LPTMD == RPTMD, resultTy);
        case BO_NE:
          return makeTruthVal(LPTMD != RPTMD, resultTy);
        default:
          return UnknownVal();
      }
    }
    case nonloc::LocAsIntegerKind: {
      Loc lhsL = lhs.castAs<nonloc::LocAsInteger>().getLoc();
      switch (rhs.getKind()) {
      case nonloc::LocAsIntegerKind:
        // FIXME: at the moment the implementation
        // of modeling "pointers as integers" is not complete.
        if (!BinaryOperator::isComparisonOp(op))
          return UnknownVal();
        return evalBinOpLL(state, op, lhsL,
                           rhs.castAs<nonloc::LocAsInteger>().getLoc(),
                           resultTy);
      case nonloc::ConcreteIntKind: {
        // FIXME: at the moment the implementation
        // of modeling "pointers as integers" is not complete.
        if (!BinaryOperator::isComparisonOp(op))
          return UnknownVal();
        // Transform the integer into a location and compare.
        // FIXME: This only makes sense for comparisons. If we want to, say,
        // add 1 to a LocAsInteger, we'd better unpack the Loc and add to it,
        // then pack it back into a LocAsInteger.
        llvm::APSInt i = rhs.castAs<nonloc::ConcreteInt>().getValue();
        // If the region has a symbolic base, pay attention to the type; it
        // might be coming from a non-default address space. For non-symbolic
        // regions it doesn't matter that much because such comparisons would
        // most likely evaluate to concrete false anyway. FIXME: We might
        // still need to handle the non-comparison case.
        if (SymbolRef lSym = lhs.getAsLocSymbol(true))
          BasicVals.getAPSIntType(lSym->getType()).apply(i);
        else
          BasicVals.getAPSIntType(Context.VoidPtrTy).apply(i);
        return evalBinOpLL(state, op, lhsL, makeLoc(i), resultTy);
      }
        default:
          switch (op) {
            case BO_EQ:
              return makeTruthVal(false, resultTy);
            case BO_NE:
              return makeTruthVal(true, resultTy);
            default:
              // This case also handles pointer arithmetic.
              return makeSymExprValNN(op, InputLHS, InputRHS, resultTy);
          }
        }
    }
    case nonloc::ConcreteIntKind: {
      llvm::APSInt LHSValue = lhs.castAs<nonloc::ConcreteInt>().getValue();

      // If we're dealing with two known constants, just perform the operation.
      if (const llvm::APSInt *KnownRHSValue = getConstValue(state, rhs)) {
        llvm::APSInt RHSValue = *KnownRHSValue;
        if (BinaryOperator::isComparisonOp(op)) {
          // We're looking for a type big enough to compare the two values.
          // FIXME: This is not correct. char + short will result in a promotion
          // to int. Unfortunately we have lost types by this point.
          APSIntType CompareType = std::max(APSIntType(LHSValue),
                                            APSIntType(RHSValue));
          CompareType.apply(LHSValue);
          CompareType.apply(RHSValue);
        } else if (!BinaryOperator::isShiftOp(op)) {
          APSIntType IntType = BasicVals.getAPSIntType(resultTy);
          IntType.apply(LHSValue);
          IntType.apply(RHSValue);
        }

        const llvm::APSInt *Result =
          BasicVals.evalAPSInt(op, LHSValue, RHSValue);
        if (!Result) {
          if (op == BO_Shl || op == BO_Shr) {
            // FIXME: At this point the constant folding claims that the result
            // of a bitwise shift is undefined. However, constant folding
            // relies on the inaccurate type information that is stored in the
            // bit size of APSInt objects, and if we reached this point, then
            // the checker core.BitwiseShift already determined that the shift
            // is valid (in a PreStmt callback, by querying the real type from
            // the AST node).
            // To avoid embarrassing false positives, let's just say that we
            // don't know anything about the result of the shift.
            return UnknownVal();
          }
          return UndefinedVal();
        }

        return nonloc::ConcreteInt(*Result);
      }

      // Swap the left and right sides and flip the operator if doing so
      // allows us to better reason about the expression (this is a form
      // of expression canonicalization).
      // While we're at it, catch some special cases for non-commutative ops.
      switch (op) {
      case BO_LT:
      case BO_GT:
      case BO_LE:
      case BO_GE:
        op = BinaryOperator::reverseComparisonOp(op);
        [[fallthrough]];
      case BO_EQ:
      case BO_NE:
      case BO_Add:
      case BO_Mul:
      case BO_And:
      case BO_Xor:
      case BO_Or:
        std::swap(lhs, rhs);
        continue;
      case BO_Shr:
        // (~0)>>a
        if (LHSValue.isAllOnes() && LHSValue.isSigned())
          return evalCast(lhs, resultTy, QualType{});
        [[fallthrough]];
      case BO_Shl:
        // 0<<a and 0>>a
        if (LHSValue == 0)
          return evalCast(lhs, resultTy, QualType{});
        return makeSymExprValNN(op, InputLHS, InputRHS, resultTy);
      case BO_Div:
        // 0 / x == 0
      case BO_Rem:
        // 0 % x == 0
        if (LHSValue == 0)
          return makeZeroVal(resultTy);
        [[fallthrough]];
      default:
        return makeSymExprValNN(op, InputLHS, InputRHS, resultTy);
      }
    }
    case nonloc::SymbolValKind: {
      // We only handle LHS as simple symbols or SymIntExprs.
      SymbolRef Sym = lhs.castAs<nonloc::SymbolVal>().getSymbol();

      // LHS is a symbolic expression.
      if (const SymIntExpr *symIntExpr = dyn_cast<SymIntExpr>(Sym)) {

        // Is this a logical not? (!x is represented as x == 0.)
        if (op == BO_EQ && rhs.isZeroConstant()) {
          // We know how to negate certain expressions. Simplify them here.

          BinaryOperator::Opcode opc = symIntExpr->getOpcode();
          switch (opc) {
          default:
            // We don't know how to negate this operation.
            // Just handle it as if it were a normal comparison to 0.
            break;
          case BO_LAnd:
          case BO_LOr:
            llvm_unreachable("Logical operators handled by branching logic.");
          case BO_Assign:
          case BO_MulAssign:
          case BO_DivAssign:
          case BO_RemAssign:
          case BO_AddAssign:
          case BO_SubAssign:
          case BO_ShlAssign:
          case BO_ShrAssign:
          case BO_AndAssign:
          case BO_XorAssign:
          case BO_OrAssign:
          case BO_Comma:
            llvm_unreachable("'=' and ',' operators handled by ExprEngine.");
          case BO_PtrMemD:
          case BO_PtrMemI:
            llvm_unreachable("Pointer arithmetic not handled here.");
          case BO_LT:
          case BO_GT:
          case BO_LE:
          case BO_GE:
          case BO_EQ:
          case BO_NE:
            assert(resultTy->isBooleanType() ||
                   resultTy == getConditionType());
            assert(symIntExpr->getType()->isBooleanType() ||
                   getContext().hasSameUnqualifiedType(symIntExpr->getType(),
                                                       getConditionType()));
            // Negate the comparison and make a value.
            opc = BinaryOperator::negateComparisonOp(opc);
            return makeNonLoc(symIntExpr->getLHS(), opc,
                symIntExpr->getRHS(), resultTy);
          }
        }

        // For now, only handle expressions whose RHS is a constant.
        if (const llvm::APSInt *RHSValue = getConstValue(state, rhs)) {
          // If both the LHS and the current expression are additive,
          // fold their constants and try again.
          if (BinaryOperator::isAdditiveOp(op)) {
            BinaryOperator::Opcode lop = symIntExpr->getOpcode();
            if (BinaryOperator::isAdditiveOp(lop)) {
              // Convert the two constants to a common type, then combine them.

              // resultTy may not be the best type to convert to, but it's
              // probably the best choice in expressions with mixed type
              // (such as x+1U+2LL). The rules for implicit conversions should
              // choose a reasonable type to preserve the expression, and will
              // at least match how the value is going to be used.
              APSIntType IntType = BasicVals.getAPSIntType(resultTy);
              const llvm::APSInt &first = IntType.convert(symIntExpr->getRHS());
              const llvm::APSInt &second = IntType.convert(*RHSValue);

              // If the op and lop agrees, then we just need to
              // sum the constants. Otherwise, we change to operation
              // type if substraction would produce negative value
              // (and cause overflow for unsigned integers),
              // as consequence x+1U-10 produces x-9U, instead
              // of x+4294967287U, that would be produced without this
              // additional check.
              const llvm::APSInt *newRHS;
              if (lop == op) {
                newRHS = BasicVals.evalAPSInt(BO_Add, first, second);
              } else if (first >= second) {
                newRHS = BasicVals.evalAPSInt(BO_Sub, first, second);
                op = lop;
              } else {
                newRHS = BasicVals.evalAPSInt(BO_Sub, second, first);
              }

              assert(newRHS && "Invalid operation despite common type!");
              rhs = nonloc::ConcreteInt(*newRHS);
              lhs = nonloc::SymbolVal(symIntExpr->getLHS());
              continue;
            }
          }

          // Otherwise, make a SymIntExpr out of the expression.
          return MakeSymIntVal(symIntExpr, op, *RHSValue, resultTy);
        }
      }

      // Is the RHS a constant?
      if (const llvm::APSInt *RHSValue = getConstValue(state, rhs))
        return MakeSymIntVal(Sym, op, *RHSValue, resultTy);

      if (std::optional<NonLoc> V = tryRearrange(state, op, lhs, rhs, resultTy))
        return *V;

      // Give up -- this is not a symbolic expression we can handle.
      return makeSymExprValNN(op, InputLHS, InputRHS, resultTy);
    }
    }
  }
}

static SVal evalBinOpFieldRegionFieldRegion(const FieldRegion *LeftFR,
                                            const FieldRegion *RightFR,
                                            BinaryOperator::Opcode op,
                                            QualType resultTy,
                                            SimpleSValBuilder &SVB) {
  // Only comparisons are meaningful here!
  if (!BinaryOperator::isComparisonOp(op))
    return UnknownVal();

  // Next, see if the two FRs have the same super-region.
  // FIXME: This doesn't handle casts yet, and simply stripping the casts
  // doesn't help.
  if (LeftFR->getSuperRegion() != RightFR->getSuperRegion())
    return UnknownVal();

  const FieldDecl *LeftFD = LeftFR->getDecl();
  const FieldDecl *RightFD = RightFR->getDecl();
  const RecordDecl *RD = LeftFD->getParent();

  // Make sure the two FRs are from the same kind of record. Just in case!
  // FIXME: This is probably where inheritance would be a problem.
  if (RD != RightFD->getParent())
    return UnknownVal();

  // We know for sure that the two fields are not the same, since that
  // would have given us the same SVal.
  if (op == BO_EQ)
    return SVB.makeTruthVal(false, resultTy);
  if (op == BO_NE)
    return SVB.makeTruthVal(true, resultTy);

  // Iterate through the fields and see which one comes first.
  // [C99 6.7.2.1.13] "Within a structure object, the non-bit-field
  // members and the units in which bit-fields reside have addresses that
  // increase in the order in which they are declared."
  bool leftFirst = (op == BO_LT || op == BO_LE);
  for (const auto *I : RD->fields()) {
    if (I == LeftFD)
      return SVB.makeTruthVal(leftFirst, resultTy);
    if (I == RightFD)
      return SVB.makeTruthVal(!leftFirst, resultTy);
  }

  llvm_unreachable("Fields not found in parent record's definition");
}

// This is used in debug builds only for now because some downstream users
// may hit this assert in their subsequent merges.
// There are still places in the analyzer where equal bitwidth Locs
// are compared, and need to be found and corrected. Recent previous fixes have
// addressed the known problems of making NULLs with specific bitwidths
// for Loc comparisons along with deprecation of APIs for the same purpose.
//
static void assertEqualBitWidths(ProgramStateRef State, Loc RhsLoc,
                                 Loc LhsLoc) {
  // Implements a "best effort" check for RhsLoc and LhsLoc bit widths
  ASTContext &Ctx = State->getStateManager().getContext();
  uint64_t RhsBitwidth =
      RhsLoc.getType(Ctx).isNull() ? 0 : Ctx.getTypeSize(RhsLoc.getType(Ctx));
  uint64_t LhsBitwidth =
      LhsLoc.getType(Ctx).isNull() ? 0 : Ctx.getTypeSize(LhsLoc.getType(Ctx));
  if (RhsBitwidth && LhsBitwidth && (LhsLoc.getKind() == RhsLoc.getKind())) {
    assert(RhsBitwidth == LhsBitwidth &&
           "RhsLoc and LhsLoc bitwidth must be same!");
  }
}

// FIXME: all this logic will change if/when we have MemRegion::getLocation().
SVal SimpleSValBuilder::evalBinOpLL(ProgramStateRef state,
                                  BinaryOperator::Opcode op,
                                  Loc lhs, Loc rhs,
                                  QualType resultTy) {

  // Assert that bitwidth of lhs and rhs are the same.
  // This can happen if two different address spaces are used,
  // and the bitwidths of the address spaces are different.
  // See LIT case clang/test/Analysis/cstring-checker-addressspace.c
  // FIXME: See comment above in the function assertEqualBitWidths
  assertEqualBitWidths(state, rhs, lhs);

  // Only comparisons and subtractions are valid operations on two pointers.
  // See [C99 6.5.5 through 6.5.14] or [C++0x 5.6 through 5.15].
  // However, if a pointer is casted to an integer, evalBinOpNN may end up
  // calling this function with another operation (PR7527). We don't attempt to
  // model this for now, but it could be useful, particularly when the
  // "location" is actually an integer value that's been passed through a void*.
  if (!(BinaryOperator::isComparisonOp(op) || op == BO_Sub))
    return UnknownVal();

  // Special cases for when both sides are identical.
  if (lhs == rhs) {
    switch (op) {
    default:
      llvm_unreachable("Unimplemented operation for two identical values");
    case BO_Sub:
      return makeZeroVal(resultTy);
    case BO_EQ:
    case BO_LE:
    case BO_GE:
      return makeTruthVal(true, resultTy);
    case BO_NE:
    case BO_LT:
    case BO_GT:
      return makeTruthVal(false, resultTy);
    }
  }

  switch (lhs.getKind()) {
  default:
    llvm_unreachable("Ordering not implemented for this Loc.");

  case loc::GotoLabelKind:
    // The only thing we know about labels is that they're non-null.
    if (rhs.isZeroConstant()) {
      switch (op) {
      default:
        break;
      case BO_Sub:
        return evalCast(lhs, resultTy, QualType{});
      case BO_EQ:
      case BO_LE:
      case BO_LT:
        return makeTruthVal(false, resultTy);
      case BO_NE:
      case BO_GT:
      case BO_GE:
        return makeTruthVal(true, resultTy);
      }
    }
    // There may be two labels for the same location, and a function region may
    // have the same address as a label at the start of the function (depending
    // on the ABI).
    // FIXME: we can probably do a comparison against other MemRegions, though.
    // FIXME: is there a way to tell if two labels refer to the same location?
    return UnknownVal();

  case loc::ConcreteIntKind: {
    auto L = lhs.castAs<loc::ConcreteInt>();

    // If one of the operands is a symbol and the other is a constant,
    // build an expression for use by the constraint manager.
    if (SymbolRef rSym = rhs.getAsLocSymbol()) {
      // We can only build expressions with symbols on the left,
      // so we need a reversible operator.
      if (!BinaryOperator::isComparisonOp(op) || op == BO_Cmp)
        return UnknownVal();

      op = BinaryOperator::reverseComparisonOp(op);
      return makeNonLoc(rSym, op, L.getValue(), resultTy);
    }

    // If both operands are constants, just perform the operation.
    if (std::optional<loc::ConcreteInt> rInt = rhs.getAs<loc::ConcreteInt>()) {
      assert(BinaryOperator::isComparisonOp(op) || op == BO_Sub);

      if (const auto *ResultInt =
              BasicVals.evalAPSInt(op, L.getValue(), rInt->getValue()))
        return evalCast(nonloc::ConcreteInt(*ResultInt), resultTy, QualType{});
      return UnknownVal();
    }

    // Special case comparisons against NULL.
    // This must come after the test if the RHS is a symbol, which is used to
    // build constraints. The address of any non-symbolic region is guaranteed
    // to be non-NULL, as is any label.
    assert((isa<loc::MemRegionVal, loc::GotoLabel>(rhs)));
    if (lhs.isZeroConstant()) {
      switch (op) {
      default:
        break;
      case BO_EQ:
      case BO_GT:
      case BO_GE:
        return makeTruthVal(false, resultTy);
      case BO_NE:
      case BO_LT:
      case BO_LE:
        return makeTruthVal(true, resultTy);
      }
    }

    // Comparing an arbitrary integer to a region or label address is
    // completely unknowable.
    return UnknownVal();
  }
  case loc::MemRegionValKind: {
    if (std::optional<loc::ConcreteInt> rInt = rhs.getAs<loc::ConcreteInt>()) {
      // If one of the operands is a symbol and the other is a constant,
      // build an expression for use by the constraint manager.
      if (SymbolRef lSym = lhs.getAsLocSymbol(true)) {
        if (BinaryOperator::isComparisonOp(op))
          return MakeSymIntVal(lSym, op, rInt->getValue(), resultTy);
        return UnknownVal();
      }
      // Special case comparisons to NULL.
      // This must come after the test if the LHS is a symbol, which is used to
      // build constraints. The address of any non-symbolic region is guaranteed
      // to be non-NULL.
      if (rInt->isZeroConstant()) {
        if (op == BO_Sub)
          return evalCast(lhs, resultTy, QualType{});

        if (BinaryOperator::isComparisonOp(op)) {
          QualType boolType = getContext().BoolTy;
          NonLoc l = evalCast(lhs, boolType, QualType{}).castAs<NonLoc>();
          NonLoc r = makeTruthVal(false, boolType).castAs<NonLoc>();
          return evalBinOpNN(state, op, l, r, resultTy);
        }
      }

      // Comparing a region to an arbitrary integer is completely unknowable.
      return UnknownVal();
    }

    // Get both values as regions, if possible.
    const MemRegion *LeftMR = lhs.getAsRegion();
    assert(LeftMR && "MemRegionValKind SVal doesn't have a region!");

    const MemRegion *RightMR = rhs.getAsRegion();
    if (!RightMR)
      // The RHS is probably a label, which in theory could address a region.
      // FIXME: we can probably make a more useful statement about non-code
      // regions, though.
      return UnknownVal();

    const MemRegion *LeftBase = LeftMR->getBaseRegion();
    const MemRegion *RightBase = RightMR->getBaseRegion();
    const MemSpaceRegion *LeftMS = LeftBase->getMemorySpace();
    const MemSpaceRegion *RightMS = RightBase->getMemorySpace();
    const MemSpaceRegion *UnknownMS = MemMgr.getUnknownRegion();

    // If the two regions are from different known memory spaces they cannot be
    // equal. Also, assume that no symbolic region (whose memory space is
    // unknown) is on the stack.
    if (LeftMS != RightMS &&
        ((LeftMS != UnknownMS && RightMS != UnknownMS) ||
         (isa<StackSpaceRegion>(LeftMS) || isa<StackSpaceRegion>(RightMS)))) {
      switch (op) {
      default:
        return UnknownVal();
      case BO_EQ:
        return makeTruthVal(false, resultTy);
      case BO_NE:
        return makeTruthVal(true, resultTy);
      }
    }

    // If both values wrap regions, see if they're from different base regions.
    // Note, heap base symbolic regions are assumed to not alias with
    // each other; for example, we assume that malloc returns different address
    // on each invocation.
    // FIXME: ObjC object pointers always reside on the heap, but currently
    // we treat their memory space as unknown, because symbolic pointers
    // to ObjC objects may alias. There should be a way to construct
    // possibly-aliasing heap-based regions. For instance, MacOSXApiChecker
    // guesses memory space for ObjC object pointers manually instead of
    // relying on us.
    if (LeftBase != RightBase &&
        ((!isa<SymbolicRegion>(LeftBase) && !isa<SymbolicRegion>(RightBase)) ||
         (isa<HeapSpaceRegion>(LeftMS) || isa<HeapSpaceRegion>(RightMS))) ){
      switch (op) {
      default:
        return UnknownVal();
      case BO_EQ:
        return makeTruthVal(false, resultTy);
      case BO_NE:
        return makeTruthVal(true, resultTy);
      }
    }

    // Handle special cases for when both regions are element regions.
    const ElementRegion *RightER = dyn_cast<ElementRegion>(RightMR);
    const ElementRegion *LeftER = dyn_cast<ElementRegion>(LeftMR);
    if (RightER && LeftER) {
      // Next, see if the two ERs have the same super-region and matching types.
      // FIXME: This should do something useful even if the types don't match,
      // though if both indexes are constant the RegionRawOffset path will
      // give the correct answer.
      if (LeftER->getSuperRegion() == RightER->getSuperRegion() &&
          LeftER->getElementType() == RightER->getElementType()) {
        // Get the left index and cast it to the correct type.
        // If the index is unknown or undefined, bail out here.
        SVal LeftIndexVal = LeftER->getIndex();
        std::optional<NonLoc> LeftIndex = LeftIndexVal.getAs<NonLoc>();
        if (!LeftIndex)
          return UnknownVal();
        LeftIndexVal = evalCast(*LeftIndex, ArrayIndexTy, QualType{});
        LeftIndex = LeftIndexVal.getAs<NonLoc>();
        if (!LeftIndex)
          return UnknownVal();

        // Do the same for the right index.
        SVal RightIndexVal = RightER->getIndex();
        std::optional<NonLoc> RightIndex = RightIndexVal.getAs<NonLoc>();
        if (!RightIndex)
          return UnknownVal();
        RightIndexVal = evalCast(*RightIndex, ArrayIndexTy, QualType{});
        RightIndex = RightIndexVal.getAs<NonLoc>();
        if (!RightIndex)
          return UnknownVal();

        // Actually perform the operation.
        // evalBinOpNN expects the two indexes to already be the right type.
        return evalBinOpNN(state, op, *LeftIndex, *RightIndex, resultTy);
      }
    }

    // Special handling of the FieldRegions, even with symbolic offsets.
    const FieldRegion *RightFR = dyn_cast<FieldRegion>(RightMR);
    const FieldRegion *LeftFR = dyn_cast<FieldRegion>(LeftMR);
    if (RightFR && LeftFR) {
      SVal R = evalBinOpFieldRegionFieldRegion(LeftFR, RightFR, op, resultTy,
                                               *this);
      if (!R.isUnknown())
        return R;
    }

    // Compare the regions using the raw offsets.
    RegionOffset LeftOffset = LeftMR->getAsOffset();
    RegionOffset RightOffset = RightMR->getAsOffset();

    if (LeftOffset.getRegion() != nullptr &&
        LeftOffset.getRegion() == RightOffset.getRegion() &&
        !LeftOffset.hasSymbolicOffset() && !RightOffset.hasSymbolicOffset()) {
      int64_t left = LeftOffset.getOffset();
      int64_t right = RightOffset.getOffset();

      switch (op) {
        default:
          return UnknownVal();
        case BO_LT:
          return makeTruthVal(left < right, resultTy);
        case BO_GT:
          return makeTruthVal(left > right, resultTy);
        case BO_LE:
          return makeTruthVal(left <= right, resultTy);
        case BO_GE:
          return makeTruthVal(left >= right, resultTy);
        case BO_EQ:
          return makeTruthVal(left == right, resultTy);
        case BO_NE:
          return makeTruthVal(left != right, resultTy);
      }
    }

    // At this point we're not going to get a good answer, but we can try
    // conjuring an expression instead.
    SymbolRef LHSSym = lhs.getAsLocSymbol();
    SymbolRef RHSSym = rhs.getAsLocSymbol();
    if (LHSSym && RHSSym)
      return makeNonLoc(LHSSym, op, RHSSym, resultTy);

    // If we get here, we have no way of comparing the regions.
    return UnknownVal();
  }
  }
}

SVal SimpleSValBuilder::evalBinOpLN(ProgramStateRef state,
                                    BinaryOperator::Opcode op, Loc lhs,
                                    NonLoc rhs, QualType resultTy) {
  if (op >= BO_PtrMemD && op <= BO_PtrMemI) {
    if (auto PTMSV = rhs.getAs<nonloc::PointerToMember>()) {
      if (PTMSV->isNullMemberPointer())
        return UndefinedVal();

      auto getFieldLValue = [&](const auto *FD) -> SVal {
        SVal Result = lhs;

        for (const auto &I : *PTMSV)
          Result = StateMgr.getStoreManager().evalDerivedToBase(
              Result, I->getType(), I->isVirtual());

        return state->getLValue(FD, Result);
      };

      if (const auto *FD = PTMSV->getDeclAs<FieldDecl>()) {
        return getFieldLValue(FD);
      }
      if (const auto *FD = PTMSV->getDeclAs<IndirectFieldDecl>()) {
        return getFieldLValue(FD);
      }
    }

    return rhs;
  }

  assert(!BinaryOperator::isComparisonOp(op) &&
         "arguments to comparison ops must be of the same type");

  // Special case: rhs is a zero constant.
  if (rhs.isZeroConstant())
    return lhs;

  // Perserve the null pointer so that it can be found by the DerefChecker.
  if (lhs.isZeroConstant())
    return lhs;

  // We are dealing with pointer arithmetic.

  // Handle pointer arithmetic on constant values.
  if (std::optional<nonloc::ConcreteInt> rhsInt =
          rhs.getAs<nonloc::ConcreteInt>()) {
    if (std::optional<loc::ConcreteInt> lhsInt =
            lhs.getAs<loc::ConcreteInt>()) {
      const llvm::APSInt &leftI = lhsInt->getValue();
      assert(leftI.isUnsigned());
      llvm::APSInt rightI(rhsInt->getValue(), /* isUnsigned */ true);

      // Convert the bitwidth of rightI.  This should deal with overflow
      // since we are dealing with concrete values.
      rightI = rightI.extOrTrunc(leftI.getBitWidth());

      // Offset the increment by the pointer size.
      llvm::APSInt Multiplicand(rightI.getBitWidth(), /* isUnsigned */ true);
      QualType pointeeType = resultTy->getPointeeType();
      Multiplicand = getContext().getTypeSizeInChars(pointeeType).getQuantity();
      rightI *= Multiplicand;

      // Compute the adjusted pointer.
      switch (op) {
        case BO_Add:
          rightI = leftI + rightI;
          break;
        case BO_Sub:
          rightI = leftI - rightI;
          break;
        default:
          llvm_unreachable("Invalid pointer arithmetic operation");
      }
      return loc::ConcreteInt(getBasicValueFactory().getValue(rightI));
    }
  }

  // Handle cases where 'lhs' is a region.
  if (const MemRegion *region = lhs.getAsRegion()) {
    rhs = convertToArrayIndex(rhs).castAs<NonLoc>();
    SVal index = UnknownVal();
    const SubRegion *superR = nullptr;
    // We need to know the type of the pointer in order to add an integer to it.
    // Depending on the type, different amount of bytes is added.
    QualType elementType;

    if (const ElementRegion *elemReg = dyn_cast<ElementRegion>(region)) {
      assert(op == BO_Add || op == BO_Sub);
      index = evalBinOpNN(state, op, elemReg->getIndex(), rhs,
                          getArrayIndexType());
      superR = cast<SubRegion>(elemReg->getSuperRegion());
      elementType = elemReg->getElementType();
    }
    else if (isa<SubRegion>(region)) {
      assert(op == BO_Add || op == BO_Sub);
      index = (op == BO_Add) ? rhs : evalMinus(rhs);
      superR = cast<SubRegion>(region);
      // TODO: Is this actually reliable? Maybe improving our MemRegion
      // hierarchy to provide typed regions for all non-void pointers would be
      // better. For instance, we cannot extend this towards LocAsInteger
      // operations, where result type of the expression is integer.
      if (resultTy->isAnyPointerType())
        elementType = resultTy->getPointeeType();
    }

    // Represent arithmetic on void pointers as arithmetic on char pointers.
    // It is fine when a TypedValueRegion of char value type represents
    // a void pointer. Note that arithmetic on void pointers is a GCC extension.
    if (elementType->isVoidType())
      elementType = getContext().CharTy;

    if (std::optional<NonLoc> indexV = index.getAs<NonLoc>()) {
      return loc::MemRegionVal(MemMgr.getElementRegion(elementType, *indexV,
                                                       superR, getContext()));
    }
  }
  return UnknownVal();
}

const llvm::APSInt *SimpleSValBuilder::getConstValue(ProgramStateRef state,
                                                     SVal V) {
  if (const llvm::APSInt *Res = getConcreteValue(V))
    return Res;

  if (SymbolRef Sym = V.getAsSymbol())
    return state->getConstraintManager().getSymVal(state, Sym);

  return nullptr;
}

const llvm::APSInt *SimpleSValBuilder::getConcreteValue(SVal V) {
  if (std::optional<loc::ConcreteInt> X = V.getAs<loc::ConcreteInt>())
    return &X->getValue();

  if (std::optional<nonloc::ConcreteInt> X = V.getAs<nonloc::ConcreteInt>())
    return &X->getValue();

  return nullptr;
}

const llvm::APSInt *SimpleSValBuilder::getKnownValue(ProgramStateRef state,
                                                     SVal V) {
  return getConstValue(state, simplifySVal(state, V));
}

const llvm::APSInt *SimpleSValBuilder::getMinValue(ProgramStateRef state,
                                                   SVal V) {
  V = simplifySVal(state, V);

  if (const llvm::APSInt *Res = getConcreteValue(V))
    return Res;

  if (SymbolRef Sym = V.getAsSymbol())
    return state->getConstraintManager().getSymMinVal(state, Sym);

  return nullptr;
}

const llvm::APSInt *SimpleSValBuilder::getMaxValue(ProgramStateRef state,
                                                   SVal V) {
  V = simplifySVal(state, V);

  if (const llvm::APSInt *Res = getConcreteValue(V))
    return Res;

  if (SymbolRef Sym = V.getAsSymbol())
    return state->getConstraintManager().getSymMaxVal(state, Sym);

  return nullptr;
}

SVal SimpleSValBuilder::simplifyUntilFixpoint(ProgramStateRef State, SVal Val) {
  SVal SimplifiedVal = simplifySValOnce(State, Val);
  while (SimplifiedVal != Val) {
    Val = SimplifiedVal;
    SimplifiedVal = simplifySValOnce(State, Val);
  }
  return SimplifiedVal;
}

SVal SimpleSValBuilder::simplifySVal(ProgramStateRef State, SVal V) {
  return simplifyUntilFixpoint(State, V);
}

SVal SimpleSValBuilder::simplifySValOnce(ProgramStateRef State, SVal V) {
  // For now, this function tries to constant-fold symbols inside a
  // nonloc::SymbolVal, and does nothing else. More simplifications should
  // be possible, such as constant-folding an index in an ElementRegion.

  class Simplifier : public FullSValVisitor<Simplifier, SVal> {
    ProgramStateRef State;
    SValBuilder &SVB;

    // Cache results for the lifetime of the Simplifier. Results change every
    // time new constraints are added to the program state, which is the whole
    // point of simplifying, and for that very reason it's pointless to maintain
    // the same cache for the duration of the whole analysis.
    llvm::DenseMap<SymbolRef, SVal> Cached;

    static bool isUnchanged(SymbolRef Sym, SVal Val) {
      return Sym == Val.getAsSymbol();
    }

    SVal cache(SymbolRef Sym, SVal V) {
      Cached[Sym] = V;
      return V;
    }

    SVal skip(SymbolRef Sym) {
      return cache(Sym, SVB.makeSymbolVal(Sym));
    }

    // Return the known const value for the Sym if available, or return Undef
    // otherwise.
    SVal getConst(SymbolRef Sym) {
      const llvm::APSInt *Const =
          State->getConstraintManager().getSymVal(State, Sym);
      if (Const)
        return Loc::isLocType(Sym->getType()) ? (SVal)SVB.makeIntLocVal(*Const)
                                              : (SVal)SVB.makeIntVal(*Const);
      return UndefinedVal();
    }

    SVal getConstOrVisit(SymbolRef Sym) {
      const SVal Ret = getConst(Sym);
      if (Ret.isUndef())
        return Visit(Sym);
      return Ret;
    }

  public:
    Simplifier(ProgramStateRef State)
        : State(State), SVB(State->getStateManager().getSValBuilder()) {}

    SVal VisitSymbolData(const SymbolData *S) {
      // No cache here.
      if (const llvm::APSInt *I =
              State->getConstraintManager().getSymVal(State, S))
        return Loc::isLocType(S->getType()) ? (SVal)SVB.makeIntLocVal(*I)
                                            : (SVal)SVB.makeIntVal(*I);
      return SVB.makeSymbolVal(S);
    }

    SVal VisitSymIntExpr(const SymIntExpr *S) {
      auto I = Cached.find(S);
      if (I != Cached.end())
        return I->second;

      SVal LHS = getConstOrVisit(S->getLHS());
      if (isUnchanged(S->getLHS(), LHS))
        return skip(S);

      SVal RHS;
      // By looking at the APSInt in the right-hand side of S, we cannot
      // figure out if it should be treated as a Loc or as a NonLoc.
      // So make our guess by recalling that we cannot multiply pointers
      // or compare a pointer to an integer.
      if (Loc::isLocType(S->getLHS()->getType()) &&
          BinaryOperator::isComparisonOp(S->getOpcode())) {
        // The usual conversion of $sym to &SymRegion{$sym}, as they have
        // the same meaning for Loc-type symbols, but the latter form
        // is preferred in SVal computations for being Loc itself.
        if (SymbolRef Sym = LHS.getAsSymbol()) {
          assert(Loc::isLocType(Sym->getType()));
          LHS = SVB.makeLoc(Sym);
        }
        RHS = SVB.makeIntLocVal(S->getRHS());
      } else {
        RHS = SVB.makeIntVal(S->getRHS());
      }

      return cache(
          S, SVB.evalBinOp(State, S->getOpcode(), LHS, RHS, S->getType()));
    }

    SVal VisitIntSymExpr(const IntSymExpr *S) {
      auto I = Cached.find(S);
      if (I != Cached.end())
        return I->second;

      SVal RHS = getConstOrVisit(S->getRHS());
      if (isUnchanged(S->getRHS(), RHS))
        return skip(S);

      SVal LHS = SVB.makeIntVal(S->getLHS());
      return cache(
          S, SVB.evalBinOp(State, S->getOpcode(), LHS, RHS, S->getType()));
    }

    SVal VisitSymSymExpr(const SymSymExpr *S) {
      auto I = Cached.find(S);
      if (I != Cached.end())
        return I->second;

      // For now don't try to simplify mixed Loc/NonLoc expressions
      // because they often appear from LocAsInteger operations
      // and we don't know how to combine a LocAsInteger
      // with a concrete value.
      if (Loc::isLocType(S->getLHS()->getType()) !=
          Loc::isLocType(S->getRHS()->getType()))
        return skip(S);

      SVal LHS = getConstOrVisit(S->getLHS());
      SVal RHS = getConstOrVisit(S->getRHS());

      if (isUnchanged(S->getLHS(), LHS) && isUnchanged(S->getRHS(), RHS))
        return skip(S);

      return cache(
          S, SVB.evalBinOp(State, S->getOpcode(), LHS, RHS, S->getType()));
    }

    SVal VisitSymbolCast(const SymbolCast *S) {
      auto I = Cached.find(S);
      if (I != Cached.end())
        return I->second;
      const SymExpr *OpSym = S->getOperand();
      SVal OpVal = getConstOrVisit(OpSym);
      if (isUnchanged(OpSym, OpVal))
        return skip(S);

      return cache(S, SVB.evalCast(OpVal, S->getType(), OpSym->getType()));
    }

    SVal VisitUnarySymExpr(const UnarySymExpr *S) {
      auto I = Cached.find(S);
      if (I != Cached.end())
        return I->second;
      SVal Op = getConstOrVisit(S->getOperand());
      if (isUnchanged(S->getOperand(), Op))
        return skip(S);

      return cache(
          S, SVB.evalUnaryOp(State, S->getOpcode(), Op, S->getType()));
    }

    SVal VisitSymExpr(SymbolRef S) { return nonloc::SymbolVal(S); }

    SVal VisitMemRegion(const MemRegion *R) { return loc::MemRegionVal(R); }

    SVal VisitSymbolVal(nonloc::SymbolVal V) {
      // Simplification is much more costly than computing complexity.
      // For high complexity, it may be not worth it.
      return Visit(V.getSymbol());
    }

    SVal VisitSVal(SVal V) { return V; }
  };

  SVal SimplifiedV = Simplifier(State).Visit(V);
  return SimplifiedV;
}
