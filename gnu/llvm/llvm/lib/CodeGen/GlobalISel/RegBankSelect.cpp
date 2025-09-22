//==- llvm/CodeGen/GlobalISel/RegBankSelect.cpp - RegBankSelect --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the RegBankSelect class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#define DEBUG_TYPE "regbankselect"

using namespace llvm;

static cl::opt<RegBankSelect::Mode> RegBankSelectMode(
    cl::desc("Mode of the RegBankSelect pass"), cl::Hidden, cl::Optional,
    cl::values(clEnumValN(RegBankSelect::Mode::Fast, "regbankselect-fast",
                          "Run the Fast mode (default mapping)"),
               clEnumValN(RegBankSelect::Mode::Greedy, "regbankselect-greedy",
                          "Use the Greedy mode (best local mapping)")));

char RegBankSelect::ID = 0;

INITIALIZE_PASS_BEGIN(RegBankSelect, DEBUG_TYPE,
                      "Assign register bank of generic virtual registers",
                      false, false);
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(RegBankSelect, DEBUG_TYPE,
                    "Assign register bank of generic virtual registers", false,
                    false)

RegBankSelect::RegBankSelect(char &PassID, Mode RunningMode)
    : MachineFunctionPass(PassID), OptMode(RunningMode) {
  if (RegBankSelectMode.getNumOccurrences() != 0) {
    OptMode = RegBankSelectMode;
    if (RegBankSelectMode != RunningMode)
      LLVM_DEBUG(dbgs() << "RegBankSelect mode overrided by command line\n");
  }
}

void RegBankSelect::init(MachineFunction &MF) {
  RBI = MF.getSubtarget().getRegBankInfo();
  assert(RBI && "Cannot work without RegisterBankInfo");
  MRI = &MF.getRegInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  TPC = &getAnalysis<TargetPassConfig>();
  if (OptMode != Mode::Fast) {
    MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
    MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  } else {
    MBFI = nullptr;
    MBPI = nullptr;
  }
  MIRBuilder.setMF(MF);
  MORE = std::make_unique<MachineOptimizationRemarkEmitter>(MF, MBFI);
}

void RegBankSelect::getAnalysisUsage(AnalysisUsage &AU) const {
  if (OptMode != Mode::Fast) {
    // We could preserve the information from these two analysis but
    // the APIs do not allow to do so yet.
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
  }
  AU.addRequired<TargetPassConfig>();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool RegBankSelect::assignmentMatch(
    Register Reg, const RegisterBankInfo::ValueMapping &ValMapping,
    bool &OnlyAssign) const {
  // By default we assume we will have to repair something.
  OnlyAssign = false;
  // Each part of a break down needs to end up in a different register.
  // In other word, Reg assignment does not match.
  if (ValMapping.NumBreakDowns != 1)
    return false;

  const RegisterBank *CurRegBank = RBI->getRegBank(Reg, *MRI, *TRI);
  const RegisterBank *DesiredRegBank = ValMapping.BreakDown[0].RegBank;
  // Reg is free of assignment, a simple assignment will make the
  // register bank to match.
  OnlyAssign = CurRegBank == nullptr;
  LLVM_DEBUG(dbgs() << "Does assignment already match: ";
             if (CurRegBank) dbgs() << *CurRegBank; else dbgs() << "none";
             dbgs() << " against ";
             assert(DesiredRegBank && "The mapping must be valid");
             dbgs() << *DesiredRegBank << '\n';);
  return CurRegBank == DesiredRegBank;
}

bool RegBankSelect::repairReg(
    MachineOperand &MO, const RegisterBankInfo::ValueMapping &ValMapping,
    RegBankSelect::RepairingPlacement &RepairPt,
    const iterator_range<SmallVectorImpl<Register>::const_iterator> &NewVRegs) {

  assert(ValMapping.NumBreakDowns == (unsigned)size(NewVRegs) &&
         "need new vreg for each breakdown");

  // An empty range of new register means no repairing.
  assert(!NewVRegs.empty() && "We should not have to repair");

  MachineInstr *MI;
  if (ValMapping.NumBreakDowns == 1) {
    // Assume we are repairing a use and thus, the original reg will be
    // the source of the repairing.
    Register Src = MO.getReg();
    Register Dst = *NewVRegs.begin();

    // If we repair a definition, swap the source and destination for
    // the repairing.
    if (MO.isDef())
      std::swap(Src, Dst);

    assert((RepairPt.getNumInsertPoints() == 1 || Dst.isPhysical()) &&
           "We are about to create several defs for Dst");

    // Build the instruction used to repair, then clone it at the right
    // places. Avoiding buildCopy bypasses the check that Src and Dst have the
    // same types because the type is a placeholder when this function is called.
    MI = MIRBuilder.buildInstrNoInsert(TargetOpcode::COPY)
      .addDef(Dst)
      .addUse(Src);
    LLVM_DEBUG(dbgs() << "Copy: " << printReg(Src) << ':'
                      << printRegClassOrBank(Src, *MRI, TRI)
                      << " to: " << printReg(Dst) << ':'
                      << printRegClassOrBank(Dst, *MRI, TRI) << '\n');
  } else {
    // TODO: Support with G_IMPLICIT_DEF + G_INSERT sequence or G_EXTRACT
    // sequence.
    assert(ValMapping.partsAllUniform() && "irregular breakdowns not supported");

    LLT RegTy = MRI->getType(MO.getReg());
    if (MO.isDef()) {
      unsigned MergeOp;
      if (RegTy.isVector()) {
        if (ValMapping.NumBreakDowns == RegTy.getNumElements())
          MergeOp = TargetOpcode::G_BUILD_VECTOR;
        else {
          assert(
              (ValMapping.BreakDown[0].Length * ValMapping.NumBreakDowns ==
               RegTy.getSizeInBits()) &&
              (ValMapping.BreakDown[0].Length % RegTy.getScalarSizeInBits() ==
               0) &&
              "don't understand this value breakdown");

          MergeOp = TargetOpcode::G_CONCAT_VECTORS;
        }
      } else
        MergeOp = TargetOpcode::G_MERGE_VALUES;

      auto MergeBuilder =
        MIRBuilder.buildInstrNoInsert(MergeOp)
        .addDef(MO.getReg());

      for (Register SrcReg : NewVRegs)
        MergeBuilder.addUse(SrcReg);

      MI = MergeBuilder;
    } else {
      MachineInstrBuilder UnMergeBuilder =
        MIRBuilder.buildInstrNoInsert(TargetOpcode::G_UNMERGE_VALUES);
      for (Register DefReg : NewVRegs)
        UnMergeBuilder.addDef(DefReg);

      UnMergeBuilder.addUse(MO.getReg());
      MI = UnMergeBuilder;
    }
  }

  if (RepairPt.getNumInsertPoints() != 1)
    report_fatal_error("need testcase to support multiple insertion points");

  // TODO:
  // Check if MI is legal. if not, we need to legalize all the
  // instructions we are going to insert.
  std::unique_ptr<MachineInstr *[]> NewInstrs(
      new MachineInstr *[RepairPt.getNumInsertPoints()]);
  bool IsFirst = true;
  unsigned Idx = 0;
  for (const std::unique_ptr<InsertPoint> &InsertPt : RepairPt) {
    MachineInstr *CurMI;
    if (IsFirst)
      CurMI = MI;
    else
      CurMI = MIRBuilder.getMF().CloneMachineInstr(MI);
    InsertPt->insert(*CurMI);
    NewInstrs[Idx++] = CurMI;
    IsFirst = false;
  }
  // TODO:
  // Legalize NewInstrs if need be.
  return true;
}

uint64_t RegBankSelect::getRepairCost(
    const MachineOperand &MO,
    const RegisterBankInfo::ValueMapping &ValMapping) const {
  assert(MO.isReg() && "We should only repair register operand");
  assert(ValMapping.NumBreakDowns && "Nothing to map??");

  bool IsSameNumOfValues = ValMapping.NumBreakDowns == 1;
  const RegisterBank *CurRegBank = RBI->getRegBank(MO.getReg(), *MRI, *TRI);
  // If MO does not have a register bank, we should have just been
  // able to set one unless we have to break the value down.
  assert(CurRegBank || MO.isDef());

  // Def: Val <- NewDefs
  //     Same number of values: copy
  //     Different number: Val = build_sequence Defs1, Defs2, ...
  // Use: NewSources <- Val.
  //     Same number of values: copy.
  //     Different number: Src1, Src2, ... =
  //           extract_value Val, Src1Begin, Src1Len, Src2Begin, Src2Len, ...
  // We should remember that this value is available somewhere else to
  // coalesce the value.

  if (ValMapping.NumBreakDowns != 1)
    return RBI->getBreakDownCost(ValMapping, CurRegBank);

  if (IsSameNumOfValues) {
    const RegisterBank *DesiredRegBank = ValMapping.BreakDown[0].RegBank;
    // If we repair a definition, swap the source and destination for
    // the repairing.
    if (MO.isDef())
      std::swap(CurRegBank, DesiredRegBank);
    // TODO: It may be possible to actually avoid the copy.
    // If we repair something where the source is defined by a copy
    // and the source of that copy is on the right bank, we can reuse
    // it for free.
    // E.g.,
    // RegToRepair<BankA> = copy AlternativeSrc<BankB>
    // = op RegToRepair<BankA>
    // We can simply propagate AlternativeSrc instead of copying RegToRepair
    // into a new virtual register.
    // We would also need to propagate this information in the
    // repairing placement.
    unsigned Cost = RBI->copyCost(*DesiredRegBank, *CurRegBank,
                                  RBI->getSizeInBits(MO.getReg(), *MRI, *TRI));
    // TODO: use a dedicated constant for ImpossibleCost.
    if (Cost != std::numeric_limits<unsigned>::max())
      return Cost;
    // Return the legalization cost of that repairing.
  }
  return std::numeric_limits<unsigned>::max();
}

const RegisterBankInfo::InstructionMapping &RegBankSelect::findBestMapping(
    MachineInstr &MI, RegisterBankInfo::InstructionMappings &PossibleMappings,
    SmallVectorImpl<RepairingPlacement> &RepairPts) {
  assert(!PossibleMappings.empty() &&
         "Do not know how to map this instruction");

  const RegisterBankInfo::InstructionMapping *BestMapping = nullptr;
  MappingCost Cost = MappingCost::ImpossibleCost();
  SmallVector<RepairingPlacement, 4> LocalRepairPts;
  for (const RegisterBankInfo::InstructionMapping *CurMapping :
       PossibleMappings) {
    MappingCost CurCost =
        computeMapping(MI, *CurMapping, LocalRepairPts, &Cost);
    if (CurCost < Cost) {
      LLVM_DEBUG(dbgs() << "New best: " << CurCost << '\n');
      Cost = CurCost;
      BestMapping = CurMapping;
      RepairPts.clear();
      for (RepairingPlacement &RepairPt : LocalRepairPts)
        RepairPts.emplace_back(std::move(RepairPt));
    }
  }
  if (!BestMapping && !TPC->isGlobalISelAbortEnabled()) {
    // If none of the mapping worked that means they are all impossible.
    // Thus, pick the first one and set an impossible repairing point.
    // It will trigger the failed isel mode.
    BestMapping = *PossibleMappings.begin();
    RepairPts.emplace_back(
        RepairingPlacement(MI, 0, *TRI, *this, RepairingPlacement::Impossible));
  } else
    assert(BestMapping && "No suitable mapping for instruction");
  return *BestMapping;
}

void RegBankSelect::tryAvoidingSplit(
    RegBankSelect::RepairingPlacement &RepairPt, const MachineOperand &MO,
    const RegisterBankInfo::ValueMapping &ValMapping) const {
  const MachineInstr &MI = *MO.getParent();
  assert(RepairPt.hasSplit() && "We should not have to adjust for split");
  // Splitting should only occur for PHIs or between terminators,
  // because we only do local repairing.
  assert((MI.isPHI() || MI.isTerminator()) && "Why do we split?");

  assert(&MI.getOperand(RepairPt.getOpIdx()) == &MO &&
         "Repairing placement does not match operand");

  // If we need splitting for phis, that means it is because we
  // could not find an insertion point before the terminators of
  // the predecessor block for this argument. In other words,
  // the input value is defined by one of the terminators.
  assert((!MI.isPHI() || !MO.isDef()) && "Need split for phi def?");

  // We split to repair the use of a phi or a terminator.
  if (!MO.isDef()) {
    if (MI.isTerminator()) {
      assert(&MI != &(*MI.getParent()->getFirstTerminator()) &&
             "Need to split for the first terminator?!");
    } else {
      // For the PHI case, the split may not be actually required.
      // In the copy case, a phi is already a copy on the incoming edge,
      // therefore there is no need to split.
      if (ValMapping.NumBreakDowns == 1)
        // This is a already a copy, there is nothing to do.
        RepairPt.switchTo(RepairingPlacement::RepairingKind::Reassign);
    }
    return;
  }

  // At this point, we need to repair a defintion of a terminator.

  // Technically we need to fix the def of MI on all outgoing
  // edges of MI to keep the repairing local. In other words, we
  // will create several definitions of the same register. This
  // does not work for SSA unless that definition is a physical
  // register.
  // However, there are other cases where we can get away with
  // that while still keeping the repairing local.
  assert(MI.isTerminator() && MO.isDef() &&
         "This code is for the def of a terminator");

  // Since we use RPO traversal, if we need to repair a definition
  // this means this definition could be:
  // 1. Used by PHIs (i.e., this VReg has been visited as part of the
  //    uses of a phi.), or
  // 2. Part of a target specific instruction (i.e., the target applied
  //    some register class constraints when creating the instruction.)
  // If the constraints come for #2, the target said that another mapping
  // is supported so we may just drop them. Indeed, if we do not change
  // the number of registers holding that value, the uses will get fixed
  // when we get to them.
  // Uses in PHIs may have already been proceeded though.
  // If the constraints come for #1, then, those are weak constraints and
  // no actual uses may rely on them. However, the problem remains mainly
  // the same as for #2. If the value stays in one register, we could
  // just switch the register bank of the definition, but we would need to
  // account for a repairing cost for each phi we silently change.
  //
  // In any case, if the value needs to be broken down into several
  // registers, the repairing is not local anymore as we need to patch
  // every uses to rebuild the value in just one register.
  //
  // To summarize:
  // - If the value is in a physical register, we can do the split and
  //   fix locally.
  // Otherwise if the value is in a virtual register:
  // - If the value remains in one register, we do not have to split
  //   just switching the register bank would do, but we need to account
  //   in the repairing cost all the phi we changed.
  // - If the value spans several registers, then we cannot do a local
  //   repairing.

  // Check if this is a physical or virtual register.
  Register Reg = MO.getReg();
  if (Reg.isPhysical()) {
    // We are going to split every outgoing edges.
    // Check that this is possible.
    // FIXME: The machine representation is currently broken
    // since it also several terminators in one basic block.
    // Because of that we would technically need a way to get
    // the targets of just one terminator to know which edges
    // we have to split.
    // Assert that we do not hit the ill-formed representation.

    // If there are other terminators before that one, some of
    // the outgoing edges may not be dominated by this definition.
    assert(&MI == &(*MI.getParent()->getFirstTerminator()) &&
           "Do not know which outgoing edges are relevant");
    const MachineInstr *Next = MI.getNextNode();
    assert((!Next || Next->isUnconditionalBranch()) &&
           "Do not know where each terminator ends up");
    if (Next)
      // If the next terminator uses Reg, this means we have
      // to split right after MI and thus we need a way to ask
      // which outgoing edges are affected.
      assert(!Next->readsRegister(Reg, /*TRI=*/nullptr) &&
             "Need to split between terminators");
    // We will split all the edges and repair there.
  } else {
    // This is a virtual register defined by a terminator.
    if (ValMapping.NumBreakDowns == 1) {
      // There is nothing to repair, but we may actually lie on
      // the repairing cost because of the PHIs already proceeded
      // as already stated.
      // Though the code will be correct.
      assert(false && "Repairing cost may not be accurate");
    } else {
      // We need to do non-local repairing. Basically, patch all
      // the uses (i.e., phis) that we already proceeded.
      // For now, just say this mapping is not possible.
      RepairPt.switchTo(RepairingPlacement::RepairingKind::Impossible);
    }
  }
}

RegBankSelect::MappingCost RegBankSelect::computeMapping(
    MachineInstr &MI, const RegisterBankInfo::InstructionMapping &InstrMapping,
    SmallVectorImpl<RepairingPlacement> &RepairPts,
    const RegBankSelect::MappingCost *BestCost) {
  assert((MBFI || !BestCost) && "Costs comparison require MBFI");

  if (!InstrMapping.isValid())
    return MappingCost::ImpossibleCost();

  // If mapped with InstrMapping, MI will have the recorded cost.
  MappingCost Cost(MBFI ? MBFI->getBlockFreq(MI.getParent())
                        : BlockFrequency(1));
  bool Saturated = Cost.addLocalCost(InstrMapping.getCost());
  assert(!Saturated && "Possible mapping saturated the cost");
  LLVM_DEBUG(dbgs() << "Evaluating mapping cost for: " << MI);
  LLVM_DEBUG(dbgs() << "With: " << InstrMapping << '\n');
  RepairPts.clear();
  if (BestCost && Cost > *BestCost) {
    LLVM_DEBUG(dbgs() << "Mapping is too expensive from the start\n");
    return Cost;
  }
  const MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();

  // Moreover, to realize this mapping, the register bank of each operand must
  // match this mapping. In other words, we may need to locally reassign the
  // register banks. Account for that repairing cost as well.
  // In this context, local means in the surrounding of MI.
  for (unsigned OpIdx = 0, EndOpIdx = InstrMapping.getNumOperands();
       OpIdx != EndOpIdx; ++OpIdx) {
    const MachineOperand &MO = MI.getOperand(OpIdx);
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    LLT Ty = MRI.getType(Reg);
    if (!Ty.isValid())
      continue;

    LLVM_DEBUG(dbgs() << "Opd" << OpIdx << '\n');
    const RegisterBankInfo::ValueMapping &ValMapping =
        InstrMapping.getOperandMapping(OpIdx);
    // If Reg is already properly mapped, this is free.
    bool Assign;
    if (assignmentMatch(Reg, ValMapping, Assign)) {
      LLVM_DEBUG(dbgs() << "=> is free (match).\n");
      continue;
    }
    if (Assign) {
      LLVM_DEBUG(dbgs() << "=> is free (simple assignment).\n");
      RepairPts.emplace_back(RepairingPlacement(MI, OpIdx, *TRI, *this,
                                                RepairingPlacement::Reassign));
      continue;
    }

    // Find the insertion point for the repairing code.
    RepairPts.emplace_back(
        RepairingPlacement(MI, OpIdx, *TRI, *this, RepairingPlacement::Insert));
    RepairingPlacement &RepairPt = RepairPts.back();

    // If we need to split a basic block to materialize this insertion point,
    // we may give a higher cost to this mapping.
    // Nevertheless, we may get away with the split, so try that first.
    if (RepairPt.hasSplit())
      tryAvoidingSplit(RepairPt, MO, ValMapping);

    // Check that the materialization of the repairing is possible.
    if (!RepairPt.canMaterialize()) {
      LLVM_DEBUG(dbgs() << "Mapping involves impossible repairing\n");
      return MappingCost::ImpossibleCost();
    }

    // Account for the split cost and repair cost.
    // Unless the cost is already saturated or we do not care about the cost.
    if (!BestCost || Saturated)
      continue;

    // To get accurate information we need MBFI and MBPI.
    // Thus, if we end up here this information should be here.
    assert(MBFI && MBPI && "Cost computation requires MBFI and MBPI");

    // FIXME: We will have to rework the repairing cost model.
    // The repairing cost depends on the register bank that MO has.
    // However, when we break down the value into different values,
    // MO may not have a register bank while still needing repairing.
    // For the fast mode, we don't compute the cost so that is fine,
    // but still for the repairing code, we will have to make a choice.
    // For the greedy mode, we should choose greedily what is the best
    // choice based on the next use of MO.

    // Sums up the repairing cost of MO at each insertion point.
    uint64_t RepairCost = getRepairCost(MO, ValMapping);

    // This is an impossible to repair cost.
    if (RepairCost == std::numeric_limits<unsigned>::max())
      return MappingCost::ImpossibleCost();

    // Bias used for splitting: 5%.
    const uint64_t PercentageForBias = 5;
    uint64_t Bias = (RepairCost * PercentageForBias + 99) / 100;
    // We should not need more than a couple of instructions to repair
    // an assignment. In other words, the computation should not
    // overflow because the repairing cost is free of basic block
    // frequency.
    assert(((RepairCost < RepairCost * PercentageForBias) &&
            (RepairCost * PercentageForBias <
             RepairCost * PercentageForBias + 99)) &&
           "Repairing involves more than a billion of instructions?!");
    for (const std::unique_ptr<InsertPoint> &InsertPt : RepairPt) {
      assert(InsertPt->canMaterialize() && "We should not have made it here");
      // We will applied some basic block frequency and those uses uint64_t.
      if (!InsertPt->isSplit())
        Saturated = Cost.addLocalCost(RepairCost);
      else {
        uint64_t CostForInsertPt = RepairCost;
        // Again we shouldn't overflow here givent that
        // CostForInsertPt is frequency free at this point.
        assert(CostForInsertPt + Bias > CostForInsertPt &&
               "Repairing + split bias overflows");
        CostForInsertPt += Bias;
        uint64_t PtCost = InsertPt->frequency(*this) * CostForInsertPt;
        // Check if we just overflowed.
        if ((Saturated = PtCost < CostForInsertPt))
          Cost.saturate();
        else
          Saturated = Cost.addNonLocalCost(PtCost);
      }

      // Stop looking into what it takes to repair, this is already
      // too expensive.
      if (BestCost && Cost > *BestCost) {
        LLVM_DEBUG(dbgs() << "Mapping is too expensive, stop processing\n");
        return Cost;
      }

      // No need to accumulate more cost information.
      // We need to still gather the repairing information though.
      if (Saturated)
        break;
    }
  }
  LLVM_DEBUG(dbgs() << "Total cost is: " << Cost << "\n");
  return Cost;
}

bool RegBankSelect::applyMapping(
    MachineInstr &MI, const RegisterBankInfo::InstructionMapping &InstrMapping,
    SmallVectorImpl<RegBankSelect::RepairingPlacement> &RepairPts) {
  // OpdMapper will hold all the information needed for the rewriting.
  RegisterBankInfo::OperandsMapper OpdMapper(MI, InstrMapping, *MRI);

  // First, place the repairing code.
  for (RepairingPlacement &RepairPt : RepairPts) {
    if (!RepairPt.canMaterialize() ||
        RepairPt.getKind() == RepairingPlacement::Impossible)
      return false;
    assert(RepairPt.getKind() != RepairingPlacement::None &&
           "This should not make its way in the list");
    unsigned OpIdx = RepairPt.getOpIdx();
    MachineOperand &MO = MI.getOperand(OpIdx);
    const RegisterBankInfo::ValueMapping &ValMapping =
        InstrMapping.getOperandMapping(OpIdx);
    Register Reg = MO.getReg();

    switch (RepairPt.getKind()) {
    case RepairingPlacement::Reassign:
      assert(ValMapping.NumBreakDowns == 1 &&
             "Reassignment should only be for simple mapping");
      MRI->setRegBank(Reg, *ValMapping.BreakDown[0].RegBank);
      break;
    case RepairingPlacement::Insert:
      // Don't insert additional instruction for debug instruction.
      if (MI.isDebugInstr())
        break;
      OpdMapper.createVRegs(OpIdx);
      if (!repairReg(MO, ValMapping, RepairPt, OpdMapper.getVRegs(OpIdx)))
        return false;
      break;
    default:
      llvm_unreachable("Other kind should not happen");
    }
  }

  // Second, rewrite the instruction.
  LLVM_DEBUG(dbgs() << "Actual mapping of the operands: " << OpdMapper << '\n');
  RBI->applyMapping(MIRBuilder, OpdMapper);

  return true;
}

bool RegBankSelect::assignInstr(MachineInstr &MI) {
  LLVM_DEBUG(dbgs() << "Assign: " << MI);

  unsigned Opc = MI.getOpcode();
  if (isPreISelGenericOptimizationHint(Opc)) {
    assert((Opc == TargetOpcode::G_ASSERT_ZEXT ||
            Opc == TargetOpcode::G_ASSERT_SEXT ||
            Opc == TargetOpcode::G_ASSERT_ALIGN) &&
           "Unexpected hint opcode!");
    // The only correct mapping for these is to always use the source register
    // bank.
    const RegisterBank *RB =
        RBI->getRegBank(MI.getOperand(1).getReg(), *MRI, *TRI);
    // We can assume every instruction above this one has a selected register
    // bank.
    assert(RB && "Expected source register to have a register bank?");
    LLVM_DEBUG(dbgs() << "... Hint always uses source's register bank.\n");
    MRI->setRegBank(MI.getOperand(0).getReg(), *RB);
    return true;
  }

  // Remember the repairing placement for all the operands.
  SmallVector<RepairingPlacement, 4> RepairPts;

  const RegisterBankInfo::InstructionMapping *BestMapping;
  if (OptMode == RegBankSelect::Mode::Fast) {
    BestMapping = &RBI->getInstrMapping(MI);
    MappingCost DefaultCost = computeMapping(MI, *BestMapping, RepairPts);
    (void)DefaultCost;
    if (DefaultCost == MappingCost::ImpossibleCost())
      return false;
  } else {
    RegisterBankInfo::InstructionMappings PossibleMappings =
        RBI->getInstrPossibleMappings(MI);
    if (PossibleMappings.empty())
      return false;
    BestMapping = &findBestMapping(MI, PossibleMappings, RepairPts);
  }
  // Make sure the mapping is valid for MI.
  assert(BestMapping->verify(MI) && "Invalid instruction mapping");

  LLVM_DEBUG(dbgs() << "Best Mapping: " << *BestMapping << '\n');

  // After this call, MI may not be valid anymore.
  // Do not use it.
  return applyMapping(MI, *BestMapping, RepairPts);
}

bool RegBankSelect::assignRegisterBanks(MachineFunction &MF) {
  // Walk the function and assign register banks to all operands.
  // Use a RPOT to make sure all registers are assigned before we choose
  // the best mapping of the current instruction.
  ReversePostOrderTraversal<MachineFunction*> RPOT(&MF);
  for (MachineBasicBlock *MBB : RPOT) {
    // Set a sensible insertion point so that subsequent calls to
    // MIRBuilder.
    MIRBuilder.setMBB(*MBB);
    SmallVector<MachineInstr *> WorkList(
        make_pointer_range(reverse(MBB->instrs())));

    while (!WorkList.empty()) {
      MachineInstr &MI = *WorkList.pop_back_val();

      // Ignore target-specific post-isel instructions: they should use proper
      // regclasses.
      if (isTargetSpecificOpcode(MI.getOpcode()) && !MI.isPreISelOpcode())
        continue;

      // Ignore inline asm instructions: they should use physical
      // registers/regclasses
      if (MI.isInlineAsm())
        continue;

      // Ignore IMPLICIT_DEF which must have a regclass.
      if (MI.isImplicitDef())
        continue;

      if (!assignInstr(MI)) {
        reportGISelFailure(MF, *TPC, *MORE, "gisel-regbankselect",
                           "unable to map instruction", MI);
        return false;
      }
    }
  }

  return true;
}

bool RegBankSelect::checkFunctionIsLegal(MachineFunction &MF) const {
#ifndef NDEBUG
  if (!DisableGISelLegalityCheck) {
    if (const MachineInstr *MI = machineFunctionIsIllegal(MF)) {
      reportGISelFailure(MF, *TPC, *MORE, "gisel-regbankselect",
                         "instruction is not legal", *MI);
      return false;
    }
  }
#endif
  return true;
}

bool RegBankSelect::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Assign register banks for: " << MF.getName() << '\n');
  const Function &F = MF.getFunction();
  Mode SaveOptMode = OptMode;
  if (F.hasOptNone())
    OptMode = Mode::Fast;
  init(MF);

#ifndef NDEBUG
  if (!checkFunctionIsLegal(MF))
    return false;
#endif

  assignRegisterBanks(MF);

  OptMode = SaveOptMode;
  return false;
}

//------------------------------------------------------------------------------
//                  Helper Classes Implementation
//------------------------------------------------------------------------------
RegBankSelect::RepairingPlacement::RepairingPlacement(
    MachineInstr &MI, unsigned OpIdx, const TargetRegisterInfo &TRI, Pass &P,
    RepairingPlacement::RepairingKind Kind)
    // Default is, we are going to insert code to repair OpIdx.
    : Kind(Kind), OpIdx(OpIdx),
      CanMaterialize(Kind != RepairingKind::Impossible), P(P) {
  const MachineOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isReg() && "Trying to repair a non-reg operand");

  if (Kind != RepairingKind::Insert)
    return;

  // Repairings for definitions happen after MI, uses happen before.
  bool Before = !MO.isDef();

  // Check if we are done with MI.
  if (!MI.isPHI() && !MI.isTerminator()) {
    addInsertPoint(MI, Before);
    // We are done with the initialization.
    return;
  }

  // Now, look for the special cases.
  if (MI.isPHI()) {
    // - PHI must be the first instructions:
    //   * Before, we have to split the related incoming edge.
    //   * After, move the insertion point past the last phi.
    if (!Before) {
      MachineBasicBlock::iterator It = MI.getParent()->getFirstNonPHI();
      if (It != MI.getParent()->end())
        addInsertPoint(*It, /*Before*/ true);
      else
        addInsertPoint(*(--It), /*Before*/ false);
      return;
    }
    // We repair a use of a phi, we may need to split the related edge.
    MachineBasicBlock &Pred = *MI.getOperand(OpIdx + 1).getMBB();
    // Check if we can move the insertion point prior to the
    // terminators of the predecessor.
    Register Reg = MO.getReg();
    MachineBasicBlock::iterator It = Pred.getLastNonDebugInstr();
    for (auto Begin = Pred.begin(); It != Begin && It->isTerminator(); --It)
      if (It->modifiesRegister(Reg, &TRI)) {
        // We cannot hoist the repairing code in the predecessor.
        // Split the edge.
        addInsertPoint(Pred, *MI.getParent());
        return;
      }
    // At this point, we can insert in Pred.

    // - If It is invalid, Pred is empty and we can insert in Pred
    //   wherever we want.
    // - If It is valid, It is the first non-terminator, insert after It.
    if (It == Pred.end())
      addInsertPoint(Pred, /*Beginning*/ false);
    else
      addInsertPoint(*It, /*Before*/ false);
  } else {
    // - Terminators must be the last instructions:
    //   * Before, move the insert point before the first terminator.
    //   * After, we have to split the outcoming edges.
    if (Before) {
      // Check whether Reg is defined by any terminator.
      MachineBasicBlock::reverse_iterator It = MI;
      auto REnd = MI.getParent()->rend();

      for (; It != REnd && It->isTerminator(); ++It) {
        assert(!It->modifiesRegister(MO.getReg(), &TRI) &&
               "copy insertion in middle of terminators not handled");
      }

      if (It == REnd) {
        addInsertPoint(*MI.getParent()->begin(), true);
        return;
      }

      // We are sure to be right before the first terminator.
      addInsertPoint(*It, /*Before*/ false);
      return;
    }
    // Make sure Reg is not redefined by other terminators, otherwise
    // we do not know how to split.
    for (MachineBasicBlock::iterator It = MI, End = MI.getParent()->end();
         ++It != End;)
      // The machine verifier should reject this kind of code.
      assert(It->modifiesRegister(MO.getReg(), &TRI) &&
             "Do not know where to split");
    // Split each outcoming edges.
    MachineBasicBlock &Src = *MI.getParent();
    for (auto &Succ : Src.successors())
      addInsertPoint(Src, Succ);
  }
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineInstr &MI,
                                                       bool Before) {
  addInsertPoint(*new InstrInsertPoint(MI, Before));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineBasicBlock &MBB,
                                                       bool Beginning) {
  addInsertPoint(*new MBBInsertPoint(MBB, Beginning));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineBasicBlock &Src,
                                                       MachineBasicBlock &Dst) {
  addInsertPoint(*new EdgeInsertPoint(Src, Dst, P));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(
    RegBankSelect::InsertPoint &Point) {
  CanMaterialize &= Point.canMaterialize();
  HasSplit |= Point.isSplit();
  InsertPoints.emplace_back(&Point);
}

RegBankSelect::InstrInsertPoint::InstrInsertPoint(MachineInstr &Instr,
                                                  bool Before)
    : Instr(Instr), Before(Before) {
  // Since we do not support splitting, we do not need to update
  // liveness and such, so do not do anything with P.
  assert((!Before || !Instr.isPHI()) &&
         "Splitting before phis requires more points");
  assert((!Before || !Instr.getNextNode() || !Instr.getNextNode()->isPHI()) &&
         "Splitting between phis does not make sense");
}

void RegBankSelect::InstrInsertPoint::materialize() {
  if (isSplit()) {
    // Slice and return the beginning of the new block.
    // If we need to split between the terminators, we theoritically
    // need to know where the first and second set of terminators end
    // to update the successors properly.
    // Now, in pratice, we should have a maximum of 2 branch
    // instructions; one conditional and one unconditional. Therefore
    // we know how to update the successor by looking at the target of
    // the unconditional branch.
    // If we end up splitting at some point, then, we should update
    // the liveness information and such. I.e., we would need to
    // access P here.
    // The machine verifier should actually make sure such cases
    // cannot happen.
    llvm_unreachable("Not yet implemented");
  }
  // Otherwise the insertion point is just the current or next
  // instruction depending on Before. I.e., there is nothing to do
  // here.
}

bool RegBankSelect::InstrInsertPoint::isSplit() const {
  // If the insertion point is after a terminator, we need to split.
  if (!Before)
    return Instr.isTerminator();
  // If we insert before an instruction that is after a terminator,
  // we are still after a terminator.
  return Instr.getPrevNode() && Instr.getPrevNode()->isTerminator();
}

uint64_t RegBankSelect::InstrInsertPoint::frequency(const Pass &P) const {
  // Even if we need to split, because we insert between terminators,
  // this split has actually the same frequency as the instruction.
  const auto *MBFIWrapper =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfoWrapperPass>();
  if (!MBFIWrapper)
    return 1;
  return MBFIWrapper->getMBFI().getBlockFreq(Instr.getParent()).getFrequency();
}

uint64_t RegBankSelect::MBBInsertPoint::frequency(const Pass &P) const {
  const auto *MBFIWrapper =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfoWrapperPass>();
  if (!MBFIWrapper)
    return 1;
  return MBFIWrapper->getMBFI().getBlockFreq(&MBB).getFrequency();
}

void RegBankSelect::EdgeInsertPoint::materialize() {
  // If we end up repairing twice at the same place before materializing the
  // insertion point, we may think we have to split an edge twice.
  // We should have a factory for the insert point such that identical points
  // are the same instance.
  assert(Src.isSuccessor(DstOrSplit) && DstOrSplit->isPredecessor(&Src) &&
         "This point has already been split");
  MachineBasicBlock *NewBB = Src.SplitCriticalEdge(DstOrSplit, P);
  assert(NewBB && "Invalid call to materialize");
  // We reuse the destination block to hold the information of the new block.
  DstOrSplit = NewBB;
}

uint64_t RegBankSelect::EdgeInsertPoint::frequency(const Pass &P) const {
  const auto *MBFIWrapper =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfoWrapperPass>();
  if (!MBFIWrapper)
    return 1;
  const auto *MBFI = &MBFIWrapper->getMBFI();
  if (WasMaterialized)
    return MBFI->getBlockFreq(DstOrSplit).getFrequency();

  auto *MBPIWrapper =
      P.getAnalysisIfAvailable<MachineBranchProbabilityInfoWrapperPass>();
  const MachineBranchProbabilityInfo *MBPI =
      MBPIWrapper ? &MBPIWrapper->getMBPI() : nullptr;
  if (!MBPI)
    return 1;
  // The basic block will be on the edge.
  return (MBFI->getBlockFreq(&Src) * MBPI->getEdgeProbability(&Src, DstOrSplit))
      .getFrequency();
}

bool RegBankSelect::EdgeInsertPoint::canMaterialize() const {
  // If this is not a critical edge, we should not have used this insert
  // point. Indeed, either the successor or the predecessor should
  // have do.
  assert(Src.succ_size() > 1 && DstOrSplit->pred_size() > 1 &&
         "Edge is not critical");
  return Src.canSplitCriticalEdge(DstOrSplit);
}

RegBankSelect::MappingCost::MappingCost(BlockFrequency LocalFreq)
    : LocalFreq(LocalFreq.getFrequency()) {}

bool RegBankSelect::MappingCost::addLocalCost(uint64_t Cost) {
  // Check if this overflows.
  if (LocalCost + Cost < LocalCost) {
    saturate();
    return true;
  }
  LocalCost += Cost;
  return isSaturated();
}

bool RegBankSelect::MappingCost::addNonLocalCost(uint64_t Cost) {
  // Check if this overflows.
  if (NonLocalCost + Cost < NonLocalCost) {
    saturate();
    return true;
  }
  NonLocalCost += Cost;
  return isSaturated();
}

bool RegBankSelect::MappingCost::isSaturated() const {
  return LocalCost == UINT64_MAX - 1 && NonLocalCost == UINT64_MAX &&
         LocalFreq == UINT64_MAX;
}

void RegBankSelect::MappingCost::saturate() {
  *this = ImpossibleCost();
  --LocalCost;
}

RegBankSelect::MappingCost RegBankSelect::MappingCost::ImpossibleCost() {
  return MappingCost(UINT64_MAX, UINT64_MAX, UINT64_MAX);
}

bool RegBankSelect::MappingCost::operator<(const MappingCost &Cost) const {
  // Sort out the easy cases.
  if (*this == Cost)
    return false;
  // If one is impossible to realize the other is cheaper unless it is
  // impossible as well.
  if ((*this == ImpossibleCost()) || (Cost == ImpossibleCost()))
    return (*this == ImpossibleCost()) < (Cost == ImpossibleCost());
  // If one is saturated the other is cheaper, unless it is saturated
  // as well.
  if (isSaturated() || Cost.isSaturated())
    return isSaturated() < Cost.isSaturated();
  // At this point we know both costs hold sensible values.

  // If both values have a different base frequency, there is no much
  // we can do but to scale everything.
  // However, if they have the same base frequency we can avoid making
  // complicated computation.
  uint64_t ThisLocalAdjust;
  uint64_t OtherLocalAdjust;
  if (LLVM_LIKELY(LocalFreq == Cost.LocalFreq)) {

    // At this point, we know the local costs are comparable.
    // Do the case that do not involve potential overflow first.
    if (NonLocalCost == Cost.NonLocalCost)
      // Since the non-local costs do not discriminate on the result,
      // just compare the local costs.
      return LocalCost < Cost.LocalCost;

    // The base costs are comparable so we may only keep the relative
    // value to increase our chances of avoiding overflows.
    ThisLocalAdjust = 0;
    OtherLocalAdjust = 0;
    if (LocalCost < Cost.LocalCost)
      OtherLocalAdjust = Cost.LocalCost - LocalCost;
    else
      ThisLocalAdjust = LocalCost - Cost.LocalCost;
  } else {
    ThisLocalAdjust = LocalCost;
    OtherLocalAdjust = Cost.LocalCost;
  }

  // The non-local costs are comparable, just keep the relative value.
  uint64_t ThisNonLocalAdjust = 0;
  uint64_t OtherNonLocalAdjust = 0;
  if (NonLocalCost < Cost.NonLocalCost)
    OtherNonLocalAdjust = Cost.NonLocalCost - NonLocalCost;
  else
    ThisNonLocalAdjust = NonLocalCost - Cost.NonLocalCost;
  // Scale everything to make them comparable.
  uint64_t ThisScaledCost = ThisLocalAdjust * LocalFreq;
  // Check for overflow on that operation.
  bool ThisOverflows = ThisLocalAdjust && (ThisScaledCost < ThisLocalAdjust ||
                                           ThisScaledCost < LocalFreq);
  uint64_t OtherScaledCost = OtherLocalAdjust * Cost.LocalFreq;
  // Check for overflow on the last operation.
  bool OtherOverflows =
      OtherLocalAdjust &&
      (OtherScaledCost < OtherLocalAdjust || OtherScaledCost < Cost.LocalFreq);
  // Add the non-local costs.
  ThisOverflows |= ThisNonLocalAdjust &&
                   ThisScaledCost + ThisNonLocalAdjust < ThisNonLocalAdjust;
  ThisScaledCost += ThisNonLocalAdjust;
  OtherOverflows |= OtherNonLocalAdjust &&
                    OtherScaledCost + OtherNonLocalAdjust < OtherNonLocalAdjust;
  OtherScaledCost += OtherNonLocalAdjust;
  // If both overflows, we cannot compare without additional
  // precision, e.g., APInt. Just give up on that case.
  if (ThisOverflows && OtherOverflows)
    return false;
  // If one overflows but not the other, we can still compare.
  if (ThisOverflows || OtherOverflows)
    return ThisOverflows < OtherOverflows;
  // Otherwise, just compare the values.
  return ThisScaledCost < OtherScaledCost;
}

bool RegBankSelect::MappingCost::operator==(const MappingCost &Cost) const {
  return LocalCost == Cost.LocalCost && NonLocalCost == Cost.NonLocalCost &&
         LocalFreq == Cost.LocalFreq;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegBankSelect::MappingCost::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

void RegBankSelect::MappingCost::print(raw_ostream &OS) const {
  if (*this == ImpossibleCost()) {
    OS << "impossible";
    return;
  }
  if (isSaturated()) {
    OS << "saturated";
    return;
  }
  OS << LocalFreq << " * " << LocalCost << " + " << NonLocalCost;
}
