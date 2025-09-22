//===- DIExpressionOptimizer.cpp - Constant folding of DIExpressions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements functions to constant fold DIExpressions. Which were
// declared in DIExpressionOptimizer.h
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace llvm;

/// Returns true if the Op is a DW_OP_constu.
static std::optional<uint64_t> isConstantVal(DIExpression::ExprOperand Op) {
  if (Op.getOp() == dwarf::DW_OP_constu)
    return Op.getArg(0);
  return std::nullopt;
}

/// Returns true if an operation and operand result in a No Op.
static bool isNeutralElement(uint64_t Op, uint64_t Val) {
  switch (Op) {
  case dwarf::DW_OP_plus:
  case dwarf::DW_OP_minus:
  case dwarf::DW_OP_shl:
  case dwarf::DW_OP_shr:
    return Val == 0;
  case dwarf::DW_OP_mul:
  case dwarf::DW_OP_div:
    return Val == 1;
  default:
    return false;
  }
}

/// Try to fold \p Const1 and \p Const2 by applying \p Operator and returning
/// the result, if there is an overflow, return a std::nullopt.
static std::optional<uint64_t>
foldOperationIfPossible(uint64_t Const1, uint64_t Const2,
                        dwarf::LocationAtom Operator) {

  bool ResultOverflowed;
  switch (Operator) {
  case dwarf::DW_OP_plus: {
    auto Result = SaturatingAdd(Const1, Const2, &ResultOverflowed);
    if (ResultOverflowed)
      return std::nullopt;
    return Result;
  }
  case dwarf::DW_OP_minus: {
    if (Const1 < Const2)
      return std::nullopt;
    return Const1 - Const2;
  }
  case dwarf::DW_OP_shl: {
    if ((uint64_t)countl_zero(Const1) < Const2)
      return std::nullopt;
    return Const1 << Const2;
  }
  case dwarf::DW_OP_shr: {
    if ((uint64_t)countr_zero(Const1) < Const2)
      return std::nullopt;
    return Const1 >> Const2;
  }
  case dwarf::DW_OP_mul: {
    auto Result = SaturatingMultiply(Const1, Const2, &ResultOverflowed);
    if (ResultOverflowed)
      return std::nullopt;
    return Result;
  }
  case dwarf::DW_OP_div: {
    if (Const2)
      return Const1 / Const2;
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }
}

/// Returns true if the two operations \p Operator1 and \p Operator2 are
/// commutative and can be folded.
static bool operationsAreFoldableAndCommutative(dwarf::LocationAtom Operator1,
                                                dwarf::LocationAtom Operator2) {
  return Operator1 == Operator2 &&
         (Operator1 == dwarf::DW_OP_plus || Operator1 == dwarf::DW_OP_mul);
}

/// Consume one operator and its operand(s).
static void consumeOneOperator(DIExpressionCursor &Cursor, uint64_t &Loc,
                               const DIExpression::ExprOperand &Op) {
  Cursor.consume(1);
  Loc = Loc + Op.getSize();
}

/// Reset the Cursor to the beginning of the WorkingOps.
void startFromBeginning(uint64_t &Loc, DIExpressionCursor &Cursor,
                        ArrayRef<uint64_t> WorkingOps) {
  Cursor.assignNewExpr(WorkingOps);
  Loc = 0;
}

/// This function will canonicalize:
/// 1. DW_OP_plus_uconst to DW_OP_constu <const-val> DW_OP_plus
/// 2. DW_OP_lit<n> to DW_OP_constu <n>
static SmallVector<uint64_t>
canonicalizeDwarfOperations(ArrayRef<uint64_t> WorkingOps) {
  DIExpressionCursor Cursor(WorkingOps);
  uint64_t Loc = 0;
  SmallVector<uint64_t> ResultOps;
  while (Loc < WorkingOps.size()) {
    auto Op = Cursor.peek();
    /// Expression has no operations, break.
    if (!Op)
      break;
    auto OpRaw = Op->getOp();

    if (OpRaw >= dwarf::DW_OP_lit0 && OpRaw <= dwarf::DW_OP_lit31) {
      ResultOps.push_back(dwarf::DW_OP_constu);
      ResultOps.push_back(OpRaw - dwarf::DW_OP_lit0);
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      continue;
    }
    if (OpRaw == dwarf::DW_OP_plus_uconst) {
      ResultOps.push_back(dwarf::DW_OP_constu);
      ResultOps.push_back(Op->getArg(0));
      ResultOps.push_back(dwarf::DW_OP_plus);
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      continue;
    }
    uint64_t PrevLoc = Loc;
    consumeOneOperator(Cursor, Loc, *Cursor.peek());
    ResultOps.append(WorkingOps.begin() + PrevLoc, WorkingOps.begin() + Loc);
  }
  return ResultOps;
}

/// This function will convert:
/// 1. DW_OP_constu <const-val> DW_OP_plus to DW_OP_plus_uconst
/// 2. DW_OP_constu, 0 to DW_OP_lit0
static SmallVector<uint64_t>
optimizeDwarfOperations(ArrayRef<uint64_t> WorkingOps) {
  DIExpressionCursor Cursor(WorkingOps);
  uint64_t Loc = 0;
  SmallVector<uint64_t> ResultOps;
  while (Loc < WorkingOps.size()) {
    auto Op1 = Cursor.peek();
    /// Expression has no operations, exit.
    if (!Op1)
      break;
    auto Op1Raw = Op1->getOp();

    if (Op1Raw == dwarf::DW_OP_constu && Op1->getArg(0) == 0) {
      ResultOps.push_back(dwarf::DW_OP_lit0);
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      continue;
    }

    auto Op2 = Cursor.peekNext();
    /// Expression has no more operations, copy into ResultOps and exit.
    if (!Op2) {
      uint64_t PrevLoc = Loc;
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      ResultOps.append(WorkingOps.begin() + PrevLoc, WorkingOps.begin() + Loc);
      break;
    }
    auto Op2Raw = Op2->getOp();

    if (Op1Raw == dwarf::DW_OP_constu && Op2Raw == dwarf::DW_OP_plus) {
      ResultOps.push_back(dwarf::DW_OP_plus_uconst);
      ResultOps.push_back(Op1->getArg(0));
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      consumeOneOperator(Cursor, Loc, *Cursor.peek());
      continue;
    }
    uint64_t PrevLoc = Loc;
    consumeOneOperator(Cursor, Loc, *Cursor.peek());
    ResultOps.append(WorkingOps.begin() + PrevLoc, WorkingOps.begin() + Loc);
  }
  return ResultOps;
}

/// {DW_OP_constu, 0, DW_OP_[plus, minus, shl, shr]} -> {}
/// {DW_OP_constu, 1, DW_OP_[mul, div]} -> {}
static bool tryFoldNoOpMath(uint64_t Const1,
                            ArrayRef<DIExpression::ExprOperand> Ops,
                            uint64_t &Loc, DIExpressionCursor &Cursor,
                            SmallVectorImpl<uint64_t> &WorkingOps) {

  if (isNeutralElement(Ops[1].getOp(), Const1)) {
    WorkingOps.erase(WorkingOps.begin() + Loc, WorkingOps.begin() + Loc + 3);
    startFromBeginning(Loc, Cursor, WorkingOps);
    return true;
  }
  return false;
}

/// {DW_OP_constu, Const1, DW_OP_constu, Const2, DW_OP_[plus,
/// minus, mul, div, shl, shr] -> {DW_OP_constu, Const1 [+, -, *, /, <<, >>]
/// Const2}
static bool tryFoldConstants(uint64_t Const1,
                             ArrayRef<DIExpression::ExprOperand> Ops,
                             uint64_t &Loc, DIExpressionCursor &Cursor,
                             SmallVectorImpl<uint64_t> &WorkingOps) {

  auto Const2 = isConstantVal(Ops[1]);
  if (!Const2)
    return false;

  auto Result = foldOperationIfPossible(
      Const1, *Const2, static_cast<dwarf::LocationAtom>(Ops[2].getOp()));
  if (!Result) {
    consumeOneOperator(Cursor, Loc, Ops[0]);
    return true;
  }
  WorkingOps.erase(WorkingOps.begin() + Loc + 2, WorkingOps.begin() + Loc + 5);
  WorkingOps[Loc] = dwarf::DW_OP_constu;
  WorkingOps[Loc + 1] = *Result;
  startFromBeginning(Loc, Cursor, WorkingOps);
  return true;
}

/// {DW_OP_constu, Const1, DW_OP_[plus, mul], DW_OP_constu, Const2,
/// DW_OP_[plus, mul]} -> {DW_OP_constu, Const1 [+, *] Const2, DW_OP_[plus,
/// mul]}
static bool tryFoldCommutativeMath(uint64_t Const1,
                                   ArrayRef<DIExpression::ExprOperand> Ops,
                                   uint64_t &Loc, DIExpressionCursor &Cursor,
                                   SmallVectorImpl<uint64_t> &WorkingOps) {

  auto Const2 = isConstantVal(Ops[2]);
  auto Operand1 = static_cast<dwarf::LocationAtom>(Ops[1].getOp());
  auto Operand2 = static_cast<dwarf::LocationAtom>(Ops[3].getOp());

  if (!Const2 || !operationsAreFoldableAndCommutative(Operand1, Operand2))
    return false;

  auto Result = foldOperationIfPossible(Const1, *Const2, Operand1);
  if (!Result) {
    consumeOneOperator(Cursor, Loc, Ops[0]);
    return true;
  }
  WorkingOps.erase(WorkingOps.begin() + Loc + 3, WorkingOps.begin() + Loc + 6);
  WorkingOps[Loc] = dwarf::DW_OP_constu;
  WorkingOps[Loc + 1] = *Result;
  startFromBeginning(Loc, Cursor, WorkingOps);
  return true;
}

/// {DW_OP_constu, Const1, DW_OP_[plus, mul], DW_OP_LLVM_arg, Arg1,
/// DW_OP_[plus, mul], DW_OP_constu, Const2, DW_OP_[plus, mul]} ->
/// {DW_OP_constu, Const1 [+, *] Const2, DW_OP_[plus, mul], DW_OP_LLVM_arg,
/// Arg1, DW_OP_[plus, mul]}
static bool tryFoldCommutativeMathWithArgInBetween(
    uint64_t Const1, ArrayRef<DIExpression::ExprOperand> Ops, uint64_t &Loc,
    DIExpressionCursor &Cursor, SmallVectorImpl<uint64_t> &WorkingOps) {

  auto Const2 = isConstantVal(Ops[4]);
  auto Operand1 = static_cast<dwarf::LocationAtom>(Ops[1].getOp());
  auto Operand2 = static_cast<dwarf::LocationAtom>(Ops[3].getOp());
  auto Operand3 = static_cast<dwarf::LocationAtom>(Ops[5].getOp());

  if (!Const2 || Ops[2].getOp() != dwarf::DW_OP_LLVM_arg ||
      !operationsAreFoldableAndCommutative(Operand1, Operand2) ||
      !operationsAreFoldableAndCommutative(Operand2, Operand3))
    return false;

  auto Result = foldOperationIfPossible(Const1, *Const2, Operand1);
  if (!Result) {
    consumeOneOperator(Cursor, Loc, Ops[0]);
    return true;
  }
  WorkingOps.erase(WorkingOps.begin() + Loc + 6, WorkingOps.begin() + Loc + 9);
  WorkingOps[Loc] = dwarf::DW_OP_constu;
  WorkingOps[Loc + 1] = *Result;
  startFromBeginning(Loc, Cursor, WorkingOps);
  return true;
}

DIExpression *DIExpression::foldConstantMath() {

  SmallVector<uint64_t, 8> WorkingOps(Elements.begin(), Elements.end());
  uint64_t Loc = 0;
  SmallVector<uint64_t> ResultOps = canonicalizeDwarfOperations(WorkingOps);
  DIExpressionCursor Cursor(ResultOps);
  SmallVector<DIExpression::ExprOperand, 8> Ops;

  // Iterate over all Operations in a DIExpression to match the smallest pattern
  // that can be folded.
  while (Loc < ResultOps.size()) {
    Ops.clear();

    auto Op = Cursor.peek();
    // Expression has no operations, exit.
    if (!Op)
      break;

    auto Const1 = isConstantVal(*Op);

    if (!Const1) {
      // Early exit, all of the following patterns start with a constant value.
      consumeOneOperator(Cursor, Loc, *Op);
      continue;
    }

    Ops.push_back(*Op);

    Op = Cursor.peekNext();
    // All following patterns require at least 2 Operations, exit.
    if (!Op)
      break;

    Ops.push_back(*Op);

    // Try to fold a constant no-op, such as {+ 0}
    if (tryFoldNoOpMath(*Const1, Ops, Loc, Cursor, ResultOps))
      continue;

    Op = Cursor.peekNextN(2);
    // Op[1] could still match a pattern, skip iteration.
    if (!Op) {
      consumeOneOperator(Cursor, Loc, Ops[0]);
      continue;
    }

    Ops.push_back(*Op);

    // Try to fold a pattern of two constants such as {C1 + C2}.
    if (tryFoldConstants(*Const1, Ops, Loc, Cursor, ResultOps))
      continue;

    Op = Cursor.peekNextN(3);
    // Op[1] and Op[2] could still match a pattern, skip iteration.
    if (!Op) {
      consumeOneOperator(Cursor, Loc, Ops[0]);
      continue;
    }

    Ops.push_back(*Op);

    // Try to fold commutative constant math, such as {C1 + C2 +}.
    if (tryFoldCommutativeMath(*Const1, Ops, Loc, Cursor, ResultOps))
      continue;

    Op = Cursor.peekNextN(4);
    if (!Op) {
      consumeOneOperator(Cursor, Loc, Ops[0]);
      continue;
    }

    Ops.push_back(*Op);
    Op = Cursor.peekNextN(5);
    if (!Op) {
      consumeOneOperator(Cursor, Loc, Ops[0]);
      continue;
    }

    Ops.push_back(*Op);

    // Try to fold commutative constant math with an LLVM_Arg in between, such
    // as {C1 + Arg + C2 +}.
    if (tryFoldCommutativeMathWithArgInBetween(*Const1, Ops, Loc, Cursor,
                                               ResultOps))
      continue;

    consumeOneOperator(Cursor, Loc, Ops[0]);
  }
  ResultOps = optimizeDwarfOperations(ResultOps);
  auto *Result = DIExpression::get(getContext(), ResultOps);
  assert(Result->isValid() && "concatenated expression is not valid");
  return Result;
}
