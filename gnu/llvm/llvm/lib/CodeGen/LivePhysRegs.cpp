//===--- LivePhysRegs.cpp - Live Physical Register Set --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LivePhysRegs utility for tracking liveness of
// physical registers across machine instructions in forward or backward order.
// A more detailed description can be found in the corresponding header file.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;


/// Remove all registers from the set that get clobbered by the register
/// mask.
/// The clobbers set will be the list of live registers clobbered
/// by the regmask.
void LivePhysRegs::removeRegsInMask(const MachineOperand &MO,
    SmallVectorImpl<std::pair<MCPhysReg, const MachineOperand*>> *Clobbers) {
  RegisterSet::iterator LRI = LiveRegs.begin();
  while (LRI != LiveRegs.end()) {
    if (MO.clobbersPhysReg(*LRI)) {
      if (Clobbers)
        Clobbers->push_back(std::make_pair(*LRI, &MO));
      LRI = LiveRegs.erase(LRI);
    } else
      ++LRI;
  }
}

/// Remove defined registers and regmask kills from the set.
void LivePhysRegs::removeDefs(const MachineInstr &MI) {
  for (const MachineOperand &MOP : phys_regs_and_masks(MI)) {
    if (MOP.isRegMask()) {
      removeRegsInMask(MOP);
      continue;
    }

    if (MOP.isDef())
      removeReg(MOP.getReg());
  }
}

/// Add uses to the set.
void LivePhysRegs::addUses(const MachineInstr &MI) {
  for (const MachineOperand &MOP : phys_regs_and_masks(MI)) {
    if (!MOP.isReg() || !MOP.readsReg())
      continue;
    addReg(MOP.getReg());
  }
}

/// Simulates liveness when stepping backwards over an instruction(bundle):
/// Remove Defs, add uses. This is the recommended way of calculating liveness.
void LivePhysRegs::stepBackward(const MachineInstr &MI) {
  // Remove defined registers and regmask kills from the set.
  removeDefs(MI);

  // Add uses to the set.
  addUses(MI);
}

/// Simulates liveness when stepping forward over an instruction(bundle): Remove
/// killed-uses, add defs. This is the not recommended way, because it depends
/// on accurate kill flags. If possible use stepBackward() instead of this
/// function.
void LivePhysRegs::stepForward(const MachineInstr &MI,
    SmallVectorImpl<std::pair<MCPhysReg, const MachineOperand*>> &Clobbers) {
  // Remove killed registers from the set.
  for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
    if (O->isReg()) {
      if (O->isDebug())
        continue;
      Register Reg = O->getReg();
      if (!Reg.isPhysical())
        continue;
      if (O->isDef()) {
        // Note, dead defs are still recorded.  The caller should decide how to
        // handle them.
        Clobbers.push_back(std::make_pair(Reg, &*O));
      } else {
        assert(O->isUse());
        if (O->isKill())
          removeReg(Reg);
      }
    } else if (O->isRegMask()) {
      removeRegsInMask(*O, &Clobbers);
    }
  }

  // Add defs to the set.
  for (auto Reg : Clobbers) {
    // Skip dead defs and registers clobbered by regmasks. They shouldn't
    // be added to the set.
    if (Reg.second->isReg() && Reg.second->isDead())
      continue;
    if (Reg.second->isRegMask() &&
        MachineOperand::clobbersPhysReg(Reg.second->getRegMask(), Reg.first))
      continue;
    addReg(Reg.first);
  }
}

/// Print the currently live registers to OS.
void LivePhysRegs::print(raw_ostream &OS) const {
  OS << "Live Registers:";
  if (!TRI) {
    OS << " (uninitialized)\n";
    return;
  }

  if (empty()) {
    OS << " (empty)\n";
    return;
  }

  for (MCPhysReg R : *this)
    OS << " " << printReg(R, TRI);
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LivePhysRegs::dump() const {
  dbgs() << "  " << *this;
}
#endif

bool LivePhysRegs::available(const MachineRegisterInfo &MRI,
                             MCPhysReg Reg) const {
  if (LiveRegs.count(Reg))
    return false;
  if (MRI.isReserved(Reg))
    return false;
  for (MCRegAliasIterator R(Reg, TRI, false); R.isValid(); ++R) {
    if (LiveRegs.count(*R))
      return false;
  }
  return true;
}

/// Add live-in registers of basic block \p MBB to \p LiveRegs.
void LivePhysRegs::addBlockLiveIns(const MachineBasicBlock &MBB) {
  for (const auto &LI : MBB.liveins()) {
    MCPhysReg Reg = LI.PhysReg;
    LaneBitmask Mask = LI.LaneMask;
    MCSubRegIndexIterator S(Reg, TRI);
    assert(Mask.any() && "Invalid livein mask");
    if (Mask.all() || !S.isValid()) {
      addReg(Reg);
      continue;
    }
    for (; S.isValid(); ++S) {
      unsigned SI = S.getSubRegIndex();
      if ((Mask & TRI->getSubRegIndexLaneMask(SI)).any())
        addReg(S.getSubReg());
    }
  }
}

/// Adds all callee saved registers to \p LiveRegs.
static void addCalleeSavedRegs(LivePhysRegs &LiveRegs,
                               const MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR; ++CSR)
    LiveRegs.addReg(*CSR);
}

void LivePhysRegs::addPristines(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.isCalleeSavedInfoValid())
    return;
  /// This function will usually be called on an empty object, handle this
  /// as a special case.
  if (empty()) {
    /// Add all callee saved regs, then remove the ones that are saved and
    /// restored.
    addCalleeSavedRegs(*this, MF);
    /// Remove the ones that are not saved/restored; they are pristine.
    for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
      removeReg(Info.getReg());
    return;
  }
  /// If a callee-saved register that is not pristine is already present
  /// in the set, we should make sure that it stays in it. Precompute the
  /// set of pristine registers in a separate object.
  /// Add all callee saved regs, then remove the ones that are saved+restored.
  LivePhysRegs Pristine(*TRI);
  addCalleeSavedRegs(Pristine, MF);
  /// Remove the ones that are not saved/restored; they are pristine.
  for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
    Pristine.removeReg(Info.getReg());
  for (MCPhysReg R : Pristine)
    addReg(R);
}

void LivePhysRegs::addLiveOutsNoPristines(const MachineBasicBlock &MBB) {
  // To get the live-outs we simply merge the live-ins of all successors.
  for (const MachineBasicBlock *Succ : MBB.successors())
    addBlockLiveIns(*Succ);
  if (MBB.isReturnBlock()) {
    // Return blocks are a special case because we currently don't mark up
    // return instructions completely: specifically, there is no explicit
    // use for callee-saved registers. So we add all callee saved registers
    // that are saved and restored (somewhere). This does not include
    // callee saved registers that are unused and hence not saved and
    // restored; they are called pristine.
    // FIXME: PEI should add explicit markings to return instructions
    // instead of implicitly handling them here.
    const MachineFunction &MF = *MBB.getParent();
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (MFI.isCalleeSavedInfoValid()) {
      for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
        if (Info.isRestored())
          addReg(Info.getReg());
    }
  }
}

void LivePhysRegs::addLiveOuts(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  addPristines(MF);
  addLiveOutsNoPristines(MBB);
}

void LivePhysRegs::addLiveIns(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  addPristines(MF);
  addBlockLiveIns(MBB);
}

void LivePhysRegs::addLiveInsNoPristines(const MachineBasicBlock &MBB) {
  addBlockLiveIns(MBB);
}

void llvm::computeLiveIns(LivePhysRegs &LiveRegs,
                          const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  LiveRegs.init(TRI);
  LiveRegs.addLiveOutsNoPristines(MBB);
  for (const MachineInstr &MI : llvm::reverse(MBB))
    LiveRegs.stepBackward(MI);
}

void llvm::addLiveIns(MachineBasicBlock &MBB, const LivePhysRegs &LiveRegs) {
  assert(MBB.livein_empty() && "Expected empty live-in list");
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  for (MCPhysReg Reg : LiveRegs) {
    if (MRI.isReserved(Reg))
      continue;
    // Skip the register if we are about to add one of its super registers.
    if (any_of(TRI.superregs(Reg), [&](MCPhysReg SReg) {
          return LiveRegs.contains(SReg) && !MRI.isReserved(SReg);
        }))
      continue;
    MBB.addLiveIn(Reg);
  }
}

void llvm::recomputeLivenessFlags(MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // We walk through the block backwards and start with the live outs.
  LivePhysRegs LiveRegs;
  LiveRegs.init(TRI);
  LiveRegs.addLiveOutsNoPristines(MBB);

  for (MachineInstr &MI : llvm::reverse(MBB)) {
    // Recompute dead flags.
    for (MIBundleOperands MO(MI); MO.isValid(); ++MO) {
      if (!MO->isReg() || !MO->isDef() || MO->isDebug())
        continue;

      Register Reg = MO->getReg();
      if (Reg == 0)
        continue;
      assert(Reg.isPhysical());

      bool IsNotLive = LiveRegs.available(MRI, Reg);

      // Special-case return instructions for cases when a return is not
      // the last instruction in the block.
      if (MI.isReturn() && MFI.isCalleeSavedInfoValid()) {
        for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo()) {
          if (Info.getReg() == Reg) {
            IsNotLive = !Info.isRestored();
            break;
          }
        }
      }

      MO->setIsDead(IsNotLive);
    }

    // Step backward over defs.
    LiveRegs.removeDefs(MI);

    // Recompute kill flags.
    for (MIBundleOperands MO(MI); MO.isValid(); ++MO) {
      if (!MO->isReg() || !MO->readsReg() || MO->isDebug())
        continue;

      Register Reg = MO->getReg();
      if (Reg == 0)
        continue;
      assert(Reg.isPhysical());

      bool IsNotLive = LiveRegs.available(MRI, Reg);
      MO->setIsKill(IsNotLive);
    }

    // Complete the stepbackward.
    LiveRegs.addUses(MI);
  }
}

void llvm::computeAndAddLiveIns(LivePhysRegs &LiveRegs,
                                MachineBasicBlock &MBB) {
  computeLiveIns(LiveRegs, MBB);
  addLiveIns(MBB, LiveRegs);
}
