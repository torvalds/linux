//===- VPlanSLP.cpp - SLP Analysis based on VPlan -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// This file implements SLP analysis based on VPlan. The analysis is based on
/// the ideas described in
///
///   Look-ahead SLP: auto-vectorization in the presence of commutative
///   operations, CGO 2018 by Vasileios Porpodas, Rodrigo C. O. Rocha,
///   Luís F. W. Góes
///
//===----------------------------------------------------------------------===//

#include "VPlan.h"
#include "VPlanValue.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "vplan-slp"

// Number of levels to look ahead when re-ordering multi node operands.
static unsigned LookaheadMaxDepth = 5;

VPInstruction *VPlanSlp::markFailed() {
  // FIXME: Currently this is used to signal we hit instructions we cannot
  //        trivially SLP'ize.
  CompletelySLP = false;
  return nullptr;
}

void VPlanSlp::addCombined(ArrayRef<VPValue *> Operands, VPInstruction *New) {
  if (all_of(Operands, [](VPValue *V) {
        return cast<VPInstruction>(V)->getUnderlyingInstr();
      })) {
    unsigned BundleSize = 0;
    for (VPValue *V : Operands) {
      Type *T = cast<VPInstruction>(V)->getUnderlyingInstr()->getType();
      assert(!T->isVectorTy() && "Only scalar types supported for now");
      BundleSize += T->getScalarSizeInBits();
    }
    WidestBundleBits = std::max(WidestBundleBits, BundleSize);
  }

  auto Res = BundleToCombined.try_emplace(to_vector<4>(Operands), New);
  assert(Res.second &&
         "Already created a combined instruction for the operand bundle");
  (void)Res;
}

bool VPlanSlp::areVectorizable(ArrayRef<VPValue *> Operands) const {
  // Currently we only support VPInstructions.
  if (!all_of(Operands, [](VPValue *Op) {
        return Op && isa<VPInstruction>(Op) &&
               cast<VPInstruction>(Op)->getUnderlyingInstr();
      })) {
    LLVM_DEBUG(dbgs() << "VPSLP: not all operands are VPInstructions\n");
    return false;
  }

  // Check if opcodes and type width agree for all instructions in the bundle.
  // FIXME: Differing widths/opcodes can be handled by inserting additional
  //        instructions.
  // FIXME: Deal with non-primitive types.
  const Instruction *OriginalInstr =
      cast<VPInstruction>(Operands[0])->getUnderlyingInstr();
  unsigned Opcode = OriginalInstr->getOpcode();
  unsigned Width = OriginalInstr->getType()->getPrimitiveSizeInBits();
  if (!all_of(Operands, [Opcode, Width](VPValue *Op) {
        const Instruction *I = cast<VPInstruction>(Op)->getUnderlyingInstr();
        return I->getOpcode() == Opcode &&
               I->getType()->getPrimitiveSizeInBits() == Width;
      })) {
    LLVM_DEBUG(dbgs() << "VPSLP: Opcodes do not agree \n");
    return false;
  }

  // For now, all operands must be defined in the same BB.
  if (any_of(Operands, [this](VPValue *Op) {
        return cast<VPInstruction>(Op)->getParent() != &this->BB;
      })) {
    LLVM_DEBUG(dbgs() << "VPSLP: operands in different BBs\n");
    return false;
  }

  if (any_of(Operands,
             [](VPValue *Op) { return Op->hasMoreThanOneUniqueUser(); })) {
    LLVM_DEBUG(dbgs() << "VPSLP: Some operands have multiple users.\n");
    return false;
  }

  // For loads, check that there are no instructions writing to memory in
  // between them.
  // TODO: we only have to forbid instructions writing to memory that could
  //       interfere with any of the loads in the bundle
  if (Opcode == Instruction::Load) {
    unsigned LoadsSeen = 0;
    VPBasicBlock *Parent = cast<VPInstruction>(Operands[0])->getParent();
    for (auto &I : *Parent) {
      auto *VPI = dyn_cast<VPInstruction>(&I);
      if (!VPI)
        break;
      if (VPI->getOpcode() == Instruction::Load &&
          llvm::is_contained(Operands, VPI))
        LoadsSeen++;

      if (LoadsSeen == Operands.size())
        break;
      if (LoadsSeen > 0 && VPI->mayWriteToMemory()) {
        LLVM_DEBUG(
            dbgs() << "VPSLP: instruction modifying memory between loads\n");
        return false;
      }
    }

    if (!all_of(Operands, [](VPValue *Op) {
          return cast<LoadInst>(cast<VPInstruction>(Op)->getUnderlyingInstr())
              ->isSimple();
        })) {
      LLVM_DEBUG(dbgs() << "VPSLP: only simple loads are supported.\n");
      return false;
    }
  }

  if (Opcode == Instruction::Store)
    if (!all_of(Operands, [](VPValue *Op) {
          return cast<StoreInst>(cast<VPInstruction>(Op)->getUnderlyingInstr())
              ->isSimple();
        })) {
      LLVM_DEBUG(dbgs() << "VPSLP: only simple stores are supported.\n");
      return false;
    }

  return true;
}

static SmallVector<VPValue *, 4> getOperands(ArrayRef<VPValue *> Values,
                                             unsigned OperandIndex) {
  SmallVector<VPValue *, 4> Operands;
  for (VPValue *V : Values) {
    // Currently we only support VPInstructions.
    auto *U = cast<VPInstruction>(V);
    Operands.push_back(U->getOperand(OperandIndex));
  }
  return Operands;
}

static bool areCommutative(ArrayRef<VPValue *> Values) {
  return Instruction::isCommutative(
      cast<VPInstruction>(Values[0])->getOpcode());
}

static SmallVector<SmallVector<VPValue *, 4>, 4>
getOperands(ArrayRef<VPValue *> Values) {
  SmallVector<SmallVector<VPValue *, 4>, 4> Result;
  auto *VPI = cast<VPInstruction>(Values[0]);

  switch (VPI->getOpcode()) {
  case Instruction::Load:
    llvm_unreachable("Loads terminate a tree, no need to get operands");
  case Instruction::Store:
    Result.push_back(getOperands(Values, 0));
    break;
  default:
    for (unsigned I = 0, NumOps = VPI->getNumOperands(); I < NumOps; ++I)
      Result.push_back(getOperands(Values, I));
    break;
  }

  return Result;
}

/// Returns the opcode of Values or ~0 if they do not all agree.
static std::optional<unsigned> getOpcode(ArrayRef<VPValue *> Values) {
  unsigned Opcode = cast<VPInstruction>(Values[0])->getOpcode();
  if (any_of(Values, [Opcode](VPValue *V) {
        return cast<VPInstruction>(V)->getOpcode() != Opcode;
      }))
    return std::nullopt;
  return {Opcode};
}

/// Returns true if A and B access sequential memory if they are loads or
/// stores or if they have identical opcodes otherwise.
static bool areConsecutiveOrMatch(VPInstruction *A, VPInstruction *B,
                                  VPInterleavedAccessInfo &IAI) {
  if (A->getOpcode() != B->getOpcode())
    return false;

  if (A->getOpcode() != Instruction::Load &&
      A->getOpcode() != Instruction::Store)
    return true;
  auto *GA = IAI.getInterleaveGroup(A);
  auto *GB = IAI.getInterleaveGroup(B);

  return GA && GB && GA == GB && GA->getIndex(A) + 1 == GB->getIndex(B);
}

/// Implements getLAScore from Listing 7 in the paper.
/// Traverses and compares operands of V1 and V2 to MaxLevel.
static unsigned getLAScore(VPValue *V1, VPValue *V2, unsigned MaxLevel,
                           VPInterleavedAccessInfo &IAI) {
  auto *I1 = dyn_cast<VPInstruction>(V1);
  auto *I2 = dyn_cast<VPInstruction>(V2);
  // Currently we only support VPInstructions.
  if (!I1 || !I2)
    return 0;

  if (MaxLevel == 0)
    return (unsigned)areConsecutiveOrMatch(I1, I2, IAI);

  unsigned Score = 0;
  for (unsigned I = 0, EV1 = I1->getNumOperands(); I < EV1; ++I)
    for (unsigned J = 0, EV2 = I2->getNumOperands(); J < EV2; ++J)
      Score +=
          getLAScore(I1->getOperand(I), I2->getOperand(J), MaxLevel - 1, IAI);
  return Score;
}

std::pair<VPlanSlp::OpMode, VPValue *>
VPlanSlp::getBest(OpMode Mode, VPValue *Last,
                  SmallPtrSetImpl<VPValue *> &Candidates,
                  VPInterleavedAccessInfo &IAI) {
  assert((Mode == OpMode::Load || Mode == OpMode::Opcode) &&
         "Currently we only handle load and commutative opcodes");
  LLVM_DEBUG(dbgs() << "      getBest\n");

  SmallVector<VPValue *, 4> BestCandidates;
  LLVM_DEBUG(dbgs() << "        Candidates  for "
                    << *cast<VPInstruction>(Last)->getUnderlyingInstr() << " ");
  for (auto *Candidate : Candidates) {
    auto *LastI = cast<VPInstruction>(Last);
    auto *CandidateI = cast<VPInstruction>(Candidate);
    if (areConsecutiveOrMatch(LastI, CandidateI, IAI)) {
      LLVM_DEBUG(dbgs() << *cast<VPInstruction>(Candidate)->getUnderlyingInstr()
                        << " ");
      BestCandidates.push_back(Candidate);
    }
  }
  LLVM_DEBUG(dbgs() << "\n");

  if (BestCandidates.empty())
    return {OpMode::Failed, nullptr};

  if (BestCandidates.size() == 1)
    return {Mode, BestCandidates[0]};

  VPValue *Best = nullptr;
  unsigned BestScore = 0;
  for (unsigned Depth = 1; Depth < LookaheadMaxDepth; Depth++) {
    unsigned PrevScore = ~0u;
    bool AllSame = true;

    // FIXME: Avoid visiting the same operands multiple times.
    for (auto *Candidate : BestCandidates) {
      unsigned Score = getLAScore(Last, Candidate, Depth, IAI);
      if (PrevScore == ~0u)
        PrevScore = Score;
      if (PrevScore != Score)
        AllSame = false;
      PrevScore = Score;

      if (Score > BestScore) {
        BestScore = Score;
        Best = Candidate;
      }
    }
    if (!AllSame)
      break;
  }
  LLVM_DEBUG(dbgs() << "Found best "
                    << *cast<VPInstruction>(Best)->getUnderlyingInstr()
                    << "\n");
  Candidates.erase(Best);

  return {Mode, Best};
}

SmallVector<VPlanSlp::MultiNodeOpTy, 4> VPlanSlp::reorderMultiNodeOps() {
  SmallVector<MultiNodeOpTy, 4> FinalOrder;
  SmallVector<OpMode, 4> Mode;
  FinalOrder.reserve(MultiNodeOps.size());
  Mode.reserve(MultiNodeOps.size());

  LLVM_DEBUG(dbgs() << "Reordering multinode\n");

  for (auto &Operands : MultiNodeOps) {
    FinalOrder.push_back({Operands.first, {Operands.second[0]}});
    if (cast<VPInstruction>(Operands.second[0])->getOpcode() ==
        Instruction::Load)
      Mode.push_back(OpMode::Load);
    else
      Mode.push_back(OpMode::Opcode);
  }

  for (unsigned Lane = 1, E = MultiNodeOps[0].second.size(); Lane < E; ++Lane) {
    LLVM_DEBUG(dbgs() << "  Finding best value for lane " << Lane << "\n");
    SmallPtrSet<VPValue *, 4> Candidates;
    LLVM_DEBUG(dbgs() << "  Candidates  ");
    for (auto Ops : MultiNodeOps) {
      LLVM_DEBUG(
          dbgs() << *cast<VPInstruction>(Ops.second[Lane])->getUnderlyingInstr()
                 << " ");
      Candidates.insert(Ops.second[Lane]);
    }
    LLVM_DEBUG(dbgs() << "\n");

    for (unsigned Op = 0, E = MultiNodeOps.size(); Op < E; ++Op) {
      LLVM_DEBUG(dbgs() << "  Checking " << Op << "\n");
      if (Mode[Op] == OpMode::Failed)
        continue;

      VPValue *Last = FinalOrder[Op].second[Lane - 1];
      std::pair<OpMode, VPValue *> Res =
          getBest(Mode[Op], Last, Candidates, IAI);
      if (Res.second)
        FinalOrder[Op].second.push_back(Res.second);
      else
        // TODO: handle this case
        FinalOrder[Op].second.push_back(markFailed());
    }
  }

  return FinalOrder;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPlanSlp::dumpBundle(ArrayRef<VPValue *> Values) {
  dbgs() << " Ops: ";
  for (auto *Op : Values) {
    if (auto *VPInstr = cast_or_null<VPInstruction>(Op))
      if (auto *Instr = VPInstr->getUnderlyingInstr()) {
        dbgs() << *Instr << " | ";
        continue;
      }
    dbgs() << " nullptr | ";
  }
  dbgs() << "\n";
}
#endif

VPInstruction *VPlanSlp::buildGraph(ArrayRef<VPValue *> Values) {
  assert(!Values.empty() && "Need some operands!");

  // If we already visited this instruction bundle, re-use the existing node
  auto I = BundleToCombined.find(to_vector<4>(Values));
  if (I != BundleToCombined.end()) {
#ifndef NDEBUG
    // Check that the resulting graph is a tree. If we re-use a node, this means
    // its values have multiple users. We only allow this, if all users of each
    // value are the same instruction.
    for (auto *V : Values) {
      auto UI = V->user_begin();
      auto *FirstUser = *UI++;
      while (UI != V->user_end()) {
        assert(*UI == FirstUser && "Currently we only support SLP trees.");
        UI++;
      }
    }
#endif
    return I->second;
  }

  // Dump inputs
  LLVM_DEBUG({
    dbgs() << "buildGraph: ";
    dumpBundle(Values);
  });

  if (!areVectorizable(Values))
    return markFailed();

  assert(getOpcode(Values) && "Opcodes for all values must match");
  unsigned ValuesOpcode = *getOpcode(Values);

  SmallVector<VPValue *, 4> CombinedOperands;
  if (areCommutative(Values)) {
    bool MultiNodeRoot = !MultiNodeActive;
    MultiNodeActive = true;
    for (auto &Operands : getOperands(Values)) {
      LLVM_DEBUG({
        dbgs() << "  Visiting Commutative";
        dumpBundle(Operands);
      });

      auto OperandsOpcode = getOpcode(Operands);
      if (OperandsOpcode && OperandsOpcode == getOpcode(Values)) {
        LLVM_DEBUG(dbgs() << "    Same opcode, continue building\n");
        CombinedOperands.push_back(buildGraph(Operands));
      } else {
        LLVM_DEBUG(dbgs() << "    Adding multinode Ops\n");
        // Create dummy VPInstruction, which will we replace later by the
        // re-ordered operand.
        VPInstruction *Op = new VPInstruction(0, {});
        CombinedOperands.push_back(Op);
        MultiNodeOps.emplace_back(Op, Operands);
      }
    }

    if (MultiNodeRoot) {
      LLVM_DEBUG(dbgs() << "Reorder \n");
      MultiNodeActive = false;

      auto FinalOrder = reorderMultiNodeOps();

      MultiNodeOps.clear();
      for (auto &Ops : FinalOrder) {
        VPInstruction *NewOp = buildGraph(Ops.second);
        Ops.first->replaceAllUsesWith(NewOp);
        for (unsigned i = 0; i < CombinedOperands.size(); i++)
          if (CombinedOperands[i] == Ops.first)
            CombinedOperands[i] = NewOp;
        delete Ops.first;
        Ops.first = NewOp;
      }
      LLVM_DEBUG(dbgs() << "Found final order\n");
    }
  } else {
    LLVM_DEBUG(dbgs() << "  NonCommuntative\n");
    if (ValuesOpcode == Instruction::Load)
      for (VPValue *V : Values)
        CombinedOperands.push_back(cast<VPInstruction>(V)->getOperand(0));
    else
      for (auto &Operands : getOperands(Values))
        CombinedOperands.push_back(buildGraph(Operands));
  }

  unsigned Opcode;
  switch (ValuesOpcode) {
  case Instruction::Load:
    Opcode = VPInstruction::SLPLoad;
    break;
  case Instruction::Store:
    Opcode = VPInstruction::SLPStore;
    break;
  default:
    Opcode = ValuesOpcode;
    break;
  }

  if (!CompletelySLP)
    return markFailed();

  assert(CombinedOperands.size() > 0 && "Need more some operands");
  auto *Inst = cast<VPInstruction>(Values[0])->getUnderlyingInstr();
  auto *VPI = new VPInstruction(Opcode, CombinedOperands, Inst->getDebugLoc());

  LLVM_DEBUG(dbgs() << "Create VPInstruction " << *VPI << " "
                    << *cast<VPInstruction>(Values[0]) << "\n");
  addCombined(Values, VPI);
  return VPI;
}
