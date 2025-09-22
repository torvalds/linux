//===- Reassociate.cpp - Reassociate binary expressions -------------------===//
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

#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <utility>

using namespace llvm;
using namespace reassociate;
using namespace PatternMatch;

#define DEBUG_TYPE "reassociate"

STATISTIC(NumChanged, "Number of insts reassociated");
STATISTIC(NumAnnihil, "Number of expr tree annihilated");
STATISTIC(NumFactor , "Number of multiplies factored");

static cl::opt<bool>
    UseCSELocalOpt(DEBUG_TYPE "-use-cse-local",
                   cl::desc("Only reorder expressions within a basic block "
                            "when exposing CSE opportunities"),
                   cl::init(true), cl::Hidden);

#ifndef NDEBUG
/// Print out the expression identified in the Ops list.
static void PrintOps(Instruction *I, const SmallVectorImpl<ValueEntry> &Ops) {
  Module *M = I->getModule();
  dbgs() << Instruction::getOpcodeName(I->getOpcode()) << " "
       << *Ops[0].Op->getType() << '\t';
  for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
    dbgs() << "[ ";
    Ops[i].Op->printAsOperand(dbgs(), false, M);
    dbgs() << ", #" << Ops[i].Rank << "] ";
  }
}
#endif

/// Utility class representing a non-constant Xor-operand. We classify
/// non-constant Xor-Operands into two categories:
///  C1) The operand is in the form "X & C", where C is a constant and C != ~0
///  C2)
///    C2.1) The operand is in the form of "X | C", where C is a non-zero
///          constant.
///    C2.2) Any operand E which doesn't fall into C1 and C2.1, we view this
///          operand as "E | 0"
class llvm::reassociate::XorOpnd {
public:
  XorOpnd(Value *V);

  bool isInvalid() const { return SymbolicPart == nullptr; }
  bool isOrExpr() const { return isOr; }
  Value *getValue() const { return OrigVal; }
  Value *getSymbolicPart() const { return SymbolicPart; }
  unsigned getSymbolicRank() const { return SymbolicRank; }
  const APInt &getConstPart() const { return ConstPart; }

  void Invalidate() { SymbolicPart = OrigVal = nullptr; }
  void setSymbolicRank(unsigned R) { SymbolicRank = R; }

private:
  Value *OrigVal;
  Value *SymbolicPart;
  APInt ConstPart;
  unsigned SymbolicRank;
  bool isOr;
};

XorOpnd::XorOpnd(Value *V) {
  assert(!isa<ConstantInt>(V) && "No ConstantInt");
  OrigVal = V;
  Instruction *I = dyn_cast<Instruction>(V);
  SymbolicRank = 0;

  if (I && (I->getOpcode() == Instruction::Or ||
            I->getOpcode() == Instruction::And)) {
    Value *V0 = I->getOperand(0);
    Value *V1 = I->getOperand(1);
    const APInt *C;
    if (match(V0, m_APInt(C)))
      std::swap(V0, V1);

    if (match(V1, m_APInt(C))) {
      ConstPart = *C;
      SymbolicPart = V0;
      isOr = (I->getOpcode() == Instruction::Or);
      return;
    }
  }

  // view the operand as "V | 0"
  SymbolicPart = V;
  ConstPart = APInt::getZero(V->getType()->getScalarSizeInBits());
  isOr = true;
}

/// Return true if I is an instruction with the FastMathFlags that are needed
/// for general reassociation set.  This is not the same as testing
/// Instruction::isAssociative() because it includes operations like fsub.
/// (This routine is only intended to be called for floating-point operations.)
static bool hasFPAssociativeFlags(Instruction *I) {
  assert(I && isa<FPMathOperator>(I) && "Should only check FP ops");
  return I->hasAllowReassoc() && I->hasNoSignedZeros();
}

/// Return true if V is an instruction of the specified opcode and if it
/// only has one use.
static BinaryOperator *isReassociableOp(Value *V, unsigned Opcode) {
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (BO && BO->hasOneUse() && BO->getOpcode() == Opcode)
    if (!isa<FPMathOperator>(BO) || hasFPAssociativeFlags(BO))
      return BO;
  return nullptr;
}

static BinaryOperator *isReassociableOp(Value *V, unsigned Opcode1,
                                        unsigned Opcode2) {
  auto *BO = dyn_cast<BinaryOperator>(V);
  if (BO && BO->hasOneUse() &&
      (BO->getOpcode() == Opcode1 || BO->getOpcode() == Opcode2))
    if (!isa<FPMathOperator>(BO) || hasFPAssociativeFlags(BO))
      return BO;
  return nullptr;
}

void ReassociatePass::BuildRankMap(Function &F,
                                   ReversePostOrderTraversal<Function*> &RPOT) {
  unsigned Rank = 2;

  // Assign distinct ranks to function arguments.
  for (auto &Arg : F.args()) {
    ValueRankMap[&Arg] = ++Rank;
    LLVM_DEBUG(dbgs() << "Calculated Rank[" << Arg.getName() << "] = " << Rank
                      << "\n");
  }

  // Traverse basic blocks in ReversePostOrder.
  for (BasicBlock *BB : RPOT) {
    unsigned BBRank = RankMap[BB] = ++Rank << 16;

    // Walk the basic block, adding precomputed ranks for any instructions that
    // we cannot move.  This ensures that the ranks for these instructions are
    // all different in the block.
    for (Instruction &I : *BB)
      if (mayHaveNonDefUseDependency(I))
        ValueRankMap[&I] = ++BBRank;
  }
}

unsigned ReassociatePass::getRank(Value *V) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) {
    if (isa<Argument>(V)) return ValueRankMap[V];   // Function argument.
    return 0;  // Otherwise it's a global or constant, rank 0.
  }

  if (unsigned Rank = ValueRankMap[I])
    return Rank;    // Rank already known?

  // If this is an expression, return the 1+MAX(rank(LHS), rank(RHS)) so that
  // we can reassociate expressions for code motion!  Since we do not recurse
  // for PHI nodes, we cannot have infinite recursion here, because there
  // cannot be loops in the value graph that do not go through PHI nodes.
  unsigned Rank = 0, MaxRank = RankMap[I->getParent()];
  for (unsigned i = 0, e = I->getNumOperands(); i != e && Rank != MaxRank; ++i)
    Rank = std::max(Rank, getRank(I->getOperand(i)));

  // If this is a 'not' or 'neg' instruction, do not count it for rank. This
  // assures us that X and ~X will have the same rank.
  if (!match(I, m_Not(m_Value())) && !match(I, m_Neg(m_Value())) &&
      !match(I, m_FNeg(m_Value())))
    ++Rank;

  LLVM_DEBUG(dbgs() << "Calculated Rank[" << V->getName() << "] = " << Rank
                    << "\n");

  return ValueRankMap[I] = Rank;
}

// Canonicalize constants to RHS.  Otherwise, sort the operands by rank.
void ReassociatePass::canonicalizeOperands(Instruction *I) {
  assert(isa<BinaryOperator>(I) && "Expected binary operator.");
  assert(I->isCommutative() && "Expected commutative operator.");

  Value *LHS = I->getOperand(0);
  Value *RHS = I->getOperand(1);
  if (LHS == RHS || isa<Constant>(RHS))
    return;
  if (isa<Constant>(LHS) || getRank(RHS) < getRank(LHS))
    cast<BinaryOperator>(I)->swapOperands();
}

static BinaryOperator *CreateAdd(Value *S1, Value *S2, const Twine &Name,
                                 BasicBlock::iterator InsertBefore,
                                 Value *FlagsOp) {
  if (S1->getType()->isIntOrIntVectorTy())
    return BinaryOperator::CreateAdd(S1, S2, Name, InsertBefore);
  else {
    BinaryOperator *Res =
        BinaryOperator::CreateFAdd(S1, S2, Name, InsertBefore);
    Res->setFastMathFlags(cast<FPMathOperator>(FlagsOp)->getFastMathFlags());
    return Res;
  }
}

static BinaryOperator *CreateMul(Value *S1, Value *S2, const Twine &Name,
                                 BasicBlock::iterator InsertBefore,
                                 Value *FlagsOp) {
  if (S1->getType()->isIntOrIntVectorTy())
    return BinaryOperator::CreateMul(S1, S2, Name, InsertBefore);
  else {
    BinaryOperator *Res =
      BinaryOperator::CreateFMul(S1, S2, Name, InsertBefore);
    Res->setFastMathFlags(cast<FPMathOperator>(FlagsOp)->getFastMathFlags());
    return Res;
  }
}

static Instruction *CreateNeg(Value *S1, const Twine &Name,
                              BasicBlock::iterator InsertBefore,
                              Value *FlagsOp) {
  if (S1->getType()->isIntOrIntVectorTy())
    return BinaryOperator::CreateNeg(S1, Name, InsertBefore);

  if (auto *FMFSource = dyn_cast<Instruction>(FlagsOp))
    return UnaryOperator::CreateFNegFMF(S1, FMFSource, Name, InsertBefore);

  return UnaryOperator::CreateFNeg(S1, Name, InsertBefore);
}

/// Replace 0-X with X*-1.
static BinaryOperator *LowerNegateToMultiply(Instruction *Neg) {
  assert((isa<UnaryOperator>(Neg) || isa<BinaryOperator>(Neg)) &&
         "Expected a Negate!");
  // FIXME: It's not safe to lower a unary FNeg into a FMul by -1.0.
  unsigned OpNo = isa<BinaryOperator>(Neg) ? 1 : 0;
  Type *Ty = Neg->getType();
  Constant *NegOne = Ty->isIntOrIntVectorTy() ?
    ConstantInt::getAllOnesValue(Ty) : ConstantFP::get(Ty, -1.0);

  BinaryOperator *Res =
      CreateMul(Neg->getOperand(OpNo), NegOne, "", Neg->getIterator(), Neg);
  Neg->setOperand(OpNo, Constant::getNullValue(Ty)); // Drop use of op.
  Res->takeName(Neg);
  Neg->replaceAllUsesWith(Res);
  Res->setDebugLoc(Neg->getDebugLoc());
  return Res;
}

using RepeatedValue = std::pair<Value *, uint64_t>;

/// Given an associative binary expression, return the leaf
/// nodes in Ops along with their weights (how many times the leaf occurs).  The
/// original expression is the same as
///   (Ops[0].first op Ops[0].first op ... Ops[0].first)  <- Ops[0].second times
/// op
///   (Ops[1].first op Ops[1].first op ... Ops[1].first)  <- Ops[1].second times
/// op
///   ...
/// op
///   (Ops[N].first op Ops[N].first op ... Ops[N].first)  <- Ops[N].second times
///
/// Note that the values Ops[0].first, ..., Ops[N].first are all distinct.
///
/// This routine may modify the function, in which case it returns 'true'.  The
/// changes it makes may well be destructive, changing the value computed by 'I'
/// to something completely different.  Thus if the routine returns 'true' then
/// you MUST either replace I with a new expression computed from the Ops array,
/// or use RewriteExprTree to put the values back in.
///
/// A leaf node is either not a binary operation of the same kind as the root
/// node 'I' (i.e. is not a binary operator at all, or is, but with a different
/// opcode), or is the same kind of binary operator but has a use which either
/// does not belong to the expression, or does belong to the expression but is
/// a leaf node.  Every leaf node has at least one use that is a non-leaf node
/// of the expression, while for non-leaf nodes (except for the root 'I') every
/// use is a non-leaf node of the expression.
///
/// For example:
///           expression graph        node names
///
///                     +        |        I
///                    / \       |
///                   +   +      |      A,  B
///                  / \ / \     |
///                 *   +   *    |    C,  D,  E
///                / \ / \ / \   |
///                   +   *      |      F,  G
///
/// The leaf nodes are C, E, F and G.  The Ops array will contain (maybe not in
/// that order) (C, 1), (E, 1), (F, 2), (G, 2).
///
/// The expression is maximal: if some instruction is a binary operator of the
/// same kind as 'I', and all of its uses are non-leaf nodes of the expression,
/// then the instruction also belongs to the expression, is not a leaf node of
/// it, and its operands also belong to the expression (but may be leaf nodes).
///
/// NOTE: This routine will set operands of non-leaf non-root nodes to undef in
/// order to ensure that every non-root node in the expression has *exactly one*
/// use by a non-leaf node of the expression.  This destruction means that the
/// caller MUST either replace 'I' with a new expression or use something like
/// RewriteExprTree to put the values back in if the routine indicates that it
/// made a change by returning 'true'.
///
/// In the above example either the right operand of A or the left operand of B
/// will be replaced by undef.  If it is B's operand then this gives:
///
///                     +        |        I
///                    / \       |
///                   +   +      |      A,  B - operand of B replaced with undef
///                  / \   \     |
///                 *   +   *    |    C,  D,  E
///                / \ / \ / \   |
///                   +   *      |      F,  G
///
/// Note that such undef operands can only be reached by passing through 'I'.
/// For example, if you visit operands recursively starting from a leaf node
/// then you will never see such an undef operand unless you get back to 'I',
/// which requires passing through a phi node.
///
/// Note that this routine may also mutate binary operators of the wrong type
/// that have all uses inside the expression (i.e. only used by non-leaf nodes
/// of the expression) if it can turn them into binary operators of the right
/// type and thus make the expression bigger.
static bool LinearizeExprTree(Instruction *I,
                              SmallVectorImpl<RepeatedValue> &Ops,
                              ReassociatePass::OrderedSet &ToRedo,
                              reassociate::OverflowTracking &Flags) {
  assert((isa<UnaryOperator>(I) || isa<BinaryOperator>(I)) &&
         "Expected a UnaryOperator or BinaryOperator!");
  LLVM_DEBUG(dbgs() << "LINEARIZE: " << *I << '\n');
  unsigned Opcode = I->getOpcode();
  assert(I->isAssociative() && I->isCommutative() &&
         "Expected an associative and commutative operation!");

  // Visit all operands of the expression, keeping track of their weight (the
  // number of paths from the expression root to the operand, or if you like
  // the number of times that operand occurs in the linearized expression).
  // For example, if I = X + A, where X = A + B, then I, X and B have weight 1
  // while A has weight two.

  // Worklist of non-leaf nodes (their operands are in the expression too) along
  // with their weights, representing a certain number of paths to the operator.
  // If an operator occurs in the worklist multiple times then we found multiple
  // ways to get to it.
  SmallVector<std::pair<Instruction *, uint64_t>, 8> Worklist; // (Op, Weight)
  Worklist.push_back(std::make_pair(I, 1));
  bool Changed = false;

  // Leaves of the expression are values that either aren't the right kind of
  // operation (eg: a constant, or a multiply in an add tree), or are, but have
  // some uses that are not inside the expression.  For example, in I = X + X,
  // X = A + B, the value X has two uses (by I) that are in the expression.  If
  // X has any other uses, for example in a return instruction, then we consider
  // X to be a leaf, and won't analyze it further.  When we first visit a value,
  // if it has more than one use then at first we conservatively consider it to
  // be a leaf.  Later, as the expression is explored, we may discover some more
  // uses of the value from inside the expression.  If all uses turn out to be
  // from within the expression (and the value is a binary operator of the right
  // kind) then the value is no longer considered to be a leaf, and its operands
  // are explored.

  // Leaves - Keeps track of the set of putative leaves as well as the number of
  // paths to each leaf seen so far.
  using LeafMap = DenseMap<Value *, uint64_t>;
  LeafMap Leaves; // Leaf -> Total weight so far.
  SmallVector<Value *, 8> LeafOrder; // Ensure deterministic leaf output order.
  const DataLayout DL = I->getDataLayout();

#ifndef NDEBUG
  SmallPtrSet<Value *, 8> Visited; // For checking the iteration scheme.
#endif
  while (!Worklist.empty()) {
    // We examine the operands of this binary operator.
    auto [I, Weight] = Worklist.pop_back_val();

    if (isa<OverflowingBinaryOperator>(I)) {
      Flags.HasNUW &= I->hasNoUnsignedWrap();
      Flags.HasNSW &= I->hasNoSignedWrap();
    }

    for (unsigned OpIdx = 0; OpIdx < I->getNumOperands(); ++OpIdx) { // Visit operands.
      Value *Op = I->getOperand(OpIdx);
      LLVM_DEBUG(dbgs() << "OPERAND: " << *Op << " (" << Weight << ")\n");
      assert(!Op->use_empty() && "No uses, so how did we get to it?!");

      // If this is a binary operation of the right kind with only one use then
      // add its operands to the expression.
      if (BinaryOperator *BO = isReassociableOp(Op, Opcode)) {
        assert(Visited.insert(Op).second && "Not first visit!");
        LLVM_DEBUG(dbgs() << "DIRECT ADD: " << *Op << " (" << Weight << ")\n");
        Worklist.push_back(std::make_pair(BO, Weight));
        continue;
      }

      // Appears to be a leaf.  Is the operand already in the set of leaves?
      LeafMap::iterator It = Leaves.find(Op);
      if (It == Leaves.end()) {
        // Not in the leaf map.  Must be the first time we saw this operand.
        assert(Visited.insert(Op).second && "Not first visit!");
        if (!Op->hasOneUse()) {
          // This value has uses not accounted for by the expression, so it is
          // not safe to modify.  Mark it as being a leaf.
          LLVM_DEBUG(dbgs()
                     << "ADD USES LEAF: " << *Op << " (" << Weight << ")\n");
          LeafOrder.push_back(Op);
          Leaves[Op] = Weight;
          continue;
        }
        // No uses outside the expression, try morphing it.
      } else {
        // Already in the leaf map.
        assert(It != Leaves.end() && Visited.count(Op) &&
               "In leaf map but not visited!");

        // Update the number of paths to the leaf.
        It->second += Weight;
        assert(It->second >= Weight && "Weight overflows");

        // If we still have uses that are not accounted for by the expression
        // then it is not safe to modify the value.
        if (!Op->hasOneUse())
          continue;

        // No uses outside the expression, try morphing it.
        Weight = It->second;
        Leaves.erase(It); // Since the value may be morphed below.
      }

      // At this point we have a value which, first of all, is not a binary
      // expression of the right kind, and secondly, is only used inside the
      // expression.  This means that it can safely be modified.  See if we
      // can usefully morph it into an expression of the right kind.
      assert((!isa<Instruction>(Op) ||
              cast<Instruction>(Op)->getOpcode() != Opcode
              || (isa<FPMathOperator>(Op) &&
                  !hasFPAssociativeFlags(cast<Instruction>(Op)))) &&
             "Should have been handled above!");
      assert(Op->hasOneUse() && "Has uses outside the expression tree!");

      // If this is a multiply expression, turn any internal negations into
      // multiplies by -1 so they can be reassociated.  Add any users of the
      // newly created multiplication by -1 to the redo list, so any
      // reassociation opportunities that are exposed will be reassociated
      // further.
      Instruction *Neg;
      if (((Opcode == Instruction::Mul && match(Op, m_Neg(m_Value()))) ||
           (Opcode == Instruction::FMul && match(Op, m_FNeg(m_Value())))) &&
           match(Op, m_Instruction(Neg))) {
        LLVM_DEBUG(dbgs()
                   << "MORPH LEAF: " << *Op << " (" << Weight << ") TO ");
        Instruction *Mul = LowerNegateToMultiply(Neg);
        LLVM_DEBUG(dbgs() << *Mul << '\n');
        Worklist.push_back(std::make_pair(Mul, Weight));
        for (User *U : Mul->users()) {
          if (BinaryOperator *UserBO = dyn_cast<BinaryOperator>(U))
            ToRedo.insert(UserBO);
        }
        ToRedo.insert(Neg);
        Changed = true;
        continue;
      }

      // Failed to morph into an expression of the right type.  This really is
      // a leaf.
      LLVM_DEBUG(dbgs() << "ADD LEAF: " << *Op << " (" << Weight << ")\n");
      assert(!isReassociableOp(Op, Opcode) && "Value was morphed?");
      LeafOrder.push_back(Op);
      Leaves[Op] = Weight;
    }
  }

  // The leaves, repeated according to their weights, represent the linearized
  // form of the expression.
  for (Value *V : LeafOrder) {
    LeafMap::iterator It = Leaves.find(V);
    if (It == Leaves.end())
      // Node initially thought to be a leaf wasn't.
      continue;
    assert(!isReassociableOp(V, Opcode) && "Shouldn't be a leaf!");
    uint64_t Weight = It->second;
    // Ensure the leaf is only output once.
    It->second = 0;
    Ops.push_back(std::make_pair(V, Weight));
    if (Opcode == Instruction::Add && Flags.AllKnownNonNegative && Flags.HasNSW)
      Flags.AllKnownNonNegative &= isKnownNonNegative(V, SimplifyQuery(DL));
    else if (Opcode == Instruction::Mul) {
      // To preserve NUW we need all inputs non-zero.
      // To preserve NSW we need all inputs strictly positive.
      if (Flags.AllKnownNonZero &&
          (Flags.HasNUW || (Flags.HasNSW && Flags.AllKnownNonNegative))) {
        Flags.AllKnownNonZero &= isKnownNonZero(V, SimplifyQuery(DL));
        if (Flags.HasNSW && Flags.AllKnownNonNegative)
          Flags.AllKnownNonNegative &= isKnownNonNegative(V, SimplifyQuery(DL));
      }
    }
  }

  // For nilpotent operations or addition there may be no operands, for example
  // because the expression was "X xor X" or consisted of 2^Bitwidth additions:
  // in both cases the weight reduces to 0 causing the value to be skipped.
  if (Ops.empty()) {
    Constant *Identity = ConstantExpr::getBinOpIdentity(Opcode, I->getType());
    assert(Identity && "Associative operation without identity!");
    Ops.emplace_back(Identity, 1);
  }

  return Changed;
}

/// Now that the operands for this expression tree are
/// linearized and optimized, emit them in-order.
void ReassociatePass::RewriteExprTree(BinaryOperator *I,
                                      SmallVectorImpl<ValueEntry> &Ops,
                                      OverflowTracking Flags) {
  assert(Ops.size() > 1 && "Single values should be used directly!");

  // Since our optimizations should never increase the number of operations, the
  // new expression can usually be written reusing the existing binary operators
  // from the original expression tree, without creating any new instructions,
  // though the rewritten expression may have a completely different topology.
  // We take care to not change anything if the new expression will be the same
  // as the original.  If more than trivial changes (like commuting operands)
  // were made then we are obliged to clear out any optional subclass data like
  // nsw flags.

  /// NodesToRewrite - Nodes from the original expression available for writing
  /// the new expression into.
  SmallVector<BinaryOperator*, 8> NodesToRewrite;
  unsigned Opcode = I->getOpcode();
  BinaryOperator *Op = I;

  /// NotRewritable - The operands being written will be the leaves of the new
  /// expression and must not be used as inner nodes (via NodesToRewrite) by
  /// mistake.  Inner nodes are always reassociable, and usually leaves are not
  /// (if they were they would have been incorporated into the expression and so
  /// would not be leaves), so most of the time there is no danger of this.  But
  /// in rare cases a leaf may become reassociable if an optimization kills uses
  /// of it, or it may momentarily become reassociable during rewriting (below)
  /// due it being removed as an operand of one of its uses.  Ensure that misuse
  /// of leaf nodes as inner nodes cannot occur by remembering all of the future
  /// leaves and refusing to reuse any of them as inner nodes.
  SmallPtrSet<Value*, 8> NotRewritable;
  for (const ValueEntry &Op : Ops)
    NotRewritable.insert(Op.Op);

  // ExpressionChangedStart - Non-null if the rewritten expression differs from
  // the original in some non-trivial way, requiring the clearing of optional
  // flags. Flags are cleared from the operator in ExpressionChangedStart up to
  // ExpressionChangedEnd inclusive.
  BinaryOperator *ExpressionChangedStart = nullptr,
                 *ExpressionChangedEnd = nullptr;
  for (unsigned i = 0; ; ++i) {
    // The last operation (which comes earliest in the IR) is special as both
    // operands will come from Ops, rather than just one with the other being
    // a subexpression.
    if (i+2 == Ops.size()) {
      Value *NewLHS = Ops[i].Op;
      Value *NewRHS = Ops[i+1].Op;
      Value *OldLHS = Op->getOperand(0);
      Value *OldRHS = Op->getOperand(1);

      if (NewLHS == OldLHS && NewRHS == OldRHS)
        // Nothing changed, leave it alone.
        break;

      if (NewLHS == OldRHS && NewRHS == OldLHS) {
        // The order of the operands was reversed.  Swap them.
        LLVM_DEBUG(dbgs() << "RA: " << *Op << '\n');
        Op->swapOperands();
        LLVM_DEBUG(dbgs() << "TO: " << *Op << '\n');
        MadeChange = true;
        ++NumChanged;
        break;
      }

      // The new operation differs non-trivially from the original. Overwrite
      // the old operands with the new ones.
      LLVM_DEBUG(dbgs() << "RA: " << *Op << '\n');
      if (NewLHS != OldLHS) {
        BinaryOperator *BO = isReassociableOp(OldLHS, Opcode);
        if (BO && !NotRewritable.count(BO))
          NodesToRewrite.push_back(BO);
        Op->setOperand(0, NewLHS);
      }
      if (NewRHS != OldRHS) {
        BinaryOperator *BO = isReassociableOp(OldRHS, Opcode);
        if (BO && !NotRewritable.count(BO))
          NodesToRewrite.push_back(BO);
        Op->setOperand(1, NewRHS);
      }
      LLVM_DEBUG(dbgs() << "TO: " << *Op << '\n');

      ExpressionChangedStart = Op;
      if (!ExpressionChangedEnd)
        ExpressionChangedEnd = Op;
      MadeChange = true;
      ++NumChanged;

      break;
    }

    // Not the last operation.  The left-hand side will be a sub-expression
    // while the right-hand side will be the current element of Ops.
    Value *NewRHS = Ops[i].Op;
    if (NewRHS != Op->getOperand(1)) {
      LLVM_DEBUG(dbgs() << "RA: " << *Op << '\n');
      if (NewRHS == Op->getOperand(0)) {
        // The new right-hand side was already present as the left operand.  If
        // we are lucky then swapping the operands will sort out both of them.
        Op->swapOperands();
      } else {
        // Overwrite with the new right-hand side.
        BinaryOperator *BO = isReassociableOp(Op->getOperand(1), Opcode);
        if (BO && !NotRewritable.count(BO))
          NodesToRewrite.push_back(BO);
        Op->setOperand(1, NewRHS);
        ExpressionChangedStart = Op;
        if (!ExpressionChangedEnd)
          ExpressionChangedEnd = Op;
      }
      LLVM_DEBUG(dbgs() << "TO: " << *Op << '\n');
      MadeChange = true;
      ++NumChanged;
    }

    // Now deal with the left-hand side.  If this is already an operation node
    // from the original expression then just rewrite the rest of the expression
    // into it.
    BinaryOperator *BO = isReassociableOp(Op->getOperand(0), Opcode);
    if (BO && !NotRewritable.count(BO)) {
      Op = BO;
      continue;
    }

    // Otherwise, grab a spare node from the original expression and use that as
    // the left-hand side.  If there are no nodes left then the optimizers made
    // an expression with more nodes than the original!  This usually means that
    // they did something stupid but it might mean that the problem was just too
    // hard (finding the mimimal number of multiplications needed to realize a
    // multiplication expression is NP-complete).  Whatever the reason, smart or
    // stupid, create a new node if there are none left.
    BinaryOperator *NewOp;
    if (NodesToRewrite.empty()) {
      Constant *Poison = PoisonValue::get(I->getType());
      NewOp = BinaryOperator::Create(Instruction::BinaryOps(Opcode), Poison,
                                     Poison, "", I->getIterator());
      if (isa<FPMathOperator>(NewOp))
        NewOp->setFastMathFlags(I->getFastMathFlags());
    } else {
      NewOp = NodesToRewrite.pop_back_val();
    }

    LLVM_DEBUG(dbgs() << "RA: " << *Op << '\n');
    Op->setOperand(0, NewOp);
    LLVM_DEBUG(dbgs() << "TO: " << *Op << '\n');
    ExpressionChangedStart = Op;
    if (!ExpressionChangedEnd)
      ExpressionChangedEnd = Op;
    MadeChange = true;
    ++NumChanged;
    Op = NewOp;
  }

  // If the expression changed non-trivially then clear out all subclass data
  // starting from the operator specified in ExpressionChanged, and compactify
  // the operators to just before the expression root to guarantee that the
  // expression tree is dominated by all of Ops.
  if (ExpressionChangedStart) {
    bool ClearFlags = true;
    do {
      // Preserve flags.
      if (ClearFlags) {
        if (isa<FPMathOperator>(I)) {
          FastMathFlags Flags = I->getFastMathFlags();
          ExpressionChangedStart->clearSubclassOptionalData();
          ExpressionChangedStart->setFastMathFlags(Flags);
        } else {
          ExpressionChangedStart->clearSubclassOptionalData();
          if (ExpressionChangedStart->getOpcode() == Instruction::Add ||
              (ExpressionChangedStart->getOpcode() == Instruction::Mul &&
               Flags.AllKnownNonZero)) {
            if (Flags.HasNUW)
              ExpressionChangedStart->setHasNoUnsignedWrap();
            if (Flags.HasNSW && (Flags.AllKnownNonNegative || Flags.HasNUW))
              ExpressionChangedStart->setHasNoSignedWrap();
          }
        }
      }

      if (ExpressionChangedStart == ExpressionChangedEnd)
        ClearFlags = false;
      if (ExpressionChangedStart == I)
        break;

      // Discard any debug info related to the expressions that has changed (we
      // can leave debug info related to the root and any operation that didn't
      // change, since the result of the expression tree should be the same
      // even after reassociation).
      if (ClearFlags)
        replaceDbgUsesWithUndef(ExpressionChangedStart);

      ExpressionChangedStart->moveBefore(I);
      ExpressionChangedStart =
          cast<BinaryOperator>(*ExpressionChangedStart->user_begin());
    } while (true);
  }

  // Throw away any left over nodes from the original expression.
  for (BinaryOperator *BO : NodesToRewrite)
    RedoInsts.insert(BO);
}

/// Insert instructions before the instruction pointed to by BI,
/// that computes the negative version of the value specified.  The negative
/// version of the value is returned, and BI is left pointing at the instruction
/// that should be processed next by the reassociation pass.
/// Also add intermediate instructions to the redo list that are modified while
/// pushing the negates through adds.  These will be revisited to see if
/// additional opportunities have been exposed.
static Value *NegateValue(Value *V, Instruction *BI,
                          ReassociatePass::OrderedSet &ToRedo) {
  if (auto *C = dyn_cast<Constant>(V)) {
    const DataLayout &DL = BI->getDataLayout();
    Constant *Res = C->getType()->isFPOrFPVectorTy()
                        ? ConstantFoldUnaryOpOperand(Instruction::FNeg, C, DL)
                        : ConstantExpr::getNeg(C);
    if (Res)
      return Res;
  }

  // We are trying to expose opportunity for reassociation.  One of the things
  // that we want to do to achieve this is to push a negation as deep into an
  // expression chain as possible, to expose the add instructions.  In practice,
  // this means that we turn this:
  //   X = -(A+12+C+D)   into    X = -A + -12 + -C + -D = -12 + -A + -C + -D
  // so that later, a: Y = 12+X could get reassociated with the -12 to eliminate
  // the constants.  We assume that instcombine will clean up the mess later if
  // we introduce tons of unnecessary negation instructions.
  //
  if (BinaryOperator *I =
          isReassociableOp(V, Instruction::Add, Instruction::FAdd)) {
    // Push the negates through the add.
    I->setOperand(0, NegateValue(I->getOperand(0), BI, ToRedo));
    I->setOperand(1, NegateValue(I->getOperand(1), BI, ToRedo));
    if (I->getOpcode() == Instruction::Add) {
      I->setHasNoUnsignedWrap(false);
      I->setHasNoSignedWrap(false);
    }

    // We must move the add instruction here, because the neg instructions do
    // not dominate the old add instruction in general.  By moving it, we are
    // assured that the neg instructions we just inserted dominate the
    // instruction we are about to insert after them.
    //
    I->moveBefore(BI);
    I->setName(I->getName()+".neg");

    // Add the intermediate negates to the redo list as processing them later
    // could expose more reassociating opportunities.
    ToRedo.insert(I);
    return I;
  }

  // Okay, we need to materialize a negated version of V with an instruction.
  // Scan the use lists of V to see if we have one already.
  for (User *U : V->users()) {
    if (!match(U, m_Neg(m_Value())) && !match(U, m_FNeg(m_Value())))
      continue;

    // We found one!  Now we have to make sure that the definition dominates
    // this use.  We do this by moving it to the entry block (if it is a
    // non-instruction value) or right after the definition.  These negates will
    // be zapped by reassociate later, so we don't need much finesse here.
    Instruction *TheNeg = dyn_cast<Instruction>(U);

    // We can't safely propagate a vector zero constant with poison/undef lanes.
    Constant *C;
    if (match(TheNeg, m_BinOp(m_Constant(C), m_Value())) &&
        C->containsUndefOrPoisonElement())
      continue;

    // Verify that the negate is in this function, V might be a constant expr.
    if (!TheNeg ||
        TheNeg->getParent()->getParent() != BI->getParent()->getParent())
      continue;

    BasicBlock::iterator InsertPt;
    if (Instruction *InstInput = dyn_cast<Instruction>(V)) {
      auto InsertPtOpt = InstInput->getInsertionPointAfterDef();
      if (!InsertPtOpt)
        continue;
      InsertPt = *InsertPtOpt;
    } else {
      InsertPt = TheNeg->getFunction()
                     ->getEntryBlock()
                     .getFirstNonPHIOrDbg()
                     ->getIterator();
    }

    // Check that if TheNeg is moved out of its parent block, we drop its
    // debug location to avoid extra coverage.
    // See test dropping_debugloc_the_neg.ll for a detailed example.
    if (TheNeg->getParent() != InsertPt->getParent())
      TheNeg->dropLocation();
    TheNeg->moveBefore(*InsertPt->getParent(), InsertPt);

    if (TheNeg->getOpcode() == Instruction::Sub) {
      TheNeg->setHasNoUnsignedWrap(false);
      TheNeg->setHasNoSignedWrap(false);
    } else {
      TheNeg->andIRFlags(BI);
    }
    ToRedo.insert(TheNeg);
    return TheNeg;
  }

  // Insert a 'neg' instruction that subtracts the value from zero to get the
  // negation.
  Instruction *NewNeg =
      CreateNeg(V, V->getName() + ".neg", BI->getIterator(), BI);
  ToRedo.insert(NewNeg);
  return NewNeg;
}

// See if this `or` looks like an load widening reduction, i.e. that it
// consists of an `or`/`shl`/`zext`/`load` nodes only. Note that we don't
// ensure that the pattern is *really* a load widening reduction,
// we do not ensure that it can really be replaced with a widened load,
// only that it mostly looks like one.
static bool isLoadCombineCandidate(Instruction *Or) {
  SmallVector<Instruction *, 8> Worklist;
  SmallSet<Instruction *, 8> Visited;

  auto Enqueue = [&](Value *V) {
    auto *I = dyn_cast<Instruction>(V);
    // Each node of an `or` reduction must be an instruction,
    if (!I)
      return false; // Node is certainly not part of an `or` load reduction.
    // Only process instructions we have never processed before.
    if (Visited.insert(I).second)
      Worklist.emplace_back(I);
    return true; // Will need to look at parent nodes.
  };

  if (!Enqueue(Or))
    return false; // Not an `or` reduction pattern.

  while (!Worklist.empty()) {
    auto *I = Worklist.pop_back_val();

    // Okay, which instruction is this node?
    switch (I->getOpcode()) {
    case Instruction::Or:
      // Got an `or` node. That's fine, just recurse into it's operands.
      for (Value *Op : I->operands())
        if (!Enqueue(Op))
          return false; // Not an `or` reduction pattern.
      continue;

    case Instruction::Shl:
    case Instruction::ZExt:
      // `shl`/`zext` nodes are fine, just recurse into their base operand.
      if (!Enqueue(I->getOperand(0)))
        return false; // Not an `or` reduction pattern.
      continue;

    case Instruction::Load:
      // Perfect, `load` node means we've reached an edge of the graph.
      continue;

    default:        // Unknown node.
      return false; // Not an `or` reduction pattern.
    }
  }

  return true;
}

/// Return true if it may be profitable to convert this (X|Y) into (X+Y).
static bool shouldConvertOrWithNoCommonBitsToAdd(Instruction *Or) {
  // Don't bother to convert this up unless either the LHS is an associable add
  // or subtract or mul or if this is only used by one of the above.
  // This is only a compile-time improvement, it is not needed for correctness!
  auto isInteresting = [](Value *V) {
    for (auto Op : {Instruction::Add, Instruction::Sub, Instruction::Mul,
                    Instruction::Shl})
      if (isReassociableOp(V, Op))
        return true;
    return false;
  };

  if (any_of(Or->operands(), isInteresting))
    return true;

  Value *VB = Or->user_back();
  if (Or->hasOneUse() && isInteresting(VB))
    return true;

  return false;
}

/// If we have (X|Y), and iff X and Y have no common bits set,
/// transform this into (X+Y) to allow arithmetics reassociation.
static BinaryOperator *convertOrWithNoCommonBitsToAdd(Instruction *Or) {
  // Convert an or into an add.
  BinaryOperator *New = CreateAdd(Or->getOperand(0), Or->getOperand(1), "",
                                  Or->getIterator(), Or);
  New->setHasNoSignedWrap();
  New->setHasNoUnsignedWrap();
  New->takeName(Or);

  // Everyone now refers to the add instruction.
  Or->replaceAllUsesWith(New);
  New->setDebugLoc(Or->getDebugLoc());

  LLVM_DEBUG(dbgs() << "Converted or into an add: " << *New << '\n');
  return New;
}

/// Return true if we should break up this subtract of X-Y into (X + -Y).
static bool ShouldBreakUpSubtract(Instruction *Sub) {
  // If this is a negation, we can't split it up!
  if (match(Sub, m_Neg(m_Value())) || match(Sub, m_FNeg(m_Value()))) 
    return false;

  // Don't breakup X - undef.
  if (isa<UndefValue>(Sub->getOperand(1)))
    return false;

  // Don't bother to break this up unless either the LHS is an associable add or
  // subtract or if this is only used by one.
  Value *V0 = Sub->getOperand(0);
  if (isReassociableOp(V0, Instruction::Add, Instruction::FAdd) ||
      isReassociableOp(V0, Instruction::Sub, Instruction::FSub))
    return true;
  Value *V1 = Sub->getOperand(1);
  if (isReassociableOp(V1, Instruction::Add, Instruction::FAdd) ||
      isReassociableOp(V1, Instruction::Sub, Instruction::FSub))
    return true;
  Value *VB = Sub->user_back();
  if (Sub->hasOneUse() &&
      (isReassociableOp(VB, Instruction::Add, Instruction::FAdd) ||
       isReassociableOp(VB, Instruction::Sub, Instruction::FSub)))
    return true;

  return false;
}

/// If we have (X-Y), and if either X is an add, or if this is only used by an
/// add, transform this into (X+(0-Y)) to promote better reassociation.
static BinaryOperator *BreakUpSubtract(Instruction *Sub,
                                       ReassociatePass::OrderedSet &ToRedo) {
  // Convert a subtract into an add and a neg instruction. This allows sub
  // instructions to be commuted with other add instructions.
  //
  // Calculate the negative value of Operand 1 of the sub instruction,
  // and set it as the RHS of the add instruction we just made.
  Value *NegVal = NegateValue(Sub->getOperand(1), Sub, ToRedo);
  BinaryOperator *New =
      CreateAdd(Sub->getOperand(0), NegVal, "", Sub->getIterator(), Sub);
  Sub->setOperand(0, Constant::getNullValue(Sub->getType())); // Drop use of op.
  Sub->setOperand(1, Constant::getNullValue(Sub->getType())); // Drop use of op.
  New->takeName(Sub);

  // Everyone now refers to the add instruction.
  Sub->replaceAllUsesWith(New);
  New->setDebugLoc(Sub->getDebugLoc());

  LLVM_DEBUG(dbgs() << "Negated: " << *New << '\n');
  return New;
}

/// If this is a shift of a reassociable multiply or is used by one, change
/// this into a multiply by a constant to assist with further reassociation.
static BinaryOperator *ConvertShiftToMul(Instruction *Shl) {
  Constant *MulCst = ConstantInt::get(Shl->getType(), 1);
  auto *SA = cast<ConstantInt>(Shl->getOperand(1));
  MulCst = ConstantFoldBinaryInstruction(Instruction::Shl, MulCst, SA);
  assert(MulCst && "Constant folding of immediate constants failed");

  BinaryOperator *Mul = BinaryOperator::CreateMul(Shl->getOperand(0), MulCst,
                                                  "", Shl->getIterator());
  Shl->setOperand(0, PoisonValue::get(Shl->getType())); // Drop use of op.
  Mul->takeName(Shl);

  // Everyone now refers to the mul instruction.
  Shl->replaceAllUsesWith(Mul);
  Mul->setDebugLoc(Shl->getDebugLoc());

  // We can safely preserve the nuw flag in all cases.  It's also safe to turn a
  // nuw nsw shl into a nuw nsw mul.  However, nsw in isolation requires special
  // handling.  It can be preserved as long as we're not left shifting by
  // bitwidth - 1.
  bool NSW = cast<BinaryOperator>(Shl)->hasNoSignedWrap();
  bool NUW = cast<BinaryOperator>(Shl)->hasNoUnsignedWrap();
  unsigned BitWidth = Shl->getType()->getIntegerBitWidth();
  if (NSW && (NUW || SA->getValue().ult(BitWidth - 1)))
    Mul->setHasNoSignedWrap(true);
  Mul->setHasNoUnsignedWrap(NUW);
  return Mul;
}

/// Scan backwards and forwards among values with the same rank as element i
/// to see if X exists.  If X does not exist, return i.  This is useful when
/// scanning for 'x' when we see '-x' because they both get the same rank.
static unsigned FindInOperandList(const SmallVectorImpl<ValueEntry> &Ops,
                                  unsigned i, Value *X) {
  unsigned XRank = Ops[i].Rank;
  unsigned e = Ops.size();
  for (unsigned j = i+1; j != e && Ops[j].Rank == XRank; ++j) {
    if (Ops[j].Op == X)
      return j;
    if (Instruction *I1 = dyn_cast<Instruction>(Ops[j].Op))
      if (Instruction *I2 = dyn_cast<Instruction>(X))
        if (I1->isIdenticalTo(I2))
          return j;
  }
  // Scan backwards.
  for (unsigned j = i-1; j != ~0U && Ops[j].Rank == XRank; --j) {
    if (Ops[j].Op == X)
      return j;
    if (Instruction *I1 = dyn_cast<Instruction>(Ops[j].Op))
      if (Instruction *I2 = dyn_cast<Instruction>(X))
        if (I1->isIdenticalTo(I2))
          return j;
  }
  return i;
}

/// Emit a tree of add instructions, summing Ops together
/// and returning the result.  Insert the tree before I.
static Value *EmitAddTreeOfValues(BasicBlock::iterator It,
                                  SmallVectorImpl<WeakTrackingVH> &Ops) {
  if (Ops.size() == 1) return Ops.back();

  Value *V1 = Ops.pop_back_val();
  Value *V2 = EmitAddTreeOfValues(It, Ops);
  return CreateAdd(V2, V1, "reass.add", It, &*It);
}

/// If V is an expression tree that is a multiplication sequence,
/// and if this sequence contains a multiply by Factor,
/// remove Factor from the tree and return the new tree.
Value *ReassociatePass::RemoveFactorFromExpression(Value *V, Value *Factor) {
  BinaryOperator *BO = isReassociableOp(V, Instruction::Mul, Instruction::FMul);
  if (!BO)
    return nullptr;

  SmallVector<RepeatedValue, 8> Tree;
  OverflowTracking Flags;
  MadeChange |= LinearizeExprTree(BO, Tree, RedoInsts, Flags);
  SmallVector<ValueEntry, 8> Factors;
  Factors.reserve(Tree.size());
  for (unsigned i = 0, e = Tree.size(); i != e; ++i) {
    RepeatedValue E = Tree[i];
    Factors.append(E.second, ValueEntry(getRank(E.first), E.first));
  }

  bool FoundFactor = false;
  bool NeedsNegate = false;
  for (unsigned i = 0, e = Factors.size(); i != e; ++i) {
    if (Factors[i].Op == Factor) {
      FoundFactor = true;
      Factors.erase(Factors.begin()+i);
      break;
    }

    // If this is a negative version of this factor, remove it.
    if (ConstantInt *FC1 = dyn_cast<ConstantInt>(Factor)) {
      if (ConstantInt *FC2 = dyn_cast<ConstantInt>(Factors[i].Op))
        if (FC1->getValue() == -FC2->getValue()) {
          FoundFactor = NeedsNegate = true;
          Factors.erase(Factors.begin()+i);
          break;
        }
    } else if (ConstantFP *FC1 = dyn_cast<ConstantFP>(Factor)) {
      if (ConstantFP *FC2 = dyn_cast<ConstantFP>(Factors[i].Op)) {
        const APFloat &F1 = FC1->getValueAPF();
        APFloat F2(FC2->getValueAPF());
        F2.changeSign();
        if (F1 == F2) {
          FoundFactor = NeedsNegate = true;
          Factors.erase(Factors.begin() + i);
          break;
        }
      }
    }
  }

  if (!FoundFactor) {
    // Make sure to restore the operands to the expression tree.
    RewriteExprTree(BO, Factors, Flags);
    return nullptr;
  }

  BasicBlock::iterator InsertPt = ++BO->getIterator();

  // If this was just a single multiply, remove the multiply and return the only
  // remaining operand.
  if (Factors.size() == 1) {
    RedoInsts.insert(BO);
    V = Factors[0].Op;
  } else {
    RewriteExprTree(BO, Factors, Flags);
    V = BO;
  }

  if (NeedsNegate)
    V = CreateNeg(V, "neg", InsertPt, BO);

  return V;
}

/// If V is a single-use multiply, recursively add its operands as factors,
/// otherwise add V to the list of factors.
///
/// Ops is the top-level list of add operands we're trying to factor.
static void FindSingleUseMultiplyFactors(Value *V,
                                         SmallVectorImpl<Value*> &Factors) {
  BinaryOperator *BO = isReassociableOp(V, Instruction::Mul, Instruction::FMul);
  if (!BO) {
    Factors.push_back(V);
    return;
  }

  // Otherwise, add the LHS and RHS to the list of factors.
  FindSingleUseMultiplyFactors(BO->getOperand(1), Factors);
  FindSingleUseMultiplyFactors(BO->getOperand(0), Factors);
}

/// Optimize a series of operands to an 'and', 'or', or 'xor' instruction.
/// This optimizes based on identities.  If it can be reduced to a single Value,
/// it is returned, otherwise the Ops list is mutated as necessary.
static Value *OptimizeAndOrXor(unsigned Opcode,
                               SmallVectorImpl<ValueEntry> &Ops) {
  // Scan the operand lists looking for X and ~X pairs, along with X,X pairs.
  // If we find any, we can simplify the expression. X&~X == 0, X|~X == -1.
  for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
    // First, check for X and ~X in the operand list.
    assert(i < Ops.size());
    Value *X;
    if (match(Ops[i].Op, m_Not(m_Value(X)))) {    // Cannot occur for ^.
      unsigned FoundX = FindInOperandList(Ops, i, X);
      if (FoundX != i) {
        if (Opcode == Instruction::And)   // ...&X&~X = 0
          return Constant::getNullValue(X->getType());

        if (Opcode == Instruction::Or)    // ...|X|~X = -1
          return Constant::getAllOnesValue(X->getType());
      }
    }

    // Next, check for duplicate pairs of values, which we assume are next to
    // each other, due to our sorting criteria.
    assert(i < Ops.size());
    if (i+1 != Ops.size() && Ops[i+1].Op == Ops[i].Op) {
      if (Opcode == Instruction::And || Opcode == Instruction::Or) {
        // Drop duplicate values for And and Or.
        Ops.erase(Ops.begin()+i);
        --i; --e;
        ++NumAnnihil;
        continue;
      }

      // Drop pairs of values for Xor.
      assert(Opcode == Instruction::Xor);
      if (e == 2)
        return Constant::getNullValue(Ops[0].Op->getType());

      // Y ^ X^X -> Y
      Ops.erase(Ops.begin()+i, Ops.begin()+i+2);
      i -= 1; e -= 2;
      ++NumAnnihil;
    }
  }
  return nullptr;
}

/// Helper function of CombineXorOpnd(). It creates a bitwise-and
/// instruction with the given two operands, and return the resulting
/// instruction. There are two special cases: 1) if the constant operand is 0,
/// it will return NULL. 2) if the constant is ~0, the symbolic operand will
/// be returned.
static Value *createAndInstr(BasicBlock::iterator InsertBefore, Value *Opnd,
                             const APInt &ConstOpnd) {
  if (ConstOpnd.isZero())
    return nullptr;

  if (ConstOpnd.isAllOnes())
    return Opnd;

  Instruction *I = BinaryOperator::CreateAnd(
      Opnd, ConstantInt::get(Opnd->getType(), ConstOpnd), "and.ra",
      InsertBefore);
  I->setDebugLoc(InsertBefore->getDebugLoc());
  return I;
}

// Helper function of OptimizeXor(). It tries to simplify "Opnd1 ^ ConstOpnd"
// into "R ^ C", where C would be 0, and R is a symbolic value.
//
// If it was successful, true is returned, and the "R" and "C" is returned
// via "Res" and "ConstOpnd", respectively; otherwise, false is returned,
// and both "Res" and "ConstOpnd" remain unchanged.
bool ReassociatePass::CombineXorOpnd(BasicBlock::iterator It, XorOpnd *Opnd1,
                                     APInt &ConstOpnd, Value *&Res) {
  // Xor-Rule 1: (x | c1) ^ c2 = (x | c1) ^ (c1 ^ c1) ^ c2
  //                       = ((x | c1) ^ c1) ^ (c1 ^ c2)
  //                       = (x & ~c1) ^ (c1 ^ c2)
  // It is useful only when c1 == c2.
  if (!Opnd1->isOrExpr() || Opnd1->getConstPart().isZero())
    return false;

  if (!Opnd1->getValue()->hasOneUse())
    return false;

  const APInt &C1 = Opnd1->getConstPart();
  if (C1 != ConstOpnd)
    return false;

  Value *X = Opnd1->getSymbolicPart();
  Res = createAndInstr(It, X, ~C1);
  // ConstOpnd was C2, now C1 ^ C2.
  ConstOpnd ^= C1;

  if (Instruction *T = dyn_cast<Instruction>(Opnd1->getValue()))
    RedoInsts.insert(T);
  return true;
}

// Helper function of OptimizeXor(). It tries to simplify
// "Opnd1 ^ Opnd2 ^ ConstOpnd" into "R ^ C", where C would be 0, and R is a
// symbolic value.
//
// If it was successful, true is returned, and the "R" and "C" is returned
// via "Res" and "ConstOpnd", respectively (If the entire expression is
// evaluated to a constant, the Res is set to NULL); otherwise, false is
// returned, and both "Res" and "ConstOpnd" remain unchanged.
bool ReassociatePass::CombineXorOpnd(BasicBlock::iterator It, XorOpnd *Opnd1,
                                     XorOpnd *Opnd2, APInt &ConstOpnd,
                                     Value *&Res) {
  Value *X = Opnd1->getSymbolicPart();
  if (X != Opnd2->getSymbolicPart())
    return false;

  // This many instruction become dead.(At least "Opnd1 ^ Opnd2" will die.)
  int DeadInstNum = 1;
  if (Opnd1->getValue()->hasOneUse())
    DeadInstNum++;
  if (Opnd2->getValue()->hasOneUse())
    DeadInstNum++;

  // Xor-Rule 2:
  //  (x | c1) ^ (x & c2)
  //   = (x|c1) ^ (x&c2) ^ (c1 ^ c1) = ((x|c1) ^ c1) ^ (x & c2) ^ c1
  //   = (x & ~c1) ^ (x & c2) ^ c1               // Xor-Rule 1
  //   = (x & c3) ^ c1, where c3 = ~c1 ^ c2      // Xor-rule 3
  //
  if (Opnd1->isOrExpr() != Opnd2->isOrExpr()) {
    if (Opnd2->isOrExpr())
      std::swap(Opnd1, Opnd2);

    const APInt &C1 = Opnd1->getConstPart();
    const APInt &C2 = Opnd2->getConstPart();
    APInt C3((~C1) ^ C2);

    // Do not increase code size!
    if (!C3.isZero() && !C3.isAllOnes()) {
      int NewInstNum = ConstOpnd.getBoolValue() ? 1 : 2;
      if (NewInstNum > DeadInstNum)
        return false;
    }

    Res = createAndInstr(It, X, C3);
    ConstOpnd ^= C1;
  } else if (Opnd1->isOrExpr()) {
    // Xor-Rule 3: (x | c1) ^ (x | c2) = (x & c3) ^ c3 where c3 = c1 ^ c2
    //
    const APInt &C1 = Opnd1->getConstPart();
    const APInt &C2 = Opnd2->getConstPart();
    APInt C3 = C1 ^ C2;

    // Do not increase code size
    if (!C3.isZero() && !C3.isAllOnes()) {
      int NewInstNum = ConstOpnd.getBoolValue() ? 1 : 2;
      if (NewInstNum > DeadInstNum)
        return false;
    }

    Res = createAndInstr(It, X, C3);
    ConstOpnd ^= C3;
  } else {
    // Xor-Rule 4: (x & c1) ^ (x & c2) = (x & (c1^c2))
    //
    const APInt &C1 = Opnd1->getConstPart();
    const APInt &C2 = Opnd2->getConstPart();
    APInt C3 = C1 ^ C2;
    Res = createAndInstr(It, X, C3);
  }

  // Put the original operands in the Redo list; hope they will be deleted
  // as dead code.
  if (Instruction *T = dyn_cast<Instruction>(Opnd1->getValue()))
    RedoInsts.insert(T);
  if (Instruction *T = dyn_cast<Instruction>(Opnd2->getValue()))
    RedoInsts.insert(T);

  return true;
}

/// Optimize a series of operands to an 'xor' instruction. If it can be reduced
/// to a single Value, it is returned, otherwise the Ops list is mutated as
/// necessary.
Value *ReassociatePass::OptimizeXor(Instruction *I,
                                    SmallVectorImpl<ValueEntry> &Ops) {
  if (Value *V = OptimizeAndOrXor(Instruction::Xor, Ops))
    return V;

  if (Ops.size() == 1)
    return nullptr;

  SmallVector<XorOpnd, 8> Opnds;
  SmallVector<XorOpnd*, 8> OpndPtrs;
  Type *Ty = Ops[0].Op->getType();
  APInt ConstOpnd(Ty->getScalarSizeInBits(), 0);

  // Step 1: Convert ValueEntry to XorOpnd
  for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
    Value *V = Ops[i].Op;
    const APInt *C;
    // TODO: Support non-splat vectors.
    if (match(V, m_APInt(C))) {
      ConstOpnd ^= *C;
    } else {
      XorOpnd O(V);
      O.setSymbolicRank(getRank(O.getSymbolicPart()));
      Opnds.push_back(O);
    }
  }

  // NOTE: From this point on, do *NOT* add/delete element to/from "Opnds".
  //  It would otherwise invalidate the "Opnds"'s iterator, and hence invalidate
  //  the "OpndPtrs" as well. For the similar reason, do not fuse this loop
  //  with the previous loop --- the iterator of the "Opnds" may be invalidated
  //  when new elements are added to the vector.
  for (XorOpnd &Op : Opnds)
    OpndPtrs.push_back(&Op);

  // Step 2: Sort the Xor-Operands in a way such that the operands containing
  //  the same symbolic value cluster together. For instance, the input operand
  //  sequence ("x | 123", "y & 456", "x & 789") will be sorted into:
  //  ("x | 123", "x & 789", "y & 456").
  //
  //  The purpose is twofold:
  //  1) Cluster together the operands sharing the same symbolic-value.
  //  2) Operand having smaller symbolic-value-rank is permuted earlier, which
  //     could potentially shorten crital path, and expose more loop-invariants.
  //     Note that values' rank are basically defined in RPO order (FIXME).
  //     So, if Rank(X) < Rank(Y) < Rank(Z), it means X is defined earlier
  //     than Y which is defined earlier than Z. Permute "x | 1", "Y & 2",
  //     "z" in the order of X-Y-Z is better than any other orders.
  llvm::stable_sort(OpndPtrs, [](XorOpnd *LHS, XorOpnd *RHS) {
    return LHS->getSymbolicRank() < RHS->getSymbolicRank();
  });

  // Step 3: Combine adjacent operands
  XorOpnd *PrevOpnd = nullptr;
  bool Changed = false;
  for (unsigned i = 0, e = Opnds.size(); i < e; i++) {
    XorOpnd *CurrOpnd = OpndPtrs[i];
    // The combined value
    Value *CV;

    // Step 3.1: Try simplifying "CurrOpnd ^ ConstOpnd"
    if (!ConstOpnd.isZero() &&
        CombineXorOpnd(I->getIterator(), CurrOpnd, ConstOpnd, CV)) {
      Changed = true;
      if (CV)
        *CurrOpnd = XorOpnd(CV);
      else {
        CurrOpnd->Invalidate();
        continue;
      }
    }

    if (!PrevOpnd || CurrOpnd->getSymbolicPart() != PrevOpnd->getSymbolicPart()) {
      PrevOpnd = CurrOpnd;
      continue;
    }

    // step 3.2: When previous and current operands share the same symbolic
    //  value, try to simplify "PrevOpnd ^ CurrOpnd ^ ConstOpnd"
    if (CombineXorOpnd(I->getIterator(), CurrOpnd, PrevOpnd, ConstOpnd, CV)) {
      // Remove previous operand
      PrevOpnd->Invalidate();
      if (CV) {
        *CurrOpnd = XorOpnd(CV);
        PrevOpnd = CurrOpnd;
      } else {
        CurrOpnd->Invalidate();
        PrevOpnd = nullptr;
      }
      Changed = true;
    }
  }

  // Step 4: Reassemble the Ops
  if (Changed) {
    Ops.clear();
    for (const XorOpnd &O : Opnds) {
      if (O.isInvalid())
        continue;
      ValueEntry VE(getRank(O.getValue()), O.getValue());
      Ops.push_back(VE);
    }
    if (!ConstOpnd.isZero()) {
      Value *C = ConstantInt::get(Ty, ConstOpnd);
      ValueEntry VE(getRank(C), C);
      Ops.push_back(VE);
    }
    unsigned Sz = Ops.size();
    if (Sz == 1)
      return Ops.back().Op;
    if (Sz == 0) {
      assert(ConstOpnd.isZero());
      return ConstantInt::get(Ty, ConstOpnd);
    }
  }

  return nullptr;
}

/// Optimize a series of operands to an 'add' instruction.  This
/// optimizes based on identities.  If it can be reduced to a single Value, it
/// is returned, otherwise the Ops list is mutated as necessary.
Value *ReassociatePass::OptimizeAdd(Instruction *I,
                                    SmallVectorImpl<ValueEntry> &Ops) {
  // Scan the operand lists looking for X and -X pairs.  If we find any, we
  // can simplify expressions like X+-X == 0 and X+~X ==-1.  While we're at it,
  // scan for any
  // duplicates.  We want to canonicalize Y+Y+Y+Z -> 3*Y+Z.

  for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
    Value *TheOp = Ops[i].Op;
    // Check to see if we've seen this operand before.  If so, we factor all
    // instances of the operand together.  Due to our sorting criteria, we know
    // that these need to be next to each other in the vector.
    if (i+1 != Ops.size() && Ops[i+1].Op == TheOp) {
      // Rescan the list, remove all instances of this operand from the expr.
      unsigned NumFound = 0;
      do {
        Ops.erase(Ops.begin()+i);
        ++NumFound;
      } while (i != Ops.size() && Ops[i].Op == TheOp);

      LLVM_DEBUG(dbgs() << "\nFACTORING [" << NumFound << "]: " << *TheOp
                        << '\n');
      ++NumFactor;

      // Insert a new multiply.
      Type *Ty = TheOp->getType();
      Constant *C = Ty->isIntOrIntVectorTy() ?
        ConstantInt::get(Ty, NumFound) : ConstantFP::get(Ty, NumFound);
      Instruction *Mul = CreateMul(TheOp, C, "factor", I->getIterator(), I);

      // Now that we have inserted a multiply, optimize it. This allows us to
      // handle cases that require multiple factoring steps, such as this:
      // (X*2) + (X*2) + (X*2) -> (X*2)*3 -> X*6
      RedoInsts.insert(Mul);

      // If every add operand was a duplicate, return the multiply.
      if (Ops.empty())
        return Mul;

      // Otherwise, we had some input that didn't have the dupe, such as
      // "A + A + B" -> "A*2 + B".  Add the new multiply to the list of
      // things being added by this operation.
      Ops.insert(Ops.begin(), ValueEntry(getRank(Mul), Mul));

      --i;
      e = Ops.size();
      continue;
    }

    // Check for X and -X or X and ~X in the operand list.
    Value *X;
    if (!match(TheOp, m_Neg(m_Value(X))) && !match(TheOp, m_Not(m_Value(X))) &&
        !match(TheOp, m_FNeg(m_Value(X))))
      continue;

    unsigned FoundX = FindInOperandList(Ops, i, X);
    if (FoundX == i)
      continue;

    // Remove X and -X from the operand list.
    if (Ops.size() == 2 &&
        (match(TheOp, m_Neg(m_Value())) || match(TheOp, m_FNeg(m_Value()))))
      return Constant::getNullValue(X->getType());

    // Remove X and ~X from the operand list.
    if (Ops.size() == 2 && match(TheOp, m_Not(m_Value())))
      return Constant::getAllOnesValue(X->getType());

    Ops.erase(Ops.begin()+i);
    if (i < FoundX)
      --FoundX;
    else
      --i;   // Need to back up an extra one.
    Ops.erase(Ops.begin()+FoundX);
    ++NumAnnihil;
    --i;     // Revisit element.
    e -= 2;  // Removed two elements.

    // if X and ~X we append -1 to the operand list.
    if (match(TheOp, m_Not(m_Value()))) {
      Value *V = Constant::getAllOnesValue(X->getType());
      Ops.insert(Ops.end(), ValueEntry(getRank(V), V));
      e += 1;
    }
  }

  // Scan the operand list, checking to see if there are any common factors
  // between operands.  Consider something like A*A+A*B*C+D.  We would like to
  // reassociate this to A*(A+B*C)+D, which reduces the number of multiplies.
  // To efficiently find this, we count the number of times a factor occurs
  // for any ADD operands that are MULs.
  DenseMap<Value*, unsigned> FactorOccurrences;

  // Keep track of each multiply we see, to avoid triggering on (X*4)+(X*4)
  // where they are actually the same multiply.
  unsigned MaxOcc = 0;
  Value *MaxOccVal = nullptr;
  for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
    BinaryOperator *BOp =
        isReassociableOp(Ops[i].Op, Instruction::Mul, Instruction::FMul);
    if (!BOp)
      continue;

    // Compute all of the factors of this added value.
    SmallVector<Value*, 8> Factors;
    FindSingleUseMultiplyFactors(BOp, Factors);
    assert(Factors.size() > 1 && "Bad linearize!");

    // Add one to FactorOccurrences for each unique factor in this op.
    SmallPtrSet<Value*, 8> Duplicates;
    for (Value *Factor : Factors) {
      if (!Duplicates.insert(Factor).second)
        continue;

      unsigned Occ = ++FactorOccurrences[Factor];
      if (Occ > MaxOcc) {
        MaxOcc = Occ;
        MaxOccVal = Factor;
      }

      // If Factor is a negative constant, add the negated value as a factor
      // because we can percolate the negate out.  Watch for minint, which
      // cannot be positivified.
      if (ConstantInt *CI = dyn_cast<ConstantInt>(Factor)) {
        if (CI->isNegative() && !CI->isMinValue(true)) {
          Factor = ConstantInt::get(CI->getContext(), -CI->getValue());
          if (!Duplicates.insert(Factor).second)
            continue;
          unsigned Occ = ++FactorOccurrences[Factor];
          if (Occ > MaxOcc) {
            MaxOcc = Occ;
            MaxOccVal = Factor;
          }
        }
      } else if (ConstantFP *CF = dyn_cast<ConstantFP>(Factor)) {
        if (CF->isNegative()) {
          APFloat F(CF->getValueAPF());
          F.changeSign();
          Factor = ConstantFP::get(CF->getContext(), F);
          if (!Duplicates.insert(Factor).second)
            continue;
          unsigned Occ = ++FactorOccurrences[Factor];
          if (Occ > MaxOcc) {
            MaxOcc = Occ;
            MaxOccVal = Factor;
          }
        }
      }
    }
  }

  // If any factor occurred more than one time, we can pull it out.
  if (MaxOcc > 1) {
    LLVM_DEBUG(dbgs() << "\nFACTORING [" << MaxOcc << "]: " << *MaxOccVal
                      << '\n');
    ++NumFactor;

    // Create a new instruction that uses the MaxOccVal twice.  If we don't do
    // this, we could otherwise run into situations where removing a factor
    // from an expression will drop a use of maxocc, and this can cause
    // RemoveFactorFromExpression on successive values to behave differently.
    Instruction *DummyInst =
        I->getType()->isIntOrIntVectorTy()
            ? BinaryOperator::CreateAdd(MaxOccVal, MaxOccVal)
            : BinaryOperator::CreateFAdd(MaxOccVal, MaxOccVal);

    SmallVector<WeakTrackingVH, 4> NewMulOps;
    for (unsigned i = 0; i != Ops.size(); ++i) {
      // Only try to remove factors from expressions we're allowed to.
      BinaryOperator *BOp =
          isReassociableOp(Ops[i].Op, Instruction::Mul, Instruction::FMul);
      if (!BOp)
        continue;

      if (Value *V = RemoveFactorFromExpression(Ops[i].Op, MaxOccVal)) {
        // The factorized operand may occur several times.  Convert them all in
        // one fell swoop.
        for (unsigned j = Ops.size(); j != i;) {
          --j;
          if (Ops[j].Op == Ops[i].Op) {
            NewMulOps.push_back(V);
            Ops.erase(Ops.begin()+j);
          }
        }
        --i;
      }
    }

    // No need for extra uses anymore.
    DummyInst->deleteValue();

    unsigned NumAddedValues = NewMulOps.size();
    Value *V = EmitAddTreeOfValues(I->getIterator(), NewMulOps);

    // Now that we have inserted the add tree, optimize it. This allows us to
    // handle cases that require multiple factoring steps, such as this:
    // A*A*B + A*A*C   -->   A*(A*B+A*C)   -->   A*(A*(B+C))
    assert(NumAddedValues > 1 && "Each occurrence should contribute a value");
    (void)NumAddedValues;
    if (Instruction *VI = dyn_cast<Instruction>(V))
      RedoInsts.insert(VI);

    // Create the multiply.
    Instruction *V2 = CreateMul(V, MaxOccVal, "reass.mul", I->getIterator(), I);

    // Rerun associate on the multiply in case the inner expression turned into
    // a multiply.  We want to make sure that we keep things in canonical form.
    RedoInsts.insert(V2);

    // If every add operand included the factor (e.g. "A*B + A*C"), then the
    // entire result expression is just the multiply "A*(B+C)".
    if (Ops.empty())
      return V2;

    // Otherwise, we had some input that didn't have the factor, such as
    // "A*B + A*C + D" -> "A*(B+C) + D".  Add the new multiply to the list of
    // things being added by this operation.
    Ops.insert(Ops.begin(), ValueEntry(getRank(V2), V2));
  }

  return nullptr;
}

/// Build up a vector of value/power pairs factoring a product.
///
/// Given a series of multiplication operands, build a vector of factors and
/// the powers each is raised to when forming the final product. Sort them in
/// the order of descending power.
///
///      (x*x)          -> [(x, 2)]
///     ((x*x)*x)       -> [(x, 3)]
///   ((((x*y)*x)*y)*x) -> [(x, 3), (y, 2)]
///
/// \returns Whether any factors have a power greater than one.
static bool collectMultiplyFactors(SmallVectorImpl<ValueEntry> &Ops,
                                   SmallVectorImpl<Factor> &Factors) {
  // FIXME: Have Ops be (ValueEntry, Multiplicity) pairs, simplifying this.
  // Compute the sum of powers of simplifiable factors.
  unsigned FactorPowerSum = 0;
  for (unsigned Idx = 1, Size = Ops.size(); Idx < Size; ++Idx) {
    Value *Op = Ops[Idx-1].Op;

    // Count the number of occurrences of this value.
    unsigned Count = 1;
    for (; Idx < Size && Ops[Idx].Op == Op; ++Idx)
      ++Count;
    // Track for simplification all factors which occur 2 or more times.
    if (Count > 1)
      FactorPowerSum += Count;
  }

  // We can only simplify factors if the sum of the powers of our simplifiable
  // factors is 4 or higher. When that is the case, we will *always* have
  // a simplification. This is an important invariant to prevent cyclicly
  // trying to simplify already minimal formations.
  if (FactorPowerSum < 4)
    return false;

  // Now gather the simplifiable factors, removing them from Ops.
  FactorPowerSum = 0;
  for (unsigned Idx = 1; Idx < Ops.size(); ++Idx) {
    Value *Op = Ops[Idx-1].Op;

    // Count the number of occurrences of this value.
    unsigned Count = 1;
    for (; Idx < Ops.size() && Ops[Idx].Op == Op; ++Idx)
      ++Count;
    if (Count == 1)
      continue;
    // Move an even number of occurrences to Factors.
    Count &= ~1U;
    Idx -= Count;
    FactorPowerSum += Count;
    Factors.push_back(Factor(Op, Count));
    Ops.erase(Ops.begin()+Idx, Ops.begin()+Idx+Count);
  }

  // None of the adjustments above should have reduced the sum of factor powers
  // below our mininum of '4'.
  assert(FactorPowerSum >= 4);

  llvm::stable_sort(Factors, [](const Factor &LHS, const Factor &RHS) {
    return LHS.Power > RHS.Power;
  });
  return true;
}

/// Build a tree of multiplies, computing the product of Ops.
static Value *buildMultiplyTree(IRBuilderBase &Builder,
                                SmallVectorImpl<Value*> &Ops) {
  if (Ops.size() == 1)
    return Ops.back();

  Value *LHS = Ops.pop_back_val();
  do {
    if (LHS->getType()->isIntOrIntVectorTy())
      LHS = Builder.CreateMul(LHS, Ops.pop_back_val());
    else
      LHS = Builder.CreateFMul(LHS, Ops.pop_back_val());
  } while (!Ops.empty());

  return LHS;
}

/// Build a minimal multiplication DAG for (a^x)*(b^y)*(c^z)*...
///
/// Given a vector of values raised to various powers, where no two values are
/// equal and the powers are sorted in decreasing order, compute the minimal
/// DAG of multiplies to compute the final product, and return that product
/// value.
Value *
ReassociatePass::buildMinimalMultiplyDAG(IRBuilderBase &Builder,
                                         SmallVectorImpl<Factor> &Factors) {
  assert(Factors[0].Power);
  SmallVector<Value *, 4> OuterProduct;
  for (unsigned LastIdx = 0, Idx = 1, Size = Factors.size();
       Idx < Size && Factors[Idx].Power > 0; ++Idx) {
    if (Factors[Idx].Power != Factors[LastIdx].Power) {
      LastIdx = Idx;
      continue;
    }

    // We want to multiply across all the factors with the same power so that
    // we can raise them to that power as a single entity. Build a mini tree
    // for that.
    SmallVector<Value *, 4> InnerProduct;
    InnerProduct.push_back(Factors[LastIdx].Base);
    do {
      InnerProduct.push_back(Factors[Idx].Base);
      ++Idx;
    } while (Idx < Size && Factors[Idx].Power == Factors[LastIdx].Power);

    // Reset the base value of the first factor to the new expression tree.
    // We'll remove all the factors with the same power in a second pass.
    Value *M = Factors[LastIdx].Base = buildMultiplyTree(Builder, InnerProduct);
    if (Instruction *MI = dyn_cast<Instruction>(M))
      RedoInsts.insert(MI);

    LastIdx = Idx;
  }
  // Unique factors with equal powers -- we've folded them into the first one's
  // base.
  Factors.erase(llvm::unique(Factors,
                             [](const Factor &LHS, const Factor &RHS) {
                               return LHS.Power == RHS.Power;
                             }),
                Factors.end());

  // Iteratively collect the base of each factor with an add power into the
  // outer product, and halve each power in preparation for squaring the
  // expression.
  for (Factor &F : Factors) {
    if (F.Power & 1)
      OuterProduct.push_back(F.Base);
    F.Power >>= 1;
  }
  if (Factors[0].Power) {
    Value *SquareRoot = buildMinimalMultiplyDAG(Builder, Factors);
    OuterProduct.push_back(SquareRoot);
    OuterProduct.push_back(SquareRoot);
  }
  if (OuterProduct.size() == 1)
    return OuterProduct.front();

  Value *V = buildMultiplyTree(Builder, OuterProduct);
  return V;
}

Value *ReassociatePass::OptimizeMul(BinaryOperator *I,
                                    SmallVectorImpl<ValueEntry> &Ops) {
  // We can only optimize the multiplies when there is a chain of more than
  // three, such that a balanced tree might require fewer total multiplies.
  if (Ops.size() < 4)
    return nullptr;

  // Try to turn linear trees of multiplies without other uses of the
  // intermediate stages into minimal multiply DAGs with perfect sub-expression
  // re-use.
  SmallVector<Factor, 4> Factors;
  if (!collectMultiplyFactors(Ops, Factors))
    return nullptr; // All distinct factors, so nothing left for us to do.

  IRBuilder<> Builder(I);
  // The reassociate transformation for FP operations is performed only
  // if unsafe algebra is permitted by FastMathFlags. Propagate those flags
  // to the newly generated operations.
  if (auto FPI = dyn_cast<FPMathOperator>(I))
    Builder.setFastMathFlags(FPI->getFastMathFlags());

  Value *V = buildMinimalMultiplyDAG(Builder, Factors);
  if (Ops.empty())
    return V;

  ValueEntry NewEntry = ValueEntry(getRank(V), V);
  Ops.insert(llvm::lower_bound(Ops, NewEntry), NewEntry);
  return nullptr;
}

Value *ReassociatePass::OptimizeExpression(BinaryOperator *I,
                                           SmallVectorImpl<ValueEntry> &Ops) {
  // Now that we have the linearized expression tree, try to optimize it.
  // Start by folding any constants that we found.
  const DataLayout &DL = I->getDataLayout();
  Constant *Cst = nullptr;
  unsigned Opcode = I->getOpcode();
  while (!Ops.empty()) {
    if (auto *C = dyn_cast<Constant>(Ops.back().Op)) {
      if (!Cst) {
        Ops.pop_back();
        Cst = C;
        continue;
      }
      if (Constant *Res = ConstantFoldBinaryOpOperands(Opcode, C, Cst, DL)) {
        Ops.pop_back();
        Cst = Res;
        continue;
      }
    }
    break;
  }
  // If there was nothing but constants then we are done.
  if (Ops.empty())
    return Cst;

  // Put the combined constant back at the end of the operand list, except if
  // there is no point.  For example, an add of 0 gets dropped here, while a
  // multiplication by zero turns the whole expression into zero.
  if (Cst && Cst != ConstantExpr::getBinOpIdentity(Opcode, I->getType())) {
    if (Cst == ConstantExpr::getBinOpAbsorber(Opcode, I->getType()))
      return Cst;
    Ops.push_back(ValueEntry(0, Cst));
  }

  if (Ops.size() == 1) return Ops[0].Op;

  // Handle destructive annihilation due to identities between elements in the
  // argument list here.
  unsigned NumOps = Ops.size();
  switch (Opcode) {
  default: break;
  case Instruction::And:
  case Instruction::Or:
    if (Value *Result = OptimizeAndOrXor(Opcode, Ops))
      return Result;
    break;

  case Instruction::Xor:
    if (Value *Result = OptimizeXor(I, Ops))
      return Result;
    break;

  case Instruction::Add:
  case Instruction::FAdd:
    if (Value *Result = OptimizeAdd(I, Ops))
      return Result;
    break;

  case Instruction::Mul:
  case Instruction::FMul:
    if (Value *Result = OptimizeMul(I, Ops))
      return Result;
    break;
  }

  if (Ops.size() != NumOps)
    return OptimizeExpression(I, Ops);
  return nullptr;
}

// Remove dead instructions and if any operands are trivially dead add them to
// Insts so they will be removed as well.
void ReassociatePass::RecursivelyEraseDeadInsts(Instruction *I,
                                                OrderedSet &Insts) {
  assert(isInstructionTriviallyDead(I) && "Trivially dead instructions only!");
  SmallVector<Value *, 4> Ops(I->operands());
  ValueRankMap.erase(I);
  Insts.remove(I);
  RedoInsts.remove(I);
  llvm::salvageDebugInfo(*I);
  I->eraseFromParent();
  for (auto *Op : Ops)
    if (Instruction *OpInst = dyn_cast<Instruction>(Op))
      if (OpInst->use_empty())
        Insts.insert(OpInst);
}

/// Zap the given instruction, adding interesting operands to the work list.
void ReassociatePass::EraseInst(Instruction *I) {
  assert(isInstructionTriviallyDead(I) && "Trivially dead instructions only!");
  LLVM_DEBUG(dbgs() << "Erasing dead inst: "; I->dump());

  SmallVector<Value *, 8> Ops(I->operands());
  // Erase the dead instruction.
  ValueRankMap.erase(I);
  RedoInsts.remove(I);
  llvm::salvageDebugInfo(*I);
  I->eraseFromParent();
  // Optimize its operands.
  SmallPtrSet<Instruction *, 8> Visited; // Detect self-referential nodes.
  for (Value *V : Ops)
    if (Instruction *Op = dyn_cast<Instruction>(V)) {
      // If this is a node in an expression tree, climb to the expression root
      // and add that since that's where optimization actually happens.
      unsigned Opcode = Op->getOpcode();
      while (Op->hasOneUse() && Op->user_back()->getOpcode() == Opcode &&
             Visited.insert(Op).second)
        Op = Op->user_back();

      // The instruction we're going to push may be coming from a
      // dead block, and Reassociate skips the processing of unreachable
      // blocks because it's a waste of time and also because it can
      // lead to infinite loop due to LLVM's non-standard definition
      // of dominance.
      if (ValueRankMap.contains(Op))
        RedoInsts.insert(Op);
    }

  MadeChange = true;
}

/// Recursively analyze an expression to build a list of instructions that have
/// negative floating-point constant operands. The caller can then transform
/// the list to create positive constants for better reassociation and CSE.
static void getNegatibleInsts(Value *V,
                              SmallVectorImpl<Instruction *> &Candidates) {
  // Handle only one-use instructions. Combining negations does not justify
  // replicating instructions.
  Instruction *I;
  if (!match(V, m_OneUse(m_Instruction(I))))
    return;

  // Handle expressions of multiplications and divisions.
  // TODO: This could look through floating-point casts.
  const APFloat *C;
  switch (I->getOpcode()) {
    case Instruction::FMul:
      // Not expecting non-canonical code here. Bail out and wait.
      if (match(I->getOperand(0), m_Constant()))
        break;

      if (match(I->getOperand(1), m_APFloat(C)) && C->isNegative()) {
        Candidates.push_back(I);
        LLVM_DEBUG(dbgs() << "FMul with negative constant: " << *I << '\n');
      }
      getNegatibleInsts(I->getOperand(0), Candidates);
      getNegatibleInsts(I->getOperand(1), Candidates);
      break;
    case Instruction::FDiv:
      // Not expecting non-canonical code here. Bail out and wait.
      if (match(I->getOperand(0), m_Constant()) &&
          match(I->getOperand(1), m_Constant()))
        break;

      if ((match(I->getOperand(0), m_APFloat(C)) && C->isNegative()) ||
          (match(I->getOperand(1), m_APFloat(C)) && C->isNegative())) {
        Candidates.push_back(I);
        LLVM_DEBUG(dbgs() << "FDiv with negative constant: " << *I << '\n');
      }
      getNegatibleInsts(I->getOperand(0), Candidates);
      getNegatibleInsts(I->getOperand(1), Candidates);
      break;
    default:
      break;
  }
}

/// Given an fadd/fsub with an operand that is a one-use instruction
/// (the fadd/fsub), try to change negative floating-point constants into
/// positive constants to increase potential for reassociation and CSE.
Instruction *ReassociatePass::canonicalizeNegFPConstantsForOp(Instruction *I,
                                                              Instruction *Op,
                                                              Value *OtherOp) {
  assert((I->getOpcode() == Instruction::FAdd ||
          I->getOpcode() == Instruction::FSub) && "Expected fadd/fsub");

  // Collect instructions with negative FP constants from the subtree that ends
  // in Op.
  SmallVector<Instruction *, 4> Candidates;
  getNegatibleInsts(Op, Candidates);
  if (Candidates.empty())
    return nullptr;

  // Don't canonicalize x + (-Constant * y) -> x - (Constant * y), if the
  // resulting subtract will be broken up later.  This can get us into an
  // infinite loop during reassociation.
  bool IsFSub = I->getOpcode() == Instruction::FSub;
  bool NeedsSubtract = !IsFSub && Candidates.size() % 2 == 1;
  if (NeedsSubtract && ShouldBreakUpSubtract(I))
    return nullptr;

  for (Instruction *Negatible : Candidates) {
    const APFloat *C;
    if (match(Negatible->getOperand(0), m_APFloat(C))) {
      assert(!match(Negatible->getOperand(1), m_Constant()) &&
             "Expecting only 1 constant operand");
      assert(C->isNegative() && "Expected negative FP constant");
      Negatible->setOperand(0, ConstantFP::get(Negatible->getType(), abs(*C)));
      MadeChange = true;
    }
    if (match(Negatible->getOperand(1), m_APFloat(C))) {
      assert(!match(Negatible->getOperand(0), m_Constant()) &&
             "Expecting only 1 constant operand");
      assert(C->isNegative() && "Expected negative FP constant");
      Negatible->setOperand(1, ConstantFP::get(Negatible->getType(), abs(*C)));
      MadeChange = true;
    }
  }
  assert(MadeChange == true && "Negative constant candidate was not changed");

  // Negations cancelled out.
  if (Candidates.size() % 2 == 0)
    return I;

  // Negate the final operand in the expression by flipping the opcode of this
  // fadd/fsub.
  assert(Candidates.size() % 2 == 1 && "Expected odd number");
  IRBuilder<> Builder(I);
  Value *NewInst = IsFSub ? Builder.CreateFAddFMF(OtherOp, Op, I)
                          : Builder.CreateFSubFMF(OtherOp, Op, I);
  I->replaceAllUsesWith(NewInst);
  RedoInsts.insert(I);
  return dyn_cast<Instruction>(NewInst);
}

/// Canonicalize expressions that contain a negative floating-point constant
/// of the following form:
///   OtherOp + (subtree) -> OtherOp {+/-} (canonical subtree)
///   (subtree) + OtherOp -> OtherOp {+/-} (canonical subtree)
///   OtherOp - (subtree) -> OtherOp {+/-} (canonical subtree)
///
/// The fadd/fsub opcode may be switched to allow folding a negation into the
/// input instruction.
Instruction *ReassociatePass::canonicalizeNegFPConstants(Instruction *I) {
  LLVM_DEBUG(dbgs() << "Combine negations for: " << *I << '\n');
  Value *X;
  Instruction *Op;
  if (match(I, m_FAdd(m_Value(X), m_OneUse(m_Instruction(Op)))))
    if (Instruction *R = canonicalizeNegFPConstantsForOp(I, Op, X))
      I = R;
  if (match(I, m_FAdd(m_OneUse(m_Instruction(Op)), m_Value(X))))
    if (Instruction *R = canonicalizeNegFPConstantsForOp(I, Op, X))
      I = R;
  if (match(I, m_FSub(m_Value(X), m_OneUse(m_Instruction(Op)))))
    if (Instruction *R = canonicalizeNegFPConstantsForOp(I, Op, X))
      I = R;
  return I;
}

/// Inspect and optimize the given instruction. Note that erasing
/// instructions is not allowed.
void ReassociatePass::OptimizeInst(Instruction *I) {
  // Only consider operations that we understand.
  if (!isa<UnaryOperator>(I) && !isa<BinaryOperator>(I))
    return;

  if (I->getOpcode() == Instruction::Shl && isa<ConstantInt>(I->getOperand(1)))
    // If an operand of this shift is a reassociable multiply, or if the shift
    // is used by a reassociable multiply or add, turn into a multiply.
    if (isReassociableOp(I->getOperand(0), Instruction::Mul) ||
        (I->hasOneUse() &&
         (isReassociableOp(I->user_back(), Instruction::Mul) ||
          isReassociableOp(I->user_back(), Instruction::Add)))) {
      Instruction *NI = ConvertShiftToMul(I);
      RedoInsts.insert(I);
      MadeChange = true;
      I = NI;
    }

  // Commute binary operators, to canonicalize the order of their operands.
  // This can potentially expose more CSE opportunities, and makes writing other
  // transformations simpler.
  if (I->isCommutative())
    canonicalizeOperands(I);

  // Canonicalize negative constants out of expressions.
  if (Instruction *Res = canonicalizeNegFPConstants(I))
    I = Res;

  // Don't optimize floating-point instructions unless they have the
  // appropriate FastMathFlags for reassociation enabled.
  if (isa<FPMathOperator>(I) && !hasFPAssociativeFlags(I))
    return;

  // Do not reassociate boolean (i1) expressions.  We want to preserve the
  // original order of evaluation for short-circuited comparisons that
  // SimplifyCFG has folded to AND/OR expressions.  If the expression
  // is not further optimized, it is likely to be transformed back to a
  // short-circuited form for code gen, and the source order may have been
  // optimized for the most likely conditions.
  if (I->getType()->isIntegerTy(1))
    return;

  // If this is a bitwise or instruction of operands
  // with no common bits set, convert it to X+Y.
  if (I->getOpcode() == Instruction::Or &&
      shouldConvertOrWithNoCommonBitsToAdd(I) && !isLoadCombineCandidate(I) &&
      (cast<PossiblyDisjointInst>(I)->isDisjoint() ||
       haveNoCommonBitsSet(I->getOperand(0), I->getOperand(1),
                           SimplifyQuery(I->getDataLayout(),
                                         /*DT=*/nullptr, /*AC=*/nullptr, I)))) {
    Instruction *NI = convertOrWithNoCommonBitsToAdd(I);
    RedoInsts.insert(I);
    MadeChange = true;
    I = NI;
  }

  // If this is a subtract instruction which is not already in negate form,
  // see if we can convert it to X+-Y.
  if (I->getOpcode() == Instruction::Sub) {
    if (ShouldBreakUpSubtract(I)) {
      Instruction *NI = BreakUpSubtract(I, RedoInsts);
      RedoInsts.insert(I);
      MadeChange = true;
      I = NI;
    } else if (match(I, m_Neg(m_Value()))) {
      // Otherwise, this is a negation.  See if the operand is a multiply tree
      // and if this is not an inner node of a multiply tree.
      if (isReassociableOp(I->getOperand(1), Instruction::Mul) &&
          (!I->hasOneUse() ||
           !isReassociableOp(I->user_back(), Instruction::Mul))) {
        Instruction *NI = LowerNegateToMultiply(I);
        // If the negate was simplified, revisit the users to see if we can
        // reassociate further.
        for (User *U : NI->users()) {
          if (BinaryOperator *Tmp = dyn_cast<BinaryOperator>(U))
            RedoInsts.insert(Tmp);
        }
        RedoInsts.insert(I);
        MadeChange = true;
        I = NI;
      }
    }
  } else if (I->getOpcode() == Instruction::FNeg ||
             I->getOpcode() == Instruction::FSub) {
    if (ShouldBreakUpSubtract(I)) {
      Instruction *NI = BreakUpSubtract(I, RedoInsts);
      RedoInsts.insert(I);
      MadeChange = true;
      I = NI;
    } else if (match(I, m_FNeg(m_Value()))) {
      // Otherwise, this is a negation.  See if the operand is a multiply tree
      // and if this is not an inner node of a multiply tree.
      Value *Op = isa<BinaryOperator>(I) ? I->getOperand(1) :
                                           I->getOperand(0);
      if (isReassociableOp(Op, Instruction::FMul) &&
          (!I->hasOneUse() ||
           !isReassociableOp(I->user_back(), Instruction::FMul))) {
        // If the negate was simplified, revisit the users to see if we can
        // reassociate further.
        Instruction *NI = LowerNegateToMultiply(I);
        for (User *U : NI->users()) {
          if (BinaryOperator *Tmp = dyn_cast<BinaryOperator>(U))
            RedoInsts.insert(Tmp);
        }
        RedoInsts.insert(I);
        MadeChange = true;
        I = NI;
      }
    }
  }

  // If this instruction is an associative binary operator, process it.
  if (!I->isAssociative()) return;
  BinaryOperator *BO = cast<BinaryOperator>(I);

  // If this is an interior node of a reassociable tree, ignore it until we
  // get to the root of the tree, to avoid N^2 analysis.
  unsigned Opcode = BO->getOpcode();
  if (BO->hasOneUse() && BO->user_back()->getOpcode() == Opcode) {
    // During the initial run we will get to the root of the tree.
    // But if we get here while we are redoing instructions, there is no
    // guarantee that the root will be visited. So Redo later
    if (BO->user_back() != BO &&
        BO->getParent() == BO->user_back()->getParent())
      RedoInsts.insert(BO->user_back());
    return;
  }

  // If this is an add tree that is used by a sub instruction, ignore it
  // until we process the subtract.
  if (BO->hasOneUse() && BO->getOpcode() == Instruction::Add &&
      cast<Instruction>(BO->user_back())->getOpcode() == Instruction::Sub)
    return;
  if (BO->hasOneUse() && BO->getOpcode() == Instruction::FAdd &&
      cast<Instruction>(BO->user_back())->getOpcode() == Instruction::FSub)
    return;

  ReassociateExpression(BO);
}

void ReassociatePass::ReassociateExpression(BinaryOperator *I) {
  // First, walk the expression tree, linearizing the tree, collecting the
  // operand information.
  SmallVector<RepeatedValue, 8> Tree;
  OverflowTracking Flags;
  MadeChange |= LinearizeExprTree(I, Tree, RedoInsts, Flags);
  SmallVector<ValueEntry, 8> Ops;
  Ops.reserve(Tree.size());
  for (const RepeatedValue &E : Tree)
    Ops.append(E.second, ValueEntry(getRank(E.first), E.first));

  LLVM_DEBUG(dbgs() << "RAIn:\t"; PrintOps(I, Ops); dbgs() << '\n');

  // Now that we have linearized the tree to a list and have gathered all of
  // the operands and their ranks, sort the operands by their rank.  Use a
  // stable_sort so that values with equal ranks will have their relative
  // positions maintained (and so the compiler is deterministic).  Note that
  // this sorts so that the highest ranking values end up at the beginning of
  // the vector.
  llvm::stable_sort(Ops);

  // Now that we have the expression tree in a convenient
  // sorted form, optimize it globally if possible.
  if (Value *V = OptimizeExpression(I, Ops)) {
    if (V == I)
      // Self-referential expression in unreachable code.
      return;
    // This expression tree simplified to something that isn't a tree,
    // eliminate it.
    LLVM_DEBUG(dbgs() << "Reassoc to scalar: " << *V << '\n');
    I->replaceAllUsesWith(V);
    if (Instruction *VI = dyn_cast<Instruction>(V))
      if (I->getDebugLoc())
        VI->setDebugLoc(I->getDebugLoc());
    RedoInsts.insert(I);
    ++NumAnnihil;
    return;
  }

  // We want to sink immediates as deeply as possible except in the case where
  // this is a multiply tree used only by an add, and the immediate is a -1.
  // In this case we reassociate to put the negation on the outside so that we
  // can fold the negation into the add: (-X)*Y + Z -> Z-X*Y
  if (I->hasOneUse()) {
    if (I->getOpcode() == Instruction::Mul &&
        cast<Instruction>(I->user_back())->getOpcode() == Instruction::Add &&
        isa<ConstantInt>(Ops.back().Op) &&
        cast<ConstantInt>(Ops.back().Op)->isMinusOne()) {
      ValueEntry Tmp = Ops.pop_back_val();
      Ops.insert(Ops.begin(), Tmp);
    } else if (I->getOpcode() == Instruction::FMul &&
               cast<Instruction>(I->user_back())->getOpcode() ==
                   Instruction::FAdd &&
               isa<ConstantFP>(Ops.back().Op) &&
               cast<ConstantFP>(Ops.back().Op)->isExactlyValue(-1.0)) {
      ValueEntry Tmp = Ops.pop_back_val();
      Ops.insert(Ops.begin(), Tmp);
    }
  }

  LLVM_DEBUG(dbgs() << "RAOut:\t"; PrintOps(I, Ops); dbgs() << '\n');

  if (Ops.size() == 1) {
    if (Ops[0].Op == I)
      // Self-referential expression in unreachable code.
      return;

    // This expression tree simplified to something that isn't a tree,
    // eliminate it.
    I->replaceAllUsesWith(Ops[0].Op);
    if (Instruction *OI = dyn_cast<Instruction>(Ops[0].Op))
      OI->setDebugLoc(I->getDebugLoc());
    RedoInsts.insert(I);
    return;
  }

  if (Ops.size() > 2 && Ops.size() <= GlobalReassociateLimit) {
    // Find the pair with the highest count in the pairmap and move it to the
    // back of the list so that it can later be CSE'd.
    // example:
    //   a*b*c*d*e
    // if c*e is the most "popular" pair, we can express this as
    //   (((c*e)*d)*b)*a
    unsigned Max = 1;
    unsigned BestRank = 0;
    std::pair<unsigned, unsigned> BestPair;
    unsigned Idx = I->getOpcode() - Instruction::BinaryOpsBegin;
    unsigned LimitIdx = 0;
    // With the CSE-driven heuristic, we are about to slap two values at the
    // beginning of the expression whereas they could live very late in the CFG.
    // When using the CSE-local heuristic we avoid creating dependences from
    // completely unrelated part of the CFG by limiting the expression
    // reordering on the values that live in the first seen basic block.
    // The main idea is that we want to avoid forming expressions that would
    // become loop dependent.
    if (UseCSELocalOpt) {
      const BasicBlock *FirstSeenBB = nullptr;
      int StartIdx = Ops.size() - 1;
      // Skip the first value of the expression since we need at least two
      // values to materialize an expression. I.e., even if this value is
      // anchored in a different basic block, the actual first sub expression
      // will be anchored on the second value.
      for (int i = StartIdx - 1; i != -1; --i) {
        const Value *Val = Ops[i].Op;
        const auto *CurrLeafInstr = dyn_cast<Instruction>(Val);
        const BasicBlock *SeenBB = nullptr;
        if (!CurrLeafInstr) {
          // The value is free of any CFG dependencies.
          // Do as if it lives in the entry block.
          //
          // We do this to make sure all the values falling on this path are
          // seen through the same anchor point. The rationale is these values
          // can be combined together to from a sub expression free of any CFG
          // dependencies so we want them to stay together.
          // We could be cleverer and postpone the anchor down to the first
          // anchored value, but that's likely complicated to get right.
          // E.g., we wouldn't want to do that if that means being stuck in a
          // loop.
          //
          // For instance, we wouldn't want to change:
          // res = arg1 op arg2 op arg3 op ... op loop_val1 op loop_val2 ...
          // into
          // res = loop_val1 op arg1 op arg2 op arg3 op ... op loop_val2 ...
          // Because all the sub expressions with arg2..N would be stuck between
          // two loop dependent values.
          SeenBB = &I->getParent()->getParent()->getEntryBlock();
        } else {
          SeenBB = CurrLeafInstr->getParent();
        }

        if (!FirstSeenBB) {
          FirstSeenBB = SeenBB;
          continue;
        }
        if (FirstSeenBB != SeenBB) {
          // ith value is in a different basic block.
          // Rewind the index once to point to the last value on the same basic
          // block.
          LimitIdx = i + 1;
          LLVM_DEBUG(dbgs() << "CSE reordering: Consider values between ["
                            << LimitIdx << ", " << StartIdx << "]\n");
          break;
        }
      }
    }
    for (unsigned i = Ops.size() - 1; i > LimitIdx; --i) {
      // We must use int type to go below zero when LimitIdx is 0.
      for (int j = i - 1; j >= (int)LimitIdx; --j) {
        unsigned Score = 0;
        Value *Op0 = Ops[i].Op;
        Value *Op1 = Ops[j].Op;
        if (std::less<Value *>()(Op1, Op0))
          std::swap(Op0, Op1);
        auto it = PairMap[Idx].find({Op0, Op1});
        if (it != PairMap[Idx].end()) {
          // Functions like BreakUpSubtract() can erase the Values we're using
          // as keys and create new Values after we built the PairMap. There's a
          // small chance that the new nodes can have the same address as
          // something already in the table. We shouldn't accumulate the stored
          // score in that case as it refers to the wrong Value.
          if (it->second.isValid())
            Score += it->second.Score;
        }

        unsigned MaxRank = std::max(Ops[i].Rank, Ops[j].Rank);

        // By construction, the operands are sorted in reverse order of their
        // topological order.
        // So we tend to form (sub) expressions with values that are close to
        // each other.
        //
        // Now to expose more CSE opportunities we want to expose the pair of
        // operands that occur the most (as statically computed in
        // BuildPairMap.) as the first sub-expression.
        //
        // If two pairs occur as many times, we pick the one with the
        // lowest rank, meaning the one with both operands appearing first in
        // the topological order.
        if (Score > Max || (Score == Max && MaxRank < BestRank)) {
          BestPair = {j, i};
          Max = Score;
          BestRank = MaxRank;
        }
      }
    }
    if (Max > 1) {
      auto Op0 = Ops[BestPair.first];
      auto Op1 = Ops[BestPair.second];
      Ops.erase(&Ops[BestPair.second]);
      Ops.erase(&Ops[BestPair.first]);
      Ops.push_back(Op0);
      Ops.push_back(Op1);
    }
  }
  LLVM_DEBUG(dbgs() << "RAOut after CSE reorder:\t"; PrintOps(I, Ops);
             dbgs() << '\n');
  // Now that we ordered and optimized the expressions, splat them back into
  // the expression tree, removing any unneeded nodes.
  RewriteExprTree(I, Ops, Flags);
}

void
ReassociatePass::BuildPairMap(ReversePostOrderTraversal<Function *> &RPOT) {
  // Make a "pairmap" of how often each operand pair occurs.
  for (BasicBlock *BI : RPOT) {
    for (Instruction &I : *BI) {
      if (!I.isAssociative() || !I.isBinaryOp())
        continue;

      // Ignore nodes that aren't at the root of trees.
      if (I.hasOneUse() && I.user_back()->getOpcode() == I.getOpcode())
        continue;

      // Collect all operands in a single reassociable expression.
      // Since Reassociate has already been run once, we can assume things
      // are already canonical according to Reassociation's regime.
      SmallVector<Value *, 8> Worklist = { I.getOperand(0), I.getOperand(1) };
      SmallVector<Value *, 8> Ops;
      while (!Worklist.empty() && Ops.size() <= GlobalReassociateLimit) {
        Value *Op = Worklist.pop_back_val();
        Instruction *OpI = dyn_cast<Instruction>(Op);
        if (!OpI || OpI->getOpcode() != I.getOpcode() || !OpI->hasOneUse()) {
          Ops.push_back(Op);
          continue;
        }
        // Be paranoid about self-referencing expressions in unreachable code.
        if (OpI->getOperand(0) != OpI)
          Worklist.push_back(OpI->getOperand(0));
        if (OpI->getOperand(1) != OpI)
          Worklist.push_back(OpI->getOperand(1));
      }
      // Skip extremely long expressions.
      if (Ops.size() > GlobalReassociateLimit)
        continue;

      // Add all pairwise combinations of operands to the pair map.
      unsigned BinaryIdx = I.getOpcode() - Instruction::BinaryOpsBegin;
      SmallSet<std::pair<Value *, Value*>, 32> Visited;
      for (unsigned i = 0; i < Ops.size() - 1; ++i) {
        for (unsigned j = i + 1; j < Ops.size(); ++j) {
          // Canonicalize operand orderings.
          Value *Op0 = Ops[i];
          Value *Op1 = Ops[j];
          if (std::less<Value *>()(Op1, Op0))
            std::swap(Op0, Op1);
          if (!Visited.insert({Op0, Op1}).second)
            continue;
          auto res = PairMap[BinaryIdx].insert({{Op0, Op1}, {Op0, Op1, 1}});
          if (!res.second) {
            // If either key value has been erased then we've got the same
            // address by coincidence. That can't happen here because nothing is
            // erasing values but it can happen by the time we're querying the
            // map.
            assert(res.first->second.isValid() && "WeakVH invalidated");
            ++res.first->second.Score;
          }
        }
      }
    }
  }
}

PreservedAnalyses ReassociatePass::run(Function &F, FunctionAnalysisManager &) {
  // Get the functions basic blocks in Reverse Post Order. This order is used by
  // BuildRankMap to pre calculate ranks correctly. It also excludes dead basic
  // blocks (it has been seen that the analysis in this pass could hang when
  // analysing dead basic blocks).
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // Calculate the rank map for F.
  BuildRankMap(F, RPOT);

  // Build the pair map before running reassociate.
  // Technically this would be more accurate if we did it after one round
  // of reassociation, but in practice it doesn't seem to help much on
  // real-world code, so don't waste the compile time running reassociate
  // twice.
  // If a user wants, they could expicitly run reassociate twice in their
  // pass pipeline for further potential gains.
  // It might also be possible to update the pair map during runtime, but the
  // overhead of that may be large if there's many reassociable chains.
  BuildPairMap(RPOT);

  MadeChange = false;

  // Traverse the same blocks that were analysed by BuildRankMap.
  for (BasicBlock *BI : RPOT) {
    assert(RankMap.count(&*BI) && "BB should be ranked.");
    // Optimize every instruction in the basic block.
    for (BasicBlock::iterator II = BI->begin(), IE = BI->end(); II != IE;)
      if (isInstructionTriviallyDead(&*II)) {
        EraseInst(&*II++);
      } else {
        OptimizeInst(&*II);
        assert(II->getParent() == &*BI && "Moved to a different block!");
        ++II;
      }

    // Make a copy of all the instructions to be redone so we can remove dead
    // instructions.
    OrderedSet ToRedo(RedoInsts);
    // Iterate over all instructions to be reevaluated and remove trivially dead
    // instructions. If any operand of the trivially dead instruction becomes
    // dead mark it for deletion as well. Continue this process until all
    // trivially dead instructions have been removed.
    while (!ToRedo.empty()) {
      Instruction *I = ToRedo.pop_back_val();
      if (isInstructionTriviallyDead(I)) {
        RecursivelyEraseDeadInsts(I, ToRedo);
        MadeChange = true;
      }
    }

    // Now that we have removed dead instructions, we can reoptimize the
    // remaining instructions.
    while (!RedoInsts.empty()) {
      Instruction *I = RedoInsts.front();
      RedoInsts.erase(RedoInsts.begin());
      if (isInstructionTriviallyDead(I))
        EraseInst(I);
      else
        OptimizeInst(I);
    }
  }

  // We are done with the rank map and pair map.
  RankMap.clear();
  ValueRankMap.clear();
  for (auto &Entry : PairMap)
    Entry.clear();

  if (MadeChange) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }

  return PreservedAnalyses::all();
}

namespace {

  class ReassociateLegacyPass : public FunctionPass {
    ReassociatePass Impl;

  public:
    static char ID; // Pass identification, replacement for typeid

    ReassociateLegacyPass() : FunctionPass(ID) {
      initializeReassociateLegacyPassPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override {
      if (skipFunction(F))
        return false;

      FunctionAnalysisManager DummyFAM;
      auto PA = Impl.run(F, DummyFAM);
      return !PA.areAllPreserved();
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addPreserved<AAResultsWrapperPass>();
      AU.addPreserved<BasicAAWrapperPass>();
      AU.addPreserved<GlobalsAAWrapperPass>();
    }
  };

} // end anonymous namespace

char ReassociateLegacyPass::ID = 0;

INITIALIZE_PASS(ReassociateLegacyPass, "reassociate",
                "Reassociate expressions", false, false)

// Public interface to the Reassociate pass
FunctionPass *llvm::createReassociatePass() {
  return new ReassociateLegacyPass();
}
