//===- CalcSpillWeights.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "calcspillweights"

void VirtRegAuxInfo::calculateSpillWeightsAndHints() {
  LLVM_DEBUG(dbgs() << "********** Compute Spill Weights **********\n"
                    << "********** Function: " << MF.getName() << '\n');

  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(Reg))
      continue;
    calculateSpillWeightAndHint(LIS.getInterval(Reg));
  }
}

// Return the preferred allocation register for reg, given a COPY instruction.
Register VirtRegAuxInfo::copyHint(const MachineInstr *MI, unsigned Reg,
                                  const TargetRegisterInfo &TRI,
                                  const MachineRegisterInfo &MRI) {
  unsigned Sub, HSub;
  Register HReg;
  if (MI->getOperand(0).getReg() == Reg) {
    Sub = MI->getOperand(0).getSubReg();
    HReg = MI->getOperand(1).getReg();
    HSub = MI->getOperand(1).getSubReg();
  } else {
    Sub = MI->getOperand(1).getSubReg();
    HReg = MI->getOperand(0).getReg();
    HSub = MI->getOperand(0).getSubReg();
  }

  if (!HReg)
    return 0;

  if (HReg.isVirtual())
    return Sub == HSub ? HReg : Register();

  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  MCRegister CopiedPReg = HSub ? TRI.getSubReg(HReg, HSub) : HReg.asMCReg();
  if (RC->contains(CopiedPReg))
    return CopiedPReg;

  // Check if reg:sub matches so that a super register could be hinted.
  if (Sub)
    return TRI.getMatchingSuperReg(CopiedPReg, Sub, RC);

  return 0;
}

// Check if all values in LI are rematerializable
bool VirtRegAuxInfo::isRematerializable(const LiveInterval &LI,
                                        const LiveIntervals &LIS,
                                        const VirtRegMap &VRM,
                                        const TargetInstrInfo &TII) {
  Register Reg = LI.reg();
  Register Original = VRM.getOriginal(Reg);
  for (LiveInterval::const_vni_iterator I = LI.vni_begin(), E = LI.vni_end();
       I != E; ++I) {
    const VNInfo *VNI = *I;
    if (VNI->isUnused())
      continue;
    if (VNI->isPHIDef())
      return false;

    MachineInstr *MI = LIS.getInstructionFromIndex(VNI->def);
    assert(MI && "Dead valno in interval");

    // Trace copies introduced by live range splitting.  The inline
    // spiller can rematerialize through these copies, so the spill
    // weight must reflect this.
    while (TII.isFullCopyInstr(*MI)) {
      // The copy destination must match the interval register.
      if (MI->getOperand(0).getReg() != Reg)
        return false;

      // Get the source register.
      Reg = MI->getOperand(1).getReg();

      // If the original (pre-splitting) registers match this
      // copy came from a split.
      if (!Reg.isVirtual() || VRM.getOriginal(Reg) != Original)
        return false;

      // Follow the copy live-in value.
      const LiveInterval &SrcLI = LIS.getInterval(Reg);
      LiveQueryResult SrcQ = SrcLI.Query(VNI->def);
      VNI = SrcQ.valueIn();
      assert(VNI && "Copy from non-existing value");
      if (VNI->isPHIDef())
        return false;
      MI = LIS.getInstructionFromIndex(VNI->def);
      assert(MI && "Dead valno in interval");
    }

    if (!TII.isTriviallyReMaterializable(*MI))
      return false;
  }
  return true;
}

bool VirtRegAuxInfo::isLiveAtStatepointVarArg(LiveInterval &LI) {
  return any_of(VRM.getRegInfo().reg_operands(LI.reg()),
                [](MachineOperand &MO) {
    MachineInstr *MI = MO.getParent();
    if (MI->getOpcode() != TargetOpcode::STATEPOINT)
      return false;
    return StatepointOpers(MI).getVarIdx() <= MO.getOperandNo();
  });
}

void VirtRegAuxInfo::calculateSpillWeightAndHint(LiveInterval &LI) {
  float Weight = weightCalcHelper(LI);
  // Check if unspillable.
  if (Weight < 0)
    return;
  LI.setWeight(Weight);
}

static bool canMemFoldInlineAsm(LiveInterval &LI,
                                const MachineRegisterInfo &MRI) {
  for (const MachineOperand &MO : MRI.reg_operands(LI.reg())) {
    const MachineInstr *MI = MO.getParent();
    if (MI->isInlineAsm() && MI->mayFoldInlineAsmRegOp(MI->getOperandNo(&MO)))
      return true;
  }

  return false;
}

float VirtRegAuxInfo::weightCalcHelper(LiveInterval &LI, SlotIndex *Start,
                                       SlotIndex *End) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock *MBB = nullptr;
  float TotalWeight = 0;
  unsigned NumInstr = 0; // Number of instructions using LI
  SmallPtrSet<MachineInstr *, 8> Visited;

  std::pair<unsigned, Register> TargetHint = MRI.getRegAllocationHint(LI.reg());

  if (LI.isSpillable()) {
    Register Reg = LI.reg();
    Register Original = VRM.getOriginal(Reg);
    const LiveInterval &OrigInt = LIS.getInterval(Original);
    // li comes from a split of OrigInt. If OrigInt was marked
    // as not spillable, make sure the new interval is marked
    // as not spillable as well.
    if (!OrigInt.isSpillable())
      LI.markNotSpillable();
  }

  // Don't recompute spill weight for an unspillable register.
  bool IsSpillable = LI.isSpillable();

  bool IsLocalSplitArtifact = Start && End;

  // Do not update future local split artifacts.
  bool ShouldUpdateLI = !IsLocalSplitArtifact;

  if (IsLocalSplitArtifact) {
    MachineBasicBlock *LocalMBB = LIS.getMBBFromIndex(*End);
    assert(LocalMBB == LIS.getMBBFromIndex(*Start) &&
           "start and end are expected to be in the same basic block");

    // Local split artifact will have 2 additional copy instructions and they
    // will be in the same BB.
    // localLI = COPY other
    // ...
    // other   = COPY localLI
    TotalWeight += LiveIntervals::getSpillWeight(true, false, &MBFI, LocalMBB);
    TotalWeight += LiveIntervals::getSpillWeight(false, true, &MBFI, LocalMBB);

    NumInstr += 2;
  }

  // CopyHint is a sortable hint derived from a COPY instruction.
  struct CopyHint {
    const Register Reg;
    const float Weight;
    CopyHint(Register R, float W) : Reg(R), Weight(W) {}
    bool operator<(const CopyHint &Rhs) const {
      // Always prefer any physreg hint.
      if (Reg.isPhysical() != Rhs.Reg.isPhysical())
        return Reg.isPhysical();
      if (Weight != Rhs.Weight)
        return (Weight > Rhs.Weight);
      return Reg.id() < Rhs.Reg.id(); // Tie-breaker.
    }
  };

  bool IsExiting = false;
  std::set<CopyHint> CopyHints;
  DenseMap<unsigned, float> Hint;
  for (MachineRegisterInfo::reg_instr_nodbg_iterator
           I = MRI.reg_instr_nodbg_begin(LI.reg()),
           E = MRI.reg_instr_nodbg_end();
       I != E;) {
    MachineInstr *MI = &*(I++);

    // For local split artifacts, we are interested only in instructions between
    // the expected start and end of the range.
    SlotIndex SI = LIS.getInstructionIndex(*MI);
    if (IsLocalSplitArtifact && ((SI < *Start) || (SI > *End)))
      continue;

    NumInstr++;
    bool identityCopy = false;
    auto DestSrc = TII.isCopyInstr(*MI);
    if (DestSrc) {
      const MachineOperand *DestRegOp = DestSrc->Destination;
      const MachineOperand *SrcRegOp = DestSrc->Source;
      identityCopy = DestRegOp->getReg() == SrcRegOp->getReg() &&
                     DestRegOp->getSubReg() == SrcRegOp->getSubReg();
    }

    if (identityCopy || MI->isImplicitDef())
      continue;
    if (!Visited.insert(MI).second)
      continue;

    // For terminators that produce values, ask the backend if the register is
    // not spillable.
    if (TII.isUnspillableTerminator(MI) &&
        MI->definesRegister(LI.reg(), /*TRI=*/nullptr)) {
      LI.markNotSpillable();
      return -1.0f;
    }

    // Force Weight onto the stack so that x86 doesn't add hidden precision,
    // similar to HWeight below.
    stack_float_t Weight = 1.0f;
    if (IsSpillable) {
      // Get loop info for mi.
      if (MI->getParent() != MBB) {
        MBB = MI->getParent();
        const MachineLoop *Loop = Loops.getLoopFor(MBB);
        IsExiting = Loop ? Loop->isLoopExiting(MBB) : false;
      }

      // Calculate instr weight.
      bool Reads, Writes;
      std::tie(Reads, Writes) = MI->readsWritesVirtualRegister(LI.reg());
      Weight = LiveIntervals::getSpillWeight(Writes, Reads, &MBFI, *MI);

      // Give extra weight to what looks like a loop induction variable update.
      if (Writes && IsExiting && LIS.isLiveOutOfMBB(LI, MBB))
        Weight *= 3;

      TotalWeight += Weight;
    }

    // Get allocation hints from copies.
    if (!TII.isCopyInstr(*MI))
      continue;
    Register HintReg = copyHint(MI, LI.reg(), TRI, MRI);
    if (!HintReg)
      continue;
    // Force HWeight onto the stack so that x86 doesn't add hidden precision,
    // making the comparison incorrectly pass (i.e., 1 > 1 == true??).
    stack_float_t HWeight = Hint[HintReg] += Weight;
    if (HintReg.isVirtual() || MRI.isAllocatable(HintReg))
      CopyHints.insert(CopyHint(HintReg, HWeight));
  }

  // Pass all the sorted copy hints to mri.
  if (ShouldUpdateLI && CopyHints.size()) {
    // Remove a generic hint if previously added by target.
    if (TargetHint.first == 0 && TargetHint.second)
      MRI.clearSimpleHint(LI.reg());

    SmallSet<Register, 4> HintedRegs;
    for (const auto &Hint : CopyHints) {
      if (!HintedRegs.insert(Hint.Reg).second ||
          (TargetHint.first != 0 && Hint.Reg == TargetHint.second))
        // Don't add the same reg twice or the target-type hint again.
        continue;
      MRI.addRegAllocationHint(LI.reg(), Hint.Reg);
    }

    // Weakly boost the spill weight of hinted registers.
    TotalWeight *= 1.01F;
  }

  // If the live interval was already unspillable, leave it that way.
  if (!IsSpillable)
    return -1.0;

  // Mark li as unspillable if all live ranges are tiny and the interval
  // is not live at any reg mask.  If the interval is live at a reg mask
  // spilling may be required. If li is live as use in statepoint instruction
  // spilling may be required due to if we mark interval with use in statepoint
  // as not spillable we are risky to end up with no register to allocate.
  // At the same time STATEPOINT instruction is perfectly fine to have this
  // operand on stack, so spilling such interval and folding its load from stack
  // into instruction itself makes perfect sense.
  if (ShouldUpdateLI && LI.isZeroLength(LIS.getSlotIndexes()) &&
      !LI.isLiveAtIndexes(LIS.getRegMaskSlots()) &&
      !isLiveAtStatepointVarArg(LI) && !canMemFoldInlineAsm(LI, MRI)) {
    LI.markNotSpillable();
    return -1.0;
  }

  // If all of the definitions of the interval are re-materializable,
  // it is a preferred candidate for spilling.
  // FIXME: this gets much more complicated once we support non-trivial
  // re-materialization.
  if (isRematerializable(LI, LIS, VRM, *MF.getSubtarget().getInstrInfo()))
    TotalWeight *= 0.5F;

  if (IsLocalSplitArtifact)
    return normalize(TotalWeight, Start->distance(*End), NumInstr);
  return normalize(TotalWeight, LI.getSize(), NumInstr);
}
