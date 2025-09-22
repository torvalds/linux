//== BitwiseShiftChecker.cpp ------------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines BitwiseShiftChecker, which is a path-sensitive checker
// that looks for undefined behavior when the operands of the bitwise shift
// operators '<<' and '>>' are invalid (negative or too large).
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/CharUnits.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/Support/FormatVariadic.h"
#include <memory>

using namespace clang;
using namespace ento;
using llvm::formatv;

namespace {
enum class OperandSide { Left, Right };

using BugReportPtr = std::unique_ptr<PathSensitiveBugReport>;

struct NoteTagTemplate {
  llvm::StringLiteral SignInfo;
  llvm::StringLiteral UpperBoundIntro;
};

constexpr NoteTagTemplate NoteTagTemplates[] = {
  {"", "right operand of bit shift is less than "},
  {"left operand of bit shift is non-negative", " and right operand is less than "},
  {"right operand of bit shift is non-negative", " but less than "},
  {"both operands of bit shift are non-negative", " and right operand is less than "}
};

/// An implementation detail class which is introduced to split the checker
/// logic into several methods while maintaining a consistently updated state
/// and access to other contextual data.
class BitwiseShiftValidator {
  CheckerContext &Ctx;
  ProgramStateRef FoldedState;
  const BinaryOperator *const Op;
  const BugType &BT;
  const bool PedanticFlag;

  // The following data members are only used for note tag creation:
  enum { NonNegLeft = 1, NonNegRight = 2 };
  unsigned NonNegOperands = 0;

  std::optional<unsigned> UpperBoundBitCount = std::nullopt;

public:
  BitwiseShiftValidator(const BinaryOperator *O, CheckerContext &C,
                        const BugType &B, bool P)
      : Ctx(C), FoldedState(C.getState()), Op(O), BT(B), PedanticFlag(P) {}
  void run();

private:
  const Expr *operandExpr(OperandSide Side) const {
    return Side == OperandSide::Left ? Op->getLHS() : Op->getRHS();
  }

  bool shouldPerformPedanticChecks() const {
    // The pedantic flag has no effect under C++20 because the affected issues
    // are no longer undefined under that version of the standard.
    return PedanticFlag && !Ctx.getASTContext().getLangOpts().CPlusPlus20;
  }

  bool assumeRequirement(OperandSide Side, BinaryOperator::Opcode Cmp, unsigned Limit);

  void recordAssumption(OperandSide Side, BinaryOperator::Opcode Cmp, unsigned Limit);
  const NoteTag *createNoteTag() const;

  BugReportPtr createBugReport(StringRef ShortMsg, StringRef Msg) const;

  BugReportPtr checkOvershift();
  BugReportPtr checkOperandNegative(OperandSide Side);
  BugReportPtr checkLeftShiftOverflow();

  bool isLeftShift() const { return Op->getOpcode() == BO_Shl; }
  StringRef shiftDir() const { return isLeftShift() ? "left" : "right"; }
  static StringRef pluralSuffix(unsigned n) { return n <= 1 ? "" : "s"; }
  static StringRef verbSuffix(unsigned n) { return n <= 1 ? "s" : ""; }
};

void BitwiseShiftValidator::run() {
  // Report a bug if the right operand is >= the bit width of the type of the
  // left operand:
  if (BugReportPtr BR = checkOvershift()) {
    Ctx.emitReport(std::move(BR));
    return;
  }

  // Report a bug if the right operand is negative:
  if (BugReportPtr BR = checkOperandNegative(OperandSide::Right)) {
    Ctx.emitReport(std::move(BR));
    return;
  }

  if (shouldPerformPedanticChecks()) {
    // Report a bug if the left operand is negative:
    if (BugReportPtr BR = checkOperandNegative(OperandSide::Left)) {
      Ctx.emitReport(std::move(BR));
      return;
    }

    // Report a bug when left shift of a concrete signed value overflows:
    if (BugReportPtr BR = checkLeftShiftOverflow()) {
      Ctx.emitReport(std::move(BR));
      return;
    }
  }

  // No bugs detected, update the state and add a single note tag which
  // summarizes the new assumptions.
  Ctx.addTransition(FoldedState, createNoteTag());
}

/// This method checks a requirement that must be satisfied by the value on the
/// given Side of a bitwise shift operator in well-defined code. If the
/// requirement is incompatible with prior knowledge, this method reports
/// failure by returning false.
bool BitwiseShiftValidator::assumeRequirement(OperandSide Side,
                                              BinaryOperator::Opcode Comparison,
                                              unsigned Limit) {
  SValBuilder &SVB = Ctx.getSValBuilder();

  const SVal OperandVal = Ctx.getSVal(operandExpr(Side));
  const auto LimitVal = SVB.makeIntVal(Limit, Ctx.getASTContext().IntTy);
  // Note that the type of `LimitVal` must be a signed, because otherwise a
  // negative `Val` could be converted to a large positive value.

  auto ResultVal = SVB.evalBinOp(FoldedState, Comparison, OperandVal, LimitVal,
                                 SVB.getConditionType());
  if (auto DURes = ResultVal.getAs<DefinedOrUnknownSVal>()) {
    auto [StTrue, StFalse] = FoldedState->assume(DURes.value());
    if (!StTrue) {
      // We detected undefined behavior (the caller will report it).
      FoldedState = StFalse;
      return false;
    }
    // The code may be valid, so let's assume that it's valid:
    FoldedState = StTrue;
    if (StFalse) {
      // Record note tag data for the assumption that we made
      recordAssumption(Side, Comparison, Limit);
    }
  }
  return true;
}

BugReportPtr BitwiseShiftValidator::checkOvershift() {
  const QualType LHSTy = Op->getLHS()->getType();
  const unsigned LHSBitWidth = Ctx.getASTContext().getIntWidth(LHSTy);

  if (assumeRequirement(OperandSide::Right, BO_LT, LHSBitWidth))
    return nullptr;

  const SVal Right = Ctx.getSVal(operandExpr(OperandSide::Right));

  std::string RightOpStr = "", LowerBoundStr = "";
  if (auto ConcreteRight = Right.getAs<nonloc::ConcreteInt>())
    RightOpStr = formatv(" '{0}'", ConcreteRight->getValue());
  else {
    SValBuilder &SVB = Ctx.getSValBuilder();
    if (const llvm::APSInt *MinRight = SVB.getMinValue(FoldedState, Right)) {
      LowerBoundStr = formatv(" >= {0},", MinRight->getExtValue());
    }
  }

  std::string ShortMsg = formatv(
      "{0} shift{1}{2} overflows the capacity of '{3}'",
      isLeftShift() ? "Left" : "Right", RightOpStr.empty() ? "" : " by",
      RightOpStr, LHSTy.getAsString());
  std::string Msg = formatv(
      "The result of {0} shift is undefined because the right "
      "operand{1} is{2} not smaller than {3}, the capacity of '{4}'",
      shiftDir(), RightOpStr, LowerBoundStr, LHSBitWidth, LHSTy.getAsString());
  return createBugReport(ShortMsg, Msg);
}

// Before C++20, at 5.8 [expr.shift] (N4296, 2014-11-19) the standard says
// 1. "... The behaviour is undefined if the right operand is negative..."
// 2. "The value of E1 << E2 ...
//     if E1 has a signed type and non-negative value ...
//     otherwise, the behavior is undefined."
// 3. "The value of E1 >> E2 ...
//     If E1 has a signed type and a negative value,
//     the resulting value is implementation-defined."
// However, negative left arguments work in practice and the C++20 standard
// eliminates conditions 2 and 3.
BugReportPtr BitwiseShiftValidator::checkOperandNegative(OperandSide Side) {
  // If the type is unsigned, it cannot be negative
  if (!operandExpr(Side)->getType()->isSignedIntegerType())
    return nullptr;

  // Main check: determine whether the operand is constrained to be negative
  if (assumeRequirement(Side, BO_GE, 0))
    return nullptr;

  std::string ShortMsg = formatv("{0} operand is negative in {1} shift",
                                 Side == OperandSide::Left ? "Left" : "Right",
                                 shiftDir())
                             .str();
  std::string Msg = formatv("The result of {0} shift is undefined "
                            "because the {1} operand is negative",
                            shiftDir(),
                            Side == OperandSide::Left ? "left" : "right")
                        .str();

  return createBugReport(ShortMsg, Msg);
}

BugReportPtr BitwiseShiftValidator::checkLeftShiftOverflow() {
  // A right shift cannot be an overflowing left shift...
  if (!isLeftShift())
    return nullptr;

  // In C++ it's well-defined to shift to the sign bit. In C however, it's UB.
  // 5.8.2 [expr.shift] (N4296, 2014-11-19)
  const bool ShouldPreserveSignBit = !Ctx.getLangOpts().CPlusPlus;

  const Expr *LHS = operandExpr(OperandSide::Left);
  const QualType LHSTy = LHS->getType();
  const unsigned LeftBitWidth = Ctx.getASTContext().getIntWidth(LHSTy);
  assert(LeftBitWidth > 0);

  // Quote "For unsigned lhs, the value of LHS << RHS is the value of LHS *
  // 2^RHS, reduced modulo maximum value of the return type plus 1."
  if (LHSTy->isUnsignedIntegerType())
    return nullptr;

  // We only support concrete integers as left operand.
  const auto Left = Ctx.getSVal(LHS).getAs<nonloc::ConcreteInt>();
  if (!Left.has_value())
    return nullptr;

  // We should have already reported a bug if the left operand of the shift was
  // negative, so it cannot be negative here.
  assert(Left->getValue().isNonNegative());

  const unsigned LeftAvailableBitWidth =
      LeftBitWidth - static_cast<unsigned>(ShouldPreserveSignBit);
  const unsigned UsedBitsInLeftOperand = Left->getValue().getActiveBits();
  assert(LeftBitWidth >= UsedBitsInLeftOperand);
  const unsigned MaximalAllowedShift =
      LeftAvailableBitWidth - UsedBitsInLeftOperand;

  if (assumeRequirement(OperandSide::Right, BO_LT, MaximalAllowedShift + 1))
    return nullptr;

  const std::string CapacityMsg =
      formatv("because '{0}' can hold only {1} bits ({2} the sign bit)",
                    LHSTy.getAsString(), LeftAvailableBitWidth,
                    ShouldPreserveSignBit ? "excluding" : "including");

  const SVal Right = Ctx.getSVal(Op->getRHS());

  std::string ShortMsg, Msg;
  if (const auto ConcreteRight = Right.getAs<nonloc::ConcreteInt>()) {
    // Here ConcreteRight must contain a small non-negative integer, because
    // otherwise one of the earlier checks should've reported a bug.
    const unsigned RHS = ConcreteRight->getValue().getExtValue();
    assert(RHS > MaximalAllowedShift);
    const unsigned OverflownBits = RHS - MaximalAllowedShift;
    ShortMsg = formatv(
        "The shift '{0} << {1}' overflows the capacity of '{2}'",
        Left->getValue(), ConcreteRight->getValue(), LHSTy.getAsString());
    Msg = formatv(
        "The shift '{0} << {1}' is undefined {2}, so {3} bit{4} overflow{5}",
        Left->getValue(), ConcreteRight->getValue(), CapacityMsg, OverflownBits,
        pluralSuffix(OverflownBits), verbSuffix(OverflownBits));
  } else {
    ShortMsg = formatv("Left shift of '{0}' overflows the capacity of '{1}'",
                       Left->getValue(), LHSTy.getAsString());
    Msg = formatv(
        "Left shift of '{0}' is undefined {1}, so some bits overflow",
        Left->getValue(), CapacityMsg);
  }

  return createBugReport(ShortMsg, Msg);
}

void BitwiseShiftValidator::recordAssumption(OperandSide Side,
                                             BinaryOperator::Opcode Comparison,
                                             unsigned Limit) {
  switch (Comparison)  {
    case BO_GE:
      assert(Limit == 0);
      NonNegOperands |= (Side == OperandSide::Left ? NonNegLeft : NonNegRight);
      break;
    case BO_LT:
      assert(Side == OperandSide::Right);
      if (!UpperBoundBitCount || Limit < UpperBoundBitCount.value())
        UpperBoundBitCount = Limit;
      break;
    default:
      llvm_unreachable("this checker does not use other comparison operators");
  }
}

const NoteTag *BitwiseShiftValidator::createNoteTag() const {
  if (!NonNegOperands && !UpperBoundBitCount)
    return nullptr;

  SmallString<128> Buf;
  llvm::raw_svector_ostream Out(Buf);
  Out << "Assuming ";
  NoteTagTemplate Templ = NoteTagTemplates[NonNegOperands];
  Out << Templ.SignInfo;
  if (UpperBoundBitCount)
    Out << Templ.UpperBoundIntro << UpperBoundBitCount.value();
  const std::string Msg(Out.str());

  return Ctx.getNoteTag(Msg, /*isPrunable=*/true);
}

std::unique_ptr<PathSensitiveBugReport>
BitwiseShiftValidator::createBugReport(StringRef ShortMsg, StringRef Msg) const {
  ProgramStateRef State = Ctx.getState();
  if (ExplodedNode *ErrNode = Ctx.generateErrorNode(State)) {
    auto BR =
        std::make_unique<PathSensitiveBugReport>(BT, ShortMsg, Msg, ErrNode);
    bugreporter::trackExpressionValue(ErrNode, Op->getLHS(), *BR);
    bugreporter::trackExpressionValue(ErrNode, Op->getRHS(), *BR);
    return BR;
  }
  return nullptr;
}
} // anonymous namespace

class BitwiseShiftChecker : public Checker<check::PreStmt<BinaryOperator>> {
  BugType BT{this, "Bitwise shift", "Suspicious operation"};

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &Ctx) const {
    BinaryOperator::Opcode Op = B->getOpcode();

    if (Op != BO_Shl && Op != BO_Shr)
      return;

    BitwiseShiftValidator(B, Ctx, BT, Pedantic).run();
  }

  bool Pedantic = false;
};

void ento::registerBitwiseShiftChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.registerChecker<BitwiseShiftChecker>();
  const AnalyzerOptions &Opts = Mgr.getAnalyzerOptions();
  Chk->Pedantic = Opts.getCheckerBooleanOption(Chk, "Pedantic");
}

bool ento::shouldRegisterBitwiseShiftChecker(const CheckerManager &mgr) {
  return true;
}
