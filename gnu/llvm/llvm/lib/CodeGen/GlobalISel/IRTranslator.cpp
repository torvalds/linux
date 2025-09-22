//===- llvm/CodeGen/GlobalISel/IRTranslator.cpp - IRTranslator ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the IRTranslator class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/SwitchLoweringUtils.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/MemoryOpRemark.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "irtranslator"

using namespace llvm;

static cl::opt<bool>
    EnableCSEInIRTranslator("enable-cse-in-irtranslator",
                            cl::desc("Should enable CSE in irtranslator"),
                            cl::Optional, cl::init(false));
char IRTranslator::ID = 0;

INITIALIZE_PASS_BEGIN(IRTranslator, DEBUG_TYPE, "IRTranslator LLVM IR -> MI",
                false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(StackProtector)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(IRTranslator, DEBUG_TYPE, "IRTranslator LLVM IR -> MI",
                false, false)

static void reportTranslationError(MachineFunction &MF,
                                   const TargetPassConfig &TPC,
                                   OptimizationRemarkEmitter &ORE,
                                   OptimizationRemarkMissed &R) {
  MF.getProperties().set(MachineFunctionProperties::Property::FailedISel);

  // Print the function name explicitly if we don't have a debug location (which
  // makes the diagnostic less useful) or if we're going to emit a raw error.
  if (!R.getLocation().isValid() || TPC.isGlobalISelAbortEnabled())
    R << (" (in function: " + MF.getName() + ")").str();

  if (TPC.isGlobalISelAbortEnabled())
    report_fatal_error(Twine(R.getMsg()));
  else
    ORE.emit(R);
}

IRTranslator::IRTranslator(CodeGenOptLevel optlevel)
    : MachineFunctionPass(ID), OptLevel(optlevel) {}

#ifndef NDEBUG
namespace {
/// Verify that every instruction created has the same DILocation as the
/// instruction being translated.
class DILocationVerifier : public GISelChangeObserver {
  const Instruction *CurrInst = nullptr;

public:
  DILocationVerifier() = default;
  ~DILocationVerifier() = default;

  const Instruction *getCurrentInst() const { return CurrInst; }
  void setCurrentInst(const Instruction *Inst) { CurrInst = Inst; }

  void erasingInstr(MachineInstr &MI) override {}
  void changingInstr(MachineInstr &MI) override {}
  void changedInstr(MachineInstr &MI) override {}

  void createdInstr(MachineInstr &MI) override {
    assert(getCurrentInst() && "Inserted instruction without a current MI");

    // Only print the check message if we're actually checking it.
#ifndef NDEBUG
    LLVM_DEBUG(dbgs() << "Checking DILocation from " << *CurrInst
                      << " was copied to " << MI);
#endif
    // We allow insts in the entry block to have no debug loc because
    // they could have originated from constants, and we don't want a jumpy
    // debug experience.
    assert((CurrInst->getDebugLoc() == MI.getDebugLoc() ||
            (MI.getParent()->isEntryBlock() && !MI.getDebugLoc()) ||
            (MI.isDebugInstr())) &&
           "Line info was not transferred to all instructions");
  }
};
} // namespace
#endif // ifndef NDEBUG


void IRTranslator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<StackProtector>();
  AU.addRequired<TargetPassConfig>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();
  if (OptLevel != CodeGenOptLevel::None) {
    AU.addRequired<BranchProbabilityInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
  }
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addPreserved<TargetLibraryInfoWrapperPass>();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

IRTranslator::ValueToVRegInfo::VRegListT &
IRTranslator::allocateVRegs(const Value &Val) {
  auto VRegsIt = VMap.findVRegs(Val);
  if (VRegsIt != VMap.vregs_end())
    return *VRegsIt->second;
  auto *Regs = VMap.getVRegs(Val);
  auto *Offsets = VMap.getOffsets(Val);
  SmallVector<LLT, 4> SplitTys;
  computeValueLLTs(*DL, *Val.getType(), SplitTys,
                   Offsets->empty() ? Offsets : nullptr);
  for (unsigned i = 0; i < SplitTys.size(); ++i)
    Regs->push_back(0);
  return *Regs;
}

ArrayRef<Register> IRTranslator::getOrCreateVRegs(const Value &Val) {
  auto VRegsIt = VMap.findVRegs(Val);
  if (VRegsIt != VMap.vregs_end())
    return *VRegsIt->second;

  if (Val.getType()->isVoidTy())
    return *VMap.getVRegs(Val);

  // Create entry for this type.
  auto *VRegs = VMap.getVRegs(Val);
  auto *Offsets = VMap.getOffsets(Val);

  if (!Val.getType()->isTokenTy())
    assert(Val.getType()->isSized() &&
           "Don't know how to create an empty vreg");

  SmallVector<LLT, 4> SplitTys;
  computeValueLLTs(*DL, *Val.getType(), SplitTys,
                   Offsets->empty() ? Offsets : nullptr);

  if (!isa<Constant>(Val)) {
    for (auto Ty : SplitTys)
      VRegs->push_back(MRI->createGenericVirtualRegister(Ty));
    return *VRegs;
  }

  if (Val.getType()->isAggregateType()) {
    // UndefValue, ConstantAggregateZero
    auto &C = cast<Constant>(Val);
    unsigned Idx = 0;
    while (auto Elt = C.getAggregateElement(Idx++)) {
      auto EltRegs = getOrCreateVRegs(*Elt);
      llvm::copy(EltRegs, std::back_inserter(*VRegs));
    }
  } else {
    assert(SplitTys.size() == 1 && "unexpectedly split LLT");
    VRegs->push_back(MRI->createGenericVirtualRegister(SplitTys[0]));
    bool Success = translate(cast<Constant>(Val), VRegs->front());
    if (!Success) {
      OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                 MF->getFunction().getSubprogram(),
                                 &MF->getFunction().getEntryBlock());
      R << "unable to translate constant: " << ore::NV("Type", Val.getType());
      reportTranslationError(*MF, *TPC, *ORE, R);
      return *VRegs;
    }
  }

  return *VRegs;
}

int IRTranslator::getOrCreateFrameIndex(const AllocaInst &AI) {
  auto MapEntry = FrameIndices.find(&AI);
  if (MapEntry != FrameIndices.end())
    return MapEntry->second;

  uint64_t ElementSize = DL->getTypeAllocSize(AI.getAllocatedType());
  uint64_t Size =
      ElementSize * cast<ConstantInt>(AI.getArraySize())->getZExtValue();

  // Always allocate at least one byte.
  Size = std::max<uint64_t>(Size, 1u);

  int &FI = FrameIndices[&AI];
  FI = MF->getFrameInfo().CreateStackObject(Size, AI.getAlign(), false, &AI);
  return FI;
}

Align IRTranslator::getMemOpAlign(const Instruction &I) {
  if (const StoreInst *SI = dyn_cast<StoreInst>(&I))
    return SI->getAlign();
  if (const LoadInst *LI = dyn_cast<LoadInst>(&I))
    return LI->getAlign();
  if (const AtomicCmpXchgInst *AI = dyn_cast<AtomicCmpXchgInst>(&I))
    return AI->getAlign();
  if (const AtomicRMWInst *AI = dyn_cast<AtomicRMWInst>(&I))
    return AI->getAlign();

  OptimizationRemarkMissed R("gisel-irtranslator", "", &I);
  R << "unable to translate memop: " << ore::NV("Opcode", &I);
  reportTranslationError(*MF, *TPC, *ORE, R);
  return Align(1);
}

MachineBasicBlock &IRTranslator::getMBB(const BasicBlock &BB) {
  MachineBasicBlock *&MBB = BBToMBB[&BB];
  assert(MBB && "BasicBlock was not encountered before");
  return *MBB;
}

void IRTranslator::addMachineCFGPred(CFGEdge Edge, MachineBasicBlock *NewPred) {
  assert(NewPred && "new predecessor must be a real MachineBasicBlock");
  MachinePreds[Edge].push_back(NewPred);
}

bool IRTranslator::translateBinaryOp(unsigned Opcode, const User &U,
                                     MachineIRBuilder &MIRBuilder) {
  // Get or create a virtual register for each value.
  // Unless the value is a Constant => loadimm cst?
  // or inline constant each time?
  // Creation of a virtual register needs to have a size.
  Register Op0 = getOrCreateVReg(*U.getOperand(0));
  Register Op1 = getOrCreateVReg(*U.getOperand(1));
  Register Res = getOrCreateVReg(U);
  uint32_t Flags = 0;
  if (isa<Instruction>(U)) {
    const Instruction &I = cast<Instruction>(U);
    Flags = MachineInstr::copyFlagsFromInstruction(I);
  }

  MIRBuilder.buildInstr(Opcode, {Res}, {Op0, Op1}, Flags);
  return true;
}

bool IRTranslator::translateUnaryOp(unsigned Opcode, const User &U,
                                    MachineIRBuilder &MIRBuilder) {
  Register Op0 = getOrCreateVReg(*U.getOperand(0));
  Register Res = getOrCreateVReg(U);
  uint32_t Flags = 0;
  if (isa<Instruction>(U)) {
    const Instruction &I = cast<Instruction>(U);
    Flags = MachineInstr::copyFlagsFromInstruction(I);
  }
  MIRBuilder.buildInstr(Opcode, {Res}, {Op0}, Flags);
  return true;
}

bool IRTranslator::translateFNeg(const User &U, MachineIRBuilder &MIRBuilder) {
  return translateUnaryOp(TargetOpcode::G_FNEG, U, MIRBuilder);
}

bool IRTranslator::translateCompare(const User &U,
                                    MachineIRBuilder &MIRBuilder) {
  auto *CI = cast<CmpInst>(&U);
  Register Op0 = getOrCreateVReg(*U.getOperand(0));
  Register Op1 = getOrCreateVReg(*U.getOperand(1));
  Register Res = getOrCreateVReg(U);
  CmpInst::Predicate Pred = CI->getPredicate();
  if (CmpInst::isIntPredicate(Pred))
    MIRBuilder.buildICmp(Pred, Res, Op0, Op1);
  else if (Pred == CmpInst::FCMP_FALSE)
    MIRBuilder.buildCopy(
        Res, getOrCreateVReg(*Constant::getNullValue(U.getType())));
  else if (Pred == CmpInst::FCMP_TRUE)
    MIRBuilder.buildCopy(
        Res, getOrCreateVReg(*Constant::getAllOnesValue(U.getType())));
  else {
    uint32_t Flags = 0;
    if (CI)
      Flags = MachineInstr::copyFlagsFromInstruction(*CI);
    MIRBuilder.buildFCmp(Pred, Res, Op0, Op1, Flags);
  }

  return true;
}

bool IRTranslator::translateRet(const User &U, MachineIRBuilder &MIRBuilder) {
  const ReturnInst &RI = cast<ReturnInst>(U);
  const Value *Ret = RI.getReturnValue();
  if (Ret && DL->getTypeStoreSize(Ret->getType()).isZero())
    Ret = nullptr;

  ArrayRef<Register> VRegs;
  if (Ret)
    VRegs = getOrCreateVRegs(*Ret);

  Register SwiftErrorVReg = 0;
  if (CLI->supportSwiftError() && SwiftError.getFunctionArg()) {
    SwiftErrorVReg = SwiftError.getOrCreateVRegUseAt(
        &RI, &MIRBuilder.getMBB(), SwiftError.getFunctionArg());
  }

  // The target may mess up with the insertion point, but
  // this is not important as a return is the last instruction
  // of the block anyway.
  return CLI->lowerReturn(MIRBuilder, Ret, VRegs, FuncInfo, SwiftErrorVReg);
}

void IRTranslator::emitBranchForMergedCondition(
    const Value *Cond, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    MachineBasicBlock *CurBB, MachineBasicBlock *SwitchBB,
    BranchProbability TProb, BranchProbability FProb, bool InvertCond) {
  // If the leaf of the tree is a comparison, merge the condition into
  // the caseblock.
  if (const CmpInst *BOp = dyn_cast<CmpInst>(Cond)) {
    CmpInst::Predicate Condition;
    if (const ICmpInst *IC = dyn_cast<ICmpInst>(Cond)) {
      Condition = InvertCond ? IC->getInversePredicate() : IC->getPredicate();
    } else {
      const FCmpInst *FC = cast<FCmpInst>(Cond);
      Condition = InvertCond ? FC->getInversePredicate() : FC->getPredicate();
    }

    SwitchCG::CaseBlock CB(Condition, false, BOp->getOperand(0),
                           BOp->getOperand(1), nullptr, TBB, FBB, CurBB,
                           CurBuilder->getDebugLoc(), TProb, FProb);
    SL->SwitchCases.push_back(CB);
    return;
  }

  // Create a CaseBlock record representing this branch.
  CmpInst::Predicate Pred = InvertCond ? CmpInst::ICMP_NE : CmpInst::ICMP_EQ;
  SwitchCG::CaseBlock CB(
      Pred, false, Cond, ConstantInt::getTrue(MF->getFunction().getContext()),
      nullptr, TBB, FBB, CurBB, CurBuilder->getDebugLoc(), TProb, FProb);
  SL->SwitchCases.push_back(CB);
}

static bool isValInBlock(const Value *V, const BasicBlock *BB) {
  if (const Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() == BB;
  return true;
}

void IRTranslator::findMergedConditions(
    const Value *Cond, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    MachineBasicBlock *CurBB, MachineBasicBlock *SwitchBB,
    Instruction::BinaryOps Opc, BranchProbability TProb,
    BranchProbability FProb, bool InvertCond) {
  using namespace PatternMatch;
  assert((Opc == Instruction::And || Opc == Instruction::Or) &&
         "Expected Opc to be AND/OR");
  // Skip over not part of the tree and remember to invert op and operands at
  // next level.
  Value *NotCond;
  if (match(Cond, m_OneUse(m_Not(m_Value(NotCond)))) &&
      isValInBlock(NotCond, CurBB->getBasicBlock())) {
    findMergedConditions(NotCond, TBB, FBB, CurBB, SwitchBB, Opc, TProb, FProb,
                         !InvertCond);
    return;
  }

  const Instruction *BOp = dyn_cast<Instruction>(Cond);
  const Value *BOpOp0, *BOpOp1;
  // Compute the effective opcode for Cond, taking into account whether it needs
  // to be inverted, e.g.
  //   and (not (or A, B)), C
  // gets lowered as
  //   and (and (not A, not B), C)
  Instruction::BinaryOps BOpc = (Instruction::BinaryOps)0;
  if (BOp) {
    BOpc = match(BOp, m_LogicalAnd(m_Value(BOpOp0), m_Value(BOpOp1)))
               ? Instruction::And
               : (match(BOp, m_LogicalOr(m_Value(BOpOp0), m_Value(BOpOp1)))
                      ? Instruction::Or
                      : (Instruction::BinaryOps)0);
    if (InvertCond) {
      if (BOpc == Instruction::And)
        BOpc = Instruction::Or;
      else if (BOpc == Instruction::Or)
        BOpc = Instruction::And;
    }
  }

  // If this node is not part of the or/and tree, emit it as a branch.
  // Note that all nodes in the tree should have same opcode.
  bool BOpIsInOrAndTree = BOpc && BOpc == Opc && BOp->hasOneUse();
  if (!BOpIsInOrAndTree || BOp->getParent() != CurBB->getBasicBlock() ||
      !isValInBlock(BOpOp0, CurBB->getBasicBlock()) ||
      !isValInBlock(BOpOp1, CurBB->getBasicBlock())) {
    emitBranchForMergedCondition(Cond, TBB, FBB, CurBB, SwitchBB, TProb, FProb,
                                 InvertCond);
    return;
  }

  //  Create TmpBB after CurBB.
  MachineFunction::iterator BBI(CurBB);
  MachineBasicBlock *TmpBB =
      MF->CreateMachineBasicBlock(CurBB->getBasicBlock());
  CurBB->getParent()->insert(++BBI, TmpBB);

  if (Opc == Instruction::Or) {
    // Codegen X | Y as:
    // BB1:
    //   jmp_if_X TBB
    //   jmp TmpBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   TrueProb for BB1 + (FalseProb for BB1 * TrueProb for TmpBB)
    //     = TrueProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A/2 and A/2+B, and set TmpBB's probabilities to
    // A/(1+B) and 2B/(1+B). This choice assumes that
    //   TrueProb for BB1 == FalseProb for BB1 * TrueProb for TmpBB.
    // Another choice is to assume TrueProb for BB1 equals to TrueProb for
    // TmpBB, but the math is more complicated.

    auto NewTrueProb = TProb / 2;
    auto NewFalseProb = TProb / 2 + FProb;
    // Emit the LHS condition.
    findMergedConditions(BOpOp0, TBB, TmpBB, CurBB, SwitchBB, Opc, NewTrueProb,
                         NewFalseProb, InvertCond);

    // Normalize A/2 and B to get A/(1+B) and 2B/(1+B).
    SmallVector<BranchProbability, 2> Probs{TProb / 2, FProb};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    findMergedConditions(BOpOp1, TBB, FBB, TmpBB, SwitchBB, Opc, Probs[0],
                         Probs[1], InvertCond);
  } else {
    assert(Opc == Instruction::And && "Unknown merge op!");
    // Codegen X & Y as:
    // BB1:
    //   jmp_if_X TmpBB
    //   jmp FBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //
    //  This requires creation of TmpBB after CurBB.

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   FalseProb for BB1 + (TrueProb for BB1 * FalseProb for TmpBB)
    //     = FalseProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A+B/2 and B/2, and set TmpBB's probabilities to
    // 2A/(1+A) and B/(1+A). This choice assumes that FalseProb for BB1 ==
    // TrueProb for BB1 * FalseProb for TmpBB.

    auto NewTrueProb = TProb + FProb / 2;
    auto NewFalseProb = FProb / 2;
    // Emit the LHS condition.
    findMergedConditions(BOpOp0, TmpBB, FBB, CurBB, SwitchBB, Opc, NewTrueProb,
                         NewFalseProb, InvertCond);

    // Normalize A and B/2 to get 2A/(1+A) and B/(1+A).
    SmallVector<BranchProbability, 2> Probs{TProb, FProb / 2};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    findMergedConditions(BOpOp1, TBB, FBB, TmpBB, SwitchBB, Opc, Probs[0],
                         Probs[1], InvertCond);
  }
}

bool IRTranslator::shouldEmitAsBranches(
    const std::vector<SwitchCG::CaseBlock> &Cases) {
  // For multiple cases, it's better to emit as branches.
  if (Cases.size() != 2)
    return true;

  // If this is two comparisons of the same values or'd or and'd together, they
  // will get folded into a single comparison, so don't emit two blocks.
  if ((Cases[0].CmpLHS == Cases[1].CmpLHS &&
       Cases[0].CmpRHS == Cases[1].CmpRHS) ||
      (Cases[0].CmpRHS == Cases[1].CmpLHS &&
       Cases[0].CmpLHS == Cases[1].CmpRHS)) {
    return false;
  }

  // Handle: (X != null) | (Y != null) --> (X|Y) != 0
  // Handle: (X == null) & (Y == null) --> (X|Y) == 0
  if (Cases[0].CmpRHS == Cases[1].CmpRHS &&
      Cases[0].PredInfo.Pred == Cases[1].PredInfo.Pred &&
      isa<Constant>(Cases[0].CmpRHS) &&
      cast<Constant>(Cases[0].CmpRHS)->isNullValue()) {
    if (Cases[0].PredInfo.Pred == CmpInst::ICMP_EQ &&
        Cases[0].TrueBB == Cases[1].ThisBB)
      return false;
    if (Cases[0].PredInfo.Pred == CmpInst::ICMP_NE &&
        Cases[0].FalseBB == Cases[1].ThisBB)
      return false;
  }

  return true;
}

bool IRTranslator::translateBr(const User &U, MachineIRBuilder &MIRBuilder) {
  const BranchInst &BrInst = cast<BranchInst>(U);
  auto &CurMBB = MIRBuilder.getMBB();
  auto *Succ0MBB = &getMBB(*BrInst.getSuccessor(0));

  if (BrInst.isUnconditional()) {
    // If the unconditional target is the layout successor, fallthrough.
    if (OptLevel == CodeGenOptLevel::None ||
        !CurMBB.isLayoutSuccessor(Succ0MBB))
      MIRBuilder.buildBr(*Succ0MBB);

    // Link successors.
    for (const BasicBlock *Succ : successors(&BrInst))
      CurMBB.addSuccessor(&getMBB(*Succ));
    return true;
  }

  // If this condition is one of the special cases we handle, do special stuff
  // now.
  const Value *CondVal = BrInst.getCondition();
  MachineBasicBlock *Succ1MBB = &getMBB(*BrInst.getSuccessor(1));

  // If this is a series of conditions that are or'd or and'd together, emit
  // this as a sequence of branches instead of setcc's with and/or operations.
  // As long as jumps are not expensive (exceptions for multi-use logic ops,
  // unpredictable branches, and vector extracts because those jumps are likely
  // expensive for any target), this should improve performance.
  // For example, instead of something like:
  //     cmp A, B
  //     C = seteq
  //     cmp D, E
  //     F = setle
  //     or C, F
  //     jnz foo
  // Emit:
  //     cmp A, B
  //     je foo
  //     cmp D, E
  //     jle foo
  using namespace PatternMatch;
  const Instruction *CondI = dyn_cast<Instruction>(CondVal);
  if (!TLI->isJumpExpensive() && CondI && CondI->hasOneUse() &&
      !BrInst.hasMetadata(LLVMContext::MD_unpredictable)) {
    Instruction::BinaryOps Opcode = (Instruction::BinaryOps)0;
    Value *Vec;
    const Value *BOp0, *BOp1;
    if (match(CondI, m_LogicalAnd(m_Value(BOp0), m_Value(BOp1))))
      Opcode = Instruction::And;
    else if (match(CondI, m_LogicalOr(m_Value(BOp0), m_Value(BOp1))))
      Opcode = Instruction::Or;

    if (Opcode && !(match(BOp0, m_ExtractElt(m_Value(Vec), m_Value())) &&
                    match(BOp1, m_ExtractElt(m_Specific(Vec), m_Value())))) {
      findMergedConditions(CondI, Succ0MBB, Succ1MBB, &CurMBB, &CurMBB, Opcode,
                           getEdgeProbability(&CurMBB, Succ0MBB),
                           getEdgeProbability(&CurMBB, Succ1MBB),
                           /*InvertCond=*/false);
      assert(SL->SwitchCases[0].ThisBB == &CurMBB && "Unexpected lowering!");

      // Allow some cases to be rejected.
      if (shouldEmitAsBranches(SL->SwitchCases)) {
        // Emit the branch for this block.
        emitSwitchCase(SL->SwitchCases[0], &CurMBB, *CurBuilder);
        SL->SwitchCases.erase(SL->SwitchCases.begin());
        return true;
      }

      // Okay, we decided not to do this, remove any inserted MBB's and clear
      // SwitchCases.
      for (unsigned I = 1, E = SL->SwitchCases.size(); I != E; ++I)
        MF->erase(SL->SwitchCases[I].ThisBB);

      SL->SwitchCases.clear();
    }
  }

  // Create a CaseBlock record representing this branch.
  SwitchCG::CaseBlock CB(CmpInst::ICMP_EQ, false, CondVal,
                         ConstantInt::getTrue(MF->getFunction().getContext()),
                         nullptr, Succ0MBB, Succ1MBB, &CurMBB,
                         CurBuilder->getDebugLoc());

  // Use emitSwitchCase to actually insert the fast branch sequence for this
  // cond branch.
  emitSwitchCase(CB, &CurMBB, *CurBuilder);
  return true;
}

void IRTranslator::addSuccessorWithProb(MachineBasicBlock *Src,
                                        MachineBasicBlock *Dst,
                                        BranchProbability Prob) {
  if (!FuncInfo.BPI) {
    Src->addSuccessorWithoutProb(Dst);
    return;
  }
  if (Prob.isUnknown())
    Prob = getEdgeProbability(Src, Dst);
  Src->addSuccessor(Dst, Prob);
}

BranchProbability
IRTranslator::getEdgeProbability(const MachineBasicBlock *Src,
                                 const MachineBasicBlock *Dst) const {
  const BasicBlock *SrcBB = Src->getBasicBlock();
  const BasicBlock *DstBB = Dst->getBasicBlock();
  if (!FuncInfo.BPI) {
    // If BPI is not available, set the default probability as 1 / N, where N is
    // the number of successors.
    auto SuccSize = std::max<uint32_t>(succ_size(SrcBB), 1);
    return BranchProbability(1, SuccSize);
  }
  return FuncInfo.BPI->getEdgeProbability(SrcBB, DstBB);
}

bool IRTranslator::translateSwitch(const User &U, MachineIRBuilder &MIB) {
  using namespace SwitchCG;
  // Extract cases from the switch.
  const SwitchInst &SI = cast<SwitchInst>(U);
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  CaseClusterVector Clusters;
  Clusters.reserve(SI.getNumCases());
  for (const auto &I : SI.cases()) {
    MachineBasicBlock *Succ = &getMBB(*I.getCaseSuccessor());
    assert(Succ && "Could not find successor mbb in mapping");
    const ConstantInt *CaseVal = I.getCaseValue();
    BranchProbability Prob =
        BPI ? BPI->getEdgeProbability(SI.getParent(), I.getSuccessorIndex())
            : BranchProbability(1, SI.getNumCases() + 1);
    Clusters.push_back(CaseCluster::range(CaseVal, CaseVal, Succ, Prob));
  }

  MachineBasicBlock *DefaultMBB = &getMBB(*SI.getDefaultDest());

  // Cluster adjacent cases with the same destination. We do this at all
  // optimization levels because it's cheap to do and will make codegen faster
  // if there are many clusters.
  sortAndRangeify(Clusters);

  MachineBasicBlock *SwitchMBB = &getMBB(*SI.getParent());

  // If there is only the default destination, jump there directly.
  if (Clusters.empty()) {
    SwitchMBB->addSuccessor(DefaultMBB);
    if (DefaultMBB != SwitchMBB->getNextNode())
      MIB.buildBr(*DefaultMBB);
    return true;
  }

  SL->findJumpTables(Clusters, &SI, std::nullopt, DefaultMBB, nullptr, nullptr);
  SL->findBitTestClusters(Clusters, &SI);

  LLVM_DEBUG({
    dbgs() << "Case clusters: ";
    for (const CaseCluster &C : Clusters) {
      if (C.Kind == CC_JumpTable)
        dbgs() << "JT:";
      if (C.Kind == CC_BitTests)
        dbgs() << "BT:";

      C.Low->getValue().print(dbgs(), true);
      if (C.Low != C.High) {
        dbgs() << '-';
        C.High->getValue().print(dbgs(), true);
      }
      dbgs() << ' ';
    }
    dbgs() << '\n';
  });

  assert(!Clusters.empty());
  SwitchWorkList WorkList;
  CaseClusterIt First = Clusters.begin();
  CaseClusterIt Last = Clusters.end() - 1;
  auto DefaultProb = getEdgeProbability(SwitchMBB, DefaultMBB);
  WorkList.push_back({SwitchMBB, First, Last, nullptr, nullptr, DefaultProb});

  while (!WorkList.empty()) {
    SwitchWorkListItem W = WorkList.pop_back_val();

    unsigned NumClusters = W.LastCluster - W.FirstCluster + 1;
    // For optimized builds, lower large range as a balanced binary tree.
    if (NumClusters > 3 &&
        MF->getTarget().getOptLevel() != CodeGenOptLevel::None &&
        !DefaultMBB->getParent()->getFunction().hasMinSize()) {
      splitWorkItem(WorkList, W, SI.getCondition(), SwitchMBB, MIB);
      continue;
    }

    if (!lowerSwitchWorkItem(W, SI.getCondition(), SwitchMBB, DefaultMBB, MIB))
      return false;
  }
  return true;
}

void IRTranslator::splitWorkItem(SwitchCG::SwitchWorkList &WorkList,
                                 const SwitchCG::SwitchWorkListItem &W,
                                 Value *Cond, MachineBasicBlock *SwitchMBB,
                                 MachineIRBuilder &MIB) {
  using namespace SwitchCG;
  assert(W.FirstCluster->Low->getValue().slt(W.LastCluster->Low->getValue()) &&
         "Clusters not sorted?");
  assert(W.LastCluster - W.FirstCluster + 1 >= 2 && "Too small to split!");

  auto [LastLeft, FirstRight, LeftProb, RightProb] =
      SL->computeSplitWorkItemInfo(W);

  // Use the first element on the right as pivot since we will make less-than
  // comparisons against it.
  CaseClusterIt PivotCluster = FirstRight;
  assert(PivotCluster > W.FirstCluster);
  assert(PivotCluster <= W.LastCluster);

  CaseClusterIt FirstLeft = W.FirstCluster;
  CaseClusterIt LastRight = W.LastCluster;

  const ConstantInt *Pivot = PivotCluster->Low;

  // New blocks will be inserted immediately after the current one.
  MachineFunction::iterator BBI(W.MBB);
  ++BBI;

  // We will branch to the LHS if Value < Pivot. If LHS is a single cluster,
  // we can branch to its destination directly if it's squeezed exactly in
  // between the known lower bound and Pivot - 1.
  MachineBasicBlock *LeftMBB;
  if (FirstLeft == LastLeft && FirstLeft->Kind == CC_Range &&
      FirstLeft->Low == W.GE &&
      (FirstLeft->High->getValue() + 1LL) == Pivot->getValue()) {
    LeftMBB = FirstLeft->MBB;
  } else {
    LeftMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, LeftMBB);
    WorkList.push_back(
        {LeftMBB, FirstLeft, LastLeft, W.GE, Pivot, W.DefaultProb / 2});
  }

  // Similarly, we will branch to the RHS if Value >= Pivot. If RHS is a
  // single cluster, RHS.Low == Pivot, and we can branch to its destination
  // directly if RHS.High equals the current upper bound.
  MachineBasicBlock *RightMBB;
  if (FirstRight == LastRight && FirstRight->Kind == CC_Range && W.LT &&
      (FirstRight->High->getValue() + 1ULL) == W.LT->getValue()) {
    RightMBB = FirstRight->MBB;
  } else {
    RightMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, RightMBB);
    WorkList.push_back(
        {RightMBB, FirstRight, LastRight, Pivot, W.LT, W.DefaultProb / 2});
  }

  // Create the CaseBlock record that will be used to lower the branch.
  CaseBlock CB(ICmpInst::Predicate::ICMP_SLT, false, Cond, Pivot, nullptr,
               LeftMBB, RightMBB, W.MBB, MIB.getDebugLoc(), LeftProb,
               RightProb);

  if (W.MBB == SwitchMBB)
    emitSwitchCase(CB, SwitchMBB, MIB);
  else
    SL->SwitchCases.push_back(CB);
}

void IRTranslator::emitJumpTable(SwitchCG::JumpTable &JT,
                                 MachineBasicBlock *MBB) {
  // Emit the code for the jump table
  assert(JT.Reg != -1U && "Should lower JT Header first!");
  MachineIRBuilder MIB(*MBB->getParent());
  MIB.setMBB(*MBB);
  MIB.setDebugLoc(CurBuilder->getDebugLoc());

  Type *PtrIRTy = PointerType::getUnqual(MF->getFunction().getContext());
  const LLT PtrTy = getLLTForType(*PtrIRTy, *DL);

  auto Table = MIB.buildJumpTable(PtrTy, JT.JTI);
  MIB.buildBrJT(Table.getReg(0), JT.JTI, JT.Reg);
}

bool IRTranslator::emitJumpTableHeader(SwitchCG::JumpTable &JT,
                                       SwitchCG::JumpTableHeader &JTH,
                                       MachineBasicBlock *HeaderBB) {
  MachineIRBuilder MIB(*HeaderBB->getParent());
  MIB.setMBB(*HeaderBB);
  MIB.setDebugLoc(CurBuilder->getDebugLoc());

  const Value &SValue = *JTH.SValue;
  // Subtract the lowest switch case value from the value being switched on.
  const LLT SwitchTy = getLLTForType(*SValue.getType(), *DL);
  Register SwitchOpReg = getOrCreateVReg(SValue);
  auto FirstCst = MIB.buildConstant(SwitchTy, JTH.First);
  auto Sub = MIB.buildSub({SwitchTy}, SwitchOpReg, FirstCst);

  // This value may be smaller or larger than the target's pointer type, and
  // therefore require extension or truncating.
  auto *PtrIRTy = PointerType::getUnqual(SValue.getContext());
  const LLT PtrScalarTy = LLT::scalar(DL->getTypeSizeInBits(PtrIRTy));
  Sub = MIB.buildZExtOrTrunc(PtrScalarTy, Sub);

  JT.Reg = Sub.getReg(0);

  if (JTH.FallthroughUnreachable) {
    if (JT.MBB != HeaderBB->getNextNode())
      MIB.buildBr(*JT.MBB);
    return true;
  }

  // Emit the range check for the jump table, and branch to the default block
  // for the switch statement if the value being switched on exceeds the
  // largest case in the switch.
  auto Cst = getOrCreateVReg(
      *ConstantInt::get(SValue.getType(), JTH.Last - JTH.First));
  Cst = MIB.buildZExtOrTrunc(PtrScalarTy, Cst).getReg(0);
  auto Cmp = MIB.buildICmp(CmpInst::ICMP_UGT, LLT::scalar(1), Sub, Cst);

  auto BrCond = MIB.buildBrCond(Cmp.getReg(0), *JT.Default);

  // Avoid emitting unnecessary branches to the next block.
  if (JT.MBB != HeaderBB->getNextNode())
    BrCond = MIB.buildBr(*JT.MBB);
  return true;
}

void IRTranslator::emitSwitchCase(SwitchCG::CaseBlock &CB,
                                  MachineBasicBlock *SwitchBB,
                                  MachineIRBuilder &MIB) {
  Register CondLHS = getOrCreateVReg(*CB.CmpLHS);
  Register Cond;
  DebugLoc OldDbgLoc = MIB.getDebugLoc();
  MIB.setDebugLoc(CB.DbgLoc);
  MIB.setMBB(*CB.ThisBB);

  if (CB.PredInfo.NoCmp) {
    // Branch or fall through to TrueBB.
    addSuccessorWithProb(CB.ThisBB, CB.TrueBB, CB.TrueProb);
    addMachineCFGPred({SwitchBB->getBasicBlock(), CB.TrueBB->getBasicBlock()},
                      CB.ThisBB);
    CB.ThisBB->normalizeSuccProbs();
    if (CB.TrueBB != CB.ThisBB->getNextNode())
      MIB.buildBr(*CB.TrueBB);
    MIB.setDebugLoc(OldDbgLoc);
    return;
  }

  const LLT i1Ty = LLT::scalar(1);
  // Build the compare.
  if (!CB.CmpMHS) {
    const auto *CI = dyn_cast<ConstantInt>(CB.CmpRHS);
    // For conditional branch lowering, we might try to do something silly like
    // emit an G_ICMP to compare an existing G_ICMP i1 result with true. If so,
    // just re-use the existing condition vreg.
    if (MRI->getType(CondLHS).getSizeInBits() == 1 && CI && CI->isOne() &&
        CB.PredInfo.Pred == CmpInst::ICMP_EQ) {
      Cond = CondLHS;
    } else {
      Register CondRHS = getOrCreateVReg(*CB.CmpRHS);
      if (CmpInst::isFPPredicate(CB.PredInfo.Pred))
        Cond =
            MIB.buildFCmp(CB.PredInfo.Pred, i1Ty, CondLHS, CondRHS).getReg(0);
      else
        Cond =
            MIB.buildICmp(CB.PredInfo.Pred, i1Ty, CondLHS, CondRHS).getReg(0);
    }
  } else {
    assert(CB.PredInfo.Pred == CmpInst::ICMP_SLE &&
           "Can only handle SLE ranges");

    const APInt& Low = cast<ConstantInt>(CB.CmpLHS)->getValue();
    const APInt& High = cast<ConstantInt>(CB.CmpRHS)->getValue();

    Register CmpOpReg = getOrCreateVReg(*CB.CmpMHS);
    if (cast<ConstantInt>(CB.CmpLHS)->isMinValue(true)) {
      Register CondRHS = getOrCreateVReg(*CB.CmpRHS);
      Cond =
          MIB.buildICmp(CmpInst::ICMP_SLE, i1Ty, CmpOpReg, CondRHS).getReg(0);
    } else {
      const LLT CmpTy = MRI->getType(CmpOpReg);
      auto Sub = MIB.buildSub({CmpTy}, CmpOpReg, CondLHS);
      auto Diff = MIB.buildConstant(CmpTy, High - Low);
      Cond = MIB.buildICmp(CmpInst::ICMP_ULE, i1Ty, Sub, Diff).getReg(0);
    }
  }

  // Update successor info
  addSuccessorWithProb(CB.ThisBB, CB.TrueBB, CB.TrueProb);

  addMachineCFGPred({SwitchBB->getBasicBlock(), CB.TrueBB->getBasicBlock()},
                    CB.ThisBB);

  // TrueBB and FalseBB are always different unless the incoming IR is
  // degenerate. This only happens when running llc on weird IR.
  if (CB.TrueBB != CB.FalseBB)
    addSuccessorWithProb(CB.ThisBB, CB.FalseBB, CB.FalseProb);
  CB.ThisBB->normalizeSuccProbs();

  addMachineCFGPred({SwitchBB->getBasicBlock(), CB.FalseBB->getBasicBlock()},
                    CB.ThisBB);

  MIB.buildBrCond(Cond, *CB.TrueBB);
  MIB.buildBr(*CB.FalseBB);
  MIB.setDebugLoc(OldDbgLoc);
}

bool IRTranslator::lowerJumpTableWorkItem(SwitchCG::SwitchWorkListItem W,
                                          MachineBasicBlock *SwitchMBB,
                                          MachineBasicBlock *CurMBB,
                                          MachineBasicBlock *DefaultMBB,
                                          MachineIRBuilder &MIB,
                                          MachineFunction::iterator BBI,
                                          BranchProbability UnhandledProbs,
                                          SwitchCG::CaseClusterIt I,
                                          MachineBasicBlock *Fallthrough,
                                          bool FallthroughUnreachable) {
  using namespace SwitchCG;
  MachineFunction *CurMF = SwitchMBB->getParent();
  // FIXME: Optimize away range check based on pivot comparisons.
  JumpTableHeader *JTH = &SL->JTCases[I->JTCasesIndex].first;
  SwitchCG::JumpTable *JT = &SL->JTCases[I->JTCasesIndex].second;
  BranchProbability DefaultProb = W.DefaultProb;

  // The jump block hasn't been inserted yet; insert it here.
  MachineBasicBlock *JumpMBB = JT->MBB;
  CurMF->insert(BBI, JumpMBB);

  // Since the jump table block is separate from the switch block, we need
  // to keep track of it as a machine predecessor to the default block,
  // otherwise we lose the phi edges.
  addMachineCFGPred({SwitchMBB->getBasicBlock(), DefaultMBB->getBasicBlock()},
                    CurMBB);
  addMachineCFGPred({SwitchMBB->getBasicBlock(), DefaultMBB->getBasicBlock()},
                    JumpMBB);

  auto JumpProb = I->Prob;
  auto FallthroughProb = UnhandledProbs;

  // If the default statement is a target of the jump table, we evenly
  // distribute the default probability to successors of CurMBB. Also
  // update the probability on the edge from JumpMBB to Fallthrough.
  for (MachineBasicBlock::succ_iterator SI = JumpMBB->succ_begin(),
                                        SE = JumpMBB->succ_end();
       SI != SE; ++SI) {
    if (*SI == DefaultMBB) {
      JumpProb += DefaultProb / 2;
      FallthroughProb -= DefaultProb / 2;
      JumpMBB->setSuccProbability(SI, DefaultProb / 2);
      JumpMBB->normalizeSuccProbs();
    } else {
      // Also record edges from the jump table block to it's successors.
      addMachineCFGPred({SwitchMBB->getBasicBlock(), (*SI)->getBasicBlock()},
                        JumpMBB);
    }
  }

  if (FallthroughUnreachable)
    JTH->FallthroughUnreachable = true;

  if (!JTH->FallthroughUnreachable)
    addSuccessorWithProb(CurMBB, Fallthrough, FallthroughProb);
  addSuccessorWithProb(CurMBB, JumpMBB, JumpProb);
  CurMBB->normalizeSuccProbs();

  // The jump table header will be inserted in our current block, do the
  // range check, and fall through to our fallthrough block.
  JTH->HeaderBB = CurMBB;
  JT->Default = Fallthrough; // FIXME: Move Default to JumpTableHeader.

  // If we're in the right place, emit the jump table header right now.
  if (CurMBB == SwitchMBB) {
    if (!emitJumpTableHeader(*JT, *JTH, CurMBB))
      return false;
    JTH->Emitted = true;
  }
  return true;
}
bool IRTranslator::lowerSwitchRangeWorkItem(SwitchCG::CaseClusterIt I,
                                            Value *Cond,
                                            MachineBasicBlock *Fallthrough,
                                            bool FallthroughUnreachable,
                                            BranchProbability UnhandledProbs,
                                            MachineBasicBlock *CurMBB,
                                            MachineIRBuilder &MIB,
                                            MachineBasicBlock *SwitchMBB) {
  using namespace SwitchCG;
  const Value *RHS, *LHS, *MHS;
  CmpInst::Predicate Pred;
  if (I->Low == I->High) {
    // Check Cond == I->Low.
    Pred = CmpInst::ICMP_EQ;
    LHS = Cond;
    RHS = I->Low;
    MHS = nullptr;
  } else {
    // Check I->Low <= Cond <= I->High.
    Pred = CmpInst::ICMP_SLE;
    LHS = I->Low;
    MHS = Cond;
    RHS = I->High;
  }

  // If Fallthrough is unreachable, fold away the comparison.
  // The false probability is the sum of all unhandled cases.
  CaseBlock CB(Pred, FallthroughUnreachable, LHS, RHS, MHS, I->MBB, Fallthrough,
               CurMBB, MIB.getDebugLoc(), I->Prob, UnhandledProbs);

  emitSwitchCase(CB, SwitchMBB, MIB);
  return true;
}

void IRTranslator::emitBitTestHeader(SwitchCG::BitTestBlock &B,
                                     MachineBasicBlock *SwitchBB) {
  MachineIRBuilder &MIB = *CurBuilder;
  MIB.setMBB(*SwitchBB);

  // Subtract the minimum value.
  Register SwitchOpReg = getOrCreateVReg(*B.SValue);

  LLT SwitchOpTy = MRI->getType(SwitchOpReg);
  Register MinValReg = MIB.buildConstant(SwitchOpTy, B.First).getReg(0);
  auto RangeSub = MIB.buildSub(SwitchOpTy, SwitchOpReg, MinValReg);

  Type *PtrIRTy = PointerType::getUnqual(MF->getFunction().getContext());
  const LLT PtrTy = getLLTForType(*PtrIRTy, *DL);

  LLT MaskTy = SwitchOpTy;
  if (MaskTy.getSizeInBits() > PtrTy.getSizeInBits() ||
      !llvm::has_single_bit<uint32_t>(MaskTy.getSizeInBits()))
    MaskTy = LLT::scalar(PtrTy.getSizeInBits());
  else {
    // Ensure that the type will fit the mask value.
    for (unsigned I = 0, E = B.Cases.size(); I != E; ++I) {
      if (!isUIntN(SwitchOpTy.getSizeInBits(), B.Cases[I].Mask)) {
        // Switch table case range are encoded into series of masks.
        // Just use pointer type, it's guaranteed to fit.
        MaskTy = LLT::scalar(PtrTy.getSizeInBits());
        break;
      }
    }
  }
  Register SubReg = RangeSub.getReg(0);
  if (SwitchOpTy != MaskTy)
    SubReg = MIB.buildZExtOrTrunc(MaskTy, SubReg).getReg(0);

  B.RegVT = getMVTForLLT(MaskTy);
  B.Reg = SubReg;

  MachineBasicBlock *MBB = B.Cases[0].ThisBB;

  if (!B.FallthroughUnreachable)
    addSuccessorWithProb(SwitchBB, B.Default, B.DefaultProb);
  addSuccessorWithProb(SwitchBB, MBB, B.Prob);

  SwitchBB->normalizeSuccProbs();

  if (!B.FallthroughUnreachable) {
    // Conditional branch to the default block.
    auto RangeCst = MIB.buildConstant(SwitchOpTy, B.Range);
    auto RangeCmp = MIB.buildICmp(CmpInst::Predicate::ICMP_UGT, LLT::scalar(1),
                                  RangeSub, RangeCst);
    MIB.buildBrCond(RangeCmp, *B.Default);
  }

  // Avoid emitting unnecessary branches to the next block.
  if (MBB != SwitchBB->getNextNode())
    MIB.buildBr(*MBB);
}

void IRTranslator::emitBitTestCase(SwitchCG::BitTestBlock &BB,
                                   MachineBasicBlock *NextMBB,
                                   BranchProbability BranchProbToNext,
                                   Register Reg, SwitchCG::BitTestCase &B,
                                   MachineBasicBlock *SwitchBB) {
  MachineIRBuilder &MIB = *CurBuilder;
  MIB.setMBB(*SwitchBB);

  LLT SwitchTy = getLLTForMVT(BB.RegVT);
  Register Cmp;
  unsigned PopCount = llvm::popcount(B.Mask);
  if (PopCount == 1) {
    // Testing for a single bit; just compare the shift count with what it
    // would need to be to shift a 1 bit in that position.
    auto MaskTrailingZeros =
        MIB.buildConstant(SwitchTy, llvm::countr_zero(B.Mask));
    Cmp =
        MIB.buildICmp(ICmpInst::ICMP_EQ, LLT::scalar(1), Reg, MaskTrailingZeros)
            .getReg(0);
  } else if (PopCount == BB.Range) {
    // There is only one zero bit in the range, test for it directly.
    auto MaskTrailingOnes =
        MIB.buildConstant(SwitchTy, llvm::countr_one(B.Mask));
    Cmp = MIB.buildICmp(CmpInst::ICMP_NE, LLT::scalar(1), Reg, MaskTrailingOnes)
              .getReg(0);
  } else {
    // Make desired shift.
    auto CstOne = MIB.buildConstant(SwitchTy, 1);
    auto SwitchVal = MIB.buildShl(SwitchTy, CstOne, Reg);

    // Emit bit tests and jumps.
    auto CstMask = MIB.buildConstant(SwitchTy, B.Mask);
    auto AndOp = MIB.buildAnd(SwitchTy, SwitchVal, CstMask);
    auto CstZero = MIB.buildConstant(SwitchTy, 0);
    Cmp = MIB.buildICmp(CmpInst::ICMP_NE, LLT::scalar(1), AndOp, CstZero)
              .getReg(0);
  }

  // The branch probability from SwitchBB to B.TargetBB is B.ExtraProb.
  addSuccessorWithProb(SwitchBB, B.TargetBB, B.ExtraProb);
  // The branch probability from SwitchBB to NextMBB is BranchProbToNext.
  addSuccessorWithProb(SwitchBB, NextMBB, BranchProbToNext);
  // It is not guaranteed that the sum of B.ExtraProb and BranchProbToNext is
  // one as they are relative probabilities (and thus work more like weights),
  // and hence we need to normalize them to let the sum of them become one.
  SwitchBB->normalizeSuccProbs();

  // Record the fact that the IR edge from the header to the bit test target
  // will go through our new block. Neeeded for PHIs to have nodes added.
  addMachineCFGPred({BB.Parent->getBasicBlock(), B.TargetBB->getBasicBlock()},
                    SwitchBB);

  MIB.buildBrCond(Cmp, *B.TargetBB);

  // Avoid emitting unnecessary branches to the next block.
  if (NextMBB != SwitchBB->getNextNode())
    MIB.buildBr(*NextMBB);
}

bool IRTranslator::lowerBitTestWorkItem(
    SwitchCG::SwitchWorkListItem W, MachineBasicBlock *SwitchMBB,
    MachineBasicBlock *CurMBB, MachineBasicBlock *DefaultMBB,
    MachineIRBuilder &MIB, MachineFunction::iterator BBI,
    BranchProbability DefaultProb, BranchProbability UnhandledProbs,
    SwitchCG::CaseClusterIt I, MachineBasicBlock *Fallthrough,
    bool FallthroughUnreachable) {
  using namespace SwitchCG;
  MachineFunction *CurMF = SwitchMBB->getParent();
  // FIXME: Optimize away range check based on pivot comparisons.
  BitTestBlock *BTB = &SL->BitTestCases[I->BTCasesIndex];
  // The bit test blocks haven't been inserted yet; insert them here.
  for (BitTestCase &BTC : BTB->Cases)
    CurMF->insert(BBI, BTC.ThisBB);

  // Fill in fields of the BitTestBlock.
  BTB->Parent = CurMBB;
  BTB->Default = Fallthrough;

  BTB->DefaultProb = UnhandledProbs;
  // If the cases in bit test don't form a contiguous range, we evenly
  // distribute the probability on the edge to Fallthrough to two
  // successors of CurMBB.
  if (!BTB->ContiguousRange) {
    BTB->Prob += DefaultProb / 2;
    BTB->DefaultProb -= DefaultProb / 2;
  }

  if (FallthroughUnreachable)
    BTB->FallthroughUnreachable = true;

  // If we're in the right place, emit the bit test header right now.
  if (CurMBB == SwitchMBB) {
    emitBitTestHeader(*BTB, SwitchMBB);
    BTB->Emitted = true;
  }
  return true;
}

bool IRTranslator::lowerSwitchWorkItem(SwitchCG::SwitchWorkListItem W,
                                       Value *Cond,
                                       MachineBasicBlock *SwitchMBB,
                                       MachineBasicBlock *DefaultMBB,
                                       MachineIRBuilder &MIB) {
  using namespace SwitchCG;
  MachineFunction *CurMF = FuncInfo.MF;
  MachineBasicBlock *NextMBB = nullptr;
  MachineFunction::iterator BBI(W.MBB);
  if (++BBI != FuncInfo.MF->end())
    NextMBB = &*BBI;

  if (EnableOpts) {
    // Here, we order cases by probability so the most likely case will be
    // checked first. However, two clusters can have the same probability in
    // which case their relative ordering is non-deterministic. So we use Low
    // as a tie-breaker as clusters are guaranteed to never overlap.
    llvm::sort(W.FirstCluster, W.LastCluster + 1,
               [](const CaseCluster &a, const CaseCluster &b) {
                 return a.Prob != b.Prob
                            ? a.Prob > b.Prob
                            : a.Low->getValue().slt(b.Low->getValue());
               });

    // Rearrange the case blocks so that the last one falls through if possible
    // without changing the order of probabilities.
    for (CaseClusterIt I = W.LastCluster; I > W.FirstCluster;) {
      --I;
      if (I->Prob > W.LastCluster->Prob)
        break;
      if (I->Kind == CC_Range && I->MBB == NextMBB) {
        std::swap(*I, *W.LastCluster);
        break;
      }
    }
  }

  // Compute total probability.
  BranchProbability DefaultProb = W.DefaultProb;
  BranchProbability UnhandledProbs = DefaultProb;
  for (CaseClusterIt I = W.FirstCluster; I <= W.LastCluster; ++I)
    UnhandledProbs += I->Prob;

  MachineBasicBlock *CurMBB = W.MBB;
  for (CaseClusterIt I = W.FirstCluster, E = W.LastCluster; I <= E; ++I) {
    bool FallthroughUnreachable = false;
    MachineBasicBlock *Fallthrough;
    if (I == W.LastCluster) {
      // For the last cluster, fall through to the default destination.
      Fallthrough = DefaultMBB;
      FallthroughUnreachable = isa<UnreachableInst>(
          DefaultMBB->getBasicBlock()->getFirstNonPHIOrDbg());
    } else {
      Fallthrough = CurMF->CreateMachineBasicBlock(CurMBB->getBasicBlock());
      CurMF->insert(BBI, Fallthrough);
    }
    UnhandledProbs -= I->Prob;

    switch (I->Kind) {
    case CC_BitTests: {
      if (!lowerBitTestWorkItem(W, SwitchMBB, CurMBB, DefaultMBB, MIB, BBI,
                                DefaultProb, UnhandledProbs, I, Fallthrough,
                                FallthroughUnreachable)) {
        LLVM_DEBUG(dbgs() << "Failed to lower bit test for switch");
        return false;
      }
      break;
    }

    case CC_JumpTable: {
      if (!lowerJumpTableWorkItem(W, SwitchMBB, CurMBB, DefaultMBB, MIB, BBI,
                                  UnhandledProbs, I, Fallthrough,
                                  FallthroughUnreachable)) {
        LLVM_DEBUG(dbgs() << "Failed to lower jump table");
        return false;
      }
      break;
    }
    case CC_Range: {
      if (!lowerSwitchRangeWorkItem(I, Cond, Fallthrough,
                                    FallthroughUnreachable, UnhandledProbs,
                                    CurMBB, MIB, SwitchMBB)) {
        LLVM_DEBUG(dbgs() << "Failed to lower switch range");
        return false;
      }
      break;
    }
    }
    CurMBB = Fallthrough;
  }

  return true;
}

bool IRTranslator::translateIndirectBr(const User &U,
                                       MachineIRBuilder &MIRBuilder) {
  const IndirectBrInst &BrInst = cast<IndirectBrInst>(U);

  const Register Tgt = getOrCreateVReg(*BrInst.getAddress());
  MIRBuilder.buildBrIndirect(Tgt);

  // Link successors.
  SmallPtrSet<const BasicBlock *, 32> AddedSuccessors;
  MachineBasicBlock &CurBB = MIRBuilder.getMBB();
  for (const BasicBlock *Succ : successors(&BrInst)) {
    // It's legal for indirectbr instructions to have duplicate blocks in the
    // destination list. We don't allow this in MIR. Skip anything that's
    // already a successor.
    if (!AddedSuccessors.insert(Succ).second)
      continue;
    CurBB.addSuccessor(&getMBB(*Succ));
  }

  return true;
}

static bool isSwiftError(const Value *V) {
  if (auto Arg = dyn_cast<Argument>(V))
    return Arg->hasSwiftErrorAttr();
  if (auto AI = dyn_cast<AllocaInst>(V))
    return AI->isSwiftError();
  return false;
}

bool IRTranslator::translateLoad(const User &U, MachineIRBuilder &MIRBuilder) {
  const LoadInst &LI = cast<LoadInst>(U);
  TypeSize StoreSize = DL->getTypeStoreSize(LI.getType());
  if (StoreSize.isZero())
    return true;

  ArrayRef<Register> Regs = getOrCreateVRegs(LI);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(LI);
  Register Base = getOrCreateVReg(*LI.getPointerOperand());
  AAMDNodes AAInfo = LI.getAAMetadata();

  const Value *Ptr = LI.getPointerOperand();
  Type *OffsetIRTy = DL->getIndexType(Ptr->getType());
  LLT OffsetTy = getLLTForType(*OffsetIRTy, *DL);

  if (CLI->supportSwiftError() && isSwiftError(Ptr)) {
    assert(Regs.size() == 1 && "swifterror should be single pointer");
    Register VReg =
        SwiftError.getOrCreateVRegUseAt(&LI, &MIRBuilder.getMBB(), Ptr);
    MIRBuilder.buildCopy(Regs[0], VReg);
    return true;
  }

  MachineMemOperand::Flags Flags =
      TLI->getLoadMemOperandFlags(LI, *DL, AC, LibInfo);
  if (AA && !(Flags & MachineMemOperand::MOInvariant)) {
    if (AA->pointsToConstantMemory(
            MemoryLocation(Ptr, LocationSize::precise(StoreSize), AAInfo))) {
      Flags |= MachineMemOperand::MOInvariant;
    }
  }

  const MDNode *Ranges =
      Regs.size() == 1 ? LI.getMetadata(LLVMContext::MD_range) : nullptr;
  for (unsigned i = 0; i < Regs.size(); ++i) {
    Register Addr;
    MIRBuilder.materializePtrAdd(Addr, Base, OffsetTy, Offsets[i] / 8);

    MachinePointerInfo Ptr(LI.getPointerOperand(), Offsets[i] / 8);
    Align BaseAlign = getMemOpAlign(LI);
    auto MMO = MF->getMachineMemOperand(
        Ptr, Flags, MRI->getType(Regs[i]),
        commonAlignment(BaseAlign, Offsets[i] / 8), AAInfo, Ranges,
        LI.getSyncScopeID(), LI.getOrdering());
    MIRBuilder.buildLoad(Regs[i], Addr, *MMO);
  }

  return true;
}

bool IRTranslator::translateStore(const User &U, MachineIRBuilder &MIRBuilder) {
  const StoreInst &SI = cast<StoreInst>(U);
  if (DL->getTypeStoreSize(SI.getValueOperand()->getType()).isZero())
    return true;

  ArrayRef<Register> Vals = getOrCreateVRegs(*SI.getValueOperand());
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(*SI.getValueOperand());
  Register Base = getOrCreateVReg(*SI.getPointerOperand());

  Type *OffsetIRTy = DL->getIndexType(SI.getPointerOperandType());
  LLT OffsetTy = getLLTForType(*OffsetIRTy, *DL);

  if (CLI->supportSwiftError() && isSwiftError(SI.getPointerOperand())) {
    assert(Vals.size() == 1 && "swifterror should be single pointer");

    Register VReg = SwiftError.getOrCreateVRegDefAt(&SI, &MIRBuilder.getMBB(),
                                                    SI.getPointerOperand());
    MIRBuilder.buildCopy(VReg, Vals[0]);
    return true;
  }

  MachineMemOperand::Flags Flags = TLI->getStoreMemOperandFlags(SI, *DL);

  for (unsigned i = 0; i < Vals.size(); ++i) {
    Register Addr;
    MIRBuilder.materializePtrAdd(Addr, Base, OffsetTy, Offsets[i] / 8);

    MachinePointerInfo Ptr(SI.getPointerOperand(), Offsets[i] / 8);
    Align BaseAlign = getMemOpAlign(SI);
    auto MMO = MF->getMachineMemOperand(
        Ptr, Flags, MRI->getType(Vals[i]),
        commonAlignment(BaseAlign, Offsets[i] / 8), SI.getAAMetadata(), nullptr,
        SI.getSyncScopeID(), SI.getOrdering());
    MIRBuilder.buildStore(Vals[i], Addr, *MMO);
  }
  return true;
}

static uint64_t getOffsetFromIndices(const User &U, const DataLayout &DL) {
  const Value *Src = U.getOperand(0);
  Type *Int32Ty = Type::getInt32Ty(U.getContext());

  // getIndexedOffsetInType is designed for GEPs, so the first index is the
  // usual array element rather than looking into the actual aggregate.
  SmallVector<Value *, 1> Indices;
  Indices.push_back(ConstantInt::get(Int32Ty, 0));

  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(&U)) {
    for (auto Idx : EVI->indices())
      Indices.push_back(ConstantInt::get(Int32Ty, Idx));
  } else if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(&U)) {
    for (auto Idx : IVI->indices())
      Indices.push_back(ConstantInt::get(Int32Ty, Idx));
  } else {
    for (unsigned i = 1; i < U.getNumOperands(); ++i)
      Indices.push_back(U.getOperand(i));
  }

  return 8 * static_cast<uint64_t>(
                 DL.getIndexedOffsetInType(Src->getType(), Indices));
}

bool IRTranslator::translateExtractValue(const User &U,
                                         MachineIRBuilder &MIRBuilder) {
  const Value *Src = U.getOperand(0);
  uint64_t Offset = getOffsetFromIndices(U, *DL);
  ArrayRef<Register> SrcRegs = getOrCreateVRegs(*Src);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(*Src);
  unsigned Idx = llvm::lower_bound(Offsets, Offset) - Offsets.begin();
  auto &DstRegs = allocateVRegs(U);

  for (unsigned i = 0; i < DstRegs.size(); ++i)
    DstRegs[i] = SrcRegs[Idx++];

  return true;
}

bool IRTranslator::translateInsertValue(const User &U,
                                        MachineIRBuilder &MIRBuilder) {
  const Value *Src = U.getOperand(0);
  uint64_t Offset = getOffsetFromIndices(U, *DL);
  auto &DstRegs = allocateVRegs(U);
  ArrayRef<uint64_t> DstOffsets = *VMap.getOffsets(U);
  ArrayRef<Register> SrcRegs = getOrCreateVRegs(*Src);
  ArrayRef<Register> InsertedRegs = getOrCreateVRegs(*U.getOperand(1));
  auto *InsertedIt = InsertedRegs.begin();

  for (unsigned i = 0; i < DstRegs.size(); ++i) {
    if (DstOffsets[i] >= Offset && InsertedIt != InsertedRegs.end())
      DstRegs[i] = *InsertedIt++;
    else
      DstRegs[i] = SrcRegs[i];
  }

  return true;
}

bool IRTranslator::translateSelect(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  Register Tst = getOrCreateVReg(*U.getOperand(0));
  ArrayRef<Register> ResRegs = getOrCreateVRegs(U);
  ArrayRef<Register> Op0Regs = getOrCreateVRegs(*U.getOperand(1));
  ArrayRef<Register> Op1Regs = getOrCreateVRegs(*U.getOperand(2));

  uint32_t Flags = 0;
  if (const SelectInst *SI = dyn_cast<SelectInst>(&U))
    Flags = MachineInstr::copyFlagsFromInstruction(*SI);

  for (unsigned i = 0; i < ResRegs.size(); ++i) {
    MIRBuilder.buildSelect(ResRegs[i], Tst, Op0Regs[i], Op1Regs[i], Flags);
  }

  return true;
}

bool IRTranslator::translateCopy(const User &U, const Value &V,
                                 MachineIRBuilder &MIRBuilder) {
  Register Src = getOrCreateVReg(V);
  auto &Regs = *VMap.getVRegs(U);
  if (Regs.empty()) {
    Regs.push_back(Src);
    VMap.getOffsets(U)->push_back(0);
  } else {
    // If we already assigned a vreg for this instruction, we can't change that.
    // Emit a copy to satisfy the users we already emitted.
    MIRBuilder.buildCopy(Regs[0], Src);
  }
  return true;
}

bool IRTranslator::translateBitCast(const User &U,
                                    MachineIRBuilder &MIRBuilder) {
  // If we're bitcasting to the source type, we can reuse the source vreg.
  if (getLLTForType(*U.getOperand(0)->getType(), *DL) ==
      getLLTForType(*U.getType(), *DL)) {
    // If the source is a ConstantInt then it was probably created by
    // ConstantHoisting and we should leave it alone.
    if (isa<ConstantInt>(U.getOperand(0)))
      return translateCast(TargetOpcode::G_CONSTANT_FOLD_BARRIER, U,
                           MIRBuilder);
    return translateCopy(U, *U.getOperand(0), MIRBuilder);
  }

  return translateCast(TargetOpcode::G_BITCAST, U, MIRBuilder);
}

bool IRTranslator::translateCast(unsigned Opcode, const User &U,
                                 MachineIRBuilder &MIRBuilder) {
  if (U.getType()->getScalarType()->isBFloatTy() ||
      U.getOperand(0)->getType()->getScalarType()->isBFloatTy())
    return false;

  uint32_t Flags = 0;
  if (const Instruction *I = dyn_cast<Instruction>(&U))
    Flags = MachineInstr::copyFlagsFromInstruction(*I);

  Register Op = getOrCreateVReg(*U.getOperand(0));
  Register Res = getOrCreateVReg(U);
  MIRBuilder.buildInstr(Opcode, {Res}, {Op}, Flags);
  return true;
}

bool IRTranslator::translateGetElementPtr(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  Value &Op0 = *U.getOperand(0);
  Register BaseReg = getOrCreateVReg(Op0);
  Type *PtrIRTy = Op0.getType();
  LLT PtrTy = getLLTForType(*PtrIRTy, *DL);
  Type *OffsetIRTy = DL->getIndexType(PtrIRTy);
  LLT OffsetTy = getLLTForType(*OffsetIRTy, *DL);

  uint32_t Flags = 0;
  if (const Instruction *I = dyn_cast<Instruction>(&U))
    Flags = MachineInstr::copyFlagsFromInstruction(*I);

  // Normalize Vector GEP - all scalar operands should be converted to the
  // splat vector.
  unsigned VectorWidth = 0;

  // True if we should use a splat vector; using VectorWidth alone is not
  // sufficient.
  bool WantSplatVector = false;
  if (auto *VT = dyn_cast<VectorType>(U.getType())) {
    VectorWidth = cast<FixedVectorType>(VT)->getNumElements();
    // We don't produce 1 x N vectors; those are treated as scalars.
    WantSplatVector = VectorWidth > 1;
  }

  // We might need to splat the base pointer into a vector if the offsets
  // are vectors.
  if (WantSplatVector && !PtrTy.isVector()) {
    BaseReg = MIRBuilder
                  .buildSplatBuildVector(LLT::fixed_vector(VectorWidth, PtrTy),
                                         BaseReg)
                  .getReg(0);
    PtrIRTy = FixedVectorType::get(PtrIRTy, VectorWidth);
    PtrTy = getLLTForType(*PtrIRTy, *DL);
    OffsetIRTy = DL->getIndexType(PtrIRTy);
    OffsetTy = getLLTForType(*OffsetIRTy, *DL);
  }

  int64_t Offset = 0;
  for (gep_type_iterator GTI = gep_type_begin(&U), E = gep_type_end(&U);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      unsigned Field = cast<Constant>(Idx)->getUniqueInteger().getZExtValue();
      Offset += DL->getStructLayout(StTy)->getElementOffset(Field);
      continue;
    } else {
      uint64_t ElementSize = GTI.getSequentialElementStride(*DL);

      // If this is a scalar constant or a splat vector of constants,
      // handle it quickly.
      if (const auto *CI = dyn_cast<ConstantInt>(Idx)) {
        if (std::optional<int64_t> Val = CI->getValue().trySExtValue()) {
          Offset += ElementSize * *Val;
          continue;
        }
      }

      if (Offset != 0) {
        auto OffsetMIB = MIRBuilder.buildConstant({OffsetTy}, Offset);
        BaseReg = MIRBuilder.buildPtrAdd(PtrTy, BaseReg, OffsetMIB.getReg(0))
                      .getReg(0);
        Offset = 0;
      }

      Register IdxReg = getOrCreateVReg(*Idx);
      LLT IdxTy = MRI->getType(IdxReg);
      if (IdxTy != OffsetTy) {
        if (!IdxTy.isVector() && WantSplatVector) {
          IdxReg = MIRBuilder
                       .buildSplatBuildVector(OffsetTy.changeElementType(IdxTy),
                                              IdxReg)
                       .getReg(0);
        }

        IdxReg = MIRBuilder.buildSExtOrTrunc(OffsetTy, IdxReg).getReg(0);
      }

      // N = N + Idx * ElementSize;
      // Avoid doing it for ElementSize of 1.
      Register GepOffsetReg;
      if (ElementSize != 1) {
        auto ElementSizeMIB = MIRBuilder.buildConstant(
            getLLTForType(*OffsetIRTy, *DL), ElementSize);
        GepOffsetReg =
            MIRBuilder.buildMul(OffsetTy, IdxReg, ElementSizeMIB).getReg(0);
      } else
        GepOffsetReg = IdxReg;

      BaseReg = MIRBuilder.buildPtrAdd(PtrTy, BaseReg, GepOffsetReg).getReg(0);
    }
  }

  if (Offset != 0) {
    auto OffsetMIB =
        MIRBuilder.buildConstant(OffsetTy, Offset);

    if (int64_t(Offset) >= 0 && cast<GEPOperator>(U).isInBounds())
      Flags |= MachineInstr::MIFlag::NoUWrap;

    MIRBuilder.buildPtrAdd(getOrCreateVReg(U), BaseReg, OffsetMIB.getReg(0),
                           Flags);
    return true;
  }

  MIRBuilder.buildCopy(getOrCreateVReg(U), BaseReg);
  return true;
}

bool IRTranslator::translateMemFunc(const CallInst &CI,
                                    MachineIRBuilder &MIRBuilder,
                                    unsigned Opcode) {
  const Value *SrcPtr = CI.getArgOperand(1);
  // If the source is undef, then just emit a nop.
  if (isa<UndefValue>(SrcPtr))
    return true;

  SmallVector<Register, 3> SrcRegs;

  unsigned MinPtrSize = UINT_MAX;
  for (auto AI = CI.arg_begin(), AE = CI.arg_end(); std::next(AI) != AE; ++AI) {
    Register SrcReg = getOrCreateVReg(**AI);
    LLT SrcTy = MRI->getType(SrcReg);
    if (SrcTy.isPointer())
      MinPtrSize = std::min<unsigned>(SrcTy.getSizeInBits(), MinPtrSize);
    SrcRegs.push_back(SrcReg);
  }

  LLT SizeTy = LLT::scalar(MinPtrSize);

  // The size operand should be the minimum of the pointer sizes.
  Register &SizeOpReg = SrcRegs[SrcRegs.size() - 1];
  if (MRI->getType(SizeOpReg) != SizeTy)
    SizeOpReg = MIRBuilder.buildZExtOrTrunc(SizeTy, SizeOpReg).getReg(0);

  auto ICall = MIRBuilder.buildInstr(Opcode);
  for (Register SrcReg : SrcRegs)
    ICall.addUse(SrcReg);

  Align DstAlign;
  Align SrcAlign;
  unsigned IsVol =
      cast<ConstantInt>(CI.getArgOperand(CI.arg_size() - 1))->getZExtValue();

  ConstantInt *CopySize = nullptr;

  if (auto *MCI = dyn_cast<MemCpyInst>(&CI)) {
    DstAlign = MCI->getDestAlign().valueOrOne();
    SrcAlign = MCI->getSourceAlign().valueOrOne();
    CopySize = dyn_cast<ConstantInt>(MCI->getArgOperand(2));
  } else if (auto *MCI = dyn_cast<MemCpyInlineInst>(&CI)) {
    DstAlign = MCI->getDestAlign().valueOrOne();
    SrcAlign = MCI->getSourceAlign().valueOrOne();
    CopySize = dyn_cast<ConstantInt>(MCI->getArgOperand(2));
  } else if (auto *MMI = dyn_cast<MemMoveInst>(&CI)) {
    DstAlign = MMI->getDestAlign().valueOrOne();
    SrcAlign = MMI->getSourceAlign().valueOrOne();
    CopySize = dyn_cast<ConstantInt>(MMI->getArgOperand(2));
  } else {
    auto *MSI = cast<MemSetInst>(&CI);
    DstAlign = MSI->getDestAlign().valueOrOne();
  }

  if (Opcode != TargetOpcode::G_MEMCPY_INLINE) {
    // We need to propagate the tail call flag from the IR inst as an argument.
    // Otherwise, we have to pessimize and assume later that we cannot tail call
    // any memory intrinsics.
    ICall.addImm(CI.isTailCall() ? 1 : 0);
  }

  // Create mem operands to store the alignment and volatile info.
  MachineMemOperand::Flags LoadFlags = MachineMemOperand::MOLoad;
  MachineMemOperand::Flags StoreFlags = MachineMemOperand::MOStore;
  if (IsVol) {
    LoadFlags |= MachineMemOperand::MOVolatile;
    StoreFlags |= MachineMemOperand::MOVolatile;
  }

  AAMDNodes AAInfo = CI.getAAMetadata();
  if (AA && CopySize &&
      AA->pointsToConstantMemory(MemoryLocation(
          SrcPtr, LocationSize::precise(CopySize->getZExtValue()), AAInfo))) {
    LoadFlags |= MachineMemOperand::MOInvariant;

    // FIXME: pointsToConstantMemory probably does not imply dereferenceable,
    // but the previous usage implied it did. Probably should check
    // isDereferenceableAndAlignedPointer.
    LoadFlags |= MachineMemOperand::MODereferenceable;
  }

  ICall.addMemOperand(
      MF->getMachineMemOperand(MachinePointerInfo(CI.getArgOperand(0)),
                               StoreFlags, 1, DstAlign, AAInfo));
  if (Opcode != TargetOpcode::G_MEMSET)
    ICall.addMemOperand(MF->getMachineMemOperand(
        MachinePointerInfo(SrcPtr), LoadFlags, 1, SrcAlign, AAInfo));

  return true;
}

bool IRTranslator::translateTrap(const CallInst &CI,
                                 MachineIRBuilder &MIRBuilder,
                                 unsigned Opcode) {
  StringRef TrapFuncName =
      CI.getAttributes().getFnAttr("trap-func-name").getValueAsString();
  if (TrapFuncName.empty()) {
    if (Opcode == TargetOpcode::G_UBSANTRAP) {
      uint64_t Code = cast<ConstantInt>(CI.getOperand(0))->getZExtValue();
      MIRBuilder.buildInstr(Opcode, {}, ArrayRef<llvm::SrcOp>{Code});
    } else {
      MIRBuilder.buildInstr(Opcode);
    }
    return true;
  }

  CallLowering::CallLoweringInfo Info;
  if (Opcode == TargetOpcode::G_UBSANTRAP)
    Info.OrigArgs.push_back({getOrCreateVRegs(*CI.getArgOperand(0)),
                             CI.getArgOperand(0)->getType(), 0});

  Info.Callee = MachineOperand::CreateES(TrapFuncName.data());
  Info.CB = &CI;
  Info.OrigRet = {Register(), Type::getVoidTy(CI.getContext()), 0};
  return CLI->lowerCall(MIRBuilder, Info);
}

bool IRTranslator::translateVectorInterleave2Intrinsic(
    const CallInst &CI, MachineIRBuilder &MIRBuilder) {
  assert(CI.getIntrinsicID() == Intrinsic::vector_interleave2 &&
         "This function can only be called on the interleave2 intrinsic!");
  // Canonicalize interleave2 to G_SHUFFLE_VECTOR (similar to SelectionDAG).
  Register Op0 = getOrCreateVReg(*CI.getOperand(0));
  Register Op1 = getOrCreateVReg(*CI.getOperand(1));
  Register Res = getOrCreateVReg(CI);

  LLT OpTy = MRI->getType(Op0);
  MIRBuilder.buildShuffleVector(Res, Op0, Op1,
                                createInterleaveMask(OpTy.getNumElements(), 2));

  return true;
}

bool IRTranslator::translateVectorDeinterleave2Intrinsic(
    const CallInst &CI, MachineIRBuilder &MIRBuilder) {
  assert(CI.getIntrinsicID() == Intrinsic::vector_deinterleave2 &&
         "This function can only be called on the deinterleave2 intrinsic!");
  // Canonicalize deinterleave2 to shuffles that extract sub-vectors (similar to
  // SelectionDAG).
  Register Op = getOrCreateVReg(*CI.getOperand(0));
  auto Undef = MIRBuilder.buildUndef(MRI->getType(Op));
  ArrayRef<Register> Res = getOrCreateVRegs(CI);

  LLT ResTy = MRI->getType(Res[0]);
  MIRBuilder.buildShuffleVector(Res[0], Op, Undef,
                                createStrideMask(0, 2, ResTy.getNumElements()));
  MIRBuilder.buildShuffleVector(Res[1], Op, Undef,
                                createStrideMask(1, 2, ResTy.getNumElements()));

  return true;
}

void IRTranslator::getStackGuard(Register DstReg,
                                 MachineIRBuilder &MIRBuilder) {
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  MRI->setRegClass(DstReg, TRI->getPointerRegClass(*MF));
  auto MIB =
      MIRBuilder.buildInstr(TargetOpcode::LOAD_STACK_GUARD, {DstReg}, {});

  Value *Global = TLI->getSDagStackGuard(*MF->getFunction().getParent());
  if (!Global)
    return;

  unsigned AddrSpace = Global->getType()->getPointerAddressSpace();
  LLT PtrTy = LLT::pointer(AddrSpace, DL->getPointerSizeInBits(AddrSpace));

  MachinePointerInfo MPInfo(Global);
  auto Flags = MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
               MachineMemOperand::MODereferenceable;
  MachineMemOperand *MemRef = MF->getMachineMemOperand(
      MPInfo, Flags, PtrTy, DL->getPointerABIAlignment(AddrSpace));
  MIB.setMemRefs({MemRef});
}

bool IRTranslator::translateOverflowIntrinsic(const CallInst &CI, unsigned Op,
                                              MachineIRBuilder &MIRBuilder) {
  ArrayRef<Register> ResRegs = getOrCreateVRegs(CI);
  MIRBuilder.buildInstr(
      Op, {ResRegs[0], ResRegs[1]},
      {getOrCreateVReg(*CI.getOperand(0)), getOrCreateVReg(*CI.getOperand(1))});

  return true;
}

bool IRTranslator::translateFixedPointIntrinsic(unsigned Op, const CallInst &CI,
                                                MachineIRBuilder &MIRBuilder) {
  Register Dst = getOrCreateVReg(CI);
  Register Src0 = getOrCreateVReg(*CI.getOperand(0));
  Register Src1 = getOrCreateVReg(*CI.getOperand(1));
  uint64_t Scale = cast<ConstantInt>(CI.getOperand(2))->getZExtValue();
  MIRBuilder.buildInstr(Op, {Dst}, { Src0, Src1, Scale });
  return true;
}

unsigned IRTranslator::getSimpleIntrinsicOpcode(Intrinsic::ID ID) {
  switch (ID) {
    default:
      break;
    case Intrinsic::acos:
      return TargetOpcode::G_FACOS;
    case Intrinsic::asin:
      return TargetOpcode::G_FASIN;
    case Intrinsic::atan:
      return TargetOpcode::G_FATAN;
    case Intrinsic::bswap:
      return TargetOpcode::G_BSWAP;
    case Intrinsic::bitreverse:
      return TargetOpcode::G_BITREVERSE;
    case Intrinsic::fshl:
      return TargetOpcode::G_FSHL;
    case Intrinsic::fshr:
      return TargetOpcode::G_FSHR;
    case Intrinsic::ceil:
      return TargetOpcode::G_FCEIL;
    case Intrinsic::cos:
      return TargetOpcode::G_FCOS;
    case Intrinsic::cosh:
      return TargetOpcode::G_FCOSH;
    case Intrinsic::ctpop:
      return TargetOpcode::G_CTPOP;
    case Intrinsic::exp:
      return TargetOpcode::G_FEXP;
    case Intrinsic::exp2:
      return TargetOpcode::G_FEXP2;
    case Intrinsic::exp10:
      return TargetOpcode::G_FEXP10;
    case Intrinsic::fabs:
      return TargetOpcode::G_FABS;
    case Intrinsic::copysign:
      return TargetOpcode::G_FCOPYSIGN;
    case Intrinsic::minnum:
      return TargetOpcode::G_FMINNUM;
    case Intrinsic::maxnum:
      return TargetOpcode::G_FMAXNUM;
    case Intrinsic::minimum:
      return TargetOpcode::G_FMINIMUM;
    case Intrinsic::maximum:
      return TargetOpcode::G_FMAXIMUM;
    case Intrinsic::canonicalize:
      return TargetOpcode::G_FCANONICALIZE;
    case Intrinsic::floor:
      return TargetOpcode::G_FFLOOR;
    case Intrinsic::fma:
      return TargetOpcode::G_FMA;
    case Intrinsic::log:
      return TargetOpcode::G_FLOG;
    case Intrinsic::log2:
      return TargetOpcode::G_FLOG2;
    case Intrinsic::log10:
      return TargetOpcode::G_FLOG10;
    case Intrinsic::ldexp:
      return TargetOpcode::G_FLDEXP;
    case Intrinsic::nearbyint:
      return TargetOpcode::G_FNEARBYINT;
    case Intrinsic::pow:
      return TargetOpcode::G_FPOW;
    case Intrinsic::powi:
      return TargetOpcode::G_FPOWI;
    case Intrinsic::rint:
      return TargetOpcode::G_FRINT;
    case Intrinsic::round:
      return TargetOpcode::G_INTRINSIC_ROUND;
    case Intrinsic::roundeven:
      return TargetOpcode::G_INTRINSIC_ROUNDEVEN;
    case Intrinsic::sin:
      return TargetOpcode::G_FSIN;
    case Intrinsic::sinh:
      return TargetOpcode::G_FSINH;
    case Intrinsic::sqrt:
      return TargetOpcode::G_FSQRT;
    case Intrinsic::tan:
      return TargetOpcode::G_FTAN;
    case Intrinsic::tanh:
      return TargetOpcode::G_FTANH;
    case Intrinsic::trunc:
      return TargetOpcode::G_INTRINSIC_TRUNC;
    case Intrinsic::readcyclecounter:
      return TargetOpcode::G_READCYCLECOUNTER;
    case Intrinsic::readsteadycounter:
      return TargetOpcode::G_READSTEADYCOUNTER;
    case Intrinsic::ptrmask:
      return TargetOpcode::G_PTRMASK;
    case Intrinsic::lrint:
      return TargetOpcode::G_INTRINSIC_LRINT;
    case Intrinsic::llrint:
      return TargetOpcode::G_INTRINSIC_LLRINT;
    // FADD/FMUL require checking the FMF, so are handled elsewhere.
    case Intrinsic::vector_reduce_fmin:
      return TargetOpcode::G_VECREDUCE_FMIN;
    case Intrinsic::vector_reduce_fmax:
      return TargetOpcode::G_VECREDUCE_FMAX;
    case Intrinsic::vector_reduce_fminimum:
      return TargetOpcode::G_VECREDUCE_FMINIMUM;
    case Intrinsic::vector_reduce_fmaximum:
      return TargetOpcode::G_VECREDUCE_FMAXIMUM;
    case Intrinsic::vector_reduce_add:
      return TargetOpcode::G_VECREDUCE_ADD;
    case Intrinsic::vector_reduce_mul:
      return TargetOpcode::G_VECREDUCE_MUL;
    case Intrinsic::vector_reduce_and:
      return TargetOpcode::G_VECREDUCE_AND;
    case Intrinsic::vector_reduce_or:
      return TargetOpcode::G_VECREDUCE_OR;
    case Intrinsic::vector_reduce_xor:
      return TargetOpcode::G_VECREDUCE_XOR;
    case Intrinsic::vector_reduce_smax:
      return TargetOpcode::G_VECREDUCE_SMAX;
    case Intrinsic::vector_reduce_smin:
      return TargetOpcode::G_VECREDUCE_SMIN;
    case Intrinsic::vector_reduce_umax:
      return TargetOpcode::G_VECREDUCE_UMAX;
    case Intrinsic::vector_reduce_umin:
      return TargetOpcode::G_VECREDUCE_UMIN;
    case Intrinsic::experimental_vector_compress:
      return TargetOpcode::G_VECTOR_COMPRESS;
    case Intrinsic::lround:
      return TargetOpcode::G_LROUND;
    case Intrinsic::llround:
      return TargetOpcode::G_LLROUND;
    case Intrinsic::get_fpenv:
      return TargetOpcode::G_GET_FPENV;
    case Intrinsic::get_fpmode:
      return TargetOpcode::G_GET_FPMODE;
  }
  return Intrinsic::not_intrinsic;
}

bool IRTranslator::translateSimpleIntrinsic(const CallInst &CI,
                                            Intrinsic::ID ID,
                                            MachineIRBuilder &MIRBuilder) {

  unsigned Op = getSimpleIntrinsicOpcode(ID);

  // Is this a simple intrinsic?
  if (Op == Intrinsic::not_intrinsic)
    return false;

  // Yes. Let's translate it.
  SmallVector<llvm::SrcOp, 4> VRegs;
  for (const auto &Arg : CI.args())
    VRegs.push_back(getOrCreateVReg(*Arg));

  MIRBuilder.buildInstr(Op, {getOrCreateVReg(CI)}, VRegs,
                        MachineInstr::copyFlagsFromInstruction(CI));
  return true;
}

// TODO: Include ConstainedOps.def when all strict instructions are defined.
static unsigned getConstrainedOpcode(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::experimental_constrained_fadd:
    return TargetOpcode::G_STRICT_FADD;
  case Intrinsic::experimental_constrained_fsub:
    return TargetOpcode::G_STRICT_FSUB;
  case Intrinsic::experimental_constrained_fmul:
    return TargetOpcode::G_STRICT_FMUL;
  case Intrinsic::experimental_constrained_fdiv:
    return TargetOpcode::G_STRICT_FDIV;
  case Intrinsic::experimental_constrained_frem:
    return TargetOpcode::G_STRICT_FREM;
  case Intrinsic::experimental_constrained_fma:
    return TargetOpcode::G_STRICT_FMA;
  case Intrinsic::experimental_constrained_sqrt:
    return TargetOpcode::G_STRICT_FSQRT;
  case Intrinsic::experimental_constrained_ldexp:
    return TargetOpcode::G_STRICT_FLDEXP;
  default:
    return 0;
  }
}

bool IRTranslator::translateConstrainedFPIntrinsic(
  const ConstrainedFPIntrinsic &FPI, MachineIRBuilder &MIRBuilder) {
  fp::ExceptionBehavior EB = *FPI.getExceptionBehavior();

  unsigned Opcode = getConstrainedOpcode(FPI.getIntrinsicID());
  if (!Opcode)
    return false;

  uint32_t Flags = MachineInstr::copyFlagsFromInstruction(FPI);
  if (EB == fp::ExceptionBehavior::ebIgnore)
    Flags |= MachineInstr::NoFPExcept;

  SmallVector<llvm::SrcOp, 4> VRegs;
  for (unsigned I = 0, E = FPI.getNonMetadataArgCount(); I != E; ++I)
    VRegs.push_back(getOrCreateVReg(*FPI.getArgOperand(I)));

  MIRBuilder.buildInstr(Opcode, {getOrCreateVReg(FPI)}, VRegs, Flags);
  return true;
}

std::optional<MCRegister> IRTranslator::getArgPhysReg(Argument &Arg) {
  auto VRegs = getOrCreateVRegs(Arg);
  if (VRegs.size() != 1)
    return std::nullopt;

  // Arguments are lowered as a copy of a livein physical register.
  auto *VRegDef = MF->getRegInfo().getVRegDef(VRegs[0]);
  if (!VRegDef || !VRegDef->isCopy())
    return std::nullopt;
  return VRegDef->getOperand(1).getReg().asMCReg();
}

bool IRTranslator::translateIfEntryValueArgument(bool isDeclare, Value *Val,
                                                 const DILocalVariable *Var,
                                                 const DIExpression *Expr,
                                                 const DebugLoc &DL,
                                                 MachineIRBuilder &MIRBuilder) {
  auto *Arg = dyn_cast<Argument>(Val);
  if (!Arg)
    return false;

  if (!Expr->isEntryValue())
    return false;

  std::optional<MCRegister> PhysReg = getArgPhysReg(*Arg);
  if (!PhysReg) {
    LLVM_DEBUG(dbgs() << "Dropping dbg." << (isDeclare ? "declare" : "value")
                      << ": expression is entry_value but "
                      << "couldn't find a physical register\n");
    LLVM_DEBUG(dbgs() << *Var << "\n");
    return true;
  }

  if (isDeclare) {
    // Append an op deref to account for the fact that this is a dbg_declare.
    Expr = DIExpression::append(Expr, dwarf::DW_OP_deref);
    MF->setVariableDbgInfo(Var, Expr, *PhysReg, DL);
  } else {
    MIRBuilder.buildDirectDbgValue(*PhysReg, Var, Expr);
  }

  return true;
}

static unsigned getConvOpcode(Intrinsic::ID ID) {
  switch (ID) {
  default:
    llvm_unreachable("Unexpected intrinsic");
  case Intrinsic::experimental_convergence_anchor:
    return TargetOpcode::CONVERGENCECTRL_ANCHOR;
  case Intrinsic::experimental_convergence_entry:
    return TargetOpcode::CONVERGENCECTRL_ENTRY;
  case Intrinsic::experimental_convergence_loop:
    return TargetOpcode::CONVERGENCECTRL_LOOP;
  }
}

bool IRTranslator::translateConvergenceControlIntrinsic(
    const CallInst &CI, Intrinsic::ID ID, MachineIRBuilder &MIRBuilder) {
  MachineInstrBuilder MIB = MIRBuilder.buildInstr(getConvOpcode(ID));
  Register OutputReg = getOrCreateConvergenceTokenVReg(CI);
  MIB.addDef(OutputReg);

  if (ID == Intrinsic::experimental_convergence_loop) {
    auto Bundle = CI.getOperandBundle(LLVMContext::OB_convergencectrl);
    assert(Bundle && "Expected a convergence control token.");
    Register InputReg =
        getOrCreateConvergenceTokenVReg(*Bundle->Inputs[0].get());
    MIB.addUse(InputReg);
  }

  return true;
}

bool IRTranslator::translateKnownIntrinsic(const CallInst &CI, Intrinsic::ID ID,
                                           MachineIRBuilder &MIRBuilder) {
  if (auto *MI = dyn_cast<AnyMemIntrinsic>(&CI)) {
    if (ORE->enabled()) {
      if (MemoryOpRemark::canHandle(MI, *LibInfo)) {
        MemoryOpRemark R(*ORE, "gisel-irtranslator-memsize", *DL, *LibInfo);
        R.visit(MI);
      }
    }
  }

  // If this is a simple intrinsic (that is, we just need to add a def of
  // a vreg, and uses for each arg operand, then translate it.
  if (translateSimpleIntrinsic(CI, ID, MIRBuilder))
    return true;

  switch (ID) {
  default:
    break;
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end: {
    // No stack colouring in O0, discard region information.
    if (MF->getTarget().getOptLevel() == CodeGenOptLevel::None)
      return true;

    unsigned Op = ID == Intrinsic::lifetime_start ? TargetOpcode::LIFETIME_START
                                                  : TargetOpcode::LIFETIME_END;

    // Get the underlying objects for the location passed on the lifetime
    // marker.
    SmallVector<const Value *, 4> Allocas;
    getUnderlyingObjects(CI.getArgOperand(1), Allocas);

    // Iterate over each underlying object, creating lifetime markers for each
    // static alloca. Quit if we find a non-static alloca.
    for (const Value *V : Allocas) {
      const AllocaInst *AI = dyn_cast<AllocaInst>(V);
      if (!AI)
        continue;

      if (!AI->isStaticAlloca())
        return true;

      MIRBuilder.buildInstr(Op).addFrameIndex(getOrCreateFrameIndex(*AI));
    }
    return true;
  }
  case Intrinsic::dbg_declare: {
    const DbgDeclareInst &DI = cast<DbgDeclareInst>(CI);
    assert(DI.getVariable() && "Missing variable");
    translateDbgDeclareRecord(DI.getAddress(), DI.hasArgList(), DI.getVariable(),
                       DI.getExpression(), DI.getDebugLoc(), MIRBuilder);
    return true;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst &DI = cast<DbgLabelInst>(CI);
    assert(DI.getLabel() && "Missing label");

    assert(DI.getLabel()->isValidLocationForIntrinsic(
               MIRBuilder.getDebugLoc()) &&
           "Expected inlined-at fields to agree");

    MIRBuilder.buildDbgLabel(DI.getLabel());
    return true;
  }
  case Intrinsic::vaend:
    // No target I know of cares about va_end. Certainly no in-tree target
    // does. Simplest intrinsic ever!
    return true;
  case Intrinsic::vastart: {
    Value *Ptr = CI.getArgOperand(0);
    unsigned ListSize = TLI->getVaListSizeInBits(*DL) / 8;
    Align Alignment = getKnownAlignment(Ptr, *DL);

    MIRBuilder.buildInstr(TargetOpcode::G_VASTART, {}, {getOrCreateVReg(*Ptr)})
        .addMemOperand(MF->getMachineMemOperand(MachinePointerInfo(Ptr),
                                                MachineMemOperand::MOStore,
                                                ListSize, Alignment));
    return true;
  }
  case Intrinsic::dbg_assign:
    // A dbg.assign is a dbg.value with more information about stack locations,
    // typically produced during optimisation of variables with leaked
    // addresses. We can treat it like a normal dbg_value intrinsic here; to
    // benefit from the full analysis of stack/SSA locations, GlobalISel would
    // need to register for and use the AssignmentTrackingAnalysis pass.
    [[fallthrough]];
  case Intrinsic::dbg_value: {
    // This form of DBG_VALUE is target-independent.
    const DbgValueInst &DI = cast<DbgValueInst>(CI);
    translateDbgValueRecord(DI.getValue(), DI.hasArgList(), DI.getVariable(),
                       DI.getExpression(), DI.getDebugLoc(), MIRBuilder);
    return true;
  }
  case Intrinsic::uadd_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_UADDO, MIRBuilder);
  case Intrinsic::sadd_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SADDO, MIRBuilder);
  case Intrinsic::usub_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_USUBO, MIRBuilder);
  case Intrinsic::ssub_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SSUBO, MIRBuilder);
  case Intrinsic::umul_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_UMULO, MIRBuilder);
  case Intrinsic::smul_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SMULO, MIRBuilder);
  case Intrinsic::uadd_sat:
    return translateBinaryOp(TargetOpcode::G_UADDSAT, CI, MIRBuilder);
  case Intrinsic::sadd_sat:
    return translateBinaryOp(TargetOpcode::G_SADDSAT, CI, MIRBuilder);
  case Intrinsic::usub_sat:
    return translateBinaryOp(TargetOpcode::G_USUBSAT, CI, MIRBuilder);
  case Intrinsic::ssub_sat:
    return translateBinaryOp(TargetOpcode::G_SSUBSAT, CI, MIRBuilder);
  case Intrinsic::ushl_sat:
    return translateBinaryOp(TargetOpcode::G_USHLSAT, CI, MIRBuilder);
  case Intrinsic::sshl_sat:
    return translateBinaryOp(TargetOpcode::G_SSHLSAT, CI, MIRBuilder);
  case Intrinsic::umin:
    return translateBinaryOp(TargetOpcode::G_UMIN, CI, MIRBuilder);
  case Intrinsic::umax:
    return translateBinaryOp(TargetOpcode::G_UMAX, CI, MIRBuilder);
  case Intrinsic::smin:
    return translateBinaryOp(TargetOpcode::G_SMIN, CI, MIRBuilder);
  case Intrinsic::smax:
    return translateBinaryOp(TargetOpcode::G_SMAX, CI, MIRBuilder);
  case Intrinsic::abs:
    // TODO: Preserve "int min is poison" arg in GMIR?
    return translateUnaryOp(TargetOpcode::G_ABS, CI, MIRBuilder);
  case Intrinsic::smul_fix:
    return translateFixedPointIntrinsic(TargetOpcode::G_SMULFIX, CI, MIRBuilder);
  case Intrinsic::umul_fix:
    return translateFixedPointIntrinsic(TargetOpcode::G_UMULFIX, CI, MIRBuilder);
  case Intrinsic::smul_fix_sat:
    return translateFixedPointIntrinsic(TargetOpcode::G_SMULFIXSAT, CI, MIRBuilder);
  case Intrinsic::umul_fix_sat:
    return translateFixedPointIntrinsic(TargetOpcode::G_UMULFIXSAT, CI, MIRBuilder);
  case Intrinsic::sdiv_fix:
    return translateFixedPointIntrinsic(TargetOpcode::G_SDIVFIX, CI, MIRBuilder);
  case Intrinsic::udiv_fix:
    return translateFixedPointIntrinsic(TargetOpcode::G_UDIVFIX, CI, MIRBuilder);
  case Intrinsic::sdiv_fix_sat:
    return translateFixedPointIntrinsic(TargetOpcode::G_SDIVFIXSAT, CI, MIRBuilder);
  case Intrinsic::udiv_fix_sat:
    return translateFixedPointIntrinsic(TargetOpcode::G_UDIVFIXSAT, CI, MIRBuilder);
  case Intrinsic::fmuladd: {
    const TargetMachine &TM = MF->getTarget();
    Register Dst = getOrCreateVReg(CI);
    Register Op0 = getOrCreateVReg(*CI.getArgOperand(0));
    Register Op1 = getOrCreateVReg(*CI.getArgOperand(1));
    Register Op2 = getOrCreateVReg(*CI.getArgOperand(2));
    if (TM.Options.AllowFPOpFusion != FPOpFusion::Strict &&
        TLI->isFMAFasterThanFMulAndFAdd(*MF,
                                        TLI->getValueType(*DL, CI.getType()))) {
      // TODO: Revisit this to see if we should move this part of the
      // lowering to the combiner.
      MIRBuilder.buildFMA(Dst, Op0, Op1, Op2,
                          MachineInstr::copyFlagsFromInstruction(CI));
    } else {
      LLT Ty = getLLTForType(*CI.getType(), *DL);
      auto FMul = MIRBuilder.buildFMul(
          Ty, Op0, Op1, MachineInstr::copyFlagsFromInstruction(CI));
      MIRBuilder.buildFAdd(Dst, FMul, Op2,
                           MachineInstr::copyFlagsFromInstruction(CI));
    }
    return true;
  }
  case Intrinsic::convert_from_fp16:
    // FIXME: This intrinsic should probably be removed from the IR.
    MIRBuilder.buildFPExt(getOrCreateVReg(CI),
                          getOrCreateVReg(*CI.getArgOperand(0)),
                          MachineInstr::copyFlagsFromInstruction(CI));
    return true;
  case Intrinsic::convert_to_fp16:
    // FIXME: This intrinsic should probably be removed from the IR.
    MIRBuilder.buildFPTrunc(getOrCreateVReg(CI),
                            getOrCreateVReg(*CI.getArgOperand(0)),
                            MachineInstr::copyFlagsFromInstruction(CI));
    return true;
  case Intrinsic::frexp: {
    ArrayRef<Register> VRegs = getOrCreateVRegs(CI);
    MIRBuilder.buildFFrexp(VRegs[0], VRegs[1],
                           getOrCreateVReg(*CI.getArgOperand(0)),
                           MachineInstr::copyFlagsFromInstruction(CI));
    return true;
  }
  case Intrinsic::memcpy_inline:
    return translateMemFunc(CI, MIRBuilder, TargetOpcode::G_MEMCPY_INLINE);
  case Intrinsic::memcpy:
    return translateMemFunc(CI, MIRBuilder, TargetOpcode::G_MEMCPY);
  case Intrinsic::memmove:
    return translateMemFunc(CI, MIRBuilder, TargetOpcode::G_MEMMOVE);
  case Intrinsic::memset:
    return translateMemFunc(CI, MIRBuilder, TargetOpcode::G_MEMSET);
  case Intrinsic::eh_typeid_for: {
    GlobalValue *GV = ExtractTypeInfo(CI.getArgOperand(0));
    Register Reg = getOrCreateVReg(CI);
    unsigned TypeID = MF->getTypeIDFor(GV);
    MIRBuilder.buildConstant(Reg, TypeID);
    return true;
  }
  case Intrinsic::objectsize:
    llvm_unreachable("llvm.objectsize.* should have been lowered already");

  case Intrinsic::is_constant:
    llvm_unreachable("llvm.is.constant.* should have been lowered already");

  case Intrinsic::stackguard:
    getStackGuard(getOrCreateVReg(CI), MIRBuilder);
    return true;
  case Intrinsic::stackprotector: {
    LLT PtrTy = getLLTForType(*CI.getArgOperand(0)->getType(), *DL);
    Register GuardVal;
    if (TLI->useLoadStackGuardNode()) {
      GuardVal = MRI->createGenericVirtualRegister(PtrTy);
      getStackGuard(GuardVal, MIRBuilder);
    } else
      GuardVal = getOrCreateVReg(*CI.getArgOperand(0)); // The guard's value.

    AllocaInst *Slot = cast<AllocaInst>(CI.getArgOperand(1));
    int FI = getOrCreateFrameIndex(*Slot);
    MF->getFrameInfo().setStackProtectorIndex(FI);

    MIRBuilder.buildStore(
        GuardVal, getOrCreateVReg(*Slot),
        *MF->getMachineMemOperand(MachinePointerInfo::getFixedStack(*MF, FI),
                                  MachineMemOperand::MOStore |
                                      MachineMemOperand::MOVolatile,
                                  PtrTy, Align(8)));
    return true;
  }
  case Intrinsic::stacksave: {
    MIRBuilder.buildInstr(TargetOpcode::G_STACKSAVE, {getOrCreateVReg(CI)}, {});
    return true;
  }
  case Intrinsic::stackrestore: {
    MIRBuilder.buildInstr(TargetOpcode::G_STACKRESTORE, {},
                          {getOrCreateVReg(*CI.getArgOperand(0))});
    return true;
  }
  case Intrinsic::cttz:
  case Intrinsic::ctlz: {
    ConstantInt *Cst = cast<ConstantInt>(CI.getArgOperand(1));
    bool isTrailing = ID == Intrinsic::cttz;
    unsigned Opcode = isTrailing
                          ? Cst->isZero() ? TargetOpcode::G_CTTZ
                                          : TargetOpcode::G_CTTZ_ZERO_UNDEF
                          : Cst->isZero() ? TargetOpcode::G_CTLZ
                                          : TargetOpcode::G_CTLZ_ZERO_UNDEF;
    MIRBuilder.buildInstr(Opcode, {getOrCreateVReg(CI)},
                          {getOrCreateVReg(*CI.getArgOperand(0))});
    return true;
  }
  case Intrinsic::invariant_start: {
    LLT PtrTy = getLLTForType(*CI.getArgOperand(0)->getType(), *DL);
    Register Undef = MRI->createGenericVirtualRegister(PtrTy);
    MIRBuilder.buildUndef(Undef);
    return true;
  }
  case Intrinsic::invariant_end:
    return true;
  case Intrinsic::expect:
  case Intrinsic::annotation:
  case Intrinsic::ptr_annotation:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group: {
    // Drop the intrinsic, but forward the value.
    MIRBuilder.buildCopy(getOrCreateVReg(CI),
                         getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  }
  case Intrinsic::assume:
  case Intrinsic::experimental_noalias_scope_decl:
  case Intrinsic::var_annotation:
  case Intrinsic::sideeffect:
    // Discard annotate attributes, assumptions, and artificial side-effects.
    return true;
  case Intrinsic::read_volatile_register:
  case Intrinsic::read_register: {
    Value *Arg = CI.getArgOperand(0);
    MIRBuilder
        .buildInstr(TargetOpcode::G_READ_REGISTER, {getOrCreateVReg(CI)}, {})
        .addMetadata(cast<MDNode>(cast<MetadataAsValue>(Arg)->getMetadata()));
    return true;
  }
  case Intrinsic::write_register: {
    Value *Arg = CI.getArgOperand(0);
    MIRBuilder.buildInstr(TargetOpcode::G_WRITE_REGISTER)
      .addMetadata(cast<MDNode>(cast<MetadataAsValue>(Arg)->getMetadata()))
      .addUse(getOrCreateVReg(*CI.getArgOperand(1)));
    return true;
  }
  case Intrinsic::localescape: {
    MachineBasicBlock &EntryMBB = MF->front();
    StringRef EscapedName = GlobalValue::dropLLVMManglingEscape(MF->getName());

    // Directly emit some LOCAL_ESCAPE machine instrs. Label assignment emission
    // is the same on all targets.
    for (unsigned Idx = 0, E = CI.arg_size(); Idx < E; ++Idx) {
      Value *Arg = CI.getArgOperand(Idx)->stripPointerCasts();
      if (isa<ConstantPointerNull>(Arg))
        continue; // Skip null pointers. They represent a hole in index space.

      int FI = getOrCreateFrameIndex(*cast<AllocaInst>(Arg));
      MCSymbol *FrameAllocSym =
          MF->getContext().getOrCreateFrameAllocSymbol(EscapedName, Idx);

      // This should be inserted at the start of the entry block.
      auto LocalEscape =
          MIRBuilder.buildInstrNoInsert(TargetOpcode::LOCAL_ESCAPE)
              .addSym(FrameAllocSym)
              .addFrameIndex(FI);

      EntryMBB.insert(EntryMBB.begin(), LocalEscape);
    }

    return true;
  }
  case Intrinsic::vector_reduce_fadd:
  case Intrinsic::vector_reduce_fmul: {
    // Need to check for the reassoc flag to decide whether we want a
    // sequential reduction opcode or not.
    Register Dst = getOrCreateVReg(CI);
    Register ScalarSrc = getOrCreateVReg(*CI.getArgOperand(0));
    Register VecSrc = getOrCreateVReg(*CI.getArgOperand(1));
    unsigned Opc = 0;
    if (!CI.hasAllowReassoc()) {
      // The sequential ordering case.
      Opc = ID == Intrinsic::vector_reduce_fadd
                ? TargetOpcode::G_VECREDUCE_SEQ_FADD
                : TargetOpcode::G_VECREDUCE_SEQ_FMUL;
      MIRBuilder.buildInstr(Opc, {Dst}, {ScalarSrc, VecSrc},
                            MachineInstr::copyFlagsFromInstruction(CI));
      return true;
    }
    // We split the operation into a separate G_FADD/G_FMUL + the reduce,
    // since the associativity doesn't matter.
    unsigned ScalarOpc;
    if (ID == Intrinsic::vector_reduce_fadd) {
      Opc = TargetOpcode::G_VECREDUCE_FADD;
      ScalarOpc = TargetOpcode::G_FADD;
    } else {
      Opc = TargetOpcode::G_VECREDUCE_FMUL;
      ScalarOpc = TargetOpcode::G_FMUL;
    }
    LLT DstTy = MRI->getType(Dst);
    auto Rdx = MIRBuilder.buildInstr(
        Opc, {DstTy}, {VecSrc}, MachineInstr::copyFlagsFromInstruction(CI));
    MIRBuilder.buildInstr(ScalarOpc, {Dst}, {ScalarSrc, Rdx},
                          MachineInstr::copyFlagsFromInstruction(CI));

    return true;
  }
  case Intrinsic::trap:
    return translateTrap(CI, MIRBuilder, TargetOpcode::G_TRAP);
  case Intrinsic::debugtrap:
    return translateTrap(CI, MIRBuilder, TargetOpcode::G_DEBUGTRAP);
  case Intrinsic::ubsantrap:
    return translateTrap(CI, MIRBuilder, TargetOpcode::G_UBSANTRAP);
  case Intrinsic::allow_runtime_check:
  case Intrinsic::allow_ubsan_check:
    MIRBuilder.buildCopy(getOrCreateVReg(CI),
                         getOrCreateVReg(*ConstantInt::getTrue(CI.getType())));
    return true;
  case Intrinsic::amdgcn_cs_chain:
    return translateCallBase(CI, MIRBuilder);
  case Intrinsic::fptrunc_round: {
    uint32_t Flags = MachineInstr::copyFlagsFromInstruction(CI);

    // Convert the metadata argument to a constant integer
    Metadata *MD = cast<MetadataAsValue>(CI.getArgOperand(1))->getMetadata();
    std::optional<RoundingMode> RoundMode =
        convertStrToRoundingMode(cast<MDString>(MD)->getString());

    // Add the Rounding mode as an integer
    MIRBuilder
        .buildInstr(TargetOpcode::G_INTRINSIC_FPTRUNC_ROUND,
                    {getOrCreateVReg(CI)},
                    {getOrCreateVReg(*CI.getArgOperand(0))}, Flags)
        .addImm((int)*RoundMode);

    return true;
  }
  case Intrinsic::is_fpclass: {
    Value *FpValue = CI.getOperand(0);
    ConstantInt *TestMaskValue = cast<ConstantInt>(CI.getOperand(1));

    MIRBuilder
        .buildInstr(TargetOpcode::G_IS_FPCLASS, {getOrCreateVReg(CI)},
                    {getOrCreateVReg(*FpValue)})
        .addImm(TestMaskValue->getZExtValue());

    return true;
  }
  case Intrinsic::set_fpenv: {
    Value *FPEnv = CI.getOperand(0);
    MIRBuilder.buildSetFPEnv(getOrCreateVReg(*FPEnv));
    return true;
  }
  case Intrinsic::reset_fpenv:
    MIRBuilder.buildResetFPEnv();
    return true;
  case Intrinsic::set_fpmode: {
    Value *FPState = CI.getOperand(0);
    MIRBuilder.buildSetFPMode(getOrCreateVReg(*FPState));
    return true;
  }
  case Intrinsic::reset_fpmode:
    MIRBuilder.buildResetFPMode();
    return true;
  case Intrinsic::vscale: {
    MIRBuilder.buildVScale(getOrCreateVReg(CI), 1);
    return true;
  }
  case Intrinsic::scmp:
    MIRBuilder.buildSCmp(getOrCreateVReg(CI),
                         getOrCreateVReg(*CI.getOperand(0)),
                         getOrCreateVReg(*CI.getOperand(1)));
    return true;
  case Intrinsic::ucmp:
    MIRBuilder.buildUCmp(getOrCreateVReg(CI),
                         getOrCreateVReg(*CI.getOperand(0)),
                         getOrCreateVReg(*CI.getOperand(1)));
    return true;
  case Intrinsic::prefetch: {
    Value *Addr = CI.getOperand(0);
    unsigned RW = cast<ConstantInt>(CI.getOperand(1))->getZExtValue();
    unsigned Locality = cast<ConstantInt>(CI.getOperand(2))->getZExtValue();
    unsigned CacheType = cast<ConstantInt>(CI.getOperand(3))->getZExtValue();

    auto Flags = RW ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
    auto &MMO = *MF->getMachineMemOperand(MachinePointerInfo(Addr), Flags,
                                          LLT(), Align());

    MIRBuilder.buildPrefetch(getOrCreateVReg(*Addr), RW, Locality, CacheType,
                             MMO);

    return true;
  }

  case Intrinsic::vector_interleave2:
  case Intrinsic::vector_deinterleave2: {
    // Both intrinsics have at least one operand.
    Value *Op0 = CI.getOperand(0);
    LLT ResTy = getLLTForType(*Op0->getType(), MIRBuilder.getDataLayout());
    if (!ResTy.isFixedVector())
      return false;

    if (CI.getIntrinsicID() == Intrinsic::vector_interleave2)
      return translateVectorInterleave2Intrinsic(CI, MIRBuilder);

    return translateVectorDeinterleave2Intrinsic(CI, MIRBuilder);
  }

#define INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC)  \
  case Intrinsic::INTRINSIC:
#include "llvm/IR/ConstrainedOps.def"
    return translateConstrainedFPIntrinsic(cast<ConstrainedFPIntrinsic>(CI),
                                           MIRBuilder);
  case Intrinsic::experimental_convergence_anchor:
  case Intrinsic::experimental_convergence_entry:
  case Intrinsic::experimental_convergence_loop:
    return translateConvergenceControlIntrinsic(CI, ID, MIRBuilder);
  }
  return false;
}

bool IRTranslator::translateInlineAsm(const CallBase &CB,
                                      MachineIRBuilder &MIRBuilder) {

  const InlineAsmLowering *ALI = MF->getSubtarget().getInlineAsmLowering();

  if (!ALI) {
    LLVM_DEBUG(
        dbgs() << "Inline asm lowering is not supported for this target yet\n");
    return false;
  }

  return ALI->lowerInlineAsm(
      MIRBuilder, CB, [&](const Value &Val) { return getOrCreateVRegs(Val); });
}

bool IRTranslator::translateCallBase(const CallBase &CB,
                                     MachineIRBuilder &MIRBuilder) {
  ArrayRef<Register> Res = getOrCreateVRegs(CB);

  SmallVector<ArrayRef<Register>, 8> Args;
  Register SwiftInVReg = 0;
  Register SwiftErrorVReg = 0;
  for (const auto &Arg : CB.args()) {
    if (CLI->supportSwiftError() && isSwiftError(Arg)) {
      assert(SwiftInVReg == 0 && "Expected only one swift error argument");
      LLT Ty = getLLTForType(*Arg->getType(), *DL);
      SwiftInVReg = MRI->createGenericVirtualRegister(Ty);
      MIRBuilder.buildCopy(SwiftInVReg, SwiftError.getOrCreateVRegUseAt(
                                            &CB, &MIRBuilder.getMBB(), Arg));
      Args.emplace_back(ArrayRef(SwiftInVReg));
      SwiftErrorVReg =
          SwiftError.getOrCreateVRegDefAt(&CB, &MIRBuilder.getMBB(), Arg);
      continue;
    }
    Args.push_back(getOrCreateVRegs(*Arg));
  }

  if (auto *CI = dyn_cast<CallInst>(&CB)) {
    if (ORE->enabled()) {
      if (MemoryOpRemark::canHandle(CI, *LibInfo)) {
        MemoryOpRemark R(*ORE, "gisel-irtranslator-memsize", *DL, *LibInfo);
        R.visit(CI);
      }
    }
  }

  std::optional<CallLowering::PtrAuthInfo> PAI;
  if (auto Bundle = CB.getOperandBundle(LLVMContext::OB_ptrauth)) {
    // Functions should never be ptrauth-called directly.
    assert(!CB.getCalledFunction() && "invalid direct ptrauth call");

    const Value *Key = Bundle->Inputs[0];
    const Value *Discriminator = Bundle->Inputs[1];

    // Look through ptrauth constants to try to eliminate the matching bundle
    // and turn this into a direct call with no ptrauth.
    // CallLowering will use the raw pointer if it doesn't find the PAI.
    const auto *CalleeCPA = dyn_cast<ConstantPtrAuth>(CB.getCalledOperand());
    if (!CalleeCPA || !isa<Function>(CalleeCPA->getPointer()) ||
        !CalleeCPA->isKnownCompatibleWith(Key, Discriminator, *DL)) {
      // If we can't make it direct, package the bundle into PAI.
      Register DiscReg = getOrCreateVReg(*Discriminator);
      PAI = CallLowering::PtrAuthInfo{cast<ConstantInt>(Key)->getZExtValue(),
                                      DiscReg};
    }
  }

  Register ConvergenceCtrlToken = 0;
  if (auto Bundle = CB.getOperandBundle(LLVMContext::OB_convergencectrl)) {
    const auto &Token = *Bundle->Inputs[0].get();
    ConvergenceCtrlToken = getOrCreateConvergenceTokenVReg(Token);
  }

  // We don't set HasCalls on MFI here yet because call lowering may decide to
  // optimize into tail calls. Instead, we defer that to selection where a final
  // scan is done to check if any instructions are calls.
  bool Success = CLI->lowerCall(
      MIRBuilder, CB, Res, Args, SwiftErrorVReg, PAI, ConvergenceCtrlToken,
      [&]() { return getOrCreateVReg(*CB.getCalledOperand()); });

  // Check if we just inserted a tail call.
  if (Success) {
    assert(!HasTailCall && "Can't tail call return twice from block?");
    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    HasTailCall = TII->isTailCall(*std::prev(MIRBuilder.getInsertPt()));
  }

  return Success;
}

bool IRTranslator::translateCall(const User &U, MachineIRBuilder &MIRBuilder) {
  const CallInst &CI = cast<CallInst>(U);
  auto TII = MF->getTarget().getIntrinsicInfo();
  const Function *F = CI.getCalledFunction();

  // FIXME: support Windows dllimport function calls and calls through
  // weak symbols.
  if (F && (F->hasDLLImportStorageClass() ||
            (MF->getTarget().getTargetTriple().isOSWindows() &&
             F->hasExternalWeakLinkage())))
    return false;

  // FIXME: support control flow guard targets.
  if (CI.countOperandBundlesOfType(LLVMContext::OB_cfguardtarget))
    return false;

  // FIXME: support statepoints and related.
  if (isa<GCStatepointInst, GCRelocateInst, GCResultInst>(U))
    return false;

  if (CI.isInlineAsm())
    return translateInlineAsm(CI, MIRBuilder);

  diagnoseDontCall(CI);

  Intrinsic::ID ID = Intrinsic::not_intrinsic;
  if (F && F->isIntrinsic()) {
    ID = F->getIntrinsicID();
    if (TII && ID == Intrinsic::not_intrinsic)
      ID = static_cast<Intrinsic::ID>(TII->getIntrinsicID(F));
  }

  if (!F || !F->isIntrinsic() || ID == Intrinsic::not_intrinsic)
    return translateCallBase(CI, MIRBuilder);

  assert(ID != Intrinsic::not_intrinsic && "unknown intrinsic");

  if (translateKnownIntrinsic(CI, ID, MIRBuilder))
    return true;

  ArrayRef<Register> ResultRegs;
  if (!CI.getType()->isVoidTy())
    ResultRegs = getOrCreateVRegs(CI);

  // Ignore the callsite attributes. Backend code is most likely not expecting
  // an intrinsic to sometimes have side effects and sometimes not.
  MachineInstrBuilder MIB = MIRBuilder.buildIntrinsic(ID, ResultRegs);
  if (isa<FPMathOperator>(CI))
    MIB->copyIRFlags(CI);

  for (const auto &Arg : enumerate(CI.args())) {
    // If this is required to be an immediate, don't materialize it in a
    // register.
    if (CI.paramHasAttr(Arg.index(), Attribute::ImmArg)) {
      if (ConstantInt *CI = dyn_cast<ConstantInt>(Arg.value())) {
        // imm arguments are more convenient than cimm (and realistically
        // probably sufficient), so use them.
        assert(CI->getBitWidth() <= 64 &&
               "large intrinsic immediates not handled");
        MIB.addImm(CI->getSExtValue());
      } else {
        MIB.addFPImm(cast<ConstantFP>(Arg.value()));
      }
    } else if (auto *MDVal = dyn_cast<MetadataAsValue>(Arg.value())) {
      auto *MD = MDVal->getMetadata();
      auto *MDN = dyn_cast<MDNode>(MD);
      if (!MDN) {
        if (auto *ConstMD = dyn_cast<ConstantAsMetadata>(MD))
          MDN = MDNode::get(MF->getFunction().getContext(), ConstMD);
        else // This was probably an MDString.
          return false;
      }
      MIB.addMetadata(MDN);
    } else {
      ArrayRef<Register> VRegs = getOrCreateVRegs(*Arg.value());
      if (VRegs.size() > 1)
        return false;
      MIB.addUse(VRegs[0]);
    }
  }

  // Add a MachineMemOperand if it is a target mem intrinsic.
  TargetLowering::IntrinsicInfo Info;
  // TODO: Add a GlobalISel version of getTgtMemIntrinsic.
  if (TLI->getTgtMemIntrinsic(Info, CI, *MF, ID)) {
    Align Alignment = Info.align.value_or(
        DL->getABITypeAlign(Info.memVT.getTypeForEVT(F->getContext())));
    LLT MemTy = Info.memVT.isSimple()
                    ? getLLTForMVT(Info.memVT.getSimpleVT())
                    : LLT::scalar(Info.memVT.getStoreSizeInBits());

    // TODO: We currently just fallback to address space 0 if getTgtMemIntrinsic
    //       didn't yield anything useful.
    MachinePointerInfo MPI;
    if (Info.ptrVal)
      MPI = MachinePointerInfo(Info.ptrVal, Info.offset);
    else if (Info.fallbackAddressSpace)
      MPI = MachinePointerInfo(*Info.fallbackAddressSpace);
    MIB.addMemOperand(
        MF->getMachineMemOperand(MPI, Info.flags, MemTy, Alignment, CI.getAAMetadata()));
  }

  if (CI.isConvergent()) {
    if (auto Bundle = CI.getOperandBundle(LLVMContext::OB_convergencectrl)) {
      auto *Token = Bundle->Inputs[0].get();
      Register TokenReg = getOrCreateVReg(*Token);
      MIB.addUse(TokenReg, RegState::Implicit);
    }
  }

  return true;
}

bool IRTranslator::findUnwindDestinations(
    const BasicBlock *EHPadBB,
    BranchProbability Prob,
    SmallVectorImpl<std::pair<MachineBasicBlock *, BranchProbability>>
        &UnwindDests) {
  EHPersonality Personality = classifyEHPersonality(
      EHPadBB->getParent()->getFunction().getPersonalityFn());
  bool IsMSVCCXX = Personality == EHPersonality::MSVC_CXX;
  bool IsCoreCLR = Personality == EHPersonality::CoreCLR;
  bool IsWasmCXX = Personality == EHPersonality::Wasm_CXX;
  bool IsSEH = isAsynchronousEHPersonality(Personality);

  if (IsWasmCXX) {
    // Ignore this for now.
    return false;
  }

  while (EHPadBB) {
    const Instruction *Pad = EHPadBB->getFirstNonPHI();
    BasicBlock *NewEHPadBB = nullptr;
    if (isa<LandingPadInst>(Pad)) {
      // Stop on landingpads. They are not funclets.
      UnwindDests.emplace_back(&getMBB(*EHPadBB), Prob);
      break;
    }
    if (isa<CleanupPadInst>(Pad)) {
      // Stop on cleanup pads. Cleanups are always funclet entries for all known
      // personalities.
      UnwindDests.emplace_back(&getMBB(*EHPadBB), Prob);
      UnwindDests.back().first->setIsEHScopeEntry();
      UnwindDests.back().first->setIsEHFuncletEntry();
      break;
    }
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Pad)) {
      // Add the catchpad handlers to the possible destinations.
      for (const BasicBlock *CatchPadBB : CatchSwitch->handlers()) {
        UnwindDests.emplace_back(&getMBB(*CatchPadBB), Prob);
        // For MSVC++ and the CLR, catchblocks are funclets and need prologues.
        if (IsMSVCCXX || IsCoreCLR)
          UnwindDests.back().first->setIsEHFuncletEntry();
        if (!IsSEH)
          UnwindDests.back().first->setIsEHScopeEntry();
      }
      NewEHPadBB = CatchSwitch->getUnwindDest();
    } else {
      continue;
    }

    BranchProbabilityInfo *BPI = FuncInfo.BPI;
    if (BPI && NewEHPadBB)
      Prob *= BPI->getEdgeProbability(EHPadBB, NewEHPadBB);
    EHPadBB = NewEHPadBB;
  }
  return true;
}

bool IRTranslator::translateInvoke(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  const InvokeInst &I = cast<InvokeInst>(U);
  MCContext &Context = MF->getContext();

  const BasicBlock *ReturnBB = I.getSuccessor(0);
  const BasicBlock *EHPadBB = I.getSuccessor(1);

  const Function *Fn = I.getCalledFunction();

  // FIXME: support invoking patchpoint and statepoint intrinsics.
  if (Fn && Fn->isIntrinsic())
    return false;

  // FIXME: support whatever these are.
  if (I.hasDeoptState())
    return false;

  // FIXME: support control flow guard targets.
  if (I.countOperandBundlesOfType(LLVMContext::OB_cfguardtarget))
    return false;

  // FIXME: support Windows exception handling.
  if (!isa<LandingPadInst>(EHPadBB->getFirstNonPHI()))
    return false;

  // FIXME: support Windows dllimport function calls and calls through
  // weak symbols.
  if (Fn && (Fn->hasDLLImportStorageClass() ||
            (MF->getTarget().getTargetTriple().isOSWindows() &&
             Fn->hasExternalWeakLinkage())))
    return false;

  bool LowerInlineAsm = I.isInlineAsm();
  bool NeedEHLabel = true;

  // Emit the actual call, bracketed by EH_LABELs so that the MF knows about
  // the region covered by the try.
  MCSymbol *BeginSymbol = nullptr;
  if (NeedEHLabel) {
    MIRBuilder.buildInstr(TargetOpcode::G_INVOKE_REGION_START);
    BeginSymbol = Context.createTempSymbol();
    MIRBuilder.buildInstr(TargetOpcode::EH_LABEL).addSym(BeginSymbol);
  }

  if (LowerInlineAsm) {
    if (!translateInlineAsm(I, MIRBuilder))
      return false;
  } else if (!translateCallBase(I, MIRBuilder))
    return false;

  MCSymbol *EndSymbol = nullptr;
  if (NeedEHLabel) {
    EndSymbol = Context.createTempSymbol();
    MIRBuilder.buildInstr(TargetOpcode::EH_LABEL).addSym(EndSymbol);
  }

  SmallVector<std::pair<MachineBasicBlock *, BranchProbability>, 1> UnwindDests;
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  MachineBasicBlock *InvokeMBB = &MIRBuilder.getMBB();
  BranchProbability EHPadBBProb =
      BPI ? BPI->getEdgeProbability(InvokeMBB->getBasicBlock(), EHPadBB)
          : BranchProbability::getZero();

  if (!findUnwindDestinations(EHPadBB, EHPadBBProb, UnwindDests))
    return false;

  MachineBasicBlock &EHPadMBB = getMBB(*EHPadBB),
                    &ReturnMBB = getMBB(*ReturnBB);
  // Update successor info.
  addSuccessorWithProb(InvokeMBB, &ReturnMBB);
  for (auto &UnwindDest : UnwindDests) {
    UnwindDest.first->setIsEHPad();
    addSuccessorWithProb(InvokeMBB, UnwindDest.first, UnwindDest.second);
  }
  InvokeMBB->normalizeSuccProbs();

  if (NeedEHLabel) {
    assert(BeginSymbol && "Expected a begin symbol!");
    assert(EndSymbol && "Expected an end symbol!");
    MF->addInvoke(&EHPadMBB, BeginSymbol, EndSymbol);
  }

  MIRBuilder.buildBr(ReturnMBB);
  return true;
}

bool IRTranslator::translateCallBr(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  // FIXME: Implement this.
  return false;
}

bool IRTranslator::translateLandingPad(const User &U,
                                       MachineIRBuilder &MIRBuilder) {
  const LandingPadInst &LP = cast<LandingPadInst>(U);

  MachineBasicBlock &MBB = MIRBuilder.getMBB();

  MBB.setIsEHPad();

  // If there aren't registers to copy the values into (e.g., during SjLj
  // exceptions), then don't bother.
  const Constant *PersonalityFn = MF->getFunction().getPersonalityFn();
  if (TLI->getExceptionPointerRegister(PersonalityFn) == 0 &&
      TLI->getExceptionSelectorRegister(PersonalityFn) == 0)
    return true;

  // If landingpad's return type is token type, we don't create DAG nodes
  // for its exception pointer and selector value. The extraction of exception
  // pointer or selector value from token type landingpads is not currently
  // supported.
  if (LP.getType()->isTokenTy())
    return true;

  // Add a label to mark the beginning of the landing pad.  Deletion of the
  // landing pad can thus be detected via the MachineModuleInfo.
  MIRBuilder.buildInstr(TargetOpcode::EH_LABEL)
    .addSym(MF->addLandingPad(&MBB));

  // If the unwinder does not preserve all registers, ensure that the
  // function marks the clobbered registers as used.
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  if (auto *RegMask = TRI.getCustomEHPadPreservedMask(*MF))
    MF->getRegInfo().addPhysRegsUsedFromRegMask(RegMask);

  LLT Ty = getLLTForType(*LP.getType(), *DL);
  Register Undef = MRI->createGenericVirtualRegister(Ty);
  MIRBuilder.buildUndef(Undef);

  SmallVector<LLT, 2> Tys;
  for (Type *Ty : cast<StructType>(LP.getType())->elements())
    Tys.push_back(getLLTForType(*Ty, *DL));
  assert(Tys.size() == 2 && "Only two-valued landingpads are supported");

  // Mark exception register as live in.
  Register ExceptionReg = TLI->getExceptionPointerRegister(PersonalityFn);
  if (!ExceptionReg)
    return false;

  MBB.addLiveIn(ExceptionReg);
  ArrayRef<Register> ResRegs = getOrCreateVRegs(LP);
  MIRBuilder.buildCopy(ResRegs[0], ExceptionReg);

  Register SelectorReg = TLI->getExceptionSelectorRegister(PersonalityFn);
  if (!SelectorReg)
    return false;

  MBB.addLiveIn(SelectorReg);
  Register PtrVReg = MRI->createGenericVirtualRegister(Tys[0]);
  MIRBuilder.buildCopy(PtrVReg, SelectorReg);
  MIRBuilder.buildCast(ResRegs[1], PtrVReg);

  return true;
}

bool IRTranslator::translateAlloca(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  auto &AI = cast<AllocaInst>(U);

  if (AI.isSwiftError())
    return true;

  if (AI.isStaticAlloca()) {
    Register Res = getOrCreateVReg(AI);
    int FI = getOrCreateFrameIndex(AI);
    MIRBuilder.buildFrameIndex(Res, FI);
    return true;
  }

  // FIXME: support stack probing for Windows.
  if (MF->getTarget().getTargetTriple().isOSWindows())
    return false;

  // Now we're in the harder dynamic case.
  Register NumElts = getOrCreateVReg(*AI.getArraySize());
  Type *IntPtrIRTy = DL->getIntPtrType(AI.getType());
  LLT IntPtrTy = getLLTForType(*IntPtrIRTy, *DL);
  if (MRI->getType(NumElts) != IntPtrTy) {
    Register ExtElts = MRI->createGenericVirtualRegister(IntPtrTy);
    MIRBuilder.buildZExtOrTrunc(ExtElts, NumElts);
    NumElts = ExtElts;
  }

  Type *Ty = AI.getAllocatedType();

  Register AllocSize = MRI->createGenericVirtualRegister(IntPtrTy);
  Register TySize =
      getOrCreateVReg(*ConstantInt::get(IntPtrIRTy, DL->getTypeAllocSize(Ty)));
  MIRBuilder.buildMul(AllocSize, NumElts, TySize);

  // Round the size of the allocation up to the stack alignment size
  // by add SA-1 to the size. This doesn't overflow because we're computing
  // an address inside an alloca.
  Align StackAlign = MF->getSubtarget().getFrameLowering()->getStackAlign();
  auto SAMinusOne = MIRBuilder.buildConstant(IntPtrTy, StackAlign.value() - 1);
  auto AllocAdd = MIRBuilder.buildAdd(IntPtrTy, AllocSize, SAMinusOne,
                                      MachineInstr::NoUWrap);
  auto AlignCst =
      MIRBuilder.buildConstant(IntPtrTy, ~(uint64_t)(StackAlign.value() - 1));
  auto AlignedAlloc = MIRBuilder.buildAnd(IntPtrTy, AllocAdd, AlignCst);

  Align Alignment = std::max(AI.getAlign(), DL->getPrefTypeAlign(Ty));
  if (Alignment <= StackAlign)
    Alignment = Align(1);
  MIRBuilder.buildDynStackAlloc(getOrCreateVReg(AI), AlignedAlloc, Alignment);

  MF->getFrameInfo().CreateVariableSizedObject(Alignment, &AI);
  assert(MF->getFrameInfo().hasVarSizedObjects());
  return true;
}

bool IRTranslator::translateVAArg(const User &U, MachineIRBuilder &MIRBuilder) {
  // FIXME: We may need more info about the type. Because of how LLT works,
  // we're completely discarding the i64/double distinction here (amongst
  // others). Fortunately the ABIs I know of where that matters don't use va_arg
  // anyway but that's not guaranteed.
  MIRBuilder.buildInstr(TargetOpcode::G_VAARG, {getOrCreateVReg(U)},
                        {getOrCreateVReg(*U.getOperand(0)),
                         DL->getABITypeAlign(U.getType()).value()});
  return true;
}

bool IRTranslator::translateUnreachable(const User &U, MachineIRBuilder &MIRBuilder) {
  if (!MF->getTarget().Options.TrapUnreachable)
    return true;

  auto &UI = cast<UnreachableInst>(U);

  // We may be able to ignore unreachable behind a noreturn call.
  if (const CallInst *Call = dyn_cast_or_null<CallInst>(UI.getPrevNode());
      Call && Call->doesNotReturn()) {
    if (MF->getTarget().Options.NoTrapAfterNoreturn)
      return true;
    // Do not emit an additional trap instruction.
    if (Call->isNonContinuableTrap())
      return true;
  }

  MIRBuilder.buildTrap();
  return true;
}

bool IRTranslator::translateInsertElement(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  // If it is a <1 x Ty> vector, use the scalar as it is
  // not a legal vector type in LLT.
  if (auto *FVT = dyn_cast<FixedVectorType>(U.getType());
      FVT && FVT->getNumElements() == 1)
    return translateCopy(U, *U.getOperand(1), MIRBuilder);

  Register Res = getOrCreateVReg(U);
  Register Val = getOrCreateVReg(*U.getOperand(0));
  Register Elt = getOrCreateVReg(*U.getOperand(1));
  unsigned PreferredVecIdxWidth = TLI->getVectorIdxTy(*DL).getSizeInBits();
  Register Idx;
  if (auto *CI = dyn_cast<ConstantInt>(U.getOperand(2))) {
    if (CI->getBitWidth() != PreferredVecIdxWidth) {
      APInt NewIdx = CI->getValue().zextOrTrunc(PreferredVecIdxWidth);
      auto *NewIdxCI = ConstantInt::get(CI->getContext(), NewIdx);
      Idx = getOrCreateVReg(*NewIdxCI);
    }
  }
  if (!Idx)
    Idx = getOrCreateVReg(*U.getOperand(2));
  if (MRI->getType(Idx).getSizeInBits() != PreferredVecIdxWidth) {
    const LLT VecIdxTy = LLT::scalar(PreferredVecIdxWidth);
    Idx = MIRBuilder.buildZExtOrTrunc(VecIdxTy, Idx).getReg(0);
  }
  MIRBuilder.buildInsertVectorElement(Res, Val, Elt, Idx);
  return true;
}

bool IRTranslator::translateExtractElement(const User &U,
                                           MachineIRBuilder &MIRBuilder) {
  // If it is a <1 x Ty> vector, use the scalar as it is
  // not a legal vector type in LLT.
  if (cast<FixedVectorType>(U.getOperand(0)->getType())->getNumElements() == 1)
    return translateCopy(U, *U.getOperand(0), MIRBuilder);

  Register Res = getOrCreateVReg(U);
  Register Val = getOrCreateVReg(*U.getOperand(0));
  unsigned PreferredVecIdxWidth = TLI->getVectorIdxTy(*DL).getSizeInBits();
  Register Idx;
  if (auto *CI = dyn_cast<ConstantInt>(U.getOperand(1))) {
    if (CI->getBitWidth() != PreferredVecIdxWidth) {
      APInt NewIdx = CI->getValue().zextOrTrunc(PreferredVecIdxWidth);
      auto *NewIdxCI = ConstantInt::get(CI->getContext(), NewIdx);
      Idx = getOrCreateVReg(*NewIdxCI);
    }
  }
  if (!Idx)
    Idx = getOrCreateVReg(*U.getOperand(1));
  if (MRI->getType(Idx).getSizeInBits() != PreferredVecIdxWidth) {
    const LLT VecIdxTy = LLT::scalar(PreferredVecIdxWidth);
    Idx = MIRBuilder.buildZExtOrTrunc(VecIdxTy, Idx).getReg(0);
  }
  MIRBuilder.buildExtractVectorElement(Res, Val, Idx);
  return true;
}

bool IRTranslator::translateShuffleVector(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  // A ShuffleVector that has operates on scalable vectors is a splat vector
  // where the value of the splat vector is the 0th element of the first
  // operand, since the index mask operand is the zeroinitializer (undef and
  // poison are treated as zeroinitializer here).
  if (U.getOperand(0)->getType()->isScalableTy()) {
    Value *Op0 = U.getOperand(0);
    auto SplatVal = MIRBuilder.buildExtractVectorElementConstant(
        LLT::scalar(Op0->getType()->getScalarSizeInBits()),
        getOrCreateVReg(*Op0), 0);
    MIRBuilder.buildSplatVector(getOrCreateVReg(U), SplatVal);
    return true;
  }

  ArrayRef<int> Mask;
  if (auto *SVI = dyn_cast<ShuffleVectorInst>(&U))
    Mask = SVI->getShuffleMask();
  else
    Mask = cast<ConstantExpr>(U).getShuffleMask();
  ArrayRef<int> MaskAlloc = MF->allocateShuffleMask(Mask);
  MIRBuilder
      .buildInstr(TargetOpcode::G_SHUFFLE_VECTOR, {getOrCreateVReg(U)},
                  {getOrCreateVReg(*U.getOperand(0)),
                   getOrCreateVReg(*U.getOperand(1))})
      .addShuffleMask(MaskAlloc);
  return true;
}

bool IRTranslator::translatePHI(const User &U, MachineIRBuilder &MIRBuilder) {
  const PHINode &PI = cast<PHINode>(U);

  SmallVector<MachineInstr *, 4> Insts;
  for (auto Reg : getOrCreateVRegs(PI)) {
    auto MIB = MIRBuilder.buildInstr(TargetOpcode::G_PHI, {Reg}, {});
    Insts.push_back(MIB.getInstr());
  }

  PendingPHIs.emplace_back(&PI, std::move(Insts));
  return true;
}

bool IRTranslator::translateAtomicCmpXchg(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  const AtomicCmpXchgInst &I = cast<AtomicCmpXchgInst>(U);

  auto Flags = TLI->getAtomicMemOperandFlags(I, *DL);

  auto Res = getOrCreateVRegs(I);
  Register OldValRes = Res[0];
  Register SuccessRes = Res[1];
  Register Addr = getOrCreateVReg(*I.getPointerOperand());
  Register Cmp = getOrCreateVReg(*I.getCompareOperand());
  Register NewVal = getOrCreateVReg(*I.getNewValOperand());

  MIRBuilder.buildAtomicCmpXchgWithSuccess(
      OldValRes, SuccessRes, Addr, Cmp, NewVal,
      *MF->getMachineMemOperand(
          MachinePointerInfo(I.getPointerOperand()), Flags, MRI->getType(Cmp),
          getMemOpAlign(I), I.getAAMetadata(), nullptr, I.getSyncScopeID(),
          I.getSuccessOrdering(), I.getFailureOrdering()));
  return true;
}

bool IRTranslator::translateAtomicRMW(const User &U,
                                      MachineIRBuilder &MIRBuilder) {
  const AtomicRMWInst &I = cast<AtomicRMWInst>(U);
  auto Flags = TLI->getAtomicMemOperandFlags(I, *DL);

  Register Res = getOrCreateVReg(I);
  Register Addr = getOrCreateVReg(*I.getPointerOperand());
  Register Val = getOrCreateVReg(*I.getValOperand());

  unsigned Opcode = 0;
  switch (I.getOperation()) {
  default:
    return false;
  case AtomicRMWInst::Xchg:
    Opcode = TargetOpcode::G_ATOMICRMW_XCHG;
    break;
  case AtomicRMWInst::Add:
    Opcode = TargetOpcode::G_ATOMICRMW_ADD;
    break;
  case AtomicRMWInst::Sub:
    Opcode = TargetOpcode::G_ATOMICRMW_SUB;
    break;
  case AtomicRMWInst::And:
    Opcode = TargetOpcode::G_ATOMICRMW_AND;
    break;
  case AtomicRMWInst::Nand:
    Opcode = TargetOpcode::G_ATOMICRMW_NAND;
    break;
  case AtomicRMWInst::Or:
    Opcode = TargetOpcode::G_ATOMICRMW_OR;
    break;
  case AtomicRMWInst::Xor:
    Opcode = TargetOpcode::G_ATOMICRMW_XOR;
    break;
  case AtomicRMWInst::Max:
    Opcode = TargetOpcode::G_ATOMICRMW_MAX;
    break;
  case AtomicRMWInst::Min:
    Opcode = TargetOpcode::G_ATOMICRMW_MIN;
    break;
  case AtomicRMWInst::UMax:
    Opcode = TargetOpcode::G_ATOMICRMW_UMAX;
    break;
  case AtomicRMWInst::UMin:
    Opcode = TargetOpcode::G_ATOMICRMW_UMIN;
    break;
  case AtomicRMWInst::FAdd:
    Opcode = TargetOpcode::G_ATOMICRMW_FADD;
    break;
  case AtomicRMWInst::FSub:
    Opcode = TargetOpcode::G_ATOMICRMW_FSUB;
    break;
  case AtomicRMWInst::FMax:
    Opcode = TargetOpcode::G_ATOMICRMW_FMAX;
    break;
  case AtomicRMWInst::FMin:
    Opcode = TargetOpcode::G_ATOMICRMW_FMIN;
    break;
  case AtomicRMWInst::UIncWrap:
    Opcode = TargetOpcode::G_ATOMICRMW_UINC_WRAP;
    break;
  case AtomicRMWInst::UDecWrap:
    Opcode = TargetOpcode::G_ATOMICRMW_UDEC_WRAP;
    break;
  }

  MIRBuilder.buildAtomicRMW(
      Opcode, Res, Addr, Val,
      *MF->getMachineMemOperand(MachinePointerInfo(I.getPointerOperand()),
                                Flags, MRI->getType(Val), getMemOpAlign(I),
                                I.getAAMetadata(), nullptr, I.getSyncScopeID(),
                                I.getOrdering()));
  return true;
}

bool IRTranslator::translateFence(const User &U,
                                  MachineIRBuilder &MIRBuilder) {
  const FenceInst &Fence = cast<FenceInst>(U);
  MIRBuilder.buildFence(static_cast<unsigned>(Fence.getOrdering()),
                        Fence.getSyncScopeID());
  return true;
}

bool IRTranslator::translateFreeze(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  const ArrayRef<Register> DstRegs = getOrCreateVRegs(U);
  const ArrayRef<Register> SrcRegs = getOrCreateVRegs(*U.getOperand(0));

  assert(DstRegs.size() == SrcRegs.size() &&
         "Freeze with different source and destination type?");

  for (unsigned I = 0; I < DstRegs.size(); ++I) {
    MIRBuilder.buildFreeze(DstRegs[I], SrcRegs[I]);
  }

  return true;
}

void IRTranslator::finishPendingPhis() {
#ifndef NDEBUG
  DILocationVerifier Verifier;
  GISelObserverWrapper WrapperObserver(&Verifier);
  RAIIDelegateInstaller DelInstall(*MF, &WrapperObserver);
#endif // ifndef NDEBUG
  for (auto &Phi : PendingPHIs) {
    const PHINode *PI = Phi.first;
    if (PI->getType()->isEmptyTy())
      continue;
    ArrayRef<MachineInstr *> ComponentPHIs = Phi.second;
    MachineBasicBlock *PhiMBB = ComponentPHIs[0]->getParent();
    EntryBuilder->setDebugLoc(PI->getDebugLoc());
#ifndef NDEBUG
    Verifier.setCurrentInst(PI);
#endif // ifndef NDEBUG

    SmallSet<const MachineBasicBlock *, 16> SeenPreds;
    for (unsigned i = 0; i < PI->getNumIncomingValues(); ++i) {
      auto IRPred = PI->getIncomingBlock(i);
      ArrayRef<Register> ValRegs = getOrCreateVRegs(*PI->getIncomingValue(i));
      for (auto *Pred : getMachinePredBBs({IRPred, PI->getParent()})) {
        if (SeenPreds.count(Pred) || !PhiMBB->isPredecessor(Pred))
          continue;
        SeenPreds.insert(Pred);
        for (unsigned j = 0; j < ValRegs.size(); ++j) {
          MachineInstrBuilder MIB(*MF, ComponentPHIs[j]);
          MIB.addUse(ValRegs[j]);
          MIB.addMBB(Pred);
        }
      }
    }
  }
}

void IRTranslator::translateDbgValueRecord(Value *V, bool HasArgList,
                                     const DILocalVariable *Variable,
                                     const DIExpression *Expression,
                                     const DebugLoc &DL,
                                     MachineIRBuilder &MIRBuilder) {
  assert(Variable->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  // Act as if we're handling a debug intrinsic.
  MIRBuilder.setDebugLoc(DL);

  if (!V || HasArgList) {
    // DI cannot produce a valid DBG_VALUE, so produce an undef DBG_VALUE to
    // terminate any prior location.
    MIRBuilder.buildIndirectDbgValue(0, Variable, Expression);
    return;
  }

  if (const auto *CI = dyn_cast<Constant>(V)) {
    MIRBuilder.buildConstDbgValue(*CI, Variable, Expression);
    return;
  }

  if (auto *AI = dyn_cast<AllocaInst>(V);
      AI && AI->isStaticAlloca() && Expression->startsWithDeref()) {
    // If the value is an alloca and the expression starts with a
    // dereference, track a stack slot instead of a register, as registers
    // may be clobbered.
    auto ExprOperands = Expression->getElements();
    auto *ExprDerefRemoved =
        DIExpression::get(AI->getContext(), ExprOperands.drop_front());
    MIRBuilder.buildFIDbgValue(getOrCreateFrameIndex(*AI), Variable,
                               ExprDerefRemoved);
    return;
  }
  if (translateIfEntryValueArgument(false, V, Variable, Expression, DL,
                                    MIRBuilder))
    return;
  for (Register Reg : getOrCreateVRegs(*V)) {
    // FIXME: This does not handle register-indirect values at offset 0. The
    // direct/indirect thing shouldn't really be handled by something as
    // implicit as reg+noreg vs reg+imm in the first place, but it seems
    // pretty baked in right now.
    MIRBuilder.buildDirectDbgValue(Reg, Variable, Expression);
  }
  return;
}

void IRTranslator::translateDbgDeclareRecord(Value *Address, bool HasArgList,
                                     const DILocalVariable *Variable,
                                     const DIExpression *Expression,
                                     const DebugLoc &DL,
                                     MachineIRBuilder &MIRBuilder) {
  if (!Address || isa<UndefValue>(Address)) {
    LLVM_DEBUG(dbgs() << "Dropping debug info for " << *Variable << "\n");
    return;
  }

  assert(Variable->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  auto AI = dyn_cast<AllocaInst>(Address);
  if (AI && AI->isStaticAlloca()) {
    // Static allocas are tracked at the MF level, no need for DBG_VALUE
    // instructions (in fact, they get ignored if they *do* exist).
    MF->setVariableDbgInfo(Variable, Expression,
                           getOrCreateFrameIndex(*AI), DL);
    return;
  }

  if (translateIfEntryValueArgument(true, Address, Variable,
                                    Expression, DL,
                                    MIRBuilder))
    return;

  // A dbg.declare describes the address of a source variable, so lower it
  // into an indirect DBG_VALUE.
  MIRBuilder.setDebugLoc(DL);
  MIRBuilder.buildIndirectDbgValue(getOrCreateVReg(*Address),
                                   Variable, Expression);
  return;
}

void IRTranslator::translateDbgInfo(const Instruction &Inst,
                                      MachineIRBuilder &MIRBuilder) {
  for (DbgRecord &DR : Inst.getDbgRecordRange()) {
    if (DbgLabelRecord *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
      MIRBuilder.setDebugLoc(DLR->getDebugLoc());
      assert(DLR->getLabel() && "Missing label");
      assert(DLR->getLabel()->isValidLocationForIntrinsic(
                 MIRBuilder.getDebugLoc()) &&
             "Expected inlined-at fields to agree");
      MIRBuilder.buildDbgLabel(DLR->getLabel());
      continue;
    }
    DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);
    const DILocalVariable *Variable = DVR.getVariable();
    const DIExpression *Expression = DVR.getExpression();
    Value *V = DVR.getVariableLocationOp(0);
    if (DVR.isDbgDeclare())
      translateDbgDeclareRecord(V, DVR.hasArgList(), Variable, Expression,
                                DVR.getDebugLoc(), MIRBuilder);
    else
      translateDbgValueRecord(V, DVR.hasArgList(), Variable, Expression,
                              DVR.getDebugLoc(), MIRBuilder);
  }
}

bool IRTranslator::translate(const Instruction &Inst) {
  CurBuilder->setDebugLoc(Inst.getDebugLoc());
  CurBuilder->setPCSections(Inst.getMetadata(LLVMContext::MD_pcsections));
  CurBuilder->setMMRAMetadata(Inst.getMetadata(LLVMContext::MD_mmra));

  if (TLI->fallBackToDAGISel(Inst))
    return false;

  switch (Inst.getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS)                                        \
  case Instruction::OPCODE:                                                    \
    return translate##OPCODE(Inst, *CurBuilder.get());
#include "llvm/IR/Instruction.def"
  default:
    return false;
  }
}

bool IRTranslator::translate(const Constant &C, Register Reg) {
  // We only emit constants into the entry block from here. To prevent jumpy
  // debug behaviour remove debug line.
  if (auto CurrInstDL = CurBuilder->getDL())
    EntryBuilder->setDebugLoc(DebugLoc());

  if (auto CI = dyn_cast<ConstantInt>(&C))
    EntryBuilder->buildConstant(Reg, *CI);
  else if (auto CF = dyn_cast<ConstantFP>(&C))
    EntryBuilder->buildFConstant(Reg, *CF);
  else if (isa<UndefValue>(C))
    EntryBuilder->buildUndef(Reg);
  else if (isa<ConstantPointerNull>(C))
    EntryBuilder->buildConstant(Reg, 0);
  else if (auto GV = dyn_cast<GlobalValue>(&C))
    EntryBuilder->buildGlobalValue(Reg, GV);
  else if (auto CPA = dyn_cast<ConstantPtrAuth>(&C)) {
    Register Addr = getOrCreateVReg(*CPA->getPointer());
    Register AddrDisc = getOrCreateVReg(*CPA->getAddrDiscriminator());
    EntryBuilder->buildConstantPtrAuth(Reg, CPA, Addr, AddrDisc);
  } else if (auto CAZ = dyn_cast<ConstantAggregateZero>(&C)) {
    if (!isa<FixedVectorType>(CAZ->getType()))
      return false;
    // Return the scalar if it is a <1 x Ty> vector.
    unsigned NumElts = CAZ->getElementCount().getFixedValue();
    if (NumElts == 1)
      return translateCopy(C, *CAZ->getElementValue(0u), *EntryBuilder);
    SmallVector<Register, 4> Ops;
    for (unsigned I = 0; I < NumElts; ++I) {
      Constant &Elt = *CAZ->getElementValue(I);
      Ops.push_back(getOrCreateVReg(Elt));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto CV = dyn_cast<ConstantDataVector>(&C)) {
    // Return the scalar if it is a <1 x Ty> vector.
    if (CV->getNumElements() == 1)
      return translateCopy(C, *CV->getElementAsConstant(0), *EntryBuilder);
    SmallVector<Register, 4> Ops;
    for (unsigned i = 0; i < CV->getNumElements(); ++i) {
      Constant &Elt = *CV->getElementAsConstant(i);
      Ops.push_back(getOrCreateVReg(Elt));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto CE = dyn_cast<ConstantExpr>(&C)) {
    switch(CE->getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS)                                        \
  case Instruction::OPCODE:                                                    \
    return translate##OPCODE(*CE, *EntryBuilder.get());
#include "llvm/IR/Instruction.def"
    default:
      return false;
    }
  } else if (auto CV = dyn_cast<ConstantVector>(&C)) {
    if (CV->getNumOperands() == 1)
      return translateCopy(C, *CV->getOperand(0), *EntryBuilder);
    SmallVector<Register, 4> Ops;
    for (unsigned i = 0; i < CV->getNumOperands(); ++i) {
      Ops.push_back(getOrCreateVReg(*CV->getOperand(i)));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto *BA = dyn_cast<BlockAddress>(&C)) {
    EntryBuilder->buildBlockAddress(Reg, BA);
  } else
    return false;

  return true;
}

bool IRTranslator::finalizeBasicBlock(const BasicBlock &BB,
                                      MachineBasicBlock &MBB) {
  for (auto &BTB : SL->BitTestCases) {
    // Emit header first, if it wasn't already emitted.
    if (!BTB.Emitted)
      emitBitTestHeader(BTB, BTB.Parent);

    BranchProbability UnhandledProb = BTB.Prob;
    for (unsigned j = 0, ej = BTB.Cases.size(); j != ej; ++j) {
      UnhandledProb -= BTB.Cases[j].ExtraProb;
      // Set the current basic block to the mbb we wish to insert the code into
      MachineBasicBlock *MBB = BTB.Cases[j].ThisBB;
      // If all cases cover a contiguous range, it is not necessary to jump to
      // the default block after the last bit test fails. This is because the
      // range check during bit test header creation has guaranteed that every
      // case here doesn't go outside the range. In this case, there is no need
      // to perform the last bit test, as it will always be true. Instead, make
      // the second-to-last bit-test fall through to the target of the last bit
      // test, and delete the last bit test.

      MachineBasicBlock *NextMBB;
      if ((BTB.ContiguousRange || BTB.FallthroughUnreachable) && j + 2 == ej) {
        // Second-to-last bit-test with contiguous range: fall through to the
        // target of the final bit test.
        NextMBB = BTB.Cases[j + 1].TargetBB;
      } else if (j + 1 == ej) {
        // For the last bit test, fall through to Default.
        NextMBB = BTB.Default;
      } else {
        // Otherwise, fall through to the next bit test.
        NextMBB = BTB.Cases[j + 1].ThisBB;
      }

      emitBitTestCase(BTB, NextMBB, UnhandledProb, BTB.Reg, BTB.Cases[j], MBB);

      if ((BTB.ContiguousRange || BTB.FallthroughUnreachable) && j + 2 == ej) {
        // We need to record the replacement phi edge here that normally
        // happens in emitBitTestCase before we delete the case, otherwise the
        // phi edge will be lost.
        addMachineCFGPred({BTB.Parent->getBasicBlock(),
                           BTB.Cases[ej - 1].TargetBB->getBasicBlock()},
                          MBB);
        // Since we're not going to use the final bit test, remove it.
        BTB.Cases.pop_back();
        break;
      }
    }
    // This is "default" BB. We have two jumps to it. From "header" BB and from
    // last "case" BB, unless the latter was skipped.
    CFGEdge HeaderToDefaultEdge = {BTB.Parent->getBasicBlock(),
                                   BTB.Default->getBasicBlock()};
    addMachineCFGPred(HeaderToDefaultEdge, BTB.Parent);
    if (!BTB.ContiguousRange) {
      addMachineCFGPred(HeaderToDefaultEdge, BTB.Cases.back().ThisBB);
    }
  }
  SL->BitTestCases.clear();

  for (auto &JTCase : SL->JTCases) {
    // Emit header first, if it wasn't already emitted.
    if (!JTCase.first.Emitted)
      emitJumpTableHeader(JTCase.second, JTCase.first, JTCase.first.HeaderBB);

    emitJumpTable(JTCase.second, JTCase.second.MBB);
  }
  SL->JTCases.clear();

  for (auto &SwCase : SL->SwitchCases)
    emitSwitchCase(SwCase, &CurBuilder->getMBB(), *CurBuilder);
  SL->SwitchCases.clear();

  // Check if we need to generate stack-protector guard checks.
  StackProtector &SP = getAnalysis<StackProtector>();
  if (SP.shouldEmitSDCheck(BB)) {
    bool FunctionBasedInstrumentation =
        TLI->getSSPStackGuardCheck(*MF->getFunction().getParent());
    SPDescriptor.initialize(&BB, &MBB, FunctionBasedInstrumentation);
  }
  // Handle stack protector.
  if (SPDescriptor.shouldEmitFunctionBasedCheckStackProtector()) {
    LLVM_DEBUG(dbgs() << "Unimplemented stack protector case\n");
    return false;
  } else if (SPDescriptor.shouldEmitStackProtector()) {
    MachineBasicBlock *ParentMBB = SPDescriptor.getParentMBB();
    MachineBasicBlock *SuccessMBB = SPDescriptor.getSuccessMBB();

    // Find the split point to split the parent mbb. At the same time copy all
    // physical registers used in the tail of parent mbb into virtual registers
    // before the split point and back into physical registers after the split
    // point. This prevents us needing to deal with Live-ins and many other
    // register allocation issues caused by us splitting the parent mbb. The
    // register allocator will clean up said virtual copies later on.
    MachineBasicBlock::iterator SplitPoint = findSplitPointForStackProtector(
        ParentMBB, *MF->getSubtarget().getInstrInfo());

    // Splice the terminator of ParentMBB into SuccessMBB.
    SuccessMBB->splice(SuccessMBB->end(), ParentMBB, SplitPoint,
                       ParentMBB->end());

    // Add compare/jump on neq/jump to the parent BB.
    if (!emitSPDescriptorParent(SPDescriptor, ParentMBB))
      return false;

    // CodeGen Failure MBB if we have not codegened it yet.
    MachineBasicBlock *FailureMBB = SPDescriptor.getFailureMBB();
    if (FailureMBB->empty()) {
      if (!emitSPDescriptorFailure(SPDescriptor, FailureMBB))
        return false;
    }

    // Clear the Per-BB State.
    SPDescriptor.resetPerBBState();
  }
  return true;
}

bool IRTranslator::emitSPDescriptorParent(StackProtectorDescriptor &SPD,
                                          MachineBasicBlock *ParentBB) {
  CurBuilder->setInsertPt(*ParentBB, ParentBB->end());
  // First create the loads to the guard/stack slot for the comparison.
  Type *PtrIRTy = PointerType::getUnqual(MF->getFunction().getContext());
  const LLT PtrTy = getLLTForType(*PtrIRTy, *DL);
  LLT PtrMemTy = getLLTForMVT(TLI->getPointerMemTy(*DL));

  MachineFrameInfo &MFI = ParentBB->getParent()->getFrameInfo();
  int FI = MFI.getStackProtectorIndex();

  Register Guard;
  Register StackSlotPtr = CurBuilder->buildFrameIndex(PtrTy, FI).getReg(0);
  const Module &M = *ParentBB->getParent()->getFunction().getParent();
  Align Align = DL->getPrefTypeAlign(PointerType::getUnqual(M.getContext()));

  // Generate code to load the content of the guard slot.
  Register GuardVal =
      CurBuilder
          ->buildLoad(PtrMemTy, StackSlotPtr,
                      MachinePointerInfo::getFixedStack(*MF, FI), Align,
                      MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile)
          .getReg(0);

  if (TLI->useStackGuardXorFP()) {
    LLVM_DEBUG(dbgs() << "Stack protector xor'ing with FP not yet implemented");
    return false;
  }

  // Retrieve guard check function, nullptr if instrumentation is inlined.
  if (const Function *GuardCheckFn = TLI->getSSPStackGuardCheck(M)) {
    // This path is currently untestable on GlobalISel, since the only platform
    // that needs this seems to be Windows, and we fall back on that currently.
    // The code still lives here in case that changes.
    // Silence warning about unused variable until the code below that uses
    // 'GuardCheckFn' is enabled.
    (void)GuardCheckFn;
    return false;
#if 0
    // The target provides a guard check function to validate the guard value.
    // Generate a call to that function with the content of the guard slot as
    // argument.
    FunctionType *FnTy = GuardCheckFn->getFunctionType();
    assert(FnTy->getNumParams() == 1 && "Invalid function signature");
    ISD::ArgFlagsTy Flags;
    if (GuardCheckFn->hasAttribute(1, Attribute::AttrKind::InReg))
      Flags.setInReg();
    CallLowering::ArgInfo GuardArgInfo(
        {GuardVal, FnTy->getParamType(0), {Flags}});

    CallLowering::CallLoweringInfo Info;
    Info.OrigArgs.push_back(GuardArgInfo);
    Info.CallConv = GuardCheckFn->getCallingConv();
    Info.Callee = MachineOperand::CreateGA(GuardCheckFn, 0);
    Info.OrigRet = {Register(), FnTy->getReturnType()};
    if (!CLI->lowerCall(MIRBuilder, Info)) {
      LLVM_DEBUG(dbgs() << "Failed to lower call to stack protector check\n");
      return false;
    }
    return true;
#endif
  }

  // If useLoadStackGuardNode returns true, generate LOAD_STACK_GUARD.
  // Otherwise, emit a volatile load to retrieve the stack guard value.
  if (TLI->useLoadStackGuardNode()) {
    Guard =
        MRI->createGenericVirtualRegister(LLT::scalar(PtrTy.getSizeInBits()));
    getStackGuard(Guard, *CurBuilder);
  } else {
    // TODO: test using android subtarget when we support @llvm.thread.pointer.
    const Value *IRGuard = TLI->getSDagStackGuard(M);
    Register GuardPtr = getOrCreateVReg(*IRGuard);

    Guard = CurBuilder
                ->buildLoad(PtrMemTy, GuardPtr,
                            MachinePointerInfo::getFixedStack(*MF, FI), Align,
                            MachineMemOperand::MOLoad |
                                MachineMemOperand::MOVolatile)
                .getReg(0);
  }

  // Perform the comparison.
  auto Cmp =
      CurBuilder->buildICmp(CmpInst::ICMP_NE, LLT::scalar(1), Guard, GuardVal);
  // If the guard/stackslot do not equal, branch to failure MBB.
  CurBuilder->buildBrCond(Cmp, *SPD.getFailureMBB());
  // Otherwise branch to success MBB.
  CurBuilder->buildBr(*SPD.getSuccessMBB());
  return true;
}

bool IRTranslator::emitSPDescriptorFailure(StackProtectorDescriptor &SPD,
                                           MachineBasicBlock *FailureBB) {
  CurBuilder->setInsertPt(*FailureBB, FailureBB->end());

  const RTLIB::Libcall Libcall = RTLIB::STACKPROTECTOR_CHECK_FAIL;
  const char *Name = TLI->getLibcallName(Libcall);

  CallLowering::CallLoweringInfo Info;
  Info.CallConv = TLI->getLibcallCallingConv(Libcall);
  Info.Callee = MachineOperand::CreateES(Name);
  Info.OrigRet = {Register(), Type::getVoidTy(MF->getFunction().getContext()),
                  0};
  if (!CLI->lowerCall(*CurBuilder, Info)) {
    LLVM_DEBUG(dbgs() << "Failed to lower call to stack protector fail\n");
    return false;
  }

  // On PS4/PS5, the "return address" must still be within the calling
  // function, even if it's at the very end, so emit an explicit TRAP here.
  // WebAssembly needs an unreachable instruction after a non-returning call,
  // because the function return type can be different from __stack_chk_fail's
  // return type (void).
  const TargetMachine &TM = MF->getTarget();
  if (TM.getTargetTriple().isPS() || TM.getTargetTriple().isWasm()) {
    LLVM_DEBUG(dbgs() << "Unhandled trap emission for stack protector fail\n");
    return false;
  }
  return true;
}

void IRTranslator::finalizeFunction() {
  // Release the memory used by the different maps we
  // needed during the translation.
  PendingPHIs.clear();
  VMap.reset();
  FrameIndices.clear();
  MachinePreds.clear();
  // MachineIRBuilder::DebugLoc can outlive the DILocation it holds. Clear it
  // to avoid accessing free’d memory (in runOnMachineFunction) and to avoid
  // destroying it twice (in ~IRTranslator() and ~LLVMContext())
  EntryBuilder.reset();
  CurBuilder.reset();
  FuncInfo.clear();
  SPDescriptor.resetPerFunctionState();
}

/// Returns true if a BasicBlock \p BB within a variadic function contains a
/// variadic musttail call.
static bool checkForMustTailInVarArgFn(bool IsVarArg, const BasicBlock &BB) {
  if (!IsVarArg)
    return false;

  // Walk the block backwards, because tail calls usually only appear at the end
  // of a block.
  return llvm::any_of(llvm::reverse(BB), [](const Instruction &I) {
    const auto *CI = dyn_cast<CallInst>(&I);
    return CI && CI->isMustTailCall();
  });
}

bool IRTranslator::runOnMachineFunction(MachineFunction &CurMF) {
  MF = &CurMF;
  const Function &F = MF->getFunction();
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  // Set the CSEConfig and run the analysis.
  GISelCSEInfo *CSEInfo = nullptr;
  TPC = &getAnalysis<TargetPassConfig>();
  bool EnableCSE = EnableCSEInIRTranslator.getNumOccurrences()
                       ? EnableCSEInIRTranslator
                       : TPC->isGISelCSEEnabled();
  TLI = MF->getSubtarget().getTargetLowering();

  if (EnableCSE) {
    EntryBuilder = std::make_unique<CSEMIRBuilder>(CurMF);
    CSEInfo = &Wrapper.get(TPC->getCSEConfig());
    EntryBuilder->setCSEInfo(CSEInfo);
    CurBuilder = std::make_unique<CSEMIRBuilder>(CurMF);
    CurBuilder->setCSEInfo(CSEInfo);
  } else {
    EntryBuilder = std::make_unique<MachineIRBuilder>();
    CurBuilder = std::make_unique<MachineIRBuilder>();
  }
  CLI = MF->getSubtarget().getCallLowering();
  CurBuilder->setMF(*MF);
  EntryBuilder->setMF(*MF);
  MRI = &MF->getRegInfo();
  DL = &F.getDataLayout();
  ORE = std::make_unique<OptimizationRemarkEmitter>(&F);
  const TargetMachine &TM = MF->getTarget();
  TM.resetTargetOptions(F);
  EnableOpts = OptLevel != CodeGenOptLevel::None && !skipFunction(F);
  FuncInfo.MF = MF;
  if (EnableOpts) {
    AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
    FuncInfo.BPI = &getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
  } else {
    AA = nullptr;
    FuncInfo.BPI = nullptr;
  }

  AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(
      MF->getFunction());
  LibInfo = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  FuncInfo.CanLowerReturn = CLI->checkReturnTypeForCallConv(*MF);

  SL = std::make_unique<GISelSwitchLowering>(this, FuncInfo);
  SL->init(*TLI, TM, *DL);

  assert(PendingPHIs.empty() && "stale PHIs");

  // Targets which want to use big endian can enable it using
  // enableBigEndian()
  if (!DL->isLittleEndian() && !CLI->enableBigEndian()) {
    // Currently we don't properly handle big endian code.
    OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                               F.getSubprogram(), &F.getEntryBlock());
    R << "unable to translate in big endian mode";
    reportTranslationError(*MF, *TPC, *ORE, R);
    return false;
  }

  // Release the per-function state when we return, whether we succeeded or not.
  auto FinalizeOnReturn = make_scope_exit([this]() { finalizeFunction(); });

  // Setup a separate basic-block for the arguments and constants
  MachineBasicBlock *EntryBB = MF->CreateMachineBasicBlock();
  MF->push_back(EntryBB);
  EntryBuilder->setMBB(*EntryBB);

  DebugLoc DbgLoc = F.getEntryBlock().getFirstNonPHI()->getDebugLoc();
  SwiftError.setFunction(CurMF);
  SwiftError.createEntriesInEntryBlock(DbgLoc);

  bool IsVarArg = F.isVarArg();
  bool HasMustTailInVarArgFn = false;

  // Create all blocks, in IR order, to preserve the layout.
  for (const BasicBlock &BB: F) {
    auto *&MBB = BBToMBB[&BB];

    MBB = MF->CreateMachineBasicBlock(&BB);
    MF->push_back(MBB);

    if (BB.hasAddressTaken())
      MBB->setAddressTakenIRBlock(const_cast<BasicBlock *>(&BB));

    if (!HasMustTailInVarArgFn)
      HasMustTailInVarArgFn = checkForMustTailInVarArgFn(IsVarArg, BB);
  }

  MF->getFrameInfo().setHasMustTailInVarArgFunc(HasMustTailInVarArgFn);

  // Make our arguments/constants entry block fallthrough to the IR entry block.
  EntryBB->addSuccessor(&getMBB(F.front()));

  if (CLI->fallBackToDAGISel(*MF)) {
    OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                               F.getSubprogram(), &F.getEntryBlock());
    R << "unable to lower function: " << ore::NV("Prototype", F.getType());
    reportTranslationError(*MF, *TPC, *ORE, R);
    return false;
  }

  // Lower the actual args into this basic block.
  SmallVector<ArrayRef<Register>, 8> VRegArgs;
  for (const Argument &Arg: F.args()) {
    if (DL->getTypeStoreSize(Arg.getType()).isZero())
      continue; // Don't handle zero sized types.
    ArrayRef<Register> VRegs = getOrCreateVRegs(Arg);
    VRegArgs.push_back(VRegs);

    if (Arg.hasSwiftErrorAttr()) {
      assert(VRegs.size() == 1 && "Too many vregs for Swift error");
      SwiftError.setCurrentVReg(EntryBB, SwiftError.getFunctionArg(), VRegs[0]);
    }
  }

  if (!CLI->lowerFormalArguments(*EntryBuilder, F, VRegArgs, FuncInfo)) {
    OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                               F.getSubprogram(), &F.getEntryBlock());
    R << "unable to lower arguments: " << ore::NV("Prototype", F.getType());
    reportTranslationError(*MF, *TPC, *ORE, R);
    return false;
  }

  // Need to visit defs before uses when translating instructions.
  GISelObserverWrapper WrapperObserver;
  if (EnableCSE && CSEInfo)
    WrapperObserver.addObserver(CSEInfo);
  {
    ReversePostOrderTraversal<const Function *> RPOT(&F);
#ifndef NDEBUG
    DILocationVerifier Verifier;
    WrapperObserver.addObserver(&Verifier);
#endif // ifndef NDEBUG
    RAIIDelegateInstaller DelInstall(*MF, &WrapperObserver);
    RAIIMFObserverInstaller ObsInstall(*MF, WrapperObserver);
    for (const BasicBlock *BB : RPOT) {
      MachineBasicBlock &MBB = getMBB(*BB);
      // Set the insertion point of all the following translations to
      // the end of this basic block.
      CurBuilder->setMBB(MBB);
      HasTailCall = false;
      for (const Instruction &Inst : *BB) {
        // If we translated a tail call in the last step, then we know
        // everything after the call is either a return, or something that is
        // handled by the call itself. (E.g. a lifetime marker or assume
        // intrinsic.) In this case, we should stop translating the block and
        // move on.
        if (HasTailCall)
          break;
#ifndef NDEBUG
        Verifier.setCurrentInst(&Inst);
#endif // ifndef NDEBUG

        // Translate any debug-info attached to the instruction.
        translateDbgInfo(Inst, *CurBuilder);

        if (translate(Inst))
          continue;

        OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                   Inst.getDebugLoc(), BB);
        R << "unable to translate instruction: " << ore::NV("Opcode", &Inst);

        if (ORE->allowExtraAnalysis("gisel-irtranslator")) {
          std::string InstStrStorage;
          raw_string_ostream InstStr(InstStrStorage);
          InstStr << Inst;

          R << ": '" << InstStrStorage << "'";
        }

        reportTranslationError(*MF, *TPC, *ORE, R);
        return false;
      }

      if (!finalizeBasicBlock(*BB, MBB)) {
        OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                   BB->getTerminator()->getDebugLoc(), BB);
        R << "unable to translate basic block";
        reportTranslationError(*MF, *TPC, *ORE, R);
        return false;
      }
    }
#ifndef NDEBUG
    WrapperObserver.removeObserver(&Verifier);
#endif
  }

  finishPendingPhis();

  SwiftError.propagateVRegs();

  // Merge the argument lowering and constants block with its single
  // successor, the LLVM-IR entry block.  We want the basic block to
  // be maximal.
  assert(EntryBB->succ_size() == 1 &&
         "Custom BB used for lowering should have only one successor");
  // Get the successor of the current entry block.
  MachineBasicBlock &NewEntryBB = **EntryBB->succ_begin();
  assert(NewEntryBB.pred_size() == 1 &&
         "LLVM-IR entry block has a predecessor!?");
  // Move all the instruction from the current entry block to the
  // new entry block.
  NewEntryBB.splice(NewEntryBB.begin(), EntryBB, EntryBB->begin(),
                    EntryBB->end());

  // Update the live-in information for the new entry block.
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : EntryBB->liveins())
    NewEntryBB.addLiveIn(LiveIn);
  NewEntryBB.sortUniqueLiveIns();

  // Get rid of the now empty basic block.
  EntryBB->removeSuccessor(&NewEntryBB);
  MF->remove(EntryBB);
  MF->deleteMachineBasicBlock(EntryBB);

  assert(&MF->front() == &NewEntryBB &&
         "New entry wasn't next in the list of basic block!");

  // Initialize stack protector information.
  StackProtector &SP = getAnalysis<StackProtector>();
  SP.copyToMachineFrameInfo(MF->getFrameInfo());

  return false;
}
