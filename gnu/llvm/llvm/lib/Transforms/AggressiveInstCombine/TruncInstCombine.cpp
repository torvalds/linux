//===- TruncInstCombine.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TruncInstCombine - looks for expression graphs post-dominated by TruncInst
// and for each eligible graph, it will create a reduced bit-width expression,
// replace the old expression with this new one and remove the old expression.
// Eligible expression graph is such that:
//   1. Contains only supported instructions.
//   2. Supported leaves: ZExtInst, SExtInst, TruncInst and Constant value.
//   3. Can be evaluated into type with reduced legal bit-width.
//   4. All instructions in the graph must not have users outside the graph.
//      The only exception is for {ZExt, SExt}Inst with operand type equal to
//      the new reduced type evaluated in (3).
//
// The motivation for this optimization is that evaluating and expression using
// smaller bit-width is preferable, especially for vectorization where we can
// fit more values in one vectorized instruction. In addition, this optimization
// may decrease the number of cast instructions, but will not increase it.
//
//===----------------------------------------------------------------------===//

#include "AggressiveInstCombineInternal.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

#define DEBUG_TYPE "aggressive-instcombine"

STATISTIC(NumExprsReduced, "Number of truncations eliminated by reducing bit "
                           "width of expression graph");
STATISTIC(NumInstrsReduced,
          "Number of instructions whose bit width was reduced");

/// Given an instruction and a container, it fills all the relevant operands of
/// that instruction, with respect to the Trunc expression graph optimizaton.
static void getRelevantOperands(Instruction *I, SmallVectorImpl<Value *> &Ops) {
  unsigned Opc = I->getOpcode();
  switch (Opc) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
    // These CastInst are considered leaves of the evaluated expression, thus,
    // their operands are not relevent.
    break;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::UDiv:
  case Instruction::URem:
  case Instruction::InsertElement:
    Ops.push_back(I->getOperand(0));
    Ops.push_back(I->getOperand(1));
    break;
  case Instruction::ExtractElement:
    Ops.push_back(I->getOperand(0));
    break;
  case Instruction::Select:
    Ops.push_back(I->getOperand(1));
    Ops.push_back(I->getOperand(2));
    break;
  case Instruction::PHI:
    for (Value *V : cast<PHINode>(I)->incoming_values())
      Ops.push_back(V);
    break;
  default:
    llvm_unreachable("Unreachable!");
  }
}

bool TruncInstCombine::buildTruncExpressionGraph() {
  SmallVector<Value *, 8> Worklist;
  SmallVector<Instruction *, 8> Stack;
  // Clear old instructions info.
  InstInfoMap.clear();

  Worklist.push_back(CurrentTruncInst->getOperand(0));

  while (!Worklist.empty()) {
    Value *Curr = Worklist.back();

    if (isa<Constant>(Curr)) {
      Worklist.pop_back();
      continue;
    }

    auto *I = dyn_cast<Instruction>(Curr);
    if (!I)
      return false;

    if (!Stack.empty() && Stack.back() == I) {
      // Already handled all instruction operands, can remove it from both the
      // Worklist and the Stack, and add it to the instruction info map.
      Worklist.pop_back();
      Stack.pop_back();
      // Insert I to the Info map.
      InstInfoMap.insert(std::make_pair(I, Info()));
      continue;
    }

    if (InstInfoMap.count(I)) {
      Worklist.pop_back();
      continue;
    }

    // Add the instruction to the stack before start handling its operands.
    Stack.push_back(I);

    unsigned Opc = I->getOpcode();
    switch (Opc) {
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
      // trunc(trunc(x)) -> trunc(x)
      // trunc(ext(x)) -> ext(x) if the source type is smaller than the new dest
      // trunc(ext(x)) -> trunc(x) if the source type is larger than the new
      // dest
      break;
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::UDiv:
    case Instruction::URem:
    case Instruction::InsertElement:
    case Instruction::ExtractElement:
    case Instruction::Select: {
      SmallVector<Value *, 2> Operands;
      getRelevantOperands(I, Operands);
      append_range(Worklist, Operands);
      break;
    }
    case Instruction::PHI: {
      SmallVector<Value *, 2> Operands;
      getRelevantOperands(I, Operands);
      // Add only operands not in Stack to prevent cycle
      for (auto *Op : Operands)
        if (!llvm::is_contained(Stack, Op))
          Worklist.push_back(Op);
      break;
    }
    default:
      // TODO: Can handle more cases here:
      // 1. shufflevector
      // 2. sdiv, srem
      // ...
      return false;
    }
  }
  return true;
}

unsigned TruncInstCombine::getMinBitWidth() {
  SmallVector<Value *, 8> Worklist;
  SmallVector<Instruction *, 8> Stack;

  Value *Src = CurrentTruncInst->getOperand(0);
  Type *DstTy = CurrentTruncInst->getType();
  unsigned TruncBitWidth = DstTy->getScalarSizeInBits();
  unsigned OrigBitWidth =
      CurrentTruncInst->getOperand(0)->getType()->getScalarSizeInBits();

  if (isa<Constant>(Src))
    return TruncBitWidth;

  Worklist.push_back(Src);
  InstInfoMap[cast<Instruction>(Src)].ValidBitWidth = TruncBitWidth;

  while (!Worklist.empty()) {
    Value *Curr = Worklist.back();

    if (isa<Constant>(Curr)) {
      Worklist.pop_back();
      continue;
    }

    // Otherwise, it must be an instruction.
    auto *I = cast<Instruction>(Curr);

    auto &Info = InstInfoMap[I];

    SmallVector<Value *, 2> Operands;
    getRelevantOperands(I, Operands);

    if (!Stack.empty() && Stack.back() == I) {
      // Already handled all instruction operands, can remove it from both, the
      // Worklist and the Stack, and update MinBitWidth.
      Worklist.pop_back();
      Stack.pop_back();
      for (auto *Operand : Operands)
        if (auto *IOp = dyn_cast<Instruction>(Operand))
          Info.MinBitWidth =
              std::max(Info.MinBitWidth, InstInfoMap[IOp].MinBitWidth);
      continue;
    }

    // Add the instruction to the stack before start handling its operands.
    Stack.push_back(I);
    unsigned ValidBitWidth = Info.ValidBitWidth;

    // Update minimum bit-width before handling its operands. This is required
    // when the instruction is part of a loop.
    Info.MinBitWidth = std::max(Info.MinBitWidth, Info.ValidBitWidth);

    for (auto *Operand : Operands)
      if (auto *IOp = dyn_cast<Instruction>(Operand)) {
        // If we already calculated the minimum bit-width for this valid
        // bit-width, or for a smaller valid bit-width, then just keep the
        // answer we already calculated.
        unsigned IOpBitwidth = InstInfoMap.lookup(IOp).ValidBitWidth;
        if (IOpBitwidth >= ValidBitWidth)
          continue;
        InstInfoMap[IOp].ValidBitWidth = ValidBitWidth;
        Worklist.push_back(IOp);
      }
  }
  unsigned MinBitWidth = InstInfoMap.lookup(cast<Instruction>(Src)).MinBitWidth;
  assert(MinBitWidth >= TruncBitWidth);

  if (MinBitWidth > TruncBitWidth) {
    // In this case reducing expression with vector type might generate a new
    // vector type, which is not preferable as it might result in generating
    // sub-optimal code.
    if (DstTy->isVectorTy())
      return OrigBitWidth;
    // Use the smallest integer type in the range [MinBitWidth, OrigBitWidth).
    Type *Ty = DL.getSmallestLegalIntType(DstTy->getContext(), MinBitWidth);
    // Update minimum bit-width with the new destination type bit-width if
    // succeeded to find such, otherwise, with original bit-width.
    MinBitWidth = Ty ? Ty->getScalarSizeInBits() : OrigBitWidth;
  } else { // MinBitWidth == TruncBitWidth
    // In this case the expression can be evaluated with the trunc instruction
    // destination type, and trunc instruction can be omitted. However, we
    // should not perform the evaluation if the original type is a legal scalar
    // type and the target type is illegal.
    bool FromLegal = MinBitWidth == 1 || DL.isLegalInteger(OrigBitWidth);
    bool ToLegal = MinBitWidth == 1 || DL.isLegalInteger(MinBitWidth);
    if (!DstTy->isVectorTy() && FromLegal && !ToLegal)
      return OrigBitWidth;
  }
  return MinBitWidth;
}

Type *TruncInstCombine::getBestTruncatedType() {
  if (!buildTruncExpressionGraph())
    return nullptr;

  // We don't want to duplicate instructions, which isn't profitable. Thus, we
  // can't shrink something that has multiple users, unless all users are
  // post-dominated by the trunc instruction, i.e., were visited during the
  // expression evaluation.
  unsigned DesiredBitWidth = 0;
  for (auto Itr : InstInfoMap) {
    Instruction *I = Itr.first;
    if (I->hasOneUse())
      continue;
    bool IsExtInst = (isa<ZExtInst>(I) || isa<SExtInst>(I));
    for (auto *U : I->users())
      if (auto *UI = dyn_cast<Instruction>(U))
        if (UI != CurrentTruncInst && !InstInfoMap.count(UI)) {
          if (!IsExtInst)
            return nullptr;
          // If this is an extension from the dest type, we can eliminate it,
          // even if it has multiple users. Thus, update the DesiredBitWidth and
          // validate all extension instructions agrees on same DesiredBitWidth.
          unsigned ExtInstBitWidth =
              I->getOperand(0)->getType()->getScalarSizeInBits();
          if (DesiredBitWidth && DesiredBitWidth != ExtInstBitWidth)
            return nullptr;
          DesiredBitWidth = ExtInstBitWidth;
        }
  }

  unsigned OrigBitWidth =
      CurrentTruncInst->getOperand(0)->getType()->getScalarSizeInBits();

  // Initialize MinBitWidth for shift instructions with the minimum number
  // that is greater than shift amount (i.e. shift amount + 1).
  // For `lshr` adjust MinBitWidth so that all potentially truncated
  // bits of the value-to-be-shifted are zeros.
  // For `ashr` adjust MinBitWidth so that all potentially truncated
  // bits of the value-to-be-shifted are sign bits (all zeros or ones)
  // and even one (first) untruncated bit is sign bit.
  // Exit early if MinBitWidth is not less than original bitwidth.
  for (auto &Itr : InstInfoMap) {
    Instruction *I = Itr.first;
    if (I->isShift()) {
      KnownBits KnownRHS = computeKnownBits(I->getOperand(1));
      unsigned MinBitWidth = KnownRHS.getMaxValue()
                                 .uadd_sat(APInt(OrigBitWidth, 1))
                                 .getLimitedValue(OrigBitWidth);
      if (MinBitWidth == OrigBitWidth)
        return nullptr;
      if (I->getOpcode() == Instruction::LShr) {
        KnownBits KnownLHS = computeKnownBits(I->getOperand(0));
        MinBitWidth =
            std::max(MinBitWidth, KnownLHS.getMaxValue().getActiveBits());
      }
      if (I->getOpcode() == Instruction::AShr) {
        unsigned NumSignBits = ComputeNumSignBits(I->getOperand(0));
        MinBitWidth = std::max(MinBitWidth, OrigBitWidth - NumSignBits + 1);
      }
      if (MinBitWidth >= OrigBitWidth)
        return nullptr;
      Itr.second.MinBitWidth = MinBitWidth;
    }
    if (I->getOpcode() == Instruction::UDiv ||
        I->getOpcode() == Instruction::URem) {
      unsigned MinBitWidth = 0;
      for (const auto &Op : I->operands()) {
        KnownBits Known = computeKnownBits(Op);
        MinBitWidth =
            std::max(Known.getMaxValue().getActiveBits(), MinBitWidth);
        if (MinBitWidth >= OrigBitWidth)
          return nullptr;
      }
      Itr.second.MinBitWidth = MinBitWidth;
    }
  }

  // Calculate minimum allowed bit-width allowed for shrinking the currently
  // visited truncate's operand.
  unsigned MinBitWidth = getMinBitWidth();

  // Check that we can shrink to smaller bit-width than original one and that
  // it is similar to the DesiredBitWidth is such exists.
  if (MinBitWidth >= OrigBitWidth ||
      (DesiredBitWidth && DesiredBitWidth != MinBitWidth))
    return nullptr;

  return IntegerType::get(CurrentTruncInst->getContext(), MinBitWidth);
}

/// Given a reduced scalar type \p Ty and a \p V value, return a reduced type
/// for \p V, according to its type, if it vector type, return the vector
/// version of \p Ty, otherwise return \p Ty.
static Type *getReducedType(Value *V, Type *Ty) {
  assert(Ty && !Ty->isVectorTy() && "Expect Scalar Type");
  if (auto *VTy = dyn_cast<VectorType>(V->getType()))
    return VectorType::get(Ty, VTy->getElementCount());
  return Ty;
}

Value *TruncInstCombine::getReducedOperand(Value *V, Type *SclTy) {
  Type *Ty = getReducedType(V, SclTy);
  if (auto *C = dyn_cast<Constant>(V)) {
    C = ConstantExpr::getTrunc(C, Ty);
    // If we got a constantexpr back, try to simplify it with DL info.
    return ConstantFoldConstant(C, DL, &TLI);
  }

  auto *I = cast<Instruction>(V);
  Info Entry = InstInfoMap.lookup(I);
  assert(Entry.NewValue);
  return Entry.NewValue;
}

void TruncInstCombine::ReduceExpressionGraph(Type *SclTy) {
  NumInstrsReduced += InstInfoMap.size();
  // Pairs of old and new phi-nodes
  SmallVector<std::pair<PHINode *, PHINode *>, 2> OldNewPHINodes;
  for (auto &Itr : InstInfoMap) { // Forward
    Instruction *I = Itr.first;
    TruncInstCombine::Info &NodeInfo = Itr.second;

    assert(!NodeInfo.NewValue && "Instruction has been evaluated");

    IRBuilder<> Builder(I);
    Value *Res = nullptr;
    unsigned Opc = I->getOpcode();
    switch (Opc) {
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt: {
      Type *Ty = getReducedType(I, SclTy);
      // If the source type of the cast is the type we're trying for then we can
      // just return the source.  There's no need to insert it because it is not
      // new.
      if (I->getOperand(0)->getType() == Ty) {
        assert(!isa<TruncInst>(I) && "Cannot reach here with TruncInst");
        NodeInfo.NewValue = I->getOperand(0);
        continue;
      }
      // Otherwise, must be the same type of cast, so just reinsert a new one.
      // This also handles the case of zext(trunc(x)) -> zext(x).
      Res = Builder.CreateIntCast(I->getOperand(0), Ty,
                                  Opc == Instruction::SExt);

      // Update Worklist entries with new value if needed.
      // There are three possible changes to the Worklist:
      // 1. Update Old-TruncInst -> New-TruncInst.
      // 2. Remove Old-TruncInst (if New node is not TruncInst).
      // 3. Add New-TruncInst (if Old node was not TruncInst).
      auto *Entry = find(Worklist, I);
      if (Entry != Worklist.end()) {
        if (auto *NewCI = dyn_cast<TruncInst>(Res))
          *Entry = NewCI;
        else
          Worklist.erase(Entry);
      } else if (auto *NewCI = dyn_cast<TruncInst>(Res))
          Worklist.push_back(NewCI);
      break;
    }
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::UDiv:
    case Instruction::URem: {
      Value *LHS = getReducedOperand(I->getOperand(0), SclTy);
      Value *RHS = getReducedOperand(I->getOperand(1), SclTy);
      Res = Builder.CreateBinOp((Instruction::BinaryOps)Opc, LHS, RHS);
      // Preserve `exact` flag since truncation doesn't change exactness
      if (auto *PEO = dyn_cast<PossiblyExactOperator>(I))
        if (auto *ResI = dyn_cast<Instruction>(Res))
          ResI->setIsExact(PEO->isExact());
      break;
    }
    case Instruction::ExtractElement: {
      Value *Vec = getReducedOperand(I->getOperand(0), SclTy);
      Value *Idx = I->getOperand(1);
      Res = Builder.CreateExtractElement(Vec, Idx);
      break;
    }
    case Instruction::InsertElement: {
      Value *Vec = getReducedOperand(I->getOperand(0), SclTy);
      Value *NewElt = getReducedOperand(I->getOperand(1), SclTy);
      Value *Idx = I->getOperand(2);
      Res = Builder.CreateInsertElement(Vec, NewElt, Idx);
      break;
    }
    case Instruction::Select: {
      Value *Op0 = I->getOperand(0);
      Value *LHS = getReducedOperand(I->getOperand(1), SclTy);
      Value *RHS = getReducedOperand(I->getOperand(2), SclTy);
      Res = Builder.CreateSelect(Op0, LHS, RHS);
      break;
    }
    case Instruction::PHI: {
      Res = Builder.CreatePHI(getReducedType(I, SclTy), I->getNumOperands());
      OldNewPHINodes.push_back(
          std::make_pair(cast<PHINode>(I), cast<PHINode>(Res)));
      break;
    }
    default:
      llvm_unreachable("Unhandled instruction");
    }

    NodeInfo.NewValue = Res;
    if (auto *ResI = dyn_cast<Instruction>(Res))
      ResI->takeName(I);
  }

  for (auto &Node : OldNewPHINodes) {
    PHINode *OldPN = Node.first;
    PHINode *NewPN = Node.second;
    for (auto Incoming : zip(OldPN->incoming_values(), OldPN->blocks()))
      NewPN->addIncoming(getReducedOperand(std::get<0>(Incoming), SclTy),
                         std::get<1>(Incoming));
  }

  Value *Res = getReducedOperand(CurrentTruncInst->getOperand(0), SclTy);
  Type *DstTy = CurrentTruncInst->getType();
  if (Res->getType() != DstTy) {
    IRBuilder<> Builder(CurrentTruncInst);
    Res = Builder.CreateIntCast(Res, DstTy, false);
    if (auto *ResI = dyn_cast<Instruction>(Res))
      ResI->takeName(CurrentTruncInst);
  }
  CurrentTruncInst->replaceAllUsesWith(Res);

  // Erase old expression graph, which was replaced by the reduced expression
  // graph.
  CurrentTruncInst->eraseFromParent();
  // First, erase old phi-nodes and its uses
  for (auto &Node : OldNewPHINodes) {
    PHINode *OldPN = Node.first;
    OldPN->replaceAllUsesWith(PoisonValue::get(OldPN->getType()));
    InstInfoMap.erase(OldPN);
    OldPN->eraseFromParent();
  }
  // Now we have expression graph turned into dag.
  // We iterate backward, which means we visit the instruction before we
  // visit any of its operands, this way, when we get to the operand, we already
  // removed the instructions (from the expression dag) that uses it.
  for (auto &I : llvm::reverse(InstInfoMap)) {
    // We still need to check that the instruction has no users before we erase
    // it, because {SExt, ZExt}Inst Instruction might have other users that was
    // not reduced, in such case, we need to keep that instruction.
    if (I.first->use_empty())
      I.first->eraseFromParent();
    else
      assert((isa<SExtInst>(I.first) || isa<ZExtInst>(I.first)) &&
             "Only {SExt, ZExt}Inst might have unreduced users");
  }
}

bool TruncInstCombine::run(Function &F) {
  bool MadeIRChange = false;

  // Collect all TruncInst in the function into the Worklist for evaluating.
  for (auto &BB : F) {
    // Ignore unreachable basic block.
    if (!DT.isReachableFromEntry(&BB))
      continue;
    for (auto &I : BB)
      if (auto *CI = dyn_cast<TruncInst>(&I))
        Worklist.push_back(CI);
  }

  // Process all TruncInst in the Worklist, for each instruction:
  //   1. Check if it dominates an eligible expression graph to be reduced.
  //   2. Create a reduced expression graph and replace the old one with it.
  while (!Worklist.empty()) {
    CurrentTruncInst = Worklist.pop_back_val();

    if (Type *NewDstSclTy = getBestTruncatedType()) {
      LLVM_DEBUG(
          dbgs() << "ICE: TruncInstCombine reducing type of expression graph "
                    "dominated by: "
                 << CurrentTruncInst << '\n');
      ReduceExpressionGraph(NewDstSclTy);
      ++NumExprsReduced;
      MadeIRChange = true;
    }
  }

  return MadeIRChange;
}
