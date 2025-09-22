//===- Reassociate.h - Reassociate binary expressions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass reassociates commutative expressions in an order that is designed
// to promote better constant propagation, GCSE, LICM, PRE, etc.
//
// For example: 4 + (x + 5) -> x + (4 + 5)
//
// In the implementation of this algorithm, constants are assigned rank = 0,
// function arguments are rank = 1, and other values are assigned ranks
// corresponding to the reverse post order traversal of current function
// (starting at 2), which effectively gives values in deep loops higher rank
// than values not in loops.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H
#define LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include <deque>

namespace llvm {

class APInt;
class BasicBlock;
class BinaryOperator;
class Function;
class Instruction;
class IRBuilderBase;
class Value;

/// A private "module" namespace for types and utilities used by Reassociate.
/// These are implementation details and should not be used by clients.
namespace reassociate {

struct ValueEntry {
  unsigned Rank;
  Value *Op;

  ValueEntry(unsigned R, Value *O) : Rank(R), Op(O) {}
};

inline bool operator<(const ValueEntry &LHS, const ValueEntry &RHS) {
  return LHS.Rank > RHS.Rank; // Sort so that highest rank goes to start.
}

/// Utility class representing a base and exponent pair which form one
/// factor of some product.
struct Factor {
  Value *Base;
  unsigned Power;

  Factor(Value *Base, unsigned Power) : Base(Base), Power(Power) {}
};

struct OverflowTracking {
  bool HasNUW;
  bool HasNSW;
  bool AllKnownNonNegative;
  bool AllKnownNonZero;
  // Note: AllKnownNonNegative can be true in a case where one of the operands
  // is negative, but one the operators is not NSW. AllKnownNonNegative should
  // not be used independently of HasNSW
  OverflowTracking()
      : HasNUW(true), HasNSW(true), AllKnownNonNegative(true),
        AllKnownNonZero(true) {}
};

class XorOpnd;

} // end namespace reassociate

/// Reassociate commutative expressions.
class ReassociatePass : public PassInfoMixin<ReassociatePass> {
public:
  using OrderedSet =
      SetVector<AssertingVH<Instruction>, std::deque<AssertingVH<Instruction>>>;

protected:
  DenseMap<BasicBlock *, unsigned> RankMap;
  DenseMap<AssertingVH<Value>, unsigned> ValueRankMap;
  OrderedSet RedoInsts;

  // Arbitrary, but prevents quadratic behavior.
  static const unsigned GlobalReassociateLimit = 10;
  static const unsigned NumBinaryOps =
      Instruction::BinaryOpsEnd - Instruction::BinaryOpsBegin;

  struct PairMapValue {
    WeakVH Value1;
    WeakVH Value2;
    unsigned Score;
    bool isValid() const { return Value1 && Value2; }
  };
  DenseMap<std::pair<Value *, Value *>, PairMapValue> PairMap[NumBinaryOps];

  bool MadeChange;

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);

private:
  void BuildRankMap(Function &F, ReversePostOrderTraversal<Function *> &RPOT);
  unsigned getRank(Value *V);
  void canonicalizeOperands(Instruction *I);
  void ReassociateExpression(BinaryOperator *I);
  void RewriteExprTree(BinaryOperator *I,
                       SmallVectorImpl<reassociate::ValueEntry> &Ops,
                       reassociate::OverflowTracking Flags);
  Value *OptimizeExpression(BinaryOperator *I,
                            SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *OptimizeAdd(Instruction *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *OptimizeXor(Instruction *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  bool CombineXorOpnd(BasicBlock::iterator It, reassociate::XorOpnd *Opnd1,
                      APInt &ConstOpnd, Value *&Res);
  bool CombineXorOpnd(BasicBlock::iterator It, reassociate::XorOpnd *Opnd1,
                      reassociate::XorOpnd *Opnd2, APInt &ConstOpnd,
                      Value *&Res);
  Value *buildMinimalMultiplyDAG(IRBuilderBase &Builder,
                                 SmallVectorImpl<reassociate::Factor> &Factors);
  Value *OptimizeMul(BinaryOperator *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *RemoveFactorFromExpression(Value *V, Value *Factor);
  void EraseInst(Instruction *I);
  void RecursivelyEraseDeadInsts(Instruction *I, OrderedSet &Insts);
  void OptimizeInst(Instruction *I);
  Instruction *canonicalizeNegFPConstantsForOp(Instruction *I, Instruction *Op,
                                               Value *OtherOp);
  Instruction *canonicalizeNegFPConstants(Instruction *I);
  void BuildPairMap(ReversePostOrderTraversal<Function *> &RPOT);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H
