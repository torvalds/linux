//===-- X86PartialReduction.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass looks for add instructions used by a horizontal reduction to see
// if we might be able to use pmaddwd or psadbw. Some cases of this require
// cross basic block knowledge and can't be done in SelectionDAG.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86TargetMachine.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

#define DEBUG_TYPE "x86-partial-reduction"

namespace {

class X86PartialReduction : public FunctionPass {
  const DataLayout *DL = nullptr;
  const X86Subtarget *ST = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid.

  X86PartialReduction() : FunctionPass(ID) { }

  bool runOnFunction(Function &Fn) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  StringRef getPassName() const override {
    return "X86 Partial Reduction";
  }

private:
  bool tryMAddReplacement(Instruction *Op, bool ReduceInOneBB);
  bool trySADReplacement(Instruction *Op);
};
}

FunctionPass *llvm::createX86PartialReductionPass() {
  return new X86PartialReduction();
}

char X86PartialReduction::ID = 0;

INITIALIZE_PASS(X86PartialReduction, DEBUG_TYPE,
                "X86 Partial Reduction", false, false)

// This function should be aligned with detectExtMul() in X86ISelLowering.cpp.
static bool matchVPDPBUSDPattern(const X86Subtarget *ST, BinaryOperator *Mul,
                                 const DataLayout *DL) {
  if (!ST->hasVNNI() && !ST->hasAVXVNNI())
    return false;

  Value *LHS = Mul->getOperand(0);
  Value *RHS = Mul->getOperand(1);

  if (isa<SExtInst>(LHS))
    std::swap(LHS, RHS);

  auto IsFreeTruncation = [&](Value *Op) {
    if (auto *Cast = dyn_cast<CastInst>(Op)) {
      if (Cast->getParent() == Mul->getParent() &&
          (Cast->getOpcode() == Instruction::SExt ||
           Cast->getOpcode() == Instruction::ZExt) &&
          Cast->getOperand(0)->getType()->getScalarSizeInBits() <= 8)
        return true;
    }

    return isa<Constant>(Op);
  };

  // (dpbusd (zext a), (sext, b)). Since the first operand should be unsigned
  // value, we need to check LHS is zero extended value. RHS should be signed
  // value, so we just check the signed bits.
  if ((IsFreeTruncation(LHS) &&
       computeKnownBits(LHS, *DL).countMaxActiveBits() <= 8) &&
      (IsFreeTruncation(RHS) && ComputeMaxSignificantBits(RHS, *DL) <= 8))
    return true;

  return false;
}

bool X86PartialReduction::tryMAddReplacement(Instruction *Op,
                                             bool ReduceInOneBB) {
  if (!ST->hasSSE2())
    return false;

  // Need at least 8 elements.
  if (cast<FixedVectorType>(Op->getType())->getNumElements() < 8)
    return false;

  // Element type should be i32.
  if (!cast<VectorType>(Op->getType())->getElementType()->isIntegerTy(32))
    return false;

  auto *Mul = dyn_cast<BinaryOperator>(Op);
  if (!Mul || Mul->getOpcode() != Instruction::Mul)
    return false;

  Value *LHS = Mul->getOperand(0);
  Value *RHS = Mul->getOperand(1);

  // If the target support VNNI, leave it to ISel to combine reduce operation
  // to VNNI instruction.
  // TODO: we can support transforming reduce to VNNI intrinsic for across block
  // in this pass.
  if (ReduceInOneBB && matchVPDPBUSDPattern(ST, Mul, DL))
    return false;

  // LHS and RHS should be only used once or if they are the same then only
  // used twice. Only check this when SSE4.1 is enabled and we have zext/sext
  // instructions, otherwise we use punpck to emulate zero extend in stages. The
  // trunc/ we need to do likely won't introduce new instructions in that case.
  if (ST->hasSSE41()) {
    if (LHS == RHS) {
      if (!isa<Constant>(LHS) && !LHS->hasNUses(2))
        return false;
    } else {
      if (!isa<Constant>(LHS) && !LHS->hasOneUse())
        return false;
      if (!isa<Constant>(RHS) && !RHS->hasOneUse())
        return false;
    }
  }

  auto CanShrinkOp = [&](Value *Op) {
    auto IsFreeTruncation = [&](Value *Op) {
      if (auto *Cast = dyn_cast<CastInst>(Op)) {
        if (Cast->getParent() == Mul->getParent() &&
            (Cast->getOpcode() == Instruction::SExt ||
             Cast->getOpcode() == Instruction::ZExt) &&
            Cast->getOperand(0)->getType()->getScalarSizeInBits() <= 16)
          return true;
      }

      return isa<Constant>(Op);
    };

    // If the operation can be freely truncated and has enough sign bits we
    // can shrink.
    if (IsFreeTruncation(Op) &&
        ComputeNumSignBits(Op, *DL, 0, nullptr, Mul) > 16)
      return true;

    // SelectionDAG has limited support for truncating through an add or sub if
    // the inputs are freely truncatable.
    if (auto *BO = dyn_cast<BinaryOperator>(Op)) {
      if (BO->getParent() == Mul->getParent() &&
          IsFreeTruncation(BO->getOperand(0)) &&
          IsFreeTruncation(BO->getOperand(1)) &&
          ComputeNumSignBits(Op, *DL, 0, nullptr, Mul) > 16)
        return true;
    }

    return false;
  };

  // Both Ops need to be shrinkable.
  if (!CanShrinkOp(LHS) && !CanShrinkOp(RHS))
    return false;

  IRBuilder<> Builder(Mul);

  auto *MulTy = cast<FixedVectorType>(Op->getType());
  unsigned NumElts = MulTy->getNumElements();

  // Extract even elements and odd elements and add them together. This will
  // be pattern matched by SelectionDAG to pmaddwd. This instruction will be
  // half the original width.
  SmallVector<int, 16> EvenMask(NumElts / 2);
  SmallVector<int, 16> OddMask(NumElts / 2);
  for (int i = 0, e = NumElts / 2; i != e; ++i) {
    EvenMask[i] = i * 2;
    OddMask[i] = i * 2 + 1;
  }
  // Creating a new mul so the replaceAllUsesWith below doesn't replace the
  // uses in the shuffles we're creating.
  Value *NewMul = Builder.CreateMul(Mul->getOperand(0), Mul->getOperand(1));
  Value *EvenElts = Builder.CreateShuffleVector(NewMul, NewMul, EvenMask);
  Value *OddElts = Builder.CreateShuffleVector(NewMul, NewMul, OddMask);
  Value *MAdd = Builder.CreateAdd(EvenElts, OddElts);

  // Concatenate zeroes to extend back to the original type.
  SmallVector<int, 32> ConcatMask(NumElts);
  std::iota(ConcatMask.begin(), ConcatMask.end(), 0);
  Value *Zero = Constant::getNullValue(MAdd->getType());
  Value *Concat = Builder.CreateShuffleVector(MAdd, Zero, ConcatMask);

  Mul->replaceAllUsesWith(Concat);
  Mul->eraseFromParent();

  return true;
}

bool X86PartialReduction::trySADReplacement(Instruction *Op) {
  if (!ST->hasSSE2())
    return false;

  // TODO: There's nothing special about i32, any integer type above i16 should
  // work just as well.
  if (!cast<VectorType>(Op->getType())->getElementType()->isIntegerTy(32))
    return false;

  Value *LHS;
  if (match(Op, PatternMatch::m_Intrinsic<Intrinsic::abs>())) {
    LHS = Op->getOperand(0);
  } else {
    // Operand should be a select.
    auto *SI = dyn_cast<SelectInst>(Op);
    if (!SI)
      return false;

    Value *RHS;
    // Select needs to implement absolute value.
    auto SPR = matchSelectPattern(SI, LHS, RHS);
    if (SPR.Flavor != SPF_ABS)
      return false;
  }

  // Need a subtract of two values.
  auto *Sub = dyn_cast<BinaryOperator>(LHS);
  if (!Sub || Sub->getOpcode() != Instruction::Sub)
    return false;

  // Look for zero extend from i8.
  auto getZeroExtendedVal = [](Value *Op) -> Value * {
    if (auto *ZExt = dyn_cast<ZExtInst>(Op))
      if (cast<VectorType>(ZExt->getOperand(0)->getType())
              ->getElementType()
              ->isIntegerTy(8))
        return ZExt->getOperand(0);

    return nullptr;
  };

  // Both operands of the subtract should be extends from vXi8.
  Value *Op0 = getZeroExtendedVal(Sub->getOperand(0));
  Value *Op1 = getZeroExtendedVal(Sub->getOperand(1));
  if (!Op0 || !Op1)
    return false;

  IRBuilder<> Builder(Op);

  auto *OpTy = cast<FixedVectorType>(Op->getType());
  unsigned NumElts = OpTy->getNumElements();

  unsigned IntrinsicNumElts;
  Intrinsic::ID IID;
  if (ST->hasBWI() && NumElts >= 64) {
    IID = Intrinsic::x86_avx512_psad_bw_512;
    IntrinsicNumElts = 64;
  } else if (ST->hasAVX2() && NumElts >= 32) {
    IID = Intrinsic::x86_avx2_psad_bw;
    IntrinsicNumElts = 32;
  } else {
    IID = Intrinsic::x86_sse2_psad_bw;
    IntrinsicNumElts = 16;
  }

  Function *PSADBWFn = Intrinsic::getDeclaration(Op->getModule(), IID);

  if (NumElts < 16) {
    // Pad input with zeroes.
    SmallVector<int, 32> ConcatMask(16);
    for (unsigned i = 0; i != NumElts; ++i)
      ConcatMask[i] = i;
    for (unsigned i = NumElts; i != 16; ++i)
      ConcatMask[i] = (i % NumElts) + NumElts;

    Value *Zero = Constant::getNullValue(Op0->getType());
    Op0 = Builder.CreateShuffleVector(Op0, Zero, ConcatMask);
    Op1 = Builder.CreateShuffleVector(Op1, Zero, ConcatMask);
    NumElts = 16;
  }

  // Intrinsics produce vXi64 and need to be casted to vXi32.
  auto *I32Ty =
      FixedVectorType::get(Builder.getInt32Ty(), IntrinsicNumElts / 4);

  assert(NumElts % IntrinsicNumElts == 0 && "Unexpected number of elements!");
  unsigned NumSplits = NumElts / IntrinsicNumElts;

  // First collect the pieces we need.
  SmallVector<Value *, 4> Ops(NumSplits);
  for (unsigned i = 0; i != NumSplits; ++i) {
    SmallVector<int, 64> ExtractMask(IntrinsicNumElts);
    std::iota(ExtractMask.begin(), ExtractMask.end(), i * IntrinsicNumElts);
    Value *ExtractOp0 = Builder.CreateShuffleVector(Op0, Op0, ExtractMask);
    Value *ExtractOp1 = Builder.CreateShuffleVector(Op1, Op0, ExtractMask);
    Ops[i] = Builder.CreateCall(PSADBWFn, {ExtractOp0, ExtractOp1});
    Ops[i] = Builder.CreateBitCast(Ops[i], I32Ty);
  }

  assert(isPowerOf2_32(NumSplits) && "Expected power of 2 splits");
  unsigned Stages = Log2_32(NumSplits);
  for (unsigned s = Stages; s > 0; --s) {
    unsigned NumConcatElts =
        cast<FixedVectorType>(Ops[0]->getType())->getNumElements() * 2;
    for (unsigned i = 0; i != 1U << (s - 1); ++i) {
      SmallVector<int, 64> ConcatMask(NumConcatElts);
      std::iota(ConcatMask.begin(), ConcatMask.end(), 0);
      Ops[i] = Builder.CreateShuffleVector(Ops[i*2], Ops[i*2+1], ConcatMask);
    }
  }

  // At this point the final value should be in Ops[0]. Now we need to adjust
  // it to the final original type.
  NumElts = cast<FixedVectorType>(OpTy)->getNumElements();
  if (NumElts == 2) {
    // Extract down to 2 elements.
    Ops[0] = Builder.CreateShuffleVector(Ops[0], Ops[0], ArrayRef<int>{0, 1});
  } else if (NumElts >= 8) {
    SmallVector<int, 32> ConcatMask(NumElts);
    unsigned SubElts =
        cast<FixedVectorType>(Ops[0]->getType())->getNumElements();
    for (unsigned i = 0; i != SubElts; ++i)
      ConcatMask[i] = i;
    for (unsigned i = SubElts; i != NumElts; ++i)
      ConcatMask[i] = (i % SubElts) + SubElts;

    Value *Zero = Constant::getNullValue(Ops[0]->getType());
    Ops[0] = Builder.CreateShuffleVector(Ops[0], Zero, ConcatMask);
  }

  Op->replaceAllUsesWith(Ops[0]);
  Op->eraseFromParent();

  return true;
}

// Walk backwards from the ExtractElementInst and determine if it is the end of
// a horizontal reduction. Return the input to the reduction if we find one.
static Value *matchAddReduction(const ExtractElementInst &EE,
                                bool &ReduceInOneBB) {
  ReduceInOneBB = true;
  // Make sure we're extracting index 0.
  auto *Index = dyn_cast<ConstantInt>(EE.getIndexOperand());
  if (!Index || !Index->isNullValue())
    return nullptr;

  const auto *BO = dyn_cast<BinaryOperator>(EE.getVectorOperand());
  if (!BO || BO->getOpcode() != Instruction::Add || !BO->hasOneUse())
    return nullptr;
  if (EE.getParent() != BO->getParent())
    ReduceInOneBB = false;

  unsigned NumElems = cast<FixedVectorType>(BO->getType())->getNumElements();
  // Ensure the reduction size is a power of 2.
  if (!isPowerOf2_32(NumElems))
    return nullptr;

  const Value *Op = BO;
  unsigned Stages = Log2_32(NumElems);
  for (unsigned i = 0; i != Stages; ++i) {
    const auto *BO = dyn_cast<BinaryOperator>(Op);
    if (!BO || BO->getOpcode() != Instruction::Add)
      return nullptr;
    if (EE.getParent() != BO->getParent())
      ReduceInOneBB = false;

    // If this isn't the first add, then it should only have 2 users, the
    // shuffle and another add which we checked in the previous iteration.
    if (i != 0 && !BO->hasNUses(2))
      return nullptr;

    Value *LHS = BO->getOperand(0);
    Value *RHS = BO->getOperand(1);

    auto *Shuffle = dyn_cast<ShuffleVectorInst>(LHS);
    if (Shuffle) {
      Op = RHS;
    } else {
      Shuffle = dyn_cast<ShuffleVectorInst>(RHS);
      Op = LHS;
    }

    // The first operand of the shuffle should be the same as the other operand
    // of the bin op.
    if (!Shuffle || Shuffle->getOperand(0) != Op)
      return nullptr;

    // Verify the shuffle has the expected (at this stage of the pyramid) mask.
    unsigned MaskEnd = 1 << i;
    for (unsigned Index = 0; Index < MaskEnd; ++Index)
      if (Shuffle->getMaskValue(Index) != (int)(MaskEnd + Index))
        return nullptr;
  }

  return const_cast<Value *>(Op);
}

// See if this BO is reachable from this Phi by walking forward through single
// use BinaryOperators with the same opcode. If we get back then we know we've
// found a loop and it is safe to step through this Add to find more leaves.
static bool isReachableFromPHI(PHINode *Phi, BinaryOperator *BO) {
  // The PHI itself should only have one use.
  if (!Phi->hasOneUse())
    return false;

  Instruction *U = cast<Instruction>(*Phi->user_begin());
  if (U == BO)
    return true;

  while (U->hasOneUse() && U->getOpcode() == BO->getOpcode())
    U = cast<Instruction>(*U->user_begin());

  return U == BO;
}

// Collect all the leaves of the tree of adds that feeds into the horizontal
// reduction. Root is the Value that is used by the horizontal reduction.
// We look through single use phis, single use adds, or adds that are used by
// a phi that forms a loop with the add.
static void collectLeaves(Value *Root, SmallVectorImpl<Instruction *> &Leaves) {
  SmallPtrSet<Value *, 8> Visited;
  SmallVector<Value *, 8> Worklist;
  Worklist.push_back(Root);

  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    if (auto *PN = dyn_cast<PHINode>(V)) {
      // PHI node should have single use unless it is the root node, then it
      // has 2 uses.
      if (!PN->hasNUses(PN == Root ? 2 : 1))
        break;

      // Push incoming values to the worklist.
      append_range(Worklist, PN->incoming_values());

      continue;
    }

    if (auto *BO = dyn_cast<BinaryOperator>(V)) {
      if (BO->getOpcode() == Instruction::Add) {
        // Simple case. Single use, just push its operands to the worklist.
        if (BO->hasNUses(BO == Root ? 2 : 1)) {
          append_range(Worklist, BO->operands());
          continue;
        }

        // If there is additional use, make sure it is an unvisited phi that
        // gets us back to this node.
        if (BO->hasNUses(BO == Root ? 3 : 2)) {
          PHINode *PN = nullptr;
          for (auto *U : BO->users())
            if (auto *P = dyn_cast<PHINode>(U))
              if (!Visited.count(P))
                PN = P;

          // If we didn't find a 2-input PHI then this isn't a case we can
          // handle.
          if (!PN || PN->getNumIncomingValues() != 2)
            continue;

          // Walk forward from this phi to see if it reaches back to this add.
          if (!isReachableFromPHI(PN, BO))
            continue;

          // The phi forms a loop with this Add, push its operands.
          append_range(Worklist, BO->operands());
        }
      }
    }

    // Not an add or phi, make it a leaf.
    if (auto *I = dyn_cast<Instruction>(V)) {
      if (!V->hasNUses(I == Root ? 2 : 1))
        continue;

      // Add this as a leaf.
      Leaves.push_back(I);
    }
  }
}

bool X86PartialReduction::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  auto &TM = TPC->getTM<X86TargetMachine>();
  ST = TM.getSubtargetImpl(F);

  DL = &F.getDataLayout();

  bool MadeChange = false;
  for (auto &BB : F) {
    for (auto &I : BB) {
      auto *EE = dyn_cast<ExtractElementInst>(&I);
      if (!EE)
        continue;

      bool ReduceInOneBB;
      // First find a reduction tree.
      // FIXME: Do we need to handle other opcodes than Add?
      Value *Root = matchAddReduction(*EE, ReduceInOneBB);
      if (!Root)
        continue;

      SmallVector<Instruction *, 8> Leaves;
      collectLeaves(Root, Leaves);

      for (Instruction *I : Leaves) {
        if (tryMAddReplacement(I, ReduceInOneBB)) {
          MadeChange = true;
          continue;
        }

        // Don't do SAD matching on the root node. SelectionDAG already
        // has support for that and currently generates better code.
        if (I != Root && trySADReplacement(I))
          MadeChange = true;
      }
    }
  }

  return MadeChange;
}
