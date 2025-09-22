//===-- LiveRangeEdit.cpp - Basic tools for editing a register live range -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LiveRangeEdit class represents changes done to a virtual register when it
// is spilled or split.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumDCEDeleted,        "Number of instructions deleted by DCE");
STATISTIC(NumDCEFoldedLoads,    "Number of single use loads folded after DCE");
STATISTIC(NumFracRanges,        "Number of live ranges fractured by DCE");
STATISTIC(NumReMaterialization, "Number of instructions rematerialized");

void LiveRangeEdit::Delegate::anchor() { }

LiveInterval &LiveRangeEdit::createEmptyIntervalFrom(Register OldReg,
                                                     bool createSubRanges) {
  Register VReg = MRI.cloneVirtualRegister(OldReg);
  if (VRM)
    VRM->setIsSplitFromReg(VReg, VRM->getOriginal(OldReg));

  LiveInterval &LI = LIS.createEmptyInterval(VReg);
  if (Parent && !Parent->isSpillable())
    LI.markNotSpillable();
  if (createSubRanges) {
    // Create empty subranges if the OldReg's interval has them. Do not create
    // the main range here---it will be constructed later after the subranges
    // have been finalized.
    LiveInterval &OldLI = LIS.getInterval(OldReg);
    VNInfo::Allocator &Alloc = LIS.getVNInfoAllocator();
    for (LiveInterval::SubRange &S : OldLI.subranges())
      LI.createSubRange(Alloc, S.LaneMask);
  }
  return LI;
}

Register LiveRangeEdit::createFrom(Register OldReg) {
  Register VReg = MRI.cloneVirtualRegister(OldReg);
  if (VRM) {
    VRM->setIsSplitFromReg(VReg, VRM->getOriginal(OldReg));
  }
  // FIXME: Getting the interval here actually computes it.
  // In theory, this may not be what we want, but in practice
  // the createEmptyIntervalFrom API is used when this is not
  // the case. Generally speaking we just want to annotate the
  // LiveInterval when it gets created but we cannot do that at
  // the moment.
  if (Parent && !Parent->isSpillable())
    LIS.getInterval(VReg).markNotSpillable();
  return VReg;
}

bool LiveRangeEdit::checkRematerializable(VNInfo *VNI,
                                          const MachineInstr *DefMI) {
  assert(DefMI && "Missing instruction");
  ScannedRemattable = true;
  if (!TII.isTriviallyReMaterializable(*DefMI))
    return false;
  Remattable.insert(VNI);
  return true;
}

void LiveRangeEdit::scanRemattable() {
  for (VNInfo *VNI : getParent().valnos) {
    if (VNI->isUnused())
      continue;
    Register Original = VRM->getOriginal(getReg());
    LiveInterval &OrigLI = LIS.getInterval(Original);
    VNInfo *OrigVNI = OrigLI.getVNInfoAt(VNI->def);
    if (!OrigVNI)
      continue;
    MachineInstr *DefMI = LIS.getInstructionFromIndex(OrigVNI->def);
    if (!DefMI)
      continue;
    checkRematerializable(OrigVNI, DefMI);
  }
  ScannedRemattable = true;
}

bool LiveRangeEdit::anyRematerializable() {
  if (!ScannedRemattable)
    scanRemattable();
  return !Remattable.empty();
}

/// allUsesAvailableAt - Return true if all registers used by OrigMI at
/// OrigIdx are also available with the same value at UseIdx.
bool LiveRangeEdit::allUsesAvailableAt(const MachineInstr *OrigMI,
                                       SlotIndex OrigIdx,
                                       SlotIndex UseIdx) const {
  OrigIdx = OrigIdx.getRegSlot(true);
  UseIdx = std::max(UseIdx, UseIdx.getRegSlot(true));
  for (const MachineOperand &MO : OrigMI->operands()) {
    if (!MO.isReg() || !MO.getReg() || !MO.readsReg())
      continue;

    // We can't remat physreg uses, unless it is a constant or target wants
    // to ignore this use.
    if (MO.getReg().isPhysical()) {
      if (MRI.isConstantPhysReg(MO.getReg()) || TII.isIgnorableUse(MO))
        continue;
      return false;
    }

    LiveInterval &li = LIS.getInterval(MO.getReg());
    const VNInfo *OVNI = li.getVNInfoAt(OrigIdx);
    if (!OVNI)
      continue;

    // Don't allow rematerialization immediately after the original def.
    // It would be incorrect if OrigMI redefines the register.
    // See PR14098.
    if (SlotIndex::isSameInstr(OrigIdx, UseIdx))
      return false;

    if (OVNI != li.getVNInfoAt(UseIdx))
      return false;

    // Check that subrange is live at UseIdx.
    if (li.hasSubRanges()) {
      const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
      unsigned SubReg = MO.getSubReg();
      LaneBitmask LM = SubReg ? TRI->getSubRegIndexLaneMask(SubReg)
                              : MRI.getMaxLaneMaskForVReg(MO.getReg());
      for (LiveInterval::SubRange &SR : li.subranges()) {
        if ((SR.LaneMask & LM).none())
          continue;
        if (!SR.liveAt(UseIdx))
          return false;
        // Early exit if all used lanes are checked. No need to continue.
        LM &= ~SR.LaneMask;
        if (LM.none())
          break;
      }
    }
  }
  return true;
}

bool LiveRangeEdit::canRematerializeAt(Remat &RM, VNInfo *OrigVNI,
                                       SlotIndex UseIdx, bool cheapAsAMove) {
  assert(ScannedRemattable && "Call anyRematerializable first");

  // Use scanRemattable info.
  if (!Remattable.count(OrigVNI))
    return false;

  // No defining instruction provided.
  SlotIndex DefIdx;
  assert(RM.OrigMI && "No defining instruction for remattable value");
  DefIdx = LIS.getInstructionIndex(*RM.OrigMI);

  // If only cheap remats were requested, bail out early.
  if (cheapAsAMove && !TII.isAsCheapAsAMove(*RM.OrigMI))
    return false;

  // Verify that all used registers are available with the same values.
  if (!allUsesAvailableAt(RM.OrigMI, DefIdx, UseIdx))
    return false;

  return true;
}

SlotIndex LiveRangeEdit::rematerializeAt(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, const Remat &RM,
                                         const TargetRegisterInfo &tri,
                                         bool Late, unsigned SubIdx,
                                         MachineInstr *ReplaceIndexMI) {
  assert(RM.OrigMI && "Invalid remat");
  TII.reMaterialize(MBB, MI, DestReg, SubIdx, *RM.OrigMI, tri);
  // DestReg of the cloned instruction cannot be Dead. Set isDead of DestReg
  // to false anyway in case the isDead flag of RM.OrigMI's dest register
  // is true.
  (*--MI).clearRegisterDeads(DestReg);
  Rematted.insert(RM.ParentVNI);
  ++NumReMaterialization;

  if (ReplaceIndexMI)
    return LIS.ReplaceMachineInstrInMaps(*ReplaceIndexMI, *MI).getRegSlot();
  return LIS.getSlotIndexes()->insertMachineInstrInMaps(*MI, Late).getRegSlot();
}

void LiveRangeEdit::eraseVirtReg(Register Reg) {
  if (TheDelegate && TheDelegate->LRE_CanEraseVirtReg(Reg))
    LIS.removeInterval(Reg);
}

bool LiveRangeEdit::foldAsLoad(LiveInterval *LI,
                               SmallVectorImpl<MachineInstr*> &Dead) {
  MachineInstr *DefMI = nullptr, *UseMI = nullptr;

  // Check that there is a single def and a single use.
  for (MachineOperand &MO : MRI.reg_nodbg_operands(LI->reg())) {
    MachineInstr *MI = MO.getParent();
    if (MO.isDef()) {
      if (DefMI && DefMI != MI)
        return false;
      if (!MI->canFoldAsLoad())
        return false;
      DefMI = MI;
    } else if (!MO.isUndef()) {
      if (UseMI && UseMI != MI)
        return false;
      // FIXME: Targets don't know how to fold subreg uses.
      if (MO.getSubReg())
        return false;
      UseMI = MI;
    }
  }
  if (!DefMI || !UseMI)
    return false;

  // Since we're moving the DefMI load, make sure we're not extending any live
  // ranges.
  if (!allUsesAvailableAt(DefMI, LIS.getInstructionIndex(*DefMI),
                          LIS.getInstructionIndex(*UseMI)))
    return false;

  // We also need to make sure it is safe to move the load.
  // Assume there are stores between DefMI and UseMI.
  bool SawStore = true;
  if (!DefMI->isSafeToMove(nullptr, SawStore))
    return false;

  LLVM_DEBUG(dbgs() << "Try to fold single def: " << *DefMI
                    << "       into single use: " << *UseMI);

  SmallVector<unsigned, 8> Ops;
  if (UseMI->readsWritesVirtualRegister(LI->reg(), &Ops).second)
    return false;

  MachineInstr *FoldMI = TII.foldMemoryOperand(*UseMI, Ops, *DefMI, &LIS);
  if (!FoldMI)
    return false;
  LLVM_DEBUG(dbgs() << "                folded: " << *FoldMI);
  LIS.ReplaceMachineInstrInMaps(*UseMI, *FoldMI);
  // Update the call site info.
  if (UseMI->shouldUpdateCallSiteInfo())
    UseMI->getMF()->moveCallSiteInfo(UseMI, FoldMI);
  UseMI->eraseFromParent();
  DefMI->addRegisterDead(LI->reg(), nullptr);
  Dead.push_back(DefMI);
  ++NumDCEFoldedLoads;
  return true;
}

bool LiveRangeEdit::useIsKill(const LiveInterval &LI,
                              const MachineOperand &MO) const {
  const MachineInstr &MI = *MO.getParent();
  SlotIndex Idx = LIS.getInstructionIndex(MI).getRegSlot();
  if (LI.Query(Idx).isKill())
    return true;
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  unsigned SubReg = MO.getSubReg();
  LaneBitmask LaneMask = TRI.getSubRegIndexLaneMask(SubReg);
  for (const LiveInterval::SubRange &S : LI.subranges()) {
    if ((S.LaneMask & LaneMask).any() && S.Query(Idx).isKill())
      return true;
  }
  return false;
}

/// Find all live intervals that need to shrink, then remove the instruction.
void LiveRangeEdit::eliminateDeadDef(MachineInstr *MI, ToShrinkSet &ToShrink) {
  assert(MI->allDefsAreDead() && "Def isn't really dead");
  SlotIndex Idx = LIS.getInstructionIndex(*MI).getRegSlot();

  // Never delete a bundled instruction.
  if (MI->isBundled()) {
    // TODO: Handle deleting copy bundles
    LLVM_DEBUG(dbgs() << "Won't delete dead bundled inst: " << Idx << '\t'
                      << *MI);
    return;
  }

  // Never delete inline asm.
  if (MI->isInlineAsm()) {
    LLVM_DEBUG(dbgs() << "Won't delete: " << Idx << '\t' << *MI);
    return;
  }

  // Use the same criteria as DeadMachineInstructionElim.
  bool SawStore = false;
  if (!MI->isSafeToMove(nullptr, SawStore)) {
    LLVM_DEBUG(dbgs() << "Can't delete: " << Idx << '\t' << *MI);
    return;
  }

  LLVM_DEBUG(dbgs() << "Deleting dead def " << Idx << '\t' << *MI);

  // Collect virtual registers to be erased after MI is gone.
  SmallVector<Register, 8> RegsToErase;
  bool ReadsPhysRegs = false;
  bool isOrigDef = false;
  Register Dest;
  unsigned DestSubReg;
  // Only optimize rematerialize case when the instruction has one def, since
  // otherwise we could leave some dead defs in the code.  This case is
  // extremely rare.
  if (VRM && MI->getOperand(0).isReg() && MI->getOperand(0).isDef() &&
      MI->getDesc().getNumDefs() == 1) {
    Dest = MI->getOperand(0).getReg();
    DestSubReg = MI->getOperand(0).getSubReg();
    Register Original = VRM->getOriginal(Dest);
    LiveInterval &OrigLI = LIS.getInterval(Original);
    VNInfo *OrigVNI = OrigLI.getVNInfoAt(Idx);
    // The original live-range may have been shrunk to
    // an empty live-range. It happens when it is dead, but
    // we still keep it around to be able to rematerialize
    // other values that depend on it.
    if (OrigVNI)
      isOrigDef = SlotIndex::isSameInstr(OrigVNI->def, Idx);
  }

  bool HasLiveVRegUses = false;

  // Check for live intervals that may shrink
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg.isVirtual()) {
      // Check if MI reads any unreserved physregs.
      if (Reg && MO.readsReg() && !MRI.isReserved(Reg))
        ReadsPhysRegs = true;
      else if (MO.isDef())
        LIS.removePhysRegDefAt(Reg.asMCReg(), Idx);
      continue;
    }
    LiveInterval &LI = LIS.getInterval(Reg);

    // Shrink read registers, unless it is likely to be expensive and
    // unlikely to change anything. We typically don't want to shrink the
    // PIC base register that has lots of uses everywhere.
    // Always shrink COPY uses that probably come from live range splitting.
    if ((MI->readsVirtualRegister(Reg) &&
         (MO.isDef() || TII.isCopyInstr(*MI))) ||
        (MO.readsReg() && (MRI.hasOneNonDBGUse(Reg) || useIsKill(LI, MO))))
      ToShrink.insert(&LI);
    else if (MO.readsReg())
      HasLiveVRegUses = true;

    // Remove defined value.
    if (MO.isDef()) {
      if (TheDelegate && LI.getVNInfoAt(Idx) != nullptr)
        TheDelegate->LRE_WillShrinkVirtReg(LI.reg());
      LIS.removeVRegDefAt(LI, Idx);
      if (LI.empty())
        RegsToErase.push_back(Reg);
    }
  }

  // Currently, we don't support DCE of physreg live ranges. If MI reads
  // any unreserved physregs, don't erase the instruction, but turn it into
  // a KILL instead. This way, the physreg live ranges don't end up
  // dangling.
  // FIXME: It would be better to have something like shrinkToUses() for
  // physregs. That could potentially enable more DCE and it would free up
  // the physreg. It would not happen often, though.
  if (ReadsPhysRegs) {
    MI->setDesc(TII.get(TargetOpcode::KILL));
    // Remove all operands that aren't physregs.
    for (unsigned i = MI->getNumOperands(); i; --i) {
      const MachineOperand &MO = MI->getOperand(i-1);
      if (MO.isReg() && MO.getReg().isPhysical())
        continue;
      MI->removeOperand(i-1);
    }
    LLVM_DEBUG(dbgs() << "Converted physregs to:\t" << *MI);
  } else {
    // If the dest of MI is an original reg and MI is reMaterializable,
    // don't delete the inst. Replace the dest with a new reg, and keep
    // the inst for remat of other siblings. The inst is saved in
    // LiveRangeEdit::DeadRemats and will be deleted after all the
    // allocations of the func are done.
    // However, immediately delete instructions which have unshrunk virtual
    // register uses. That may provoke RA to split an interval at the KILL
    // and later result in an invalid live segment end.
    if (isOrigDef && DeadRemats && !HasLiveVRegUses &&
        TII.isTriviallyReMaterializable(*MI)) {
      LiveInterval &NewLI = createEmptyIntervalFrom(Dest, false);
      VNInfo::Allocator &Alloc = LIS.getVNInfoAllocator();
      VNInfo *VNI = NewLI.getNextValue(Idx, Alloc);
      NewLI.addSegment(LiveInterval::Segment(Idx, Idx.getDeadSlot(), VNI));

      if (DestSubReg) {
        const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
        auto *SR = NewLI.createSubRange(
            Alloc, TRI->getSubRegIndexLaneMask(DestSubReg));
        SR->addSegment(LiveInterval::Segment(Idx, Idx.getDeadSlot(),
                                             SR->getNextValue(Idx, Alloc)));
      }

      pop_back();
      DeadRemats->insert(MI);
      const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
      MI->substituteRegister(Dest, NewLI.reg(), 0, TRI);
      assert(MI->registerDefIsDead(NewLI.reg(), &TRI));
    } else {
      if (TheDelegate)
        TheDelegate->LRE_WillEraseInstruction(MI);
      LIS.RemoveMachineInstrFromMaps(*MI);
      MI->eraseFromParent();
      ++NumDCEDeleted;
    }
  }

  // Erase any virtregs that are now empty and unused. There may be <undef>
  // uses around. Keep the empty live range in that case.
  for (Register Reg : RegsToErase) {
    if (LIS.hasInterval(Reg) && MRI.reg_nodbg_empty(Reg)) {
      ToShrink.remove(&LIS.getInterval(Reg));
      eraseVirtReg(Reg);
    }
  }
}

void LiveRangeEdit::eliminateDeadDefs(SmallVectorImpl<MachineInstr *> &Dead,
                                      ArrayRef<Register> RegsBeingSpilled) {
  ToShrinkSet ToShrink;

  for (;;) {
    // Erase all dead defs.
    while (!Dead.empty())
      eliminateDeadDef(Dead.pop_back_val(), ToShrink);

    if (ToShrink.empty())
      break;

    // Shrink just one live interval. Then delete new dead defs.
    LiveInterval *LI = ToShrink.pop_back_val();
    if (foldAsLoad(LI, Dead))
      continue;
    Register VReg = LI->reg();
    if (TheDelegate)
      TheDelegate->LRE_WillShrinkVirtReg(VReg);
    if (!LIS.shrinkToUses(LI, &Dead))
      continue;

    // Don't create new intervals for a register being spilled.
    // The new intervals would have to be spilled anyway so its not worth it.
    // Also they currently aren't spilled so creating them and not spilling
    // them results in incorrect code.
    if (llvm::is_contained(RegsBeingSpilled, VReg))
      continue;

    // LI may have been separated, create new intervals.
    LI->RenumberValues();
    SmallVector<LiveInterval*, 8> SplitLIs;
    LIS.splitSeparateComponents(*LI, SplitLIs);
    if (!SplitLIs.empty())
      ++NumFracRanges;

    Register Original = VRM ? VRM->getOriginal(VReg) : Register();
    for (const LiveInterval *SplitLI : SplitLIs) {
      // If LI is an original interval that hasn't been split yet, make the new
      // intervals their own originals instead of referring to LI. The original
      // interval must contain all the split products, and LI doesn't.
      if (Original != VReg && Original != 0)
        VRM->setIsSplitFromReg(SplitLI->reg(), Original);
      if (TheDelegate)
        TheDelegate->LRE_DidCloneVirtReg(SplitLI->reg(), VReg);
    }
  }
}

// Keep track of new virtual registers created via
// MachineRegisterInfo::createVirtualRegister.
void
LiveRangeEdit::MRI_NoteNewVirtualRegister(Register VReg) {
  if (VRM)
    VRM->grow();

  NewRegs.push_back(VReg);
}

void LiveRangeEdit::calculateRegClassAndHint(MachineFunction &MF,
                                             VirtRegAuxInfo &VRAI) {
  for (unsigned I = 0, Size = size(); I < Size; ++I) {
    LiveInterval &LI = LIS.getInterval(get(I));
    if (MRI.recomputeRegClass(LI.reg()))
      LLVM_DEBUG({
        const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
        dbgs() << "Inflated " << printReg(LI.reg()) << " to "
               << TRI->getRegClassName(MRI.getRegClass(LI.reg())) << '\n';
      });
    VRAI.calculateSpillWeightAndHint(LI);
  }
}
